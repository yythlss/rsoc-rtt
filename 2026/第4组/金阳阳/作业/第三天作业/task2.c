/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     Student      Multi-window ticket sale system
 *
 * 练习二：多窗口售票系统
 *
 * MSH 命令:
 *   ticket_demo           使用互斥量保护售票过程（正常模式）
 *   ticket_demo nomutex   对比实验：不加互斥量，且在读取余票与修改余票
 *                         之间插入延时，观察重复售票 / 余票统计错误
 */

#include <rtthread.h>

#define DBG_TAG "ticket"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ============ 售票系统配置 ============ */
#define TOTAL_TICKETS       20                /* 总票数 */
#define THREAD_PRIORITY     15                /* 线程优先级 */
#define THREAD_STACK_SIZE   512               /* 线程栈大小 */
#define TICKET_INTERVAL_MS  20                /* 售票间隔(ms) */
#define RACE_DELAY_MS       1                 /* 无互斥量模式下"读-改"之间的延时(ms) */

/* 售出记录数组容量：正常模式最多 20 条；无互斥量模式可能出现重复售票，
 * 记录条数可能超过总票数，容量翻倍并做越界保护 */
#define MAX_SOLD_RECORDS    (TOTAL_TICKETS * 2)

/* ============ 事件位定义 ============ */
#define EVENT_WIN_A_DONE    (1U << 0)
#define EVENT_WIN_B_DONE    (1U << 1)
#define EVENT_ALL_DONE      (EVENT_WIN_A_DONE | EVENT_WIN_B_DONE)

/* ============ 全局变量 ============ */
static rt_mutex_t g_mutex = RT_NULL;                  /* 互斥量，保护完整售票过程 */
static rt_event_t g_event = RT_NULL;                  /* 事件集，同步两窗口完成 */
static volatile int g_running = 0;                    /* 售票系统运行标志 */
static volatile int g_with_mutex = 1;                 /* 是否使用互斥量(1: 是, 0: 对比实验) */
static volatile int g_remaining = 0;                  /* 剩余票数 */
static volatile int g_sold[MAX_SOLD_RECORDS];         /* 售出票号记录 */
static volatile int g_sold_count = 0;                 /* 已售票数 */

/* ============ 售票窗口线程参数 ============ */
typedef struct
{
    const char *name;          /* 窗口名 */
    rt_uint32_t done_flag;     /* 本窗口完成事件位 */
} window_arg_t;

static window_arg_t g_window_a = { "window A", EVENT_WIN_A_DONE };
static window_arg_t g_window_b = { "window B", EVENT_WIN_B_DONE };

/*
 * 售票窗口线程入口
 *
 * 使用互斥量模式：互斥量保护"检查余票 -> 减1 -> 记录票号"完整事务，
 * 保证不重复售票、余票数不会小于 0。
 *
 * 对比实验模式(无互斥量)：在读取余票与修改余票之间插入人为延时，
 * 放大竞态窗口，复现重复售票与余票统计错误。
 */
