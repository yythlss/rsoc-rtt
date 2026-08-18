/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-18     RT-Thread    first version
 */

#include <rtthread.h>
#include <board.h>

#define DBG_TAG "thread_study"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* pin definition */
#define GPIO_LED_B                  GET_PIN(F, 11)
#define GPIO_LED_R                  GET_PIN(F, 12)
#define GPIO_BTN_UP                 GET_PIN(C, 5)

/* led thread configuration */
#define LED_THREAD_STACK_SIZE       1024
#define LED_THREAD_PRIORITY         16
#define LED_THREAD_TIMESLICE        5

/* monitor thread configuration */
#define MONITOR_THREAD_STACK_SIZE   1024
#define MONITOR_THREAD_PRIORITY     10
#define MONITOR_THREAD_TIMESLICE    5

/* application timing configuration */
#define LED_PERIOD_MS               1000
#define MONITOR_PERIOD_MS           2000
#define KEY_DEBOUNCE_MS             50

/* led2 static thread object */
static struct rt_thread led2_thread;
static rt_uint8_t led2_stack[LED_THREAD_STACK_SIZE];

/* dynamic thread handle */
static rt_thread_t led1_tid = RT_NULL;
static rt_thread_t monitor_tid = RT_NULL;

/* key semaphore */
static struct rt_semaphore key_sem;

/* led1 suspend control */
static volatile rt_bool_t led1_suspend_request = RT_FALSE;
static volatile rt_bool_t led1_suspended = RT_FALSE;

/* study start flag */
static rt_bool_t study_started = RT_FALSE;

static void th_led1(void *parameter);
static void th_led2(void *parameter);
static void th_monitor(void *parameter);

static void board_io_init(void);
static rt_err_t app_thread_init(void);
static rt_err_t thread_study_start(void);
static int thread_study(int argc, char **argv);

static void led_toggle(rt_base_t pin);

static const char *get_thread_state(rt_thread_t thread);
static rt_size_t get_stack_free(rt_thread_t thread);
static void monitor_print_thread_info(void);

static void key_irq_callback(void *args);
static rt_err_t key_init(rt_base_t key_pin);
static void key_event_process(void);

/**
 * @brief 启动线程学习示例。
 *
 * 初始化板载 LED、按键中断以及示例线程。
 *
 * @return RT_EOK 启动成功。
 * @return 其他错误码 启动失败。
 */
static rt_err_t thread_study_start(void)
{
    rt_err_t result;

    if (study_started == RT_TRUE)
    {
        LOG_W("thread study already started");
        return RT_EOK;
    }

    /* board io init */
    board_io_init();

    /* key irq init */
    result = key_init(GPIO_BTN_UP);
    if (result != RT_EOK)
    {
        LOG_E("key initialization failed: %d", result);
        return result;
    }

    /* thread init */
    result = app_thread_init();
    if (result != RT_EOK)
    {
        LOG_E("thread initialization failed: %d", result);
        return result;
    }

    study_started = RT_TRUE;

    LOG_I("thread study started");

    return RT_EOK;
}

/**
 * @brief MSH 命令入口。
 *
 * 在 MSH 中执行 thread_study 命令启动线程学习示例。
 *
 * @param argc 命令参数个数。
 * @param argv 命令参数列表。
 *
 * @return RT_EOK 命令执行成功。
 * @return 其他错误码 命令执行失败。
 */
static int thread_study(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    return thread_study_start();
}

/**
 * @brief 初始化板载 LED GPIO。
 */
static void board_io_init(void)
{
    /* led gpio init */
    rt_pin_mode(GPIO_LED_B, PIN_MODE_OUTPUT);
    rt_pin_mode(GPIO_LED_R, PIN_MODE_OUTPUT);

    /* set led default level */
    rt_pin_write(GPIO_LED_B, PIN_LOW);
    rt_pin_write(GPIO_LED_R, PIN_HIGH);
}

/**
 * @brief LED1 工作线程入口函数。
 *
 * 动态创建，周期翻转红色 LED，并打印运行计数和系统 Tick。
 * 当收到挂起请求时，由线程主动挂起自身。
 *
 * @param parameter 线程入口参数，本例未使用。
 */
static void th_led1(void *parameter)
{
    rt_uint32_t count = 0;
    rt_err_t result;

    RT_UNUSED(parameter);

    while (1)
    {
        /* 检查挂起请求 */
        if (led1_suspend_request == RT_TRUE)
        {
            LOG_I("th_led1 suspend");

            result = rt_thread_suspend(rt_thread_self());
            if (result == RT_EOK)
            {
                led1_suspended = RT_TRUE;

                rt_schedule();

                /* 线程恢复后继续执行 */
                led1_suspended = RT_FALSE;

                LOG_I("th_led1 resume");
            }
            else
            {
                LOG_E("th_led1 suspend failed: %d", result);
            }
        }

        /* update led1 count */
        count++;

        /* toggle led1 */
        led_toggle(GPIO_LED_R);

        /* print led1 info */
        LOG_D("th_led1 count=%u, tick=%u",
              (unsigned int)count,
              (unsigned int)rt_tick_get());

        rt_thread_mdelay(LED_PERIOD_MS);
    }
}

