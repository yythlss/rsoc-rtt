/**
 * 【DAY3 练习三】系统启动条件检查 —— 事件集
 *
 * 场景：设备启动前需要完成三个独立模块的初始化（网络 / 传感器 / 存储），
 * 只有三个模块全部准备完成，主业务线程才能启动。
 *
 * 功能：
 *  1. 定义三个事件位：EVENT_NET_READY / EVENT_SENSOR_READY / EVENT_STORAGE_READY；
 *  2. 创建三个初始化线程（netinit 1s / sensor 2s / storage 5s），
 *     延时结束后发送对应的就绪事件；
 *  3. 创建主业务线程，用 AND 模式等待三个事件全部到达；
 *  4. 超时测试：业务线程最多等 3s，超时后打印尚未完成的模块，再次等待；
 *     （storage 需 5s，第一次等待必然超时，第二次等待时 storage 已就绪）
 *  5. 三个事件齐备后打印 "all modules are ready" 与 "business task started"。
 *
 * 设计说明（与 kernel_samples 示例的差异）：
 *  - 动态创建线程与事件集，所有创建结果均检查；
 *  - 业务线程用 AND + CLEAR 模式等待；超时后借助 "OR 模式 + 非清除"
 *    的非阻塞读取获取当前已置位的事件，据此打印缺失模块；
 *  - 任务可自然结束：命令等待全部线程退出后统一删除线程与事件对象，
 *    重复执行命令不会因同名对象残留而异常。
 */
#include <rtthread.h>

#define EVENT_NET_READY         (1U << 0)    /* 网络模块就绪事件 */
#define EVENT_SENSOR_READY      (1U << 1)    /* 传感器模块就绪事件 */
#define EVENT_STORAGE_READY     (1U << 2)    /* 存储模块就绪事件 */
#define EVENT_ALL               (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

#define INIT_PRIO               15           /* 初始化线程优先级 */
#define BUSINESS_PRIO           12           /* 业务线程优先级 */
#define THREAD_TIMESLICE        10
#define THREAD_STACK_SIZE       1024

#define BUSINESS_WAIT_TIMEOUT   3000         /* 业务线程等待模块就绪的超时（ms） */

static rt_event_t   startup_event = RT_NULL;    /* 启动就绪事件集 */
static rt_sem_t     done_sem      = RT_NULL;    /* 完成信号量：命令等待全部线程退出 */
static rt_thread_t  init_thread[3];             /* 三个初始化线程句柄 */
static rt_thread_t  business_thread = RT_NULL;  /* 业务线程句柄 */
static volatile rt_uint8_t demo_running = 0;    /* 任务运行标志，防止重复启动 */

/* 模块初始化参数表：延时时间 / 就绪事件位 / 模块名 */
struct init_info
{
    rt_uint32_t delay_ms;
    rt_uint32_t evt;
    const char *name;
};

static const struct init_info init_tab[3] =
{
    { 1000, EVENT_NET_READY,     "network" },
    { 2000, EVENT_SENSOR_READY,  "sensor"  },
    { 5000, EVENT_STORAGE_READY, "storage" },
};

static const char *init_thread_name[3] = { "netinit", "sensor", "storage" };

/* 初始化线程入口：延时 -> 发送就绪事件 -> 退出 */
static void init_thread_entry(void *param)
{
    const struct init_info *info = (const struct init_info *)param;

    rt_thread_mdelay(info->delay_ms);
    rt_event_send(startup_event, info->evt);
    rt_kprintf("%s ready\n", info->name);

    rt_sem_release(done_sem);       /* 通知命令：本线程已退出 */
}

