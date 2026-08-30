/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     24527       the first version
 */

#include <rtthread.h>
#include <finsh.h>

/* 三个模块对应的事件位 */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)

#define EVENT_ALL_READY      (EVENT_NET_READY | \
                              EVENT_SENSOR_READY | \
                              EVENT_STORAGE_READY)

#define THREAD_PRIORITY      25
#define THREAD_STACK_SIZE    1024
#define THREAD_TIMESLICE     5

/* 模块初始化时间 */
#define NET_INIT_MS          1000
#define SENSOR_INIT_MS       2000
#define STORAGE_INIT_MS      5000

/* 主业务线程第一次最多等待 3 秒 */
#define BUSINESS_WAIT_MS     3000

/* 系统启动事件集 */
static rt_event_t startup_event = RT_NULL;

/* 线程句柄 */
static rt_thread_t net_thread = RT_NULL;
static rt_thread_t sensor_thread = RT_NULL;
static rt_thread_t storage_thread = RT_NULL;
static rt_thread_t business_thread = RT_NULL;

/* 防止重复启动 */
static rt_bool_t demo_running = RT_FALSE;


/**
 * @brief 网络模块初始化线程。
 *
 * 模拟网络模块初始化 1 秒，完成后发送网络就绪事件。
 *
 * @param parameter 未使用。
 */
static void net_init_entry(void *parameter)
{
    rt_thread_mdelay(NET_INIT_MS);

    rt_event_send(startup_event, EVENT_NET_READY);

    rt_kprintf("network ready\n");
}


/**
 * @brief 传感器模块初始化线程。
 *
 * 模拟传感器模块初始化 2 秒，完成后发送传感器就绪事件。
 *
 * @param parameter 未使用。
 */
static void sensor_init_entry(void *parameter)
{
    rt_thread_mdelay(SENSOR_INIT_MS);

    rt_event_send(startup_event, EVENT_SENSOR_READY);

    rt_kprintf("sensor ready\n");
}


/**
 * @brief 存储模块初始化线程。
 *
 * 为测试超时，将存储模块初始化时间设置为 5 秒，
 * 完成后发送存储就绪事件。
 *
 * @param parameter 未使用。
 */
static void storage_init_entry(void *parameter)
{
    rt_thread_mdelay(STORAGE_INIT_MS);

    rt_event_send(startup_event, EVENT_STORAGE_READY);

    rt_kprintf("storage ready\n");
}


/**
 * @brief 打印当前尚未完成的模块。
 *
 * 通过事件集当前已经到达的事件位，
 * 判断网络、传感器和存储模块中哪些还没有准备完成。
 */
static void print_not_ready_modules(void)
{
    rt_uint32_t received = 0;

    /* 使用 OR 模式立即读取当前已经到达的事件。 */
    rt_event_recv(startup_event,
                  EVENT_ALL_READY,
                  RT_EVENT_FLAG_OR,
                  0,
                  &received);

    rt_kprintf("wait modules timeout");

    if ((received & EVENT_NET_READY) == 0)
    {
        rt_kprintf(", network is not ready");
    }

    if ((received & EVENT_SENSOR_READY) == 0)
    {
        rt_kprintf(", sensor is not ready");
    }

    if ((received & EVENT_STORAGE_READY) == 0)
    {
        rt_kprintf(", storage is not ready");
    }

    rt_kprintf("\n");
}


/**
 * @brief 主业务线程。
 *
 * 第一次使用 AND 模式等待三个模块全部就绪，
 * 最多等待 3 秒。
 *
 * 如果发生超时，则打印尚未完成的模块，
 * 随后再次使用 AND 模式等待，直到三个事件全部到达。
 *
 * @param parameter 未使用。
 */
