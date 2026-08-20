/*
 * startup.c - 练习三：系统启动条件检查（事件集）
 *
 * 芯片     : STM32F407ZGT6
 * 开发板   : 星火1号 (RT-Thread STM32F407-RT-SPARK)
 * 开发环境 : RT-Thread Studio
 *
 * 场景:
 *   设备启动前需完成三个独立模块初始化：网络 / 传感器 / 存储。
 *   三者全部就绪后，主业务线程才能启动。模拟即可。
 *
 * 设计:
 *   - 三个事件位 EVENT_NET/SENSOR/STORAGE_READY。
 *   - 三个初始化线程经不同延时后发送对应就绪事件，随后自注销。
 *   - 主业务线程用 AND 模式等待三个事件全部到达。
 *     · 第一次等待超时 3 秒（存储模块需 5 秒），打印尚未就绪的模块后再次等待。
 *     · 第二次等待全部就绪后打印 all modules are ready / business task started。
 *   - 全部线程结束后主动释放资源，命令可重复执行。
 *
 * 需回答的思考题: 与"三个独立信号量"实现的区别（见 README.md）。
 */

#include <rtthread.h>
#include <rtdevice.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

/* 各模块初始化延时 */
#define NET_INIT_MS          500   /* 快速就绪            */
#define SENSOR_INIT_MS       700   /* 快速就绪            */
#define STORAGE_INIT_MS      5000  /* 慢速就绪，触发超时   */
#define BUSINESS_TIMEOUT_MS  3000  /* 第一次等待超时 3 秒   */
#define BUSINESS_TIMEOUT_2MS 6000  /* 第二次等待更长        */

#define INIT_PRIORITY        15
#define BUSINESS_PRIORITY    10
#define INIT_STACK           1024
#define BUSINESS_STACK       1024

static struct rt_event boot_events;      /* 静态事件集对象    */
static volatile rt_uint8_t startup_running = 0;

/* 非阻塞查询某事件位是否已置起（不改变事件集） */
static int module_ready(rt_uint32_t bit)
{
    rt_uint32_t recvd = 0;
    return rt_event_recv(&boot_events, bit, RT_EVENT_FLAG_AND, 0, &recvd) == RT_EOK;
}

static void init_thread_entry(void *param)
{
    rt_uint32_t evt  = (rt_uint32_t)param;
    rt_uint32_t delay;
    const char *name;

    switch (evt)
    {
    case EVENT_NET_READY:     delay = NET_INIT_MS;      name = "network";  break;
    case EVENT_SENSOR_READY:  delay = SENSOR_INIT_MS;   name = "sensor";   break;
    default:                  delay = STORAGE_INIT_MS;  name = "storage";  break;
    }

    rt_thread_mdelay(delay);                       /* 模拟初始化耗时 */
    if (rt_event_send(&boot_events, evt) != RT_EOK)
    {
        rt_kprintf("%s init send event failed\n", name);
    }
    rt_kprintf("%s ready\n", name);

    return;                                  /* 初始化完成即退出（内核自动清理） */
}

static void business_entry(void *param)
{
    rt_uint32_t recvd = 0;
    rt_err_t    ret;

    for (;;)
    {
        /* AND 模式等待三个模块全部就绪（第一次超时 3s，之后超时 6s） */
        ret = rt_event_recv(&boot_events, EVENT_ALL_READY,
                            RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                            BUSINESS_TIMEOUT_MS, &recvd);
        if (ret == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
        if (ret == -RT_ETIMEOUT)
        {
            rt_kprintf("wait modules timeout, ");
            if (!module_ready(EVENT_NET_READY))     rt_kprintf("network ");
            if (!module_ready(EVENT_SENSOR_READY))  rt_kprintf("sensor ");
            if (!module_ready(EVENT_STORAGE_READY)) rt_kprintf("storage ");
            rt_kprintf("is not ready\n");
            /* 进入第二次等待，给出更长超时（此时存储应就绪） */
            ret = rt_event_recv(&boot_events, EVENT_ALL_READY,
                                RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                BUSINESS_TIMEOUT_2MS, &recvd);
            if (ret == RT_EOK)
            {
                rt_kprintf("all modules are ready\n");
                rt_kprintf("business task started\n");
                break;
            }
            rt_kprintf("business wait again timeout\n");
        }
        else
        {
            rt_kprintf("event recv error 0x%x\n", ret);
            break;
        }
    }

    startup_running = 0;                 /* 允许下次重新启动 */
    return;                              /* 业务线程退出（内核自动清理） */
}

int startup_demo(void)
{
    rt_thread_t tid_init[3], tid_bus;
    rt_uint32_t evts[3] = { EVENT_NET_READY, EVENT_SENSOR_READY, EVENT_STORAGE_READY };
    rt_uint32_t i;

    if (startup_running)
    {
        rt_kprintf("startup_demo is already running, ignore\n");
        return -RT_EBUSY;
    }
    startup_running = 1;

    rt_memset(&boot_events, 0, sizeof(boot_events));
    if (rt_event_init(&boot_events, "boot_events", RT_IPC_FLAG_FIFO) != RT_EOK)
    {
        rt_kprintf("init boot_events failed\n");
        startup_running = 0;
        return -RT_ERROR;
    }

    /* 业务线程 */
    tid_bus = rt_thread_create("business", business_entry, RT_NULL,
                               BUSINESS_STACK, BUSINESS_PRIORITY, 10);
    if (tid_bus == RT_NULL)
    {
        rt_kprintf("create business thread failed\n");
        rt_event_detach(&boot_events);
        startup_running = 0;
        return -RT_ENOMEM;
    }

    /* 三个初始化线程 */
    for (i = 0; i < 3; i++)
    {
        tid_init[i] = rt_thread_create("boot_init", init_thread_entry, (void *)evts[i],
                                       INIT_STACK, INIT_PRIORITY, 5);
        if (tid_init[i] == RT_NULL)
        {
            rt_kprintf("create init thread failed\n");
            while (i--)
                rt_thread_delete(tid_init[i]);
            rt_thread_delete(tid_bus);
            rt_event_detach(&boot_events);
            startup_running = 0;
            return -RT_ENOMEM;
        }
        rt_thread_startup(tid_init[i]);
    }

    if (rt_thread_startup(tid_bus) != RT_EOK)
    {
        rt_kprintf("startup business thread failed\n");
        rt_thread_delete(tid_bus);
    }
    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, startup demo: event set wait 3 modules then start business);