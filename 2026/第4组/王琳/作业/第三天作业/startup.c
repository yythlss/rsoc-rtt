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

/* 事件标志位 */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)

static rt_event_t startup_event;          /* 事件集对象 */

/* 网络初始化线程 */
static void network_init_entry(void *param)
{
    rt_kprintf("network init start\n");
    rt_thread_mdelay(1000);               /* 模拟 1s 初始化 */
    rt_kprintf("network ready\n");
    rt_event_send(startup_event, EVENT_NET_READY);
}

/* 传感器初始化线程 */
static void sensor_init_entry(void *param)
{
    rt_kprintf("sensor init start\n");
    rt_thread_mdelay(2000);               /* 模拟 2s 初始化 */
    rt_kprintf("sensor ready\n");
    rt_event_send(startup_event, EVENT_SENSOR_READY);
}

/* 存储初始化线程（故意设 5s，用于超时测试） */
static void storage_init_entry(void *param)
{
    rt_kprintf("storage init start\n");
    rt_thread_mdelay(5000);               /* 模拟 5s 初始化 */
    rt_kprintf("storage ready\n");
    rt_event_send(startup_event, EVENT_STORAGE_READY);
}

/* 主业务线程 */
static void business_entry(void *param)
{
    rt_uint32_t recved;
    rt_err_t ret;

    while (1)
    {
        /* AND 模式等待所有事件，超时 3 秒，成功后自动清除事件位 */
        ret = rt_event_recv(startup_event,
                            EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY,
                            RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                            RT_TICK_PER_SECOND * 3,
                            &recved);

        if (ret == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;                         /* 业务启动后退出线程 */
        }
        else if (ret == -RT_ETIMEOUT)
        {
            /* 超时：recved 中保存了当前已设置的事件位（未被清除） */
            rt_kprintf("wait modules timeout, ");
            if (!(recved & EVENT_NET_READY))   rt_kprintf("network ");
            if (!(recved & EVENT_SENSOR_READY)) rt_kprintf("sensor ");
            if (!(recved & EVENT_STORAGE_READY)) rt_kprintf("storage ");
            rt_kprintf("is not ready\n");
            /* 继续循环等待，直到全部就绪 */
        }
        else
        {
            rt_kprintf("event recv error: %d\n", ret);
            break;
        }
    }
}

/* MSH 命令：启动整个启动检查流程 */
void startup_demo(int argc, char **argv)
{
    rt_thread_t tid;

    /* 1. 创建事件集 */
    startup_event = rt_event_create("startup_evt", RT_IPC_FLAG_PRIO);
    if (startup_event == RT_NULL)
    {
        rt_kprintf("create event failed\n");
        return;
    }

    /* 2. 创建网络初始化线程 */
    tid = rt_thread_create("net_init", network_init_entry, RT_NULL,
                           1024, 15, 10);
    if (tid == RT_NULL)
    {
        rt_kprintf("create net_init thread failed\n");
        goto cleanup;
    }
    rt_thread_startup(tid);

    /* 3. 创建传感器初始化线程 */
    tid = rt_thread_create("sensor_init", sensor_init_entry, RT_NULL,
                           1024, 15, 10);
    if (tid == RT_NULL)
    {
        rt_kprintf("create sensor_init thread failed\n");
        goto cleanup;
    }
    rt_thread_startup(tid);

    /* 4. 创建存储初始化线程 */
    tid = rt_thread_create("storage_init", storage_init_entry, RT_NULL,
                           1024, 15, 10);
    if (tid == RT_NULL)
    {
        rt_kprintf("create storage_init thread failed\n");
        goto cleanup;
    }
    rt_thread_startup(tid);

    /* 5. 创建主业务线程（优先级低于初始化线程，让它们先跑） */
    tid = rt_thread_create("business", business_entry, RT_NULL,
                           2048, 10, 10);
    if (tid == RT_NULL)
    {
        rt_kprintf("create business thread failed\n");
        goto cleanup;
    }
    rt_thread_startup(tid);

    return;

cleanup:
    /* 若创建失败，可删除已创建的资源 */
    rt_kprintf("startup_demo failed, cleanup not implemented\n");
}


MSH_CMD_EXPORT(startup_demo, startup demo: check network/sensor/storage ready);
