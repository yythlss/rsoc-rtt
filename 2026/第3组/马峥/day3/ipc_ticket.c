/*
 * 练习二：多窗口售票系统
 * 机制：互斥量（rt_mutex）+ 对比实验（无锁 / 有锁）
 *
 * 场景：20张票，2个窗口同时出售
 * 原理：互斥量保护"检查-减票-打印"原子操作
 */

#include <rtthread.h>

#define TOTAL_TICKETS  20
#define WINDOW_A       0
#define WINDOW_B       1

/* ========== 对比实验：全局变量 ========== */
static volatile rt_int32_t ticket_count = TOTAL_TICKETS;   /* 剩余票数 */
static volatile rt_int32_t ticket_no    = TOTAL_TICKETS;   /* 当前票号（递减） */
static rt_mutex_t mutex_ticket = RT_NULL;                 /* 互斥量 */

/* ========== 无锁版本（有并发问题） ========== */
static void window_entry_no_lock(void *parameter)
{
    rt_uint32_t window = (rt_uint32_t)parameter;

    while (1)
    {
        /* === 无锁操作：读取 -> 延时（模拟处理） -> 写入 === */
        if (ticket_count <= 0)
            break;

        rt_int32_t cur_count = ticket_count;   /* 读取余票 */
        rt_int32_t cur_no    = ticket_no;      /* 读取票号 */

        rt_thread_mdelay(1);   /* 短暂延时，放大并发冲突窗口 */

        cur_count--;
        cur_no--;

        ticket_count = cur_count;   /* 写入余票 */
        ticket_no    = cur_no;      /* 写入票号 */

        const char *name = (window == WINDOW_A) ? "A" : "B";
        rt_kprintf("[NO LOCK] window %s sold ticket %" RT_DEBUG_NOTAB "d\n", name, cur_no + 1);
    }

    const char *name = (window == WINDOW_A) ? "A" : "B";
    rt_kprintf("[NO LOCK] window %s: no tickets left, exit\n", name);
}

/* ========== 有锁版本（线程安全） ========== */
static void window_entry_with_lock(void *parameter)
{
    rt_uint32_t window = (rt_uint32_t)parameter;

    while (1)
    {
        /* 加锁，保护完整的售票过程 */
        rt_mutex_take(mutex_ticket, RT_WAITING_FOREVER);

        if (ticket_count <= 0)
        {
            rt_mutex_release(mutex_ticket);
            break;
        }

        /* 检查余票 */
        rt_int32_t cur_count = ticket_count;
        rt_int32_t cur_no    = ticket_no;

        /* 模拟处理延时 */
        rt_thread_mdelay(1);

        /* 减票并记录 */
        cur_count--;
        cur_no--;

        ticket_count = cur_count;
        ticket_no    = cur_no;

        const char *name = (window == WINDOW_A) ? "A" : "B";
        rt_kprintf("[WITH LOCK] window %s sold ticket %" RT_DEBUG_NOTAB "d\n", name, cur_no + 1);

        /* 解锁 */
        rt_mutex_release(mutex_ticket);
    }

    const char *name = (window == WINDOW_A) ? "A" : "B";
    rt_kprintf("[WITH LOCK] window %s: no tickets left, exit\n", name);
}

/* ========== 线程控制块 ========== */
static struct rt_thread win_a_thread;
static struct rt_thread win_b_thread;
static rt_uint8_t win_a_stack[1024];
static rt_uint8_t win_b_stack[1024];

/* ========== 无锁测试 ========== */
static int run_no_lock_test(void)
{
    rt_kprintf("\n========== [TEST 1] NO LOCK (race condition) ==========\n");

    ticket_count = TOTAL_TICKETS;
    ticket_no    = TOTAL_TICKETS;

    rt_err_t err;

    err = rt_thread_init(&win_a_thread, "winA_nolock", window_entry_no_lock,
                        (void *)WINDOW_A, win_a_stack, sizeof(win_a_stack), 12, 5);
    if (err != RT_EOK) { rt_kprintf("winA init failed\n"); return -1; }

    err = rt_thread_init(&win_b_thread, "winB_nolock", window_entry_no_lock,
                        (void *)WINDOW_B, win_b_stack, sizeof(win_b_stack), 12, 5);
    if (err != RT_EOK) { rt_kprintf("winB init failed\n"); return -1; }

    rt_thread_startup(&win_a_thread);
    rt_thread_startup(&win_b_thread);

    /* 等待两个窗口都退出 */
    rt_thread_mdelay(3000);

    rt_kprintf("\n[RESULT] final ticket_count=%d (expected 0)\n", ticket_count);
    rt_kprintf("        ticket_no=%d (expected 0)\n", ticket_no);
    if (ticket_count != 0)
        rt_kprintf("[WARNING] ticket_count != 0, race condition detected!\n");
    rt_kprintf("========== NO LOCK TEST END ==========\n\n");

    return 0;
}

/* ========== 有锁测试 ========== */
static int run_with_lock_test(void)
{
    rt_kprintf("========== [TEST 2] WITH LOCK (thread-safe) ==========\n");

    ticket_count = TOTAL_TICKETS;
    ticket_no    = TOTAL_TICKETS;

    /* 创建互斥量 */
    mutex_ticket = rt_mutex_create("mutex_tick", RT_IPC_FLAG_FIFO);
    if (mutex_ticket == RT_NULL)
    {
        rt_kprintf("[ticket_demo] ERROR: mutex create failed\n");
        return -1;
    }

    rt_err_t err;

    /* 使用新线程名，避免与无锁测试冲突 */
    err = rt_thread_init(&win_a_thread, "winA_lock", window_entry_with_lock,
                        (void *)WINDOW_A, win_a_stack, sizeof(win_a_stack), 12, 5);
    if (err != RT_EOK) { rt_kprintf("winA init failed\n"); return -1; }

    err = rt_thread_init(&win_b_thread, "winB_lock", window_entry_with_lock,
                        (void *)WINDOW_B, win_b_stack, sizeof(win_b_stack), 12, 5);
    if (err != RT_EOK) { rt_kprintf("winB init failed\n"); return -1; }

    rt_thread_startup(&win_a_thread);
    rt_thread_startup(&win_b_thread);

    /* 等待两个窗口都退出 */
    rt_thread_mdelay(3000);

    rt_kprintf("\n[RESULT] final ticket_count=%d (expected 0)\n", ticket_count);
    rt_kprintf("        all tickets sold\n");
    RT_ASSERT(ticket_count == 0);
    rt_kprintf("========== WITH LOCK TEST END ==========\n\n");

    rt_mutex_delete(mutex_ticket);
    mutex_ticket = RT_NULL;

    return 0;
}

/* ========== MSH 命令入口 ========== */
static int ticket_demo(void)
{
    /* 检查是否已运行 */
    rt_thread_t t = rt_thread_find("winA_nolock");
    if (t != RT_NULL || rt_thread_find("winA_lock") != RT_NULL)
    {
        rt_kprintf("[ticket_demo] demo already running, skip\n");
        return -1;
    }

    rt_kprintf("[ticket_demo] total tickets: %d\n", TOTAL_TICKETS);

    /* 测试1：无锁（演示并发问题） */
    run_no_lock_test();

    /* 等待片刻 */
    rt_thread_mdelay(500);

    /* 测试2：有锁（线程安全） */
    run_with_lock_test();

    return 0;
}
MSH_CMD_EXPORT(ticket_demo, ticket selling system with mutex (no-lock vs with-lock comparison));
