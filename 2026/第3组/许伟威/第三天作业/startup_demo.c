/*
 * startup_demo.c - 练习三：系统启动条件检查（事件集）
 *
 * 场景：设备启动前要完成网络、传感器、存储三个模块的初始化，全部就绪后
 *       主业务线程才能启动。主业务用 AND 模式等待三个事件位全部到达。
 *
 * 超时测试：存储模块初始化 5 秒，主业务最多等 3 秒，超时打印未就绪模块
 *       并再次等待。
 *
 * 用法：MSH 中输入 startup_demo 启动。
 */
#include <rtthread.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL            (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t startup_event = RT_NULL;

static void net_init_entry(void *param)
{
    rt_thread_mdelay(1000);
    rt_kprintf("network ready\n");
    rt_event_send(startup_event, EVENT_NET_READY);
}

static void sensor_init_entry(void *param)
{
    rt_thread_mdelay(2000);
    rt_kprintf("sensor ready\n");
    rt_event_send(startup_event, EVENT_SENSOR_READY);
}

static void storage_init_entry(void *param)
{
    rt_thread_mdelay(5000);
    rt_kprintf("storage ready\n");
    rt_event_send(startup_event, EVENT_STORAGE_READY);
}

static void business_entry(void *param)
{
    rt_uint32_t recved = 0;

    while (1)
    {
        rt_err_t ret = rt_event_recv(startup_event, EVENT_ALL,
                                     RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                     rt_tick_from_millisecond(3000), &recved);
        if (ret == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
        else
        {
            rt_uint32_t cur = startup_event->set;

            rt_kprintf("wait modules timeout");
            if (!(cur & EVENT_NET_READY))
                rt_kprintf(", network is not ready");
            if (!(cur & EVENT_SENSOR_READY))
                rt_kprintf(", sensor is not ready");
            if (!(cur & EVENT_STORAGE_READY))
                rt_kprintf(", storage is not ready");
            rt_kprintf("\n");
        }
    }
}

static void startup_demo(int argc, char **argv)
{
    rt_thread_t t;

    if (startup_event != RT_NULL)
    {
        rt_kprintf("startup demo is already running\n");
        return;
    }

    startup_event = rt_event_create("sevent", RT_IPC_FLAG_PRIO);
    if (startup_event == RT_NULL)
    {
        rt_kprintf("create event failed\n");
        return;
    }

    t = rt_thread_create("netinit", net_init_entry, RT_NULL, 1024, 21, 5);
    if (t != RT_NULL) rt_thread_startup(t);

    t = rt_thread_create("seninit", sensor_init_entry, RT_NULL, 1024, 21, 5);
    if (t != RT_NULL) rt_thread_startup(t);

    t = rt_thread_create("storinit", storage_init_entry, RT_NULL, 1024, 21, 5);
    if (t != RT_NULL) rt_thread_startup(t);

    t = rt_thread_create("business", business_entry, RT_NULL, 1024, 20, 5);
    if (t != RT_NULL) rt_thread_startup(t);
}
MSH_CMD_EXPORT(startup_demo, startup condition check with event set);
