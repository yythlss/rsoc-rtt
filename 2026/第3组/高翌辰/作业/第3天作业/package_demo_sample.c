/*
 * 程序清单：快递分拣中心
 *
 * 两个发件线程向容量为 5 的消息队列发送快递，分拣线程按区域统计。
 */
#include <rtthread.h>

#define PACKAGE_QUEUE_CAPACITY  5
#define SENDER_COUNT            2
#define PACKAGE_PER_SENDER      5
#define SENDER_THREAD_PRIORITY  20
#define SORTER_THREAD_PRIORITY  19
#define THREAD_TIMESLICE        5
#define PACKAGE_POOL_SIZE       (PACKAGE_QUEUE_CAPACITY * \
                                (RT_ALIGN(sizeof(struct package_msg), RT_ALIGN_SIZE) + sizeof(void *)))

#define REGION_EAST             1
#define REGION_SOUTH            2
#define REGION_NORTH            3

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

struct package_sender
{
    const char *thread_name;
    rt_uint32_t first_id;
    rt_uint8_t regions[PACKAGE_PER_SENDER];
    rt_uint16_t weights[PACKAGE_PER_SENDER];
    struct rt_thread thread;
    char stack[1024];
};

static struct rt_messagequeue package_mq;
#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static rt_uint8_t package_pool[PACKAGE_POOL_SIZE];
static rt_uint32_t send_failed_count;
static rt_uint8_t senders_finished;
static rt_bool_t package_demo_running;
static struct rt_thread sorter_thread;
static char sorter_stack[1024];
static struct package_sender senders[SENDER_COUNT] =
{
    {"senderA", 1001, {REGION_EAST, REGION_SOUTH, REGION_NORTH, REGION_EAST, REGION_SOUTH},
     {1200, 850, 960, 1100, 900}},
    {"senderB", 2001, {REGION_NORTH, REGION_EAST, REGION_SOUTH, REGION_NORTH, REGION_EAST},
     {1300, 750, 1050, 1100, 1040}}
};

static const char *region_name(rt_uint8_t region)
{
    if (region == REGION_EAST)
    {
        return "east";
    }
    if (region == REGION_SOUTH)
    {
        return "south";
    }
    return "north";
}

static void package_demo_cleanup(void)
{
    rt_mq_detach(&package_mq);
    package_demo_running = RT_FALSE;
}

static void record_send_failure(void)
{
    rt_enter_critical();
    send_failed_count++;
    rt_exit_critical();
}

static void send_message_retry(const struct package_msg *message)
{
    while (rt_mq_send(&package_mq, message, sizeof(*message)) != RT_EOK)
    {
        /* 队列已满时保留失败次数，并稍后重试同一条快递。 */
        record_send_failure();
        rt_thread_mdelay(5);
    }
}

static void sender_entry(void *parameter)
{
    struct package_sender *sender = parameter;
    struct package_msg message;
    rt_uint8_t i;
    rt_bool_t is_last_sender = RT_FALSE;

    for (i = 0; i < PACKAGE_PER_SENDER; i++)
    {
        message.id = sender->first_id + i;
        message.region = sender->regions[i];
        message.weight = sender->weights[i];
        send_message_retry(&message);
        rt_thread_mdelay(5);
    }

    rt_enter_critical();
    if (++senders_finished == SENDER_COUNT)
    {
        is_last_sender = RT_TRUE;
    }
    rt_exit_critical();

    if (is_last_sender)
    {
        /* 结束消息排在全部快递消息之后，分拣线程收到它即可退出。 */
        message.id = 0;
        message.region = 0;
        message.weight = 0;
        send_message_retry(&message);
    }
}

static void sorter_entry(void *parameter)
{
    struct package_msg message;
    rt_uint32_t total_packages = 0;
    rt_uint32_t region_count[4] = {0};
    rt_uint32_t total_weight = 0;
    rt_err_t result;

    while (1)
    {
        result = rt_mq_recv(&package_mq, &message, sizeof(message), RT_WAITING_FOREVER);
        if (result != RT_EOK)
        {
            rt_kprintf("receive package failed: %d\n", result);
            package_demo_cleanup();
            return;
        }
        if (message.id == 0)
        {
            break;
        }

        total_packages++;
        region_count[message.region]++;
        total_weight += message.weight;
        rt_kprintf("package %d -> %s, weight: %d g\n",
                   message.id, region_name(message.region), message.weight);

        /* 分拣耗时，便于验证队列满时发送线程会记录失败并重试。 */
        rt_thread_mdelay(20);
    }

    rt_kprintf("total packages: %d\n", total_packages);
    rt_kprintf("east: %d, south: %d, north: %d\n",
               region_count[REGION_EAST], region_count[REGION_SOUTH], region_count[REGION_NORTH]);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", send_failed_count);
    package_demo_cleanup();
}

int package_demo(void)
{
    rt_uint8_t i;
    rt_err_t result;

    if (package_demo_running)
    {
        rt_kprintf("package demo is already running.\n");
        return -RT_EBUSY;
    }

    result = rt_mq_init(&package_mq,
                        "package",
                        package_pool,
                        sizeof(struct package_msg),
                        sizeof(package_pool),
                        RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        rt_kprintf("create package queue failed: %d\n", result);
        return result;
    }

    package_demo_running = RT_TRUE;
    send_failed_count = 0;
    senders_finished = 0;

    result = rt_thread_init(&sorter_thread,
                            "sorter",
                            sorter_entry,
                            RT_NULL,
                            sorter_stack,
                            sizeof(sorter_stack),
                            SORTER_THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_kprintf("create sorter thread failed: %d\n", result);
        package_demo_cleanup();
        return result;
    }

    for (i = 0; i < SENDER_COUNT; i++)
    {
        result = rt_thread_init(&senders[i].thread,
                                senders[i].thread_name,
                                sender_entry,
                                &senders[i],
                                senders[i].stack,
                                sizeof(senders[i].stack),
                                SENDER_THREAD_PRIORITY,
                                THREAD_TIMESLICE);
        if (result != RT_EOK)
        {
            rt_kprintf("create %s thread failed: %d\n", senders[i].thread_name, result);
            while (i > 0)
            {
                rt_thread_detach(&senders[--i].thread);
            }
            rt_thread_detach(&sorter_thread);
            package_demo_cleanup();
            return result;
        }
    }

    result = rt_thread_startup(&sorter_thread);
    if (result != RT_EOK)
    {
        rt_kprintf("start sorter thread failed: %d\n", result);
        for (i = 0; i < SENDER_COUNT; i++)
        {
            rt_thread_detach(&senders[i].thread);
        }
        rt_thread_detach(&sorter_thread);
        package_demo_cleanup();
        return result;
    }

    for (i = 0; i < SENDER_COUNT; i++)
    {
        result = rt_thread_startup(&senders[i].thread);
        if (result != RT_EOK)
        {
            rt_kprintf("start %s thread failed: %d\n", senders[i].thread_name, result);
            return result;
        }
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, package message queue demo);
