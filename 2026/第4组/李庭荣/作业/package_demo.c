/* Day3 exercise: two producers and one message-queue sorting consumer. */
#include <rtthread.h>

#define PACKAGE_PRODUCER_COUNT  2
#define PACKAGE_COUNT_PER_THREAD 5
#define PACKAGE_QUEUE_CAPACITY  5
#define PACKAGE_STACK_SIZE      1024

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
    rt_uint8_t end;
};

#define PACKAGE_MSG_SIZE RT_ALIGN(sizeof(struct package_msg), RT_ALIGN_SIZE)

struct package_producer
{
    rt_uint32_t base_id;
    rt_uint8_t region;
};

static struct rt_messagequeue package_queue;
static struct rt_mutex package_stat_lock;
static ALIGN(RT_ALIGN_SIZE) rt_uint8_t package_pool[PACKAGE_MSG_SIZE * PACKAGE_QUEUE_CAPACITY];
static struct rt_thread package_threads[PACKAGE_PRODUCER_COUNT + 1];
static ALIGN(RT_ALIGN_SIZE) rt_uint8_t package_stacks[PACKAGE_PRODUCER_COUNT + 1][PACKAGE_STACK_SIZE];
static struct package_producer package_producers[PACKAGE_PRODUCER_COUNT] = {
    {1001, 1},
    {2001, 2},
};
static rt_uint32_t package_end_count;
static rt_uint32_t package_total;
static rt_uint32_t package_region_count[4];
static rt_uint32_t package_total_weight;
static rt_uint32_t package_send_failed;
static rt_uint32_t package_finished;
static rt_uint32_t package_expected;
static rt_bool_t package_abort;
static rt_bool_t package_running;

static const char *package_region_name(rt_uint8_t region)
{
    switch (region)
    {
    case 1: return "east";
    case 2: return "south";
    case 3: return "north";
    default: return "unknown";
    }
}

static void package_cleanup(void)
{
    rt_mq_detach(&package_queue);
    rt_mutex_detach(&package_stat_lock);
    package_running = RT_FALSE;
}

static void package_detach_threads(rt_uint32_t first, rt_uint32_t count)
{
    rt_uint32_t index;

    for (index = first; index < count; index++)
        rt_thread_detach(&package_threads[index]);
}

static void package_finish(void)
{
    rt_bool_t last = RT_FALSE;

    rt_mutex_take(&package_stat_lock, RT_WAITING_FOREVER);
    package_finished++;
    if (package_finished == package_expected)
        last = RT_TRUE;
    rt_mutex_release(&package_stat_lock);

    if (last)
    {
        package_cleanup();
        rt_kprintf("package_demo finished\n");
    }
}

static rt_err_t package_send_retry(const struct package_msg *message)
{
    rt_err_t result;

    while (!package_abort)
    {
        result = rt_mq_send(&package_queue, message, sizeof(*message));
        if (result == RT_EOK)
            return RT_EOK;

        rt_mutex_take(&package_stat_lock, RT_WAITING_FOREVER);
        package_send_failed++;
        rt_mutex_release(&package_stat_lock);
        rt_kprintf("package send failed, retrying\n");
        rt_thread_mdelay(80);
    }

    return -RT_ERROR;
}

static void package_producer_entry(void *parameter)
{
    struct package_producer *producer = (struct package_producer *)parameter;
    struct package_msg message;
    rt_uint32_t index;

    for (index = 0; index < PACKAGE_COUNT_PER_THREAD; index++)
    {
        message.id = producer->base_id + index;
        message.region = producer->region;
        message.weight = (rt_uint16_t)(700 + producer->region * 100 + index * 30);
        message.end = 0;
        if (package_send_retry(&message) != RT_EOK)
            break;
        rt_kprintf("producer %u sent package %u\n",
                   producer->base_id, message.id);
        rt_thread_mdelay(5);
    }

    message.id = 0;
    message.region = 0;
    message.weight = 0;
    message.end = 1;
    package_send_retry(&message);
    package_finish();
}

