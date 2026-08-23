#define DBG_TAG "AHT10"
#define DBG_LVL DBG_LOG
#include <ulog.h>

#include <rtthread.h>
#include <aht10.h>

float humi ,temp;

int aht10_read_entry(void *par)
{
    aht10_device_t aht10 = aht10_init("i2c3");
    if(aht10 == RT_NULL)
    {
        LOG_E("aht10 init failed!\n");
        return -RT_ERROR;
    }

    while(1)
    {
        temp = aht10_read_temperature(aht10);
        humi = aht10_read_humidity(aht10);

        //rt_kprintf("temp = %.2f,humi = %.2f",temp,humi);
        rt_thread_mdelay(3000);
    }
}

int aht10_thread_init(void)
{
    rt_thread_t aht10_thread = rt_thread_create("aht10", aht10_read_entry, RT_NULL, 1024, 10, 10);
    if(aht10_thread == RT_NULL)
    {
        LOG_E("aht10 thread create failed!\n");
        return RT_ERROR;
    }
    rt_thread_startup(aht10_thread);
    return RT_EOK;
}
MSH_CMD_EXPORT(aht10_thread_init,aht10_thread_init);
