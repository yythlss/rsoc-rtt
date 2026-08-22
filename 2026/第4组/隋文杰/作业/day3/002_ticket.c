#include <rtthread.h>

#define TOTAL_TICKETS  20
#define STACK_SIZE     512
#define PRIORITY       10

/* 剩余票数 */
static rt_int32_t remaining_tickets = TOTAL_TICKETS;

/* 互斥锁 */
static rt_mutex_t ticket_mutex = RT_NULL;

/* 售票线程入口 */
static void ticket_window_entry(void *param)
{
    const char *window_name = (const char *)param;
    rt_int32_t ticket_no = 0;

    while (1)
    {
        /* 获取互斥锁 */
        rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        /* 临界区：检查并卖票 */
        if (remaining_tickets > 0)
        {
            ticket_no = remaining_tickets;
            remaining_tickets--;
            rt_kprintf("%s sold ticket %d\n", window_name, ticket_no);
        }
        else
        {
            /* 票卖完了，先释放锁再退出 */
            rt_mutex_release(ticket_mutex);
            break;
        }

        /* 释放互斥锁 */
        rt_mutex_release(ticket_mutex);

        /* 模拟售票间隔 */
        rt_thread_mdelay(10);
    }

    rt_kprintf("%s closed.\n", window_name);
}

int multi_window_ticket_system(void)
{
    rt_thread_t tid_a, tid_b;

    /* 防重复运行：如果锁已存在先清理 */
    if (ticket_mutex != RT_NULL)
    {
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
    }

    /* 1. 创建互斥锁 */
    ticket_mutex = rt_mutex_create("mtx", RT_IPC_FLAG_FIFO);
    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("Failed to create mutex!\n");
        return -1;
    }

    /* 2. 重置票数 */
    remaining_tickets = TOTAL_TICKETS;

    /* 3. 创建窗口 A */
    tid_a = rt_thread_create("winA",
                              ticket_window_entry,
                              (void *)"Window A",
                              STACK_SIZE,
                              PRIORITY,
                              10);
    if (tid_a != RT_NULL)
        rt_thread_startup(tid_a);

    /* 4. 创建窗口 B */
    tid_b = rt_thread_create("winB",
                              ticket_window_entry,
                              (void *)"Window B",
                              STACK_SIZE,
                              PRIORITY,
                              10);
    if (tid_b != RT_NULL)
        rt_thread_startup(tid_b);

    /* 5. 等待窗口把票卖完 */
    rt_thread_mdelay(500);

    /* 6. 清理资源（线程自然退出，无需手动 delete） */
    rt_mutex_delete(ticket_mutex);
    ticket_mutex = RT_NULL;

    rt_kprintf("All tickets sold out!\n");
    return 0;
}


MSH_CMD_EXPORT(ticket_demo, ticket_demo);
int main(void)
{
    return 0;
}

