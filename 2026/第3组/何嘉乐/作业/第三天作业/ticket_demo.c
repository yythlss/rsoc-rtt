#include <rtthread.h>

#define TOTAL_TICKETS       20

#define THREAD_STACK_SIZE   1024
#define THREAD_PRIORITY     20
#define THREAD_TIMESLICE    10

static rt_mutex_t ticket_mutex = RT_NULL;

static rt_int32_t ticket_count = TOTAL_TICKETS;
static rt_uint8_t finished_windows = 0;
static rt_uint8_t demo_running = 0;

static char window_name[2] = {'A', 'B'};


/* 售票线程 */
static void ticket_entry(void *parameter)
{
    char name;
    rt_int32_t ticket_no;
    rt_uint8_t finished;

    name = *(char *)parameter;

    while (1)
    {
        /* 获取互斥量 */
        if (rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
        {
            rt_kprintf("window %c take mutex failed\n", name);
            break;
        }

        /* 检查余票 */
        if (ticket_count <= 0)
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        /* 记录当前票号 */
        ticket_no = ticket_count;

        /* 售出一张票 */
        ticket_count--;

        rt_kprintf("window %c sold ticket %d\n",
                   name,
                   ticket_no);

        /* 释放互斥量 */
        rt_mutex_release(ticket_mutex);

        /* 模拟售票间隔 */
        rt_thread_mdelay(100);
    }

    /* 记录完成窗口数量 */
    rt_enter_critical();

    finished_windows++;
    finished = finished_windows;

    rt_exit_critical();

    /* 最后一个线程负责清理 */
    if (finished == 2)
    {
        rt_kprintf("all tickets sold\n");
        rt_kprintf("remaining tickets: %d\n",
                   ticket_count);

        if (ticket_mutex != RT_NULL)
        {
            rt_mutex_delete(ticket_mutex);
            ticket_mutex = RT_NULL;
        }

        rt_enter_critical();

        ticket_count = TOTAL_TICKETS;
        finished_windows = 0;
        demo_running = 0;

        rt_exit_critical();

        rt_kprintf("ticket demo finished\n");
    }
}


/* 售票实验 */
static void ticket_demo(void)
{
    rt_thread_t tid_a;
    rt_thread_t tid_b;

    /* 防止重复启动 */
    rt_enter_critical();

    if (demo_running)
    {
        rt_exit_critical();

        rt_kprintf("ticket demo is already running\n");
        return;
    }

    demo_running = 1;
    ticket_count = TOTAL_TICKETS;
    finished_windows = 0;

    rt_exit_critical();

    /* 创建互斥量 */
    ticket_mutex = rt_mutex_create("ticket_mtx",
                                   RT_IPC_FLAG_PRIO);

    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed\n");

        demo_running = 0;
        return;
    }

    /* 创建窗口 A 线程 */
    tid_a = rt_thread_create("win_a",
                             ticket_entry,
                             &window_name[0],
                             THREAD_STACK_SIZE,
                             THREAD_PRIORITY,
                             THREAD_TIMESLICE);

    if (tid_a == RT_NULL)
    {
        rt_kprintf("create window A thread failed\n");

        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
        demo_running = 0;

        return;
    }

    /* 创建窗口 B 线程 */
    tid_b = rt_thread_create("win_b",
                             ticket_entry,
                             &window_name[1],
                             THREAD_STACK_SIZE,
                             THREAD_PRIORITY,
                             THREAD_TIMESLICE);

    if (tid_b == RT_NULL)
    {
        rt_kprintf("create window B thread failed\n");

        rt_thread_delete(tid_a);

        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
        demo_running = 0;

        return;
    }

    rt_kprintf("ticket demo start\n");
    rt_kprintf("total tickets: %d\n", TOTAL_TICKETS);

    /* 启动售票线程 */
    rt_thread_startup(tid_a);
    rt_thread_startup(tid_b);
}

MSH_CMD_EXPORT(ticket_demo, ticket mutex demo);
