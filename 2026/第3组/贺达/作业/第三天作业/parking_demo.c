#include <rtthread.h>

#define PARKING_PRINT(...)           \
    do                               \
    {                                \
        rt_enter_critical();         \
        rt_kprintf(__VA_ARGS__);     \
        rt_exit_critical();          \
    } while (0)

#define PARKING_SPACE_COUNT  3
#define PARKING_CAR_COUNT    5
#define PARKING_WAIT_MS      2000

static rt_sem_t parking_slots = RT_NULL;
static rt_sem_t parking_done = RT_NULL;
static rt_mutex_t parking_stats_lock = RT_NULL;
static volatile rt_bool_t parking_running = RT_FALSE;
static rt_uint8_t parking_used = 0;

static void parking_set_running(rt_bool_t running)
{
    rt_base_t level = rt_hw_interrupt_disable();
    parking_running = running;
    rt_hw_interrupt_enable(level);
}

static rt_bool_t parking_begin(void)
{
    rt_base_t level;
    rt_bool_t accepted = RT_FALSE;

    level = rt_hw_interrupt_disable();
    if (parking_running == RT_FALSE)
    {
        parking_running = RT_TRUE;
        accepted = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    return accepted;
}

static void parking_car_entry(void *parameter)
{
    rt_uint8_t car_id = (rt_uint8_t)(rt_ubase_t)parameter;
    rt_err_t result;

    PARKING_PRINT("[parking] car%u arrived\n", car_id);

    while (1)
    {
        result = rt_sem_take(parking_slots,
                             rt_tick_from_millisecond(PARKING_WAIT_MS));
        if (result == RT_EOK)
        {
            break;
        }

        PARKING_PRINT("[parking] car%u wait timeout, retrying\n", car_id);
        rt_thread_mdelay(300 + car_id * 30);
    }

    if (rt_mutex_take(parking_stats_lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        parking_used++;
        PARKING_PRINT("[parking] car%u entered, used spaces: %u/%u\n",
                      car_id, parking_used, PARKING_SPACE_COUNT);
        rt_mutex_release(parking_stats_lock);
    }

    rt_thread_mdelay(3200 + car_id * 100);

    if (rt_mutex_take(parking_stats_lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        if (parking_used > 0)
        {
            parking_used--;
        }
        PARKING_PRINT("[parking] car%u left, used spaces: %u/%u\n",
                      car_id, parking_used, PARKING_SPACE_COUNT);
        rt_mutex_release(parking_stats_lock);
    }

    rt_sem_release(parking_slots);
    rt_sem_release(parking_done);
}

static void parking_manager_entry(void *parameter)
{
    rt_thread_t tid;
    rt_uint8_t index;
    rt_uint8_t started = 0;
    char name[RT_NAME_MAX];

    (void)parameter;
    parking_used = 0;

    parking_slots = rt_sem_create("pslots", PARKING_SPACE_COUNT,
                                  RT_IPC_FLAG_PRIO);
    parking_done = rt_sem_create("pdone", 0, RT_IPC_FLAG_PRIO);
    parking_stats_lock = rt_mutex_create("pstat", RT_IPC_FLAG_PRIO);

    if ((parking_slots == RT_NULL) || (parking_done == RT_NULL) ||
        (parking_stats_lock == RT_NULL))
    {
        PARKING_PRINT("[parking] IPC object creation failed\n");
        if (parking_stats_lock != RT_NULL)
            rt_mutex_delete(parking_stats_lock);
        if (parking_done != RT_NULL)
            rt_sem_delete(parking_done);
        if (parking_slots != RT_NULL)
            rt_sem_delete(parking_slots);
        parking_stats_lock = RT_NULL;
        parking_done = RT_NULL;
        parking_slots = RT_NULL;
        parking_set_running(RT_FALSE);
        return;
    }

    for (index = 0; index < PARKING_CAR_COUNT; index++)
    {
        rt_snprintf(name, sizeof(name), "car%u", index + 1);
        tid = rt_thread_create(name, parking_car_entry,
                               (void *)(rt_ubase_t)(index + 1),
                               1024, 18, 5);
        if (tid == RT_NULL)
        {
            PARKING_PRINT("[parking] create car%u failed\n", index + 1);
            continue;
        }

        if (rt_thread_startup(tid) != RT_EOK)
        {
            PARKING_PRINT("[parking] start car%u failed\n", index + 1);
            rt_thread_delete(tid);
            continue;
        }
        started++;
    }

    for (index = 0; index < started; index++)
    {
        rt_sem_take(parking_done, RT_WAITING_FOREVER);
    }

    PARKING_PRINT("[parking] finished, peak occupancy never exceeded %u\n",
                  PARKING_SPACE_COUNT);

    rt_mutex_delete(parking_stats_lock);
    rt_sem_delete(parking_done);
    rt_sem_delete(parking_slots);
    parking_stats_lock = RT_NULL;
    parking_done = RT_NULL;
    parking_slots = RT_NULL;
    parking_set_running(RT_FALSE);
}

static void parking_demo(void)
{
    rt_thread_t manager;

    if (parking_begin() == RT_FALSE)
    {
        PARKING_PRINT("[parking] demo is already running\n");
        return;
    }

    manager = rt_thread_create("p_mgr", parking_manager_entry, RT_NULL,
                               1536, 16, 5);
    if (manager == RT_NULL)
    {
        PARKING_PRINT("[parking] create manager failed\n");
        parking_set_running(RT_FALSE);
        return;
    }

    if (rt_thread_startup(manager) != RT_EOK)
    {
        PARKING_PRINT("[parking] start manager failed\n");
        rt_thread_delete(manager);
        parking_set_running(RT_FALSE);
    }
}
MSH_CMD_EXPORT(parking_demo, counting semaphore parking demo);
