/******************************************************************************
 * Copyright (C) 2025
 * 
 * All Rights Reserved.
 * 
 * @file thread_study.c
 * 
 * @par dependencies 
 * - rtthread.h
 * 
 * @author ZHE
 * 
 * @brief rtthread
 * 
 * Processing flow:
 * 
 * Call directly.
 * 
 * @version V1.0 2026-8-18
 *
 * @note 1 tab == 4 spaces!
 * 
 *****************************************************************************/
 

//******************************** Includes *********************************//
#include <rtthread.h>
#include <rtdevice.h>
#include <stdint.h>
#include <rthw.h>
//******************************** Includes *********************************//

//******************************** Defines **********************************//

//******************************** Defines **********************************//

//******************************** Variables ********************************//
static void key_left_cb(void*params);

volatile static uint8_t  key_pressed_flag = 0;
volatile static uint8_t led1_suspend_flag = 0;


rt_thread_t th_led1_handle;
struct rt_thread th_led2;
rt_thread_t th_monitor_handle;
static rt_uint32_t led2_stack[256]={0};

static void print_one(rt_thread_t t);
static void thread_led1_entry(void *parameter);
static void thread_led2_entry(void *parameter);
static void thread_monitor_entry(void *parameter);

//******************************** Variables ********************************//

//******************************** Functions ********************************//

int thread_example(void)
{

    rt_err_t ret_err=RT_EOK;
    th_led1_handle = rt_thread_create(  "th_led1", 
                                        thread_led1_entry,
                                                     NULL,
                                                    1024,16, 5);
    if(th_led1_handle==NULL)
    {
        rt_kprintf("thread led1 create failed\n");
        // Handle error
        return -1;
    }
    if(RT_EOK!= rt_thread_startup(th_led1_handle))
    {
        rt_kprintf("thread led1 startup failed\n");
        // Handle error
        return -1;
    }
    ret_err = rt_thread_init(&th_led2,"th_led2",thread_led2_entry, NULL,
                                    led2_stack, sizeof(led2_stack), 16, 5);
    if(ret_err!=RT_EOK)
    {
        rt_kprintf("thread led2 init failed\n");
        // Handle error
        return -1;
    }
    if(RT_EOK!= rt_thread_startup(&th_led2))
    {
        rt_kprintf("thread led2 startup failed\n");
        // Handle error
        return -1;
    }
    th_monitor_handle = rt_thread_create("th_monitor",
                                         thread_monitor_entry,
                                         NULL,
                                         1024, 10, 5);
    if(th_monitor_handle==NULL)
    {
        rt_kprintf("thread monitor create failed\n");
        // Handle error
        return -1;
    }
    if(RT_EOK!= rt_thread_startup(th_monitor_handle))
    {
        rt_kprintf("thread monitor startup failed\n");
        // Handle error
        return -1;
    }
    return 0;
}


static void thread_led1_entry(void *parameter)
{
    rt_base_t led1_pin = rt_pin_get("PF.12");
    rt_uint32_t count = 0;

    if (led1_pin < 0)
    {
        rt_kprintf("led1 pin get failed\n");
        return;
    }
    rt_pin_mode(led1_pin, PIN_MODE_OUTPUT);

    for(;;)
    {
        if(led1_suspend_flag)
        {
            led1_suspend_flag=0;
            rt_pin_write(led1_pin, PIN_HIGH);
            rt_thread_suspend(th_led1_handle);
        }
        count++;
        rt_kprintf("led1 count: %u, tick: %u\n", count, rt_tick_get());
        rt_pin_write(led1_pin, PIN_HIGH);
        rt_thread_mdelay(1000);
        rt_pin_write(led1_pin, PIN_LOW);
        rt_thread_mdelay(1000);
    }
}
static void thread_led2_entry(void *parameter)
{
    rt_base_t led2_pin = rt_pin_get("PF.11");
    rt_uint32_t count = 0;
    if(led2_pin<0)
    {
        rt_kprintf("led2 pin get failed\n");
        return;
    }
    rt_pin_mode(led2_pin, PIN_MODE_OUTPUT);
    for(;;)
    {
        count++;
        rt_kprintf("led2 count: %u, tick: %u\n", count, rt_tick_get());
        rt_pin_write(led2_pin, PIN_LOW);
        rt_thread_mdelay(1000);
        rt_pin_write(led2_pin, PIN_HIGH);
        rt_thread_mdelay(1000);
    }
}
static void thread_monitor_entry(void *parameter)
{

//    rt_thread_t thread;
//    rt_uint8_t *p;
//    rt_uint32_t free_size;


    /******KEY pin ********/
    rt_base_t key_left_pin= rt_pin_get("PC.0");
    if(key_left_pin<0)
    {
        rt_kprintf("key left pin get failed\n");
        return;
    }
    rt_pin_mode(key_left_pin, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(key_left_pin, PIN_IRQ_MODE_FALLING, key_left_cb, NULL);
    rt_pin_irq_enable(key_left_pin, PIN_IRQ_ENABLE);

    /******KEY pin ********/
    while (1)
    {
        if(key_pressed_flag)
        {
            static int cnt=0;
            cnt++;
            key_pressed_flag = 0;
            if(cnt%2!=0)
            {
                //
                led1_suspend_flag=1;
                
            }
            else
            {
                led1_suspend_flag = 0;
                rt_thread_resume(th_led1_handle);
            }

        }
        /* stop scheduler so the object list won't change while iterating */
        rt_enter_critical();
        print_one(th_led1_handle);
        print_one(&th_led2);
        rt_exit_critical();
        rt_thread_mdelay(2000);
    }
}

static rt_tick_t last_tick = 0;
static void key_left_cb(void *params)
{
    if (rt_tick_get() - last_tick > RT_TICK_PER_SECOND / 20) { // 50ms����
        key_pressed_flag = 1;
        last_tick = rt_tick_get();
    }
}

static void print_one(rt_thread_t t)
{
    const char *stat_str = "unknown";
    rt_uint32_t free_size = 0;
    rt_uint8_t *p;

    switch (t->stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:    stat_str = "init";    break;
    case RT_THREAD_READY:   stat_str = "ready";   break;
    case RT_THREAD_SUSPEND: stat_str = "suspend"; break;
    case RT_THREAD_RUNNING: stat_str = "running"; break;
    case RT_THREAD_CLOSE:   stat_str = "close";   break;
    }

    p = (rt_uint8_t *)t->stack_addr;
    while ((free_size < t->stack_size) && (*p == '#'))
    {
        p++;
        free_size++;
    }

    rt_kprintf("%-*.*s %-3d %-9s %-8u %u\n",
                RT_NAME_MAX, RT_NAME_MAX, t->name,
                t->current_priority, stat_str,
                t->stack_size, free_size);
}

static int thread_dump(int argc, char **argv)
{

    if (th_led1_handle == RT_NULL)
    {
        rt_kprintf("threads not started\n");
        return -1;
    }
    rt_kprintf("\n%-12s %-3s %-9s %-8s %s\n",
                "thread", "pri", "status", "stack", "free");
    print_one(th_led1_handle);
    print_one(&th_led2);
    return 0;
}


MSH_CMD_EXPORT(thread_dump, dump th_led1 and th_led2 status);
INIT_APP_EXPORT(thread_example);
//******************************** Functions ********************************//
