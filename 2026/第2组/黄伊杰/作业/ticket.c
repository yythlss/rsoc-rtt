/*
 * ticket.c - 练习二：多窗口售票系统（互斥量保护共享计数）
 *
 * 芯片     : STM32F407ZGT6
 * 开发板   : 星火1号 (RT-Thread STM32F407-RT-SPARK)
 * 开发环境 : RT-Thread Studio
 *
 * 场景:
 *   两个售票窗口（线程 A/B）同时出售同一批 20 张车票。每次只能由一个
 *   窗口修改剩余票数，故用互斥量保护完整的"检查余票 -> 读票号 ->
 *   减 1 -> 打印"售票过程。
 *
 * 命令:
 *   - ticket_demo    : 使用互斥量的正确版。
 *   - ticket_race    : 对比实验版：不加互斥量，并在"读取余票"与
 *                       "修改余票"之间插入短暂延时，复现重复售票/余票错误。
 *
 * 完成后两个线程自动退出并主动释放资源，可重复执行命令。
 *
 * 需回答的思考题: 为何只给 ticket_count-- 加锁仍可能不安全（见 README.md）。
 */

#include <rtthread.h>
#include <rtdevice.h>

#define TICKET_TOTAL        20
#define RACE_DELAY_MS       30
#define WINDOW_STACK        1024
#define WINDOW_PRIORITY     20
#define WINDOW_TIMESLICE    10

static rt_int32_t ticket_remaining = TICKET_TOTAL;  /* 全局剩余票数 */
static rt_int32_t ticket_total_sold = 0;
static rt_mutex_t ticket_mutex = RT_NULL;           /* 保护共享计数   */
static rt_sem_t   ticket_done_sem = RT_NULL;        /* 窗口完成信号量  */
static volatile rt_uint8_t ticket_running = 0;      /* 任务运行标志  */

/* 是否为互斥量正确版指示开关 */
static volatile rt_uint8_t ticket_use_mutex = 1;

static void window_entry(void *param)
{
    rt_int32_t window_no = (rt_int32_t)param;
    rt_int32_t ticket_no;

    while (1)
    {
        /* ---- 临界区开始 ---- */
        if (ticket_use_mutex)
        {
            if (rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
                break;
        }

        /* 1. 检查是否还有余票 */
        if (ticket_remaining <= 0)
        {
            if (ticket_remaining == 0 && ticket_total_sold == TICKET_TOTAL)
                rt_kprintf("all tickets sold\nremaining tickets: 0\n");
            ticket_remaining = 0;          /* 归零，确保不小于 0 */
            if (ticket_use_mutex)
                rt_mutex_release(ticket_mutex);
            break;
        }

        /* 2. 读取即将售出的票号（= 当前剩余数） */
        ticket_no = ticket_remaining;

        /* 仅对比实验（ticket_race）插入延时，模拟"读到一半被抢占" */
        if (!ticket_use_mutex)
            rt_thread_mdelay(RACE_DELAY_MS);

        /* 3. 修改余票：减 1 */
        ticket_remaining--;
        ticket_total_sold++;

        /* 4. 打印售票结果 */
        rt_kprintf("window %c sold ticket %d\n", (ticket_no == 0) ? '?' : 'A' + window_no - 1, ticket_no);
        if (!ticket_use_mutex)
            rt_kprintf("  (race) remaining now %d\n", ticket_remaining);

        if (ticket_use_mutex)
            rt_mutex_release(ticket_mutex);
        /* ---- 临界区结束 ---- */

        /* 模拟窗口处理时间，给两窗口交错机会 */
        rt_thread_mdelay(10);
    }

    /* 该窗口退出，通知 ticket_run 本窗口已结束 */
    rt_kprintf("window %c exits\n", 'A' + window_no - 1);
    rt_sem_release(ticket_done_sem);
    return;
}

static int ticket_run(int use_mutex)
{
    rt_thread_t tid[2];
    rt_uint32_t i;

    if (ticket_running)
    {
        rt_kprintf("ticket demo is still running, try later\n");
        return -RT_EBUSY;
    }
    ticket_running = 1;
    ticket_use_mutex   = (use_mutex != 0);
    ticket_remaining   = TICKET_TOTAL;
    ticket_total_sold  = 0;

    /* 窗口完成信号量：两个窗口各 release 一次，ticket_run 据此等待全部退出 */
    ticket_done_sem = rt_sem_create("tick_done", 0, RT_IPC_FLAG_FIFO);
    if (ticket_done_sem == RT_NULL)
    {
        ticket_running = 0;
        rt_kprintf("create done sem failed\n");
        return -RT_ENOMEM;
    }

    if (ticket_use_mutex)
    {
        ticket_mutex = rt_mutex_create("tick_mutex", RT_IPC_FLAG_FIFO);
        if (ticket_mutex == RT_NULL)
        {
            rt_sem_delete(ticket_done_sem);
            ticket_done_sem = RT_NULL;
            ticket_running = 0;
            rt_kprintf("create ticket mutex failed\n");
            return -RT_ENOMEM;
        }
    }

    for (i = 0; i < 2; i++)
    {
        tid[i] = rt_thread_create("win_ticket", window_entry, (void *)(i + 1),
                                  WINDOW_STACK, WINDOW_PRIORITY, WINDOW_TIMESLICE);
        if (tid[i] == RT_NULL)
        {
            rt_kprintf("create window thread failed\n");
            while (i--)
                rt_thread_delete(tid[i]);
            if (ticket_mutex)
            {
                rt_mutex_delete(ticket_mutex);
                ticket_mutex = RT_NULL;
            }
            rt_sem_delete(ticket_done_sem);
            ticket_done_sem = RT_NULL;
            ticket_running = 0;
            return -RT_ENOMEM;
        }
        if (rt_thread_startup(tid[i]) != RT_EOK)
        {
            rt_kprintf("startup window thread failed\n");
            rt_thread_delete(tid[i]);
            if (ticket_mutex)
            {
                rt_mutex_delete(ticket_mutex);
                ticket_mutex = RT_NULL;
            }
            rt_sem_delete(ticket_done_sem);
            ticket_done_sem = RT_NULL;
            ticket_running = 0;
            return -RT_ERROR;
        }
    }

    /* 等待两个窗口线程都退出 */
    for (i = 0; i < 2; i++)
    {
        rt_sem_take(ticket_done_sem, RT_WAITING_FOREVER);
    }

    /* 收尾：清理互斥量并允许重新启动 */
    if (ticket_mutex)
    {
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
    }
    rt_sem_delete(ticket_done_sem);
    ticket_done_sem = RT_NULL;
    ticket_running = 0;
    return RT_EOK;
}

int ticket_demo(void)
{
    rt_kprintf("=== with mutex ===\n");
    rt_kprintf("sale %d tickets by 2 windows\n", TICKET_TOTAL);
    return ticket_run(1);
}
MSH_CMD_EXPORT(ticket_demo, ticket demo: with mutex sell 20 tickets via 2 windows);

/* 对比实验：无互斥量版本，观察数据不一致 */
int ticket_race(void)
{
    rt_kprintf("=== WITHOUT mutex (race condition) ===\n");
    return ticket_run(0);
}
MSH_CMD_EXPORT(ticket_race, ticket demo without mutex: observe duplicate/wrong tickets);