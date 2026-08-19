/*
 * 【RSOC26】第3天作业 - 练习二：多窗口售票系统
 *
 * 场景：两个售票窗口出售 20 张票，使用互斥量保护完整的售票过程。
 * 对比实验：无锁版本 vs 加锁版本。
 */

#include <rtthread.h>

#define TOTAL_TICKETS   20

static int ticket_count = TOTAL_TICKETS;

/* 无锁版本的 ticket_count（对比实验用） */
static int ticket_count_nolock = TOTAL_TICKETS;

static rt_mutex_t ticket_mutex = RT_NULL;

/*
 * 带互斥量的售票线程
 */
static void ticket_window_locked(void *param)
{
    const char *win_name = (const char *)param;
    int sold = 0;

    while (1)
    {
        /* 加锁：保护「读取→修改→记录」整个临界区 */
        if (rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
        {
            rt_kprintf("%s: failed to take mutex\n", win_name);
            continue;
        }

        if (ticket_count <= 0)
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        int my_ticket = ticket_count;
        ticket_count--;
        sold++;

        rt_kprintf("%s sold ticket %d\n", win_name, my_ticket);

        rt_mutex_release(ticket_mutex);

        rt_thread_mdelay(100 + (win_name[0] == 'A' ? 0 : 50));
    }

    rt_kprintf("%s: sold %d tickets, exit\n", win_name, sold);
}

/*
 * 无锁版本（对比实验）
 */
static void ticket_window_nolock(void *param)
{
    const char *win_name = (const char *)param;
    int sold = 0;

    while (1)
    {
        /* 故意在「读取」和「修改」之间制造竞态窗口 */
        int my_ticket = ticket_count_nolock;      /* 读取 */
        rt_thread_mdelay(5);                       /* 等另一个线程也走到这里 */
        if (my_ticket <= 0) break;
        ticket_count_nolock = my_ticket - 1;       /* 修改 */

        rt_kprintf("[NO LOCK] %s sold ticket %d\n", win_name, my_ticket);
        sold++;
        rt_thread_mdelay(10);
    }

    rt_kprintf("[NO LOCK] %s: sold %d tickets\n", win_name, sold);
}

static void ticket_demo(int argc, char **argv)
{
    rt_thread_t tid_a, tid_b;

    if (ticket_mutex != RT_NULL)
    {
        rt_kprintf("ticket_demo already running\n");
        return;
    }

    /* 创建互斥量 */
    ticket_mutex = rt_mutex_create("ticket_mtx", RT_IPC_FLAG_PRIO);
    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("Failed to create mutex\n");
        return;
    }

    /* 重置计数器 */
    ticket_count = TOTAL_TICKETS;

    rt_kprintf("=== Ticket Demo: %d tickets, 2 windows ===\n", TOTAL_TICKETS);

    tid_a = rt_thread_create("winA", ticket_window_locked, (void *)"window A",
                             1024, 25, 20);
    tid_b = rt_thread_create("winB", ticket_window_locked, (void *)"window B",
                             1024, 25, 20);

    if (tid_a) rt_thread_startup(tid_a);
    if (tid_b) rt_thread_startup(tid_b);

    /* 等待两个线程结束再打印最终票数 */
    rt_thread_mdelay(3000);
    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", ticket_count);

    /* 清理 */
    rt_mutex_delete(ticket_mutex);
    ticket_mutex = RT_NULL;
}

MSH_CMD_EXPORT(ticket_demo, multi-window ticketing with mutex);

/*
 * 思考题答案：
 *
 * Q: 为什么只给 ticket_count-- 加锁仍可能不安全？
 *
 * A: 票务业务的临界区是「读取余票 + 判断是否 > 0 + 减 1 + 记录票号」
 * 四步的原子组合。只给 -- 加锁的话：
 *
 *     线程 A: 读取 count=1（看起来有票）
 *     线程 B: 读取 count=1（也看起来有票）
 *     线程 A: -- → count=0（加锁保护此步）
 *     线程 B: -- → count=-1（加锁保护此步，但逻辑已经错了）
 *
 * 两个线程都认为自己卖了最后一张票，最终结果为 -1。
 * 完整的「if-读-写-记」四步必须在一个互斥量保护下完成。
 */
