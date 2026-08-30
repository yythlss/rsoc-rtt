/*
 * 【RSOC26】第3天作业 - 练习三：系统启动条件检查
 *
 * 场景：三个模块独立初始化（网络/传感器/存储），全部完成后主业务才能启动。
 * 使用事件集（AND 模式）等待多个就绪信号。
 *
 * 关键 IPC 对象：rt_event_t（事件集）
 *   发送方：rt_event_send() — 置位一个或多个事件位
 *   接收方：rt_event_recv() — 按 AND/OR 逻辑等待指定事件位
 */

#include <rtthread.h>

/* ────────── 事件位定义 ────────── */
#define EVENT_NET_READY       (1U << 0)    /* bit 0: 网络模块就绪 */
#define EVENT_SENSOR_READY    (1U << 1)    /* bit 1: 传感器模块就绪 */
#define EVENT_STORAGE_READY   (1U << 2)    /* bit 2: 存储模块就绪 */
#define ALL_READY    (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t startup_event = RT_NULL;

/* ────────── 模块初始化线程参数结构 ────────── */
struct module_param
{
    const char *name;
    rt_uint32_t delay_ms;
    rt_uint32_t event_flag;
};

/*
 * 通用初始化线程
 * 通过结构体参数获取名称、延迟和事件位——避免指针比较的不确定性
 */
static void module_init_thread(void *param)
{
    struct module_param *mp = (struct module_param *)param;

    rt_kprintf("[init] %s: starting, will take %d ms\n", mp->name, mp->delay_ms);

    rt_thread_mdelay(mp->delay_ms);

    rt_kprintf("%s ready (took %d ms)\n", mp->name, mp->delay_ms);

    rt_event_send(startup_event, mp->event_flag);
}

/*
 * 主业务线程：等待所有模块就绪
 *
 * 第一次等待有 3 秒超时，故意超时以演示超时处理流程。
 * storage 模块耗时 5 秒，所以第一次一定超时。
 */
static void business_thread_entry(void *param)
{
    rt_uint32_t recved;
    rt_err_t result;

    rt_kprintf("business thread: waiting for modules...\n");

    /* 第一次等待：3 秒超时 */
    result = rt_event_recv(startup_event, ALL_READY,
                           RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                           3000, &recved);

    if (result == -RT_ETIMEOUT)
    {
        rt_kprintf("wait modules timeout, ");
        if (!(recved & EVENT_NET_READY))
            rt_kprintf("network is not ready, ");
        if (!(recved & EVENT_SENSOR_READY))
            rt_kprintf("sensor is not ready, ");
        if (!(recved & EVENT_STORAGE_READY))
            rt_kprintf("storage is not ready, ");
        rt_kprintf("\n");

        /* 第二次：无限等待 */
        rt_event_recv(startup_event, ALL_READY,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER, &recved);
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");
}

/* 三个模块的线程参数（静态声明，不依赖动态内存） */
static struct module_param net_param      = {"network",    1000, EVENT_NET_READY};
static struct module_param sensor_param   = {"sensor",     2000, EVENT_SENSOR_READY};
static struct module_param storage_param  = {"storage",    5000, EVENT_STORAGE_READY};

static void startup_demo(int argc, char **argv)
{
    rt_thread_t tid_net, tid_sensor, tid_storage, tid_biz;

    if (startup_event != RT_NULL)
    {
        rt_kprintf("startup_demo already running\n");
        return;
    }

    /* 创建事件集 */
    startup_event = rt_event_create("startup_evt", RT_IPC_FLAG_PRIO);
    if (startup_event == RT_NULL)
    {
        rt_kprintf("startup_demo: failed to create event\n");
        return;
    }

    rt_kprintf("=== Startup Demo: 3 modules, event AND wait ===\n");

    /* 创建三个初始化线程 */
    tid_net = rt_thread_create("net_init",
                                module_init_thread, &net_param,
                                1024, 25, 10);
    tid_sensor = rt_thread_create("sensor_init",
                                  module_init_thread, &sensor_param,
                                  1024, 25, 10);
    tid_storage = rt_thread_create("storage_init",
                                   module_init_thread, &storage_param,
                                   1024, 25, 10);
    tid_biz = rt_thread_create("business",
                               business_thread_entry, RT_NULL,
                               1024, 20, 10);

    if (tid_net)    rt_thread_startup(tid_net);
    else            rt_kprintf("startup_demo: failed to create net thread\n");
    if (tid_sensor) rt_thread_startup(tid_sensor);
    else            rt_kprintf("startup_demo: failed to create sensor thread\n");
    if (tid_storage)rt_thread_startup(tid_storage);
    else            rt_kprintf("startup_demo: failed to create storage thread\n");
    if (tid_biz)    rt_thread_startup(tid_biz);
    else            rt_kprintf("startup_demo: failed to create business thread\n");
}

MSH_CMD_EXPORT(startup_demo, system startup condition check with event);

/*
 * 思考题答案：
 *
 * Q: 如果用三个独立信号量实现这个功能，与事件集相比有什么区别？
 *
 * A:
 * 1. 资源开销：3 个信号量各占一个 struct rt_semaphore（~40B）+ 等待链表 + 信号量对象本身；
 *    事件集只占一个 struct rt_event（约 44B）。3 个信号量约需 ~150B，事件集 ~44B。
 * 2. 原子性：事件集的 AND 模式是内核保证的——所有指定事件位同时置位后才唤醒等待线程。
 *    三个信号量需要用户自行设计轮询 / 嵌套等待，无法做到原子性组合判断。
 * 3. 超时粒度：事件集一次 rt_event_recv 即可指定整体超时；三个信号量需逐个设置 timeout
 *    或使用 rt_sem_take 的返回值组合判断，设计更复杂且超时语义不统一。
 * 4. 适用场景区分：
 *    信号量适合「资源计数」或「单事件通知」；
 *    事件集适合「多条件组合仲裁」——多个事件位 + AND/OR 逻辑 + 可选清除。
 *    RT-Thread 组件内部的 COMPONENT_INIT 调度就是基于事件集的典型应用。
 */