static void package_consumer_entry(void *parameter)
{
    struct package_msg message;
    rt_err_t result;

    (void)parameter;
    while (!package_abort && package_end_count < PACKAGE_PRODUCER_COUNT)
    {
        result = rt_mq_recv(&package_queue, &message, sizeof(message),
                            RT_TICK_PER_SECOND);
        if (result != RT_EOK)
        {
            if (result != -RT_ETIMEOUT)
                rt_kprintf("package receive failed: %d\n", result);
            continue;
        }

        if (message.end)
        {
            package_end_count++;
            rt_kprintf("producer end received: %u/%u\n",
                       package_end_count, PACKAGE_PRODUCER_COUNT);
            continue;
        }

        package_total++;
        package_region_count[message.region]++;
        package_total_weight += message.weight;
        rt_kprintf("package %u -> %s, weight: %u g\n",
                   message.id, package_region_name(message.region),
                   message.weight);
        rt_thread_mdelay(50);
    }

    rt_kprintf("total packages: %u\n", package_total);
    rt_kprintf("east: %u, south: %u, north: %u\n",
               package_region_count[1], package_region_count[2],
               package_region_count[3]);
    rt_kprintf("total weight: %u g\n", package_total_weight);
    rt_mutex_take(&package_stat_lock, RT_WAITING_FOREVER);
    rt_kprintf("send failed: %u\n", package_send_failed);
    rt_mutex_release(&package_stat_lock);
    package_finish();
}

int package_demo(void)
{
    rt_err_t result;
    rt_uint32_t index;

    if (package_running)
    {
        rt_kprintf("package_demo is already running\n");
        return -RT_EBUSY;
    }

    result = rt_mq_init(&package_queue, "pkgmq", package_pool,
                        PACKAGE_MSG_SIZE, sizeof(package_pool),
                        RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_kprintf("package queue init failed: %d\n", result);
        return result;
    }

    result = rt_mutex_init(&package_stat_lock, "pkglk", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_mq_detach(&package_queue);
        rt_kprintf("package mutex init failed: %d\n", result);
        return result;
    }

    package_end_count = 0;
    package_total = 0;
    package_region_count[1] = 0;
    package_region_count[2] = 0;
    package_region_count[3] = 0;
    package_total_weight = 0;
    package_send_failed = 0;
    package_finished = 0;
    package_expected = PACKAGE_PRODUCER_COUNT + 1;
    package_abort = RT_FALSE;
    package_running = RT_TRUE;

    for (index = 0; index < PACKAGE_PRODUCER_COUNT; index++)
    {
        result = rt_thread_init(&package_threads[index], "producer",
                                package_producer_entry, &package_producers[index],
                                package_stacks[index], PACKAGE_STACK_SIZE,
                                18, 5);
        if (result != RT_EOK)
        {
            package_detach_threads(0, index);
            package_cleanup();
            rt_kprintf("producer %u init failed: %d\n", index + 1, result);
            return result;
        }
    }

    result = rt_thread_init(&package_threads[PACKAGE_PRODUCER_COUNT],
                            "sorter", package_consumer_entry, RT_NULL,
                            package_stacks[PACKAGE_PRODUCER_COUNT],
                            PACKAGE_STACK_SIZE, 16, 5);
    if (result != RT_EOK)
    {
        package_detach_threads(0, PACKAGE_PRODUCER_COUNT);
        package_cleanup();
        rt_kprintf("sorter init failed: %d\n", result);
        return result;
    }

    for (index = 0; index < PACKAGE_PRODUCER_COUNT + 1; index++)
    {
        result = rt_thread_startup(&package_threads[index]);
        if (result != RT_EOK)
        {
            rt_bool_t cleanup_now;

            rt_mutex_take(&package_stat_lock, RT_WAITING_FOREVER);
            package_abort = RT_TRUE;
            package_expected = index;
            cleanup_now = (package_finished == package_expected);
            rt_mutex_release(&package_stat_lock);
            package_detach_threads(index, PACKAGE_PRODUCER_COUNT + 1);
            if (cleanup_now)
                package_cleanup();
            rt_kprintf("package thread %u startup failed: %d\n", index, result);
            return result;
        }
    }

    rt_kprintf("package_demo started: queue=%u, producers=%u\n",
               PACKAGE_QUEUE_CAPACITY, PACKAGE_PRODUCER_COUNT);
    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, message queue package demo);
