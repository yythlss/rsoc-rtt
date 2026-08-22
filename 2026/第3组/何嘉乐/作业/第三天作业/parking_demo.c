#include <rtthread.h>

#define CAR_NUM             5
#define PARKING_SPACES      3
#define WAIT_TIME_MS        2000
#define PARK_TIME_MS        3000
#define RETRY_TIME_MS       500

static rt_sem_t parking_sem = RT_NULL;

static rt_uint8_t used_spaces = 0;
static rt_uint8_t finished_cars = 0;
static rt_uint8_t demo_running = 0;

static rt_uint8_t car_id[CAR_NUM] = {1, 2, 3, 4, 5};

/* 车辆线程 */
static void car_entry(void *parameter)
{
    rt_uint8_t id;
    rt_uint8_t spaces;
    rt_uint8_t finished;
    rt_err_t result;

    id = *(rt_uint8_t *)parameter;

    while (1)
    {
        /* 申请车位，最多等待 2 秒 */
        result = rt_sem_take(parking_sem,
                             rt_tick_from_millisecond(WAIT_TIME_MS));

        if (result == RT_EOK)
        {
            /* 更新已使用车位 */
            rt_enter_critical();
            used_spaces++;
            spaces = used_spaces;
            rt_exit_critical();

            rt_kprintf("car%d entered, used spaces: %d\n",
                       id, spaces);

            /* 模拟停车 */
            rt_thread_mdelay(PARK_TIME_MS);

            /* 更新已使用车位 */
            rt_enter_critical();
            used_spaces--;
            spaces = used_spaces;
            rt_exit_critical();

            rt_kprintf("car%d left, used spaces: %d\n",
                       id, spaces);

            /* 释放车位 */
            rt_sem_release(parking_sem);

            break;
        }
        else if (result == -RT_ETIMEOUT)
        {
            rt_kprintf("car%d waiting for a space\n", id);

            /* 稍后重新申请 */
            rt_thread_mdelay(RETRY_TIME_MS);
        }
        else
        {
            rt_kprintf("car%d take semaphore failed: %d\n",
                       id, result);

            rt_thread_mdelay(RETRY_TIME_MS);
        }
    }

    /* 记录完成车辆数量 */
    rt_enter_critical();
    finished_cars++;
    finished = finished_cars;
    rt_exit_critical();

    /* 最后一辆车负责释放资源 */
    if (finished == CAR_NUM)
    {
        rt_kprintf("all cars finished\n");

        if (parking_sem != RT_NULL)
        {
            rt_sem_delete(parking_sem);
            parking_sem = RT_NULL;
        }

        rt_enter_critical();
        used_spaces = 0;
        finished_cars = 0;
        demo_running = 0;
        rt_exit_critical();

        rt_kprintf("parking demo finished\n");
    }
}


/* 停车场实验 */
static void parking_demo(void)
{
    rt_thread_t tid[CAR_NUM];
    rt_uint8_t i;

    /* 防止重复启动 */
    rt_enter_critical();

    if (demo_running)
    {
        rt_exit_critical();

        rt_kprintf("parking demo is already running\n");
        return;
    }

    demo_running = 1;
    used_spaces = 0;
    finished_cars = 0;

    rt_exit_critical();

    /* 创建计数信号量 */
    parking_sem = rt_sem_create("park_sem",
                                PARKING_SPACES,
                                RT_IPC_FLAG_FIFO);

    if (parking_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed\n");

        demo_running = 0;
        return;
    }

    /* 创建车辆线程 */
    for (i = 0; i < CAR_NUM; i++)
    {
        tid[i] = rt_thread_create("car",
                                  car_entry,
                                  &car_id[i],
                                  1024,
                                  20,
                                  10);

        if (tid[i] == RT_NULL)
        {
            rt_uint8_t j;

            rt_kprintf("create car%d thread failed\n",
                       car_id[i]);

            /* 删除已创建但未启动的线程 */
            for (j = 0; j < i; j++)
            {
                if (tid[j] != RT_NULL)
                {
                    rt_thread_delete(tid[j]);
                }
            }

            rt_sem_delete(parking_sem);
            parking_sem = RT_NULL;

            demo_running = 0;

            return;
        }
    }

    rt_kprintf("parking demo start\n");
    rt_kprintf("total spaces: %d, total cars: %d\n",
               PARKING_SPACES, CAR_NUM);

    /* 启动车辆线程 */
    for (i = 0; i < CAR_NUM; i++)
    {
        rt_thread_startup(tid[i]);
    }
}

MSH_CMD_EXPORT(parking_demo, parking semaphore demo);
