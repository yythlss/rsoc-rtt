#include <rtthread.h>
#include <finsh.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

#define THREAD_STACK         1024
#define THREAD_PRIORITY      18

struct module_info
{
    const char *name;
    rt_uint32_t event;
    rt_uint32_t delay_ms;
};

static rt_event_t ready_event;
static rt_sem_t startup_done_sem;
static rt_bool_t startup_running;
static struct module_info modules[3] =
{
    {"network", EVENT_NET_READY, 800},
    {"sensor", EVENT_SENSOR_READY, 1400},
    {"storage", EVENT_STORAGE_READY, 5000}
};

static void module_entry(void *parameter)
{
    struct module_info *module = (struct module_info *)parameter;

    rt_thread_mdelay(module->delay_ms);
    rt_kprintf("%s ready\n", module->name);
    rt_event_send(ready_event, module->event);
    rt_sem_release(startup_done_sem);
}

static void business_entry(void *parameter)
{
    rt_uint32_t received;
    rt_uint32_t current = 0;

    RT_UNUSED(parameter);

    if (rt_event_recv(ready_event, EVENT_ALL_READY, RT_EVENT_FLAG_AND,
                      rt_tick_from_millisecond(3000), &received) != RT_EOK)
    {
        rt_event_recv(ready_event, EVENT_ALL_READY, RT_EVENT_FLAG_OR, 0, &current);
        rt_kprintf("wait modules timeout:");
        if (!(current & EVENT_NET_READY))
            rt_kprintf(" network");
        if (!(current & EVENT_SENSOR_READY))
            rt_kprintf(" sensor");
        if (!(current & EVENT_STORAGE_READY))
            rt_kprintf(" storage");
        rt_kprintf(" is not ready\n");

        rt_event_recv(ready_event, EVENT_ALL_READY, RT_EVENT_FLAG_AND,
                      RT_WAITING_FOREVER, &received);
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");
    rt_sem_release(startup_done_sem);
}

static void startup_task(void *parameter)
{
    static const char *const names[4] = {"stnet", "stsens", "ststore", "stbiz"};
    rt_thread_t tid[4] = {RT_NULL};
    rt_uint8_t i;

    RT_UNUSED(parameter);

    ready_event = rt_event_create("stevent", RT_IPC_FLAG_FIFO);
    startup_done_sem = rt_sem_create("stdone", 0, RT_IPC_FLAG_FIFO);
    if ((ready_event == RT_NULL) || (startup_done_sem == RT_NULL))
    {
        rt_kprintf("create startup IPC failed\n");
        goto cleanup;
    }

    for (i = 0; i < 3; i++)
    {
        tid[i] = rt_thread_create(names[i], module_entry, &modules[i],
                                  THREAD_STACK, THREAD_PRIORITY, 5);
        if (tid[i] == RT_NULL)
            break;
    }
    if (i == 3)
    {
        tid[3] = rt_thread_create(names[3], business_entry, RT_NULL,
                                  THREAD_STACK, THREAD_PRIORITY, 5);
        if (tid[3] == RT_NULL)
            i = 3;
        else
            i = 4;
    }

    if (i != 4)
    {
        rt_kprintf("create startup thread failed\n");
        while (i > 0)
            rt_thread_delete(tid[--i]);
        goto cleanup;
    }

    rt_kprintf("startup check begin\n");
    for (i = 0; i < 4; i++)
        rt_thread_startup(tid[i]);
    for (i = 0; i < 4; i++)
        rt_sem_take(startup_done_sem, RT_WAITING_FOREVER);

cleanup:
    if (startup_done_sem != RT_NULL)
    {
        rt_sem_delete(startup_done_sem);
        startup_done_sem = RT_NULL;
    }
    if (ready_event != RT_NULL)
    {
        rt_event_delete(ready_event);
        ready_event = RT_NULL;
    }
    startup_running = RT_FALSE;
}

int startup_demo(void)
{
    rt_thread_t tid;

    if (startup_running)
    {
        rt_kprintf("startup demo is running\n");
        return -RT_EBUSY;
    }

    startup_running = RT_TRUE;
    tid = rt_thread_create("stcoord", startup_task, RT_NULL,
                           1280, THREAD_PRIORITY - 1, 5);
    if (tid == RT_NULL)
    {
        startup_running = RT_FALSE;
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, event set startup demo);
