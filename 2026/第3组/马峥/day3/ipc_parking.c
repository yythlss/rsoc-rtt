/*
 * 练习一：停车场车位管理
 * 机制：计数信号量（rt_semaphore）
 *
 * 场景：3个车位，5辆车竞争
 * 原理：计数信号量初始值=3，每辆车 take(-1)，离开时 release(+1)
 */

#include <rtthread.h>
#include <rtdevice.h>

#define PARKING_SPACES  3
#define CAR_COUNT       5
#define WAIT_TIMEOUT_MS 2000

/* 计数信号量：剩余车位数 */
static rt_sem_t sem_parking = RT_NULL;

/* 当前使用车位数（用于打印） */
static rt_uint8_t used_spaces = 0;

/* 线程栈 */
static rt_uint32_t car_stacks[CAR_COUNT][512 / 4];
static struct rt_thread car_threads[CAR_COUNT];

/* 车辆入口 */
static void car_entry(void *parameter)
{
    rt_uint32_t car_no = (rt_uint32_t)parameter;

    while (1)
    {
        rt_kprintf("car%" RT_DEBUG_NOTAB "d arrived\n", car_no);

        /* 尝试获取车位，等待 WAIT_TIMEOUT_MS ms */
        rt_err_t err = rt_sem_take(sem_parking, rt_tick_from_millisecond(WAIT_TIMEOUT_MS));

        if (err == RT_EOK)
        {
            /* 获得车位，进入 */
            used_spaces++;
            rt_kprintf("car%" RT_DEBUG_NOTAB "d entered, used spaces: %d\n",
                       car_no, used_spaces);

            /* 停留 1~3 秒后离开 */
            rt_thread_mdelay(1000 + (car_no % 3) * 1000);

            /* 离开，释放车位 */
            used_spaces--;
            rt_kprintf("car%" RT_DEBUG_NOTAB "d left\n", car_no);
            rt_sem_release(sem_parking);
        }
        else
        {
            /* 等待超时，稍后重试 */
            rt_kprintf("car%" RT_DEBUG_NOTAB "d waiting for a space (timeout), will retry\n", car_no);
            rt_thread_mdelay(500);
        }
    }
}

/* 主动清理函数（防止重复执行时线程残留） */
static void parking_cleanup(void)
{
    rt_err_t err;
    for (int i = 0; i < CAR_COUNT; i++)
    {
        rt_thread_t t = rt_thread_find("parking");
        if (t != RT_NULL)
        {
            err = rt_thread_delete(t);
            if (err == RT_EOK)
                rt_kprintf("[parking] cleanup: deleted thread\n");
        }
    }
}

/* MSH 命令入口 */
static int parking_demo(void)
{
    rt_err_t err;

    /* ---- 防止重复执行：先清理残留 ---- */
    for (int i = 0; i < CAR_COUNT; i++)
    {
        char name[12];
        rt_snprintf(name, sizeof(name), "car%d", i + 1);
        rt_thread_t t = rt_thread_find(name);
        if (t != RT_NULL)
        {
            rt_kprintf("[parking_demo] warning: thread %s already exists, skip\n", name);
            return -1;
        }
    }

    /* ---- 初始化计数信号量 ---- */
    sem_parking = rt_sem_create("sem_park", PARKING_SPACES, RT_IPC_FLAG_FIFO);
    if (sem_parking == RT_NULL)
    {
        rt_kprintf("[parking_demo] ERROR: sem create failed\n");
        return -1;
    }
    rt_kprintf("[parking_demo] sem_parking created, spaces=%d\n", PARKING_SPACES);

    /* ---- 创建 5 个车辆线程 ---- */
    for (int i = 0; i < CAR_COUNT; i++)
    {
        char name[12];
        rt_snprintf(name, sizeof(name), "car%d", i + 1);

        err = rt_thread_init(&car_threads[i],
                             name,
                             car_entry,
                             (void *)(rt_uint32_t)(i + 1),
                             &car_stacks[i],
                             sizeof(car_stacks[i]),
                             15,     /* 优先级 15 */
                             10);    /* 时间片 10 */
        if (err != RT_EOK)
        {
            rt_kprintf("[parking_demo] ERROR: car%d thread init failed: %d\n", i + 1, err);
            continue;
        }

        err = rt_thread_startup(&car_threads[i]);
        if (err != RT_EOK)
        {
            rt_kprintf("[parking_demo] ERROR: car%d thread startup failed: %d\n", i + 1, err);
        }
    }

    rt_kprintf("[parking_demo] all %d car threads started\n", CAR_COUNT);
    return 0;
}
MSH_CMD_EXPORT(parking_demo, parking lot management with counting semaphore);
