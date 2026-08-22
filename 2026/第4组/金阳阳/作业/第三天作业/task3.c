/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     Student      System startup condition check
 *
 * 练习三：系统启动条件检查（事件集）
 *
 * 设备启动前需要完成三个独立模块的初始化：
 *   网络模块、传感器模块、存储模块。
 * 只有三个模块全部就绪，主业务线程才能启动。
 *
 * MSH 命令: startup_demo
 */

#include <rtthread.h>

#define DBG_TAG "startup"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ============ 事件位定义 ============ */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

/* ============ 启动时序配置 ============ */
#define NET_INIT_TIME_MS      1000    /* 网络模块初始化耗时 */
#define SENSOR_INIT_TIME_MS   2000    /* 传感器模块初始化耗时 */
#define STORAGE_INIT_TIME_MS  5000    /* 存储模块初始化耗时(超过等待超时) */
#define WAIT_TIMEOUT_MS       3000    /* 主业务线程等待超时 */

#define THREAD_PRIORITY       16
#define THREAD_STACK_SIZE     512

/* ============ 全局变量 ============ */
static rt_event_t g_startup_event = RT_NULL;      /* 启动事件集 */
static volatile rt_uint32_t g_ready_state = 0;    /* 模块就绪状态(供超时打印使用) */
static volatile int g_running = 0;                /* 演示运行标志 */

/* ============ 模块初始化线程参数 ============ */
typedef struct
{
    const char *name;         /* 模块名 */
    rt_uint32_t event_bit;    /* 对应的就绪事件位 */
    rt_int32_t init_time_ms;  /* 模拟初始化耗时 */
} module_arg_t;

static module_arg_t g_net_arg     = { "network", EVENT_NET_READY,      NET_INIT_TIME_MS };
static module_arg_t g_sensor_arg  = { "sensor",  EVENT_SENSOR_READY,   SENSOR_INIT_TIME_MS };
static module_arg_t g_storage_arg = { "storage", EVENT_STORAGE_READY,  STORAGE_INIT_TIME_MS };

/*
 * 模块初始化线程入口
 * 延时模拟初始化过程，完成后发送就绪事件并记录就绪状态
 */
static void module_init_entry(void *parameter)
{
    module_arg_t *arg = (module_arg_t *)parameter;

    /* 模拟初始化耗时 */
    rt_thread_mdelay(arg->init_time_ms);

    /* 发送就绪事件 */
    rt_event_send(g_startup_event, arg->event_bit);

    /* 记录就绪状态（供主业务线程超时时打印未就绪模块） */
    rt_enter_critical();
    g_ready_state |= arg->event_bit;
    rt_exit_critical();

    rt_kprintf("%s ready\n", arg->name);
}

/*
 * 打印尚未就绪的模块
 */
static void print_missing_modules(void)
{
    rt_base_t level;
    rt_uint32_t state;
    int first = 1;

    /* 关中断读取共享状态，保证读取一致性 */
    level = rt_hw_interrupt_disable();
    state = g_ready_state;
    rt_hw_interrupt_enable(level);

    if (!(state & EVENT_NET_READY))
    {
        rt_kprintf("network is not ready");
        first = 0;
    }
    if (!(state & EVENT_SENSOR_READY))
    {
        rt_kprintf("%ssensor is not ready", first ? "" : ", ");
        first = 0;
    }
    if (!(state & EVENT_STORAGE_READY))
    {
        rt_kprintf("%sstorage is not ready", first ? "" : ", ");
    }
}

/*
 * 主业务线程入口
 * AND 模式等待三个模块全部就绪；超时则打印未就绪模块并再次等待
 */
