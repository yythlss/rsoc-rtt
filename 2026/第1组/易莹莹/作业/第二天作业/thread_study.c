/*
 * RT-Thread 夏令营 Day2 作业：动态线程、静态线程与线程监控
 * 目标板：STM32F407 星火1号
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <board.h>

#define DBG_TAG "thread_study"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 星火1号板载资源：LED 低电平点亮，KEY0 按下为低电平。 */
#define PIN_LED_B       GET_PIN(F, 11)      /* PF11 */
#define PIN_LED_R       GET_PIN(F, 12)      /* PF12 */
#define PIN_KEY0        GET_PIN(C, 0)       /* PC0 */

#define LED_STACK_SIZE  1024
#define MONITOR_STACK_SIZE 1024
#define THREAD_PRIORITY 16
#define THREAD_TICK      5
#define DAY2_AUTO_PRINT  0

/* 动态创建的线程句柄。 */
static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;

/* 静态线程的 TCB 与栈。 */
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t th_led2_stack[LED_STACK_SIZE];
static struct rt_thread th_led2;

static volatile rt_uint8_t led1_suspend_request;
static volatile rt_uint8_t led1_is_suspended;
static rt_uint32_t led1_count;
static rt_uint32_t led2_count;
static rt_tick_t led1_last_tick;

static const char *thread_state_name(rt_uint8_t stat)
{
    switch (stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:
        return "init";
    case RT_THREAD_READY:
        return "ready";
    case RT_THREAD_RUNNING:
        return "running";
    case RT_THREAD_SUSPEND:
        return "suspend";
    case RT_THREAD_CLOSE:
        return "close";
    default:
        return "unknown";
    }
}

/* RT-Thread 初始化时会用 '#' 填充线程栈，扫描得到历史未使用空间。 */
static rt_size_t thread_stack_remaining(rt_thread_t thread)
{
    rt_uint8_t *stack;
    rt_size_t index = 0;

    if (thread == RT_NULL || thread->stack_addr == RT_NULL)
    {
        return 0;
    }

    stack = (rt_uint8_t *)thread->stack_addr;
    while (index < thread->stack_size && stack[index] == '#')
    {
        index++;
    }

    return index;
}

static void dump_one_thread(const char *label, rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        rt_kprintf("%-10s: not created\n", label);
        return;
    }

    rt_kprintf("%-10s: state=%-7s priority=%u stack=%u/%u bytes\n",
               label,
               thread_state_name(thread->stat),
               thread->current_priority,
               (unsigned int)thread_stack_remaining(thread),
               (unsigned int)thread->stack_size);
}

static void led1_entry(void *parameter)
{
    (void)parameter;
    while (1)
    {
        /* rt_thread_suspend() 只能由当前线程调用，故在这里自挂起。 */
        if (led1_suspend_request)
        {
            led1_suspend_request = 0;
            led1_is_suspended = 1;
            rt_thread_suspend(rt_thread_self());
            rt_schedule();
            led1_is_suspended = 0;
            continue;
        }

        rt_pin_write(PIN_LED_R, PIN_LOW);
        led1_count++;
        led1_last_tick = rt_tick_get();
#if DAY2_AUTO_PRINT
        rt_kprintf("th_led1 count=%u tick=%u\n",
                   (unsigned int)led1_count,
                   (unsigned int)led1_last_tick);
#endif
        rt_thread_mdelay(500);

        rt_pin_write(PIN_LED_R, PIN_HIGH);
        rt_thread_mdelay(500);
    }
}

static void led2_entry(void *parameter)
{
    (void)parameter;
    while (1)
    {
        rt_pin_write(PIN_LED_B, PIN_LOW);
        led2_count++;
#if DAY2_AUTO_PRINT
        rt_kprintf("th_led2 count=%u\n", (unsigned int)led2_count);
#endif
        rt_thread_mdelay(700);

        rt_pin_write(PIN_LED_B, PIN_HIGH);
        rt_thread_mdelay(700);
    }
}

static void monitor_entry(void *parameter)
{
#if DAY2_AUTO_PRINT
    rt_tick_t last_report = rt_tick_get();
#endif

    (void)parameter;
    while (1)
    {
        /* KEY0/PC0 为低电平有效，轮询并消抖。 */
        if (rt_pin_read(PIN_KEY0) == PIN_LOW)
        {
            rt_thread_mdelay(50);
            if (rt_pin_read(PIN_KEY0) == PIN_LOW)
            {
                if (!led1_is_suspended)
                {
                    /* 由 th_led1 自己调用 rt_thread_suspend。 */
                    led1_suspend_request = 1;
                }
                else if (th_led1 != RT_NULL &&
                         rt_thread_resume(th_led1) == RT_EOK)
                {
                    led1_is_suspended = 0;
                }

                while (rt_pin_read(PIN_KEY0) == PIN_LOW)
                    rt_thread_mdelay(10);
            }
        }

#if DAY2_AUTO_PRINT
        if (rt_tick_get() - last_report >= rt_tick_from_millisecond(2000))
        {
            last_report = rt_tick_get();
            dump_one_thread("th_led1", th_led1);
            dump_one_thread("th_led2", &th_led2);
            dump_one_thread("th_monitor", th_monitor);
        }
#endif
        /* 监控线程持续轮询，默认通过 thread_dump 手动查看。 */
        rt_thread_mdelay(20);
    }
}

static int thread_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("key=%d suspended=%u | count: led1=%u tick=%u led2=%u\n",
               rt_pin_read(PIN_KEY0),
               led1_is_suspended,
               (unsigned int)led1_count,
               (unsigned int)led1_last_tick,
               (unsigned int)led2_count);
    dump_one_thread("th_led1", th_led1);
    dump_one_thread("th_led2", &th_led2);
    dump_one_thread("th_monitor", th_monitor);
    return 0;
}
MSH_CMD_EXPORT(thread_dump, dump Day2 thread state and stack information);

static int thread_study_init(void)
{
    rt_err_t result;

    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);
    rt_pin_write(PIN_LED_B, PIN_HIGH);

    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);

#ifdef RT_USING_HEAP
    th_led1 = rt_thread_create("th_led1", led1_entry, RT_NULL,
                               LED_STACK_SIZE, THREAD_PRIORITY, THREAD_TICK);
    if (th_led1 == RT_NULL)
    {
        LOG_E("create th_led1 failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(th_led1);
#else
    LOG_E("Day2 requires RT_USING_HEAP for dynamic th_led1");
    return -RT_ENOMEM;
#endif

    result = rt_thread_init(&th_led2, "th_led2", led2_entry, RT_NULL,
                            th_led2_stack, sizeof(th_led2_stack),
                            THREAD_PRIORITY, THREAD_TICK);
    if (result != RT_EOK)
    {
        LOG_E("init th_led2 failed: %d", result);
        return result;
    }
    rt_thread_startup(&th_led2);

    th_monitor = rt_thread_create("th_monitor", monitor_entry, RT_NULL,
                                  MONITOR_STACK_SIZE, 10, THREAD_TICK);
    if (th_monitor == RT_NULL)
    {
        LOG_E("create th_monitor failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(th_monitor);

    return 0;
}
INIT_APP_EXPORT(thread_study_init);
