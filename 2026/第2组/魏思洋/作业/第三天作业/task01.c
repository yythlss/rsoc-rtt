/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     Vincent       the first version
 */

#include <rtthread.h>

#define THREAD_PRIORITY         25
#define THREAD_STACK_SIZE       1*1024
#define THREAD_TIMESLICE        5

/* 指向信号量的指针 */
static rt_sem_t dynamic_sem = RT_NULL;
static rt_thread_t tid1, tid2, tid3, tid4, tid5 = RT_NULL;

/* 当前已用车位数 */
static volatile rt_uint8_t used_spaces = 0;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif

static void car1_entry(void *parameter)
{
    rt_uint8_t car_id = 1;
    while (1)
    {
        /* 申请车位，2秒超时 */
        rt_err_t result = rt_sem_take(dynamic_sem, rt_tick_from_millisecond(2000));

        if (result == RT_EOK)
        {
            /* 获得车位 */
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, used_spaces);

            /* 停留 3~5 秒（模拟） */
            rt_thread_mdelay(3000);

            /* 离开，释放车位 */
            used_spaces--;
            rt_kprintf("car%d left\n", car_id);
            rt_sem_release(dynamic_sem);
        }
        else
        {
            /* 超时 */
            rt_kprintf("car%d waiting for a space\n", car_id);
            rt_thread_mdelay(500);  // 稍后再申请
        }
    }
}

static void car2_entry(void *parameter)
{
    rt_uint8_t car_id = 2;
    while (1)
    {
        /* 申请车位，2秒超时 */
        rt_err_t result = rt_sem_take(dynamic_sem, rt_tick_from_millisecond(2000));

        if (result == RT_EOK)
        {
            /* 获得车位 */
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, used_spaces);

            /* 停留 3~5 秒（模拟） */
            rt_thread_mdelay(3000);

            /* 离开，释放车位 */
            used_spaces--;
            rt_kprintf("car%d left\n", car_id);
            rt_sem_release(dynamic_sem);
        }
        else
        {
            /* 超时 */
            rt_kprintf("car%d waiting for a space\n", car_id);
            rt_thread_mdelay(500);  // 稍后再申请
        }
    }
}

static void car3_entry(void *parameter)
{
    rt_uint8_t car_id = 3;
    while (1)
    {
        /* 申请车位，2秒超时 */
        rt_err_t result = rt_sem_take(dynamic_sem, rt_tick_from_millisecond(2000));

        if (result == RT_EOK)
        {
            /* 获得车位 */
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, used_spaces);

            /* 停留 3~5 秒（模拟） */
            rt_thread_mdelay(3000);

            /* 离开，释放车位 */
            used_spaces--;
            rt_kprintf("car%d left\n", car_id);
            rt_sem_release(dynamic_sem);
        }
        else
        {
            /* 超时 */
            rt_kprintf("car%d waiting for a space\n", car_id);
            rt_thread_mdelay(500);  // 稍后再申请
        }
    }
}

static void car4_entry(void *parameter)
{
    rt_uint8_t car_id = 4;
    while (1)
    {
        /* 申请车位，2秒超时 */
        rt_err_t result = rt_sem_take(dynamic_sem, rt_tick_from_millisecond(2000));

        if (result == RT_EOK)
        {
            /* 获得车位 */
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, used_spaces);

            /* 停留 3~5 秒（模拟） */
            HAL_Delay(3000);

            /* 离开，释放车位 */
            used_spaces--;
            rt_kprintf("car%d left\n", car_id);
            rt_sem_release(dynamic_sem);
        }
        else
        {
            /* 超时 */
            rt_kprintf("car%d waiting for a space\n", car_id);
            rt_thread_mdelay(500);  // 稍后再申请
        }
    }
}

static void car5_entry(void *parameter)
{
    rt_uint8_t car_id = 5;
    while (1)
    {
        /* 申请车位，2秒超时 */
        rt_err_t result = rt_sem_take(dynamic_sem, rt_tick_from_millisecond(2000));

        if (result == RT_EOK)
        {
            /* 获得车位 */
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, used_spaces);

            /* 停留 3~5 秒（模拟） */
            rt_thread_mdelay(3000);

            /* 离开，释放车位 */
            used_spaces--;
            rt_kprintf("car%d left\n", car_id);
            rt_sem_release(dynamic_sem);
        }
        else
        {
            /* 超时 */
            rt_kprintf("car%d waiting for a space\n", car_id);
            rt_thread_mdelay(500);  // 稍后再申请
        }
    }
}

/* 信号量示例的初始化 */
int parking_demo()
{
    /* 创建一个动态信号量，初始值是0 */
    dynamic_sem = rt_sem_create("car_sem", 3, RT_IPC_FLAG_PRIO);
    if (dynamic_sem == RT_NULL)
    {
        rt_kprintf("create car semaphore failed.\n");
        return -1;
    }
    else
    {
        rt_kprintf("create done. used spaces = %d\r\n",used_spaces);
    }
    /* 动态创建五个线程，用来管理车辆 */
    tid1 = rt_thread_create("car1",
                            car1_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    tid2 = rt_thread_create("car2",
                            car2_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY+1, THREAD_TIMESLICE);
    tid3 = rt_thread_create("car3",
                            car3_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY+2, THREAD_TIMESLICE);
    tid4 = rt_thread_create("car4",
                            car4_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY+3, THREAD_TIMESLICE);
    tid5 = rt_thread_create("car5",
                            car5_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY+4, THREAD_TIMESLICE);

    if (tid1 != RT_NULL)
            rt_thread_startup(tid1);
    if (tid2 != RT_NULL)
            rt_thread_startup(tid2);
    if (tid3 != RT_NULL)
            rt_thread_startup(tid3);
    if (tid4 != RT_NULL)
            rt_thread_startup(tid4);
    if (tid5 != RT_NULL)
            rt_thread_startup(tid5);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(parking_demo, parking demo);

