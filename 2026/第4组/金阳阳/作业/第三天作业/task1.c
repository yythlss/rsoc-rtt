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

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ============ 停车场配置 ============ */
#define PARKING_SPACES       3     /* 车位总数 */
#define CAR_COUNT            5     /* 车辆总数 */
#define WAIT_TIMEOUT_MS      2000  /* 等待车位超时时间(ms) */
#define PARKING_TIME_MS      3000  /* 每辆车停车时间(ms) */
#define RETRY_INTERVAL_MS    500   /* 超时后重试间隔(ms) */

/* ============ 全局变量 ============ */
static rt_sem_t parking_sem = RT_NULL;   /* 车位计数信号量 */
static volatile int exercise_running = 0; /* 练习运行标志 */
static int total_entered = 0;             /* 累计进入车辆数 */
static int total_left = 0;                /* 累计离开车辆数 */

/* ============ 车辆线程参数 ============ */
typedef struct
{
    int car_id;          /* 车辆编号 (1~5) */
    rt_sem_t sem;       /* 车位信号量句柄 */
} car_arg_t;

/*
 * 车辆线程入口
 * 每辆车尝试获取车位，成功后停车一段时间再释放
 * 超时则打印等待信息后重试
 */
static void car_thread_entry(void *parameter)
{
    car_arg_t *arg = (car_arg_t *)parameter;
    int car_id = arg->car_id;
    rt_sem_t sem = arg->sem;
    rt_err_t result;
    char name[RT_NAME_MAX];

    rt_snprintf(name, sizeof(name), "car%d", car_id);
    LOG_I("%s arriving, looking for space...", name);

    while (1)
    {
        result = rt_sem_take(sem, rt_tick_from_millisecond(WAIT_TIMEOUT_MS));

        if (result == RT_EOK)
        {
            rt_enter_critical();
            total_entered++;
            int used = total_entered - total_left;
            rt_exit_critical();

            LOG_I("%s entered, used spaces: %d", name, used);
            rt_thread_mdelay(PARKING_TIME_MS);

            result = rt_sem_release(sem);
            if (result != RT_EOK)
                LOG_E("%s sem_release failed! err=%d", name, result);

            rt_enter_critical();
            total_left++;
            int all_done = (total_left >= CAR_COUNT);
            rt_exit_critical();

            LOG_I("%s left", name);

            if (all_done)
            {
                LOG_I("=== All %d cars processed. Exercise complete! ===", CAR_COUNT);
                if (parking_sem != RT_NULL)
                {
                    rt_sem_delete(parking_sem);
                    parking_sem = RT_NULL;
                    LOG_I("Semaphore released.");
                }
                exercise_running = 0;
            }

            rt_free(arg);
            return;
        }
        else if (result == -RT_ETIMEOUT)
        {
            LOG_I("%s waiting for a space...", name);
            rt_thread_mdelay(RETRY_INTERVAL_MS);
        }
        else
        {
            LOG_E("%s sem_take error: %d", name, result);
            rt_free(arg);
            return;
        }
    }
}

/*
 * 检查并清理残留的练习资源
 */
static int cleanup_residuals(void)
{
    int i;
    char tname[RT_NAME_MAX];
    rt_thread_t tid;

    for (i = 1; i <= CAR_COUNT; i++)
    {
        rt_snprintf(tname, sizeof(tname), "car%d", i);
        tid = rt_thread_find(tname);
        if (tid != RT_NULL)
        {
            LOG_W("Found residual thread '%s', deleting...", tname);
            rt_thread_delete(tid);
        }
    }

    if (parking_sem != RT_NULL)
    {
        LOG_W("Found residual semaphore, deleting...");
        rt_sem_delete(parking_sem);
        parking_sem = RT_NULL;
    }

    total_entered = 0;
    total_left = 0;
    return RT_EOK;
}

/*
 * 停车场练习主函数 - MSH命令: parking_demo
 */
static int parking_demo(int argc, char *argv[])
{
    int i, j;
    rt_thread_t thread;
    car_arg_t *arg;
    char tname[RT_NAME_MAX];

    if (exercise_running)
    {
        LOG_W("Parking exercise already running! Please wait.");
        return RT_EBUSY;
    }

    /* 先置运行标志，避免车辆线程先于标志完成 */
    exercise_running = 1;

    cleanup_residuals();

    parking_sem = rt_sem_create("parking", PARKING_SPACES, RT_IPC_FLAG_FIFO);
    if (parking_sem == RT_NULL)
    {
        LOG_E("Failed to create parking semaphore!");
        return RT_ERROR;
    }
    LOG_I("Created semaphore: %d spaces available", PARKING_SPACES);

    for (i = 1; i <= CAR_COUNT; i++)
    {
        arg = (car_arg_t *)rt_malloc(sizeof(car_arg_t));
        if (arg == RT_NULL)
        {
            LOG_E("No memory for car %d!", i);
            for (j = 1; j < i; j++)
            {
                rt_snprintf(tname, sizeof(tname), "car%d", j);
                thread = rt_thread_find(tname);
                if (thread) rt_thread_delete(thread);
            }
            rt_sem_delete(parking_sem);
            parking_sem = RT_NULL;
            return RT_ERROR;
        }
        arg->car_id = i;
        arg->sem = parking_sem;

        rt_snprintf(tname, sizeof(tname), "car%d", i);
        thread = rt_thread_create(tname, car_thread_entry, arg, 512, 15, 10);
        if (thread == RT_NULL)
        {
            LOG_E("Failed to create thread for car%d!", i);
            rt_free(arg);
            for (j = 1; j < i; j++)
            {
                rt_snprintf(tname, sizeof(tname), "car%d", j);
                thread = rt_thread_find(tname);
                if (thread) rt_thread_delete(thread);
            }
            rt_sem_delete(parking_sem);
            parking_sem = RT_NULL;
            return RT_ERROR;
        }

        if (rt_thread_startup(thread) != RT_EOK)
        {
            LOG_E("Failed to start thread for car%d!", i);
            rt_thread_delete(thread);
            rt_free(arg);
            for (j = 1; j < i; j++)
            {
                rt_snprintf(tname, sizeof(tname), "car%d", j);
                thread = rt_thread_find(tname);
                if (thread) rt_thread_delete(thread);
            }
            rt_sem_delete(parking_sem);
            parking_sem = RT_NULL;
            return RT_ERROR;
        }
        LOG_I("Car %d ready ('%s' started)", i, tname);
    }

    LOG_I("All %d cars competing for %d spaces...", CAR_COUNT, PARKING_SPACES);
    return RT_EOK;
}

MSH_CMD_EXPORT(parking_demo, Parking lot semaphore exercise);

int main(void)
{
    LOG_I("Parking Lot Exercise - type 'parking_demo' in MSH");
    return RT_EOK;
}
