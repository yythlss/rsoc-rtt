#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_common.h>
#include <finsh.h>

/* LED 和按键引脚 */
#define LED_R_PIN       GET_PIN(F, 12)
#define LED_B_PIN       GET_PIN(F, 11)
#define KEY_PIN         GET_PIN(C, 5)

/* 工作线程配置 */
#define LED_STACK_SIZE      1024
#define LED_PRIORITY        16
#define LED_TIMESLICE       5
#define LED_PERIOD_MS       1000

/* 监控线程配置 */
#define MONITOR_STACK_SIZE  1024
#define MONITOR_PRIORITY    10
#define MONITOR_TIMESLICE   5
#define MONITOR_PERIOD_MS   2000

/* 按键消抖时间 */
#define KEY_DEBOUNCE_MS     50

/* 动态线程句柄 */
static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;

/* 静态线程 th_led2 的栈空间和线程控制块 */
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t th_led2_stack[LED_STACK_SIZE];

ALIGN(RT_ALIGN_SIZE)
static struct rt_thread th_led2;

/* 按键中断事件标志 */
static volatile rt_uint8_t key_event = 0;
static volatile rt_tick_t key_last_tick = 0;

/*
 * th_led1 的挂起控制变量。
 *
 * 监控线程只设置挂起请求，
 * th_led1 检测到请求后自行执行挂起。
 */
static volatile rt_uint8_t led1_suspend_request = 0;
static volatile rt_uint8_t led1_manual_suspended = 0;


/* 翻转指定 GPIO 的输出电平 */
static void led_toggle(rt_base_t pin)
{
    rt_base_t level;

    level = rt_pin_read(pin);

    if (level == PIN_LOW)
    {
        rt_pin_write(pin, PIN_HIGH);
    }
    else
    {
        rt_pin_write(pin, PIN_LOW);
    }
}


/* 将线程状态转换为英文字符串 */
static const char *thread_state_name(rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        return "null";
    }

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
 * 计算线程剩余栈空间。
 *
 * RT-Thread 在线程初始化时会将栈填充为 '#',
 * 通过统计连续的 '#' 估算未使用的栈空间。
 */
static rt_uint32_t thread_stack_free(rt_thread_t thread)
{
    rt_uint8_t *start;
    rt_uint8_t *end;
    rt_uint8_t *current;

    if (thread == RT_NULL)
    {
        return 0;
    }

    start = (rt_uint8_t *)thread->stack_addr;
    end = start + thread->stack_size;
    current = start;

    while ((current < end) && (*current == '#'))
    {
        current++;
    }

    return (rt_uint32_t)(current - start);
}


/* 打印线程状态、优先级和剩余栈空间 */
static void print_thread_info(const char *label, rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        rt_kprintf("%s: not created\n", label);
        return;
    }

    rt_kprintf(
        "%s: state=%s, priority=%u, stack_free=%u/%u bytes\n",
        label,
        thread_state_name(thread),
        (unsigned int)thread->current_priority,
        (unsigned int)thread_stack_free(thread),
        (unsigned int)thread->stack_size);
}


/*
 * 工作线程 th_led1。
 *
 * 动态创建；
 * 优先级为 16；
 * 时间片为 5 ticks；
 * 栈大小为 1024 字节；
 * 循环翻转红色 LED，并打印计数和系统 Tick。
 */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    (void)parameter;

    while (1)
    {
        /*
         * 当前 RT-Thread 版本要求线程只能挂起自己，
         * 因此由 th_led1 自己执行 rt_thread_suspend()。
         */
        if (led1_suspend_request != 0)
        {
            led1_suspend_request = 0;
            led1_manual_suspended = 1;

            rt_kprintf("th_led1: suspended\n");

            rt_thread_suspend(rt_thread_self());

            /*
             * 被 th_monitor 恢复后，从这里继续执行。
             */
            continue;
        }

        /* 翻转红色 LED */
        led_toggle(LED_R_PIN);

        count++;

        /* 打印计数和系统 Tick */
        rt_kprintf(
            "[th_led1] count=%u, tick=%u\n",
            (unsigned int)count,
            (unsigned int)rt_tick_get());

        rt_thread_mdelay(LED_PERIOD_MS);
    }
}


/*
 * 工作线程 th_led2。
 *
 * 静态初始化；
 * 使用全局定义的线程控制块和栈空间；
 * 优先级为 16；
 * 时间片为 5 ticks；
 * 栈大小为 1024 字节；
 * 循环翻转蓝色 LED，并打印计数。
 */
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    (void)parameter;

    while (1)
    {
        /* 翻转蓝色 LED */
        led_toggle(LED_B_PIN);

        count++;

        /* 打印计数 */
        rt_kprintf(
            "[th_led2] count=%u\n",
            (unsigned int)count);

        rt_thread_mdelay(LED_PERIOD_MS);
    }
}


/* 按键中断回调函数 */
static void key_irq_callback(void *args)
{
    rt_tick_t now;

    (void)args;

    now = rt_tick_get();

    /*
     * 软件消抖，防止一次按键产生多个中断事件。
     */
    if ((rt_tick_t)(now - key_last_tick) >=
        rt_tick_from_millisecond(KEY_DEBOUNCE_MS))
    {
        key_event = 1;
        key_last_tick = now;
    }
}


