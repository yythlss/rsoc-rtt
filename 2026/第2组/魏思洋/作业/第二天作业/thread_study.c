/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-08-24     yangjie      the first version
 */

/*
 * 程序清单：创建、初始化/脱离线程
 *
 * 这个例子会创建两个线程，一个动态线程，一个静态线程。
 * 静态线程在运行完毕后自动被系统脱离，动态线程一直打印计数。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>    //如果不包含，可能会遇到错误提示没有'F'定义

#define LED_B        GET_PIN(F,  11)
#define LED_R        GET_PIN(F,  12)

#define THREAD_PRIORITY         16
#define THREAD_STACK_SIZE       1024
#define THREAD_TIMESLICE        5

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char thread2_stack[1024];
static struct rt_thread th_led2;

static rt_thread_t th_led1,th_monitor = RT_NULL;


/* 线程1的入口函数 */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        /* 线程1采用16优先级运行，一直打印计数值 */
        rt_pin_write(LED_R, !rt_pin_read(LED_R));
        rt_kprintf("th_led1 count and tick: %d and %d \n", count++, rt_tick_get());
        rt_thread_mdelay(500);
    }
}

/* 线程3的入口函数 */
static void th_monitor_entry(void *parameter)
{
    while (1)
    {
        rt_uint8_t stat1 = 0;
        rt_uint8_t stat2 = 0;

#ifdef RT_USING_FINSH
        if (th_led1)
        {
            stat1 = th_led1->stat;
        }

        stat2 = th_led2.stat;
#endif

        rt_kprintf("\n========== THREAD MONITOR ==========\n");

        rt_kprintf("th_led1: state=%d\n", stat1);
        rt_kprintf("th_led2: state=%d\n", stat2);

        rt_kprintf("====================================\n\n");

        rt_thread_mdelay(2000);
    }
}

/* 线程2入口 */
static void th_led2_entry(void *param)
{
    rt_uint32_t count = 0;

    /* 线程2采用16优先级，一直打印计数值 */
    while(1)
    {
        /* 线程2打印计数值 */
        rt_pin_write(LED_B, !rt_pin_read(LED_B));
        rt_kprintf("th_led2 count and tick: %d and %d \n", count++, rt_tick_get());
        rt_thread_mdelay(500);
    }
}

/* 线程示例 */
int thread_dump(void)
{
    /* 创建线程1，名称是thread1，入口是thread1_entry*/
    th_led1 = rt_thread_create("th_led1",
                            th_led1_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    /* 创建线程3，名称是th_monitor，入口是th_monitor*/
    th_monitor = rt_thread_create("th_monitor",
                            th_monitor_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY - 6, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(tid1, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    /* 如果获得线程控制块，启动这个线程 */
    if (th_led1 != RT_NULL)
        rt_thread_startup(th_led1);
    if (th_monitor != RT_NULL)
            rt_thread_startup(th_monitor);

    /* 初始化线程2，名称是thread2，入口是thread2_entry */
    rt_thread_init(&th_led2,
                   "th_led2",
                   th_led2_entry,
                   RT_NULL,
                   &thread2_stack[0],
                   sizeof(thread2_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread2, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&th_led2);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(thread_dump, thread_dump);
//INIT_APP_EXPORT(thread_dump);
