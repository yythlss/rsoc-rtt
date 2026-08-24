/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-24     wangqi       the first version
 */
#ifndef APPLICATIONS_THREADS_MUTEX_C_
#define APPLICATIONS_THREADS_MUTEX_C_

#include <rtthread.h>

#define TOTAL_TICKETS   20

static rt_mutex_t ticket_mutex = RT_NULL;
static int remaining_tickets = TOTAL_TICKETS;

static void window_thread_entry(void *parameter)
{
    const char *window_name = (const char *)parameter;

    while (1)
    {
        rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        if (remaining_tickets <= 0)
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        int ticket_no = remaining_tickets;
        remaining_tickets--;

        rt_kprintf("%s sold ticket %d\n", window_name, ticket_no);

        rt_mutex_release(ticket_mutex);

        rt_thread_mdelay(10);
    }

    rt_kprintf("%s exit\n", window_name);
}

static int ticket_test_init(void)
{
    rt_thread_t tid_a, tid_b;

    ticket_mutex = rt_mutex_create("ticket", RT_IPC_FLAG_PRIO);
    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed!\n");
        return -1;
    }

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

MSH_CMD_EXPORT_ALIAS(ticket_test_init, ticket_test, multi-window ticket test);

#endif /* APPLICATIONS_THREADS_MUTEX_C_ */
