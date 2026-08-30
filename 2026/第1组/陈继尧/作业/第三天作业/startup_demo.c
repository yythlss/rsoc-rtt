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
 * 程序清单：事件例程
 *
 * 程序会初始化2个线程及初始化一个静态事件对象
 * 一个线程等待于事件对象上，以接收事件；
 * 一个线程发送事件 (事件3/事件5)
*/
#include <rtthread.h>

#define THREAD_PRIORITY      9
#define THREAD_TIMESLICE     5

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)

/* 事件控制块 */
static struct rt_event event;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char thread1_stack[1024];
static struct rt_thread thread1;

/* ================== 主业务线程（接收方） ================== */
static void thread1_recv_event(void *param)
{
    rt_uint32_t e;
    rt_err_t result;

    /* 第一次接收事件，接收完后清除事件标志 */
    while(1)
    {
        result= rt_event_recv(&event, (EVENT_NET_READY | EVENT_SENSOR_READY |EVENT_STORAGE_READY ),
                              RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                              rt_tick_from_millisecond(3000), &e);




    if (result==RT_EOK)
    {
        rt_kprintf("all modules are ready\n");

        rt_kprintf("business task started\n");
        break;
    }
    else if (result == -RT_ETIMEOUT)
   {
        rt_kprintf("wait modules timeout, ");

        rt_uint32_t current_flags = event.set;
        if ((current_flags & EVENT_STORAGE_READY) == 0)
            {

             rt_kprintf("storage is not ready\n");

            }

      }

    }
}

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char thread2_stack[1024];
static struct rt_thread thread2;

static char thread3_stack[1024];
static struct rt_thread thread3;

static char thread4_stack[1024];
static struct rt_thread thread4;

/* 线程2入口 */
static void Function_send_event(void *param)
{

    int Fid=(int)param;


        switch (Fid)
       {
        case 2:
                rt_thread_mdelay(200);
                rt_kprintf(" network ready\n");
                rt_event_send(&event, EVENT_NET_READY);

                break;

        case 3:
                rt_thread_mdelay(200);
                rt_kprintf("sensor ready\n");
                rt_event_send(&event, EVENT_SENSOR_READY );

                break;

        case 4:
                rt_thread_mdelay(5000);
                rt_kprintf(" storage ready\n");
                rt_event_send(&event, EVENT_STORAGE_READY);

                break;
        }

}

int event_sample(void)
{
    rt_err_t result;

    /* 初始化事件对象 */
    result = rt_event_init(&event, "event", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        rt_kprintf("init event failed.\n");
        return -1;
    }

    rt_thread_init(&thread1,
                   "thread1",
                   thread1_recv_event,
                   RT_NULL,
                   &thread1_stack[0],
                   sizeof(thread1_stack),
                   THREAD_PRIORITY - 1, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread1, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread1);

    rt_thread_init(&thread2,
                   "NET",
                   Function_send_event,
                   (void*)2,
                   &thread2_stack[0],
                   sizeof(thread2_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread2, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread2);


    rt_thread_init(&thread3,
                   "SENSOR",
                   Function_send_event,
                   (void*)3,
                   &thread3_stack[0],
                   sizeof(thread3_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread3, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread3);

    rt_thread_init(&thread4,
                   "STORAGE",
                   Function_send_event,
                   (void*)4,
                   &thread4_stack[0],
                   sizeof(thread4_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread4, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread4);




    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT_ALIAS(event_sample,startup_demo, event sample);
