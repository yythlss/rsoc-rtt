/*
 * parking_demo.c - 练习一：停车场车位管理（计数信号量）
 *
 * 场景：停车场只有 3 个车位，同时有 5 辆车尝试进入。
 * 实现：初始值为 3 的计数信号量表示剩余车位，车辆 take 到车位才能进入，
 *       停留一段时间后离开并 release 车位；2 秒内没拿到车位的车辆打印
 *       超时信息后重新申请。任意时刻在场车辆不超过 3 辆。
 *
 * 用法：MSH 中输入 parking_demo 启动。
 */
#include <rtthread.h>

#define PARKING_SPACES  3
#define CAR_COUNT       5

static rt_sem_t parking_sem = RT_NULL;

static void car_entry(void *param)
{
    int car_id = (int)(rt_ubase_t)param;
    rt_err_t ret;

    while (1)
    {
        ret = rt_sem_take(parking_sem, rt_tick_from_millisecond(2000));
        if (ret == RT_EOK)
        {
            rt_kprintf("car%d entered, used spaces: %d\n",
                       car_id, PARKING_SPACES - parking_sem->value);
            rt_thread_mdelay(3000 + car_id * 500);
            rt_sem_release(parking_sem);
            rt_kprintf("car%d left\n", car_id);
            break;
        }
        else
        {
            rt_kprintf("car%d waiting for a space\n", car_id);
        }
    }
}

static void parking_demo(int argc, char **argv)
{
    int i;

    if (parking_sem != RT_NULL)
    {
        rt_kprintf("parking demo is already running\n");
        return;
    }

    parking_sem = rt_sem_create("psem", PARKING_SPACES, RT_IPC_FLAG_PRIO);
    if (parking_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed\n");
        return;
    }

    for (i = 1; i <= CAR_COUNT; i++)
    {
        char name[RT_NAME_MAX];
        rt_thread_t tid;

        rt_snprintf(name, sizeof(name), "car%d", i);
        tid = rt_thread_create(name, car_entry, (void *)(rt_ubase_t)i, 1024, 20, 5);
        if (tid == RT_NULL)
        {
            rt_kprintf("create %s failed\n", name);
            continue;
        }
        rt_thread_startup(tid);
    }
}
MSH_CMD_EXPORT(parking_demo, parking lot demo with counting semaphore);
