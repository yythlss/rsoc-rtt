#include <rtthread.h>


static rt_sem_t parking_sem = RT_NULL;
static rt_uint8_t car_count = 0; //车辆计数器

#define PARKING_NUM 3

#define CAR_NUM 5


static void car_entry(void *parameter)
{
    rt_uint32_t car_id = (rt_uint32_t)parameter;

    while(1)
    {
        rt_err_t temp = rt_sem_take(parking_sem, rt_tick_from_millisecond(2000));
        if(temp == RT_EOK){
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, PARKING_NUM - parking_sem->value);
            rt_thread_mdelay(3500);
            rt_sem_release(parking_sem);
            rt_kprintf("car%d left\n", car_id);
            car_count++;
            if(car_count == CAR_NUM){
                rt_sem_delete(parking_sem);
                parking_sem = RT_NULL;
                car_count = 0;
            }
            break;
        }
        else{
            rt_kprintf("car%d waiting for a space\n", car_id);
        }
    }
}

static int parking_demo(void)
{
    if (parking_sem != RT_NULL)
    {
        rt_kprintf("parking demo already running\n");
        return -1;
    }

    parking_sem = rt_sem_create("parking", PARKING_NUM, RT_IPC_FLAG_PRIO);

    if(parking_sem == RT_NULL){
        rt_kprintf("parking_sem create failed\n");
        return -1;
    }
    else{
        for (rt_uint8_t i = 0; i < CAR_NUM; ++i) {
            char name[8];
            rt_snprintf(name, sizeof(name), "car%d", i + 1);
            rt_thread_t temp = rt_thread_create(name, car_entry, (void *)(i + 1), 1024, 20, 10);
            if(temp == RT_NULL){
                rt_kprintf("car thread create failed\n");
                return -1;
            }
            else {
                rt_thread_startup(temp);
            }
        }
    }
    return 0;
}
MSH_CMD_EXPORT(parking_demo, parking lot demo);
