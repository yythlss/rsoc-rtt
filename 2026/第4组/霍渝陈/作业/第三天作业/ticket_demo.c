/*
 * ============================================================
 * 练习二：多窗口售票系统（互斥量）
 *
 * 场景：两个售票窗口（A/B）同时出售同一批 20 张票，
 *       每次只能由一个窗口修改剩余票数。
 *
 * 设计思路：
 *   1. 全局变量 ticket_count 保存剩余票数（初始 20）
 *   2. 互斥量保护"检查余票 + 读票号 + 修改余票 + 打印"完整过程
 *   3. 两个售票线程循环卖票，卖完（余票 0）自动退出
 *   4. 对比实验：ticket_demo_nolock 命令 = 不加锁 + 读写之间加延时，
 *      用来演示不加锁会出现重复售票
 *
 * MSH 命令：
 *   ticket_demo        带互斥量（正确版）
 *   ticket_demo_nolock 不带互斥量（对比实验，演示竞态）
 * ============================================================
 */

#include <rtthread.h>
#include <rtdevice.h>

#define TOTAL_TICKETS   20                  /* 总票数 */

static rt_mutex_t ticket_mutex = RT_NULL;   /* 售票互斥量 */
static volatile int ticket_count = 0;       /* 剩余票数（全局共享） */
static volatile rt_uint8_t sellers_done = 0;/* 已结束的售票线程数 */
static volatile rt_uint8_t task_running = 0;/* 任务是否在运行（防重复启动） */

/* ------------------------------------------------------------
 * 加锁版售票线程：检查+读+改+打印 整个过程都在互斥量保护内
 * param 传窗口字母（'A' / 'B'）
 * ---------------------------------------------------------- */
static void seller_locked_entry(void *param)
{
    char name = (char)(rt_ubase_t)param;

    while (1)
    {
        /* 进入临界区 */
        rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        if (ticket_count <= 0)              /* 没票了，退出 */
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        int n = ticket_count;               /* 读出当前票号 */
        rt_thread_mdelay(1);                /* 模拟售票耗时（在锁内，安全） */
        ticket_count = n - 1;               /* 修改余票 */
        rt_kprintf("window %c sold ticket %d\n", name, n);

        rt_mutex_release(ticket_mutex);     /* 退出临界区 */
    }

    sellers_done++;
}

/* ------------------------------------------------------------
 * 无锁版售票线程（对比实验）：
 * "读余票"和"修改余票"之间加延时 → 两个窗口可能读到同一张票号
 * ---------------------------------------------------------- */
static void seller_nolock_entry(void *param)
{
    char name = (char)(rt_ubase_t)param;

    while (1)
    {
        int n = ticket_count;               /* ① 读余票 */
        rt_thread_mdelay(2);                /* ② 延时：此时另一个窗口可能也读到了同一个 n */
        if (n <= 0)                         /* ③ 检查余票 */
            break;
        ticket_count = n - 1;               /* ④ 修改余票 */
        rt_kprintf("window %c sold ticket %d\n", name, n);
    }

    sellers_done++;
}

/* ------------------------------------------------------------
 * 控制线程：创建互斥量 + 两个售票线程，等全部结束后清理
 * param = 1 加锁模式 / 0 无锁模式
 * ---------------------------------------------------------- */
static void ticket_ctrl_entry(void *param)
{
    int locked = (int)(rt_ubase_t)param;
    rt_thread_t win_a, win_b;

    ticket_count = TOTAL_TICKETS;
    sellers_done = 0;

    /* 1. 创建互斥量（仅加锁模式需要） */
    if (locked)
    {
        ticket_mutex = rt_mutex_create("ticket_mx", RT_IPC_FLAG_PRIO);
        if (ticket_mutex == RT_NULL)
        {
            rt_kprintf("ticket: create mutex failed\n");
            task_running = 0;
            return;
        }
    }

    /* 2. 创建两个售票线程，检查创建结果 */
    win_a = rt_thread_create("winA", locked ? seller_locked_entry : seller_nolock_entry,
                             (void *)(rt_ubase_t)'A', 2048, 20, 10);
    win_b = rt_thread_create("winB", locked ? seller_locked_entry : seller_nolock_entry,
                             (void *)(rt_ubase_t)'B', 2048, 20, 10);
    if (win_a == RT_NULL || win_b == RT_NULL)
    {
        rt_kprintf("ticket: create seller thread failed\n");
        if (locked && ticket_mutex != RT_NULL)
            rt_mutex_delete(ticket_mutex);
        task_running = 0;
        return;
    }
    rt_thread_startup(win_a);
    rt_thread_startup(win_b);

    /* 3. 等两个窗口都卖完 */
    while (sellers_done < 2)
        rt_thread_mdelay(50);

    /* 4. 收尾：删互斥量、复位状态，允许下次再执行 */
    if (locked && ticket_mutex != RT_NULL)
    {
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
    }
    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", ticket_count);
    task_running = 0;
}

/* ------------------------------------------------------------
 * 启动函数：locked=1 带互斥量 / locked=0 不带
 * ---------------------------------------------------------- */
static void start_ticket_demo(int locked)
{
    rt_thread_t ctrl;

    if (task_running)
    {
        rt_kprintf("ticket_demo is already running\n");
        return;
    }
    task_running = 1;

    ctrl = rt_thread_create("ticket_ctrl", ticket_ctrl_entry,
                            (void *)(rt_ubase_t)locked, 2048, 15, 10);
    if (ctrl == RT_NULL)
    {
        rt_kprintf("ticket_demo: create control thread failed\n");
        task_running = 0;
        return;
    }
    rt_thread_startup(ctrl);
}

/* 两个 MSH 命令入口：加锁版 / 无锁对比版 */
static void ticket_demo(void)
{
    start_ticket_demo(1);
}

static void ticket_demo_nolock(void)
{
    start_ticket_demo(0);
}

/* 导出为 msh 命令 */
MSH_CMD_EXPORT(ticket_demo, ticket demo: 2 windows sell 20 tickets with mutex);
MSH_CMD_EXPORT(ticket_demo_nolock, ticket demo WITHOUT mutex: show race);
