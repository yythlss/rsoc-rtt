/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     Vincent       the first version
 */

#include <rtthread.h>

#define THREAD_PRIORITY         25
#define THREAD_STACK_SIZE       1*1024
#define THREAD_TIMESLICE        5

/* 指向互斥量的指针 */
static rt_mutex_t dynamic_mutex = RT_NULL;
static rt_thread_t tid1, tid2 = RT_NULL;

/* 剩余票数 */
static volatile rt_uint8_t sold_ticket = 20;
static  rt_uint8_t A_ticket[20] = {0};
static  rt_uint8_t B_ticket[20] = {0};

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif

static void A_entry(void *parameter)
{
    static rt_uint8_t count = 0;
    while (1)
    {
        if (rt_mutex_take(dynamic_mutex, RT_WAITING_FOREVER) == RT_EOK)
        {
            count++;
            if(sold_ticket > 0)
            {
                /* 记录售出的票号，并将余票-1 */
                A_ticket[count] = sold_ticket;
                rt_kprintf("window A sold ticket %d\r\n", sold_ticket);
                sold_ticket--;
            }else
            {
                sold_ticket = 0;
                rt_kprintf("all tickets sold\r\n remaining tickets: 0\r\n");
            }
            rt_mutex_release(dynamic_mutex);
            rt_thread_mdelay(100);
        }else {
            rt_kprintf("take mutex error!\r\n");
        }
    }
}

static void B_entry(void *parameter)
{
    static rt_uint8_t count = 0;
    while (1)
    {
        if (rt_mutex_take(dynamic_mutex, RT_WAITING_FOREVER) == RT_EOK)
        {
            count++;
            if(sold_ticket > 0)
            {
                /* 记录售出的票号，并将余票-1 */
                B_ticket[count] = sold_ticket;
                rt_kprintf("window B sold ticket %d\r\n", sold_ticket);
                sold_ticket--;
            }else
            {
                sold_ticket = 0;
                rt_kprintf("all tickets sold\r\n remaining tickets: 0\r\n");
            }
            rt_mutex_release(dynamic_mutex);
            rt_thread_mdelay(100);
        }else {
            rt_kprintf("take mutex error!\r\n");
        }
//        count++;
//        if(sold_ticket > 0)
//        {
//            /* 记录售出的票号，并将余票-1 */
//            A_ticket[count] = sold_ticket;
//            rt_kprintf("window B sold ticket %d\r\n", sold_ticket);
//            sold_ticket--;
//            rt_thread_mdelay(100);
//        }
//        else
//        {
//            sold_ticket = 0;
//            rt_kprintf("all tickets sold\r\n remaining tickets: 0");
//            rt_thread_mdelay(100);
//        }
    }
}

/* 信号量示例的初始化 */
int ticket_demo()
{
    /* 创建一个动态互斥量 */
    dynamic_mutex = rt_mutex_create("tickets", RT_IPC_FLAG_PRIO);
    if (dynamic_mutex == RT_NULL)
    {
        rt_kprintf("create dynamic tickets failed.\n");
        return -1;
    }

    /* 动态创建两个线程，分别代表窗口A和B */
    tid1 = rt_thread_create("A",
                            A_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    tid2 = rt_thread_create("B",
                            B_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY+1, THREAD_TIMESLICE);
    if (tid1 != RT_NULL)
            rt_thread_startup(tid1);
    if (tid2 != RT_NULL)
            rt_thread_startup(tid2);
    rt_kprintf("create OK!\r\n");
    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(ticket_demo, ticket demo);

