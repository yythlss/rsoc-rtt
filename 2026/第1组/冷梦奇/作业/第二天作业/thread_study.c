/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     Leng Mengqi  Day2 Thread Study Implementation (Beginner friendly)
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* 引脚定义（星火1号板载硬件） */
#define PIN_LED_R       GET_PIN(F, 12)  /* 红色 LED */
#define PIN_LED_B       GET_PIN(F, 11)  /* 蓝色 LED */
#define PIN_KEY         GET_PIN(C, 0)   /* KEY_LEFT 按键 */

/* 线程句柄与控制块 */
static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;

/* 静态线程控制块与栈空间 */
static struct rt_thread th_led2;
static rt_uint8_t th_led2_stack[1024];

/* 按键挂起状态标志 */
static rt_bool_t is_suspended = RT_FALSE;

/* ================= 1. 工作线程 1（动态创建） ================= */
static void led1_thread_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);

    while (1)
    {
        /* 翻转红灯 */
        rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
        rt_kprintf("[th_led1] count: %d, tick: %d\n", count++, rt_tick_get());
        rt_thread_mdelay(500);
    }
}

/* ================= 2. 工作线程 2（静态初始化） ================= */
static void led2_thread_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);

    while (1)
    {
        /* 翻转蓝灯 */
        rt_pin_write(PIN_LED_B, !rt_pin_read(PIN_LED_B));
        rt_kprintf("[th_led2] count: %d\n", count++);
        rt_thread_mdelay(500);
    }
}

/* ================= 3. 按键中断：控制 th_led1 挂起与恢复 ================= */
static void key_callback(void *args)
{
    static rt_tick_t last_tick = 0;
    rt_tick_t current_tick = rt_tick_get();

    /* 软件消抖：两次触发间隔小于 200ms 则判定为抖动，直接忽略 */
    if ((current_tick - last_tick) < rt_tick_from_millisecond(200))
    {
        return;
    }
    last_tick = current_tick;

    if (th_led1 == RT_NULL)
    {
        return;
    }

    if (is_suspended == RT_FALSE)
    {
        rt_thread_suspend(th_led1);
        is_suspended = RT_TRUE;
        rt_kprintf("\n>>> [Key] th_led1 suspended! <<<\n\n");
    }
    else
    {
        rt_thread_resume(th_led1);
        is_suspended = RT_FALSE;
        rt_kprintf("\n>>> [Key] th_led1 resumed! <<<\n\n");
    }
}

/* ================= 4. 监控线程（动态创建） ================= */
static void monitor_thread_entry(void *parameter)
{
    while (1)
    {
        rt_thread_mdelay(2000);

        rt_kprintf("\n--- Monitor Report ---\n");

        if (th_led1 != RT_NULL)
        {
            /* Cortex-M 栈向下生长：当前栈顶指针 sp 到栈底 stack_addr 即为剩余栈空间 */
            rt_size_t free1 = (rt_uint32_t)th_led1->sp - (rt_uint32_t)th_led1->stack_addr;
            rt_kprintf("th_led1: stat=0x%02x, free stack=%u B\n", th_led1->stat, free1);
        }

        rt_size_t free2 = (rt_uint32_t)th_led2.sp - (rt_uint32_t)th_led2.stack_addr;
        rt_kprintf("th_led2: stat=0x%02x, free stack=%u B\n", th_led2.stat, free2);
        rt_kprintf("-----------------------\n\n");
    }
}

/* ================= 5. 自定义 MSH 命令（作业要求导出查看命令） ================= */
static void thread_dump(void)
{
    rt_kprintf("=== Custom Thread Dump ===\n");
    if (th_led1 != RT_NULL)
    {
        rt_kprintf("th_led1: prio=%d, stat=0x%02x\n", th_led1->current_priority, th_led1->stat);
    }
    rt_kprintf("th_led2: prio=%d, stat=0x%02x\n", th_led2.current_priority, th_led2.stat);
}
MSH_CMD_EXPORT(thread_dump, show thread info);

/* ================= 6. 自动初始化模块 ================= */
static int thread_study_init(void)
{
    /* 1. 动态创建 th_led1（优先级 16，时间片 5，栈 1024） */
    th_led1 = rt_thread_create("th_led1",
                               led1_thread_entry,
                               RT_NULL,
                               1024,
                               16,
                               5);
    if (th_led1 != RT_NULL)
    {
        rt_thread_startup(th_led1);
    }

    /* 2. 静态初始化 th_led2（优先级 16，时间片 5，栈 1024） */
    rt_thread_init(&th_led2,
                   "th_led2",
                   led2_thread_entry,
                   RT_NULL,
                   th_led2_stack,
                   sizeof(th_led2_stack),
                   16,
                   5);
    rt_thread_startup(&th_led2);

    /* 3. 配置按键中断 */
    rt_pin_mode(PIN_KEY, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(PIN_KEY, PIN_IRQ_MODE_FALLING, key_callback, RT_NULL);
    rt_pin_irq_enable(PIN_KEY, PIN_IRQ_ENABLE);

    /* 4. 动态创建 th_monitor（优先级 10，高于工作线程） */
    th_monitor = rt_thread_create("th_monitor",
                                  monitor_thread_entry,
                                  RT_NULL,
                                  1024,
                                  10,
                                  5);
    if (th_monitor != RT_NULL)
    {
        rt_thread_startup(th_monitor);
    }

    return 0;
}
INIT_APP_EXPORT(thread_study_init);
