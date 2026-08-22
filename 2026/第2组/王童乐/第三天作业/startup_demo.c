#include <rtthread.h>
#include <finsh.h>

#define EVENT_NET_READY     (1U << 0)
#define EVENT_SENSOR_READY  (1U << 1)
#define EVENT_STORAGE_READY (1U << 2)
#define EVENT_ALL_READY     (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

struct startup_context
{
    rt_event_t event;
    volatile rt_uint32_t ready_mask;
    rt_bool_t running;
    rt_thread_t threads[4];
};

struct startup_arg
{
    rt_uint32_t bit;
    rt_uint32_t delay_ms;
    const char *name;
};

static struct startup_context startup;
static struct startup_arg startup_args[3] = {
    {EVENT_NET_READY, 1000, "network"},
    {EVENT_SENSOR_READY, 2000, "sensor"},
    {EVENT_STORAGE_READY, 5000, "storage"}
};

static void startup_module_worker(void *parameter)
{
    struct startup_arg *arg = (struct startup_arg *)parameter;

    rt_thread_mdelay(arg->delay_ms);
    rt_enter_critical();
    startup.ready_mask |= arg->bit;
    rt_exit_critical();
    if (rt_event_send(startup.event, arg->bit) != RT_EOK)
        rt_kprintf("%s event send failed\n", arg->name);
    else
        rt_kprintf("%s ready\n", arg->name);
    rt_thread_delete(rt_thread_self());
}

static void startup_business_worker(void *parameter)
{
    rt_uint32_t received = 0;
    rt_uint32_t missing;

    (void)parameter;
    if (rt_event_recv(startup.event, EVENT_ALL_READY,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                      RT_TICK_PER_SECOND * 3, &received) != RT_EOK)
    {
        missing = EVENT_ALL_READY & ~startup.ready_mask;
        rt_kprintf("wait modules timeout, missing:%s%s%s\n",
                   (missing & EVENT_NET_READY) ? " network" : "",
                   (missing & EVENT_SENSOR_READY) ? " sensor" : "",
                   (missing & EVENT_STORAGE_READY) ? " storage" : "");

        if (rt_event_recv(startup.event, EVENT_ALL_READY,
                          RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                          RT_WAITING_FOREVER, &received) != RT_EOK)
        {
            rt_kprintf("startup_demo: event wait failed\n");
            rt_event_delete(startup.event);
            startup.running = RT_FALSE;
            rt_thread_delete(rt_thread_self());
            return;
        }
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");
    rt_event_delete(startup.event);
    startup.running = RT_FALSE;
    rt_thread_delete(rt_thread_self());
}

static void startup_demo(void)
{
    rt_uint32_t i;

    if (startup.running)
    {
        rt_kprintf("startup_demo is already running\n");
        return;
    }

    startup.event = rt_event_create("startup_evt", RT_IPC_FLAG_FIFO);
    if (startup.event == RT_NULL)
    {
        rt_kprintf("startup_demo: event create failed\n");
        return;
    }
    startup.ready_mask = 0;
    startup.running = RT_TRUE;

    for (i = 0; i < 3; i++)
    {
        startup.threads[i] = rt_thread_create(startup_args[i].name,
                                              startup_module_worker,
                                              &startup_args[i], 768, 16, 10);
        if (startup.threads[i] == RT_NULL)
        {
            rt_uint32_t j;
            startup.running = RT_FALSE;
            for (j = 0; j < i; j++)
                rt_thread_delete(startup.threads[j]);
            rt_event_delete(startup.event);
            rt_kprintf("startup_demo: module thread %u create failed\n", i);
            return;
        }
    }

    startup.threads[3] = rt_thread_create("business", startup_business_worker,
                                          RT_NULL, 1024, 15, 10);
    if (startup.threads[3] == RT_NULL)
    {
        startup.running = RT_FALSE;
        for (i = 0; i < 3; i++)
            rt_thread_delete(startup.threads[i]);
        rt_event_delete(startup.event);
        rt_kprintf("startup_demo: business thread create failed\n");
        return;
    }

    for (i = 0; i < 4; i++)
        rt_thread_startup(startup.threads[i]);
    rt_kprintf("startup_demo started; storage intentionally takes 5 seconds\n");
}
MSH_CMD_EXPORT(startup_demo, event based startup condition demo);
