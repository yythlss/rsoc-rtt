#include <rtthread.h>

#define TICKET_USE_MUTEX   1
#define WINDOW_STACK_SIZE  1024

static rt_mutex_t ticket_mutex = RT_NULL;
static rt_sem_t done_sem = RT_NULL;

static struct rt_thread window_a_thread;
static struct rt_thread window_b_thread;
static rt_uint8_t window_a_stack[WINDOW_STACK_SIZE];
static rt_uint8_t window_b_stack[WINDOW_STACK_SIZE];

static volatile rt_int32_t ticket_count = 20;
static volatile rt_uint8_t demo_running = 0;

static void window_thread_entry(void *parameter)
{
    const char *name = (const char *)parameter;

    while (1)
    {
#if TICKET_USE_MUTEX
        if (ticket_mutex != RT_NULL)
        {
            if (rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
            {
                rt_kprintf("%s failed to take mutex\n", name);
                rt_thread_mdelay(10);
                continue;
            }
        }
#endif

        if (ticket_count <= 0)
        {
#if TICKET_USE_MUTEX
            if (ticket_mutex != RT_NULL)
            {
                rt_mutex_release(ticket_mutex);
            }
#endif
            break;
        }

#if !TICKET_USE_MUTEX
        rt_thread_mdelay(20);
#endif

        rt_kprintf("%s sold ticket %d\n", name, ticket_count);
        ticket_count--;

#if TICKET_USE_MUTEX
        if (ticket_mutex != RT_NULL)
        {
            rt_mutex_release(ticket_mutex);
        }
#endif

        rt_thread_mdelay(50);
    }

    if (done_sem != RT_NULL)
    {
        rt_sem_release(done_sem);
    }

    rt_kprintf("%s exit\n", name);
}

int ticket_demo(int argc, char **argv)
{
    rt_err_t result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (demo_running)
    {
        rt_kprintf("ticket demo is already running\n");
        return -RT_ERROR;
    }

    demo_running = 1;
    ticket_count = 20;

#if TICKET_USE_MUTEX
    ticket_mutex = rt_mutex_create("tktmtx", RT_IPC_FLAG_PRIO);
    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed\n");
        demo_running = 0;
        return -RT_ERROR;
    }
#endif

    done_sem = rt_sem_create("tkdone", 0, RT_IPC_FLAG_PRIO);
    if (done_sem == RT_NULL)
    {
        rt_kprintf("create done semaphore failed\n");
#if TICKET_USE_MUTEX
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
#endif
        demo_running = 0;
        return -RT_ERROR;
    }

    result = rt_thread_init(&window_a_thread,
                            "winA",
                            window_thread_entry,
                            (void *)"window A",
                            window_a_stack,
                            sizeof(window_a_stack),
                            20,
                            10);
    if (result != RT_EOK)
    {
        rt_kprintf("init window A thread failed, err=%d\n", result);
        rt_sem_delete(done_sem);
        done_sem = RT_NULL;
#if TICKET_USE_MUTEX
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
#endif
        demo_running = 0;
        return result;
    }

    result = rt_thread_init(&window_b_thread,
                            "winB",
                            window_thread_entry,
                            (void *)"window B",
                            window_b_stack,
                            sizeof(window_b_stack),
                            20,
                            10);
    if (result != RT_EOK)
    {
        rt_kprintf("init window B thread failed, err=%d\n", result);
        rt_sem_delete(done_sem);
        done_sem = RT_NULL;
#if TICKET_USE_MUTEX
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
#endif
        demo_running = 0;
        return result;
    }

    result = rt_thread_startup(&window_a_thread);
    if (result != RT_EOK)
    {
        rt_kprintf("startup window A thread failed, err=%d\n", result);
        rt_sem_delete(done_sem);
        done_sem = RT_NULL;
#if TICKET_USE_MUTEX
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
#endif
        demo_running = 0;
        return result;
    }

    result = rt_thread_startup(&window_b_thread);
    if (result != RT_EOK)
    {
        rt_kprintf("startup window B thread failed, err=%d\n", result);
        rt_sem_delete(done_sem);
        done_sem = RT_NULL;
#if TICKET_USE_MUTEX
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
#endif
        demo_running = 0;
        return result;
    }

    rt_sem_take(done_sem, RT_WAITING_FOREVER);
    rt_sem_take(done_sem, RT_WAITING_FOREVER);

    rt_sem_delete(done_sem);
    done_sem = RT_NULL;

#if TICKET_USE_MUTEX
    rt_mutex_delete(ticket_mutex);
    ticket_mutex = RT_NULL;
#endif

    demo_running = 0;

    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", ticket_count);

    return RT_EOK;
}

MSH_CMD_EXPORT(ticket_demo, ticket sale demo);
