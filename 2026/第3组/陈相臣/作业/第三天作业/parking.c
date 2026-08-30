/*
 * 练习一：停车场车位管理
 * 目标板：星火一号，主控芯片：STM32F407ZG
 *
 * 作业要求：
 * - 计数信号量初始值为 3，表示剩余车位
 * - 创建 5 个车辆线程，每辆车尝试获取车位
 * - 获取失败且 2 秒超时则打印等待信息并重试
 * - 任意时刻占用车位不超过 3 个
 */

#include <rtthread.h>

#define CAR_COUNT       5
#define PARKING_SPACES  3
#define THREAD_STACK    1024
#define THREAD_PRIORITY 25

static rt_sem_t parking_sem;
static rt_mutex_t count_mutex;
static rt_sem_t done_sem;
static rt_uint32_t used_spaces;
static rt_uint8_t running;

/* 车辆线程：获得车位后进入，停留一段时间再离开 */
static void car_entry(void *parameter)
{
    rt_uint32_t car_id = (rt_uint32_t)parameter;

    rt_thread_mdelay((car_id - 1U) * 50U);

    while (1)
    {
        rt_err_t result = rt_sem_take(parking_sem, RT_TICK_PER_SECOND * 2);

        if (result != RT_EOK)
        {
            if (result == -RT_ETIMEOUT)
            {
                rt_kprintf("car%u waiting for a space\n", car_id);
                continue;
            }
            break;
        }

        rt_mutex_take(count_mutex, RT_WAITING_FOREVER);
        used_spaces++;
        rt_kprintf("car%u entered, used spaces: %u\n", car_id, used_spaces);
        rt_mutex_release(count_mutex);

        rt_thread_mdelay(2500U + car_id * 50U);

        rt_mutex_take(count_mutex, RT_WAITING_FOREVER);
        used_spaces--;
        rt_kprintf("car%u left\n", car_id);
        rt_mutex_release(count_mutex);

        rt_sem_release(parking_sem);
        break;
    }

    rt_sem_release(done_sem);
}

/* MSH 命令：parking_demo */
static int parking_demo(int argc, char **argv)
{
    rt_uint32_t i;
    rt_uint32_t started = 0;
    rt_thread_t tid;

    (void)argc;
    (void)argv;

    if (running)
    {
        rt_kprintf("parking_demo is already running\n");
        return -RT_EBUSY;
    }

    running = 1;
    used_spaces = 0;

    parking_sem = rt_sem_create("pk_sem", PARKING_SPACES, RT_IPC_FLAG_FIFO);
    count_mutex = rt_mutex_create("pk_cnt", RT_IPC_FLAG_FIFO);
    done_sem = rt_sem_create("pk_done", 0, RT_IPC_FLAG_FIFO);
    if ((parking_sem == RT_NULL) || (count_mutex == RT_NULL) ||
        (done_sem == RT_NULL))
    {
        rt_kprintf("create parking IPC failed\n");
        goto cleanup;
    }

    for (i = 1; i <= CAR_COUNT; i++)
    {
        char name[RT_NAME_MAX];

        rt_snprintf(name, sizeof(name), "car%u", (unsigned int)i);
        tid = rt_thread_create(name, car_entry, (void *)i,
                               THREAD_STACK, THREAD_PRIORITY, 5);
        if (tid == RT_NULL)
        {
            rt_kprintf("create car%u thread failed\n", i);
            continue;
        }

        if (rt_thread_startup(tid) != RT_EOK)
        {
            rt_kprintf("start car%u thread failed\n", i);
            rt_thread_delete(tid);
            continue;
        }

        started++;
    }

    for (i = 0; i < started; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);

cleanup:
    if (parking_sem != RT_NULL)
        rt_sem_delete(parking_sem);
    if (count_mutex != RT_NULL)
        rt_mutex_delete(count_mutex);
    if (done_sem != RT_NULL)
        rt_sem_delete(done_sem);

    parking_sem = RT_NULL;
    count_mutex = RT_NULL;
    done_sem = RT_NULL;
    running = 0;

    return 0;
}

MSH_CMD_EXPORT(parking_demo, parking lot demo);
