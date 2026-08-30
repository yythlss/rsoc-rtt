/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-24     wangqi       the first version
 */
#ifndef APPLICATIONS_THREADS_EVENT_T_C_
#define APPLICATIONS_THREADS_EVENT_T_C_

#include <rtthread.h>

/* 定义三个事件位 */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)

/* 三个模块全部就绪的组合掩码 */
#define ALL_MODULES_READY    (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t startup_event = RT_NULL;

/* 网络初始化线程 */
static void net_init_entry(void *parameter)
{
    rt_kprintf("network module initializing...\n");
    rt_thread_mdelay(1500);  /* 模拟网络初始化耗时 1.5 秒 */
    rt_kprintf("network module ready\n");

    rt_event_send(startup_event, EVENT_NET_READY);
}

/* 传感器初始化线程 */
static void sensor_init_entry(void *parameter)
{
    rt_kprintf("sensor module initializing...\n");
    rt_thread_mdelay(800);   /* 模拟传感器初始化耗时 0.8 秒 */
    rt_kprintf("sensor module ready\n");

    rt_event_send(startup_event, EVENT_SENSOR_READY);
}

/* 存储初始化线程 */
static void storage_init_entry(void *parameter)
{
    rt_kprintf("storage module initializing...\n");
    rt_thread_mdelay(5000);  /* 改为 5 秒，模拟初始化缓慢 */
    rt_kprintf("storage module ready\n");

    rt_event_send(startup_event, EVENT_STORAGE_READY);
}

/* 主业务线程 */
static void business_entry(void *parameter)
{
    rt_uint32_t recved = 0;

    rt_kprintf("business thread waiting for all modules...\n");

    while (1)
    {
        /* 最多等待 3 秒（3000 个 tick，假设 tick = 1ms） */
        rt_err_t ret = rt_event_recv(startup_event,
                                     ALL_MODULES_READY,
                                     RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                     rt_tick_from_millisecond(3000),
                                     &recved);

        if (ret == RT_EOK)
        {
            /* 三个事件全部到达，正常启动 */
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
        else if (ret == -RT_ETIMEOUT)
        {
            rt_kprintf("wait timeout! checking unfinished modules...\n");

            rt_uint32_t single = 0;

            /* 非阻塞查询网络事件 */
            if (rt_event_recv(startup_event, EVENT_NET_READY,
                              RT_EVENT_FLAG_OR, 0, &single) != RT_EOK)
                rt_kprintf("  - network module NOT ready\n");

            /* 非阻塞查询传感器事件 */
            if (rt_event_recv(startup_event, EVENT_SENSOR_READY,
                              RT_EVENT_FLAG_OR, 0, &single) != RT_EOK)
                rt_kprintf("  - sensor module NOT ready\n");

            /* 非阻塞查询存储事件 */
            if (rt_event_recv(startup_event, EVENT_STORAGE_READY,
                              RT_EVENT_FLAG_OR, 0, &single) != RT_EOK)
                rt_kprintf("  - storage module NOT ready\n");

            rt_kprintf("continue waiting...\n");
        }
        else
        {
            rt_kprintf("wait for modules failed, error: %d\n", ret);
            break;
        }
    }
}
static int startup_check_init(void)
{
    rt_thread_t tid;

    /* 创建事件集 */
    startup_event = rt_event_create("startup", RT_IPC_FLAG_PRIO);
    if (startup_event == RT_NULL)
    {
        rt_kprintf("create event failed!\n");
        return -1;
    }

    /* 创建网络初始化线程 */
    tid = rt_thread_create("net_init",
                           net_init_entry,
                           RT_NULL,
                           1024, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    /* 创建传感器初始化线程 */
    tid = rt_thread_create("sensor_init",
                           sensor_init_entry,
                           RT_NULL,
                           1024, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    /* 创建存储初始化线程 */
    tid = rt_thread_create("storage_init",
                           storage_init_entry,
                           RT_NULL,
                           1024, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    /* 创建主业务线程（优先级略低，确保初始化线程先运行） */
    tid = rt_thread_create("business",
                           business_entry,
                           RT_NULL,
                           1024, 21, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    return 0;
}

MSH_CMD_EXPORT_ALIAS(startup_check_init, startup_test_1, system startup check test);

#endif /* APPLICATIONS_THREADS_EVENT_T_C_ */
