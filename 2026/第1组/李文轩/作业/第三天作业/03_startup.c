/*
 * 练习三：系统启动条件检查（事件集）
 *
 * 场景：设备启动前需要完成网络、传感器、存储三个模块的初始化，
 *       三个模块全部就绪后主业务线程才能启动。
 * 使用事件集：每个初始化线程完成后发送对应的就绪事件位，
 * 主业务线程以 AND 模式等待三个事件位全部到达；
 * 另做一次超时测试：存储模块需 5 秒，主业务线程每次最多等 3 秒，
 * 超时后打印尚未完成的模块并再次等待。
 *
 * MSH 命令：startup_demo
 *
 * 思考题：如果用三个独立信号量实现，与事件集相比有什么区别？
 * 答：事件集用一个对象即可等待"多个条件同时满足"，且能以 AND/OR 模式灵活
 *     组合、还能一次性查询已就绪的子集；用三个信号量则需要分别 take，
 *     无法表达"多个条件同时满足"的原子判断，代码也更繁琐、易出错。
 */

#include <rtthread.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

/* 各模块初始化时间（tick） */
#define INIT_NET_TICKS       1000
#define INIT_SENSOR_TICKS    2000
#define INIT_STORAGE_TICKS   5000 /* 超时测试：存储模块较慢 */

/* 主业务线程单次等待上限（tick） */
#define BUSINESS_WAIT_TICKS  3000

static rt_event_t s_boot_evt = RT_NULL; /* 启动事件集 */
static rt_sem_t s_done_sem = RT_NULL;   /* 业务线程完成信号量 */
static volatile rt_bool_t s_running = RT_FALSE; /* 防重复启动 */

/* 网络初始化线程 */
static void init_net_thread(void *param)
{
    rt_thread_mdelay(INIT_NET_TICKS);
    rt_kprintf("network ready\n");
    rt_event_send(s_boot_evt, EVENT_NET_READY);
}

/* 传感器初始化线程 */
static void init_sensor_thread(void *param)
{
    rt_thread_mdelay(INIT_SENSOR_TICKS);
    rt_kprintf("sensor ready\n");
    rt_event_send(s_boot_evt, EVENT_SENSOR_READY);
}

/* 存储初始化线程 */
static void init_storage_thread(void *param)
{
    rt_thread_mdelay(INIT_STORAGE_TICKS);
    rt_kprintf("storage ready\n");
    rt_event_send(s_boot_evt, EVENT_STORAGE_READY);
}

/* 主业务线程：AND 模式等待三个事件全部到达，超时则打印未就绪模块 */
static void business_thread(void *param)
{
    rt_uint32_t recved = 0;

    while (RT_TRUE)
    {
        rt_err_t res = rt_event_recv(s_boot_evt, EVENT_ALL_READY,
                                     RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                     BUSINESS_WAIT_TICKS, &recved);
        if (res == RT_EOK)
        {
            break; /* 三个模块全部就绪 */
        }

        /* 超时：查询当前已就绪的事件位，打印尚未完成的模块 */
        rt_event_recv(s_boot_evt, EVENT_ALL_READY, RT_EVENT_FLAG_OR, 0, &recved);
        rt_uint32_t missing = EVENT_ALL_READY & (~recved);
        if (missing & EVENT_NET_READY)
        {
            rt_kprintf("wait modules timeout, network is not ready\n");
        }
        if (missing & EVENT_SENSOR_READY)
        {
            rt_kprintf("wait modules timeout, sensor is not ready\n");
        }
        if (missing & EVENT_STORAGE_READY)
        {
            rt_kprintf("wait modules timeout, storage is not ready\n");
        }
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");

    /* 通知命令线程：业务线程已结束 */
    rt_sem_release(s_done_sem);
}

/* MSH 命令：startup_demo */
static int startup_demo(int argc, char *argv[])
{
    rt_thread_t net = RT_NULL;
    rt_thread_t sensor = RT_NULL;
    rt_thread_t storage = RT_NULL;
    rt_thread_t business = RT_NULL;

    if (s_running)
    {
        rt_kprintf("startup_demo is already running\n");
        return -1;
    }
    s_running = RT_TRUE;

    /* 1. 创建事件集 */
    s_boot_evt = rt_event_create("boot_evt", RT_IPC_FLAG_FIFO);
    if (s_boot_evt == RT_NULL)
    {
        rt_kprintf("create boot event failed\n");
        s_running = RT_FALSE;
        return -1;
    }

    /* 2. 创建完成信号量 */
    s_done_sem = rt_sem_create("bt_done", 0, RT_IPC_FLAG_FIFO);
    if (s_done_sem == RT_NULL)
    {
        rt_kprintf("create done semaphore failed\n");
        rt_event_delete(s_boot_evt);
        s_boot_evt = RT_NULL;
        s_running = RT_FALSE;
        return -1;
    }

    /* 3. 创建 3 个初始化线程 + 1 个主业务线程，并检查创建结果 */
    net      = rt_thread_create("init_net",  init_net_thread,     RT_NULL, 1024, 8, 10);
    sensor   = rt_thread_create("init_sen",  init_sensor_thread,  RT_NULL, 1024, 8, 10);
    storage  = rt_thread_create("init_sto",  init_storage_thread, RT_NULL, 1024, 8, 10);
    business = rt_thread_create("business",  business_thread,     RT_NULL, 1024, 7, 10);

    if (net == RT_NULL || sensor == RT_NULL || storage == RT_NULL || business == RT_NULL)
    {
        rt_kprintf("create startup thread failed\n");
        if (net)      rt_thread_delete(net);
        if (sensor)   rt_thread_delete(sensor);
        if (storage)  rt_thread_delete(storage);
        if (business) rt_thread_delete(business);
        rt_event_delete(s_boot_evt);
        rt_sem_delete(s_done_sem);
        s_boot_evt = RT_NULL;
        s_done_sem = RT_NULL;
        s_running = RT_FALSE;
        return -1;
    }

    /* 4. 启动所有线程 */
    rt_thread_startup(net);
    rt_thread_startup(sensor);
    rt_thread_startup(storage);
    rt_thread_startup(business);

    /* 5. 等待主业务线程完成 */
    if (rt_sem_take(s_done_sem, 20000) != RT_EOK)
    {
        rt_kprintf("startup_demo wait timeout\n");
    }

    /* 6. 释放资源 */
    rt_event_delete(s_boot_evt);
    rt_sem_delete(s_done_sem);
    s_boot_evt = RT_NULL;
    s_done_sem = RT_NULL;
    s_running = RT_FALSE;

    return 0;
}
MSH_CMD_EXPORT(startup_demo, system startup check with event set);