static void business_entry(void *parameter)
{
    rt_err_t result;
    rt_uint32_t recved = 0;
    (void)parameter;

    while (1)
    {
        /* AND 模式：等待三个事件位全部置位，最多等 3 秒。
         * 超时不带 CLEAR，已就绪的事件位不会被清除，可直接再次等待。 */
        result = rt_event_recv(g_startup_event, EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND,
                               rt_tick_from_millisecond(WAIT_TIMEOUT_MS),
                               &recved);
        if (result == RT_EOK)
        {
            break;
        }

        /* 超时：打印尚未完成的模块，然后继续等待 */
        rt_kprintf("wait modules timeout, ");
        print_missing_modules();
        rt_kprintf("\n");
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");

    /* 演示结束：释放事件集并复位运行标志 */
    rt_event_delete(g_startup_event);
    g_startup_event = RT_NULL;
    g_running = 0;
}

/*
 * MSH 命令：startup_demo
 */
static int startup_demo(int argc, char *argv[])
{
    rt_thread_t tid_net = RT_NULL;
    rt_thread_t tid_sensor = RT_NULL;
    rt_thread_t tid_storage = RT_NULL;
    rt_thread_t tid_business = RT_NULL;

    /* 已在运行则拒绝重复启动 */
    if (g_running)
    {
        rt_kprintf("startup demo is already running, please wait...\n");
        return RT_EBUSY;
    }

    /* 创建事件集 */
    g_startup_event = rt_event_create("start_ev", RT_IPC_FLAG_PRIO);
    if (g_startup_event == RT_NULL)
    {
        rt_kprintf("Error: failed to create event!\n");
        return RT_ERROR;
    }

    g_ready_state = 0;

    /* 创建三个模块初始化线程 */
    tid_net = rt_thread_create("net_init", module_init_entry, &g_net_arg,
                               THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_net == RT_NULL)
    {
        rt_kprintf("Error: failed to create network init thread!\n");
        goto cleanup_fail;
    }
    tid_sensor = rt_thread_create("sen_init", module_init_entry, &g_sensor_arg,
                                  THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_sensor == RT_NULL)
    {
        rt_kprintf("Error: failed to create sensor init thread!\n");
        goto cleanup_fail;
    }
    tid_storage = rt_thread_create("sto_init", module_init_entry, &g_storage_arg,
                                   THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_storage == RT_NULL)
    {
        rt_kprintf("Error: failed to create storage init thread!\n");
        goto cleanup_fail;
    }

    /* 创建主业务线程 */
    tid_business = rt_thread_create("business", business_entry, RT_NULL,
                                    THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_business == RT_NULL)
    {
        rt_kprintf("Error: failed to create business thread!\n");
        goto cleanup_fail;
    }

    /* 启动所有线程 */
    if (rt_thread_startup(tid_net) != RT_EOK ||
        rt_thread_startup(tid_sensor) != RT_EOK ||
        rt_thread_startup(tid_storage) != RT_EOK ||
        rt_thread_startup(tid_business) != RT_EOK)
    {
        rt_kprintf("Error: failed to start threads!\n");
        goto cleanup_fail;
    }

    g_running = 1;
    rt_kprintf("startup demo: waiting for all modules ready (timeout %d ms)\n",
               WAIT_TIMEOUT_MS);
    return RT_EOK;

cleanup_fail:
    /* 创建/启动失败：清理已创建的对象。
     * 已退出的线程为 CLOSE 状态，rt_thread_delete 为空操作，可安全重复调用。 */
    if (tid_net != RT_NULL)
        rt_thread_delete(tid_net);
    if (tid_sensor != RT_NULL)
        rt_thread_delete(tid_sensor);
    if (tid_storage != RT_NULL)
        rt_thread_delete(tid_storage);
    if (tid_business != RT_NULL)
        rt_thread_delete(tid_business);
    if (g_startup_event != RT_NULL)
    {
        rt_event_delete(g_startup_event);
        g_startup_event = RT_NULL;
    }
    return RT_ERROR;
}
MSH_CMD_EXPORT(startup_demo, system startup condition check demo);

/*
 * 主函数
 */
int main(void)
{
    rt_kprintf("System startup check demo ready.\n");
    rt_kprintf("Command: startup_demo\n");
    return RT_EOK;
}
