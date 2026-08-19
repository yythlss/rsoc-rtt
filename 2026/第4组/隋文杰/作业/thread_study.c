/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023-5-10      ShiHao       first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define DBG_TAG "main"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* 配置 GPIO 引脚 */
#define PIN_LED_B              GET_PIN(F, 11)      // PF11 :  LED_B        --> LED
#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED
#define KEY_PIN                GET_PIN(C, 1)

rt_thread_t th_led1 = RT_NULL;
#define TH_LED1_PRIORITY         16
#define TH_LED1_STACK_SIZE       1024
#define TH_LED1_TIMESLICE        5

ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t th_led2_stack[1024]; // 静态定义栈
static struct rt_thread th_led2; // 静态定义 TCB (线程控制块)

rt_thread_t th_monitor = RT_NULL;
#define TH_MONINTOR_PRIORITY         10
#define TH_MONINTOR_STACK_SIZE      1024
#define TH_MONINTOR_TIMESLICE        5

static rt_sem_t sem_led1 = RT_NULL;

const char *state_to_str(rt_uint8_t stat)
{
    switch (stat)
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

static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    while (1)
    {
        if (rt_sem_take(sem_led1, RT_WAITING_FOREVER) == RT_EOK)
        {
            rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
            rt_kprintf("th_led1 count: %d\n", count++);

            rt_thread_mdelay(500);

        }
    }
}

static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);

    while (1)
    {
        rt_pin_write(PIN_LED_B, !rt_pin_read(PIN_LED_B));
        rt_kprintf("th_led2 count: %d\n", count++);
        rt_thread_mdelay(500);
    }
}

static void th_monitor_entry(void *parameter)
{
    rt_uint8_t key_state;
    rt_uint8_t key_prev = 1;

    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);

    while (1)
    {
        key_state = rt_pin_read(KEY_PIN);

        /* 下降沿：按键按下 */
        if ((key_prev == 1) && (key_state == 0))
        {
            rt_thread_mdelay(20);   // 消抖
            if (rt_pin_read(KEY_PIN) == 0)
            {

                rt_kprintf("th_led1 stat: %s\n", state_to_str(th_led1->stat));
                rt_kprintf("th_led2 stat: %s\n", state_to_str(th_led2.stat));

                rt_uint32_t th_led1_used = (rt_uint32_t) th_led1->stack_addr + th_led1->stack_size
                        - (rt_uint32_t) th_led1->sp;

                rt_uint32_t th_led2_used = (rt_uint32_t) th_led2.stack_addr + th_led2.stack_size
                        - (rt_uint32_t) th_led2.sp;

                rt_kprintf("th_led1 used: %d\n", th_led1_used);
                rt_kprintf("th_led2 used: %d\n", th_led2_used);

                /*
                 * “恢复”语义：
                 * 释放一次信号量，th_led1 立即继续运行
                 */
                rt_sem_release(sem_led1);
                rt_kprintf("Key: th_led1 resumed\n");

            }
        }

        key_prev = key_state;
        rt_thread_mdelay(10);
    }
}

int multi_thread_init(void)
{

    sem_led1 = rt_sem_create("sem_led1", 0, RT_IPC_FLAG_FIFO);
    if (sem_led1 == RT_NULL)
    {
        rt_kprintf("Failed to create sem_led1!\n");
        return -1;
    }

    /* ---------- 动态创建 th_led1 ---------- */
    th_led1 = rt_thread_create("th_led1",          // 线程名
            th_led1_entry,      // 入口函数
            RT_NULL,            // 参数
            TH_LED1_STACK_SIZE,  // 栈大小
            TH_LED1_PRIORITY,    // 优先级
            TH_LED1_TIMESLICE    // 时间片
            );

    if (th_led1 != RT_NULL)
    {
        rt_thread_startup(th_led1);
    }
    else
    {
        rt_kprintf("Failed to create th_led1!\n");
    }

    /* ---------- 静态初始化 th_led2 ---------- */
    rt_thread_init(&th_led2,           // 线程控制块
            "th_led2",          // 线程名
            th_led2_entry,      // 入口函数
            RT_NULL,            // 参数
            th_led2_stack,      // 栈地址
            sizeof(th_led2_stack), TH_LED1_PRIORITY, TH_LED1_TIMESLICE);

    rt_thread_startup(&th_led2);

    th_monitor = rt_thread_create("th_monitor", th_monitor_entry,
    RT_NULL,
    TH_MONINTOR_STACK_SIZE,
    TH_MONINTOR_PRIORITY,   // 按键优先级略低，不影响 LED 线程
            TH_MONINTOR_TIMESLICE);

    if (th_monitor != RT_NULL)
    {
        rt_thread_startup(th_monitor);
    }
    else
    {
        rt_kprintf("Failed to create th_monitor!\n");
    }
    return 0;
}

INIT_APP_EXPORT(multi_thread_init);

int main(void)
{
//    rt_kprintf("Hello RT-Thread!\n");
    return 0;
}

