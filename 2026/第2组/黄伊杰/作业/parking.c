/*
 * parking.c - 练习一：停车场车位管理（计数信号量）
 *
 * 芯片     : STM32F407ZGT6
 * 开发板   : 星火1号 (RT-Thread STM32F407-RT-SPARK)
 * 开发环境 : RT-Thread Studio
 *
 * 场景:
 *   停车场只有 3 个车位，同时有 5 辆车尝试进入。车位是有限资源，
 *   车辆只有获得车位（信号量）后才能进入停车场。
 *
 * 设计:
 *   - 创建一个初始值为 3 的计数信号量，代表剩余车位。
 *   - 动态创建 5 个车辆线程 car1..car5。
 *   - 每辆车到达后 rt_sem_take(..., TIME_WAIT_MS) 申请一个车位，
 *     成功则 park_used++ 并打印进入信息；超时则打印等待超时信息。
 *   - 车辆停留一段时间后 park_used-- 并 rt_sem_release 释放车位。
 *   - 由计数信号量保证任意时刻场内车辆数 <= 3。
 *
 * 集成方式:
 *   - MSH_CMD_EXPORT() 导出 parking_demo 命令启动。
 *   - 通过静态 running 标志检测任务是否仍在运行，重复执行时拒绝启动，
 *     避免因同名线程/IPC 对象残留导致异常。
 *
 * 需回答的思考题: 为何用计数信号量而非互斥量（见 README.md）。
 */

#include <rtthread.h>
#include <rtdevice.h>

#define PARK_SLOTS         3               /* 车位数（信号量初值）   */
#define CAR_NUM            5               /* 车辆数                */
#define CAR_WAIT_MS        2000            /* 申请车位超时: 2 秒     */
#define CAR_STAY_MS        1500            /* 车内停留时间            */
#define CAR_STACK_SIZE     1024
#define CAR_PRIORITY       20
#define CAR_TIMESLICE      5

static rt_sem_t park_sem = RT_NULL;        /* 剩余车位计数信号量      */
static rt_uint8_t  park_used = 0;          /* 场内车辆数（受信号量保护）*/
static rt_uint8_t  park_running = 0;       /* 防重入运行标志          */

static void car_entry(void *param)
{
    rt_uint32_t car_no = (rt_uint32_t)param;
    rt_err_t    ret;

    while (1)
    {
        /* 申请一个车位，最多等待 CAR_WAIT_MS */
        ret = rt_sem_take(park_sem, CAR_WAIT_MS);
        if (ret == -RT_ETIMEOUT)
        {
            rt_kprintf("car%d waiting for a space, try again later\n", car_no);
            rt_thread_mdelay(300);
            continue;
        }
        if (ret != RT_EOK)
        {
            rt_kprintf("car%d take sem error 0x%x\n", car_no, ret);
            break;
        }

        /* 已获得车位进入。此处只有持有信号量的线程会修改 park_used，安全。 */
        park_used++;
        rt_kprintf("car%d entered, used spaces: %d\n", car_no, park_used);

        /* 停留一段时间后离开并释放车位 */
        rt_thread_mdelay(CAR_STAY_MS);

        park_used--;
        rt_sem_release(park_sem);
        rt_kprintf("car%d left, used spaces: %d\n", car_no, park_used);

        /* 稍作间隔再排队进入，便于观察 */
        rt_thread_mdelay(500);
    }
}

int parking_demo(void)
{
    rt_thread_t tid[CAR_NUM];
    rt_uint32_t i;

    if (park_running)
    {
        rt_kprintf("parking_demo is already running, ignore this command\n");
        return -RT_EBUSY;
    }
    park_running = 1;

    /* 1. 创建初值为 PARK_SLOTS 的计数信号量 */
    park_sem = rt_sem_create("park_sem", PARK_SLOTS, RT_IPC_FLAG_FIFO);
    if (park_sem == RT_NULL)
    {
        rt_kprintf("create park sem failed\n");
        park_running = 0;
        return -RT_ENOMEM;
    }
    /* rt_sem_create 成功即返回有效句柄，此处再校验一遍保险 */
    RT_ASSERT(park_sem != RT_NULL);

    /* 2. 创建 5 个车辆线程 */
    park_used = 0;
    for (i = 0; i < CAR_NUM; i++)
    {
        tid[i] = rt_thread_create("park_car", car_entry, (void *)(i + 1),
                                  CAR_STACK_SIZE, CAR_PRIORITY, CAR_TIMESLICE);
        if (tid[i] == RT_NULL)
        {
            rt_kprintf("create car%u thread failed\n", i + 1);
            /* 清理已创建的线程与信号量，保证重入干净 */
            while (i--)
                rt_thread_delete(tid[i]);
            rt_sem_delete(park_sem);
            park_running = 0;
            return -RT_ENOMEM;
        }
        if (rt_thread_startup(tid[i]) != RT_EOK)
        {
            rt_kprintf("startup car%u thread failed\n", i + 1);
            rt_thread_delete(tid[i]);
            rt_sem_delete(park_sem);
            park_running = 0;
            return -RT_ERROR;
        }
    }

    rt_kprintf("parking: %d lots, %d cars, demo started\n", PARK_SLOTS, CAR_NUM);
    /* 任务常驻运行，park_running 保持为 1 以拒绝重复启动 */
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, parking demo: counting semaphore 3 lots 5 cars);