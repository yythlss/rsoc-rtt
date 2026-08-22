/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     xlc       the first version
 */
#include <rtthread.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

#define THREAD_STACK_SIZE    1024
#define THREAD_PRIORITY      20
#define THREAD_TIMESLICE     10

static rt_event_t startup_event = RT_NULL;
static rt_mutex_t state_mutex = RT_NULL;

static rt_thread_t net_thread = RT_NULL;
static rt_thread_t sensor_thread = RT_NULL;
static rt_thread_t storage_thread = RT_NULL;
static rt_thread_t business_thread = RT_NULL;

static rt_uint8_t net_stack[THREAD_STACK_SIZE];
static rt_uint8_t sensor_stack[THREAD_STACK_SIZE];
static rt_uint8_t storage_stack[THREAD_STACK_SIZE];
static rt_uint8_t business_stack[THREAD_STACK_SIZE];

static struct rt_thread net_th;
static struct rt_thread sensor_th;
static struct rt_thread storage_th;
static struct rt_thread business_th;

static volatile rt_uint8_t demo_running = 0;
static volatile rt_uint32_t ready_flags = 0;

static void set_ready_flag(rt_uint32_t flag)
{
    rt_mutex_take(state_mutex, RT_WAITING_FOREVER);
    ready_flags |= flag;
    rt_mutex_release(state_mutex);
}

static rt_uint32_t get_ready_flag(void)
{
    rt_uint32_t flags;

    rt_mutex_take(state_mutex, RT_WAITING_FOREVER);
    flags = ready_flags;
    rt_mutex_release(state_mutex);

    return flags;
}

static void net_init_entry(void *parameter)
{
    RT_UNUSED(parameter);

    rt_thread_mdelay(1000);
    set_ready_flag(EVENT_NET_READY);
    rt_event_send(startup_event, EVENT_NET_READY);
    rt_kprintf("network ready\n");
}

static void sensor_init_entry(void *parameter)
{
    RT_UNUSED(parameter);

    rt_thread_mdelay(2000);
    set_ready_flag(EVENT_SENSOR_READY);
    rt_event_send(startup_event, EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
}

static void storage_init_entry(void *parameter)
{
    RT_UNUSED(parameter);

    rt_thread_mdelay(5000);
    set_ready_flag(EVENT_STORAGE_READY);
    rt_event_send(startup_event, EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
}

static void business_entry(void *parameter)
{
    rt_err_t result;
    rt_uint32_t recved;

    RT_UNUSED(parameter);

    while (1)
    {
        result = rt_event_recv(startup_event,
                               EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               rt_tick_from_millisecond(3000),
                               &recved);
        if (result == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
        else if (result == -RT_ETIMEOUT)
        {
            rt_uint32_t flags = get_ready_flag();

            rt_kprintf("wait modules timeout, ");
            if ((flags & EVENT_STORAGE_READY) == 0)
            {
                rt_kprintf("storage is not ready");
            }
            else if ((flags & EVENT_SENSOR_READY) == 0)
            {
                rt_kprintf("sensor is not ready");
            }
            else if ((flags & EVENT_NET_READY) == 0)
            {
                rt_kprintf("network is not ready");
            }
            else
            {
                rt_kprintf("modules are still not fully ready");
            }
            rt_kprintf("\n");
        }
        else
        {
            rt_kprintf("wait event failed, err=%d\n", result);
            rt_thread_mdelay(200);
        }
    }
}

int startup_demo(int argc, char **argv)
{
    rt_err_t result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (demo_running)
    {
        rt_kprintf("startup demo is already running\n");
        return -RT_ERROR;
    }

    demo_running = 1;
    ready_flags = 0;

    startup_event = rt_event_create("stpev", RT_IPC_FLAG_PRIO);
    if (startup_event == RT_NULL)
    {
        rt_kprintf("create event failed\n");
        demo_running = 0;
        return -RT_ERROR;
    }

    state_mutex = rt_mutex_create("stpmtx", RT_IPC_FLAG_PRIO);
    if (state_mutex == RT_NULL)
    {
        rt_kprintf("create mutex failed\n");
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return -RT_ERROR;
    }

    result = rt_thread_init(&net_th,
                            "netini",
                            net_init_entry,
                            RT_NULL,
                            net_stack,
                            sizeof(net_stack),
                            THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_kprintf("init network thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    result = rt_thread_init(&sensor_th,
                            "sensi",
                            sensor_init_entry,
                            RT_NULL,
                            sensor_stack,
                            sizeof(sensor_stack),
                            THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_kprintf("init sensor thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    result = rt_thread_init(&storage_th,
                            "store",
                            storage_init_entry,
                            RT_NULL,
                            storage_stack,
                            sizeof(storage_stack),
                            THREAD_PRIORITY,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_kprintf("init storage thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    result = rt_thread_init(&business_th,
                            "biz",
                            business_entry,
                            RT_NULL,
                            business_stack,
                            sizeof(business_stack),
                            THREAD_PRIORITY - 1,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_kprintf("init business thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    result = rt_thread_startup(&net_th);
    if (result != RT_EOK)
    {
        rt_kprintf("startup network thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    result = rt_thread_startup(&sensor_th);
    if (result != RT_EOK)
    {
        rt_kprintf("startup sensor thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    result = rt_thread_startup(&storage_th);
    if (result != RT_EOK)
    {
        rt_kprintf("startup storage thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    result = rt_thread_startup(&business_th);
    if (result != RT_EOK)
    {
        rt_kprintf("startup business thread failed, err=%d\n", result);
        rt_mutex_delete(state_mutex);
        state_mutex = RT_NULL;
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;
        return result;
    }

    /*
     * 业务线程会在模块全部就绪后退出。
     * 为了让命令可再次启动，这里做一个简单等待，
     * 等业务线程打印完结果后再释放资源。
     */
    while (1)
    {
        rt_uint32_t flags = get_ready_flag();
        if ((flags & EVENT_ALL_READY) == EVENT_ALL_READY)
        {
            break;
        }
        rt_thread_mdelay(200);
    }

    rt_thread_mdelay(500);

    rt_event_delete(startup_event);
    startup_event = RT_NULL;

    rt_mutex_delete(state_mutex);
    state_mutex = RT_NULL;

    demo_running = 0;

    return RT_EOK;
}

MSH_CMD_EXPORT(startup_demo, startup condition check demo);