/* 初始化按键 GPIO 和外部中断 */
static rt_err_t key_init(void)
{
    rt_err_t result;

    /* 按键使用内部上拉输入 */
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);

    key_last_tick =
        rt_tick_get() - rt_tick_from_millisecond(KEY_DEBOUNCE_MS);

    /* 配置按键下降沿中断 */
    result = rt_pin_attach_irq(
        KEY_PIN,
        PIN_IRQ_MODE_FALLING,
        key_irq_callback,
        RT_NULL);

    if (result != RT_EOK)
    {
        rt_kprintf("attach key irq failed: %d\n", result);
        return result;
    }

    /* 开启按键中断 */
    result = rt_pin_irq_enable(KEY_PIN, PIN_IRQ_ENABLE);

    if (result != RT_EOK)
    {
        rt_kprintf("enable key irq failed: %d\n", result);
    }

    return result;
}


/*
 * 第一次按键：请求挂起 th_led1；
 * 第二次按键：恢复 th_led1。
 */
static void process_key_event(void)
{
    rt_err_t result;

    if (key_event == 0)
    {
        return;
    }

    key_event = 0;

    if (led1_manual_suspended != 0)
    {
        rt_kprintf(
            "\n========== [KEY] PRESS ==========\n");
        rt_kprintf(
            "[KEY] action: RESUME th_led1\n");

        result = rt_thread_resume(th_led1);

        if (result == RT_EOK)
        {
            led1_manual_suspended = 0;
            rt_kprintf("[KEY] result: th_led1 RESUMED\n");
        }
        else
        {
            rt_kprintf(
                "[KEY] result: RESUME FAILED, error=%d\n",
                result);
        }

        rt_kprintf(
            "=================================\n");
    }
    else if (led1_suspend_request == 0)
    {
        rt_kprintf(
            "\n========== [KEY] PRESS ==========\n");
        rt_kprintf(
            "[KEY] action: REQUEST SUSPEND th_led1\n");

        led1_suspend_request = 1;

        rt_kprintf(
            "[KEY] result: waiting for th_led1...\n");
        rt_kprintf(
            "=================================\n");
    }
    else
    {
        rt_kprintf(
            "[KEY] th_led1 suspend request is pending\n");
    }
}


/*
 * 监控线程每隔 2 秒唤醒一次，
 * 打印两个工作线程的状态和剩余栈空间。
 */
static void th_monitor_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        rt_kprintf(
            "\n========== [MONITOR] WAKE UP ==========\n");

        /* 处理按键事件 */
        process_key_event();

        /* 打印两个工作线程的信息 */
        print_thread_info("[MONITOR] th_led1", th_led1);
        print_thread_info("[MONITOR] th_led2", &th_led2);

        rt_kprintf(
            "========== [MONITOR] SLEEP 2s ==========\n");

        rt_thread_mdelay(MONITOR_PERIOD_MS);
    }
}


/*
 * 自定义 MSH 命令。
 *
 * 在串口输入 thread_dump，
 * 可以查看三个线程的状态和剩余栈空间。
 */
int thread_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("========== thread dump ==========\n");

    print_thread_info("th_led1", th_led1);
    print_thread_info("th_led2", &th_led2);
    print_thread_info("th_monitor", th_monitor);

    rt_kprintf("=================================\n");

    return RT_EOK;
}

/* 将 thread_dump 注册为 MSH 命令 */
MSH_CMD_EXPORT(thread_dump, dump thread state and remaining stack);


/*
 * 应用初始化函数。
 *
 * 初始化 LED、按键，
 * 创建动态线程、静态线程和监控线程。
 */
static int thread_study_init(void)
{
    rt_err_t result;

    /* 配置红色和蓝色 LED 为输出模式 */
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);

    /* 设置 LED 初始电平 */
    rt_pin_write(LED_R_PIN, PIN_HIGH);
    rt_pin_write(LED_B_PIN, PIN_HIGH);

    /* 初始化按键 */
    result = key_init();

    if (result != RT_EOK)
    {
        rt_kprintf("key initialization failed: %d\n", result);
    }

    /*
     * 动态创建 th_led1。
     */
    th_led1 = rt_thread_create(
        "th_led1",
        th_led1_entry,
        RT_NULL,
        LED_STACK_SIZE,
        LED_PRIORITY,
        LED_TIMESLICE);

    if (th_led1 == RT_NULL)
    {
        rt_kprintf("create th_led1 failed\n");
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(th_led1);

    if (result != RT_EOK)
    {
        rt_kprintf("startup th_led1 failed: %d\n", result);
        return result;
    }

    /*
     * 静态初始化 th_led2。
     */
    result = rt_thread_init(
        &th_led2,
        "th_led2",
        th_led2_entry,
        RT_NULL,
        th_led2_stack,
        sizeof(th_led2_stack),
        LED_PRIORITY,
        LED_TIMESLICE);

    if (result != RT_EOK)
    {
        rt_kprintf("initialize th_led2 failed: %d\n", result);
        return result;
    }

    result = rt_thread_startup(&th_led2);

    if (result != RT_EOK)
    {
        rt_kprintf("startup th_led2 failed: %d\n", result);
        return result;
    }

    /*
     * 动态创建 th_monitor。
     */
    th_monitor = rt_thread_create(
        "th_monitor",
        th_monitor_entry,
        RT_NULL,
        MONITOR_STACK_SIZE,
        MONITOR_PRIORITY,
        MONITOR_TIMESLICE);

    if (th_monitor == RT_NULL)
    {
        rt_kprintf("create th_monitor failed\n");
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(th_monitor);

    if (result != RT_EOK)
    {
        rt_kprintf("startup th_monitor failed: %d\n", result);
        return result;
    }

    return RT_EOK;
}


/*
 * 使用自动初始化机制加载本模块。
 *
 * 不需要在 main.c 中手动调用 thread_study_init()。
 */
INIT_APP_EXPORT(thread_study_init);
