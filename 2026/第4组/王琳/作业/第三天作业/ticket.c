/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     d0ubleU0x00       the first version
 */
#include <rtthread.h>

/* 修改此宏为 0 可关闭互斥量，用于对比实验（观察数据竞争） */
#define USE_MUTEX 0

static int ticket_num = 20;
static rt_mutex_t mutex = RT_NULL;
static rt_sem_t sem_done = RT_NULL;

static void ticket_thread_entry(void *parameter)
{
    char win = (char)(int)parameter;

    while (1)
    {
#if USE_MUTEX
        rt_mutex_take(mutex, RT_WAITING_FOREVER);
#endif

        /* 临界区开始 */
        if (ticket_num > 0)
        {
            int ticket = ticket_num;

            /* 关键延时：模拟“读取余票”与“修改余票”之间的时间空隙，
             * 在无互斥量时会导致竞争条件 */
            rt_thread_mdelay(10);

            ticket_num--;
            rt_kprintf("window %c sold ticket %d\n", win, ticket);
        }
        else
        {
#if USE_MUTEX
            rt_mutex_release(mutex);
#endif
            break;
        }

#if USE_MUTEX
        rt_mutex_release(mutex);
#endif

        /* 售票后短暂休息，让出 CPU */
        rt_thread_mdelay(5);
    }

    rt_sem_release(sem_done);
}

static void ticket_demo(int argc, char *argv[])
{
    rt_thread_t tid1, tid2;

    ticket_num = 20;

#if USE_MUTEX
    mutex = rt_mutex_create("ticket_mtx", RT_IPC_FLAG_PRIO);
    if (mutex == RT_NULL)
    {
        rt_kprintf("create mutex failed\n");
        return;
    }
#endif

    sem_done = rt_sem_create("ticket_done", 0, RT_IPC_FLAG_PRIO);
    if (sem_done == RT_NULL)
    {
        rt_kprintf("create semaphore failed\n");
#if USE_MUTEX
        rt_mutex_delete(mutex);
#endif
        return;
    }

    tid1 = rt_thread_create("winA", ticket_thread_entry, (void *)'A',
                            1024, 25, 10);
    if (tid1 != RT_NULL)
        rt_thread_startup(tid1);
    else
        rt_kprintf("create window A thread failed\n");

    tid2 = rt_thread_create("winB", ticket_thread_entry, (void *)'B',
                            1024, 25, 10);
    if (tid2 != RT_NULL)
        rt_thread_startup(tid2);
    else
        rt_kprintf("create window B thread failed\n");

    /* 等待两个线程运行结束 */
    rt_sem_take(sem_done, RT_WAITING_FOREVER);
    rt_sem_take(sem_done, RT_WAITING_FOREVER);

    rt_sem_delete(sem_done);
#if USE_MUTEX
    rt_mutex_delete(mutex);
#endif

    rt_kprintf("all tickets sold\nremaining tickets: %d\n", ticket_num);
}

MSH_CMD_EXPORT(ticket_demo, ticket_demo - multi-window ticket selling demo);
