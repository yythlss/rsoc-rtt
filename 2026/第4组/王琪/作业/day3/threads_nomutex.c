/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-24     wangqi       the first version
 */
#ifndef APPLICATIONS_THREADS_NOMUTEX_C_
#define APPLICATIONS_THREADS_NOMUTEX_C_

#include <rtthread.h>

#define TOTAL_TICKETS   20

static int remaining_tickets = TOTAL_TICKETS;

static void window_thread_entry(void *parameter)
{
    const char *window_name = (const char *)parameter;

    while (1)
    {
        if (remaining_tickets <= 0)
        {
            break;
        }

        int ticket_no = remaining_tickets;

        /* 故意制造时间窗口，让另一个线程有机会同时读到同一个余票值 */
        rt_thread_mdelay(5);

        remaining_tickets = ticket_no - 1;

        rt_kprintf("%s sold ticket %d\n", window_name, ticket_no);
    }

    rt_kprintf("%s exit\n", window_name);
}

static int ticket_test_init(void)
{
    rt_thread_t tid_a, tid_b;

    remaining_tickets = TOTAL_TICKETS;

    tid_a = rt_thread_create("win_a",
                             window_thread_entry,
                             (void *)"window A",
                             1024,
                             20,
                             10);
    if (tid_a != RT_NULL)
        rt_thread_startup(tid_a);

    tid_b = rt_thread_create("win_b",
                             window_thread_entry,
                             (void *)"window B",
                             1024,
                             20,
                             10);
    if (tid_b != RT_NULL)
        rt_thread_startup(tid_b);

    return 0;
}

MSH_CMD_EXPORT_ALIAS(ticket_test_init, ticket_test_1, multi-window ticket test);

#endif /* APPLICATIONS_THREADS_NOMUTEX_C_ */
