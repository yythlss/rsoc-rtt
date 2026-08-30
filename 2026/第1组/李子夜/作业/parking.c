/*
 * 【RSOC26】第3天作业 - 练习一：停车场车位管理
 *
 * 场景：停车场 3 个车位，5 辆车尝试进入。
 * 使用计数信号量管理有限资源，超时车辆退出等待后重新申请。
 */

#include <rtthread.h>

#define PARKING_SPACES  3
#define CAR_COUNT       5
#define PARK_TIMEOUT    2000   /* 等车位的超时时间（ms） */
#define PARK_DURATION   3000   /* 每辆车停留时间（ms） */

/* 当前已用车位数（仅用于打印，不做调度用） */
static rt_atomic_t used_spaces = 0;

/*
 * 车辆线程：尝试获取车位 → 停留 → 释放车位
 */
static void car_thread_entry(void *param)
{
    rt_uint32_t car_id = (rt_uint32_t)(rt_uintptr_t)param;
    char thread_name[16];

    rt_snprintf(thread_name, sizeof(thread_name), "car%d", car_id);

    /* 申请车位（超时时间 2 秒） */
    if (rt_sem_take(parking_sem, PARK_TIMEOUT) == RT_EOK)
    {
        rt_atomic_add(&used_spaces, 1);
        rt_kprintf("%s entered, used spaces: %d\n",
                   thread_name, rt_atomic_load(&used_spaces));

        rt_thread_mdelay(PARK_DURATION);

        rt_atomic_sub(&used_spaces, 1);
        rt_kprintf("%s left, remaining spaces: %d\n",
                   thread_name, PARKING_SPACES - rt_atomic_load(&used_spaces));

        rt_sem_release(parking_sem);
    }
    else
    {
        rt_kprintf("%s waiting for a space timeout, will retry\n", thread_name);

        /* 等待一会再重试 */
        rt_thread_mdelay(500);

        if (rt_sem_take(parking_sem, PARK_TIMEOUT) == RT_EOK)
        {
            rt_atomic_add(&used_spaces, 1);
            rt_kprintf("%s entered (retry), used spaces: %d\n",
                       thread_name, rt_atomic_load(&used_spaces));

            rt_thread_mdelay(PARK_DURATION);

            rt_atomic_sub(&used_spaces, 1);
            rt_kprintf("%s left, remaining spaces: %d\n",
                       thread_name, PARKING_SPACES - rt_atomic_load(&used_spaces));

            rt_sem_release(parking_sem);
        }
        else
        {
            rt_kprintf("%s gave up\n", thread_name);
        }
    }
}

/* 信号量句柄 */
static rt_sem_t parking_sem = RT_NULL;

/*
 * MSH 入口函数
 */
static void parking_demo(int argc, char **argv)
{
    rt_thread_t tid[CAR_COUNT];
    char name[16];
    int i;

    /* 防止重复执行 */
    if (parking_sem != RT_NULL)
    {
        rt_kprintf("parking_demo is already running\n");
        return;
    }

    /* 创建计数信号量，初始值 = 3 */
    parking_sem = rt_sem_create("park_sem", PARKING_SPACES, RT_IPC_FLAG_FIFO);
    if (parking_sem == RT_NULL)
    {
        rt_kprintf("Failed to create parking semaphore\n");
        return;
    }

    rt_kprintf("=== Parking Demo: %d spaces, %d cars ===\n", PARKING_SPACES, CAR_COUNT);

    /* 创建 5 个车辆线程 */
    for (i = 0; i < CAR_COUNT; i++)
    {
        rt_snprintf(name, sizeof(name), "car%d", i + 1);
        tid[i] = rt_thread_create(name,
                                  car_thread_entry,
                                  (void *)(rt_uintptr_t)(i + 1),
                                  1024,
                                  20,
                                  20);
        if (tid[i] != RT_NULL)
        {
            rt_thread_startup(tid[i]);
        }
        else
        {
            rt_kprintf("Failed to create car%d thread\n", i + 1);
        }
    }
}

/* 导出到 MSH */
MSH_CMD_EXPORT(parking_demo, parking lot semaphore demo);

/*
 * 思考题答案（编码进注释）：
 *
 * Q: 为什么使用计数信号量而不是互斥量？
 *
 * A:
 * 1. 资源数量 > 1：3 个车位是「可同时被多个消费者占用」的资源，
 *    互斥量用于「同一时刻最多一个持有者」的串行化访问。
 * 2. 计数信号量的值域是 [0, 3]，直观表示「剩余车位数」。
 * 3. 互斥量有「优先级继承」机制，防优先级反转——但这里不存在调用者
 *    持有后再去调 get 的场景（不会嵌套），所以不需要。
 * 4. 用互斥量 + 自定义计数器也能模拟，但不会比直接用计数信号量
 *    更简单、更安全。
 */
