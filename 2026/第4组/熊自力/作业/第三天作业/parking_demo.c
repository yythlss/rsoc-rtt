/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     godbear       练习一：停车场车位管理（计数信号量）
 */

#include <rtthread.h>
#include <finsh.h>

#define DBG_TAG "parking"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define PARK_SPACES        3                       /* 车位总数 */
#define PARK_CAR_NUM       5                       /* 车辆数 */
#define PARK_WAIT_MS       2000                    /* 申请车位超时时间(ms) */
#define PARK_STAY_MS       2500                    /* 车辆停留时间(ms) */

static rt_sem_t park_sem = RT_NULL;
static volatile rt_uint8_t park_running = 0;       /* 任务是否运行中 */
static volatile rt_uint8_t park_entered = 0;       /* 当前在场车辆数 */
static volatile rt_uint8_t park_finished = 0;      /* 已离场车辆数 */
static volatile rt_uint8_t park_created = 0;       /* 实际创建的车辆线程数 */

/* 车辆线程：申请车位 -> 停留 -> 离开并释放车位 */
static void parking_car_entry(void *param)
{
    rt_uint8_t id = (rt_uint8_t)(rt_size_t)param;
    rt_err_t result;

    while (1)
    {
        /* 带 2 秒超时申请车位 */
        result = rt_sem_take(park_sem, rt_tick_from_millisecond(PARK_WAIT_MS));
        if (result == RT_EOK)
        {
            /* 获得车位，进入停车场 */
            rt_enter_critical();
            park_entered++;
            rt_uint8_t used = park_entered;
            rt_exit_critical();
            rt_kprintf("car%d entered, used spaces: %d\n", id, used);

            /* 停留一段时间 */
            rt_thread_mdelay(rt_tick_from_millisecond(PARK_STAY_MS));

            /* 离开并释放车位 */
            rt_enter_critical();
            park_entered--;
            rt_exit_critical();
            rt_kprintf("car%d left\n", id);
            rt_sem_release(park_sem);
            break;
        }
        else
        {
            /* 2 秒内未获得车位，打印等待超时信息，稍后重新申请 */
            rt_kprintf("car%d waiting for a space\n", id);
            rt_thread_mdelay(rt_tick_from_millisecond(100));
        }
    }

    /* 所有车辆离场后清理资源，保证命令可重复执行 */
    rt_enter_critical();
    park_finished++;
    if (park_finished >= park_created)
    {
        park_finished = 0;
        park_entered = 0;
        park_created = 0;
        park_running = 0;
        rt_exit_critical();

        rt_kprintf("all cars have parked, used spaces: 0\n");
        if (park_sem)
        {
            rt_sem_delete(park_sem);
            park_sem = RT_NULL;
        }
    }
    else
    {
        rt_exit_critical();
    }
}

static int parking_demo(int argc, char *argv[])
{
    rt_uint8_t i;

    if (park_running)
    {
        rt_kprintf("parking_demo is already running, please wait...\n");
        return -RT_EBUSY;
    }

    /* 创建初始值为 3 的计数信号量 */
    park_sem = rt_sem_create("park_sem", PARK_SPACES, RT_IPC_FLAG_PRIO);
    if (park_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed\n");
        return -RT_ENOMEM;
    }

    park_running = 1;
    park_entered = 0;
    park_finished = 0;
    park_created = 0;

    /* 创建 5 个车辆线程 */
    for (i = 1; i <= PARK_CAR_NUM; i++)
    {
        char name[RT_NAME_MAX];
        rt_thread_t tid;

        rt_snprintf(name, sizeof(name), "pcar%d", i);
        tid = rt_thread_create(name, parking_car_entry, (void *)(rt_size_t)i,
                               1024, RT_THREAD_PRIORITY_MAX - 3, 10);
        if (tid == RT_NULL)
        {
            rt_kprintf("create car thread %s failed\n", name);
            continue;
        }
        rt_thread_startup(tid);
        park_created++;
    }

    if (park_created == 0)
    {
        park_running = 0;
        rt_sem_delete(park_sem);
        park_sem = RT_NULL;
        return -RT_ERROR;
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, parking lot demo with counting semaphore);
