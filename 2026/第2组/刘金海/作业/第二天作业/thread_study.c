/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-18     yanran       the first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/*-----------------------引脚定义-----------------------*/
#define PIN_LED_R       rt_pin_get("PF.12")
#define PIN_LED_B       rt_pin_get("PF.11")
#define PIN_KEY_UP      rt_pin_get("PC.5")

/*-----------------------线程参数-----------------------*/
#define TH_LED1_PRIORITY                16
#define TH_LED1_TIMESLICE               5
#define TH_LED1_STACK_SIZE              1024

#define TH_LED2_PRIORITY                16
#define TH_LED2_TIMESLICE               5
#define TH_LED2_STACK_SIZE              1024

#define TH_MONITOR_PRIORITY             10
#define TH_MONITOR_TIMESLICE            5
#define TH_MONITOR_STACK_SIZE           1024

#define TH_LED1_DELAY_MS                500
#define TH_LED2_DELAY_MS                1000
#define TH_MONITOR_PERIOD_MS            2000

/*-------------------------线程句柄--------------------*/
static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_led2 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;


/* th_led2 采用静态方式：TCB 与栈均在全局数据段静态定义 */
static struct rt_thread led2_thread;
static rt_uint8_t led2_stack[TH_LED2_STACK_SIZE];

/* ===================== 按键与挂起控制 ===================== */
static volatile rt_uint8_t key_pressed = 0;        /* ISR 中置位，monitor 中消费 */
static volatile rt_uint8_t led1_suspend_req = 0;   /* 请求 th_led1 自行挂起 */

/* 按键中断回调（ISR 上下文，仅置位标志） */
static void key_up_callback(void *args)
{
    key_pressed = 1;
}

/* ===================== 工具函数 ===================== */

/*
 * 计算线程剩余（未使用）栈空间。
 * RT-Thread 创建线程时用 '#'(0x23) 填充整个栈区，ARM 栈向下生长，
 * 从低地址向高地址扫描连续 '#' 的字节数即为剩余栈。
 */
static rt_uint32_t get_stack_remain(rt_thread_t thread)
{
    rt_uint32_t remain = 0;
    rt_uint8_t *p = (rt_uint8_t *)thread->stack_addr;
    rt_uint8_t *end = (rt_uint8_t *)((rt_ubase_t)thread->stack_addr + thread->stack_size);

    while ((p < end) && (*p == '#'))
    {
        remain++;
        p++;
    }
    return remain;
}

/* 线程状态数值 -> 可读字符串 */
static const char *sta_to_str(rt_uint8_t stat)
{
    stat &= RT_THREAD_STAT_MASK;
    switch (stat)
    {
        case RT_THREAD_INIT:    return "INIT";
        case RT_THREAD_READY:   return "READY";
        case RT_THREAD_SUSPEND: return "SUSPEND";
        case RT_THREAD_RUNNING: return "RUNNING";
        case RT_THREAD_CLOSE:   return "CLOSE";
        default:                return "UNKNOWN";
    }
}

/* ===================== 工作线程 1：动态创建，翻转 LED_R ===================== */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    /* 按键挂起请求由本线程自行挂起（本内核 rt_thread_suspend 仅允许挂起自身） */
    while (1)
    {
        if (led1_suspend_req)
        {
            led1_suspend_req = 0;
            rt_kprintf("th_led1: self suspend\n");
            rt_thread_suspend(rt_thread_self());
            rt_schedule();
            rt_kprintf("th_led1: resumed\n");
        }

        rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
        rt_kprintf("th_led1: count=%d, tick=%u\n", count++, (rt_ubase_t)rt_tick_get());
        rt_thread_mdelay(TH_LED1_DELAY_MS);
    }
}

/* ===================== 工作线程 2：静态初始化，翻转 LED_B ===================== */
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        rt_pin_write(PIN_LED_B, !rt_pin_read(PIN_LED_B));
        rt_kprintf("th_led2: count=%d\n", count++);
        rt_thread_mdelay(TH_LED2_DELAY_MS);
    }
}

