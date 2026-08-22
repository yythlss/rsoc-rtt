/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     RT-Thread    first version
 */

#include <rtthread.h>
#include <finsh.h>

/* 停车参数设置 */
#define CAR_COUNT           5
#define PARKING_SPACE_COUNT 3

/* 线程参数 */
#define THREAD_PRIORITY     25
#define THREAD_STACK_SIZE   1024
#define THREAD_TIMESLICE    5

/* 等待时间与停车时间 */
#define WAIT_TIME_MS        2000
#define PARK_TIME_MS        3000    //  假设停车时间一样，随机停车时间也可以用rand()

/* 停车位信号量 */
static rt_sem_t parking_sem = RT_NULL;

/* 用于等待所有车辆线程结束 */
static rt_sem_t done_sem = RT_NULL;

/* 当前剩余车位 */
static rt_int8_t remaining_spaces = PARKING_SPACE_COUNT;

/* 车辆线程句柄 */
static rt_thread_t car_threads[CAR_COUNT];

/**
 * @brief 修改剩余车位数量。
 *
 * 车辆成功获得信号量后车位减 1，
 * 车辆释放信号量后车位加 1。
 *
 * @param change 车位变化量，-1 表示占用，+1 表示释放。
 *
 * @return 修改后的剩余车位数量。
 */
static rt_int8_t parking_space_change(rt_int8_t change)
{
    rt_int8_t spaces;

    rt_enter_critical();

    remaining_spaces += change;
    spaces = remaining_spaces;

    rt_exit_critical();

    return spaces;
}

/**
 * @brief 车辆线程入口函数。
 *
 * 5 个车辆线程使用相同优先级和时间片，
  * 由 RT-Thread 调度器决定线程运行顺序。
  * 每辆车最多等待 2 秒申请车位，超时后重新申请。
 *
 * @param parameter 车辆编号。
 */
static void car_thread_entry(void *parameter)
{
    rt_uint32_t car_id = (rt_uint32_t)parameter;
    rt_err_t result;
    rt_int8_t spaces;

    while (1)
    {
        /* 申请一个停车位，成功后信号量数量减 1 */
        result = rt_sem_take(
            parking_sem,
            rt_tick_from_millisecond(WAIT_TIME_MS));

        if (result == RT_EOK)
        {
            spaces = parking_space_change(-1);

            rt_kprintf(
                "car%d entered, used spaces: %d \n",
                car_id,
                spaces);

            break;
        }

        if (result == -RT_ETIMEOUT)
        {
            rt_kprintf(
                "car%d wait timeout, retry later.\n",
                car_id);

            rt_thread_mdelay(500);
            continue;
        }

        rt_kprintf(
            "car%d take semaphore failed, error = %d.\n",
            car_id,
            result);

        rt_sem_release(done_sem);
        return;
    }

    /* 模拟车辆停车 */
    rt_thread_mdelay(PARK_TIME_MS);

    /*
     * 车辆离开时释放停车位信号量。
     * rt_sem_release() 相当于归还一个停车资源。
     */
    result = rt_sem_release(parking_sem);

    if (result == RT_EOK)
    {
        spaces = parking_space_change(1);

        rt_kprintf(
            "car%d left \n",
            car_id);
    }
    else
    {
        rt_kprintf(
            "car%d thread release semaphore failed, error = %d.\n",
            car_id,
            result);
    }

    /* 通知主函数当前车辆线程已经结束 */
    rt_sem_release(done_sem);
}

/**
 * @brief 启动停车场车位管理实验。
 *
 * 创建一个初始值为 3 的计数信号量和 5 个同优先级车辆线程。
 * 所有车辆线程以相同优先级参与调度竞争，获得信号量的车辆进入停车场。
 *
 * @return RT_EOK 实验执行成功。
 * @return -RT_ERROR 创建 IPC 或线程失败。
 */
int parking_demo(void)
{
    rt_err_t result;
    int i;
    int started_count = 0;

    remaining_spaces = PARKING_SPACE_COUNT;

    /* 创建停车位计数信号量，初始值为 3 */
    parking_sem = rt_sem_create(
        "parksem",
        PARKING_SPACE_COUNT,
        RT_IPC_FLAG_FIFO);

    if (parking_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed.\n");
        return -RT_ERROR;
    }

    /* 创建线程完成信号量 */
    done_sem = rt_sem_create(
        "parkdone",
        0,
        RT_IPC_FLAG_FIFO);

    if (done_sem == RT_NULL)
    {
        rt_kprintf("create done semaphore failed.\n");
        rt_sem_delete(parking_sem);
        parking_sem = RT_NULL;
        return -RT_ERROR;
    }

    /* 创建 5 个相同优先级的车辆线程， 由RT-thread随机决定谁先进 */
    for (i = 0; i < CAR_COUNT; i++)
    {
        car_threads[i] = rt_thread_create(
            "car",
            car_thread_entry,
            (void *)(rt_uint32_t)(i + 1),
            THREAD_STACK_SIZE,
            THREAD_PRIORITY,
            THREAD_TIMESLICE);

        if (car_threads[i] == RT_NULL)
        {
            rt_kprintf("create car %d failed.\n", i + 1);
            continue;
        }

        result = rt_thread_startup(car_threads[i]);

        if (result != RT_EOK)
        {
            rt_kprintf("startup car %d failed.\n", i + 1);
            rt_thread_delete(car_threads[i]);
            car_threads[i] = RT_NULL;
            continue;
        }

        started_count++;
    }

    /* 等待所有成功启动的车辆线程结束 */
    for (i = 0; i < started_count; i++)
    {
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    }

    /* 删除 IPC 对象 */
    rt_sem_delete(done_sem);
    rt_sem_delete(parking_sem);

    done_sem = RT_NULL;
    parking_sem = RT_NULL;

    rt_kprintf("parking demo finished.\n");

    return RT_EOK;
}

/* 导出为 MSH 命令 */
MSH_CMD_EXPORT(parking_demo, parking lot semaphore demo);
