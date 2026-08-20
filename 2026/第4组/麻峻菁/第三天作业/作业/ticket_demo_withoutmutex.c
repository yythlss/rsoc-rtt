/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     24527       the first version
 */

#include <rtthread.h>
#include <finsh.h>

#define TOTAL_TICKETS       20
#define THREAD_PRIORITY     25
#define THREAD_STACK_SIZE   1024
#define THREAD_TIMESLICE    5

/* 线程延时 */
#define RACE_DELAY_MS       50

/* 全局剩余票数 */
static rt_int32_t remaining_tickets = TOTAL_TICKETS;

/* 两个售票线程 */
static rt_thread_t window_a_thread = RT_NULL;
static rt_thread_t window_b_thread = RT_NULL;

/**
 * @brief 不使用互斥量的售票窗口线程。
 *
 * 线程先读取全局剩余票数，再故意延时一小段时间，
 * 最后才修改剩余票数。
 *
 * 由于窗口 A 和窗口 B 没有互斥保护，
 * 两个线程可能同时读取到相同的票号，
 * 从而产生重复售票或剩余票数更新错误。
 *
 * @param parameter 窗口名称。
 *
 * @return 无。
 */
static void ticket_window_entry(void *parameter)
{
    const char *window_name = (const char *)parameter;
    rt_int32_t ticket_no;

    while (1)
    {
        /* 检查是否还有余票 */
        if (remaining_tickets <= 0)
        {
            break;
        }

        /* 读取当前余票。 */
        ticket_no = remaining_tickets;

        /* 故意加入短暂延时。  */
        rt_thread_mdelay(RACE_DELAY_MS);

        /*
         * 根据之前读取到的票号修改剩余票数。  */
        remaining_tickets = ticket_no - 1;

        rt_kprintf(
            "window %s sold ticket %d, remaining: %d\n",
            window_name,
            ticket_no,
            remaining_tickets);

        /* 给另一个同优先级线程运行机会 */
        rt_thread_mdelay(20);
    }

    rt_kprintf("window %s finished.\n", window_name);
}

/**
 * @brief 启动不使用互斥量的售票实验。
 *
 * 创建两个相同优先级的售票线程，
 * 两个线程共同访问全局变量 remaining_tickets。
 *
 * 不使用互斥量，并在读取和修改余票之间延时，
 * 用于观察竞态条件造成的重复售票或数据错误。
 *
 * @return RT_EOK 启动成功。
 * @return -RT_ERROR 线程创建或启动失败。
 */
static int ticket_demo_withoutmutex(void)
{
    rt_err_t result;

    /* 每次运行前初始化为20张票 */
    remaining_tickets = TOTAL_TICKETS;

    rt_kprintf("\n");
    rt_kprintf("===== ticket demo WITHOUT mutex =====\n");
    rt_kprintf("initial tickets: %d\n", remaining_tickets);

    /* 创建窗口A线程 */
    window_a_thread = rt_thread_create(
        "window_a",
        ticket_window_entry,
        "A",
        THREAD_STACK_SIZE,
        THREAD_PRIORITY,
        THREAD_TIMESLICE);

    if (window_a_thread == RT_NULL)
    {
        rt_kprintf("create window A thread failed.\n");
        return -RT_ERROR;
    }

    /* 创建窗口B线程 */
    window_b_thread = rt_thread_create(
        "window_b",
        ticket_window_entry,
        "B",
        THREAD_STACK_SIZE,
        THREAD_PRIORITY,
        THREAD_TIMESLICE);

    if (window_b_thread == RT_NULL)
    {
        rt_kprintf("create window B thread failed.\n");

        rt_thread_delete(window_a_thread);
        window_a_thread = RT_NULL;

        return -RT_ERROR;
    }

    /* 启动窗口A线程 */
    result = rt_thread_startup(window_a_thread);

    // 创建线程A失败
    if (result != RT_EOK)
    {
        rt_kprintf("startup window A failed, error = %d.\n", result);

        rt_thread_delete(window_a_thread);
        rt_thread_delete(window_b_thread);

        //  删除未初始化的线程
        window_a_thread = RT_NULL;
        window_b_thread = RT_NULL;

        return -RT_ERROR;
    }

    /* 启动窗口B线程 */
    result = rt_thread_startup(window_b_thread);

    if (result != RT_EOK)
    {
        rt_kprintf("startup window B failed, error = %d.\n", result);

        //  a已经运行，仅删除b线程
        rt_thread_delete(window_b_thread);
        window_b_thread = RT_NULL;

        return -RT_ERROR;
    }

    return RT_EOK;
}

/* 导出为 MSH 命令 */
MSH_CMD_EXPORT(ticket_demo_withoutmutex, ticket demo without mutex);
