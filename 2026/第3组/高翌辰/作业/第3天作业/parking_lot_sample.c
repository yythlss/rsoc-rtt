/*
 * 程序清单：停车场车位管理
 *
 * 3 个车位由一个初始值为 3 的计数信号量表示；5 辆车申请车位，
 * 未在 2 秒内取得车位时打印超时信息，并在短暂等待后重新申请。
 */
#include <rtthread.h>

#define PARKING_SPACE_COUNT     3
#define CAR_COUNT               5
#define CAR1_THREAD_PRIORITY    20
#define CAR_THREAD_TIMESLICE    5
#define CAR_PARK_TIME_MS        3000
#define CAR_WAIT_TIMEOUT_MS     2000

struct car
{
    rt_uint8_t id;
    struct rt_thread thread;
    char stack[1024];
};

static rt_sem_t parking_spaces = RT_NULL;
static rt_mutex_t parking_lock = RT_NULL;
static rt_uint8_t used_spaces;
static rt_uint8_t cars_finished;
static rt_uint8_t cars_started;
static struct car cars[CAR_COUNT];
static const char * const car_names[CAR_COUNT] =
{
    "car1", "car2", "car3", "car4", "car5"
};

static void car_entry(void *parameter)
{
    struct car *car = parameter;

    while (rt_sem_take(parking_spaces,
                       rt_tick_from_millisecond(CAR_WAIT_TIMEOUT_MS)) != RT_EOK)
    {
        rt_mutex_take(parking_lock, RT_WAITING_FOREVER);
        rt_kprintf("car%d waiting for a space (timeout)\n", car->id);
        rt_mutex_release(parking_lock);
        rt_thread_mdelay(100);
    }

    rt_mutex_take(parking_lock, RT_WAITING_FOREVER);
    used_spaces++;
    rt_kprintf("car%d entered, used spaces: %d\n", car->id, used_spaces);
    rt_mutex_release(parking_lock);

    rt_thread_mdelay(CAR_PARK_TIME_MS);

    rt_mutex_take(parking_lock, RT_WAITING_FOREVER);
    used_spaces--;
    rt_kprintf("car%d left, used spaces: %d\n", car->id, used_spaces);
    if (++cars_finished == cars_started)
    {
        rt_mutex_release(parking_lock);
        rt_sem_release(parking_spaces);
        rt_sem_delete(parking_spaces);
        rt_mutex_delete(parking_lock);
        parking_spaces = RT_NULL;
        parking_lock = RT_NULL;
        return;
    }
    rt_mutex_release(parking_lock);
    rt_sem_release(parking_spaces);
}

int parking_demo(void)
{
    rt_uint8_t i;
    rt_err_t result;

    if (parking_spaces != RT_NULL)
    {
        rt_kprintf("parking demo is already running.\n");
        return -RT_EBUSY;
    }

    parking_spaces = rt_sem_create("parking", PARKING_SPACE_COUNT, RT_IPC_FLAG_PRIO);
    if (parking_spaces == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed.\n");
        return -RT_ENOMEM;
    }

    parking_lock = rt_mutex_create("parklock", RT_IPC_FLAG_PRIO);
    if (parking_lock == RT_NULL)
    {
        rt_kprintf("create parking mutex failed.\n");
        rt_sem_delete(parking_spaces);
        parking_spaces = RT_NULL;
        return -RT_ENOMEM;
    }

    used_spaces = 0;
    cars_finished = 0;
    cars_started = 0;

    /* 先完成全部线程初始化；编号越小优先级越高，保证按 car1 到 car5 进入。 */
    for (i = 0; i < CAR_COUNT; i++)
    {
        cars[i].id = i + 1;
        result = rt_thread_init(&cars[i].thread,
                                car_names[i],
                                car_entry,
                                &cars[i],
                                cars[i].stack,
                                sizeof(cars[i].stack),
                                CAR1_THREAD_PRIORITY + i,
                                CAR_THREAD_TIMESLICE);
        if (result != RT_EOK)
        {
            rt_kprintf("create car%d thread failed: %d\n", cars[i].id, result);
            while (i > 0)
            {
                rt_thread_detach(&cars[--i].thread);
            }
            rt_mutex_delete(parking_lock);
            rt_sem_delete(parking_spaces);
            parking_lock = RT_NULL;
            parking_spaces = RT_NULL;
            return result;
        }
    }

    for (i = 0; i < CAR_COUNT; i++)
    {
        result = rt_thread_startup(&cars[i].thread);
        if (result != RT_EOK)
        {
            rt_kprintf("start car%d thread failed: %d\n", cars[i].id, result);
            for (; i < CAR_COUNT; i++)
            {
                rt_thread_detach(&cars[i].thread);
            }
            return result;
        }
        cars_started++;
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, parking space semaphore demo);
