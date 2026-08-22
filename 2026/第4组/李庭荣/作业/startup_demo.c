/* Day3 exercise: event-set startup barrier with a bounded first wait. */
#include <rtthread.h>

#define EVENT_NET_READY       (1u << 0)
#define EVENT_SENSOR_READY    (1u << 1)
#define EVENT_STORAGE_READY   (1u << 2)
#define EVENT_ALL_READY       (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)
#define STARTUP_THREAD_COUNT  4
#define STARTUP_STACK_SIZE    1024

struct startup_module
{
    const char *name;
    rt_uint32_t event_bit;
    rt_uint32_t delay_ms;
};

static struct rt_event startup_event;
static struct rt_mutex startup_lock;
static struct rt_thread startup_threads[STARTUP_THREAD_COUNT];
static ALIGN(RT_ALIGN_SIZE) rt_uint8_t startup_stacks[STARTUP_THREAD_COUNT][STARTUP_STACK_SIZE];
static struct startup_module startup_modules[3] = {
    {"network", EVENT_NET_READY, 700},
    {"sensor", EVENT_SENSOR_READY, 1400},
    {"storage", EVENT_STORAGE_READY, 5000},
};
static rt_uint32_t startup_ready_mask;
static rt_uint32_t startup_finished;
static rt_uint32_t startup_expected;
static rt_bool_t startup_abort;
static rt_bool_t startup_running;

static const char *startup_missing_name(rt_uint32_t bit)
{
    if ((bit & EVENT_NET_READY) != 0)
        return "network";
    if ((bit & EVENT_SENSOR_READY) != 0)
        return "sensor";
    if ((bit & EVENT_STORAGE_READY) != 0)
        return "storage";
    return "none";
}

static void startup_cleanup(void)
{
    rt_event_detach(&startup_event);
    rt_mutex_detach(&startup_lock);
    startup_running = RT_FALSE;
}

static void startup_detach_threads(rt_uint32_t first, rt_uint32_t count)
{
    rt_uint32_t index;

    for (index = first; index < count; index++)
        rt_thread_detach(&startup_threads[index]);
}

static void startup_finish(void)
{
    rt_bool_t last = RT_FALSE;

    rt_mutex_take(&startup_lock, RT_WAITING_FOREVER);
    startup_finished++;
    if (startup_finished == startup_expected)
        last = RT_TRUE;
    rt_mutex_release(&startup_lock);

    if (last)
    {
        startup_cleanup();
        rt_kprintf("startup_demo finished\n");
    }
}

static void startup_module_entry(void *parameter)
{
    struct startup_module *module = (struct startup_module *)parameter;

    rt_thread_mdelay(module->delay_ms);
    if (startup_abort)
    {
        startup_finish();
        return;
    }

    rt_mutex_take(&startup_lock, RT_WAITING_FOREVER);
    startup_ready_mask |= module->event_bit;
    rt_mutex_release(&startup_lock);
    rt_kprintf("%s ready\n", module->name);
    rt_event_send(&startup_event, module->event_bit);
    startup_finish();
}

static void startup_business_entry(void *parameter)
{
    rt_uint32_t recved = 0;
    rt_uint32_t ready_mask;
    rt_err_t result;

    (void)parameter;
    if (startup_abort)
        goto finish;

    result = rt_event_recv(&startup_event, EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                           3 * RT_TICK_PER_SECOND, &recved);
    if (result == -RT_ETIMEOUT)
    {
        rt_mutex_take(&startup_lock, RT_WAITING_FOREVER);
        ready_mask = startup_ready_mask;
        rt_mutex_release(&startup_lock);
        rt_kprintf("wait modules timeout, %s is not ready\n",
                   startup_missing_name(EVENT_ALL_READY & ~ready_mask));
    }
    else if (result != RT_EOK)
    {
        rt_kprintf("startup event wait failed: %d\n", result);
    }

    if (startup_abort)
        goto finish;

    result = rt_event_recv(&startup_event, EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                           RT_WAITING_FOREVER, &recved);
    if (result == RT_EOK)
    {
        rt_kprintf("all modules are ready\n");
        rt_kprintf("business task started\n");
    }
    else
    {
        rt_kprintf("startup final wait failed: %d\n", result);
    }

finish:
    startup_finish();
}

int startup_demo(void)
{
    rt_err_t result;
    rt_uint32_t index;

    if (startup_running)
    {
        rt_kprintf("startup_demo is already running\n");
        return -RT_EBUSY;
    }

    result = rt_event_init(&startup_event, "stup_evt", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_kprintf("startup event init failed: %d\n", result);
        return result;
    }
    result = rt_mutex_init(&startup_lock, "stup_lk", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_event_detach(&startup_event);
        rt_kprintf("startup mutex init failed: %d\n", result);
        return result;
    }

    startup_ready_mask = 0;
    startup_finished = 0;
    startup_expected = STARTUP_THREAD_COUNT;
    startup_abort = RT_FALSE;
    startup_running = RT_TRUE;

    for (index = 0; index < 3; index++)
    {
        result = rt_thread_init(&startup_threads[index], "initmod",
                                startup_module_entry, &startup_modules[index],
                                startup_stacks[index], STARTUP_STACK_SIZE,
                                17, 5);
        if (result != RT_EOK)
        {
            startup_detach_threads(0, index);
            startup_cleanup();
            rt_kprintf("startup module thread init failed: %d\n", result);
            return result;
        }
    }

    result = rt_thread_init(&startup_threads[3], "business",
                            startup_business_entry, RT_NULL,
                            startup_stacks[3], STARTUP_STACK_SIZE,
                            16, 5);
    if (result != RT_EOK)
    {
        startup_detach_threads(0, 3);
        startup_cleanup();
        rt_kprintf("business thread init failed: %d\n", result);
        return result;
    }

    for (index = 0; index < STARTUP_THREAD_COUNT; index++)
    {
        result = rt_thread_startup(&startup_threads[index]);
        if (result != RT_EOK)
        {
            rt_bool_t cleanup_now;

            rt_mutex_take(&startup_lock, RT_WAITING_FOREVER);
            startup_abort = RT_TRUE;
            startup_expected = index;
            cleanup_now = (startup_finished == startup_expected);
            rt_mutex_release(&startup_lock);
            startup_detach_threads(index, STARTUP_THREAD_COUNT);
            if (cleanup_now)
                startup_cleanup();
            else
                rt_event_send(&startup_event, EVENT_ALL_READY);
            rt_kprintf("startup thread %u startup failed: %d\n", index, result);
            return result;
        }
    }

    rt_kprintf("startup_demo started\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, event startup demo);
