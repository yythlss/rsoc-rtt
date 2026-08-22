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

/* 剩余票数 */
static rt_int32_t remaining_tickets = TOTAL_TICKETS;

/* 售票互斥量 */
static rt_mutex_t ticket_mutex = RT_NULL;

/* 两个售票线程 */
static rt_thread_t window_a_thread = RT_NULL;
static rt_thread_t window_b_thread = RT_NULL;

/* 已结束的窗口数量 */
static rt_uint8_t finished_windows = 0;

/* 实验运行标志 */
static rt_bool_t ticket_running = RT_FALSE;

/**
 * @brief 售票窗口线程入口。
 *
 * 每个窗口循环申请互斥量，在获得互斥量后完成
 * 检查余票、记录票号、余票减 1 和打印售票结果。
 *
 * @param parameter 窗口名称。
 *
 * @return 无。
 */
static void ticket_window_entry(void *parameter)
{
    const char *window_name = (const char *)parameter;
    rt_err_t result;
    rt_int32_t ticket_no;

    while (1)
    {
        /* 获取互斥量，保护完整售票过程 */
        result = rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        if (result != RT_EOK)
        {
            rt_kprintf("window %s take mutex failed.\n", window_name);
            return;
        }

        /* 检查是否还有余票 */
        if (remaining_tickets <= 0)
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        /* 当前剩余票数作为本次售出的票号 */
        ticket_no = remaining_tickets;

        /* 售出一张票，余票减 1 */
        remaining_tickets--;

        rt_kprintf(
            "window %s sold ticket %d, remaining: %d\n",
            window_name,
            ticket_no,
            remaining_tickets);

        /* 售票完成后释放互斥量 */
        result = rt_mutex_release(ticket_mutex);

        if (result != RT_EOK)
        {
            rt_kprintf("window %s release mutex failed.\n", window_name);
            return;
        }

        /*
         * 模拟窗口处理下一位乘客，
         * 同时给另一个同优先级线程运行机会。
         */
        rt_thread_mdelay(100);
    }

    /*
     * 统计已经结束的窗口。
     * finished_windows 也是共享变量，因此同样使用互斥量保护。
     */
    result = rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

    if (result == RT_EOK)
    {
        rt_bool_t last_window = RT_FALSE;

        finished_windows++;

        if (finished_windows == 2)
        {
            rt_kprintf("all tickets sold\n");
            rt_kprintf("remaining tickets: %d\n", remaining_tickets);
            last_window = RT_TRUE;
        }

        rt_mutex_release(ticket_mutex);

        /*
         * 最后结束的窗口负责删除互斥量，
         * 此时两个售票线程都已经完成售票循环。
         */
        if (last_window == RT_TRUE)
        {
            rt_mutex_delete(ticket_mutex);
            ticket_mutex = RT_NULL;
            ticket_running = RT_FALSE;
        }
    }
}

/**
 * @brief 启动多窗口售票实验。
 *
 * 创建一个互斥量和两个同优先级售票线程，
 * 两个窗口共同出售 20 张票。
 *
 * @return RT_EOK 启动成功。
 * @return -RT_ERROR 创建互斥量或线程失败。
 */
static int ticket_demo(void)
{
    /* 防止实验尚未结束时重复启动 */
    if (ticket_running == RT_TRUE)
    {
        rt_kprintf("ticket demo is already running.\n");
        return RT_EOK;
    }

    /* 重置实验数据 */
    remaining_tickets = TOTAL_TICKETS;
    finished_windows = 0;

    /* 创建售票互斥量 */
    ticket_mutex = rt_mutex_create("ticket", RT_IPC_FLAG_FIFO);

    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed.\n");
        return -RT_ERROR;
    }

    ticket_running = RT_TRUE;

    /* 创建窗口 A 线程 */
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
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
        ticket_running = RT_FALSE;
        return -RT_ERROR;
    }

    /* 创建窗口 B 线程 */
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
        rt_mutex_delete(ticket_mutex);

        window_a_thread = RT_NULL;
        ticket_mutex = RT_NULL;
        ticket_running = RT_FALSE;

        return -RT_ERROR;
    }

    /* 启动两个售票线程 */
    rt_thread_startup(window_a_thread);
    rt_thread_startup(window_b_thread);

    return RT_EOK;
}

/* 导出为 MSH 命令 */
MSH_CMD_EXPORT(ticket_demo, ticket demo);

