/* 练习四：快递分拣中心；星火一号 STM32F407ZG。
 * 1. 消息队列容量为 5 条消息。
 * 2. 两个发件线程各产生 5 条快递消息，快递编号不得重复。
 * 3. 分拣线程接收消息，按 region 分类并统计数量和总重量。
 * 4. 队列满时非阻塞发送失败并计数，不崩溃。
 * 5. 两个发送线程完成后发送一条结束消息。
 * 消息队列容量为 5，两个发件线程各产生 5 条快递，分拣线程统计数量和重量。 */
#include <rtthread.h>
#define QUEUE_SIZE          5
#define SENDER_COUNT        2
#define PACKAGES_PER_SENDER 5
#define THREAD_STACK        1024
#define THREAD_PRIORITY     25
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};
static rt_mq_t package_mq;
static rt_mutex_t stats_mutex;
static rt_sem_t done_sem;
static rt_uint32_t send_failed, senders_done;
static rt_uint8_t running;
static const char *region_name(rt_uint8_t region)
{
    return region == 1U ? "east" : (region == 2U ? "south" : "north");
}
static rt_bool_t start_thread(const char *name, void (*entry)(void *), void *param,
                              rt_uint8_t priority, rt_uint32_t *started)
{
    rt_thread_t tid = rt_thread_create(name, entry, param, THREAD_STACK, priority, 5);
    if (tid == RT_NULL)
        return RT_FALSE;
    if (rt_thread_startup(tid) != RT_EOK)
    {
        rt_thread_delete(tid);
        return RT_FALSE;
    }
    (*started)++;
    return RT_TRUE;
}
static void sender_entry(void *parameter)
{
    rt_uint32_t sender_id = (rt_uint32_t)parameter;
    rt_uint32_t index;
    for (index = 0; index < PACKAGES_PER_SENDER; index++)
    {
        struct package_msg msg;
        rt_bool_t sent = RT_FALSE;

        msg.id = (sender_id + 1U) * 1000U + index + 1U;
        msg.region = (rt_uint8_t)((index + sender_id) % 3U + 1U);
        msg.weight = (rt_uint16_t)(800U + sender_id * 250U + index * 120U);

        while (!sent)
        {
            rt_err_t result = rt_mq_send(package_mq, &msg, sizeof(msg));
            if (result == RT_EOK)
            {
                sent = RT_TRUE;
            }
            else if (result == -RT_EFULL)
            {
                rt_mutex_take(stats_mutex, RT_WAITING_FOREVER);
                send_failed++;
                rt_mutex_release(stats_mutex);
                rt_thread_mdelay(50);
            }
            else
            {
                rt_kprintf("sender %u send failed: %d\n", sender_id + 1U, result);
                break;
            }
        }
        if (!sent)
            break;
    }

    rt_mutex_take(stats_mutex, RT_WAITING_FOREVER);
    senders_done++;
    if (senders_done == SENDER_COUNT)
    {
        struct package_msg end_msg;
        rt_memset(&end_msg, 0, sizeof(end_msg));
        if (rt_mq_send_wait(package_mq, &end_msg, sizeof(end_msg),
                            RT_WAITING_FOREVER) != RT_EOK)
            rt_kprintf("send package end message failed\n");
    }
    rt_mutex_release(stats_mutex);
    rt_sem_release(done_sem);
}

static void sorter_entry(void *parameter)
{
    rt_uint32_t total = 0, east = 0, south = 0, north = 0, total_weight = 0;
    (void)parameter;

    while (1)
    {
        struct package_msg msg;
        rt_err_t result = rt_mq_recv(package_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (result != RT_EOK)
        {
            rt_kprintf("package sorter recv failed: %d\n", result);
            break;
        }
        if (msg.region == 0U)
            break;

        total++;
        total_weight += msg.weight;
        if (msg.region == 1U)
            east++;
        else if (msg.region == 2U)
            south++;
        else
            north++;
        rt_kprintf("package %u -> %s, weight: %u g\n",
                   msg.id, region_name(msg.region), msg.weight);
    }

    rt_kprintf("total packages: %u\n", total);
    rt_kprintf("east: %u, south: %u, north: %u\n", east, south, north);
    rt_kprintf("total weight: %u g\n", total_weight);
    rt_kprintf("send failed: %u\n", send_failed);
    rt_sem_release(done_sem);
}

static int package_demo(int argc, char **argv)
{
    rt_uint32_t i, started = 0;
    (void)argc;
    (void)argv;
    if (running)
    {
        rt_kprintf("package_demo is already running\n");
        return -RT_EBUSY;
    }

    running = 1;
    send_failed = 0;
    senders_done = 0;
    package_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg),
                              QUEUE_SIZE, RT_IPC_FLAG_FIFO);
    stats_mutex = rt_mutex_create("pkg_mtx", RT_IPC_FLAG_FIFO);
    done_sem = rt_sem_create("pkg_done", 0, RT_IPC_FLAG_FIFO);
    if ((package_mq == RT_NULL) || (stats_mutex == RT_NULL) || (done_sem == RT_NULL))
    {
        rt_kprintf("create package IPC failed\n");
        goto cleanup;
    }

    for (i = 0; i < SENDER_COUNT; i++)
    {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "sender%u", (unsigned int)i);
        if (!start_thread(name, sender_entry, (void *)i, THREAD_PRIORITY, &started))
            rt_kprintf("start sender %u failed\n", i);
    }

    if (started == SENDER_COUNT &&
        !start_thread("sorter", sorter_entry, RT_NULL, THREAD_PRIORITY - 1, &started))
        rt_kprintf("start sorter failed\n");

    for (i = 0; i < started; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);

cleanup:
    if (package_mq != RT_NULL)
        rt_mq_delete(package_mq);
    if (stats_mutex != RT_NULL)
        rt_mutex_delete(stats_mutex);
    if (done_sem != RT_NULL)
        rt_sem_delete(done_sem);
    package_mq = RT_NULL;
    stats_mutex = RT_NULL;
    done_sem = RT_NULL;
    running = 0;
    return 0;
}

MSH_CMD_EXPORT(package_demo, package sorting demo);
