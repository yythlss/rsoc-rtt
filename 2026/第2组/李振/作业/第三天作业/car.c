/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     user         parking semaphore demo
 */

#include <rtthread.h>

#define THREAD_PRIORITY         25
#define THREAD_TIMESLICE        5
#define CAR_COUNT               5
#define PARK_SPACE_NUM          3
#define PARK_WAIT_TIME          (RT_TICK_PER_SECOND * 2)
/*新增：车辆到达错峰间隔*/
#define ARRIVAL_GAP_MS          300

/* 计数信号量：车位 */
static rt_sem_t park_sem = RT_NULL;
/* 互斥锁保护已占用车位计数 */
static rt_mutex_t space_mtx = RT_NULL;
static rt_uint8_t used_spaces = 0;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char car_stack[CAR_COUNT][1024];
static struct rt_thread car_thread[CAR_COUNT];

static void car_thread_entry(void *parameter)
{
    rt_int32_t car_id = (rt_int32_t)parameter;
    char car_name[16];
    rt_err_t ret;

    rt_sprintf(car_name, "car%d", car_id);
    rt_thread_mdelay((car_id - 1) * ARRIVAL_GAP_MS);

    while(1)
    {
        ret = rt_sem_take(park_sem, PARK_WAIT_TIME);
        if(ret == RT_EOK)
        {
            rt_mutex_take(space_mtx, RT_WAITING_FOREVER);
            used_spaces ++;
            rt_uint8_t cur_use = used_spaces;
            rt_mutex_release(space_mtx);

            rt_kprintf("%s entered, used spaces: %d\n", car_name, cur_use);

            rt_thread_mdelay(2000);

            rt_mutex_take(space_mtx, RT_WAITING_FOREVER);
            used_spaces --;
            rt_mutex_release(space_mtx);
            rt_sem_release(park_sem);

            rt_kprintf("%s left\n", car_name);
            break;
        }
        else
        {
            rt_kprintf("%s waiting for a space\n", car_name);
            rt_thread_mdelay(500);
        }
    }
}

int parking_sample(void)
{
    rt_err_t res;
    int i;

    park_sem = rt_sem_create("park_sem", PARK_SPACE_NUM, RT_IPC_FLAG_PRIO);
    if (park_sem == RT_NULL)
    {
        rt_kprintf("create park semaphore failed.\n");
        return -1;
    }

    space_mtx = rt_mutex_create("space_mtx", RT_IPC_FLAG_PRIO);
    if (space_mtx == RT_NULL)
    {
        rt_kprintf("create mutex failed.\n");
        rt_sem_delete(park_sem);
        return -1;
    }
    used_spaces = 0;

    for(i = 0; i < CAR_COUNT; i++)
    {
        res = rt_thread_init(&car_thread[i],
                              RT_NULL,
                              car_thread_entry,
                              (void *)(i + 1),
                              &car_stack[i][0],
                              sizeof(car_stack[i]),
                              THREAD_PRIORITY,
                              THREAD_TIMESLICE);
#ifdef RT_USING_SMP
        rt_thread_control(&car_thread[i], RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
        if(res == RT_EOK)
        {
            rt_thread_startup(&car_thread[i]);
        }
    }

    return 0;
}

MSH_CMD_EXPORT(parking_sample, parking semaphore sample);
