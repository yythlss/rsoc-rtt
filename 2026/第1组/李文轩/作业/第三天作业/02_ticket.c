/*
 * 练习二：多窗口售票系统（互斥量）
 *
 * 场景：窗口 A、窗口 B 同时出售同一批 20 张车票（票号从 20 递减到 1）。
 * 使用互斥量保护"检查余票 -> 读取票号 -> 修改余票"的完整售票过程，
 * 保证票号不重复、余票不为负，20 张票全部售出后两个窗口自动退出。
 *
 * MSH 命令：ticket_demo
 *
 * 对比实验：把 TICKET_USE_MUTEX 改为 0 重新编译，可复现不加互斥量时的
 *          重复售票 / 余票负数问题（TICKET_RACE_DELAY_MS 会放大竞争窗口）。
 *
 * 思考题：为什么只给 ticket_count-- 加锁仍可能不安全？
 * 答：售票的完整操作是"检查余票 -> 读票号 -> 减一"，三者必须作为一个原子
 *     临界区。如果只对 `ticket_count--` 加锁，两个线程可能同时通过
 *     `if (ticket_count > 0)` 检查（此时只剩 1 张票），然后各自执行减一，
 *     导致同一张票被卖出两次、余票变为 -1。
 */

#include <rtthread.h>

#define TICKET_TOTAL      20  /* 车票总数 */

/* 对比实验开关：
 *   1 = 使用互斥量保护完整售票过程（正确版本，默认）
 *   0 = 不加互斥量，观察重复售票 / 余票错误 */
#define TICKET_USE_MUTEX  1

/* 读取余票与修改余票之间的延时（ms），用于无锁实验放大竞争窗口 */
#if TICKET_USE_MUTEX
#define TICKET_RACE_DELAY_MS  1
#else
#define TICKET_RACE_DELAY_MS  5
#endif

static rt_mutex_t s_ticket_mtx = RT_NULL; /* 售票互斥量 */
static rt_sem_t s_done_sem = RT_NULL;     /* 窗口完成计数信号量 */
static volatile int s_remaining = 0;      /* 剩余票数（全局共享） */
static volatile rt_bool_t s_running = RT_FALSE; /* 防重复启动 */

/* 出售一张票：成功返回票号，售完返回 0 */
static int ticket_sell_one(void)
{
    int id = 0;

#if TICKET_USE_MUTEX
    rt_mutex_take(s_ticket_mtx, RT_WAITING_FOREVER);
#endif

    if (s_remaining > 0)
    {
        id = s_remaining;                          /* 检查余票并读取票号 */
        rt_thread_mdelay(TICKET_RACE_DELAY_MS);    /* 读取与修改之间的延时 */
        s_remaining = s_remaining - 1;             /* 修改余票 */
    }

#if TICKET_USE_MUTEX
    rt_mutex_release(s_ticket_mtx);
#endif

    return id;
}

/* 售票窗口线程：循环售票，售完自动退出 */
static void window_thread(void *param)
{
    rt_ubase_t win = (rt_ubase_t)param;

    while (RT_TRUE)
    {
        int id = ticket_sell_one();
        if (id == 0)
        {
            break; /* 无票可售，退出 */
        }
        rt_kprintf("window %c sold ticket %d\n", (char)win, id);
        rt_thread_mdelay(2); /* 出票间隔 */
    }

    /* 通知命令线程：本窗口已结束 */
    rt_sem_release(s_done_sem);
}

/* MSH 命令：ticket_demo */
static int ticket_demo(int argc, char *argv[])
{
    rt_thread_t win_a = RT_NULL;
    rt_thread_t win_b = RT_NULL;

    if (s_running)
    {
        rt_kprintf("ticket_demo is already running\n");
        return -1;
    }
    s_running = RT_TRUE;
    s_remaining = TICKET_TOTAL;

    /* 1. 创建互斥量 */
    s_ticket_mtx = rt_mutex_create("tick_mtx", RT_IPC_FLAG_FIFO);
    if (s_ticket_mtx == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed\n");
        s_running = RT_FALSE;
        return -1;
    }

    /* 2. 创建完成计数信号量 */
    s_done_sem = rt_sem_create("tk_done", 0, RT_IPC_FLAG_FIFO);
    if (s_done_sem == RT_NULL)
    {
        rt_kprintf("create done semaphore failed\n");
        rt_mutex_delete(s_ticket_mtx);
        s_ticket_mtx = RT_NULL;
        s_running = RT_FALSE;
        return -1;
    }

    /* 3. 创建两个售票窗口线程并检查创建结果 */
    win_a = rt_thread_create("win_a", window_thread, (void *)(rt_ubase_t)'A',
                             1024, 8, 10);
    win_b = rt_thread_create("win_b", window_thread, (void *)(rt_ubase_t)'B',
                             1024, 8, 10);
    if (win_a == RT_NULL || win_b == RT_NULL)
    {
        rt_kprintf("create window thread failed\n");
        if (win_a != RT_NULL) rt_thread_delete(win_a);
        if (win_b != RT_NULL) rt_thread_delete(win_b);
        rt_mutex_delete(s_ticket_mtx);
        rt_sem_delete(s_done_sem);
        s_ticket_mtx = RT_NULL;
        s_done_sem = RT_NULL;
        s_running = RT_FALSE;
        return -1;
    }

    /* 4. 启动两个窗口线程 */
    rt_thread_startup(win_a);
    rt_thread_startup(win_b);

    /* 5. 等待两个窗口售完退出 */
    if (rt_sem_take(s_done_sem, 20000) != RT_EOK)
    {
        rt_kprintf("ticket_demo wait timeout\n");
    }
    if (rt_sem_take(s_done_sem, 20000) != RT_EOK)
    {
        rt_kprintf("ticket_demo wait timeout\n");
    }

    /* 6. 打印结果并释放资源 */
    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", s_remaining);

    rt_mutex_delete(s_ticket_mtx);
    rt_sem_delete(s_done_sem);
    s_ticket_mtx = RT_NULL;
    s_done_sem = RT_NULL;
    s_running = RT_FALSE;

    return 0;
}
MSH_CMD_EXPORT(ticket_demo, multi-window ticket selling with mutex);
