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

/* 定义三个事件位 */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)

/* 事件控制块 */
static struct rt_event event;
static rt_thread_t tid1, tid2, tid3, tid4  = RT_NULL;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif

/* 线程 network 入口函数 */
static void network_entry(void *param)
{
    rt_thread_mdelay(1000);
    rt_kprintf("network: send event0\n");
    rt_event_send(&event, EVENT_NET_READY);
    rt_thread_mdelay(200);
}

/* 线程 sensor 入口函数 */
static void sensor_entry(void *param)
{
    rt_thread_mdelay(2000);
    rt_kprintf("sensor: send event1\n");
    rt_event_send(&event, EVENT_SENSOR_READY);
    rt_thread_mdelay(200);
}

/* 线程 storage 入口函数 */
static void storage_entry(void *param)
{
    rt_thread_mdelay(5000);
    rt_kprintf("storage: send event2\n");
    rt_event_send(&event, EVENT_STORAGE_READY);
    rt_thread_mdelay(200);
}

/* 线程 business 入口函数 */
static void business_entry(void *param)
{
    while(1)
    {
        rt_uint32_t recv_event;
        rt_err_t result = rt_event_recv(&event, (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY),
                                        RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                        rt_tick_from_millisecond(3000), &recv_event) ;
        /* 初始化完成 */
        if (result == RT_EOK)
        {
            rt_kprintf("all modules are ready\r\n");
            rt_kprintf("business task started\r\n");
            rt_event_detach(&event);
            rt_kprintf("init event leave.\n");
        }
        /* 超时 */
        else if (result == -RT_ETIMEOUT)
        {
            /* 只打印未就绪的模块 */
            rt_kprintf("wait modules timeout, ");

            if (!(recv_event & EVENT_STORAGE_READY))
                rt_kprintf("STORAGE is not ready\r\n");
            else if (!(recv_event & EVENT_NET_READY))
                rt_kprintf("NET is not ready\r\n");
            else if (!(recv_event & EVENT_SENSOR_READY))
                rt_kprintf("SENSOR is not ready\r\n");
            rt_kprintf("\n");
            /* 继续while循环，再次等待 */
        }
    }
}

int startup_demo(void)
{
    rt_err_t result;

    /* 初始化事件对象 */
    result = rt_event_init(&event, "event", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        rt_kprintf("init event failed.\n");
        return -1;
    }else {
        rt_kprintf("init event OK.\r\n");
    }
    /* 动态创建四个线程,并且启动 */
    tid1 = rt_thread_create("network",
                            network_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid1 != RT_NULL)
        rt_thread_startup(tid1);

    tid2 = rt_thread_create("sensor",
                            sensor_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid2 != RT_NULL)
        rt_thread_startup(tid2);

    tid3 = rt_thread_create("storage",
                            storage_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid3 != RT_NULL)
        rt_thread_startup(tid3);

    tid4 = rt_thread_create("business",
                            business_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid4 != RT_NULL)
        rt_thread_startup(tid4);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(startup_demo, startup demo);
