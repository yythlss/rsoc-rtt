/*
 * 练习二：多窗口售票系统（互斥量）
 * 与 ex1/ex3/ex4 互斥，applications/ 下同一时刻只留一个。
 * 对比实验：把 USE_MUTEX 改为 0，观察无保护时的竞争。
 */
#include <rtthread.h>

#define USE_MUTEX 1   /* 对比实验：改为 0，观察无保护时的竞争 */

static rt_int32_t ticket_count = 20;
static rt_mutex_t ticket_mutex = RT_NULL;

static void window_entry(void *param)
{
    const char *name = (const char *)param;
    rt_int32_t sold;
    rt_bool_t  last;

    while (1)
    {
#if USE_MUTEX
        rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);
#endif
        if (ticket_count > 0)
        {
            sold = ticket_count;
#if !USE_MUTEX
            rt_thread_mdelay(10);   /* 读票之后、减票之前留出竞争窗口 */
#endif
            ticket_count--;
            last = (ticket_count == 0);
#if USE_MUTEX
            rt_mutex_release(ticket_mutex);
#endif
            rt_kprintf("window %s sold ticket %d\n", name, sold);
            if (last)
            {
                rt_kprintf("all tickets sold\n");
                rt_kprintf("remaining tickets: %d\n", ticket_count);
            }
            rt_thread_mdelay(50);
        }
        else
        {
#if USE_MUTEX
            rt_mutex_release(ticket_mutex);
#endif
            break;
        }
    }
}

static int tickets_init(void)
{
    rt_thread_t ta, tb;

    ticket_mutex = rt_mutex_create("ticket", RT_IPC_FLAG_FIFO);
    if (ticket_mutex == RT_NULL) return -RT_ENOMEM;

    ta = rt_thread_create("winA", window_entry, (void *)"A", 1024, 15, 5);
    tb = rt_thread_create("winB", window_entry, (void *)"B", 1024, 15, 5);
    if (ta) rt_thread_startup(ta);
    if (tb) rt_thread_startup(tb);
    return RT_EOK;
}
INIT_APP_EXPORT(tickets_init);
