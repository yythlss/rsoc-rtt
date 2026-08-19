#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>

#define CAR_COUNT       5
#define PARKING_SPACES  3
#define THREAD_STACK    1024
#define THREAD_PRIORITY 18

struct car_info
{
    rt_uint8_t id;
};

static rt_sem_t space_sem;
static rt_sem_t car_done_sem;
static struct car_info cars[CAR_COUNT];
static rt_uint8_t used_spaces;
static rt_uint8_t max_used_spaces;
static rt_bool_t parking_running;

static void car_entry(void *parameter)
{
    struct car_info *car = (struct car_info *)parameter;
    rt_uint8_t used;

    rt_thread_mdelay((car->id - 1) * 40);

    /* 2 秒内没有车位就提示，稍后继续申请。 */
    while (rt_sem_take(space_sem, rt_tick_from_millisecond(2000)) != RT_EOK)
    {
        rt_kprintf("car%u wait timeout, retry later\n", car->id);
        rt_thread_mdelay(300);
    }

    {
        rt_base_t level = rt_hw_interrupt_disable();
        used_spaces++;
        if (used_spaces > max_used_spaces)
            max_used_spaces = used_spaces;
        used = used_spaces;
        rt_hw_interrupt_enable(level);
    }
    rt_kprintf("car%u entered, used spaces: %u\n", car->id, used);

    /* 前 3 辆车停得久一些，让后两辆车看到超时现象。 */
    rt_thread_mdelay((car->id <= 3) ? 2600 : 800);

    {
        rt_base_t level = rt_hw_interrupt_disable();
        used_spaces--;
        used = used_spaces;
        rt_hw_interrupt_enable(level);
    }
    rt_kprintf("car%u left, used spaces: %u\n", car->id, used);

    rt_sem_release(space_sem);
    rt_sem_release(car_done_sem);
}

static void parking_task(void *parameter)
{
    rt_thread_t tid[CAR_COUNT] = {RT_NULL};
    rt_uint8_t i;

    RT_UNUSED(parameter);
    used_spaces = 0;
    max_used_spaces = 0;

    space_sem = rt_sem_create("pkspace", PARKING_SPACES, RT_IPC_FLAG_FIFO);
    car_done_sem = rt_sem_create("pkdone", 0, RT_IPC_FLAG_FIFO);
    if ((space_sem == RT_NULL) || (car_done_sem == RT_NULL))
    {
        rt_kprintf("create parking semaphore failed\n");
        goto cleanup;
    }

    for (i = 0; i < CAR_COUNT; i++)
    {
        char name[RT_NAME_MAX];

        cars[i].id = i + 1;
        rt_snprintf(name, sizeof(name), "pkcar%u", i + 1);
        tid[i] = rt_thread_create(name, car_entry, &cars[i],
                                  THREAD_STACK, THREAD_PRIORITY, 5);
        if (tid[i] == RT_NULL)
        {
            rt_kprintf("create car%u failed\n", i + 1);
            while (i > 0)
                rt_thread_delete(tid[--i]);
            goto cleanup;
        }
    }

    rt_kprintf("parking demo start: 3 spaces, 5 cars\n");
    for (i = 0; i < CAR_COUNT; i++)
        rt_thread_startup(tid[i]);
    for (i = 0; i < CAR_COUNT; i++)
        rt_sem_take(car_done_sem, RT_WAITING_FOREVER);

    rt_kprintf("parking finished, max used: %u, result: %s\n",
               max_used_spaces,
               (max_used_spaces <= PARKING_SPACES) ? "PASS" : "FAIL");

cleanup:
    if (car_done_sem != RT_NULL)
    {
        rt_sem_delete(car_done_sem);
        car_done_sem = RT_NULL;
    }
    if (space_sem != RT_NULL)
    {
        rt_sem_delete(space_sem);
        space_sem = RT_NULL;
    }
    parking_running = RT_FALSE;
}

int parking_demo(void)
{
    rt_thread_t tid;

    if (parking_running)
    {
        rt_kprintf("parking demo is running\n");
        return -RT_EBUSY;
    }

    parking_running = RT_TRUE;
    tid = rt_thread_create("pkcoord", parking_task, RT_NULL,
                           1280, THREAD_PRIORITY - 1, 5);
    if (tid == RT_NULL)
    {
        parking_running = RT_FALSE;
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, counting semaphore parking demo);
