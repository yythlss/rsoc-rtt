/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     wangqi       the first version
 *
 * 作业说明：
 * - 动态创建 th_led1 (优先级16, 时间片5, 栈1024) 翻转 LED_R
 * - 静态创建 th_led2 (优先级16, 时间片5, 全局栈+TCB) 翻转 LED_B
 * - 监控线程 th_monitor (优先级10) 每2s打印状态 + 轮询按键控制挂起/恢复
 * - 使用 INIT_APP_EXPORT 自动初始化，MSH_CMD_EXPORT 导出自定义命令
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define THREAD_PRIORITY_LED     16
#define THREAD_PRIORITY_MON     10
#define THREAD_TIMESLICE        5
#define THREAD_STACK_SIZE_LED1  1024

// 引脚定义（请按原理图修改）
#ifndef LED_R_PIN
#define LED_R_PIN GET_PIN(F, 12)
#endif
#ifndef LED_B_PIN
#define LED_B_PIN GET_PIN(F, 11)
#endif
#ifndef KEY0_PIN
#define KEY0_PIN GET_PIN(C, 1)
#endif

static struct rt_thread led2_thread;
static rt_uint8_t led2_stack[512];

static rt_thread_t led1_thread = RT_NULL;
static rt_thread_t monitor_thread = RT_NULL;

// 新增：用于通知 th_led1 挂起的信号量
static rt_sem_t led1_sem = RT_NULL;

static volatile rt_uint8_t key_press_count = 0; // 用于切换挂起/恢复

/* ---------- 工具函数 ---------- */
static rt_uint32_t my_get_stack_remain(rt_thread_t thread)
{
    rt_uint8_t *ptr;
    rt_uint32_t remain = 0;

    if (thread == RT_NULL) return 0;

    ptr = (rt_uint8_t *)thread->stack_addr;
    while (ptr < (rt_uint8_t *)(thread->stack_addr + thread->stack_size))
    {
        if (*ptr != '#') break;
        ptr++;
        remain++;
    }
    return remain;
}

static const char* get_thread_stat_str(rt_uint8_t stat)
{
    switch (stat)
    {
    case RT_THREAD_INIT: return "init";
    case RT_THREAD_READY: return "ready";
    case RT_THREAD_RUNNING: return "running";
    case RT_THREAD_SUSPEND: return "suspend";
    case RT_THREAD_CLOSE: return "close";
    default: return "unknown";
    }
}

/* ---------- 工作线程1 ---------- */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    while (1)
    {
        // 修改：使用信号量等待代替固定的 mdelay
        // 等待 500 个系统节拍，如果超时则继续执行，如果收到信号则立即返回
        rt_err_t result = rt_sem_take(led1_sem, 500);

        if (result == -RT_ETIMEOUT)
        {
            // 正常超时，执行 LED 翻转
            rt_pin_write(LED_R_PIN, !rt_pin_read(LED_R_PIN));
            rt_kprintf("[th_led1] count=%d tick=%d\n", count, rt_tick_get());
            count++;
        }
        else if (result == RT_EOK)
        {
            // 收到了来自 th_monitor 的信号，主动挂起自己
            rt_kprintf("[th_led1] Received signal, suspending self.\n");
            rt_thread_suspend(rt_thread_self());
            // 线程被恢复后，会从 suspend 处继续执行，即回到 while(1) 循环开头
        }
    }
}

/* ---------- 工作线程2 ---------- */
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    while (1)
    {
        rt_pin_write(LED_B_PIN, !rt_pin_read(LED_B_PIN));
        rt_kprintf("[th_led2] count=%d\n", count);
        count++;
        rt_thread_mdelay(500);
    }
}

