/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     godbear       练习二：多窗口售票系统（互斥量）
 */

#include <rtthread.h>
#include <finsh.h>

#define DBG_TAG "ticket"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define TICKET_TOTAL        20                      /* 初始余票 */

static rt_mutex_t ticket_mutex = RT_NULL;
static volatile rt_int16_t ticket_count = TICKET_TOTAL;
static volatile rt_uint8_t ticket_running = 0;
static volatile rt_uint8_t ticket_finished = 0;
static volatile rt_uint8_t ticket_created = 0;

/* 售票窗口线程：互斥量保护完整的“检查余票-减一-记录-打印”流程 */
static void ticket_window_entry(void *param)
{
    char name = (char)(rt_size_t)param;             /* 'A' 或 'B' */

    while (1)
    {
        rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        /* 检查余票，售完退出 */
        if (ticket_count <= 0)
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        /* 记录票号并减一（票号取自当前余票，互斥下不会重复） */
        rt_int16_t no = ticket_count;
        ticket_count--;
        rt_kprintf("window %c sold ticket %d\n", name, no);

        rt_mutex_release(ticket_mutex);

        /* 短暂让出，模拟不同窗口交替售票 */
        rt_thread_mdelay(rt_tick_from_millisecond(20));
    }

    /* 两个窗口都结束后清理资源 */
    rt_enter_critical();
    ticket_finished++;
    if (ticket_finished >= ticket_created)
    {
        ticket_finished = 0;
        ticket_created = 0;
        ticket_running = 0;
        rt_exit_critical();

        rt_kprintf("all tickets sold\n");
        rt_kprintf("remaining tickets: %d\n", ticket_count);
        if (ticket_mutex)
        {
            rt_mutex_delete(ticket_mutex);
            ticket_mutex = RT_NULL;
        }
    }
    else
    {
        rt_exit_critical();
    }
}

static int ticket_demo(int argc, char *argv[])
{
    rt_thread_t tid;

    if (ticket_running)
    {
        rt_kprintf("ticket_demo is already running, please wait...\n");
        return -RT_EBUSY;
    }

    /* 创建互斥量保护售票流程 */
    ticket_mutex = rt_mutex_create("tk_mutex", RT_IPC_FLAG_PRIO);
    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed\n");
        return -RT_ENOMEM;
    }

    ticket_count = TICKET_TOTAL;
    ticket_running = 1;
    ticket_finished = 0;
    ticket_created = 0;

    /* 窗口 A */
    tid = rt_thread_create("tickA", ticket_window_entry, (void *)(rt_size_t)'A',
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
        ticket_created++;
    }
    else
    {
        rt_kprintf("create window A thread failed\n");
    }

    /* 窗口 B */
    tid = rt_thread_create("tickB", ticket_window_entry, (void *)(rt_size_t)'B',
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
        ticket_created++;
    }
    else
    {
        rt_kprintf("create window B thread failed\n");
    }

    if (ticket_created == 0)
    {
        ticket_running = 0;
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
        return -RT_ERROR;
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, multi-window ticket selling demo with mutex);
