/*
 * ticket_demo.c - 练习二：多窗口售票系统（互斥量）
 *
 * 场景：两个窗口同时出售同一批 20 张票，用互斥量保护"检查余票→减1"的
 *       完整过程，保证不重复售票、余票不为负。
 *
 * 对比实验：把 USE_MUTEX 改成 0，会看到重复票号或余票错乱。
 *
 * 用法：MSH 中输入 ticket_demo 启动。
 */
#include <rtthread.h>

#define TOTAL_TICKETS 20

/* 1 使用互斥量，0 不使用（用于对比实验，观察竞态） */
#define USE_MUTEX 1

static rt_mutex_t ticket_mutex = RT_NULL;
static int remaining_tickets = TOTAL_TICKETS;

static void window_entry(void *param)
{
    const char *name = (const char *)param;

    while (1)
    {
        int ticket_no = 0;
        int sold = 0;

#if USE_MUTEX
        if (rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
            continue;
#endif

        if (remaining_tickets > 0)
        {
            ticket_no = remaining_tickets;
#if !USE_MUTEX
            rt_thread_mdelay(5);   /* 人为放大读-改窗口，暴露竞态 */
#endif
            remaining_tickets--;
            sold = 1;
        }

#if USE_MUTEX
        rt_mutex_release(ticket_mutex);
#endif

        if (sold)
        {
            rt_kprintf("%s sold ticket %d\n", name, ticket_no);
            if (remaining_tickets == 0)
            {
                rt_kprintf("all tickets sold, remaining tickets: 0\n");
            }
        }
        else
        {
            break;
        }

        rt_thread_mdelay(10);
    }
}

static void ticket_demo(int argc, char **argv)
{
    rt_thread_t ta, tb;

    if (ticket_mutex != RT_NULL)
    {
        rt_kprintf("ticket demo is already running\n");
        return;
    }

    remaining_tickets = TOTAL_TICKETS;

    ticket_mutex = rt_mutex_create("tmutex", RT_IPC_FLAG_PRIO);
    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed\n");
        return;
    }

    ta = rt_thread_create("winA", window_entry, "window A", 1024, 20, 5);
    tb = rt_thread_create("winB", window_entry, "window B", 1024, 20, 5);

    if (ta == RT_NULL)
        rt_kprintf("create window A failed\n");
    else
        rt_thread_startup(ta);

    if (tb == RT_NULL)
        rt_kprintf("create window B failed\n");
    else
        rt_thread_startup(tb);
}
MSH_CMD_EXPORT(ticket_demo, ticket sale demo with mutex);
