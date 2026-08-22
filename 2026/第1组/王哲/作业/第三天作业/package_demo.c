/******************************************************************************
 * Copyright (C) 2025
 *
 * All Rights Reserved.
 *
 * @file package_demo.c
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

//******************************** Defines **********************************//
//******************************** Variables ********************************//

static rt_thread_t package1_handle   =    RT_NULL;
static rt_thread_t package2_handle   =    RT_NULL;
static rt_thread_t      rec_handle   =    RT_NULL;
static rt_mq_t       package_queue   =    RT_NULL;
static rt_mutex_t    package_mutex   =    RT_NULL;
typedef enum
{
    PKG_REGION_END      = 0,
    EAST                = 1,
    SOUTH               = 2,
    NORTH               = 3
}region_t;
struct package_msg
{
    rt_uint32_t id;
    region_t region;
    rt_uint16_t weight;
};
typedef struct package_msg package_msg_t;

static rt_uint32_t g_total, g_east, g_south, g_north, g_weight;
static rt_uint32_t g_send_fail = 0;
//******************************** Variables ********************************//
//******************************** Functions ********************************//
static void rec_entry(void *param);
static void package1_entry(void *param);
static void package2_entry(void *param);

static int package_demo(void)
{
    g_total = g_east = g_south = g_north = g_weight = g_send_fail = 0;
    package_mutex = rt_mutex_create("package_mutex", RT_IPC_FLAG_PRIO);
    if(RT_NULL == package_mutex)
    {
        rt_kprintf("Failed to create package mutex!\n");
        return -RT_ERROR;
    }
    package_queue = rt_mq_create("package_queue",
                      sizeof(struct package_msg),
                                               5,
                                               RT_IPC_FLAG_PRIO);
    if(RT_NULL == package_queue)
    {
        rt_kprintf("Failed to create package queue!\n");
        return -RT_ERROR;
    }
    package1_handle = rt_thread_create("package1_thread",
                                          package1_entry,
                                                 RT_NULL,
                                                    1024,
                                                      20,
                                                      10);
    
    if (RT_NULL == package1_handle)
    {
        rt_kprintf("thread create failed\n");
        return -RT_ERROR;
    }
    package2_handle = rt_thread_create("package2_thread",
                                          package2_entry,
                                                 RT_NULL,
                                                    1024,
                                                      20,
                                                      10);
    
    if (RT_NULL == package2_handle)
    {
        rt_kprintf("thread create failed\n");
        return -RT_ERROR;
    }
    rec_handle = rt_thread_create("rec_thread",
                                   rec_entry,
                                     RT_NULL,
                                        1024,
                                          20,
                                           10);
    if (RT_NULL == rec_handle)
    {
        rt_kprintf("thread create failed\n");
        return -RT_ERROR;
    }
    rt_thread_startup(package1_handle);
    rt_thread_startup(package2_handle);
    rt_thread_startup(rec_handle);
    return RT_EOK;

}


static void package1_entry(void *param)
{
    struct package_msg msg;
    for(int i = 0; i < 6; i++)
    {
        // Package 1 logic here
        msg.id = 2000+i;
        msg.region = (region_t)(rand() % 3 + 1);
        msg.weight = rand() % 100 + 1;
        if(i == 5)
        {
          msg.region = PKG_REGION_END;
          msg.weight = 0;  
        }

        while (RT_EOK != (rt_mq_send(package_queue, &msg, sizeof(package_msg_t))))
        {
            if (RT_EOK == rt_mutex_take(package_mutex, RT_WAITING_FOREVER))
            {
                g_send_fail++;
                rt_mutex_release(package_mutex);
            }
            rt_thread_mdelay(10);
        }
        //rt_kprintf("queue send success\n");
    }
}
static void package2_entry(void *param)
{
    package_msg_t msg;
    for(int i = 0; i < 6; i++)
    {
        msg.id = 1000+i;
        msg.region = (region_t)(rand() % 3 + 1);
        msg.weight = rand() % 100 + 1;
        if(i == 5)
        {
          msg.region = PKG_REGION_END;
          msg.weight = 0;  
        }
        while (RT_EOK != (rt_mq_send(package_queue, &msg, sizeof(package_msg_t))))
        {
            if (RT_EOK == rt_mutex_take(package_mutex, RT_WAITING_FOREVER))
            {
                g_send_fail++;
                rt_mutex_release(package_mutex);
            }
            rt_thread_mdelay(10);
        }
        //rt_kprintf("queue send success\n");
    }
}

static void rec_entry(void *param)
{
    package_msg_t msg;
    rt_uint32_t finish_cnt = 0;

    for(;;)
    {
        if(rt_mq_recv(package_queue, &msg, sizeof(package_msg_t), RT_WAITING_FOREVER))
        {
            if (msg.region == PKG_REGION_END)
            {
                finish_cnt++;
                if (finish_cnt == 2)
                {
                    rt_kprintf("All packages: total=%d, east=%d, south=%d, north=%d, weight=%d g, send_fail=%d\n",
                               g_total, g_east, g_south, g_north, g_weight, g_send_fail);
                    break;
                }
                continue;
            }

            if (msg.region == EAST)       g_east++;
            else if (msg.region == SOUTH) g_south++;
            else if (msg.region == NORTH) g_north++;
            g_total++;
            g_weight += msg.weight;
            rt_kprintf("message received: id=%d, region=%d, weight=%d\n", msg.id, msg.region, msg.weight);
        }
    }
}

MSH_CMD_EXPORT( package_demo,package_demo);
//INIT_APP_EXPORT(thread_example);
//******************************** Functions ********************************//
