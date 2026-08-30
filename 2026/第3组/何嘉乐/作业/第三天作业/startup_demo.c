#include <rtthread.h>

#define EVENT_NET_READY       (1U << 0)
#define EVENT_SENSOR_READY    (1U << 1)
#define EVENT_STORAGE_READY   (1U << 2)

#define EVENT_ALL_READY       (EVENT_NET_READY    | \
                               EVENT_SENSOR_READY | \
                               EVENT_STORAGE_READY)

#define THREAD_STACK_SIZE     1024
#define INIT_PRIORITY         20
#define BUSINESS_PRIORITY     21
#define THREAD_TIMESLICE      10

#define NET_DELAY_MS          1000
#define SENSOR_DELAY_MS       2000
#define STORAGE_DELAY_MS      5000
#define FIRST_WAIT_MS         3000

static rt_event_t startup_event = RT_NULL;

static rt_uint32_t ready_flags = 0;
static rt_uint8_t demo_running = 0;


/* 记录模块状态 */
static void set_ready_flag(rt_uint32_t flag)
{
    rt_enter_critical();

    ready_flags |= flag;

    rt_exit_critical();
}


/* 网络初始化线程 */
static void net_init_entry(void *parameter)
{
    rt_err_t result;

    rt_thread_mdelay(NET_DELAY_MS);

    set_ready_flag(EVENT_NET_READY);

    result = rt_event_send(startup_event,
                           EVENT_NET_READY);

    if (result == RT_EOK)
    {
        rt_kprintf("network ready\n");
    }
    else
    {
        rt_kprintf("network event send failed\n");
    }
}


/* 传感器初始化线程 */
static void sensor_init_entry(void *parameter)
{
    rt_err_t result;

    rt_thread_mdelay(SENSOR_DELAY_MS);

    set_ready_flag(EVENT_SENSOR_READY);

    result = rt_event_send(startup_event,
                           EVENT_SENSOR_READY);

    if (result == RT_EOK)
    {
        rt_kprintf("sensor ready\n");
    }
    else
    {
        rt_kprintf("sensor event send failed\n");
    }
}


/* 存储初始化线程 */
static void storage_init_entry(void *parameter)
{
    rt_err_t result;

    rt_thread_mdelay(STORAGE_DELAY_MS);

    set_ready_flag(EVENT_STORAGE_READY);

    result = rt_event_send(startup_event,
                           EVENT_STORAGE_READY);

    if (result == RT_EOK)
    {
        rt_kprintf("storage ready\n");
    }
    else
    {
        rt_kprintf("storage event send failed\n");
    }
}


/* 打印未完成模块 */
static void print_not_ready(void)
{
    rt_uint32_t flags;

    rt_enter_critical();

    flags = ready_flags;

    rt_exit_critical();

    rt_kprintf("wait modules timeout");

    if ((flags & EVENT_NET_READY) == 0)
    {
        rt_kprintf(", network is not ready");
    }

    if ((flags & EVENT_SENSOR_READY) == 0)
    {
        rt_kprintf(", sensor is not ready");
    }

    if ((flags & EVENT_STORAGE_READY) == 0)
    {
        rt_kprintf(", storage is not ready");
    }

    rt_kprintf("\n");
}


/* 主业务线程 */
static void business_entry(void *parameter)
{
    rt_uint32_t received;
    rt_err_t result;

    /* 第一次等待，最多 3 秒 */
    result = rt_event_recv(startup_event,
                           EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND,
                           rt_tick_from_millisecond(FIRST_WAIT_MS),
                           &received);

    if (result == -RT_ETIMEOUT)
    {
        print_not_ready();

        /* 超时后再次等待 */
        result = rt_event_recv(startup_event,
                               EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND |
                               RT_EVENT_FLAG_CLEAR,
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
        rt_kprintf("receive startup event failed: %d\n",
                   result);
    }

    /* 删除事件集 */
    if (startup_event != RT_NULL)
    {
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
    }

    rt_enter_critical();

    ready_flags = 0;
    demo_running = 0;

    rt_exit_critical();

    rt_kprintf("startup demo finished\n");
}


/* 系统启动实验 */
static void startup_demo(void)
{
    rt_thread_t net_tid;
    rt_thread_t sensor_tid;
    rt_thread_t storage_tid;
    rt_thread_t business_tid;

    /* 防止重复启动 */
    rt_enter_critical();

    if (demo_running)
    {
        rt_exit_critical();

        rt_kprintf("startup demo is already running\n");
        return;
    }

    demo_running = 1;
    ready_flags = 0;

    rt_exit_critical();

    /* 创建事件集 */
    startup_event = rt_event_create("startup",
                                    RT_IPC_FLAG_FIFO);

    if (startup_event == RT_NULL)
    {
        rt_kprintf("create startup event failed\n");

        demo_running = 0;
        return;
    }

    /* 创建网络线程 */
    net_tid = rt_thread_create("net_init",
                               net_init_entry,
                               RT_NULL,
                               THREAD_STACK_SIZE,
                               INIT_PRIORITY,
                               THREAD_TIMESLICE);

    if (net_tid == RT_NULL)
    {
        rt_kprintf("create network thread failed\n");

        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;

        return;
    }

    /* 创建传感器线程 */
    sensor_tid = rt_thread_create("sensor",
                                  sensor_init_entry,
                                  RT_NULL,
                                  THREAD_STACK_SIZE,
                                  INIT_PRIORITY,
                                  THREAD_TIMESLICE);

    if (sensor_tid == RT_NULL)
    {
        rt_kprintf("create sensor thread failed\n");

        rt_thread_delete(net_tid);

        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;

        return;
    }

    /* 创建存储线程 */
    storage_tid = rt_thread_create("storage",
                                   storage_init_entry,
                                   RT_NULL,
                                   THREAD_STACK_SIZE,
                                   INIT_PRIORITY,
                                   THREAD_TIMESLICE);

    if (storage_tid == RT_NULL)
    {
        rt_kprintf("create storage thread failed\n");

        rt_thread_delete(net_tid);
        rt_thread_delete(sensor_tid);

        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;

        return;
    }

    /* 创建业务线程 */
    business_tid = rt_thread_create("business",
                                    business_entry,
                                    RT_NULL,
                                    THREAD_STACK_SIZE,
                                    BUSINESS_PRIORITY,
                                    THREAD_TIMESLICE);

    if (business_tid == RT_NULL)
    {
        rt_kprintf("create business thread failed\n");

        rt_thread_delete(net_tid);
        rt_thread_delete(sensor_tid);
        rt_thread_delete(storage_tid);

        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        demo_running = 0;

        return;
    }

    rt_kprintf("startup demo start\n");

    /* 启动初始化线程 */
    rt_thread_startup(net_tid);
    rt_thread_startup(sensor_tid);
    rt_thread_startup(storage_tid);

    /* 启动业务线程 */
    rt_thread_startup(business_tid);
}

MSH_CMD_EXPORT(startup_demo, startup event demo);
