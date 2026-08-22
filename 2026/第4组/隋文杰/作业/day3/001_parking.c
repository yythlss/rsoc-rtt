#include <rtthread.h>

#define CAR_NUM     5
#define MAX_SLOTS   3
#define TIMEOUT_MS  2000

/* 信号量：真正控制车位 */
static rt_sem_t parking_sem;

/* 剩余车位计数器：专门用来显示 */
static rt_uint32_t remaining_slots = MAX_SLOTS;

/* 打印当前空位数 */
static void print_parking_status(void)
{
    rt_kprintf("Remaining slots: %d\n", remaining_slots);
}

/* 车辆线程入口 */
static void car_thread_entry(void *param)
{
    rt_uint32_t car_id = (rt_uint32_t)param;

    while (1)
    {
        if (rt_sem_take(parking_sem, TIMEOUT_MS) == RT_EOK)
        {
            /* 进入成功，空位减 1 */
            remaining_slots--;
            rt_kprintf("Car %d entered. ", car_id);
            print_parking_status();

            /* 模拟停车 2~4 秒 */
            rt_thread_mdelay(2000 + (car_id * 500));

            /* 离开，空位加 1 */
            remaining_slots++;
            rt_kprintf("Car %d left.    ", car_id);
            print_parking_status();

            rt_sem_release(parking_sem);
            break;
        }
        else
        {
            rt_kprintf("Car %d waiting timeout, retrying...\n", car_id);
        }
    }
}

int parking_lot_sample(void)
{
    rt_thread_t tid[CAR_NUM];
    rt_uint32_t i;

    /* 创建信号量 */
    parking_sem = rt_sem_create("parking", MAX_SLOTS, RT_IPC_FLAG_FIFO);
    if (parking_sem == RT_NULL)
    {
        rt_kprintf("Failed to create semaphore!\n");
        return -1;
    }

    rt_kprintf("=== Parking Lot Opened, Total Slots: %d ===\n", MAX_SLOTS);
    print_parking_status();

    /* 创建 5 辆车 */
    for (i = 0; i < CAR_NUM; i++)
    {
        tid[i] = rt_thread_create("car",
                                  car_thread_entry,
                                  (void *)(i + 1),
                                  1024,
                                  10,
                                  10);
        if (tid[i] != RT_NULL)
            rt_thread_startup(tid[i]);
    }

    return 0;
}


MSH_CMD_EXPORT(parking_demo, parking_demo);

int main(void)
{
//    rt_kprintf("Hello RT-Thread!\n");
    return 0;
}

