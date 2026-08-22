#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>

#define TICKET_COUNT    20
#define THREAD_STACK    1024
#define THREAD_PRIORITY 18

struct window_info
{
    char name;
};

static rt_mutex_t ticket_mutex;
static rt_sem_t window_done_sem;
static struct window_info windows[2] = {{'A'}, {'B'}};
static volatile rt_int32_t tickets_left;
static rt_bool_t use_mutex;
static rt_bool_t sold[TICKET_COUNT + 1];
static rt_uint8_t sold_count;
static rt_uint8_t duplicate_count;
static rt_bool_t ticket_running;

static void record_ticket(rt_int32_t ticket_no)
{
    rt_base_t level = rt_hw_interrupt_disable();

    if (sold[ticket_no])
        duplicate_count++;
    else
        sold[ticket_no] = RT_TRUE;
    sold_count++;

    rt_hw_interrupt_enable(level);
}

static void window_entry(void *parameter)
{
    struct window_info *window = (struct window_info *)parameter;

    while (1)
    {
        rt_int32_t ticket_no;

        if (use_mutex)
            rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        if (tickets_left <= 0)
        {
            if (use_mutex)
                rt_mutex_release(ticket_mutex);
            break;
        }

        ticket_no = tickets_left;
        if (!use_mutex)
            rt_thread_mdelay(10); /* 故意扩大无锁阶段的竞争窗口。 */

        tickets_left--;
        record_ticket(ticket_no);
        rt_kprintf("[%s] window %c sold ticket %d\n",
                   use_mutex ? "mutex" : "no lock",
                   window->name, ticket_no);

        if (use_mutex)
            rt_mutex_release(ticket_mutex);
        rt_thread_mdelay(5);
    }

    rt_sem_release(window_done_sem);
}

static rt_err_t run_ticket_test(rt_bool_t locked)
{
    rt_thread_t tid_a;
    rt_thread_t tid_b;
    rt_uint8_t i;
    rt_uint8_t unique = 0;

    use_mutex = locked;
    tickets_left = TICKET_COUNT;
    sold_count = 0;
    duplicate_count = 0;
    rt_memset(sold, 0, sizeof(sold));

    tid_a = rt_thread_create(locked ? "tklkA" : "tknlA", window_entry,
                             &windows[0], THREAD_STACK, THREAD_PRIORITY, 5);
    tid_b = rt_thread_create(locked ? "tklkB" : "tknlB", window_entry,
                             &windows[1], THREAD_STACK, THREAD_PRIORITY, 5);
    if ((tid_a == RT_NULL) || (tid_b == RT_NULL))
    {
        if (tid_a != RT_NULL)
            rt_thread_delete(tid_a);
        if (tid_b != RT_NULL)
            rt_thread_delete(tid_b);
        rt_kprintf("create ticket window failed\n");
        return -RT_ENOMEM;
    }

    rt_kprintf("ticket test start: %s\n", locked ? "with mutex" : "without mutex");
    rt_thread_startup(tid_a);
    rt_thread_startup(tid_b);
    rt_sem_take(window_done_sem, RT_WAITING_FOREVER);
    rt_sem_take(window_done_sem, RT_WAITING_FOREVER);

    for (i = 1; i <= TICKET_COUNT; i++)
        if (sold[i])
            unique++;

    rt_kprintf("ticket result: sold=%u, unique=%u, duplicate=%u, left=%d\n",
               sold_count, unique, duplicate_count, tickets_left);
    return RT_EOK;
}

static void ticket_task(void *parameter)
{
    RT_UNUSED(parameter);

    ticket_mutex = rt_mutex_create("tkmutex", RT_IPC_FLAG_PRIO);
    window_done_sem = rt_sem_create("tkdone", 0, RT_IPC_FLAG_FIFO);
    if ((ticket_mutex == RT_NULL) || (window_done_sem == RT_NULL))
    {
        rt_kprintf("create ticket IPC failed\n");
        goto cleanup;
    }

    if (run_ticket_test(RT_FALSE) == RT_EOK)
    {
        rt_kprintf("--------------------------------\n");
        run_ticket_test(RT_TRUE);
    }

cleanup:
    if (window_done_sem != RT_NULL)
    {
        rt_sem_delete(window_done_sem);
        window_done_sem = RT_NULL;
    }
    if (ticket_mutex != RT_NULL)
    {
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
    }
    ticket_running = RT_FALSE;
}

int ticket_demo(void)
{
    rt_thread_t tid;

    if (ticket_running)
    {
        rt_kprintf("ticket demo is running\n");
        return -RT_EBUSY;
    }

    ticket_running = RT_TRUE;
    tid = rt_thread_create("tkcoord", ticket_task, RT_NULL,
                           1280, THREAD_PRIORITY - 1, 5);
    if (tid == RT_NULL)
    {
        ticket_running = RT_FALSE;
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, mutex ticket selling demo);
