#include <rtthread.h>

#define TICKET_PRINT(...)            \
    do                               \
    {                                \
        rt_enter_critical();         \
        rt_kprintf(__VA_ARGS__);     \
        rt_exit_critical();          \
    } while (0)

#define TICKET_TOTAL  20

static rt_mutex_t ticket_lock = RT_NULL;
static rt_sem_t ticket_done = RT_NULL;
static volatile rt_bool_t ticket_running = RT_FALSE;
static rt_bool_t ticket_safe_mode = RT_FALSE;
static rt_int32_t ticket_remaining = 0;
static rt_uint32_t ticket_sold = 0;

static void ticket_set_running(rt_bool_t running)
{
    rt_base_t level = rt_hw_interrupt_disable();
    ticket_running = running;
    rt_hw_interrupt_enable(level);
}

static rt_bool_t ticket_begin(void)
{
    rt_base_t level;
    rt_bool_t accepted = RT_FALSE;

    level = rt_hw_interrupt_disable();
    if (ticket_running == RT_FALSE)
    {
        ticket_running = RT_TRUE;
        accepted = RT_TRUE;
    }
    rt_hw_interrupt_enable(level);

    return accepted;
}

static void ticket_window_entry(void *parameter)
{
    const char *window = (const char *)parameter;

    while (1)
    {
        if (ticket_safe_mode == RT_TRUE)
        {
            if (rt_mutex_take(ticket_lock, RT_WAITING_FOREVER) != RT_EOK)
            {
                TICKET_PRINT("[ticket] window %s lock failed\n", window);
                break;
            }

            if (ticket_remaining <= 0)
            {
                rt_mutex_release(ticket_lock);
                break;
            }

            /* The whole check-read-update-record transaction is protected. */
            TICKET_PRINT("[ticket/safe] window %s sold ticket %d\n",
                         window, ticket_remaining);
            ticket_remaining--;
            ticket_sold++;
            rt_mutex_release(ticket_lock);
        }
        else
        {
            rt_int32_t observed;

            if (ticket_remaining <= 0)
            {
                break;
            }

            observed = ticket_remaining;
            /* Deliberately widen the race window for the comparison run. */
            rt_thread_mdelay(20);
            ticket_remaining = observed - 1;
            TICKET_PRINT("[ticket/race] window %s sold ticket %d\n",
                         window, observed);
        }

        rt_thread_mdelay(10);
    }

    rt_sem_release(ticket_done);
}

static rt_uint8_t ticket_start_pair(const char *name_a, const char *name_b)
{
    rt_thread_t tid;
    rt_uint8_t started = 0;

    tid = rt_thread_create(name_a, ticket_window_entry, (void *)"A",
                           1024, 18, 5);
    if (tid == RT_NULL)
    {
        TICKET_PRINT("[ticket] create window A failed\n");
    }
    else if (rt_thread_startup(tid) != RT_EOK)
    {
        TICKET_PRINT("[ticket] start window A failed\n");
        rt_thread_delete(tid);
    }
    else
    {
        started++;
    }

    tid = rt_thread_create(name_b, ticket_window_entry, (void *)"B",
                           1024, 18, 5);
    if (tid == RT_NULL)
    {
        TICKET_PRINT("[ticket] create window B failed\n");
    }
    else if (rt_thread_startup(tid) != RT_EOK)
    {
        TICKET_PRINT("[ticket] start window B failed\n");
        rt_thread_delete(tid);
    }
    else
    {
        started++;
    }

    return started;
}

static void ticket_manager_entry(void *parameter)
{
    rt_uint8_t index;
    rt_uint8_t started;

    (void)parameter;
    ticket_done = rt_sem_create("tdone", 0, RT_IPC_FLAG_PRIO);
    if (ticket_done == RT_NULL)
    {
        TICKET_PRINT("[ticket] create completion semaphore failed\n");
        ticket_set_running(RT_FALSE);
        return;
    }

    TICKET_PRINT("[ticket] phase 1: race condition without mutex\n");
    ticket_safe_mode = RT_FALSE;
    ticket_remaining = TICKET_TOTAL;
    ticket_sold = 0;
    started = ticket_start_pair("race_a", "race_b");
    for (index = 0; index < started; index++)
    {
        rt_sem_take(ticket_done, RT_WAITING_FOREVER);
    }
    TICKET_PRINT("[ticket/race] phase ended, remaining=%d; check duplicate IDs above\n",
                 ticket_remaining);

    ticket_lock = rt_mutex_create("tlock", RT_IPC_FLAG_PRIO);
    if (ticket_lock == RT_NULL)
    {
        TICKET_PRINT("[ticket] create mutex failed\n");
        rt_sem_delete(ticket_done);
        ticket_done = RT_NULL;
        ticket_set_running(RT_FALSE);
        return;
    }

    TICKET_PRINT("[ticket] phase 2: complete transaction protected by mutex\n");
    ticket_safe_mode = RT_TRUE;
    ticket_remaining = TICKET_TOTAL;
    ticket_sold = 0;
    started = ticket_start_pair("safe_a", "safe_b");
    for (index = 0; index < started; index++)
    {
        rt_sem_take(ticket_done, RT_WAITING_FOREVER);
    }

    TICKET_PRINT("[ticket/safe] all tickets sold: %u, remaining: %d\n",
                 ticket_sold, ticket_remaining);

    rt_mutex_delete(ticket_lock);
    rt_sem_delete(ticket_done);
    ticket_lock = RT_NULL;
    ticket_done = RT_NULL;
    ticket_set_running(RT_FALSE);
}

static void ticket_demo(void)
{
    rt_thread_t manager;

    if (ticket_begin() == RT_FALSE)
    {
        TICKET_PRINT("[ticket] demo is already running\n");
        return;
    }

    manager = rt_thread_create("t_mgr", ticket_manager_entry, RT_NULL,
                               1536, 16, 5);
    if (manager == RT_NULL)
    {
        TICKET_PRINT("[ticket] create manager failed\n");
        ticket_set_running(RT_FALSE);
        return;
    }

    if (rt_thread_startup(manager) != RT_EOK)
    {
        TICKET_PRINT("[ticket] start manager failed\n");
        rt_thread_delete(manager);
        ticket_set_running(RT_FALSE);
    }
}
MSH_CMD_EXPORT(ticket_demo, compare unsafe and mutex-protected ticket sales);
