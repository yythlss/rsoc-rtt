#include <rtthread.h>
#include <finsh.h>

#define PARKING_CAR_COUNT 5
#define PARKING_SPACE_COUNT 3

struct parking_context
{
    rt_sem_t spaces;
    rt_mutex_t lock;
    rt_uint32_t used;
    rt_uint32_t finished;
    rt_bool_t running;
    rt_thread_t threads[PARKING_CAR_COUNT];
};

static struct parking_context parking;
static const char *const parking_names[PARKING_CAR_COUNT] = {
    "car1", "car2", "car3", "car4", "car5"
};

static void parking_worker(void *parameter)
{
    rt_uint32_t id = (rt_uint32_t)(rt_ubase_t)parameter;
    rt_bool_t entered = RT_FALSE;
    rt_bool_t is_last = RT_FALSE;

    while (!entered)
    {
        if (rt_sem_take(parking.spaces, RT_TICK_PER_SECOND * 2) == RT_EOK)
        {
            rt_mutex_take(parking.lock, RT_WAITING_FOREVER);
            parking.used++;
            rt_kprintf("car%u entered, used spaces: %u\n", id, parking.used);
            rt_mutex_release(parking.lock);
            entered = RT_TRUE;
        }
        else
        {
            rt_kprintf("car%u waiting for a space\n", id);
        }
    }

    rt_thread_mdelay(2500 + id * 150);
    rt_mutex_take(parking.lock, RT_WAITING_FOREVER);
    if (parking.used > 0)
        parking.used--;
    rt_kprintf("car%u left\n", id);
    parking.finished++;
    if (parking.finished == PARKING_CAR_COUNT)
        is_last = RT_TRUE;
    rt_mutex_release(parking.lock);
    rt_sem_release(parking.spaces);

    if (is_last)
    {
        rt_kprintf("parking_demo finished, maximum capacity was %u\n",
                   PARKING_SPACE_COUNT);
        rt_sem_delete(parking.spaces);
        rt_mutex_delete(parking.lock);
        parking.running = RT_FALSE;
    }

    rt_thread_delete(rt_thread_self());
}

static void parking_demo(void)
{
    rt_uint32_t i;

    if (parking.running)
    {
        rt_kprintf("parking_demo is already running\n");
        return;
    }

    parking.spaces = rt_sem_create("park_sem", PARKING_SPACE_COUNT,
                                   RT_IPC_FLAG_FIFO);
    if (parking.spaces == RT_NULL)
    {
        rt_kprintf("parking_demo: semaphore create failed\n");
        return;
    }
    parking.lock = rt_mutex_create("park_lock", RT_IPC_FLAG_FIFO);
    if (parking.lock == RT_NULL)
    {
        rt_sem_delete(parking.spaces);
        rt_kprintf("parking_demo: mutex create failed\n");
        return;
    }

    parking.used = 0;
    parking.finished = 0;
    parking.running = RT_TRUE;
    for (i = 0; i < PARKING_CAR_COUNT; i++)
    {
        parking.threads[i] = rt_thread_create(parking_names[i], parking_worker,
                                               (void *)(rt_ubase_t)(i + 1),
                                               768, 16, 10);
        if (parking.threads[i] == RT_NULL)
        {
            rt_uint32_t j;
            parking.running = RT_FALSE;
            for (j = 0; j < i; j++)
                rt_thread_delete(parking.threads[j]);
            rt_mutex_delete(parking.lock);
            rt_sem_delete(parking.spaces);
            rt_kprintf("parking_demo: car thread %u create failed\n", i + 1);
            return;
        }
    }

    for (i = 0; i < PARKING_CAR_COUNT; i++)
        rt_thread_startup(parking.threads[i]);
    rt_kprintf("parking_demo started: %u cars, %u spaces\n",
               PARKING_CAR_COUNT, PARKING_SPACE_COUNT);
}
MSH_CMD_EXPORT(parking_demo, counting semaphore parking lot demo);
