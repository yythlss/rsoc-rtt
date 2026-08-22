/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     user         system startup event demo
 */
#include <rtthread.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define ALL_EVENT_MASK       (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

#define THREAD_STACK_SIZE    1024
#define THREAD_PRIORITY      20

/* 事件集 */
static rt_event_t startup_event = RT_NULL;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif

static char net_stack[THREAD_STACK_SIZE];
static struct rt_thread net_thread;

static char sensor_stack[THREAD_STACK_SIZE];
static struct rt_thread sensor_thread;

static char storage_stack[THREAD_STACK_SIZE];
static struct rt_thread storage_thread;

static char business_stack[THREAD_STACK_SIZE];
static struct rt_thread business_thread;

/* 网络初始化线程 延时1s就绪 */
static void net_thread_entry(void *parameter)
{
    rt_thread_mdelay(1000);
    rt_event_send(startup_event, EVENT_NET_READY);
    rt_kprintf("network ready\n");
}

/* 传感器初始化线程 延时2s就绪 */
static void sensor_thread_entry(void *parameter)
{
    rt_thread_mdelay(2000);
    rt_event_send(startup_event, EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
}

/* 存储初始化线程 延时5s就绪 */
static void storage_thread_entry(void *parameter)
{
    rt_thread_mdelay(5000);
    rt_event_send(startup_event, EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
}

/* 主业务线程 */
static void business_thread_entry(void *parameter)
{
    rt_uint32_t recv;
    rt_err_t ret;

    while (1)
    {
        /* AND模式，等待全部事件，最长等待3秒 */
        ret = rt_event_recv(startup_event,
                            ALL_EVENT_MASK,
                            RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                            RT_TICK_PER_SECOND * 3,
                            &recv);
        if (ret == RT_EOK)
        {
            rt_kprintf("all modules are ready\nbusiness task started\n");
            break;
        }
        else
        {
            rt_kprintf("wait modules timeout, ");
            if (!(recv & EVENT_NET_READY))
            {
                rt_kprintf("net ");
            }
            if (!(recv & EVENT_SENSOR_READY))
            {
                rt_kprintf("sensor ");
            }
            if (!(recv & EVENT_STORAGE_READY))
            {
                rt_kprintf("storage ");
            }
            rt_kprintf("is not ready\n");
        }
    }
}

int startup_check_sample(void)
{
    rt_err_t res;

    startup_event = rt_event_create("startup_evt", RT_IPC_FLAG_PRIO);
    if (startup_event == RT_NULL)
    {
        rt_kprintf("create event failed\n");
        return -1;
    }

    /* 网络线程 */
    res = rt_thread_init(&net_thread,
                          "net_init",
                          net_thread_entry,
                          RT_NULL,
                          net_stack,
                          sizeof(net_stack),
                          THREAD_PRIORITY,
                          5);
    if (res == RT_EOK)
    {
        rt_thread_startup(&net_thread);
    }

    /* 传感器线程 */
    res = rt_thread_init(&sensor_thread,
                          "sensor_init",
                          sensor_thread_entry,
                          RT_NULL,
                          sensor_stack,
                          sizeof(sensor_stack),
                          THREAD_PRIORITY,
                          5);
    if (res == RT_EOK)
    {
        rt_thread_startup(&sensor_thread);
    }

    /* 存储线程 */
    res = rt_thread_init(&storage_thread,
                          "storage_init",
                          storage_thread_entry,
                          RT_NULL,
                          storage_stack,
                          sizeof(storage_stack),
                          THREAD_PRIORITY,
                          5);
    if (res == RT_EOK)
    {
        rt_thread_startup(&storage_thread);
    }

    /* 业务线程 */
    res = rt_thread_init(&business_thread,
                          "business",
                          business_thread_entry,
                          RT_NULL,
                          business_stack,
                          sizeof(business_stack),
                          THREAD_PRIORITY,
                          5);
    if (res == RT_EOK)
    {
        rt_thread_startup(&business_thread);
    }

    return 0;
}

MSH_CMD_EXPORT(startup_check_sample, system startup event check sample);
