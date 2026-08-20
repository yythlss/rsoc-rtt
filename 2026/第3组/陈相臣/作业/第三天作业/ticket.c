/*
 * 练习二：多窗口售票系统
 * 目标板：星火一号，主控芯片：STM32F407ZG
 *
 * 作业要求：
 * - 两个窗口线程共同出售 20 张票
 * - 使用互斥量保护完整售票过程
 * - 售完后线程自动退出，票号不重复
 * - 命令参数 unsafe 用于对比不加锁时的竞态现象
 */

#include <rtthread.h>

#define TOTAL_TICKETS   20
#define WINDOW_COUNT    2
#define THREAD_STACK    1024
#define THREAD_PRIORITY 25

static rt_mutex_t ticket_mutex;
static rt_sem_t done_sem;
static rt_uint32_t remaining;
static rt_uint8_t use_mutex;
static rt_uint8_t running;

/* 售票窗口线程 */
static void window_entry(void *parameter)
{
    rt_uint32_t window_id = (rt_uint32_t)parameter;
    char window_name = (window_id == 0U) ? 'A' : 'B';

    while (1)
    {
        rt_uint32_t sold_ticket;

        if (use_mutex)
        {
            if (rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
                break;

            if (remaining == 0U)
            {
                rt_mutex_release(ticket_mutex);
                break;
            }

            sold_ticket = remaining;
            remaining--;
            rt_kprintf("window %c sold ticket %u\n", window_name, sold_ticket);
            rt_mutex_release(ticket_mutex);
            rt_thread_mdelay(10);
        }
        else
        {
            /* 不加锁模式：读票和改票之间故意延时，制造竞态 */
            if (remaining == 0U)
                break;

            sold_ticket = remaining;
            rt_thread_mdelay(50);
            remaining = sold_ticket - 1U;
            rt_kprintf("window %c sold ticket %u\n", window_name, sold_ticket);
            rt_thread_mdelay(10);
        }
    }

    rt_sem_release(done_sem);
}

/* MSH 命令：ticket_demo [unsafe] */
static int ticket_demo(int argc, char **argv)
{
    rt_uint32_t i;
    rt_uint32_t started = 0;
    rt_thread_t tid;

    if (running)
    {
        rt_kprintf("ticket_demo is already running\n");
        return -RT_EBUSY;
    }

    use_mutex = 1;
    if (argc >= 2 && rt_strcmp(argv[1], "unsafe") == 0)
    {
        use_mutex = 0;
        rt_kprintf("ticket demo runs without mutex\n");
    }

    running = 1;
    remaining = TOTAL_TICKETS;

    done_sem = rt_sem_create("tk_done", 0, RT_IPC_FLAG_FIFO);
    if (done_sem == RT_NULL)
    {
        rt_kprintf("create ticket done semaphore failed\n");
        goto cleanup;
    }

    if (use_mutex)
    {
        ticket_mutex = rt_mutex_create("tk_mtx", RT_IPC_FLAG_FIFO);
        if (ticket_mutex == RT_NULL)
        {
            rt_kprintf("create ticket mutex failed\n");
            goto cleanup;
        }
    }

    for (i = 0; i < WINDOW_COUNT; i++)
    {
        char name[RT_NAME_MAX];

        rt_snprintf(name, sizeof(name), "win%u", (unsigned int)i);
        tid = rt_thread_create(name, window_entry, (void *)i,
                               THREAD_STACK, THREAD_PRIORITY, 5);
        if (tid == RT_NULL)
        {
            rt_kprintf("create window %u thread failed\n", i);
            continue;
        }

        if (rt_thread_startup(tid) != RT_EOK)
        {
            rt_kprintf("start window %u thread failed\n", i);
            rt_thread_delete(tid);
            continue;
        }

        started++;
    }

    for (i = 0; i < started; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);

    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %u\n", remaining);

cleanup:
    if (ticket_mutex != RT_NULL)
        rt_mutex_delete(ticket_mutex);
    if (done_sem != RT_NULL)
        rt_sem_delete(done_sem);

    ticket_mutex = RT_NULL;
    done_sem = RT_NULL;
    running = 0;

    return 0;
}

MSH_CMD_EXPORT(ticket_demo, ticket demo [unsafe]);
