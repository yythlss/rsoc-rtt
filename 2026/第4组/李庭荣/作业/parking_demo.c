/* Day3 exercise: counting semaphore as a parking-space resource limit. */
#include <rtthread.h>

#define PARKING_CAR_COUNT       5
#define PARKING_SPACE_COUNT     3
#define PARKING_TIMEOUT_TICKS   (2 * RT_TICK_PER_SECOND)
#define PARKING_STACK_SIZE      1024

struct parking_car
{
    rt_uint32_t id;
    rt_uint32_t stay_ms;
};

static struct rt_semaphore parking_spaces;
static struct rt_mutex parking_stat_lock;
static struct rt_thread parking_threads[PARKING_CAR_COUNT];
static ALIGN(RT_ALIGN_SIZE) rt_uint8_t parking_stacks[PARKING_CAR_COUNT][PARKING_STACK_SIZE];
static struct parking_car parking_cars[PARKING_CAR_COUNT];
static rt_uint32_t parking_used;
static rt_uint32_t parking_finished;
static rt_uint32_t parking_expected;
static rt_bool_t parking_abort;
static rt_bool_t parking_running;

static void parking_cleanup(void)
{
    rt_sem_detach(&parking_spaces);
    rt_mutex_detach(&parking_stat_lock);
    parking_running = RT_FALSE;
}

static void parking_detach_threads(rt_uint32_t first, rt_uint32_t count)
{
    rt_uint32_t index;

    for (index = first; index < count; index++)
        rt_thread_detach(&parking_threads[index]);
}

static void parking_finish(void)
{
    rt_bool_t last = RT_FALSE;

    rt_mutex_take(&parking_stat_lock, RT_WAITING_FOREVER);
    parking_finished++;
    if (parking_finished == parking_expected)
        last = RT_TRUE;
    rt_mutex_release(&parking_stat_lock);

    if (last)
    {
        parking_cleanup();
        rt_kprintf("parking_demo finished\n");
    }
}

static void parking_car_entry(void *parameter)
{
    struct parking_car *car = (struct parking_car *)parameter;
    rt_err_t result;

    while (!parking_abort)
    {
        result = rt_sem_take(&parking_spaces, PARKING_TIMEOUT_TICKS);
        if (result == RT_EOK)
        {
            if (parking_abort)
            {
                rt_sem_release(&parking_spaces);
                break;
            }

            rt_mutex_take(&parking_stat_lock, RT_WAITING_FOREVER);
            parking_used++;
            rt_kprintf("car%u entered, used spaces: %u\n",
                       car->id, parking_used);
            rt_mutex_release(&parking_stat_lock);

            rt_thread_mdelay(car->stay_ms);

            rt_mutex_take(&parking_stat_lock, RT_WAITING_FOREVER);
            parking_used--;
            rt_kprintf("car%u left, used spaces: %u\n",
                       car->id, parking_used);
            rt_mutex_release(&parking_stat_lock);
            rt_sem_release(&parking_spaces);
            break;
        }

        if (parking_abort)
            break;

        rt_kprintf("car%u waiting for a space\n", car->id);
        rt_thread_mdelay(300);
    }

    parking_finish();
}

int parking_demo(void)
{
    rt_err_t result;
    rt_uint32_t index;

    if (parking_running)
    {
        rt_kprintf("parking_demo is already running\n");
        return -RT_EBUSY;
    }

    result = rt_sem_init(&parking_spaces, "parksem",
                         PARKING_SPACE_COUNT, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_kprintf("parking semaphore init failed: %d\n", result);
        return result;
    }

    result = rt_mutex_init(&parking_stat_lock, "parklock", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_sem_detach(&parking_spaces);
        rt_kprintf("parking mutex init failed: %d\n", result);
        return result;
    }

    parking_used = 0;
    parking_finished = 0;
    parking_expected = PARKING_CAR_COUNT;
    parking_abort = RT_FALSE;
    parking_running = RT_TRUE;

    for (index = 0; index < PARKING_CAR_COUNT; index++)
    {
        parking_cars[index].id = index + 1;
        parking_cars[index].stay_ms = 700 + index * 180;
        result = rt_thread_init(&parking_threads[index], "car",
                                parking_car_entry, &parking_cars[index],
                                parking_stacks[index], PARKING_STACK_SIZE,
                                18, 5);
        if (result != RT_EOK)
        {
            parking_detach_threads(0, index);
            parking_cleanup();
            rt_kprintf("car%u thread init failed: %d\n", index + 1, result);
            return result;
        }
    }

    for (index = 0; index < PARKING_CAR_COUNT; index++)
    {
        result = rt_thread_startup(&parking_threads[index]);
        if (result != RT_EOK)
        {
            rt_bool_t cleanup_now;

            rt_mutex_take(&parking_stat_lock, RT_WAITING_FOREVER);
            parking_abort = RT_TRUE;
            parking_expected = index;
            cleanup_now = (parking_finished == parking_expected);
            rt_mutex_release(&parking_stat_lock);
            parking_detach_threads(index, PARKING_CAR_COUNT);
            if (cleanup_now)
                parking_cleanup();
            rt_kprintf("car%u thread startup failed: %d\n", index + 1, result);
            return result;
        }
    }

    rt_kprintf("parking_demo started: spaces=%u, cars=%u\n",
               PARKING_SPACE_COUNT, PARKING_CAR_COUNT);
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, counting semaphore parking demo);