/**
 * @brief LED2 工作线程入口函数。
 *
 * 静态创建，周期翻转蓝色 LED，并打印运行计数。
 *
 * @param parameter 线程入口参数，未使用。
 */
static void th_led2(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        /* update led2 count */
        count++;

        /* toggle led2 */
        led_toggle(GPIO_LED_B);

        /* print led2 info */
        LOG_D("th_led2 count=%u",
              (unsigned int)count);

        rt_thread_mdelay(LED_PERIOD_MS);
    }
}

/**
 * @brief 监控线程入口函数。
 *
 * 周期打印两个 LED 线程的运行状态和剩余栈空间，
 * 同时等待并处理按键事件。
 *
 * @param parameter 线程入口参数，本例未使用。
 */
static void th_monitor(void *parameter)
{
    rt_tick_t period_tick;
    rt_tick_t last_tick;
    rt_tick_t current_tick;
    rt_tick_t elapsed_tick;
    rt_tick_t timeout_tick;
    rt_err_t result;

    RT_UNUSED(parameter);

    period_tick = rt_tick_from_millisecond(MONITOR_PERIOD_MS);
    last_tick = rt_tick_get();

    while (1)
    {
        current_tick = rt_tick_get();
        elapsed_tick = current_tick - last_tick;

        /* 计算监控线程等待超时时间 */
        if (elapsed_tick < period_tick)
        {
            timeout_tick = period_tick - elapsed_tick;
        }
        else
        {
            timeout_tick = 0;
        }

        if (timeout_tick == 0)
        {
            monitor_print_thread_info();
            last_tick = rt_tick_get();
            continue;
        }

        /* wait key event or timeout */
        result = rt_sem_take(&key_sem, timeout_tick);

        if (result == RT_EOK)
        {
            /* process key event */
            key_event_process();
        }
        else if (result == -RT_ETIMEOUT)
        {
            /* print thread info */
            monitor_print_thread_info();
            last_tick = rt_tick_get();
        }
    }
}

/**
 * @brief 创建并启动应用线程。
 *
 * @return RT_EOK 所有线程初始化并启动成功。
 * @return 其他错误码 线程创建、初始化或启动失败。
 */
