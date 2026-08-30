/*
 * 程序清单：系统启动条件检查
 *
 * 三个初始化线程发送各自的就绪事件；业务线程以 AND 模式等待全部事件。
 */
#include <rtthread.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

#define MODULE_COUNT             3
#define THREAD_COUNT             4
#define THREAD_PRIORITY          20
#define THREAD_TIMESLICE         5
#define BUSINESS_TIMEOUT_MS      3000

struct startup_module
{
    const char *name;
    rt_uint32_t event_bit;
    rt_uint32_t delay_ms;
};

struct startup_thread
{
    const char *name;
    void (*entry)(void *parameter);
    void *parameter;
    rt_uint8_t priority;
    struct rt_thread thread;
    char stack[1024];
};

static void module_init_entry(void *parameter);
static void business_entry(void *parameter);

static struct rt_event startup_event;
static rt_uint32_t ready_events;
static rt_bool_t startup_running;
static struct startup_module modules[MODULE_COUNT] =
{
    {"network", EVENT_NET_READY, 1000},
    {"sensor", EVENT_SENSOR_READY, 2000},
    {"storage", EVENT_STORAGE_READY, 5000}
};
static struct startup_thread startup_threads[THREAD_COUNT] =
{
    {"net_init", module_init_entry, &modules[0], THREAD_PRIORITY},
    {"sensor", module_init_entry, &modules[1], THREAD_PRIORITY},
    {"storage", module_init_entry, &modules[2], THREAD_PRIORITY},
    {"business", business_entry, RT_NULL, THREAD_PRIORITY - 1}
};

static void startup_demo_cleanup(void)
{
    rt_event_detach(&startup_event);
    startup_running = RT_FALSE;
}

static void module_init_entry(void *parameter)
{
    struct startup_module *module = parameter;
    rt_err_t result;

    rt_thread_mdelay(module->delay_ms);

    rt_enter_critical();
    ready_events |= module->event_bit;
    rt_exit_critical();

    rt_kprintf("%s ready\n", module->name);

    result = rt_event_send(&startup_event, module->event_bit);
    if (result != RT_EOK)
    {
        rt_kprintf("send %s ready event failed: %d\n", module->name, result);
    }
}

static void print_not_ready_modules(void)
{
    rt_uint32_t events;

    rt_enter_critical();
    events = ready_events;
    rt_exit_critical();

    rt_kprintf("wait modules timeout,");
    if (!(events & EVENT_NET_READY))
    {
        rt_kprintf(" network is not ready");
    }
    if (!(events & EVENT_SENSOR_READY))
    {
        rt_kprintf(" sensor is not ready");
    }
    if (!(events & EVENT_STORAGE_READY))
    {
        rt_kprintf(" storage is not ready");
    }
    rt_kprintf("\n");
}

static void business_entry(void *parameter)
{
    rt_uint32_t received;
    rt_err_t result;

    result = rt_event_recv(&startup_event, EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND,
                           rt_tick_from_millisecond(BUSINESS_TIMEOUT_MS),
                           &received);
    if (result != RT_EOK)
    {
        print_not_ready_modules();
        result = rt_event_recv(&startup_event, EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND,
                               RT_WAITING_FOREVER,
                               &received);
    }

    if (result == RT_EOK)
    {
        rt_kprintf("all modules are ready\n");
        rt_kprintf("business task started\n");
    }
    else
    {
        rt_kprintf("wait all modules failed: %d\n", result);
    }

    startup_demo_cleanup();
}

int startup_demo(void)
{
    rt_uint8_t i;
    rt_err_t result;

    if (startup_running)
    {
        rt_kprintf("startup demo is already running.\n");
        return -RT_EBUSY;
    }

    result = rt_event_init(&startup_event, "startup", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        rt_kprintf("create startup event failed: %d\n", result);
        return result;
    }

    startup_running = RT_TRUE;
    ready_events = 0;

    /* 所有线程均初始化成功后，才启动业务线程和三个模块线程。 */
    for (i = 0; i < THREAD_COUNT; i++)
    {
        result = rt_thread_init(&startup_threads[i].thread,
                                startup_threads[i].name,
                                startup_threads[i].entry,
                                startup_threads[i].parameter,
                                startup_threads[i].stack,
                                sizeof(startup_threads[i].stack),
                                startup_threads[i].priority,
                                THREAD_TIMESLICE);
        if (result != RT_EOK)
        {
            rt_kprintf("create %s thread failed: %d\n", startup_threads[i].name, result);
            while (i > 0)
            {
                rt_thread_detach(&startup_threads[--i].thread);
            }
            startup_demo_cleanup();
            return result;
        }
    }

    /* 业务线程先进入等待状态，再启动三个独立的初始化线程。 */
    result = rt_thread_startup(&startup_threads[MODULE_COUNT].thread);
    if (result != RT_EOK)
    {
        rt_kprintf("start business thread failed: %d\n", result);
        for (i = 0; i < THREAD_COUNT; i++)
        {
            rt_thread_detach(&startup_threads[i].thread);
        }
        startup_demo_cleanup();
        return result;
    }

    for (i = 0; i < MODULE_COUNT; i++)
    {
        result = rt_thread_startup(&startup_threads[i].thread);
        if (result != RT_EOK)
        {
            rt_kprintf("start %s thread failed: %d\n", startup_threads[i].name, result);
            return result;
        }
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, startup event demo);
