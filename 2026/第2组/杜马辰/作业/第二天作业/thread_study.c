/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     DuMachen     DAY2 thread study implementation
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define DBG_TAG "thread_study"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/*----------------------- 板载硬件引脚定义 -----------------------*/
#define PIN_LED_R               GET_PIN(F, 12)  // 板载红色 LED
#define PIN_LED_B               GET_PIN(F, 11)  // 板载蓝色 LED
#define PIN_KEY0                GET_PIN(C, 0)   // 板载 KEY0 按键

/*----------------------- 线程参数配置 -----------------------*/
#define TH_LED1_PRIORITY        16
#define TH_LED1_TIMESLICE       5
#define TH_LED1_STACK_SIZE      1024

#define TH_LED2_PRIORITY        16
#define TH_LED2_TIMESLICE       5
#define TH_LED2_STACK_SIZE      1024

#define TH_MONITOR_PRIORITY     10
#define TH_MONITOR_TIMESLICE    5
#define TH_MONITOR_STACK_SIZE   1024

#define TH_LED1_PERIOD_MS       500
#define TH_LED2_PERIOD_MS       1000
#define TH_MONITOR_PERIOD_MS    2000

/*----------------------- 线程句柄与控制块 -----------------------*/
static rt_thread_t th_led1 = RT_NULL;    /* 工作线程 1 (动态创建) */
static rt_thread_t th_led2 = RT_NULL;    /* 工作线程 2 (静态句柄) */
static rt_thread_t th_monitor = RT_NULL; /* 监控线程 (动态创建) */

/* th_led2 采用静态方式：TCB 与栈均在全局数据段静态定义 */
ALIGN(RT_ALIGN_SIZE)
static struct rt_thread led2_thread;
static rt_uint8_t led2_stack[TH_LED2_STACK_SIZE];

/* 按键中断标志（ISR 中置位，monitor 线程中处理，符合 RTOS 规范） */
static volatile rt_uint8_t g_key_pressed = 0;

/*----------------------- 按键外部中断服务函数 -----------------------*/
static void key0_irq_callback(void *args)
{
    /* 中断上下文仅作轻量级标记 */
    g_key_pressed = 1;
}

/*----------------------- 工具函数：计算剩余栈空间 -----------------------*/
/*
 * RT-Thread 在创建线程时会使用魔数 '#' (0x23) 填充整个栈空间。
 * Cortex-M 架构栈向下生长，从栈底向栈顶扫描连续 '#' 字节数即为剩余安全栈空间。
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

/* 线程状态数值转换为可读字符串 */
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

/*----------------------- 工作线程 1：动态创建，翻转红灯 -----------------------*/
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        /* 翻转红色 LED 电平 */
        rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
        count++;

        LOG_I("[th_led1] count: %u, tick: %u", count, (rt_ubase_t)rt_tick_get());
        rt_thread_mdelay(TH_LED1_PERIOD_MS);
    }
}

/*----------------------- 工作线程 2：静态初始化，翻转蓝灯 -----------------------*/
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        /* 翻转蓝色 LED 电平 */
        rt_pin_write(PIN_LED_B, !rt_pin_read(PIN_LED_B));
        count++;

        LOG_I("[th_led2] count: %u", count);
        rt_thread_mdelay(TH_LED2_PERIOD_MS);
    }
}

