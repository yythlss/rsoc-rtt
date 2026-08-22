#include <rtthread.h>
#include <rthw.h>               /* rt_hw_interrupt_disable / enable */

#define CAR_NUM             5          /* 车辆数量 */
#define CAR_PRIO            15         /* 车辆线程优先级 */
#define THREAD_TIMESLICE    10         /* 时间片 */
#define THREAD_STACK_SIZE   1024       /* 线程栈 */

#define CAR_STAY_TIME       3000       /* 车辆停留时间（ms） */
#define CAR_WAIT_TIMEOUT    2000       /* 申请车位超时（ms） */
#define CAR_RETRY_DELAY     100        /* 超时后重试间隔（ms） */
#define CAR_ROUNDS          3          /* 每辆车进出次数 */

static rt_sem_t    parking_sem = RT_NULL;   /* 车位信号量，初值 3 */
static rt_sem_t    done_sem    = RT_NULL;   /* 完成信号量：命令等待全部车辆退出 */
static rt_thread_t car_thread[CAR_NUM];     /* 车辆线程句柄 */
static volatile rt_uint8_t demo_running = 0;    /* 任务运行标志，防止重复启动 */

/* 场内车辆数（与信号量同步维护，仅用于打印观测） */
static volatile rt_uint8_t used_spaces = 0;

/* 车辆线程入口：错开到达 -> 申请车位（2s 超时重试）-> 停留 -> 离开 */
static void car_entry(void *param)
{
    int id = (int)param;
    int round;

    rt_thread_mdelay(id * 300);

    for (round = 0; round < CAR_ROUNDS; round++)
    {
        rt_err_t ret;

        /* 申请车位：2s 超时，失败打印并稍后重新申请 */
        for (;;)
        {
            ret = rt_sem_take(parking_sem, CAR_WAIT_TIMEOUT);
            if (ret == RT_EOK)
            {
                break;
            }
            rt_kprintf("car%d wait timeout, retry later\n", id);
            rt_thread_mdelay(CAR_RETRY_DELAY);
        }

        /* 进入停车场 */
        {
            rt_base_t level = rt_hw_interrupt_disable();
            used_spaces++;
            rt_hw_interrupt_enable(level);
        }
        rt_kprintf("car%d entered, used spaces: %d\n", id, used_spaces);

        /* 停留一段时间（模拟停车） */
        rt_thread_mdelay(CAR_STAY_TIME);

        /* 离开并释放车位 */
        {
            rt_base_t level = rt_hw_interrupt_disable();
            used_spaces--;
            rt_hw_interrupt_enable(level);
        }
        rt_sem_release(parking_sem);
        rt_kprintf("car%d left, used spaces: %d\n", id, used_spaces);

        rt_thread_mdelay(200);
    }

    rt_kprintf("car%d finished\n", id);
    rt_sem_release(done_sem);       /* 通知命令：本车已退出 */
}

static int parking_demo(void)
{
    int i;

    if (demo_running)
    {
        rt_kprintf("parking_demo is already running\n");
        return -RT_ERROR;
    }

    /* 1. 创建车位信号量：初值 3，表示 3 个空车位 */
    parking_sem = rt_sem_create("p_sem", 3, RT_IPC_FLAG_FIFO);
    if (parking_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed\n");
        return -RT_ERROR;
    }

    /* 2. 创建完成信号量：用于命令等待所有车辆线程退出 */
    done_sem = rt_sem_create("done", 0, RT_IPC_FLAG_FIFO);
    if (done_sem == RT_NULL)
    {
        rt_sem_delete(parking_sem);
        parking_sem = RT_NULL;
        rt_kprintf("create done semaphore failed\n");
        return -RT_ERROR;
    }

    /* 3. 先全部创建车辆线程（暂不启动），中途失败则统一回收，保证无残留 */
    for (i = 0; i < CAR_NUM; i++)
    {
        char name[RT_NAME_MAX];

        rt_snprintf(name, sizeof(name), "car%d", i + 1);
        car_thread[i] = rt_thread_create(name, car_entry, (void *)(i + 1),
                                         THREAD_STACK_SIZE, CAR_PRIO, THREAD_TIMESLICE);
        if (car_thread[i] == RT_NULL)
        {
            int j;

            rt_kprintf("create thread %s failed\n", name);
            for (j = 0; j < i; j++)
            {
                rt_thread_delete(car_thread[j]);   
            }
            rt_sem_delete(done_sem);
            rt_sem_delete(parking_sem);
            done_sem = RT_NULL;
            parking_sem = RT_NULL;
            return -RT_ERROR;
        }
    }

    /* 4. 全部创建成功后再统一启动 */
    demo_running = 1;
    for (i = 0; i < CAR_NUM; i++)
    {
        rt_thread_startup(car_thread[i]);
    }

    /* 5. 等待所有车辆线程退出（每车退出释放一次 done_sem） */
    for (i = 0; i < CAR_NUM; i++)
    {
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    }

    /* 6. 统一清理：删除线程与 IPC 对象，保证可重复执行 */
    for (i = 0; i < CAR_NUM; i++)
    {
        if (car_thread[i] != RT_NULL)
        {
            rt_thread_delete(car_thread[i]);
            car_thread[i] = RT_NULL;
        }
    }
    rt_sem_delete(done_sem);
    rt_sem_delete(parking_sem);
    done_sem    = RT_NULL;
    parking_sem = RT_NULL;
    demo_running = 0;

    rt_kprintf("parking demo finished\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, parking lot demo with counting semaphore);
