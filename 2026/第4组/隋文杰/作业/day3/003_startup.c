#include <rtthread.h>

#define EVENT_NET_READY     (1U << 0)
#define EVENT_SENSOR_READY  (1U << 1)
#define EVENT_STORAGE_READY (1U << 2)

static rt_event_t system_event = RT_NULL;

static void net_init_thread(void *param)
{
    rt_thread_mdelay(1000);
    rt_kprintf("network ready\n");
    rt_event_send(system_event, EVENT_NET_READY);
}

static void sensor_init_thread(void *param)
{
    rt_thread_mdelay(2000);
    rt_kprintf("sensor ready\n");
    rt_event_send(system_event, EVENT_SENSOR_READY);
}

static void storage_init_thread(void *param)
{
    rt_thread_mdelay(5000);
    rt_kprintf("storage ready\n");
    rt_event_send(system_event, EVENT_STORAGE_READY);
}

static void main_business_thread(void *param)
{
    rt_uint32_t recved_events = 0;
    rt_uint32_t wait_events = EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY;

    while (1)
    {
        rt_int32_t ret = rt_event_recv(system_event,
                                       wait_events,
                                       RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                       3000,
                                       &recved_events);

        if (ret == -RT_ETIMEOUT)
        {
            rt_kprintf("wait modules timeout, ");
            if (!(recved_events & EVENT_NET_READY))     rt_kprintf("network is not ready ");
            if (!(recved_events & EVENT_SENSOR_READY))  rt_kprintf("sensor is not ready ");
            if (!(recved_events & EVENT_STORAGE_READY)) rt_kprintf("storage is not ready ");
            rt_kprintf("\n");
        }
        else if (ret == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
    }
}

int system_startup_check(void)
{
    system_event = rt_event_create("sys_evt", RT_IPC_FLAG_FIFO);
    if (system_event == RT_NULL)
    {
        rt_kprintf("Failed to create event!\n");
        return -1;
    }

    rt_thread_t tid_net = rt_thread_create("net", net_init_thread, RT_NULL, 512, 10, 10);
    if (tid_net != RT_NULL) rt_thread_startup(tid_net);

    rt_thread_t tid_sensor = rt_thread_create("sensor", sensor_init_thread, RT_NULL, 512, 10, 10);
    if (tid_sensor != RT_NULL) rt_thread_startup(tid_sensor);

    rt_thread_t tid_storage = rt_thread_create("storage", storage_init_thread, RT_NULL, 512, 10, 10);
    if (tid_storage != RT_NULL) rt_thread_startup(tid_storage);

    rt_thread_t tid_main = rt_thread_create("main_biz", main_business_thread, RT_NULL, 512, 11, 10);
    if (tid_main != RT_NULL) rt_thread_startup(tid_main);

    return 0;
}

MSH_CMD_EXPORT(startup_demo,startup_demo);

int main(void)
{

    return 0;
}

