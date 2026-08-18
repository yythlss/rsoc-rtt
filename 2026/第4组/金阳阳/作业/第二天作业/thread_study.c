/*
 * 多线程实战测试模块(通过 INIT_APP_EXPORT 自动加载, main.c 中无任何硬编码调用)
 *
 *   th_led1    : 动态创建, 优先级 16, 时间片 5 ticks, 栈 1024 Bytes
 *                循环翻转 LED_R(PF12), 每次执行打印计数与 rt_tick_get()
 *   th_led2    : 静态初始化(全局栈 + 全局 TCB), 优先级 16, 时间片 5 ticks
 *                循环翻转 LED_B(PF11), 每次执行打印计数
 *   th_monitor : 动态创建, 优先级 10(高于工作线程), 周期 rt_thread_mdelay(2000),
 *                唤醒后打印 th_led1/th_led2 的运行状态与剩余栈空间;
 *                KEY1(PC5) 单次触发调用 rt_thread_suspend() 挂起 th_led1,
 *                再次触发调用 rt_thread_resume() 恢复 th_led1
 *
 * 调试命令:
 *   MSH_CMD_EXPORT(thread_dump, ...) -> msh 中执行 thread_dump 查看线程状态
 *   执行时工作线程的周期打印会被临时暂停(协作式挂起), 命令输出完毕后再恢复,
 *   避免工作线程日志刷屏覆盖本命令的输出。
 *   MSH_CMD_EXPORT(thread_log, ...) -> msh 中执行 thread_log on/off 开关周期
 *   日志输出; 周期日志上电默认关闭, 串口保持安静, 便于在 msh 中输入命令。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <drv_common.h>
#include <string.h>

#define DBG_TAG "thread_study"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 板载 LED 与按键引脚 */
#define LED_B_PIN               GET_PIN(F, 11)
#define LED_R_PIN               GET_PIN(F, 12)
#define KEY1_PIN                GET_PIN(C, 5)
#define LED_OFF_LEVEL           PIN_HIGH

/* 工作线程参数:优先级 16, 时间片 5 ticks, 栈 1024 Bytes */
#define LED_THREAD_STACK_SIZE   1024
#define LED_THREAD_PRIORITY     16
#define LED_THREAD_TIMESLICE    5

/* 监控线程参数:优先级 10(数值越小优先级越高, 高于工作线程的 16) */
#define MONITOR_STACK_SIZE      1024
#define MONITOR_PRIORITY        10
#define MONITOR_TIMESLICE       5

#define MONITOR_PERIOD_MS       2000
#define LED_PERIOD_MS           500
#define KEY_DEBOUNCE_MS         50

/* 动态创建的两个线程句柄 */
static rt_thread_t th_led1;
static rt_thread_t th_monitor;

/* th_led2 静态初始化:全局 TCB 与全局栈 */
static struct rt_thread th_led2;
ALIGN(8) static rt_uint8_t th_led2_stack[LED_THREAD_STACK_SIZE];

/* KEY1 按键事件信号量:中断里只释放信号量, 挂起/恢复在监控线程中执行 */
static struct rt_semaphore key1_sem;

/* th_led1 挂起/恢复标记, 详见 monitor_key_events() 中的说明 */
static volatile rt_bool_t led1_suspend_request;
static volatile rt_bool_t led1_suspended;

/* thread_dump 命令的临时暂停标记:暂停期间工作线程不打印, 便于查看命令输出 */
static volatile rt_bool_t led1_pause_request; /* 请求 th_led1 暂停 */
static volatile rt_bool_t led2_pause_request; /* 请求 th_led2 暂停 */
static volatile rt_bool_t led1_dump_paused;   /* th_led1 已暂停 */
static volatile rt_bool_t led2_dump_paused;   /* th_led2 已暂停 */
static volatile rt_bool_t quiet_dump;         /* 监控线程暂停打印与按键处理 */

/* 周期日志开关:thread_log 命令运行时控制。
 * 上电默认关闭, 串口保持安静、msh 可以正常输入命令;
 * 执行 thread_log on 后打开演示日志。 */
