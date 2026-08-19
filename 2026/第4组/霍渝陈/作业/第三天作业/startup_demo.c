/*
 * ============================================================
 * 练习三：系统启动条件检查（事件集）
 *
 * 场景：设备启动前，网络/传感器/存储三个模块各自独立初始化，
 *       全部就绪后主业务线程才能启动。
 *
 * 设计思路：
 *   1. 事件集 + 3 个事件位：NET=1<<0, SENSOR=1<<1, STORAGE=1<<2
 *   2. 三个初始化线程延时不同时间后 send 各自就绪事件
 *   3. 主业务线程用 AND 模式等全部事件（缺一不可）
 *   4. 超时测试：业务先只等 3 秒，存储模块要 5 秒 → 超时打印
 *      未就绪模块，然后继续等待直到全部就绪
 *
 * MSH 命令：startup_demo
 * ============================================================
 */

#include <rtthread.h>
#include <rtdevice.h>

#define EVENT_NET_READY      (1U << 0)      /* 网络就绪事件位 */
#define EVENT_SENSOR_READY   (1U << 1)      /* 传感器就绪事件位 */
#define EVENT_STORAGE_READY  (1U << 2)      /* 存储就绪事件位 */
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

#define NET_INIT_TIME        (1 * RT_TICK_PER_SECOND)   /* 网络初始化 1 秒 */
#define SENSOR_INIT_TIME     (2 * RT_TICK_PER_SECOND)   /* 传感器初始化 2 秒 */
#define STORAGE_INIT_TIME    (5 * RT_TICK_PER_SECOND)   /* 存储初始化 5 秒（故意拖慢） */
#define BUSINESS_WAIT_TIME   (3 * RT_TICK_PER_SECOND)   /* 业务线程第一轮只等 3 秒 */

static rt_event_t startup_evt = RT_NULL;    /* 启动事件集 */
static volatile rt_uint8_t threads_done = 0;/* 已结束的线程数 */
static volatile rt_uint8_t task_running = 0;/* 任务是否在运行（防重复启动） */

/* 三个模块的初始化参数表 */
struct module_def
{
    rt_uint32_t init_time;      /* 初始化耗时 */
    rt_uint32_t ready_bit;      /* 对应的事件位 */
    const char *name;           /* 模块名 */
};

static const struct module_def modules[] =
{
    { NET_INIT_TIME,     EVENT_NET_READY,     "network" },
    { SENSOR_INIT_TIME,  EVENT_SENSOR_READY,  "sensor"  },
    { STORAGE_INIT_TIME, EVENT_STORAGE_READY, "storage" },
};

/* ------------------------------------------------------------
 * 初始化线程：延时模拟初始化 → 打印 ready → 发送就绪事件
 * param 传模块下标（0=network, 1=sensor, 2=storage）
 * ---------------------------------------------------------- */
static void init_module_entry(void *param)
{
    int idx = (int)(rt_ubase_t)param;

    /* 模拟模块初始化耗时 */
    rt_thread_mdelay(modules[idx].init_time);

    /* 先打印再发事件，保证串口输出顺序稳定 */
    rt_kprintf("%s ready\n", modules[idx].name);
    rt_event_send(startup_evt, modules[idx].ready_bit);

    threads_done++;
}

/* ------------------------------------------------------------
 * 主业务线程：AND 模式等三个事件全部到达
 * 第一轮超时 3 秒 → 打印未就绪模块 → 第二轮不限时等齐
 * ---------------------------------------------------------- */
static void business_entry(void *param)
{
    rt_uint32_t recved = 0;
    rt_err_t ret;

    /* 第一轮：AND 模式等全部事件，最多 3 秒 */
    ret = rt_event_recv(startup_evt, EVENT_ALL_READY,
                        RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                        BUSINESS_WAIT_TIME, &recved);
    if (ret == RT_ETIMEOUT)
    {
        /* 超时：recved 里是已经到位的位，差谁就打印谁 */
        if (!(recved & EVENT_NET_READY))
            rt_kprintf("wait modules timeout, network is not ready\n");
        if (!(recved & EVENT_SENSOR_READY))
            rt_kprintf("wait modules timeout, sensor is not ready\n");
        if (!(recved & EVENT_STORAGE_READY))
            rt_kprintf("wait modules timeout, storage is not ready\n");
    }

    /* 第二轮：不限时，继续等直到三个事件全部到达 */
    ret = rt_event_recv(startup_evt, EVENT_ALL_READY,
                        RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                        RT_WAITING_FOREVER, &recved);
    if (ret == RT_EOK)
    {
        rt_kprintf("all modules are ready\n");
        rt_kprintf("business task started\n");
    }

    threads_done++;
}

/* ------------------------------------------------------------
 * 控制线程：创建事件集 + 4 个线程，等全部结束后清理
 * ---------------------------------------------------------- */
static void startup_ctrl_entry(void *param)
{
    rt_thread_t th[4];
    rt_uint8_t created = 0;
    int i;

    /* 1. 创建事件集，检查创建结果 */
    startup_evt = rt_event_create("startup_evt", RT_IPC_FLAG_PRIO);
    if (startup_evt == RT_NULL)
    {
        rt_kprintf("startup: create event failed\n");
        task_running = 0;
        return;
    }

    /* 2. 创建三个初始化线程 */
    for (i = 0; i < 3; i++)
    {
        th[i] = rt_thread_create("init_mod", init_module_entry,
                                 (void *)(rt_ubase_t)i, 1024, 20, 10);
        if (th[i] == RT_NULL)
        {
            rt_kprintf("startup: create init thread %d failed\n", i);
            break;
        }
        rt_thread_startup(th[i]);
        created++;
    }

    /* 3. 创建主业务线程（优先级更高） */
    th[3] = rt_thread_create("business", business_entry, RT_NULL, 1024, 15, 10);
    if (th[3] != RT_NULL)
    {
        rt_thread_startup(th[3]);
        created++;
    }
    else
    {
        rt_kprintf("startup: create business thread failed\n");
    }

    /* 4. 等所有线程结束（3 个初始化 + 1 个业务） */
    while (threads_done < created)
        rt_thread_mdelay(50);

    /* 5. 收尾：删事件集、复位状态 */
    rt_event_delete(startup_evt);
    startup_evt = RT_NULL;
    threads_done = 0;
    task_running = 0;
    rt_kprintf("startup: demo finished, resources released\n");
}

/* ------------------------------------------------------------
 * MSH 命令入口：startup_demo
 * ---------------------------------------------------------- */
static void startup_demo(void)
{
    rt_thread_t ctrl;

    if (task_running)
    {
        rt_kprintf("startup_demo is already running\n");
        return;
    }
    task_running = 1;

    ctrl = rt_thread_create("startup_ctrl", startup_ctrl_entry, RT_NULL, 2048, 12, 10);
    if (ctrl == RT_NULL)
    {
        rt_kprintf("startup_demo: create control thread failed\n");
        task_running = 0;
        return;
    }
    rt_thread_startup(ctrl);
}

/* 导出为 msh 命令 */
MSH_CMD_EXPORT(startup_demo, startup demo 3 modules init business waits events);
