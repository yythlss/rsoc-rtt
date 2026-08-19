/*
 * 练习三：系统启动条件检查（事件集）
 * 与 ex1/ex2/ex4 互斥，applications/ 下同一时刻只留一个。
 */
#include <rtthread.h>

#define EVENT_NET_READY     (1U << 0)
#define EVENT_SENSOR_READY  (1U << 1)
#define EVENT_STORAGE_READY (1U << 2)
#define EVENT_ALL           (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t init_event = RT_NULL;
static volatile rt_uint32_t g_ready = 0;   /* 记录已就绪模块，供超时打印用 */

static void mark_ready(rt_uint32_t flag)
{
    rt_base_t level = rt_hw_interrupt_disable();
    g_ready |= flag;
    rt_hw_interrupt_enable(level);
}

static void net_entry(void *p)
{
    rt_thread_mdelay(1000);
    rt_event_send(init_event, EVENT_NET_READY);
    mark_ready(EVENT_NET_READY);
    rt_kprintf("network ready\n");
}

static void sensor_entry(void *p)
{
    rt_thread_mdelay(1500);
    rt_event_send(init_event, EVENT_SENSOR_READY);
    mark_ready(EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
}

static void storage_entry(void *p)
{
    rt_thread_mdelay(5000);   /* 超时测试：故意设长，让业务先超时 */
    rt_event_send(init_event, EVENT_STORAGE_READY);
    mark_ready(EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
}

static void business_entry(void *p)
{
    rt_uint32_t recved;
    rt_err_t err;

    /* 先等 3 秒（AND 模式，三个事件都到才算数） */
    err = rt_event_recv(init_event, EVENT_ALL,
                        RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                        rt_tick_from_millisecond(3000), &recved);
    if (err != RT_EOK)
    {
        if (!(g_ready & EVENT_NET_READY))     rt_kprintf("wait modules timeout, network is not ready\n");
        if (!(g_ready & EVENT_SENSOR_READY))  rt_kprintf("wait modules timeout, sensor is not ready\n");
        if (!(g_ready & EVENT_STORAGE_READY)) rt_kprintf("wait modules timeout, storage is not ready\n");
        /* storage 还在初始化，继续等到它完成 */
        rt_event_recv(init_event, EVENT_ALL,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER, &recved);
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");
}

static int startup_init(void)
{
    rt_thread_t t;

    init_event = rt_event_create("init", RT_IPC_FLAG_FIFO);
    if (init_event == RT_NULL) return -RT_ENOMEM;

    t = rt_thread_create("net",      net_entry,      RT_NULL, 1024, 10, 5); if (t) rt_thread_startup(t);
    t = rt_thread_create("sensor",   sensor_entry,   RT_NULL, 1024, 10, 5); if (t) rt_thread_startup(t);
    t = rt_thread_create("storage",  storage_entry,  RT_NULL, 1024, 10, 5); if (t) rt_thread_startup(t);
    t = rt_thread_create("business", business_entry, RT_NULL, 1024,  8, 5); if (t) rt_thread_startup(t);
    return RT_EOK;
}
INIT_APP_EXPORT(startup_init);
