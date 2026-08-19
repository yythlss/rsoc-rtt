#include <rtthread.h>

#define CAR_COUNT           5
#define PARKING_SPACES      3
#define CAR_STACK_SIZE      1024
#define MONITOR_STACK_SIZE  1024

static rt_sem_t parking_sem = RT_NULL;
static rt_mutex_t count_mutex = RT_NULL;

static struct rt_thread car_threads[CAR_COUNT];
static rt_uint8_t car_stacks[CAR_COUNT][CAR_STACK_SIZE];

static struct rt_thread monitor_thread;
static rt_uint8_t monitor_stack[MONITOR_STACK_SIZE];

static volatile rt_uint8_t demo_running = 0;
static volatile rt_uint8_t cars_ready = 0;
static volatile rt_uint8_t started_cars = 0;
static volatile rt_uint8_t finished_cars = 0;
static volatile rt_uint8_t used_spaces = 0;

static void parking_reset_state(void)
{
    cars_ready = 0;
    started_cars = 0;
    finished_cars = 0;
    used_spaces = 0;
    demo_running = 0;
}

static void parking_cleanup_ipc(void)
{
    if (parking_sem != RT_NULL)
    {
        rt_sem_delete(parking_sem);
        parking_sem = RT_NULL;
    }

    if (count_mutex != RT_NULL)
    {
        rt_mutex_delete(count_mutex);
        count_mutex = RT_NULL;
    }
}

static void car_thread_entry(void *parameter)
{
    rt_uint32_t car_id = (rt_uint32_t)parameter;
    rt_err_t result;
    rt_tick_t timeout = rt_tick_from_millisecond(2000);

    rt_thread_mdelay(car_id * 300);

    while (1)
    {
        result = rt_sem_take(parking_sem, timeout);
        if (result == RT_EOK)
        {
            rt_mutex_take(count_mutex, RT_WAITING_FOREVER);
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, used_spaces);
            rt_mutex_release(count_mutex);

            rt_thread_mdelay(2500 + car_id * 200);

            rt_mutex_take(count_mutex, RT_WAITING_FOREVER);
            if (used_spaces > 0)
                used_spaces--;
            rt_kprintf("car%d left, used spaces: %d\n", car_id, used_spaces);
            rt_mutex_release(count_mutex);

            rt_sem_release(parking_sem);
            break;
        }
        else if (result == -RT_ETIMEOUT)
        {
            rt_kprintf("car%d waiting for a space\n", car_id);
            rt_thread_mdelay(500);
        }
        else
        {
            rt_kprintf("car%d failed to take parking space, err=%d\n", car_id, result);
            rt_thread_mdelay(500);
        }
    }

    rt_mutex_take(count_mutex, RT_WAITING_FOREVER);
    finished_cars++;
    rt_mutex_release(count_mutex);
}

static void monitor_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        if (!cars_ready)
        {
            rt_thread_mdelay(200);
            continue;
        }

        rt_mutex_take(count_mutex, RT_WAITING_FOREVER);
        if (finished_cars >= started_cars)
        {
            rt_mutex_release(count_mutex);
            break;
        }
        rt_mutex_release(count_mutex);

        rt_thread_mdelay(200);
    }

    parking_reset_state();
    parking_cleanup_ipc();

    rt_kprintf("parking demo finished\n");
}

int parking_demo(int argc, char **argv)
{
    rt_uint8_t i;
    rt_err_t result;

    if (demo_running)
    {
        rt_kprintf("parking demo is already running\n");
        return -RT_ERROR;
    }

    demo_running = 1;
    cars_ready = 0;
    started_cars = 0;
    finished_cars = 0;
    used_spaces = 0;

    parking_sem = rt_sem_create("parksem", PARKING_SPACES, RT_IPC_FLAG_PRIO);
    if (parking_sem == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed\n");
        parking_reset_state();
        return -RT_ERROR;
    }

    count_mutex = rt_mutex_create("parkmtx", RT_IPC_FLAG_PRIO);
    if (count_mutex == RT_NULL)
    {
        rt_kprintf("create count mutex failed\n");
        parking_cleanup_ipc();
        parking_reset_state();
        return -RT_ERROR;
    }

    result = rt_thread_init(&monitor_thread,
                            "parkmon",
                            monitor_thread_entry,
                            RT_NULL,
                            monitor_stack,
                            sizeof(monitor_stack),
                            21,
                            10);
    if (result != RT_EOK)
    {
        rt_kprintf("init monitor thread failed, err=%d\n", result);
        parking_cleanup_ipc();
        parking_reset_state();
        return result;
    }

    result = rt_thread_startup(&monitor_thread);
    if (result != RT_EOK)
    {
        rt_kprintf("startup monitor thread failed, err=%d\n", result);
        parking_cleanup_ipc();
        parking_reset_state();
        return result;
    }

    for (i = 0; i < CAR_COUNT; i++)
    {
        result = rt_thread_init(&car_threads[i],
                                "car",
                                car_thread_entry,
                                (void *)(rt_ubase_t)(i + 1),
                                car_stacks[i],
                                sizeof(car_stacks[i]),
                                20,
                                10);
        if (result != RT_EOK)
        {
            rt_kprintf("init car%d thread failed, err=%d\n", i + 1, result);
            continue;
        }

        result = rt_thread_startup(&car_threads[i]);
        if (result != RT_EOK)
        {
            rt_kprintf("startup car%d thread failed, err=%d\n", i + 1, result);
            continue;
        }

        started_cars++;
    }

    cars_ready = 1;

    return RT_EOK;
}

MSH_CMD_EXPORT(parking_demo, parking lot semaphore demo);
