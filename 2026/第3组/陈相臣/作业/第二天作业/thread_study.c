/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-18     RT-Thread    Day2 multi-thread exercise
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

/* 星火号板载资源引脚。LED 为低电平点亮，KEY0 使用内部上拉输入。 */
#define PIN_LED_R          GET_PIN(F, 12)
#define PIN_LED_B          GET_PIN(F, 11)
#define PIN_KEY0           GET_PIN(C, 0)

/* 的工作线程参数，以及监控线程参数。 */
#define THREAD_STACK_SIZE  1024
#define WORKER_PRIORITY    16
#define WORKER_TIMESLICE   5
#define MONITOR_PRIORITY   10
#define MONITOR_TIMESLICE  5

/* 动态创建的线程使用句柄保存，便于查询状态和挂起/恢复。 */
static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;

/* th_led2 使用静态 TCB 和全局栈，无需动态内存分配。 */
static struct rt_thread th_led2;
ALIGN(RT_ALIGN_SIZE)
static char th_led2_stack[THREAD_STACK_SIZE];

/*
 * 当前 RT-Thread 内核中 rt_thread_suspend() 只能挂起调用它的线程自身。
 * 因此监控线程只设置请求标志，由 th_led1 收到请求后自我挂起。
 */
static volatile int th_led1_suspended = 0;
static volatile int th_led1_suspend_request = 0;

/* 根据栈初始化时填充的 '#' 统计剩余栈空间，供监控线程打印。 */
static rt_uint32_t thread_stack_free(struct rt_thread *thread)
{
    rt_uint8_t *ptr = (rt_uint8_t *)thread->stack_addr;
    rt_uint8_t *end = ptr + thread->stack_size;

    while (ptr < end && *ptr == '#')
    {
        ptr++;
    }

    return (rt_uint32_t)(ptr - (rt_uint8_t *)thread->stack_addr);
}

/* 动态工作线程：翻转 LED_R，并打印计数和系统 tick。 */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_base_t level = PIN_HIGH;

    (void)parameter;

    while (1)
    {
        /* 收到监控线程的挂起请求后，由本线程自己进入挂起状态。 */
        if (th_led1_suspend_request != 0)
        {
            th_led1_suspend_request = 0;
            th_led1_suspended = 1;
            rt_kprintf("[th_led1] suspending itself\n");
            rt_thread_suspend(rt_thread_self());
            rt_schedule();
            th_led1_suspended = 0;
            rt_kprintf("[th_led1] resumed\n");
        }

        /* 每次循环翻转一次红色 LED，周期约 500 ms。 */
        count++;
        level = (level == PIN_LOW) ? PIN_HIGH : PIN_LOW;
        rt_pin_write(PIN_LED_R, level);
        rt_kprintf("[th_led1] count=%lu tick=%lu\n",
                   (unsigned long)count, (unsigned long)rt_tick_get());
        rt_thread_mdelay(500);
    }
}

/* 静态工作线程：翻转 LED_B，并打印计数。 */
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_base_t level = PIN_HIGH;

    (void)parameter;

    while (1)
    {
        count++;
        level = (level == PIN_LOW) ? PIN_HIGH : PIN_LOW;
        rt_pin_write(PIN_LED_B, level);
        rt_kprintf("[th_led2] count=%lu\n", (unsigned long)count);
        rt_thread_mdelay(500);
    }
}

/* 打印指定线程的运行状态和剩余栈空间。 */
static void print_thread_status(const char *name, struct rt_thread *thread)
{
    if (thread == RT_NULL)
    {
        rt_kprintf("%s: not created\n", name);
        return;
    }

    rt_kprintf("%s: state=0x%02x free=%lu\n",
               name,
               (unsigned int)(thread->stat & RT_THREAD_STAT_MASK),
               (unsigned long)thread_stack_free(thread));
}

/* 动态监控线程：周期打印状态，并根据 KEY0 切换 th_led1 的挂起/恢复。 */
static void th_monitor_entry(void *parameter)
{
    (void)parameter;

    while (1)
    {
        /* 以 2 秒为周期查看工作线程状态。 */
        rt_thread_mdelay(2000);

        print_thread_status("th_led1", th_led1);
        print_thread_status("th_led2", &th_led2);

        /* KEY0 低电平时执行一次挂起或恢复切换。 */
        if ((th_led1 != RT_NULL) && (rt_pin_read(PIN_KEY0) == PIN_LOW))
        {
            if (th_led1_suspended != 0)
            {
                /* 已挂起，则恢复 th_led1。 */
                rt_thread_resume(th_led1);
                rt_kprintf("[monitor] th_led1 resumed\n");
            }
            else
            {
                /* 未挂起，则请求 th_led1 自行挂起。 */
                th_led1_suspend_request = 1;
                rt_kprintf("[monitor] th_led1 suspend requested\n");
            }

            /* 等待按键释放，避免一次按下被重复处理。 */
            while (rt_pin_read(PIN_KEY0) == PIN_LOW)
            {
                rt_thread_mdelay(10);
            }
        }
    }
}

/* 应用初始化入口：配置外设并创建两个工作线程和一个监控线程。 */
static int thread_study_init(void)
{
    /* 初始化 LED 输出和 KEY0 内部上拉输入。 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);

    /* 两个 LED 初始置高，即熄灭状态。 */
    rt_pin_write(PIN_LED_R, PIN_HIGH);
    rt_pin_write(PIN_LED_B, PIN_HIGH);

    /* 动态创建并启动红色 LED 工作线程。 */
    th_led1 = rt_thread_create("th_led1",
                               th_led1_entry,
                               RT_NULL,
                               THREAD_STACK_SIZE,
                               WORKER_PRIORITY,
                               WORKER_TIMESLICE);
    if (th_led1 != RT_NULL)
    {
        rt_thread_startup(th_led1);
    }

    /* 静态初始化并启动蓝色 LED 工作线程。 */
    if (rt_thread_init(&th_led2,
                       "th_led2",
                       th_led2_entry,
                       RT_NULL,
                       th_led2_stack,
                       sizeof(th_led2_stack),
                       WORKER_PRIORITY,
                       WORKER_TIMESLICE) == RT_EOK)
    {
        rt_thread_startup(&th_led2);
    }

    /* 动态创建并启动监控线程。 */
    th_monitor = rt_thread_create("monitor",
                                  th_monitor_entry,
                                  RT_NULL,
                                  THREAD_STACK_SIZE,
                                  MONITOR_PRIORITY,
                                  MONITOR_TIMESLICE);
    if (th_monitor != RT_NULL)
    {
        rt_thread_startup(th_monitor);
    }

    rt_kprintf("[thread_study] init done\n");
    return RT_EOK;
}

/* 注册到应用自动初始化，main.c 中无需手动调用本函数。 */
INIT_APP_EXPORT(thread_study_init);

#ifdef RT_USING_FINSH
/* FinSH 命令处理函数：输出三个线程的状态。 */
static int thread_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("thread dump:\n");
    print_thread_status("th_led1", th_led1);
    print_thread_status("th_led2", &th_led2);
    print_thread_status("monitor", th_monitor);

    return RT_EOK;
}

/* 导出 thread_dump 命令，可在 FinSH 中直接输入查看线程状态。 */
MSH_CMD_EXPORT(thread_dump, dump thread_study thread status);
#endif
