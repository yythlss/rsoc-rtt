/*
 * 练习一：停车场车位管理（计数信号量）
 *
 * 场景：停车场只有 3 个车位，5 辆车尝试进入。
 * 使用一个初始值为 3 的计数信号量管理有限的车位资源；
 * 每辆车线程申请车位（2 秒超时），获得车位后进入、停留一段时间后离开并释放车位。
 * 任意时刻进入停车场的车辆数由信号量保证不超过 3。
 *
 * MSH 命令：parking_demo
 *
 * 思考题：为什么使用计数信号量而不是互斥量？
 * 答：互斥量是"谁持有谁释放"的排他锁，只允许一个持有者；
 *     而车位是允许多个（最多 3 个）持有者的有限资源，计数信号量的计数值
 *     正好表示"剩余资源数量"，适用于资源池/容量受限的场景。
 */

#include <rtthread.h>

#define PARK_SPACES          3    /* 车位总数 */
#define PARK_CAR_NUM         5    /* 车辆数量 */
#define PARK_WAIT_TICKS      2000 /* 申请车位超时时间（tick，1 tick = 1 ms） */
#define PARK_RETRY_TICKS     500  /* 超时后重新申请前的等待时间 */
#define PARK_STAY_TICKS      1500 /* 车辆在停车场内的停留时间 */

static rt_sem_t s_park_sem = RT_NULL;   /* 车位信号量，初值 = 3 */
static rt_sem_t s_done_sem = RT_NULL;   /* 车辆完成计数信号量，用于命令线程等待 */
static volatile rt_uint8_t s_used = 0;  /* 当前占用车位数（仅用于显示） */
static volatile rt_bool_t s_running = RT_FALSE; /* 防重复启动 */

/* 车辆线程：申请车位 -> 进入 -> 停留 -> 离开 -> 释放车位 */
static void car_thread(void *param)
{
    rt_ubase_t id = (rt_ubase_t)param;

    while (RT_TRUE)
    {
        /* 申请车位，2 秒内未获得则超时 */
        if (rt_sem_take(s_park_sem, PARK_WAIT_TICKS) != RT_EOK)
        {
            rt_kprintf("car%d waiting for a space\n", id);
            rt_thread_mdelay(PARK_RETRY_TICKS);
            continue;
        }

        /* 获得车位，进入停车场 */
        rt_enter_critical();
        s_used++;
        rt_ubase_t used = s_used;
        rt_exit_critical();
        rt_kprintf("car%d entered, used spaces: %d\n", id, used);

        /* 停留一段时间 */
        rt_thread_mdelay(PARK_STAY_TICKS);

        /* 离开停车场，释放车位 */
        rt_kprintf("car%d left\n", id);
        rt_enter_critical();
        s_used--;
        rt_exit_critical();
        rt_sem_release(s_park_sem);
        break;
    }

    /* 通知命令线程：本车已结束 */
    rt_sem_release(s_done_sem);
}

/* MSH 命令：parking_demo */
static int parking_demo(int argc, char *argv[])
{
    rt_thread_t cars[PARK_CAR_NUM] = {RT_NULL};
    rt_uint8_t i;
    rt_err_t ret = RT_EOK;

    if (s_running)
    {
        rt_kprintf("parking_demo is already running\n");
        return -1;
    }
    s_running = RT_TRUE;
    s_used = 0;

    /* 1. 创建初始值为 3 的计数信号量 */
    s_park_sem = rt_sem_create("park_sem", PARK_SPACES, RT_IPC_FLAG_FIFO);
    if (s_park_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed\n");
        s_running = RT_FALSE;
        return -1;
    }

    /* 2. 创建完成计数信号量 */
    s_done_sem = rt_sem_create("pk_done", 0, RT_IPC_FLAG_FIFO);
    if (s_done_sem == RT_NULL)
    {
        rt_kprintf("create done semaphore failed\n");
        rt_sem_delete(s_park_sem);
        s_park_sem = RT_NULL;
        s_running = RT_FALSE;
        return -1;
    }

    /* 3. 创建 5 个车辆线程并检查创建结果 */
    for (i = 0; i < PARK_CAR_NUM; i++)
    {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "car%d", i + 1);
        cars[i] = rt_thread_create(name, car_thread, (void *)(rt_ubase_t)(i + 1),
                                   1024, 8, 10);
        if (cars[i] == RT_NULL)
        {
            rt_kprintf("create thread %s failed\n", name);
            ret = -RT_ENOMEM;
            break;
        }
    }

    if (ret == RT_EOK)
    {
        /* 4. 启动所有车辆线程 */
        for (i = 0; i < PARK_CAR_NUM; i++)
        {
            rt_thread_startup(cars[i]);
        }

        /* 5. 等待全部车辆完成（线程结束后由 idle 线程自动回收） */
        for (i = 0; i < PARK_CAR_NUM; i++)
        {
            if (rt_sem_take(s_done_sem, 20000) != RT_EOK)
            {
                rt_kprintf("parking_demo wait timeout\n");
                break;
            }
        }
    }
    else
    {
        /* 创建失败：回收已创建的线程与信号量 */
        for (i = 0; i < PARK_CAR_NUM; i++)
        {
            if (cars[i] != RT_NULL)
            {
                rt_thread_delete(cars[i]);
            }
        }
    }

    rt_sem_delete(s_park_sem);
    rt_sem_delete(s_done_sem);
    s_park_sem = RT_NULL;
    s_done_sem = RT_NULL;
    rt_kprintf("parking_demo finished\n");
    s_running = RT_FALSE;

    return ret;
}
MSH_CMD_EXPORT(parking_demo, parking lot demo with counting semaphore);