static rt_err_t app_thread_init(void)
{
    rt_err_t result;

    /* 动态创建 LED1 线程 */
    led1_tid = rt_thread_create("th_led1",
                                th_led1,
                                RT_NULL,
                                LED_THREAD_STACK_SIZE,
                                LED_THREAD_PRIORITY,
                                LED_THREAD_TIMESLICE);

    if (led1_tid == RT_NULL)
    {
        LOG_E("create th_led1 failed");
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(led1_tid);
    if (result != RT_EOK)
    {
        LOG_E("startup th_led1 failed: %d", result);
        return result;
    }

    /* 静态初始化 LED2 线程 */
    result = rt_thread_init(&led2_thread,
                            "th_led2",
                            th_led2,
                            RT_NULL,
                            led2_stack,
                            sizeof(led2_stack),
                            LED_THREAD_PRIORITY,
                            LED_THREAD_TIMESLICE);

    if (result != RT_EOK)
    {
        LOG_E("initialize th_led2 failed: %d", result);
        return result;
    }

    result = rt_thread_startup(&led2_thread);
    if (result != RT_EOK)
    {
        LOG_E("startup th_led2 failed: %d", result);
        return result;
    }

    /* 动态创建监控线程 */
    monitor_tid = rt_thread_create("th_monitor",
                                   th_monitor,
                                   RT_NULL,
                                   MONITOR_THREAD_STACK_SIZE,
                                   MONITOR_THREAD_PRIORITY,
                                   MONITOR_THREAD_TIMESLICE);

    if (monitor_tid == RT_NULL)
    {
        LOG_E("create th_monitor failed");
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(monitor_tid);
    if (result != RT_EOK)
    {
        LOG_E("startup th_monitor failed: %d", result);
        return result;
    }

    return RT_EOK;
}

/**
 * @brief 翻转指定 GPIO 的输出电平。
 *
 * @param pin RT-Thread PIN 设备引脚编号。
 */
static void led_toggle(rt_base_t pin)
{
    rt_base_t level;

    /* read pin level */
    level = rt_pin_read(pin);

    /* toggle pin level */
    if (level == PIN_LOW)
    {
        rt_pin_write(pin, PIN_HIGH);
    }
    else
    {
        rt_pin_write(pin, PIN_LOW);
    }
}

/**
 * @brief 获取线程状态对应的可读字符串。
 *
 * @param thread 目标线程句柄。
 *
 * @return 线程状态字符串。
 */
static const char *get_thread_state(rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        return "NULL";
    }

    switch (thread->stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:
        return "INIT";

    case RT_THREAD_READY:
        return "READY";

    case RT_THREAD_RUNNING:
        return "RUNNING";

    case RT_THREAD_SUSPEND:
        return "SUSPEND";

    case RT_THREAD_CLOSE:
        return "CLOSE";

    default:
        return "UNKNOWN";
    }
}

/**
 * @brief 估算线程历史未使用的栈空间。
 *
 * RT-Thread 在线程栈初始化时使用 '#' 填充栈区域，
 * 本函数通过统计仍保持填充值的连续区域估算栈剩余空间。
 *
 * @param thread 目标线程句柄。
 *
 * @return 估算得到的未使用栈空间，单位为字节。
 */
static rt_size_t get_stack_free(rt_thread_t thread)
{
    rt_uint8_t *stack_ptr;
    rt_size_t free_size = 0;

    if (thread == RT_NULL)
    {
        return 0;
    }

    /* get stack start address */
    stack_ptr = (rt_uint8_t *)thread->stack_addr;

    /* calculate free stack size */
    while ((free_size < thread->stack_size) &&
           (*stack_ptr == '#'))
    {
        stack_ptr++;
        free_size++;
    }

    return free_size;
}

/**
 * @brief 打印 LED1 和 LED2 的线程状态及剩余栈空间。
 */
static void monitor_print_thread_info(void)
{
    LOG_D("th_led1: state=%s, stack_free=%u",
          get_thread_state(led1_tid),
          (unsigned int)get_stack_free(led1_tid));

    LOG_D("th_led2: state=%s, stack_free=%u",
          get_thread_state(&led2_thread),
          (unsigned int)get_stack_free(&led2_thread));
}

/**
 * @brief 按键 GPIO 中断回调函数。
 *
 * 中断中仅完成软件消抖和信号量释放，不执行耗时业务。
 *
 * @param args 中断回调参数，本例未使用。
 */
static void key_irq_callback(void *args)
{
    static rt_tick_t last_tick = 0;

    rt_tick_t current_tick;
    rt_tick_t debounce_tick;

    RT_UNUSED(args);

    current_tick = rt_tick_get();
    debounce_tick = rt_tick_from_millisecond(KEY_DEBOUNCE_MS);

    /* 按键软件消抖 */
    if ((last_tick != 0) &&
        ((current_tick - last_tick) < debounce_tick))
    {
        return;
    }

    last_tick = current_tick;

    /* 中断中释放按键信号量 */
    rt_sem_release(&key_sem);
}

/**
 * @brief 初始化按键信号量及 GPIO 外部中断。
 *
 * @param key_pin 按键对应的 RT-Thread PIN 编号。
 *
 * @return RT_EOK 初始化成功。
 * @return 其他错误码 信号量或 GPIO 中断初始化失败。
 */
static rt_err_t key_init(rt_base_t key_pin)
{
    rt_err_t result;

    /* semaphore init */
    result = rt_sem_init(&key_sem,
                         "key",
                         0,
                         RT_IPC_FLAG_PRIO);

    if (result != RT_EOK)
    {
        LOG_E("initialize key semaphore failed: %d", result);
        return result;
    }

    /* key gpio init */
    rt_pin_mode(key_pin, PIN_MODE_INPUT_PULLUP);

    /* attach key irq */
    result = rt_pin_attach_irq(key_pin,
                               PIN_IRQ_MODE_FALLING,
                               key_irq_callback,
                               RT_NULL);

    if (result != RT_EOK)
    {
        LOG_E("attach key irq failed: %d", result);
        return result;
    }

    /* enable key irq */
    result = rt_pin_irq_enable(key_pin, PIN_IRQ_ENABLE);

    if (result != RT_EOK)
    {
        LOG_E("enable key irq failed: %d", result);
        return result;
    }

    return RT_EOK;
}

/**
 * @brief 处理按键事件并控制 LED1 线程挂起或恢复。
 *
 * 第一次触发设置挂起请求，第二次触发恢复已挂起的 LED1 线程。
 */
static void key_event_process(void)
{
    rt_err_t result;

    if (led1_suspend_request == RT_FALSE)
    {
        /* 设置 LED1 挂起请求 */
        led1_suspend_request = RT_TRUE;

        LOG_I("request th_led1 suspend");
    }
    else
    {
        /* 清除 LED1 挂起请求 */
        led1_suspend_request = RT_FALSE;

        if (led1_suspended == RT_TRUE)
        {
            /* resume LED1 thread */
            result = rt_thread_resume(led1_tid);

            if (result != RT_EOK)
            {
                LOG_E("resume th_led1 failed: %d", result);
                return;
            }
        }

        LOG_I("request th_led1 resume");
    }
}

MSH_CMD_EXPORT(thread_study, start thread study example);
