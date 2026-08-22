#include <rtthread.h>

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)

static rt_event_t startup_event = RT_NULL;

static void net_entry(void *parameter){
    rt_thread_mdelay(1000);
    rt_event_send(startup_event, EVENT_NET_READY);
    rt_kprintf("net ready\n");
}

static void sensor_entry(void *parameter){
    rt_thread_mdelay(2000);
    rt_event_send(startup_event, EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
}

static void storage_entry(void *parameter){
    rt_thread_mdelay(5000);
    rt_event_send(startup_event, EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
}

static void main_business_entry(void *parameter){
    rt_uint32_t received;
    rt_err_t temp = rt_event_recv(startup_event, EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, rt_tick_from_millisecond(3000), &received);
    if(temp == RT_EOK){
        rt_kprintf("all modules are ready\n business task started\n");
    }
    else if (temp == -RT_ETIMEOUT) {
        if((startup_event->set & EVENT_NET_READY) == 0){
            rt_kprintf("wait modules timeout, net is not ready\n");
        }
        if((startup_event->set & EVENT_SENSOR_READY) == 0){
            rt_kprintf("wait modules timeout, sensor is not ready\n");
        }
        if((startup_event->set & EVENT_STORAGE_READY) == 0){
            rt_kprintf("wait modules timeout, storage is not ready\n");
        }
        rt_event_recv(startup_event, EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &received);
        rt_kprintf("all modules are ready\n business task started\n");
    }

    //清理
    rt_event_delete(startup_event);
    startup_event = RT_NULL;
}

static int startup_demo(void){
    if (startup_event != RT_NULL)
    {
        rt_kprintf("startup demo already running\n");
        return -1;
    }
    else {
        startup_event = rt_event_create("startup", RT_IPC_FLAG_PRIO);
        if(startup_event == RT_NULL){
            rt_kprintf("startup event create failed\n");
            return -1;
        }
        else {
            //创建net线程
            rt_thread_t net_thread = rt_thread_create("net", net_entry, (void *)0, 1024, 20, 10);
            if(net_thread == RT_NULL){
               rt_kprintf("net thread create failed\n");
               return -1;
            }
            else {
               rt_thread_startup(net_thread);
            }

            //创建sensor线程
            rt_thread_t sensor_thread = rt_thread_create("sensor", sensor_entry, (void *)0, 1024, 20, 10);
            if(sensor_thread == RT_NULL){
               rt_kprintf("sensor thread create failed\n");
               return -1;
            }
            else {
               rt_thread_startup(sensor_thread);
            }

            //创建storage线程
            rt_thread_t storage_thread = rt_thread_create("storage", storage_entry, (void *)0, 1024, 20, 10);
            if(storage_thread == RT_NULL){
               rt_kprintf("storage thread create failed\n");
               return -1;
            }
            else {
               rt_thread_startup(storage_thread);
            }

            //创建main线程
            rt_thread_t main_thread = rt_thread_create("main", main_business_entry, (void *)0, 1024, 20, 10);
            if(main_thread == RT_NULL){
               rt_kprintf("main thread create failed\n");
               return -1;
            }
            else {
               rt_thread_startup(main_thread);
            }
        }
    }

    return 0;
}
MSH_CMD_EXPORT(startup_demo, a startup demo);
