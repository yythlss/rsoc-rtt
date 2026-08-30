/******************************************************************************
 * Copyright (C) 2025
 *
 * All Rights Reserved.
 *
 * @file muti_window_sys.c
 *
 * @par dependencies
 * - rtthread.h
 * - rtdevice.h
 * - stdint.h
 * - stdlib.h
 * 
 * @author ZHE
 *
 * @brief startup_demo
 *
 * Processing flow:
 *
 * Call directly.
 *
 * @version V1.0 2026-8-19
 *
 * @note 1 tab == 4 spaces!
 *
 *****************************************************************************/


//******************************** Includes *********************************//
#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>
#include <stdlib.h>
//******************************** Includes *********************************//

//******************************** Defines **********************************//
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
//******************************** Defines **********************************//
//******************************** Variables ********************************//
static rt_thread_t net_thread_handle             =   RT_NULL;
static rt_thread_t sensor_thread_handle          =   RT_NULL;
static rt_thread_t storage_thread_handle         =   RT_NULL;
static rt_thread_t application_thread_handle     =   RT_NULL;
static rt_event_t startup_ev                     =   RT_NULL;

//******************************** Variables ********************************//
//******************************** Functions ********************************//


static void net_entry(void *param);
static void sensor_entry(void *param);
static void storage_entry(void *param);
static void application_entry(void *param);


static int startup_demo(void)
{
    startup_ev=rt_event_create("startup_ev", RT_IPC_FLAG_PRIO);
   if (!startup_ev) {
        rt_kprintf("event create failed\n");
        return -RT_ERROR;
    }


    net_thread_handle     = rt_thread_create("net",     
                                           net_entry,     
                                             RT_NULL, 
                                                4096, 
                                                  20,
                                                   10);
    sensor_thread_handle  = rt_thread_create("sensor",  
                                         sensor_entry,  
                                              RT_NULL, 
                                                 4096, 
                                                   20, 
                                                    10); 
    storage_thread_handle = rt_thread_create("storage",
                                         storage_entry, 
                                               RT_NULL, 
                                                  4096,
                                                    20, 
                                                    10);
    application_thread_handle = rt_thread_create("app", 
                                     application_entry,
                                               RT_NULL,
                                                  4096, 
                                                    20, 
                                                    10);

    if (    
        !net_thread_handle                   || 
        !sensor_thread_handle                || 
        !storage_thread_handle               || 
        !application_thread_handle
        ) 
    {
        rt_kprintf("thread create failed\n");
        return -RT_ERROR;
    }

    rt_thread_startup(net_thread_handle);
    rt_thread_startup(sensor_thread_handle);
    rt_thread_startup(storage_thread_handle);
    rt_thread_startup(application_thread_handle);
    return RT_EOK;

}


static void net_entry(void *param)
{

    rt_thread_mdelay(100);
    if(RT_EOK != rt_event_send(startup_ev, EVENT_NET_READY))
    {
        rt_kprintf("Failed to send net ready event!\n");
    }
    rt_kprintf("network ready\n");
//    rt_thread_delete(rt_thread_self());

}
static void sensor_entry(void *param)
{

    rt_thread_mdelay(200);
    if(RT_EOK != rt_event_send(startup_ev, EVENT_SENSOR_READY))
    {
        rt_kprintf("Failed to send sensor ready event!\n");
    }
    rt_kprintf("sensor ready\n");

//    rt_thread_delete(rt_thread_self());
}
static void storage_entry(void *param)
{


    rt_thread_mdelay(RT_TICK_PER_SECOND*5);
    if(RT_EOK != rt_event_send(startup_ev, EVENT_STORAGE_READY))
    {
        rt_kprintf("Failed to send storage ready event!\n");
    }
    rt_kprintf("storage ready\n");

//    rt_thread_delete(rt_thread_self());

}
static void application_entry(void *param)
{
    rt_uint32_t e = 0;
    rt_err_t ret=RT_EOK;
    for(;;)
    {
        ret = rt_event_recv(startup_ev,
                                           EVENT_NET_READY  |
                                        EVENT_SENSOR_READY  |
                                        EVENT_STORAGE_READY  ,
                                          RT_EVENT_FLAG_AND | 
                                        RT_EVENT_FLAG_CLEAR  ,
                                            RT_TICK_PER_SECOND*3,
                                                                &e);
        if(RT_EOK == ret)
        {
            rt_kprintf("all modules are ready\nbusiness task started\n");
            break;
        }
        else if(-RT_ETIMEOUT == ret)
        {
            rt_kprintf("wait modules timeout");
            if (!(e & EVENT_NET_READY))      rt_kprintf(", net");
            if (!(e & EVENT_SENSOR_READY))   rt_kprintf(", sensor");
            if (!(e & EVENT_STORAGE_READY))  rt_kprintf(", storage");
            rt_kprintf(" is not ready\n");
            continue;
        }
        else
        {
            rt_kprintf("wait modules error\n");
            break;
        }
        rt_thread_mdelay(100+rand()%100);
    }
}
MSH_CMD_EXPORT( startup_demo,startup_demo);
//INIT_APP_EXPORT(thread_example);
//******************************** Functions ********************************//