static volatile rt_bool_t worker_log_enabled = RT_FALSE;

/* KEY1 中断去抖 */
static volatile rt_bool_t key1_irq_seen;
static volatile rt_tick_t key1_last_irq_tick;

/* 翻转一个引脚的电平 */
static void led_toggle(rt_base_t pin)
{
    int level = rt_pin_read(pin);

    rt_pin_write(pin, level == PIN_LOW ? PIN_HIGH : PIN_LOW);
}

/* 线程运行状态名称, 供打印使用 */
static const char *thread_state_name(rt_thread_t thread)
{
    switch (thread->stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:
        return "init";
    case RT_THREAD_READY:
        return "ready";
    case RT_THREAD_SUSPEND:
        return "suspend";
    case RT_THREAD_RUNNING:
        return "running";
    case RT_THREAD_CLOSE:
        return "close";
    default:
        return "unknown";
    }
}

/*
 * 剩余栈空间统计:
 * RT-Thread 初始化线程栈时用 '#' 填充(rt-thread/src/thread.c),
 * Cortex-M 栈向下生长, 因此从栈底向上数 '#' 即为未被使用的字节数。
 */
static rt_size_t thread_stack_free(rt_thread_t thread)
{
    rt_uint8_t *start = (rt_uint8_t *)thread->stack_addr;
    rt_uint8_t *end = start + thread->stack_size;
    rt_uint8_t *cursor = start;

    while (cursor < end && *cursor == '#')
    {
        cursor++;
    }

    return (rt_size_t)(cursor - start);
}

/* th_led1 线程入口:翻转 LED_R, 打印计数与系统滴答 */
static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        /*
         * 监控线程只能"请求"挂起:本内核版本的 rt_thread_suspend() 带有
         * RT_ASSERT(thread == rt_thread_self()) 断言(RT_DEBUG 开启), 且
         * 无法挂起处于睡眠态的线程, 因此由 th_led1 自己执行挂起操作,
         * 之后再由监控线程调用 rt_thread_resume() 恢复。
         */
        if (led1_suspend_request)
        {
            led1_suspend_request = RT_FALSE;
            led1_suspended = RT_TRUE;

            /* 挂起自己并主动让出 CPU, 直到被 rt_thread_resume() 恢复 */
            rt_thread_suspend(rt_thread_self());
            rt_schedule();

            /* 恢复后从此处继续执行 */
            led1_suspended = RT_FALSE;
        }

        /* thread_dump 命令请求:暂停本线程打印, 待命令输出完毕后恢复 */
        if (led1_pause_request)
        {
            led1_pause_request = RT_FALSE;
            led1_dump_paused = RT_TRUE;

            rt_thread_suspend(rt_thread_self());
            rt_schedule();

            led1_dump_paused = RT_FALSE;
        }

        led_toggle(LED_R_PIN);
        count++;
        if (worker_log_enabled)
        {
            LOG_I("th_led1 count=%lu, tick=%lu",
                  (unsigned long)count,
                  (unsigned long)rt_tick_get());
        }
        rt_thread_mdelay(LED_PERIOD_MS);
    }
}

/* th_led2 线程入口:翻转 LED_B, 打印计数 */
static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        /* thread_dump 命令请求:暂停本线程打印, 待命令输出完毕后恢复 */
        if (led2_pause_request)
        {
            led2_pause_request = RT_FALSE;
            led2_dump_paused = RT_TRUE;

            rt_thread_suspend(rt_thread_self());
            rt_schedule();

            led2_dump_paused = RT_FALSE;
        }

        led_toggle(LED_B_PIN);
        count++;
        if (worker_log_enabled)
        {
            LOG_I("th_led2 count=%lu", (unsigned long)count);
        }
        rt_thread_mdelay(LED_PERIOD_MS);
    }
}

/* KEY1 中断回调:只做去抖并释放信号量, 不直接操作线程 */
static void key1_irq_callback(void *parameter)
{
    rt_tick_t now = rt_tick_get();

    RT_UNUSED(parameter);

    if (!key1_irq_seen ||
        (now - key1_last_irq_tick) >= rt_tick_from_millisecond(KEY_DEBOUNCE_MS))
    {
        key1_irq_seen = RT_TRUE;
        key1_last_irq_tick = now;
        rt_sem_release(&key1_sem);
    }
}

