/*
 * Copyright (c) 2006-2022, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-08-24     yangjie      the first version
 */

/*
 * 程序清单：信号量例程
 *
 * 该例程创建了一个动态信号量，初始化两个线程，线程1在count每计数10次时，
 * 发送一个信号量，线程2在接收信号量后，对number进行加1操作
 */
#include <rtthread.h>

#define THREAD_PRIORITY         25
#define THREAD_TIMESLICE        5

/* 指向信号量的指针 */
static rt_sem_t dynamic_sem = RT_NULL;


#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char thread1_stack[1024];
static struct rt_thread thread1;

ALIGN(RT_ALIGN_SIZE)
static char thread2_stack[1024];
static struct rt_thread thread2;

ALIGN(RT_ALIGN_SIZE)
static char thread3_stack[1024];
static struct rt_thread thread3;

ALIGN(RT_ALIGN_SIZE)
static char thread4_stack[1024];
static struct rt_thread thread4;

ALIGN(RT_ALIGN_SIZE)
static char thread5_stack[1024];
static struct rt_thread thread5;

static void rt_thread_entry(void *parameter)
{
    int car_id = (int)parameter;
    static rt_err_t result;
    //static rt_uint8_t number = 0;
    while (1)
    {

        result = rt_sem_take(dynamic_sem, rt_tick_from_millisecond(2000));
        if (result != RT_EOK)
        {
            rt_kprintf("car%d waiting for a space\n",car_id);
            rt_thread_mdelay(3000);
        }
        else
        {
            //number++;
            rt_kprintf("car%d entered, used spaces: %d\n",car_id, 3-dynamic_sem->value);

            rt_thread_mdelay(car_id*1000);

            rt_sem_release(dynamic_sem);

            rt_kprintf("car%d left\n",car_id);

            rt_thread_mdelay(3000);
        }

    }
}


/* 信号量示例的初始化 */
int semaphore_sample()
{
    /* 创建一个动态信号量，初始值是3*/
    dynamic_sem = rt_sem_create("dsem", 3, RT_IPC_FLAG_PRIO);
    if (dynamic_sem == RT_NULL)
    {
        rt_kprintf("create dynamic semaphore failed.\n");
        return -1;
    }
    else
    {
        rt_kprintf("create done. dynamic semaphore value = 3.\n");
    }

    rt_thread_init(&thread1,
                   "car1",
                   rt_thread_entry,
                   (void*)1,
                   &thread1_stack[0],
                   sizeof(thread1_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread1, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread1);

    rt_thread_init(&thread2,
                   "car2",
                   rt_thread_entry,
                   (void*)2,
                   &thread2_stack[0],
                   sizeof(thread2_stack),
                   THREAD_PRIORITY - 1, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread2, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread2);

    rt_thread_init(&thread3,
                   "car3",
                   rt_thread_entry,
                   (void*)3,
                   &thread3_stack[0],
                   sizeof(thread3_stack),
                   THREAD_PRIORITY - 2, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread3, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread3);


    rt_thread_init(&thread4,
                   "car4",
                   rt_thread_entry,
                   (void*)4,
                   &thread4_stack[0],
                   sizeof(thread4_stack),
                   THREAD_PRIORITY - 3, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread4, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread4);


    rt_thread_init(&thread5,
                   "car5",
                   rt_thread_entry,
                   (void*)5,
                   &thread5_stack[0],
                   sizeof(thread5_stack),
                   THREAD_PRIORITY - 4, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread5, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread5);





    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT_ALIAS(semaphore_sample, parking_demo, 5 cars parking demo);