static void window_entry(void *parameter)
{
    window_arg_t *arg = (window_arg_t *)parameter;

    if (g_with_mutex)
    {
        /* ---- 正常模式：互斥量保护完整售票过程 ---- */
        while (1)
        {
            /* 获取互斥量，保护完整售票事务 */
            if (rt_mutex_take(g_mutex, RT_WAITING_FOREVER) != RT_EOK)
            {
                LOG_E("%s: failed to take mutex", arg->name);
                break;
            }

            /* 临界区开始：检查 -> 减1 -> 记录 */
            if (g_remaining > 0)
            {
                int ticket_no;

                g_remaining--;                              /* 将余票减 1 */
                ticket_no = g_remaining + 1;                /* 本次售出的票号 */

                g_sold[g_sold_count] = ticket_no;           /* 记录售出的票号 */
                g_sold_count++;

                /* 临界区结束 */
                rt_mutex_release(g_mutex);

                /* 打印售票结果（打印在临界区外） */
                rt_kprintf("%s sold ticket %d\n", arg->name, ticket_no);

                rt_thread_mdelay(TICKET_INTERVAL_MS);
            }
            else
            {
                /* 没有余票，释放互斥量并退出 */
                rt_mutex_release(g_mutex);
                break;
            }
        }
    }
    else
    {
        /* ---- 对比实验：无互斥量，读-改之间插入延时 ---- */
        while (1)
        {
            int cur = g_remaining;               /* 1. 读取余票 */

            if (cur > 0)
            {
                /* 2. 人为延时，放大竞态窗口：
                 *    两个窗口可能同时读到相同的余票值 */
                rt_thread_mdelay(RACE_DELAY_MS);

                g_remaining = cur - 1;           /* 3. 修改余票 */

                /* 4. 记录票号（带越界保护，防止实验模式写坏内存） */
                if (g_sold_count < MAX_SOLD_RECORDS)
                {
                    g_sold[g_sold_count] = cur;
                    g_sold_count++;
                }

                rt_kprintf("%s sold ticket %d\n", arg->name, cur);

                rt_thread_mdelay(TICKET_INTERVAL_MS);
            }
            else
            {
                /* 读不到余票，退出 */
                break;
            }
        }
    }

    /* 通知监控线程：本窗口售完 */
    rt_event_send(g_event, arg->done_flag);
}

/*
 * 打印售票结果汇总，并校验数据一致性
 */
static void print_summary(void)
{
    int i, j;
    int dup = 0;

    rt_kprintf("\nall tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", g_remaining);
    rt_kprintf("total sold: %d\n", g_sold_count);

    /* 校验：不出现相同票号 */
    for (i = 0; i < g_sold_count; i++)
    {
        for (j = i + 1; j < g_sold_count; j++)
        {
            if (g_sold[i] == g_sold[j])
            {
                dup++;
                break;
            }
        }
    }

    if (dup > 0)
    {
        rt_kprintf("duplicate tickets: %d\n", dup);
        rt_kprintf("data verification: FAILED!\n");
    }
    else if (g_remaining == 0 && g_sold_count == TOTAL_TICKETS)
    {
        rt_kprintf("no duplicate tickets\n");
        rt_kprintf("data verification: PASSED\n");
    }
    else
    {
        rt_kprintf("data verification: FAILED!\n");
    }
}

/*
 * 监控线程：等待两个窗口全部售完，打印结果并清理资源
 */
static void monitor_entry(void *parameter)
{
    rt_uint32_t recved = 0;
    (void)parameter;

    /* AND 模式等待两个窗口的完成事件（CLEAR 防止残留） */
    if (g_event != RT_NULL)
    {
        rt_event_recv(g_event, EVENT_ALL_DONE,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER, &recved);
    }

    print_summary();

    /* 售票结束，释放 IPC 资源 */
    if (g_mutex != RT_NULL)
    {
        rt_mutex_delete(g_mutex);
        g_mutex = RT_NULL;
    }
    if (g_event != RT_NULL)
    {
        rt_event_delete(g_event);
        g_event = RT_NULL;
    }
    g_running = 0;
}

/*
 * 初始化售票系统状态
 */
static void ticket_init_state(void)
{
    int i;

    g_remaining = TOTAL_TICKETS;
    g_sold_count = 0;
    for (i = 0; i < MAX_SOLD_RECORDS; i++)
    {
        g_sold[i] = 0;
    }
}

/*
 * MSH 命令：ticket_demo [nomutex]
 */
