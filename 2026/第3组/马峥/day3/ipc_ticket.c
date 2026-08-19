/*
 * 练习二：多窗口售票系统
 * 机制：互斥量（rt_mutex）+ 对比实验（无锁 / 有锁）
 *
 * 场景：20张票，2个窗口同时出售
 * 原理：互斥量保护"检查-减票-打印"原子操作
 */

#include <rtthread.h>

#define TOTAL_TICKETS  20
#define WINDOW_A      0
#define WINDOW_B      1

/* ========== 全局变量 ========== */
static volatile rt_int32_t ticket_count = TOTAL_TICKETS;
static volatile rt_int32_t ticket_no    = TOTAL_TICKETS;

/* 互斥量（静态初始化） */
static struct rt_mutex mutex_ticket;

/* ========== 无锁版本（有并发问题） ========== */
static void window_entry_no_lock(void *parameter)
{
    rt_uint32_t window = (rt_uint32_t)parameter;

    while (1)
    {
        if (ticket_count <= 0)
            break;

        rt_int32_t cur_count = ticket_count;
        rt_int32_t cur_no    = ticket_no;

        rt_thread_mdelay(1);   /* 短暂延时，放大并发冲突窗口 */

        cur_count--;
        cur_no--;

        ticket_count = cur_count;
        ticket_no    = cur_no;

        const char *name = (window == WINDOW_A) ? "A" : "B";
        rt_kprintf("[NO LOCK] window %s sold ticket %d\n", name, cur_no + 1);
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
        rt_mutex_take(&mutex_ticket, RT_WAITING_FOREVER);

        if (ticket_count <= 0)
        {
            rt_mutex_release(&mutex_ticket);
            break;
        }

        rt_int32_t cur_count = ticket_count;
        rt_int32_t cur_no    = ticket_no;

        rt_thread_mdelay(1);   /* 模拟处理延时 */

        cur_count--;
        cur_no--;

        ticket_count = cur_count;
        ticket_no    = cur_no;

        const char *name = (window == WINDOW_A) ? "A" : "B";
        rt_kprintf("[WITH LOCK] window %s sold ticket %d\n", name, cur_no + 1);

        rt_mutex_release(&mutex_ticket);
    }

    const char *name = (window == WINDOW_A) ? "A" : "B";
    rt_kprintf("[WITH LOCK] window %s: no tickets left, exit\n", name);
}

/* ========== 线程栈（需对齐） ========== */
#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char win_a_stack[1024];
static char win_b_stack[1024];

static struct rt_thread win_a_thread;
static struct rt_thread win_b_thread;

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

    /* 等待线程退出 */
    rt_thread_mdelay(3000);

    rt_kprintf("\n[RESULT] final ticket_count=%d (expected 0)\n", ticket_count);
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

    /* 初始化互斥量 */
    rt_err_t err = rt_mutex_init(&mutex_ticket, "mutex_tick", RT_IPC_FLAG_PRIO);
    if (err != RT_EOK)
    {
        rt_kprintf("[ticket_demo] ERROR: mutex init failed: %d\n", err);
        return -1;
    }

    /* 使用新线程名，避免与无锁测试冲突 */
    err = rt_thread_init(&win_a_thread, "winA_lock", window_entry_with_lock,
                        (void *)WINDOW_A, win_a_stack, sizeof(win_a_stack), 12, 5);
    if (err != RT_EOK) { rt_kprintf("winA init failed\n"); return -1; }

    err = rt_thread_init(&win_b_thread, "winB_lock", window_entry_with_lock,
                        (void *)WINDOW_B, win_b_stack, sizeof(win_b_stack), 12, 5);
    if (err != RT_EOK) { rt_kprintf("winB init failed\n"); return -1; }

    rt_thread_startup(&win_a_thread);
    rt_thread_startup(&win_b_thread);

    /* 等待线程退出 */
    rt_thread_mdelay(3000);

    rt_kprintf("\n[RESULT] final ticket_count=%d (expected 0)\n", ticket_count);
    rt_kprintf("        all tickets sold\n");
    rt_kprintf("========== WITH LOCK TEST END ==========\n\n");

    rt_mutex_detach(&mutex_ticket);

    return 0;
}

/* ========== MSH 命令入口 ========== */
int ticket_demo(void)
{
    rt_thread_t t;

    t = rt_thread_find("winA_nolock");
    if (t != RT_NULL || rt_thread_find("winA_lock") != RT_NULL)
    {
        rt_kprintf("[ticket_demo] demo already running, skip\n");
        return -1;
    }

    rt_kprintf("[ticket_demo] total tickets: %d\n", TOTAL_TICKETS);

    /* 测试1：无锁 */
    run_no_lock_test();
    rt_thread_mdelay(500);

    /* 测试2：有锁 */
    run_with_lock_test();

    return 0;
}
MSH_CMD_EXPORT(ticket_demo, ticket selling system with mutex (no-lock vs with-lock comparison));