/*----------------------- 监控与控制线程：周期打印与按键处理 -----------------------*/
static void th_monitor_entry(void *parameter)
{
    while (1)
    {
        rt_thread_mdelay(TH_MONITOR_PERIOD_MS);

        /* 处理按键触发：在线程上下文中执行挂起与恢复 */
        if (g_key_pressed)
        {
            g_key_pressed = 0;

            if (th_led1 != RT_NULL)
            {
                if ((th_led1->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
                {
                    rt_thread_resume(th_led1);
                    LOG_W(">> Monitor: [th_led1] has been RESUMED! <<");
                }
                else
                {
                    rt_thread_suspend(th_led1);
                    LOG_W(">> Monitor: [th_led1] has been SUSPENDED! <<");
                }
            }
        }

        /* 周期性打印工作线程运行状态与剩余栈空间 */
        rt_kprintf("\n============== [Thread Monitor Dump] ==============\n");
        if (th_led1 != RT_NULL)
        {
            rt_kprintf("Name: %-8s | Stat: %-7s | Priority: %2u | Stack Free: %4u / %4u Bytes\n",
                       th_led1->name,
                       sta_to_str(th_led1->stat),
                       th_led1->current_priority,
                       get_stack_remain(th_led1),
                       th_led1->stack_size);
        }
        if (th_led2 != RT_NULL)
        {
            rt_kprintf("Name: %-8s | Stat: %-7s | Priority: %2u | Stack Free: %4u / %4u Bytes\n",
                       th_led2->name,
                       sta_to_str(th_led2->stat),
                       th_led2->current_priority,
                       get_stack_remain(th_led2),
                       th_led2->stack_size);
        }
        rt_kprintf("===================================================\n\n");
    }
}

/*----------------------- 自定义 MSH 调试命令 -----------------------*/
static void thread_dump(int argc, char **argv)
{
    rt_kprintf("\n--------------------------------------------------------\n");
    rt_kprintf("Name      Status     Priority   Remaining_Tick   Stack_Remain\n");
    rt_kprintf("--------------------------------------------------------\n");
    if (th_led1 != RT_NULL)
    {
        rt_kprintf("%-9s %-10s %-10u %-16d %u Bytes\n",
                   th_led1->name,
                   sta_to_str(th_led1->stat),
                   th_led1->current_priority,
                   th_led1->remaining_tick,
                   get_stack_remain(th_led1));
    }
    if (th_led2 != RT_NULL)
    {
        rt_kprintf("%-9s %-10s %-10u %-16d %u Bytes\n",
                   th_led2->name,
                   sta_to_str(th_led2->stat),
                   th_led2->current_priority,
                   th_led2->remaining_tick,
                   get_stack_remain(th_led2));
    }
    rt_kprintf("--------------------------------------------------------\n\n");
}
MSH_CMD_EXPORT(thread_dump, dump worker thread status and stack watermark);

/*----------------------- 自动初始化入口 -----------------------*/
static int thread_study_init(void)
{
    rt_err_t res;

    /* 1. 初始化 GPIO 引脚 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);

    /* 2. 配置 KEY0 按键中断 (下降沿触发，上拉输入) */
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(PIN_KEY0, PIN_IRQ_MODE_FALLING, key0_irq_callback, RT_NULL);
    rt_pin_irq_enable(PIN_KEY0, PIN_IRQ_ENABLE);

    /* 3. 动态创建工作线程 1 (优先级 16, 时间片 5 ticks) */
    th_led1 = rt_thread_create("th_led1",
                               th_led1_entry,
                               RT_NULL,
                               TH_LED1_STACK_SIZE,
                               TH_LED1_PRIORITY,
                               TH_LED1_TIMESLICE);
    if (th_led1 == RT_NULL)
    {
        LOG_E("Failed to create dynamic thread th_led1!");
        return -RT_ERROR;
    }
    rt_thread_startup(th_led1);

    /* 4. 静态初始化工作线程 2 (优先级 16, 时间片 5 ticks) */
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
        LOG_E("Failed to initialize static thread th_led2!");
        return res;
    }
    th_led2 = &led2_thread;
    rt_thread_startup(th_led2);

    /* 5. 动态创建监控线程 (优先级 10, 时间片 5 ticks) */
    th_monitor = rt_thread_create("th_mon",
                                  th_monitor_entry,
                                  RT_NULL,
                                  TH_MONITOR_STACK_SIZE,
                                  TH_MONITOR_PRIORITY,
                                  TH_MONITOR_TIMESLICE);
    if (th_monitor == RT_NULL)
    {
        LOG_E("Failed to create dynamic thread th_monitor!");
        return -RT_ERROR;
    }
    rt_thread_startup(th_monitor);

    LOG_I("DAY2 Thread Study module initialized successfully via INIT_APP_EXPORT.");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