static int ticket_demo(int argc, char *argv[])
{
    rt_thread_t tid_a = RT_NULL;
    rt_thread_t tid_b = RT_NULL;
    rt_thread_t tid_mon = RT_NULL;
    rt_err_t result;

    /* 可选参数 nomutex：对比实验模式 */
    if (argc >= 2 && rt_strcmp(argv[1], "nomutex") == 0)
        g_with_mutex = 0;
    else
        g_with_mutex = 1;

    /* 已在运行则拒绝重复启动 */
    if (g_running)
    {
        rt_kprintf("ticket demo is already running, please wait...\n");
        return RT_EBUSY;
    }

    ticket_init_state();

    /* 创建互斥量（对比实验模式不需要） */
    if (g_with_mutex)
    {
        g_mutex = rt_mutex_create("tick_mx", RT_IPC_FLAG_PRIO);
        if (g_mutex == RT_NULL)
        {
            LOG_E("Failed to create mutex");
            rt_kprintf("Error: failed to create mutex!\n");
            return RT_ERROR;
        }
    }

    /* 创建事件集，用于同步两个窗口完成 */
    g_event = rt_event_create("tick_ev", RT_IPC_FLAG_PRIO);
    if (g_event == RT_NULL)
    {
        LOG_E("Failed to create event");
        rt_kprintf("Error: failed to create event!\n");
        if (g_mutex != RT_NULL)
        {
            rt_mutex_delete(g_mutex);
            g_mutex = RT_NULL;
        }
        return RT_ERROR;
    }

    /* 创建窗口 A / B 线程 */
    tid_a = rt_thread_create("win_a", window_entry, &g_window_a,
                             THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_a == RT_NULL)
    {
        rt_kprintf("Error: failed to create window A thread!\n");
        goto cleanup_fail;
    }
    tid_b = rt_thread_create("win_b", window_entry, &g_window_b,
                             THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_b == RT_NULL)
    {
        rt_kprintf("Error: failed to create window B thread!\n");
        goto cleanup_fail;
    }

    /* 创建监控线程 */
    tid_mon = rt_thread_create("tick_mon", monitor_entry, RT_NULL,
                               THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_mon == RT_NULL)
    {
        rt_kprintf("Error: failed to create monitor thread!\n");
        goto cleanup_fail;
    }

    /* 启动线程 */
    result = rt_thread_startup(tid_a);
    if (result != RT_EOK)
    {
        rt_kprintf("Error: failed to start window A thread!\n");
        goto cleanup_fail;
    }
    result = rt_thread_startup(tid_b);
    if (result != RT_EOK)
    {
        rt_kprintf("Error: failed to start window B thread!\n");
        goto cleanup_fail;
    }
    result = rt_thread_startup(tid_mon);
    if (result != RT_EOK)
    {
        rt_kprintf("Error: failed to start monitor thread!\n");
        goto cleanup_fail;
    }

    g_running = 1;

    if (g_with_mutex)
    {
        rt_kprintf("ticket demo started: %d tickets, windows A/B selling (with mutex)\n",
                   TOTAL_TICKETS);
    }
    else
    {
        rt_kprintf("ticket demo started: %d tickets, windows A/B selling (NO mutex, "
                   "delay between read and write)\n", TOTAL_TICKETS);
        rt_kprintf("comparison experiment: expect duplicate tickets or wrong count\n");
    }
    return RT_EOK;

cleanup_fail:
    /* 创建/启动失败：清理已创建的对象。
     * 未启动的线程处于 INIT 状态，rt_thread_delete 会将其交给空闲线程回收；
     * 已退出的线程为 CLOSE 状态，rt_thread_delete 为空操作，可安全重复调用。 */
    if (tid_a != RT_NULL)
        rt_thread_delete(tid_a);
    if (tid_b != RT_NULL)
        rt_thread_delete(tid_b);
    if (tid_mon != RT_NULL)
        rt_thread_delete(tid_mon);
    if (g_mutex != RT_NULL)
    {
        rt_mutex_delete(g_mutex);
        g_mutex = RT_NULL;
    }
    if (g_event != RT_NULL)
    {
        rt_event_delete(g_event);
        g_event = RT_NULL;
    }
    return RT_ERROR;
}
MSH_CMD_EXPORT(ticket_demo, ticket demo: ticket_demo [nomutex]);

/*
 * 主函数
 */
int main(void)
{
    rt_kprintf("Ticket sale system ready.\n");
    rt_kprintf("Commands: ticket_demo (with mutex), ticket_demo nomutex (comparison)\n");
    return RT_EOK;
}