static void business_entry(void *parameter)
{
    rt_err_t result;
    rt_uint32_t received = 0;

    /*
     * 第一次等待：
     * 必须三个事件全部到达，最长等待 3 秒。
     */
    result = rt_event_recv(
        startup_event,
        EVENT_ALL_READY,
        RT_EVENT_FLAG_AND,
        rt_tick_from_millisecond(BUSINESS_WAIT_MS),
        &received);

    if (result == -RT_ETIMEOUT)
    {
        /* 超时后打印尚未完成的模块 */
        print_not_ready_modules();

        /*
         * 第二次继续等待。
         * 使用 AND 模式，只有三个模块全部完成后才会返回。
         */
        result = rt_event_recv(
            startup_event,
            EVENT_ALL_READY,
            RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
            RT_WAITING_FOREVER,
            &received);
    }

    if (result == RT_EOK)
    {
        rt_kprintf("all modules are ready\n");
        rt_kprintf("business task started\n");
    }
    else
    {
        rt_kprintf("wait startup event failed, error = %d\n", result);
    }

    /* 业务线程负责释放事件集。 */
    rt_event_delete(startup_event);
    startup_event = RT_NULL;
    demo_running = RT_FALSE;
}


/**
 * @brief 创建并启动一个线程。
 *
 * 用于减少四个线程重复的创建和启动代码。
 *
 * @param thread 保存线程句柄的地址。
 * @param name 线程名称。
 * @param entry 线程入口函数。
 *
 * @return RT_EOK 创建并启动成功。
 * @return -RT_ERROR 创建或启动失败。
 */
static rt_err_t create_start_thread(rt_thread_t *thread,
                                    const char *name,
                                    void (*entry)(void *parameter))
{
    rt_err_t result;

    *thread = rt_thread_create(name,
                               entry,
                               RT_NULL,
                               THREAD_STACK_SIZE,
                               THREAD_PRIORITY,
                               THREAD_TIMESLICE);

    if (*thread == RT_NULL)
    {
        rt_kprintf("create %s thread failed\n", name);
        return -RT_ERROR;
    }

    result = rt_thread_startup(*thread);

    if (result != RT_EOK)
    {
        rt_kprintf("startup %s thread failed\n", name);

        rt_thread_delete(*thread);
        *thread = RT_NULL;

        return result;
    }

    return RT_EOK;
}


/**
 * @brief 启动系统启动条件检查实验。
 *
 * 创建网络、传感器、存储和主业务四个线程。
 * 三个初始化线程分别发送对应事件位，
 * 主业务线程必须使用 AND 模式等待所有模块完成。
 *
 * @return RT_EOK 启动成功。
 * @return -RT_ERROR 事件集或线程创建失败。
 */
static rt_err_t startup_demo(void)
{
    rt_err_t result;

    if (demo_running == RT_TRUE)
    {
        rt_kprintf("system start demo is already running\n");
        return RT_EOK;
    }

    /* 创建动态事件集 */
    startup_event = rt_event_create("startup", RT_IPC_FLAG_FIFO);

    if (startup_event == RT_NULL)
    {
        rt_kprintf("create startup event failed\n");
        return -RT_ERROR;
    }

    demo_running = RT_TRUE;

    /* 创建并启动三个初始化线程和一个业务线程 */
    result = create_start_thread(&net_thread,
                                 "net_init",
                                 net_init_entry);
    if (result != RT_EOK)
    {
        goto __exit;
    }

    result = create_start_thread(&sensor_thread,
                                 "sensor_init",
                                 sensor_init_entry);
    if (result != RT_EOK)
    {
        goto __exit;
    }

    result = create_start_thread(&storage_thread,
                                 "storage_init",
                                 storage_init_entry);
    if (result != RT_EOK)
    {
        goto __exit;
    }

    result = create_start_thread(&business_thread,
                                 "business",
                                 business_entry);
    if (result != RT_EOK)
    {
        goto __exit;
    }

    return RT_EOK;

__exit:

    /* 创建失败时删除事件集。 */
    if (startup_event != RT_NULL)
    {
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
    }

    demo_running = RT_FALSE;

    return -RT_ERROR;
}


/* 导出为 MSH 命令 */
MSH_CMD_EXPORT(startup_demo, startup demo);
