/* 练习三：系统启动条件检查；星火一号 STM32F407ZG。
 * - 两个窗口线程共同出售 20 张票
 * - 使用互斥量保护完整售票过程
 * - 售完后线程自动退出，票号不重复
 * - 命令参数 unsafe 用于对比不加锁时的竞态现象
 * 三个初始化线程模拟网络/传感器/存储模块，主业务线程用 AND 事件等待全部就绪。 */
#include <rtthread.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)
#define THREAD_STACK         1024
#define THREAD_PRIORITY      25

struct module_info
{
    const char *name;
    rt_uint32_t event_bit;
    rt_uint32_t delay_ms;
};

static const struct module_info modules[] =
{
    { "network", EVENT_NET_READY, 1000 },
    { "sensor", EVENT_SENSOR_READY, 2000 },
    { "storage", EVENT_STORAGE_READY, 5000 },
};

static rt_event_t startup_event;
static rt_sem_t done_sem;
static volatile rt_uint32_t ready_flags;
static rt_uint8_t running;

static rt_bool_t start_thread(const char *name, void (*entry)(void *), void *param,
                              rt_uint8_t priority, rt_uint32_t *started)
{
    rt_thread_t tid = rt_thread_create(name, entry, param, THREAD_STACK, priority, 5);
    if (tid == RT_NULL)
        return RT_FALSE;
    if (rt_thread_startup(tid) != RT_EOK)
    {
        rt_thread_delete(tid);
        return RT_FALSE;
    }
    (*started)++;
    return RT_TRUE;
}

static void print_missing_modules(rt_uint32_t ready)
{
    const char *separator = "";
    rt_kprintf("wait modules timeout, ");
    if ((ready & EVENT_NET_READY) == 0U)
    {
        rt_kprintf("network");
        separator = ", ";
    }
    if ((ready & EVENT_SENSOR_READY) == 0U)
    {
        rt_kprintf("%ssensor", separator);
        separator = ", ";
    }
    if ((ready & EVENT_STORAGE_READY) == 0U)
        rt_kprintf("%sstorage", separator);
    rt_kprintf(" is not ready\n");
}

static void module_entry(void *parameter)
{
    const struct module_info *module = &modules[(rt_uint32_t)parameter];
    rt_thread_mdelay(module->delay_ms);
    ready_flags |= module->event_bit;
    rt_kprintf("%s ready\n", module->name);
    if (rt_event_send(startup_event, module->event_bit) != RT_EOK)
        rt_kprintf("%s send ready event failed\n", module->name);
    rt_sem_release(done_sem);
}

static void business_entry(void *parameter)
{
    rt_uint32_t received = 0;
    (void)parameter;

    while (1)
    {
        rt_err_t result = rt_event_recv(startup_event, EVENT_ALL_READY,
                                        RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                        RT_TICK_PER_SECOND * 3, &received);
        if (result == RT_EOK)
        {
            rt_kprintf("all modules are ready\nbusiness task started\n");
            break;
        }
        if (result == -RT_ETIMEOUT)
        {
            print_missing_modules(ready_flags);
            continue;
        }
        rt_kprintf("business wait events failed: %d\n", result);
        break;
    }

    rt_sem_release(done_sem);
}

static int startup_demo(int argc, char **argv)
{
    rt_uint32_t i, started = 0;
    (void)argc;
    (void)argv;

    if (running)
    {
        rt_kprintf("startup_demo is already running\n");
        return -RT_EBUSY;
    }

    running = 1;
    ready_flags = 0;
    startup_event = rt_event_create("st_event", RT_IPC_FLAG_FIFO);
    done_sem = rt_sem_create("st_done", 0, RT_IPC_FLAG_FIFO);
    if ((startup_event == RT_NULL) || (done_sem == RT_NULL))
    {
        rt_kprintf("create startup IPC failed\n");
        goto cleanup;
    }

    for (i = 0; i < 3; i++)
    {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "mod%u", (unsigned int)i);
        if (!start_thread(name, module_entry, (void *)i, THREAD_PRIORITY, &started))
            rt_kprintf("start module %u failed\n", i);
    }

    if (started == 3U &&
        !start_thread("sbiz", business_entry, RT_NULL, THREAD_PRIORITY, &started))
        rt_kprintf("start business thread failed\n");

    for (i = 0; i < started; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);

cleanup:
    if (startup_event != RT_NULL)
        rt_event_delete(startup_event);
    if (done_sem != RT_NULL)
        rt_sem_delete(done_sem);
    startup_event = RT_NULL;
    done_sem = RT_NULL;
    running = 0;
    return 0;
}

MSH_CMD_EXPORT(startup_demo, startup condition demo);
