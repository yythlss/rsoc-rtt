#include <rtthread.h>
#include <finsh.h>

#define PACKAGE_QUEUE_CAPACITY 5
#define PACKAGE_PRODUCER_COUNT 2
#define PACKAGE_PER_PRODUCER 5
#define PACKAGE_END_MARKER 0

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

struct package_context
{
    rt_mq_t queue;
    rt_uint32_t end_count;
    rt_uint32_t total;
    rt_uint32_t region_count[3];
    rt_uint32_t total_weight;
    rt_uint32_t send_failed[PACKAGE_PRODUCER_COUNT];
    rt_bool_t running;
    rt_thread_t threads[PACKAGE_PRODUCER_COUNT + 1];
};

static struct package_context package;

static const char *package_region_name(rt_uint8_t region)
{
    if (region == 1) return "east";
    if (region == 2) return "south";
    if (region == 3) return "north";
    return "unknown";
}

static rt_err_t package_send_retry(const struct package_msg *msg, rt_uint32_t index)
{
    while (rt_mq_send(package.queue, msg, sizeof(*msg)) != RT_EOK)
    {
        package.send_failed[index]++;
        rt_thread_mdelay(100);
    }
    return RT_EOK;
}

static void package_producer_worker(void *parameter)
{
    rt_uint32_t index = (rt_uint32_t)(rt_ubase_t)parameter;
    rt_uint32_t i;
    struct package_msg msg;

    for (i = 0; i < PACKAGE_PER_PRODUCER; i++)
    {
        msg.id = (index == 0 ? 1001U : 2001U) + i;
        msg.region = (rt_uint8_t)(((i + index) % 3) + 1);
        msg.weight = (rt_uint16_t)(700 + index * 170 + i * 95);
        package_send_retry(&msg, index);
        rt_kprintf("sender%u queued package %u\n", index + 1, msg.id);
        rt_thread_mdelay(20);
    }

    msg.id = PACKAGE_END_MARKER;
    msg.region = 0;
    msg.weight = 0;
    package_send_retry(&msg, index);
    rt_kprintf("sender%u finished\n", index + 1);
    rt_thread_delete(rt_thread_self());
}

static void package_sorter_worker(void *parameter)
{
    struct package_msg msg;
    (void)parameter;
    while (package.end_count < PACKAGE_PRODUCER_COUNT)
    {
        if (rt_mq_recv(package.queue, &msg, sizeof(msg), RT_WAITING_FOREVER) < 0)
        {
            rt_kprintf("package_demo: queue receive failed\n");
            continue;
        }

        if (msg.id == PACKAGE_END_MARKER)
        {
            package.end_count++;
            continue;
        }

        if (msg.region >= 1 && msg.region <= 3)
            package.region_count[msg.region - 1]++;
        package.total++;
        package.total_weight += msg.weight;
        rt_kprintf("package %u -> %s, weight: %u g\n", msg.id,
                   package_region_name(msg.region), msg.weight);
        rt_thread_mdelay(80);
    }

    rt_kprintf("total packages: %u\n", package.total);
    rt_kprintf("east: %u, south: %u, north: %u\n",
               package.region_count[0], package.region_count[1],
               package.region_count[2]);
    rt_kprintf("total weight: %u g\n", package.total_weight);
    rt_kprintf("send failed: %u\n",
               package.send_failed[0] + package.send_failed[1]);
    rt_mq_delete(package.queue);
    package.running = RT_FALSE;
    rt_thread_delete(rt_thread_self());
}

static void package_demo(void)
{
    rt_uint32_t i;

    if (package.running)
    {
        rt_kprintf("package_demo is already running\n");
        return;
    }

    package.queue = rt_mq_create("package_mq", sizeof(struct package_msg),
                                 PACKAGE_QUEUE_CAPACITY, RT_IPC_FLAG_FIFO);
    if (package.queue == RT_NULL)
    {
        rt_kprintf("package_demo: message queue create failed\n");
        return;
    }
    package.end_count = 0;
    package.total = 0;
    package.region_count[0] = 0;
    package.region_count[1] = 0;
    package.region_count[2] = 0;
    package.total_weight = 0;
    package.send_failed[0] = 0;
    package.send_failed[1] = 0;
    package.running = RT_TRUE;

    for (i = 0; i < PACKAGE_PRODUCER_COUNT; i++)
    {
        package.threads[i] = rt_thread_create(i == 0 ? "sender_a" : "sender_b",
                                              package_producer_worker,
                                              (void *)(rt_ubase_t)i,
                                              768, 16, 10);
        if (package.threads[i] == RT_NULL)
        {
            rt_uint32_t j;
            package.running = RT_FALSE;
            for (j = 0; j < i; j++)
                rt_thread_delete(package.threads[j]);
            rt_mq_delete(package.queue);
            rt_kprintf("package_demo: sender %u create failed\n", i + 1);
            return;
        }
    }

    package.threads[PACKAGE_PRODUCER_COUNT] =
        rt_thread_create("sorter", package_sorter_worker, RT_NULL,
                         1024, 18, 10);
    if (package.threads[PACKAGE_PRODUCER_COUNT] == RT_NULL)
    {
        package.running = RT_FALSE;
        for (i = 0; i < PACKAGE_PRODUCER_COUNT; i++)
            rt_thread_delete(package.threads[i]);
        rt_mq_delete(package.queue);
        rt_kprintf("package_demo: sorter thread create failed\n");
        return;
    }

    for (i = 0; i < PACKAGE_PRODUCER_COUNT + 1; i++)
        rt_thread_startup(package.threads[i]);
    rt_kprintf("package_demo started: queue capacity %d\n",
               PACKAGE_QUEUE_CAPACITY);
}
MSH_CMD_EXPORT(package_demo, message queue package sorting demo);
