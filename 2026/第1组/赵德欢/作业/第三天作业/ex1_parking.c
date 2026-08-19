/*
 * 练习一：停车场车位管理（计数信号量）
 * 与 ex2/ex3/ex4 互斥，applications/ 下同一时刻只留一个。
 */
#include <rtthread.h>

#define PARK_TOTAL       3
#define CAR_NUM          5
#define CAR_STAY_TICK    rt_tick_from_millisecond(3000)
#define CAR_WAIT_TICK    rt_tick_from_millisecond(2000)

static rt_sem_t park_sem = RT_NULL;

static void car_entry(void *param)
{
    rt_uint32_t car_no = (rt_uint32_t)param;
    rt_uint32_t value;

    while (1)
    {
        if (rt_sem_take(park_sem, CAR_WAIT_TICK) == RT_EOK)
        {
            rt_sem_getvalue(park_sem, &value);
            rt_kprintf("car%u entered, used spaces: %u\n", car_no, PARK_TOTAL - value);
            rt_thread_mdelay(CAR_STAY_TICK);
            rt_kprintf("car%u left\n", car_no);
            rt_sem_release(park_sem);
            break;
        }
        else
        {
            rt_kprintf("car%u waiting for a space\n", car_no);
            rt_thread_mdelay(rt_tick_from_millisecond(500));
        }
    }
}

static int parking_init(void)
{
    rt_thread_t tid;
    rt_uint32_t i;
    char name[8];

    park_sem = rt_sem_create("park", PARK_TOTAL, RT_IPC_FLAG_FIFO);
    if (park_sem == RT_NULL)
    {
        rt_kprintf("park sem create failed\n");
        return -RT_ENOMEM;
    }

    for (i = 1; i <= CAR_NUM; i++)
    {
        rt_snprintf(name, sizeof(name), "car%u", i);
        tid = rt_thread_create(name, car_entry, (void *)i, 1024, 20, 5);
        if (tid) rt_thread_startup(tid);
        else     rt_kprintf("%s create failed\n", name);
    }
    return RT_EOK;
}
INIT_APP_EXPORT(parking_init);
