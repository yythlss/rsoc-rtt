#include <rtthread.h>

#define TOTAL_TICKETS       20

#define THREAD_STACK_SIZE   1024
#define THREAD_PRIORITY     20
#define THREAD_TIMESLICE    10

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
        /* 检查余票 */
        if (ticket_count <= 0)
        {
            break;
        }

        /* 读取当前票号 */
        ticket_no = ticket_count;

        /* 放大线程竞争 */
        rt_thread_mdelay(50);

        /* 修改余票 */
        ticket_count--;

        rt_kprintf("window %c sold ticket %d\n",
                   name,
                   ticket_no);

        /* 模拟售票间隔 */
        rt_thread_mdelay(100);
    }

    /* 记录完成窗口数量 */
    rt_enter_critical();

    finished_windows++;
    finished = finished_windows;

    rt_exit_critical();

    /* 最后一个线程负责结束实验 */
    if (finished == 2)
    {
        rt_kprintf("all tickets sold\n");
        rt_kprintf("remaining tickets: %d\n",
                   ticket_count);

        rt_enter_critical();

        ticket_count = TOTAL_TICKETS;
        finished_windows = 0;
        demo_running = 0;

        rt_exit_critical();

        rt_kprintf("ticket differ demo finished\n");
    }
}


/* 无互斥量售票实验 */
static void ticketdiffer_demo(void)
{
    rt_thread_t tid_a;
    rt_thread_t tid_b;

    /* 防止重复启动 */
    rt_enter_critical();

    if (demo_running)
    {
        rt_exit_critical();

        rt_kprintf("ticket differ demo is already running\n");
        return;
    }

    demo_running = 1;
    ticket_count = TOTAL_TICKETS;
    finished_windows = 0;

    rt_exit_critical();

    /* 创建窗口 A 线程 */
    tid_a = rt_thread_create("diff_a",
                             ticket_entry,
                             &window_name[0],
                             THREAD_STACK_SIZE,
                             THREAD_PRIORITY,
                             THREAD_TIMESLICE);

    if (tid_a == RT_NULL)
    {
        rt_kprintf("create window A thread failed\n");

        demo_running = 0;
        return;
    }

    /* 创建窗口 B 线程 */
    tid_b = rt_thread_create("diff_b",
                             ticket_entry,
                             &window_name[1],
                             THREAD_STACK_SIZE,
                             THREAD_PRIORITY,
                             THREAD_TIMESLICE);

    if (tid_b == RT_NULL)
    {
        rt_kprintf("create window B thread failed\n");

        rt_thread_delete(tid_a);

        demo_running = 0;
        return;
    }

    rt_kprintf("ticket differ demo start\n");
    rt_kprintf("total tickets: %d\n", TOTAL_TICKETS);
    rt_kprintf("mutex: disabled\n");

    /* 启动售票线程 */
    rt_thread_startup(tid_a);
    rt_thread_startup(tid_b);
}

MSH_CMD_EXPORT(ticketdiffer_demo, ticket race condition demo);
