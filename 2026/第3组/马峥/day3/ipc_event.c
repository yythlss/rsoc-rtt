/*
 * 练习三：系统启动条件检查
 * 机制：事件集（rt_event）+ AND 模式等待
 *
 * 场景：3个模块（网络/传感器/存储）初始化，主业务等待全部就绪
 * 原理：rt_event 等待多个事件位（AND 模式），超时重试
 */

#include <rtthread.h>

/* 事件位定义 */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY   (1U << 2)
#define ALL_EVENTS           (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

/* 初始化延时配置（毫秒） */
#define NET_INIT_DELAY_MS    800
#define SENSOR_INIT_DELAY_MS 1500
#define STORAGE_INIT_DELAY_MS 5000   /* 故意设长，触发超时 */

#define BUSINESS_WAIT_MS     3000    /* 主业务最多等 3 秒 */

/* 事件集对象 */
static rt_event_t event_startup = RT_NULL;

/* 标记各模块是否已就绪 */
static rt_uint8_t net_ready    = 0;
static rt_uint8_t sensor_ready = 0;
static rt_uint8_t storage_ready = 0;

/* ========== 初始化线程 ========== */
static void net_init_thread(void *parameter)
{
    (void)parameter;
    rt_thread_mdelay(NET_INIT_DELAY_MS);
    net_ready = 1;
    rt_event_send(event_startup, EVENT_NET_READY);
    rt_kprintf("network ready\n");
}

static void sensor_init_thread(void *parameter)
{
    (void)parameter;
    rt_thread_mdelay(SENSOR_INIT_DELAY_MS);
    sensor_ready = 1;
    rt_event_send(event_startup, EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
}

static void storage_init_thread(void *parameter)
{
    (void)parameter;
    rt_thread_mdelay(STORAGE_INIT_DELAY_MS);
    storage_ready = 1;
    rt_event_send(event_startup, EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
}

/* ========== 主业务线程 ========== */
static void business_thread(void *parameter)
{
    (void)parameter;
    rt_err_t err;
    rt_uint32_t recved;

    while (1)
    {
        rt_kprintf("business thread: waiting for all modules...\n");

        /* 等待所有三个事件（AND 模式），超时 BUSINESS_WAIT_MS */
        err = rt_event_recv(event_startup,
                           ALL_EVENTS,
                           RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                           rt_tick_from_millisecond(BUSINESS_WAIT_MS),
                           &recved);

        if (err == RT_EOK)
        {
            /* 所有事件都到达了 */
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
        else if (err == -RT_ETIMEOUT)
        {
            /* 超时，检查哪些模块还未就绪 */
            rt_kprintf("wait modules timeout, ");
            if (!net_ready)    rt_kprintf("network ");
            if (!sensor_ready) rt_kprintf("sensor ");
            if (!storage_ready) rt_kprintf("storage ");
            rt_kprintf("not ready\n");

            /* 继续等待（重新触发 rt_event_recv） */
        }
    }
}

/* ========== 线程栈和 TCB ========== */
static struct rt_thread net_thread, sensor_thread, storage_thread, business;
static rt_uint8_t net_stack[512], sensor_stack[512], storage_stack[512], business_stack[512];

/* ========== MSH 命令入口 ========== */
static int startup_demo(void)
{
    rt_thread_t t;

    /* 防止重复执行 */
    t = rt_thread_find("net_init");
    if (t != RT_NULL || rt_thread_find("business") != RT_NULL)
    {
        rt_kprintf("[startup_demo] demo already running, skip\n");
        return -1;
    }

    /* 重置就绪标记 */
    net_ready = 0; sensor_ready = 0; storage_ready = 0;

    /* 创建事件集 */
    event_startup = rt_event_create("evt_startup", RT_IPC_FLAG_FIFO);
    if (event_startup == RT_NULL)
    {
        rt_kprintf("[startup_demo] ERROR: event create failed\n");
        return -1;
    }

    rt_err_t err;

    /* 创建网络初始化线程 */
    err = rt_thread_init(&net_thread, "net_init", net_init_thread,
                         RT_NULL, net_stack, sizeof(net_stack), 10, 5);
    if (err != RT_EOK) { rt_kprintf("net_init init failed\n"); return -1; }
    rt_thread_startup(&net_thread);

    /* 创建传感器初始化线程 */
    err = rt_thread_init(&sensor_thread, "sensor_init", sensor_init_thread,
                         RT_NULL, sensor_stack, sizeof(sensor_stack), 10, 5);
    if (err != RT_EOK) { rt_kprintf("sensor_init init failed\n"); return -1; }
    rt_thread_startup(&sensor_thread);

    /* 创建存储初始化线程 */
    err = rt_thread_init(&storage_thread, "storage_init", storage_init_thread,
                         RT_NULL, storage_stack, sizeof(storage_stack), 10, 5);
    if (err != RT_EOK) { rt_kprintf("storage_init init failed\n"); return -1; }
    rt_thread_startup(&storage_thread);

    /* 创建主业务线程 */
    err = rt_thread_init(&business, "business", business_thread,
                         RT_NULL, business_stack, sizeof(business_stack), 8, 10);
    if (err != RT_EOK) { rt_kprintf("business init failed\n"); return -1; }
    rt_thread_startup(&business);

    rt_kprintf("[startup_demo] all init threads started\n");
    return 0;
}
MSH_CMD_EXPORT(startup_demo, system startup check with event set);
