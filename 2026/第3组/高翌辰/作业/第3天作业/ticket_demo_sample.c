/*
 * 程序清单：多窗口售票系统
 *
 * ticket_demo_nolock：不使用互斥量，观察重复售票。
 * ticket_demo：使用互斥量，保证每张票只售出一次。
 */
#include <rtthread.h>

#define TICKET_COUNT            20
#define WINDOW_COUNT            2
#define WINDOW_THREAD_PRIORITY  20
#define WINDOW_THREAD_TIMESLICE 5

struct ticket_window
{
    char name;
    struct rt_thread thread;
    char stack[1024];
};

static rt_mutex_t ticket_mutex = RT_NULL;
static rt_int16_t remaining_tickets;
static rt_uint8_t windows_finished;
static rt_uint8_t windows_started;
static rt_bool_t use_mutex;
static rt_bool_t demo_running;
static struct ticket_window windows[WINDOW_COUNT] =
{
    {'A'}, {'B'}
};

static void ticket_demo_cleanup(void)
{
    if (ticket_mutex != RT_NULL)
    {
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
    }
    demo_running = RT_FALSE;
}

static void ticket_window_entry(void *parameter)
{
    struct ticket_window *window = parameter;
    rt_int16_t ticket_no;
    rt_bool_t is_last = RT_FALSE;

    while (1)
    {
        if (use_mutex)
        {
            rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);
            if (remaining_tickets <= 0)
            {
                rt_mutex_release(ticket_mutex);
                break;
            }

            ticket_no = remaining_tickets--;
            rt_kprintf("window %c sold ticket %d\n", window->name, ticket_no);
            rt_mutex_release(ticket_mutex);
        }
        else
        {
            /* 故意将读、改操作分开，便于观察无互斥量时的竞争。 */
            ticket_no = remaining_tickets;
            if (ticket_no <= 0)
            {
                break;
            }
            rt_thread_mdelay(5);
            remaining_tickets = ticket_no - 1;
            rt_kprintf("window %c sold ticket %d\n", window->name, ticket_no);
        }

        rt_thread_mdelay(1);
    }

    rt_enter_critical();
    if (++windows_finished == windows_started)
    {
        is_last = RT_TRUE;
    }
    rt_exit_critical();

    if (is_last)
    {
        if (use_mutex)
        {
            rt_kprintf("all tickets sold\n");
        }
        else
        {
            rt_kprintf("no-lock experiment finished\n");
        }
        rt_kprintf("remaining tickets: %d\n", remaining_tickets);
        ticket_demo_cleanup();
    }
}

static int ticket_demo_start(rt_bool_t enable_mutex)
{
    rt_uint8_t i;
    rt_err_t result;

    if (demo_running)
    {
        rt_kprintf("ticket demo is already running.\n");
        return -RT_EBUSY;
    }

    demo_running = RT_TRUE;
    use_mutex = enable_mutex;
    remaining_tickets = TICKET_COUNT;
    windows_finished = 0;
    windows_started = 0;

    if (use_mutex)
    {
        ticket_mutex = rt_mutex_create("ticket", RT_IPC_FLAG_PRIO);
        if (ticket_mutex == RT_NULL)
        {
            rt_kprintf("create ticket mutex failed.\n");
            demo_running = RT_FALSE;
            return -RT_ENOMEM;
        }
    }

    /* 先完成两个窗口线程的初始化，再启动售票。 */
    for (i = 0; i < WINDOW_COUNT; i++)
    {
        result = rt_thread_init(&windows[i].thread,
                                windows[i].name == 'A' ? "winA" : "winB",
                                ticket_window_entry,
                                &windows[i],
                                windows[i].stack,
                                sizeof(windows[i].stack),
                                WINDOW_THREAD_PRIORITY,
                                WINDOW_THREAD_TIMESLICE);
        if (result != RT_EOK)
        {
            rt_kprintf("create window %c thread failed: %d\n", windows[i].name, result);
            while (i > 0)
            {
                rt_thread_detach(&windows[--i].thread);
            }
            ticket_demo_cleanup();
            return result;
        }
    }

    for (i = 0; i < WINDOW_COUNT; i++)
    {
        result = rt_thread_startup(&windows[i].thread);
        if (result != RT_EOK)
        {
            rt_kprintf("start window %c thread failed: %d\n", windows[i].name, result);
            for (; i < WINDOW_COUNT; i++)
            {
                rt_thread_detach(&windows[i].thread);
            }
            if (windows_started == 0)
            {
                ticket_demo_cleanup();
            }
            return result;
        }
        windows_started++;
    }

    return RT_EOK;
}

int ticket_demo_nolock(void)
{
    return ticket_demo_start(RT_FALSE);
}
MSH_CMD_EXPORT(ticket_demo_nolock, ticket demo without mutex);

int ticket_demo(void)
{
    return ticket_demo_start(RT_TRUE);
}
MSH_CMD_EXPORT(ticket_demo, ticket demo with mutex);