/* 处理按键事件:单次触发挂起 th_led1, 再次触发恢复 th_led1 */
static void monitor_key_events(void)
{
    while (rt_sem_trytake(&key1_sem) == RT_EOK)
    {
        if (led1_suspended)
        {
            /* 确认 th_led1 确实处于挂起态后再恢复 */
            if ((th_led1->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
            {
                if (rt_thread_resume(th_led1) == RT_EOK)
                {
                    LOG_I("KEY1: th_led1 resumed");
                }
            }
        }
        else if (!led1_suspend_request)
        {
            /* th_led1 在下一次循环检查到该请求后自行调用
             * rt_thread_suspend() 挂起, 挂起完成后进入 suspend 状态 */
            led1_suspend_request = RT_TRUE;
            LOG_I("KEY1: suspend requested for th_led1");
        }
        else
        {
            LOG_W("KEY1: suspend pending, press ignored");
        }
    }
}

/* 打印 th_led1 与 th_led2 的运行状态与剩余栈空间 */
static void monitor_report(void)
{
    if (!worker_log_enabled)
    {
        return;
    }

    LOG_I("th_led1: state=%s, free_stack=%lu/%lu bytes",
          thread_state_name(th_led1),
          (unsigned long)thread_stack_free(th_led1),
          (unsigned long)th_led1->stack_size);
    LOG_I("th_led2: state=%s, free_stack=%lu/%lu bytes",
          thread_state_name(&th_led2),
          (unsigned long)thread_stack_free(&th_led2),
          (unsigned long)th_led2.stack_size);
}

/* 监控线程入口:周期唤醒, 处理按键事件并打印线程状态 */
static void monitor_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        rt_thread_mdelay(MONITOR_PERIOD_MS);

        /* thread_dump 命令执行期间暂停打印与按键处理, 避免刷屏覆盖命令输出 */
        if (!quiet_dump)
        {
            monitor_key_events();
            monitor_report();
        }
    }
}

/* 板载 LED 与按键初始化 */
static void board_io_init(void)
{
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_B_PIN, LED_OFF_LEVEL);
    rt_pin_write(LED_R_PIN, LED_OFF_LEVEL);

    rt_pin_mode(KEY1_PIN, PIN_MODE_INPUT_PULLUP);
}

