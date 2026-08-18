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
#include <rtdevice.h>
#include <finsh.h>
#include "board.h"

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define LED_R                      GET_PIN(F, 12)
#define LED_B                      GET_PIN(F, 11)
#define KEY_PIN                    GET_PIN(C, 4)

#define THREAD_PRIORITY            16
#define MONITOR_THREAD_PRIORITY    10
#define THREAD_STACK_SIZE          1024
#define THREAD_TIMESLICE           5

static rt_thread_t tid_led1 = RT_NULL;
static rt_sem_t sem_led1 = RT_NULL;
static volatile rt_uint8_t led1_paused = 0;

static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_pin_mode(LED_R, PIN_MODE_OUTPUT);   /* 设置 LED 引脚为输出 */
    while (1)
    {
        if (led1_paused)             /* 暂停中？ */
        {
            rt_sem_take(sem_led1, 0);                  /* 先清掉积压的信号 */
            rt_sem_take(sem_led1, RT_WAITING_FOREVER); /* 真正阻塞等唤醒 */
        }
        rt_pin_write(LED_R, !rt_pin_read(LED_R));
        rt_kprintf("th_led1 count: %d, rt_tick: %d\n", count ++, rt_tick_get());
        rt_thread_mdelay(10);

    }
}

static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_pin_mode(LED_B, PIN_MODE_OUTPUT);
    while (1)
    {
        rt_pin_write(LED_B, !rt_pin_read(LED_B));
        rt_kprintf("th_led2 count: %d\n", count ++);
        rt_thread_mdelay(10);

    }
}

static const char *stat2str(rt_uint8_t s)
{
    switch (s)
    {
        case RT_THREAD_INIT:    return "init";
        case RT_THREAD_READY:   return "ready";
        case RT_THREAD_SUSPEND: return "suspend";
        case RT_THREAD_RUNNING: return "running";
        case RT_THREAD_CLOSE:   return "close";
        default:                return "?";
    }
}

static void key_callback(void *args)
{
    rt_sem_release(sem_led1);
    led1_paused = !led1_paused;
}

static void th_monitor_entry(void *parameter)
{
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(KEY_PIN, PIN_IRQ_MODE_FALLING, key_callback, RT_NULL);
    rt_pin_irq_enable(KEY_PIN, PIN_IRQ_ENABLE);
    while (1)
    {
        rt_uint8_t s1 = tid_led1->stat & RT_THREAD_STAT_MASK;      /* 状态 */
        rt_uint32_t remain1 = tid_led1->stack_size
                              - ((rt_ubase_t)tid_led1->stack_addr + tid_led1->stack_size
                                 - (rt_ubase_t)tid_led1->sp);
        rt_kprintf("th_led1 stat: %s, remain: %u\n", stat2str(s1), remain1);
        rt_thread_mdelay(2000);
    }
}

ALIGN(RT_ALIGN_SIZE)
static char th_led2_stack[THREAD_STACK_SIZE];
static struct rt_thread th_led2;

static int rt_demo_init(void)
{
    sem_led1 = rt_sem_create("sem_led1", 0, RT_IPC_FLAG_FIFO);

    tid_led1 = rt_thread_create("th_led1", th_led1_entry, RT_NULL,
                                THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid_led1 != RT_NULL)
    {
        rt_thread_startup(tid_led1);   // 启动线程
    }

    rt_thread_init(&th_led2,
                   "th_led2",
                   th_led2_entry,
                   RT_NULL,
                   &th_led2_stack[0],
                   sizeof(th_led2_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
    rt_thread_startup(&th_led2);

    rt_thread_t mon = rt_thread_create("th_monitor", th_monitor_entry, RT_NULL,
                                       THREAD_STACK_SIZE, MONITOR_THREAD_PRIORITY, THREAD_TIMESLICE);
    if (mon != RT_NULL)
    {
        rt_thread_startup(mon);
    }
    return RT_EOK;
}

INIT_APP_EXPORT(rt_demo_init);

static void thread_dump(void)
{
    rt_base_t level;
    struct rt_object *obj;
    struct rt_list_node *node;
    struct rt_object_information *info;

    info = rt_object_get_information(RT_Object_Class_Thread);   /* 线程对象容器 */

    level = rt_hw_interrupt_disable();                          /* 关中断保护遍历 */
    for (node = info->object_list.next; node != &(info->object_list); node = node->next)
    {
        obj = rt_list_entry(node, struct rt_object, list);      /* 链表节点 → 对象 */
        struct rt_thread *t = (struct rt_thread *)obj;
        rt_kprintf("%-12s pri=%d stat=%s stack=%u\n",
                   t->name,
                   t->current_priority,
                   stat2str(t->stat & RT_THREAD_STAT_MASK),
                   t->stack_size);
    }
    rt_hw_interrupt_enable(level);
}
MSH_CMD_EXPORT(thread_dump, dump all threads info);

int main(void)
{
    int count = 1;

    while (count++)
    {
        // LOG_D("Hello RT-Thread!");
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}