/* ---------- 监控线程（轮询按键） ---------- */
static void th_monitor_entry(void *parameter)
{
    rt_uint8_t key_prev = 1; // 假设按键上拉，未按下为高电平
    rt_uint8_t key_cur;

    while (1)
    {
        // ----- 轮询按键（检测下降沿） -----
        key_cur = rt_pin_read(KEY0_PIN);
        if ((key_prev == 1) && (key_cur == 0)) // 下降沿
        {
            rt_thread_mdelay(20); // 消抖
            if (rt_pin_read(KEY0_PIN) == 0)
            {
                key_press_count++;
                rt_kprintf("[th_monitor] Key Pressed %d times.\n", key_press_count);

                // 修改：通过信号量通知 th_led1，而不是直接挂起它
                if (led1_thread != RT_NULL)
                {
                    if (key_press_count % 2 == 1) // 奇数次按键，请求挂起
                    {
                        rt_sem_release(led1_sem);
                        rt_kprintf("[th_monitor] Signal sent to suspend th_led1.\n");
                    }
                    else // 偶数次按键，恢复
                    {
                        rt_thread_resume(led1_thread);
                        rt_kprintf("[th_monitor] th_led1 Resumed.\n");
                    }
                }
                else
                {
                     rt_kprintf("[th_monitor] Warning: led1_thread is NULL.\n");
                }
            }
        }
        key_prev = key_cur;

        // ----- 每2秒打印一次状态 -----
        static rt_tick_t last_print = 0;
        if (rt_tick_get() - last_print >= RT_TICK_PER_SECOND * 2)
        {
            last_print = rt_tick_get();
            rt_kprintf("[th_monitor] tick=%d\n"
                       " th_led1 : stat=%s pri=%d stack=%d/%d\n"
                       " th_led2 : stat=%s pri=%d stack=%d/%d\n",
                       rt_tick_get(),
                       (led1_thread ? get_thread_stat_str(led1_thread->stat) : "null"),
                       (led1_thread ? led1_thread->current_priority : 0),
                       (led1_thread ? my_get_stack_remain(led1_thread) : 0),
                       (led1_thread ? led1_thread->stack_size : 0),
                       get_thread_stat_str(led2_thread.stat),
                       led2_thread.current_priority,
                       my_get_stack_remain(&led2_thread),
                       led2_thread.stack_size);
        }
        rt_thread_mdelay(10); // 每10ms轮询一次
    }
}

/* ---------- 初始化 ---------- */
static int thread_study_init(void)
{
    rt_err_t result;

    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(KEY0_PIN, PIN_MODE_INPUT_PULLUP);

    // 新增：创建信号量
    led1_sem = rt_sem_create("led1sem", 0, RT_IPC_FLAG_FIFO);
    if (led1_sem == RT_NULL)
    {
        rt_kprintf("Failed to create semaphore!\n");
        return -1;
    }

    // 1. 动态创建 th_led1
    led1_thread = rt_thread_create("th_led1",
                                   th_led1_entry,
                                   RT_NULL,
                                   THREAD_STACK_SIZE_LED1,
                                   THREAD_PRIORITY_LED,
                                   THREAD_TIMESLICE);
    if (led1_thread != RT_NULL)
    {
        result = rt_thread_startup(led1_thread);
        if (result != RT_EOK) rt_kprintf("Failed to startup th_led1!\n");
    }
    else rt_kprintf("Failed to create th_led1!\n");

    // 2. 静态初始化 th_led2
    result = rt_thread_init(&led2_thread,
                            "th_led2",
                            th_led2_entry,
                            RT_NULL,
                            &led2_stack[0],
                            sizeof(led2_stack),
                            THREAD_PRIORITY_LED,
                            THREAD_TIMESLICE);
    if (result == RT_EOK)
    {
        result = rt_thread_startup(&led2_thread);
        if (result != RT_EOK) rt_kprintf("Failed to startup th_led2!\n");
    }
    else rt_kprintf("Failed to init th_led2!\n");

    // 3. 动态创建 th_monitor
    monitor_thread = rt_thread_create("th_monitor",
                                      th_monitor_entry,
                                      RT_NULL,
                                      1024,
                                      THREAD_PRIORITY_MON,
                                      10);
    if (monitor_thread != RT_NULL)
    {
        result = rt_thread_startup(monitor_thread);
        if (result != RT_EOK) rt_kprintf("Failed to startup th_monitor!\n");
    }
    else rt_kprintf("Failed to create th_monitor!\n");

    return 0;
}
INIT_APP_EXPORT(thread_study_init);

/* ---------- MSH 命令 ---------- */
void thread_dump(void)
{
    rt_kprintf("\n--- Thread Status Dump ---\n"
               " th_led1 : stat=%s pri=%d stack=%d/%d\n"
               " th_led2 : stat=%s pri=%d stack=%d/%d\n"
               "--------------------------\n",
               (led1_thread ? get_thread_stat_str(led1_thread->stat) : "null"),
               (led1_thread ? led1_thread->current_priority : 0),
               (led1_thread ? my_get_stack_remain(led1_thread) : 0),
               (led1_thread ? led1_thread->stack_size : 0),
               get_thread_stat_str(led2_thread.stat),
               led2_thread.current_priority,
               my_get_stack_remain(&led2_thread),
               led2_thread.stack_size);
}
MSH_CMD_EXPORT(thread_dump, Dump thread status for homework);