/* 主业务线程入口：AND 模式等待三个事件，超时则报告缺失模块后再次等待 */
static void business_entry(void *param)
{
    rt_uint32_t e;
    rt_err_t ret;

    while (1)
    {
        ret = rt_event_recv(startup_event, EVENT_ALL,
                            RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                            BUSINESS_WAIT_TIMEOUT, &e);
        if (ret == RT_EOK)
        {
            break;                  /* 三个事件全部到达 */
        }

        /* 超时：非阻塞 OR 模式读取当前已置位的事件（不带 CLEAR，不改变状态） */
        e = 0;
        if (rt_event_recv(startup_event, EVENT_ALL, RT_EVENT_FLAG_OR, 0, &e) != RT_EOK)
        {
            e = 0;
        }

        /* 打印尚未完成的模块 */
        rt_kprintf("wait modules timeout, ");
        if (!(e & EVENT_NET_READY))
        {
            rt_kprintf("network ");
        }
        if (!(e & EVENT_SENSOR_READY))
        {
            rt_kprintf("sensor ");
        }
        if (!(e & EVENT_STORAGE_READY))
        {
            rt_kprintf("storage ");
        }
        rt_kprintf("is not ready\n");
        /* 再次等待（storage 就绪后第二轮等待成功） */
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");

    rt_sem_release(done_sem);       /* 通知命令：业务线程已退出 */
}

static int startup_demo(void)
{
    int i;

    if (demo_running)
    {
        rt_kprintf("startup_demo is already running\n");
        return -RT_ERROR;
    }

    /* 1. 创建事件集 */
    startup_event = rt_event_create("s_evt", RT_IPC_FLAG_PRIO);
    if (startup_event == RT_NULL)
    {
        rt_kprintf("create startup event failed\n");
        return -RT_ERROR;
    }

    /* 2. 创建完成信号量 */
    done_sem = rt_sem_create("done", 0, RT_IPC_FLAG_FIFO);
    if (done_sem == RT_NULL)
    {
        rt_event_delete(startup_event);
        startup_event = RT_NULL;
        rt_kprintf("create done semaphore failed\n");
        return -RT_ERROR;
    }

    /* 3. 创建三个初始化线程 */
    for (i = 0; i < 3; i++)
    {
        init_thread[i] = rt_thread_create(init_thread_name[i], init_thread_entry,
                                          (void *)&init_tab[i],
                                          THREAD_STACK_SIZE, INIT_PRIO, THREAD_TIMESLICE);
        if (init_thread[i] == RT_NULL)
        {
            int j;

            rt_kprintf("create thread %s failed\n", init_thread_name[i]);
            for (j = 0; j < i; j++)
            {
                rt_thread_delete(init_thread[j]);
            }
            rt_sem_delete(done_sem);
            rt_event_delete(startup_event);
            done_sem       = RT_NULL;
            startup_event  = RT_NULL;
            return -RT_ERROR;
        }
    }

    /* 4. 创建主业务线程 */
    business_thread = rt_thread_create("business", business_entry, RT_NULL,
                                       THREAD_STACK_SIZE, BUSINESS_PRIO, THREAD_TIMESLICE);
    if (business_thread == RT_NULL)
    {
        rt_kprintf("create thread business failed\n");
        for (i = 0; i < 3; i++)
        {
            rt_thread_delete(init_thread[i]);
        }
        rt_sem_delete(done_sem);
        rt_event_delete(startup_event);
        done_sem       = RT_NULL;
        startup_event  = RT_NULL;
        return -RT_ERROR;
    }

    /* 5. 全部创建成功，统一启动 */
    demo_running = 1;
    for (i = 0; i < 3; i++)
    {
        rt_thread_startup(init_thread[i]);
    }
    rt_thread_startup(business_thread);

    /* 6. 等待 4 个线程全部退出 */
    for (i = 0; i < 4; i++)
    {
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    }

    /* 7. 统一清理，保证可重复执行；动态线程退出后由空闲线程自动回收，无需手动删除 */
rt_sem_delete(done_sem);
rt_event_delete(startup_event);
done_sem       = RT_NULL;
startup_event  = RT_NULL;
demo_running   = 0;

rt_kprintf("startup demo finished\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, startup condition check demo with event set);