/* ===================== 监控与控制线程：打印状态、处理按键 ===================== */
static void th_monitor_entry(void *parameter)
{
    rt_err_t res;

    while (1)
    {
        rt_thread_mdelay(TH_MONITOR_PERIOD_MS);

        /* 按键单次触发：挂起/恢复 th_led1 */
        if (key_pressed)
        {
            key_pressed = 0;
            if ((th_led1->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
            {
                res = rt_thread_resume(th_led1);
                rt_kprintf("monitor: resume th_led1, ret=%d\n", res);
            }
            else
            {
                led1_suspend_req = 1;
                rt_kprintf("monitor: request th_led1 suspend\n");
            }
        }

        /* 打印两个工作线程的运行状态与剩余栈空间 */
        rt_kprintf("monitor: [%-8s] stat=%-8s stack_remain=%uB | [%-8s] stat=%-8s stack_remain=%uB\n",
              th_led1->name, sta_to_str(th_led1->stat), get_stack_remain(th_led1),
              th_led2->name, sta_to_str(th_led2->stat), get_stack_remain(th_led2));
    }
}

/* ===================== 自定义 MSH 命令：thread_dump ===================== */
static void thread_dump(void)
{
    rt_kprintf("--------------------------------------------------------\n");
    rt_kprintf("name      stat       prio    remain_stack(B)\n");
    rt_kprintf("--------------------------------------------------------\n");
    rt_kprintf("%-9s %-9s %-7d %u\n", th_led1->name, sta_to_str(th_led1->stat),
               th_led1->current_priority, get_stack_remain(th_led1));
    rt_kprintf("%-9s %-9s %-7d %u\n", th_led2->name, sta_to_str(th_led2->stat),
               th_led2->current_priority, get_stack_remain(th_led2));
    rt_kprintf("--------------------------------------------------------\n");
}
MSH_CMD_EXPORT(thread_dump, dump thread status and remain stack);

/* ===================== 自动初始化（禁止在 main.c 硬编码调用） ===================== */
static int thread_study_init(void)
{
    rt_err_t res;

    /* 板载 LED 引脚初始化 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);

    /* 按键中断初始化 */
    rt_pin_mode(PIN_KEY_UP, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(PIN_KEY_UP, PIN_IRQ_MODE_FALLING, key_up_callback, RT_NULL);
    rt_pin_irq_enable(PIN_KEY_UP, PIN_IRQ_ENABLE);

    /* 动态创建 th_led1 */
    th_led1 = rt_thread_create("th_led1",
                               th_led1_entry,
                               RT_NULL,
                               TH_LED1_STACK_SIZE,
                               TH_LED1_PRIORITY,
                               TH_LED1_TIMESLICE);
    if (th_led1 == RT_NULL)
    {
        rt_kprintf("create th_led1 failed\n");
        return -RT_ERROR;
    }
    rt_thread_startup(th_led1);

    /* 静态初始化 th_led2 */
    res = rt_thread_init(&led2_thread,
                         "th_led2",
                         th_led2_entry,
                         RT_NULL,
                         led2_stack,
                         sizeof(led2_stack),
                         TH_LED2_PRIORITY,
                         TH_LED2_TIMESLICE);
    if (res != RT_EOK)
    {
        rt_kprintf("init th_led2 failed, ret=%d\n", res);
        return res;
    }
    th_led2 = &led2_thread;
    rt_thread_startup(th_led2);

    /* 动态创建 th_monitor */
    th_monitor = rt_thread_create("th_mon",
                                  th_monitor_entry,
                                  RT_NULL,
                                  TH_MONITOR_STACK_SIZE,
                                  TH_MONITOR_PRIORITY,
                                  TH_MONITOR_TIMESLICE);
    if (th_monitor == RT_NULL)
    {
        rt_kprintf("create th_monitor failed\n");
        return -RT_ERROR;
    }
    rt_thread_startup(th_monitor);

    rt_kprintf("thread_study_init done\n");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);

