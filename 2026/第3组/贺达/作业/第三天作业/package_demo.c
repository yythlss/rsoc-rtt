#include <rtthread.h>

#define PACKAGE_PRINT(...)           \
    do                               \
    {                                \
        rt_enter_critical();         \
        rt_kprintf(__VA_ARGS__);     \
        rt_exit_critical();          \
    } while (0)

#define PACKAGE_PER_SENDER  5
#define PACKAGE_QUEUE_DEPTH 5
#define PACKAGE_END_ID      0xFFFFFFFFU

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

struct package_sender
{
    rt_uint32_t id_base;
    rt_uint16_t weight_base;
    const char *name;
};

static const struct package_sender package_senders[2] =
{
    {1100, 720, "receiver-A"},
    {2200, 930, "receiver-B"}
};

static rt_mq_t package_queue = RT_NULL;
static rt_sem_t package_done = RT_NULL;
static volatile rt_uint32_t package_send_failed = 0;
static volatile rt_bool_t package_running = RT_FALSE;

static void package_set_running(rt_bool_t running)
{
    rt_base_t level = rt_hw_interrupt_disable();
    package_running = running;
    rt_hw_interrupt_enable(level);
}

static rt_bool_t package_begin(void)
{
    rt_base_t level;
    rt_bool_t accepted = RT_FALSE;

    level = rt_hw_interrupt_disable();
    if (package_running == RT_FALSE)
    {
        package_running = RT_TRUE;
        accepted = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    return accepted;
}

static void package_record_send_failure(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    package_send_failed++;
    rt_hw_interrupt_enable(level);
}

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

static void package_sender_entry(void *parameter)
{
    const struct package_sender *sender =
        (const struct package_sender *)parameter;
    struct package_msg message;
    rt_uint8_t index;

    for (index = 1; index <= PACKAGE_PER_SENDER; index++)
    {
        message.id = sender->id_base + index;
        message.region = (rt_uint8_t)(((sender->id_base / 100 + index) % 3) + 1);
        message.weight = (rt_uint16_t)(sender->weight_base + index * 85);

        while (rt_mq_send(package_queue, &message, sizeof(message)) != RT_EOK)
        {
            package_record_send_failure();
            PACKAGE_PRINT("[package] %s queue full for package %u, retry\n",
                          sender->name, message.id);
            rt_thread_mdelay(80);
        }

        PACKAGE_PRINT("[package] %s accepted package %u\n",
                      sender->name, message.id);
        rt_thread_mdelay(15);
    }

    rt_sem_release(package_done);
}

static void package_sorter_entry(void *parameter)
{
    struct package_msg message;
    rt_uint32_t total_count = 0;
    rt_uint32_t total_weight = 0;
    rt_uint32_t region_count[3] = {0, 0, 0};
    rt_ssize_t received_size;

    (void)parameter;
    while (1)
    {
        received_size = rt_mq_recv(package_queue, &message, sizeof(message),
                                   RT_WAITING_FOREVER);
        if (received_size != (rt_ssize_t)sizeof(message))
        {
            PACKAGE_PRINT("[package] receive failed: %d\n", received_size);
            continue;
        }

        if (message.id == PACKAGE_END_ID)
        {
            break;
        }

        total_count++;
        total_weight += message.weight;
        if ((message.region >= 1) && (message.region <= 3))
        {
            region_count[message.region - 1]++;
        }

        PACKAGE_PRINT("[package] %u -> %s, weight: %u g\n",
                      message.id, package_region_name(message.region),
                      message.weight);

        /* Sorting is intentionally slower, so producers can observe a full queue. */
        rt_thread_mdelay(140);
    }

    PACKAGE_PRINT("[package] total packages: %u\n", total_count);
    PACKAGE_PRINT("[package] east: %u, south: %u, north: %u\n",
                  region_count[0], region_count[1], region_count[2]);
    PACKAGE_PRINT("[package] total weight: %u g, send failed/retried: %u\n",
                  total_weight, package_send_failed);
    rt_sem_release(package_done);
}

static void package_manager_entry(void *parameter)
{
    rt_thread_t sorter = RT_NULL;
    rt_thread_t senders[2] = {RT_NULL};
    rt_uint8_t index;
    rt_uint8_t started_senders = 0;
    struct package_msg end_message = {PACKAGE_END_ID, 0, 0};

    (void)parameter;
    package_send_failed = 0;
    package_queue = rt_mq_create("pkgq", sizeof(struct package_msg),
                                 PACKAGE_QUEUE_DEPTH, RT_IPC_FLAG_PRIO);
    package_done = rt_sem_create("pkgdone", 0, RT_IPC_FLAG_PRIO);

    if ((package_queue == RT_NULL) || (package_done == RT_NULL))
    {
        PACKAGE_PRINT("[package] IPC object creation failed\n");
        if (package_done != RT_NULL)
            rt_sem_delete(package_done);
        if (package_queue != RT_NULL)
            rt_mq_delete(package_queue);
        package_done = RT_NULL;
        package_queue = RT_NULL;
        package_set_running(RT_FALSE);
        return;
    }

    sorter = rt_thread_create("sorter", package_sorter_entry, RT_NULL,
                              1280, 17, 5);
    senders[0] = rt_thread_create("pkgsnd1", package_sender_entry,
                                  (void *)&package_senders[0], 1024, 18, 5);
    senders[1] = rt_thread_create("pkgsnd2", package_sender_entry,
                                  (void *)&package_senders[1], 1024, 18, 5);

    if ((sorter == RT_NULL) || (senders[0] == RT_NULL) ||
        (senders[1] == RT_NULL))
    {
        PACKAGE_PRINT("[package] worker creation failed\n");
        if (sorter != RT_NULL)
            rt_thread_delete(sorter);
        for (index = 0; index < 2; index++)
        {
            if (senders[index] != RT_NULL)
                rt_thread_delete(senders[index]);
        }
        rt_sem_delete(package_done);
        rt_mq_delete(package_queue);
        package_done = RT_NULL;
        package_queue = RT_NULL;
        package_set_running(RT_FALSE);
        return;
    }

    if (rt_thread_startup(sorter) != RT_EOK)
    {
        PACKAGE_PRINT("[package] start sorter failed\n");
        rt_thread_delete(sorter);
        rt_thread_delete(senders[0]);
        rt_thread_delete(senders[1]);
        rt_sem_delete(package_done);
        rt_mq_delete(package_queue);
        package_done = RT_NULL;
        package_queue = RT_NULL;
        package_set_running(RT_FALSE);
        return;
    }

    for (index = 0; index < 2; index++)
    {
        if (rt_thread_startup(senders[index]) == RT_EOK)
        {
            started_senders++;
        }
        else
        {
            PACKAGE_PRINT("[package] start sender %u failed\n", index + 1);
            rt_thread_delete(senders[index]);
        }
    }

    for (index = 0; index < started_senders; index++)
    {
        rt_sem_take(package_done, RT_WAITING_FOREVER);
    }

    /* One terminator is queued only after both producers have finished. */
    while (rt_mq_send(package_queue, &end_message, sizeof(end_message)) != RT_EOK)
    {
        rt_thread_mdelay(50);
    }

    rt_sem_take(package_done, RT_WAITING_FOREVER);
    rt_sem_delete(package_done);
    rt_mq_delete(package_queue);
    package_done = RT_NULL;
    package_queue = RT_NULL;
    package_set_running(RT_FALSE);
}

static void package_demo(void)
{
    rt_thread_t manager;

    if (package_begin() == RT_FALSE)
    {
        PACKAGE_PRINT("[package] demo is already running\n");
        return;
    }

    manager = rt_thread_create("pkg_mgr", package_manager_entry, RT_NULL,
                               1536, 16, 5);
    if (manager == RT_NULL)
    {
        PACKAGE_PRINT("[package] create manager failed\n");
        package_set_running(RT_FALSE);
        return;
    }

    if (rt_thread_startup(manager) != RT_EOK)
    {
        PACKAGE_PRINT("[package] start manager failed\n");
        rt_thread_delete(manager);
        package_set_running(RT_FALSE);
    }
}
MSH_CMD_EXPORT(package_demo, bounded message queue package sorting demo);
