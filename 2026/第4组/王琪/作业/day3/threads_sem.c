/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-24     wangqi       the first version
 */
#ifndef APPLICATIONS_THREADS_SEM_C_
#define APPLICATIONS_THREADS_SEM_C_

#include <rtthread.h>

#define PARKING_CAPACITY    3           /* 车位总数 */
#define CAR_COUNT           5           /* 车辆总数 */
#define PARKING_STAY_TIME   (3 * RT_TICK_PER_SECOND)  /* 停车停留时间 3秒 */
#define WAIT_TIMEOUT        (2 * RT_TICK_PER_SECOND)  /* 等待超时时间 2秒 */

static rt_sem_t parking_sem = RT_NULL;

/* 车辆线程入口函数 */
static void car_thread_entry(void *parameter)
{
    int car_id = (int)(rt_uint32_t)parameter;
    rt_err_t result;

    while (1)
    {
        /* 尝试获取车位，超时时间 2 秒 */
        result = rt_sem_take(parking_sem, WAIT_TIMEOUT);

        if (result == RT_EOK)
        {
            /* 成功获得车位 */
            rt_kprintf("car%d entered, used spaces: %d\n",
                       car_id, PARKING_CAPACITY - parking_sem->value);

            /* 模拟停车停留一段时间 */
            rt_thread_mdelay(PARKING_STAY_TIME / RT_TICK_PER_SECOND * 1000);

            /* 离开停车场，释放车位 */
            rt_kprintf("car%d left\n", car_id);
            rt_sem_release(parking_sem);

            break;  /* 本次停车结束，退出线程 */
        }
        else
        {
            /* 等待超时，未获得车位 */
            rt_kprintf("car%d waiting for a space\n", car_id);
            rt_thread_mdelay(500);  /* 稍等片刻后重新申请 */
        }
    }
}

/* 初始化函数 */
static int parking_test_init(void)
{
    int i;
    char thread_name[10];
    rt_thread_t tid;

    /* 创建计数信号量，初始值为车位数量 3 */
    parking_sem = rt_sem_create("parking", PARKING_CAPACITY, RT_IPC_FLAG_PRIO);
    if (parking_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed!\n");
        return -1;
    }

    /* 创建 5 个车辆线程 */
    for (i = 0; i < CAR_COUNT; i++)
    {
        rt_snprintf(thread_name, sizeof(thread_name), "car%d", i + 1);
        tid = rt_thread_create(thread_name,
                               car_thread_entry,
                               (void *)(rt_uint32_t)(i + 1),
                               1024,
                               20,
                               10);
        if (tid != RT_NULL)
            rt_thread_startup(tid);
    }

    return 0;
}
/* 导出到 msh 命令行，输入 parking_test 即可运行 */
//MSH_CMD_EXPORT(parking_test_init, parking lot test);
MSH_CMD_EXPORT_ALIAS(parking_test_init, parking_test, parking lot test);

#endif /* APPLICATIONS_THREADS_SEM_C_ */
