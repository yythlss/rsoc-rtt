/******************************************************************************
 * Copyright (C) 2025
 *
 * All Rights Reserved.
 *
 * @file packing_space_manager.c
 *
 * @par dependencies
 * - rtthread.h
 * - rtdevice.h
 * - stdint.h
 * - stdlib.h
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

//******************************** Defines **********************************//

//******************************** Variables ********************************//
static rt_sem_t pack_space_sem = RT_NULL;
static rt_thread_t pack_space_thread = RT_NULL;
static volatile uint8_t use_pack_space=0;
static void pack_space_manager_thread_entry(void *param);
//******************************** Variables ********************************//

//******************************** Functions ********************************//




static int parking_demo(void)
{
    pack_space_sem = rt_sem_create("pack_sem", 3, RT_IPC_FLAG_PRIO);
    if (pack_space_sem == RT_NULL)
    {
        rt_kprintf("pack_space_sem create failed!\n");
    }
    char name[8];
    for(int i=0;i<5;i++)
    {
        rt_snprintf(name, sizeof(name), "pack%d", i);
        pack_space_thread = rt_thread_create(name,
                                        pack_space_manager_thread_entry,
                                                               (void*)i,
                                                                   1024,
                                                                     20,
                                                                    10);
        if(pack_space_thread != RT_NULL)
        {
            rt_thread_startup(pack_space_thread);
        }
        else
        {
            rt_kprintf("pack_space_thread create failed!\n");
        }
    }                              
       return RT_EOK;
}



static void pack_space_manager_thread_entry(void *param)
{
    rt_uint32_t thread_id = (uint32_t)param;
    char name[20];

    rt_err_t ret =RT_EOK;
    for(;;)
    {
        rt_snprintf(name, sizeof(name), "car%d", thread_id);
        ret = rt_sem_take(pack_space_sem, RT_TICK_PER_SECOND*2);
        if(RT_EOK == ret)
        {
            rt_enter_critical();
            use_pack_space++;
            rt_kprintf("%s entered, used spaces %d\n", name, use_pack_space);
            rt_exit_critical();
            rt_thread_mdelay(1000+rand()%1000);
            rt_sem_release(pack_space_sem);
            rt_enter_critical();
            use_pack_space--;
            rt_kprintf("%s left\n", name);
            rt_exit_critical();
            rt_thread_mdelay(1000+rand()%1000);
        }
        else if(RT_ETIMEOUT == ret)
        {
            rt_kprintf("%s waiting for a space\n", name);
        }
        rt_thread_mdelay(500);
    }
}



MSH_CMD_EXPORT(parking_demo,parking_demo );
// INIT_APP_EXPORT(parking_demo);
//******************************** Functions ********************************//
