/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-18     25488       the first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

extern rt_thread_t th_led1;            /* 来自 led_task.c */
extern struct rt_thread th_led2_tcb;   /* 来自 led_task.c */

#define KEY0_PIN    GET_PIN(C, 0)

static rt_thread_t th_monitor = RT_NULL;
static rt_uint8_t led1_suspended = 0;

static const char *thread_stat_name(rt_uint8_t stat)
{
    switch (stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_READY:   return "ready";
    case RT_THREAD_SUSPEND: return "suspend";
    case RT_THREAD_RUNNING: return "running";
    case RT_THREAD_CLOSE:   return "close";
    default:                return "init";
    }
}

static rt_ubase_t thread_remain(rt_thread_t t)
{

    return (rt_ubase_t)t->sp - (rt_ubase_t)t->stack_addr;
}

static void print_thread_state(const char *name, rt_thread_t t)
{
    if (t == RT_NULL) { rt_kprintf("%s: not created\n", name); return; }
    rt_kprintf("%s: stat=%s, remain_stack=%u / %u\n",
               name, thread_stat_name(t->stat),
               (unsigned int)thread_remain(t),
               (unsigned int)t->stack_size);
}

static void th_monitor_entry(void *parameter)
{
    rt_pin_mode(KEY0_PIN, PIN_MODE_INPUT_PULLUP);

    while(1)
    {
        rt_kprintf("--- monitor ---\n");
        print_thread_state("th_led1", th_led1);
        print_thread_state("th_led2", &th_led2_tcb);

        /* 按一次 KEY0 挂起 th_led1，再按一次恢复 */
        if (rt_pin_read(KEY0_PIN) == PIN_LOW)
        {
            rt_thread_mdelay(20);
            if (rt_pin_read(KEY0_PIN) == PIN_LOW)
            {
                if (led1_suspended)
                {
                    rt_thread_resume(th_led1);
                    led1_suspended = 0;
                    rt_kprintf("KEY0: resume th_led1\n");
                }
                else
                {
                    rt_thread_suspend(th_led1);
                    led1_suspended = 1;
                    rt_kprintf("KEY0: suspend th_led1\n");
                }
                while (rt_pin_read(KEY0_PIN) == PIN_LOW)
                    rt_thread_mdelay(10);
            }
        }

        rt_thread_mdelay(2000);
    }
}

static void thread_dump(void)
{
    print_thread_state("th_led1", th_led1);
    print_thread_state("th_led2", &th_led2_tcb);
    print_thread_state("th_monitor", th_monitor);
}
MSH_CMD_EXPORT(thread_dump, dump status of led and monitor threads);

static int monitor_task_init(void)
{
    th_monitor = rt_thread_create("monitor", th_monitor_entry, RT_NULL,
                                  2048, 10, 5);
    if (th_monitor != RT_NULL) rt_thread_startup(th_monitor);
    return 0;
}
INIT_APP_EXPORT(monitor_task_init);