/* 本模块自动加载入口:由 INIT_APP_EXPORT 注册, main.c 中不做任何调用 */
static int thread_study_init(void)
{
    rt_err_t result;

    board_io_init();

    result = rt_sem_init(&key1_sem, "key1", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        LOG_E("init key1 semaphore failed: %d", result);
        return result;
    }

    result = rt_pin_attach_irq(KEY1_PIN,
                               PIN_IRQ_MODE_FALLING,
                               key1_irq_callback,
                               RT_NULL);
    if (result != RT_EOK)
    {
        LOG_E("attach KEY1 irq failed: %d", result);
        return result;
    }

    result = rt_pin_irq_enable(KEY1_PIN, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        LOG_E("enable KEY1 irq failed: %d", result);
        return result;
    }

    /* 工作线程 1:动态创建 th_led1 */
    th_led1 = rt_thread_create("th_led1",
                               led1_entry,
                               RT_NULL,
                               LED_THREAD_STACK_SIZE,
                               LED_THREAD_PRIORITY,
                               LED_THREAD_TIMESLICE);
    if (th_led1 == RT_NULL)
    {
        LOG_E("create th_led1 failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(th_led1);

    /* 工作线程 2:静态初始化 th_led2(全局栈 + 全局 TCB) */
    result = rt_thread_init(&th_led2,
                            "th_led2",
                            led2_entry,
                            RT_NULL,
                            th_led2_stack,
                            sizeof(th_led2_stack),
                            LED_THREAD_PRIORITY,
                            LED_THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        LOG_E("init th_led2 failed: %d", result);
        return result;
    }
    rt_thread_startup(&th_led2);

    /* 监控与控制线程:动态创建 th_monitor */
    th_monitor = rt_thread_create("th_monitor",
                                  monitor_entry,
                                  RT_NULL,
                                  MONITOR_STACK_SIZE,
                                  MONITOR_PRIORITY,
                                  MONITOR_TIMESLICE);
    if (th_monitor == RT_NULL)
    {
        LOG_E("create th_monitor failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(th_monitor);

    LOG_I("thread study module loaded: th_led1(dynamic), th_led2(static), th_monitor");
    LOG_I("period log is off by default, run 'thread_log on' in msh to enable");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);

#ifdef RT_USING_FINSH
/* 打印单个线程的详细状态(供 thread_dump 命令使用) */
static void thread_detail(rt_thread_t thread)
{
    rt_kprintf("%-10s prio=%-3d state=%-8s stack_free=%lu/%lu bytes\n",
               thread->name,
               thread->current_priority,
               thread_state_name(thread),
               (unsigned long)thread_stack_free(thread),
               (unsigned long)thread->stack_size);
}

/* finsh 内置的系统线程列表命令 */
extern long list_thread(void);

/* MSH 命令:thread_dump 查看本模块线程的运行状态与剩余栈空间。
 * 执行时先请求工作线程暂停打印, 输出完毕后恢复, 避免日志刷屏覆盖输出。 */
static int thread_dump(int argc, char **argv)
{
    rt_bool_t paused_led1 = RT_FALSE;
    rt_bool_t paused_led2 = RT_FALSE;
    rt_tick_t deadline;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    /* 命令执行期间, 监控线程暂停打印与按键处理 */
    quiet_dump = RT_TRUE;

    /* 请求正在运行的工作线程暂停(已被按键挂起的 th_led1 无需再暂停) */
    if (!led1_suspended && !led1_suspend_request)
    {
        paused_led1 = RT_TRUE;
        led1_pause_request = RT_TRUE;
    }
    paused_led2 = RT_TRUE;
    led2_pause_request = RT_TRUE;

    /* 等待工作线程确认暂停(最多等待一个工作周期, 留 2s 余量) */
    deadline = rt_tick_get() + rt_tick_from_millisecond(2000);
    while (rt_tick_get() < deadline &&
           ((paused_led1 && !led1_dump_paused) ||
            (paused_led2 && !led2_dump_paused)))
    {
        rt_thread_mdelay(20);
    }

    /* 先打印系统全部线程概览 */
    list_thread();

    /* 再打印本模块线程的详细状态 */
    rt_kprintf("\n[thread_study] worker/monitor threads:\n");
    thread_detail(th_led1);
    thread_detail(&th_led2);
    thread_detail(th_monitor);

    /* 恢复被本命令暂停的线程(已被按键挂起的 th_led1 保持挂起不动) */
    if (paused_led1 && led1_dump_paused &&
        (th_led1->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
    {
        rt_thread_resume(th_led1);
    }
    if (paused_led2 && led2_dump_paused &&
        (th_led2.stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
    {
        rt_thread_resume(&th_led2);
    }

    quiet_dump = RT_FALSE;

    return 0;
}
MSH_CMD_EXPORT(thread_dump, "dump thread_study threads status and stack usage");

/* MSH 命令:thread_log on/off 开关周期日志, 关闭后串口安静, 便于操作 msh */
static int thread_log(int argc, char **argv)
{
    if (argc == 1)
    {
        rt_kprintf("thread_study log output: %s\n",
                   worker_log_enabled ? "on" : "off");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0)
    {
        worker_log_enabled = RT_TRUE;
        rt_kprintf("thread_study log output: on\n");
    }
    else if (strcmp(argv[1], "off") == 0)
    {
        worker_log_enabled = RT_FALSE;
        rt_kprintf("thread_study log output: off\n");
    }
    else
    {
        rt_kprintf("usage: thread_log [on|off]\n");
    }

    return 0;
}
MSH_CMD_EXPORT(thread_log, "turn on/off thread_study period log output");
#endif
