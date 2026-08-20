/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     godbear       练习三：系统启动条件检查（事件集 AND+超时）
 */

#include <rtthread.h>
#include <finsh.h>

#define DBG_TAG "startup"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define EVENT_NET_READY        (1U << 0)
#define EVENT_SENSOR_READY     (1U << 1)
#define EVENT_STORAGE_READY    (1U << 2)
#define EVENT_ALL              (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t startup_evt = RT_NULL;
static volatile rt_uint32_t ready_flags = 0;        /* 各模块就绪状态位 */
static volatile rt_uint8_t startup_running = 0;

/* 网络初始化线程：1 秒后就绪 */
static void net_init_entry(void *param)
{
    (void)param;
    rt_thread_mdelay(rt_tick_from_millisecond(1000));
    ready_flags |= EVENT_NET_READY;
    rt_event_send(startup_evt, EVENT_NET_READY);
    rt_kprintf("network ready\n");
}

/* 传感器初始化线程：2 秒后就绪 */
static void sensor_init_entry(void *param)
{
    (void)param;
    rt_thread_mdelay(rt_tick_from_millisecond(2000));
    ready_flags |= EVENT_SENSOR_READY;
    rt_event_send(startup_evt, EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
}

/* 存储初始化线程：5 秒后就绪（用于触发主线程 3 秒超时） */
static void storage_init_entry(void *param)
{
    (void)param;
    rt_thread_mdelay(rt_tick_from_millisecond(5000));
    ready_flags |= EVENT_STORAGE_READY;
    rt_event_send(startup_evt, EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
}

/* 主业务线程：AND 模式等待三个事件全部到达，最多等 3 秒 */
static void business_entry(void *param)
{
    (void)param;

    while (1)
    {
        rt_err_t res = rt_event_recv(startup_evt, EVENT_ALL,
                                     RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                     rt_tick_from_millisecond(3000), RT_NULL);
        if (res == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
        else
        {
            /* 3 秒超时：打印尚未完成的模块，并再次等待 */
            if (!(ready_flags & EVENT_NET_READY))
                rt_kprintf("wait modules timeout, network is not ready\n");
            if (!(ready_flags & EVENT_SENSOR_READY))
                rt_kprintf("wait modules timeout, sensor is not ready\n");
            if (!(ready_flags & EVENT_STORAGE_READY))
                rt_kprintf("wait modules timeout, storage is not ready\n");
        }
    }

    /* 此时三个初始化线程均已结束，可安全清理资源，支持命令重复执行 */
    rt_thread_mdelay(rt_tick_from_millisecond(100));
    startup_running = 0;
    ready_flags = 0;
    if (startup_evt)
    {
        rt_event_delete(startup_evt);
        startup_evt = RT_NULL;
    }
}

static int startup_demo(int argc, char *argv[])
{
    rt_thread_t tid;

    if (startup_running)
    {
        rt_kprintf("startup_demo is already running, please wait...\n");
        return -RT_EBUSY;
    }

    /* 创建事件集 */
    startup_evt = rt_event_create("start_evt", RT_IPC_FLAG_PRIO);
    if (startup_evt == RT_NULL)
    {
        rt_kprintf("create startup event failed\n");
        return -RT_ENOMEM;
    }

    ready_flags = 0;
    startup_running = 1;

    /* 网络初始化线程 */
    tid = rt_thread_create("netinit", net_init_entry, RT_NULL,
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("create network init thread failed\n");
        startup_running = 0;
        rt_event_delete(startup_evt);
        startup_evt = RT_NULL;
        return -RT_ERROR;
    }

    /* 传感器初始化线程 */
    tid = rt_thread_create("seninit", sensor_init_entry, RT_NULL,
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("create sensor init thread failed\n");
    }

    /* 存储初始化线程 */
    tid = rt_thread_create("stoinit", storage_init_entry, RT_NULL,
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("create storage init thread failed\n");
    }

    /* 主业务线程 */
    tid = rt_thread_create("bizmain", business_entry, RT_NULL,
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("create business thread failed\n");
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, system startup condition check demo with event);
