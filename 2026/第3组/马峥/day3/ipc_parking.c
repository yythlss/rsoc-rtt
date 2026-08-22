/*
 * 练习一：停车场车位管理
 * 机制：计数信号量（rt_sem）
 *
 * 场景：3个车位，5辆车竞争
 * 原理：计数信号量初始值=3，每辆车 take(-1)，离开时 release(+1)
 */

#include <rtthread.h>

#define PARKING_SPACES  3
#define CAR_COUNT       5
#define WAIT_TIMEOUT_MS 2000

/* 计数信号量（静态初始化） */
static struct rt_semaphore sem_parking;

/* 当前使用车位数（用于打印） */
static rt_uint8_t used_spaces = 0;

/* 线程栈（需要字节对齐） */
#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char car_stacks[CAR_COUNT][512];

static struct rt_thread car_threads[CAR_COUNT];

/* 车辆入口 */
static void car_entry(void *parameter)
{
    rt_uint32_t car_no = (rt_uint32_t)parameter;

    while (1)
    {
        rt_kprintf("car%d arrived\n", car_no);

        /* 尝试获取车位，等待 WAIT_TIMEOUT_MS ms */
        rt_tick_t tick = RT_TICK_PER_SECOND * WAIT_TIMEOUT_MS / 1000;
        rt_err_t err = rt_sem_take(&sem_parking, tick);

        if (err == RT_EOK)
        {
            /* 获得车位，进入 */
            used_spaces++;
            rt_kprintf("car%d entered, used spaces: %d\n", car_no, used_spaces);

            /* 停留 1~3 秒后离开 */
            rt_thread_mdelay(1000 + (car_no % 3) * 1000);

            /* 离开，释放车位 */
            used_spaces--;
            rt_kprintf("car%d left\n", car_no);
            rt_sem_release(&sem_parking);
        }
        else
        {
            /* 等待超时，稍后重试 */
            rt_kprintf("car%d waiting for a space (timeout), will retry\n", car_no);
            rt_thread_mdelay(500);
        }
    }
}

/* MSH 命令入口 */
int parking_demo(void)
{
    rt_err_t err;
    rt_thread_t t;

    /* 防止重复执行 */
    for (int i = 0; i < CAR_COUNT; i++)
    {
        char name[12];
        rt_snprintf(name, sizeof(name), "car%d", i + 1);
        t = rt_thread_find(name);
        if (t != RT_NULL)
        {
            rt_kprintf("[parking_demo] warning: thread %s already exists, skip\n", name);
            return -1;
        }
    }

    /* 初始化计数信号量 */
    err = rt_sem_init(&sem_parking, "sem_park", PARKING_SPACES, RT_IPC_FLAG_PRIO);
    if (err != RT_EOK)
    {
        rt_kprintf("[parking_demo] ERROR: sem init failed: %d\n", err);
        return -1;
    }
    rt_kprintf("[parking_demo] sem_parking init, spaces=%d\n", PARKING_SPACES);

    /* 创建 5 个车辆线程 */
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
                             15,
                             10);
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
