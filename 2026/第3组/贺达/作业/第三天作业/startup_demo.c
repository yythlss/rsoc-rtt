#include <rtthread.h>

#define STARTUP_PRINT(...)           \
    do                               \
    {                                \
        rt_enter_critical();         \
        rt_kprintf(__VA_ARGS__);     \
        rt_exit_critical();          \
    } while (0)

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

struct startup_module
{
    const char *name;
    rt_uint32_t event;
    rt_uint32_t delay_ms;
};

static const struct startup_module startup_modules[] =
{
    {"network", EVENT_NET_READY, 700},
    {"sensor", EVENT_SENSOR_READY, 1300},
    {"storage", EVENT_STORAGE_READY, 5000}
};

static rt_event_t startup_events = RT_NULL;
static rt_sem_t startup_done = RT_NULL;
static volatile rt_uint32_t startup_ready_mask = 0;
static volatile rt_bool_t startup_running = RT_FALSE;

static void startup_set_running(rt_bool_t running)
{
    rt_base_t level = rt_hw_interrupt_disable();
    startup_running = running;
    rt_hw_interrupt_enable(level);
}

static rt_bool_t startup_begin(void)
{
    rt_base_t level;
    rt_bool_t accepted = RT_FALSE;

    level = rt_hw_interrupt_disable();
    if (startup_running == RT_FALSE)
    {
        startup_running = RT_TRUE;
        accepted = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    return accepted;
}

static void startup_module_entry(void *parameter)
{
    const struct startup_module *module =
        (const struct startup_module *)parameter;
    rt_base_t level;

    rt_thread_mdelay(module->delay_ms);

    level = rt_hw_interrupt_disable();
    startup_ready_mask |= module->event;
    rt_hw_interrupt_enable(level);

    STARTUP_PRINT("[startup] %s ready at %u ms\n",
                  module->name, module->delay_ms);

    if (rt_event_send(startup_events, module->event) != RT_EOK)
    {
        STARTUP_PRINT("[startup] %s event send failed\n", module->name);
    }

    rt_sem_release(startup_done);
}

static void startup_print_missing(rt_uint32_t ready)
{
    rt_enter_critical();
    rt_kprintf("[startup] wait modules timeout");
    if ((ready & EVENT_NET_READY) == 0)
        rt_kprintf(", network not ready");
    if ((ready & EVENT_SENSOR_READY) == 0)
        rt_kprintf(", sensor not ready");
    if ((ready & EVENT_STORAGE_READY) == 0)
        rt_kprintf(", storage not ready");
    rt_kprintf("\n");
    rt_exit_critical();
}

static void startup_business_entry(void *parameter)
{
    rt_uint32_t received = 0;
    rt_err_t result;

    (void)parameter;
    result = rt_event_recv(startup_events, EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND,
                           rt_tick_from_millisecond(3000), &received);
    if (result != RT_EOK)
    {
        startup_print_missing(startup_ready_mask);
        result = rt_event_recv(startup_events, EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               RT_WAITING_FOREVER, &received);
    }

    if (result == RT_EOK)
    {
        STARTUP_PRINT("[startup] all modules are ready\n");
        STARTUP_PRINT("[startup] business task started\n");
    }
    else
    {
        STARTUP_PRINT("[startup] event receive failed: %d\n", result);
    }

    rt_sem_release(startup_done);
}

static void startup_manager_entry(void *parameter)
{
    rt_thread_t threads[4] = {RT_NULL};
    rt_uint8_t index;
    rt_uint8_t started = 0;
    rt_bool_t startup_failed = RT_FALSE;

    (void)parameter;
    startup_ready_mask = 0;
    startup_events = rt_event_create("d3event", RT_IPC_FLAG_PRIO);
    startup_done = rt_sem_create("sdone", 0, RT_IPC_FLAG_PRIO);

    if ((startup_events == RT_NULL) || (startup_done == RT_NULL))
    {
        STARTUP_PRINT("[startup] IPC object creation failed\n");
        if (startup_done != RT_NULL)
            rt_sem_delete(startup_done);
        if (startup_events != RT_NULL)
            rt_event_delete(startup_events);
        startup_done = RT_NULL;
        startup_events = RT_NULL;
        startup_set_running(RT_FALSE);
        return;
    }

    threads[0] = rt_thread_create("netinit", startup_module_entry,
                                  (void *)&startup_modules[0], 1024, 18, 5);
    threads[1] = rt_thread_create("seninit", startup_module_entry,
                                  (void *)&startup_modules[1], 1024, 18, 5);
    threads[2] = rt_thread_create("storage", startup_module_entry,
                                  (void *)&startup_modules[2], 1024, 18, 5);
    threads[3] = rt_thread_create("biz", startup_business_entry,
                                  RT_NULL, 1280, 17, 5);

    for (index = 0; index < 4; index++)
    {
        if (threads[index] == RT_NULL)
        {
            STARTUP_PRINT("[startup] create worker %u failed\n", index);
            startup_failed = RT_TRUE;
        }
    }

    if (startup_failed == RT_TRUE)
    {
        for (index = 0; index < 4; index++)
        {
            if (threads[index] != RT_NULL)
                rt_thread_delete(threads[index]);
        }
        rt_sem_delete(startup_done);
        rt_event_delete(startup_events);
        startup_done = RT_NULL;
        startup_events = RT_NULL;
        startup_set_running(RT_FALSE);
        return;
    }

    for (index = 0; index < 4; index++)
    {
        if (rt_thread_startup(threads[index]) == RT_EOK)
        {
            started++;
        }
        else
        {
            STARTUP_PRINT("[startup] start worker %u failed\n", index);
            rt_thread_delete(threads[index]);
            startup_failed = RT_TRUE;
        }
    }

    if (startup_failed == RT_TRUE)
    {
        /* Prevent a started business thread from waiting forever. */
        startup_ready_mask = EVENT_ALL_READY;
        rt_event_send(startup_events, EVENT_ALL_READY);
    }

    for (index = 0; index < started; index++)
    {
        rt_sem_take(startup_done, RT_WAITING_FOREVER);
    }

    rt_sem_delete(startup_done);
    rt_event_delete(startup_events);
    startup_done = RT_NULL;
    startup_events = RT_NULL;
    startup_set_running(RT_FALSE);
}

static void startup_demo(void)
{
    rt_thread_t manager;

    if (startup_begin() == RT_FALSE)
    {
        STARTUP_PRINT("[startup] demo is already running\n");
        return;
    }

    manager = rt_thread_create("s_mgr", startup_manager_entry, RT_NULL,
                               1536, 16, 5);
    if (manager == RT_NULL)
    {
        STARTUP_PRINT("[startup] create manager failed\n");
        startup_set_running(RT_FALSE);
        return;
    }

    if (rt_thread_startup(manager) != RT_EOK)
    {
        STARTUP_PRINT("[startup] start manager failed\n");
        rt_thread_delete(manager);
        startup_set_running(RT_FALSE);
    }
}
MSH_CMD_EXPORT(startup_demo, event-set startup condition demo);
