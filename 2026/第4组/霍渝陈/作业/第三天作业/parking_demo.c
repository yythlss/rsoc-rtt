/*
 * ============================================================
 * 练习一：停车场车位管理（计数信号量）
 *
 * 场景：停车场只有 3 个车位，同时有 5 辆车尝试进入。
 *       车位是有限资源，车辆必须先申请到车位才能进入。
 *
 * 设计思路：
 *   1. 计数信号量初始值 = 3（剩余车位）
 *   2. 每辆车 = 一个线程，进入前 rt_sem_take() 申请一个车位
 *   3. 申请成功 → 打印进入信息 → 停车 3 秒 → 离开时 rt_sem_release() 释放车位
 *   4. 2 秒内申请不到 → 打印 waiting 信息，等 1 秒后重新申请
 *   5. 用全局变量 used_spaces 记录当前已占用车位（take 成功才 +1，
 *      此时持有信号量，其他车进不来，所以计数是安全的）
 *
 * MSH 命令：parking_demo   （可重复执行，运行时重复输入会被拒绝）
 * ============================================================
 */

#include <rtthread.h>
#include <rtdevice.h>

#define PARKING_SPACES  3                               /* 车位总数 */
#define CAR_COUNT       5                               /* 车辆数量 */
#define CAR_STAY_TIME   (3 * RT_TICK_PER_SECOND)        /* 每辆车停车 3 秒 */
#define WAIT_TIMEOUT    (2 * RT_TICK_PER_SECOND)        /* 申请车位最多等 2 秒 */
#define RETRY_DELAY     (1 * RT_TICK_PER_SECOND)        /* 超时后等 1 秒再申请 */

static rt_sem_t parking_sem = RT_NULL;                  /* 车位信号量（3 个资源） */
static volatile rt_uint8_t used_spaces = 0;             /* 当前已占用车位 */
static volatile rt_uint8_t cars_finished = 0;           /* 已离开的车辆数 */
static volatile rt_uint8_t task_running = 0;            /* 任务是否在运行（防重复启动） */

/* ------------------------------------------------------------
 * 车辆线程：申请车位 → 停车 → 离开
 * param 传的是车辆编号（1~5）
 * ---------------------------------------------------------- */
static void car_thread_entry(void *param)
{
    rt_uint8_t id = (rt_uint8_t)(rt_ubase_t)param;

    while (1)
    {
        /* 申请一个车位，最多等 2 秒 */
        if (rt_sem_take(parking_sem, WAIT_TIMEOUT) == RT_EOK)
        {
            /* 拿到车位：占用数 +1，打印进入信息 */
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", id, used_spaces);

            /* 停车 3 秒（模拟停留） */
            rt_thread_mdelay(CAR_STAY_TIME);

            /* 离开：占用数 -1，释放车位 */
            used_spaces--;
            rt_kprintf("car%d left\n", id);
            rt_sem_release(parking_sem);
            break;
        }
        else
        {
            /* 2 秒没等到车位：打印等待信息，稍后重新申请 */
            rt_kprintf("car%d waiting for a space\n", id);
            rt_thread_mdelay(RETRY_DELAY);
        }
    }

    /* 记录一辆车完成，供控制线程判断何时收尾 */
    cars_finished++;
}

/* ------------------------------------------------------------
 * 控制线程：创建信号量 + 5 辆车，等全部结束后清理资源
 * ---------------------------------------------------------- */
static void parking_ctrl_entry(void *param)
{
    rt_thread_t cars[CAR_COUNT];
    rt_uint8_t created = 0;
    int i;

    /* 1. 创建计数信号量，初始值 = 3 个车位 */
    parking_sem = rt_sem_create("parking", PARKING_SPACES, RT_IPC_FLAG_PRIO);
    if (parking_sem == RT_NULL)
    {
        rt_kprintf("parking: create semaphore failed\n");
        task_running = 0;
        return;
    }

    /* 2. 创建 5 个车辆线程，逐个检查创建结果 */
    for (i = 0; i < CAR_COUNT; i++)
    {
        cars[i] = rt_thread_create("car", car_thread_entry,
                                   (void *)(rt_ubase_t)(i + 1), /* 车辆编号 1~5 */
                                   2048, 20, 10);               /* 栈 2KB, 优先级 20 */
        if (cars[i] == RT_NULL)
        {
            rt_kprintf("parking: create car%d thread failed\n", i + 1);
            break;      /* 创建失败就只跑已创建的车 */
        }
        rt_thread_startup(cars[i]);
        created++;
    }

    /* 3. 等待所有车辆线程跑完（每辆车离开后 cars_finished +1） */
    while (cars_finished < created)
    {
        rt_thread_mdelay(50);
    }

    /* 4. 全部结束：删除信号量，复位状态，允许下次再执行 */
    rt_sem_delete(parking_sem);
    parking_sem = RT_NULL;
    used_spaces = 0;
    cars_finished = 0;
    task_running = 0;
    rt_kprintf("parking: all cars done, resources released\n");
}

/* ------------------------------------------------------------
 * MSH 命令入口：parking_demo
 * ---------------------------------------------------------- */
static void parking_demo(void)
{
    rt_thread_t ctrl;

    /* 任务还在跑时拒绝重复启动 */
    if (task_running)
    {
        rt_kprintf("parking_demo is already running\n");
        return;
    }

    task_running = 1;
    used_spaces = 0;
    cars_finished = 0;

    ctrl = rt_thread_create("parking_ctrl", parking_ctrl_entry, RT_NULL,
                            2048, 15, 10);              /* 控制线程优先级更高 */
    if (ctrl == RT_NULL)
    {
        rt_kprintf("parking_demo: create control thread failed\n");
        task_running = 0;
        return;
    }
    rt_thread_startup(ctrl);
}

/* 导出为 msh 命令 */
MSH_CMD_EXPORT(parking_demo, parking lot demo: 5 cars compete for 3 spaces);
