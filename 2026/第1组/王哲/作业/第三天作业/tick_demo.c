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
 *
 * @author ZHE
 *
 * @brief packing_space_manager
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
#define WINDOW_TICKET_COUNT 20
//******************************** Defines **********************************//
//******************************** Variables ********************************//
static void window_tick_entry(void *param);

static  rt_mutex_t window_mutex_handle      =   RT_NULL;
static rt_thread_t window_A_thread_handle   =   RT_NULL;
static rt_thread_t window_B_thread_handle   =   RT_NULL;
static rt_uint8_t window_ticket_count = WINDOW_TICKET_COUNT;

//******************************** Variables ********************************//
//******************************** Functions ********************************//


static int ticket_demo(void)
{
    window_mutex_handle = rt_mutex_create("window_mutex", RT_IPC_FLAG_PRIO);
    if (window_mutex_handle == RT_NULL)
    {
        rt_kprintf("window_mutex_create failed!\n");
        return -1;
    }
    window_A_thread_handle = rt_thread_create("window_A",
                                                   window_tick_entry,
                                                          (void*)'A',
                                                                1024,
                                                                    20,
                                                                    10);
    if (window_A_thread_handle == RT_NULL)
    {
        rt_kprintf("window_A_thread_create failed!\n");
        return -1;
    }
    window_B_thread_handle = rt_thread_create("window_B",
                                                   window_tick_entry,
                                                          (void*)'B',
                                                                1024,
                                                                  20,
                                                                    10);
    if (window_B_thread_handle == RT_NULL)
    {
        rt_kprintf("window_B_thread_create failed!\n");
        return -1;
    }
    if(rt_thread_startup(window_A_thread_handle) != RT_EOK)
    {
        rt_kprintf("window_A_thread_startup failed!\n");
        return -1;
    }
    if(rt_thread_startup(window_B_thread_handle) != RT_EOK)
    {
        rt_kprintf("window_B_thread_startup failed!\n");
        return -1;
    }
    return 0;
}
static void window_tick_entry(void *param)
{
    char window_name = (char)(rt_uintptr_t)param;
    static int count_zero_flag=1;
    for(;;)
    {
        // if(RT_EOK == rt_mutex_take(window_mutex_handle,RT_WAITING_FOREVER))
        // {
            if(window_ticket_count > 0)
            {
                rt_kprintf("window_%c sold ticket %d\n", window_name, 
                                                          window_ticket_count);
                window_ticket_count--;
                rt_thread_mdelay(100+rand()%100);
//                if(rt_mutex_release(window_mutex_handle) != RT_EOK)
//                {
//                    rt_kprintf("window_%c rt_mutex_release failed!\n",
//                                                                  window_name);
//                }
            }
            else
            {
                if(count_zero_flag)
                {
                    rt_kprintf("all tickets sold\nremaining tickets: 0\n");
                    count_zero_flag=0;
                }
                // rt_mutex_release(window_mutex_handle);
                break;
            }
        // }
        // else
        // {
        //     rt_mutex_release(window_mutex_handle);
        //     rt_kprintf("window_%c rt_mutex_take failed!\n", window_name);
        //     break;
        // }
            rt_thread_mdelay(1000+rand()%1000);
    }
}

MSH_CMD_EXPORT( ticket_demo,ticket_demo);
//INIT_APP_EXPORT(thread_example);
//******************************** Functions ********************************//
