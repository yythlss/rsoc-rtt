/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     yanran       the first version
 */

#include <rtthread.h>

#define TASK1_THREAD_PRIORITY         25
#define TASK1_THREAD_TIMESLICE        5
static rt_sem_t task1_sem = RT_NULL;

static char thread1_stack[1024];
static struct rt_thread thread1;

static void rt_thread1_entry(void *parameter)
{
    static rt_err_t result;
    static rt_uint8_t number = 0;
    while(1)
    {

        if(0 == number)
        {

            result = rt_sem_take(task1_sem, rt_tick_from_millisecond(2000));
            if (result == -RT_ETIMEOUT)
            {
               rt_kprintf("car1 waiting for a space\n");
               rt_thread_mdelay(1000);
            }
            else if (result == RT_EOK)
            {
                number = 1;
                rt_kprintf("car1 entered, used spaces: %d\n",3 - (task1_sem->value));
                rt_thread_mdelay(2000);
            }
        }


        if(number == 1)
        {
            number = 0;
            rt_sem_release(task1_sem);
            rt_kprintf("car1 left\n");
            rt_thread_mdelay(500);
        }
    }
}

static char thread2_stack[1024];
static struct rt_thread thread2;
static void rt_thread2_entry(void *parameter)
{
    static rt_err_t result;
    static rt_uint8_t number = 0;
    while(1)
    {

        if(0 == number)
        {

            result = rt_sem_take(task1_sem, rt_tick_from_millisecond(2000));
            if (result == -RT_ETIMEOUT)
            {
               rt_kprintf("car2 waiting for a space\n");
               rt_thread_mdelay(2000);
            }
            else if (result == RT_EOK)
            {
                number = 1;
                rt_kprintf("car2 entered, used spaces: %d\n",3 - (task1_sem->value));
                rt_thread_mdelay(4000);
            }
        }

        if(number == 1)
        {
            number = 0;
            rt_sem_release(task1_sem);
            rt_kprintf("car2 left\n");
            rt_thread_mdelay(1000);
        }
    }
}

static char thread3_stack[1024];
static struct rt_thread thread3;
static void rt_thread3_entry(void *parameter)
{
    static rt_err_t result;
    static rt_uint8_t number = 0;
    while(1)
    {

        if(0 == number)
        {
            
            result = rt_sem_take(task1_sem, rt_tick_from_millisecond(2000));
            if (result == -RT_ETIMEOUT)
            {
               rt_kprintf("car3 waiting for a space\n");
               rt_thread_mdelay(2000);

            }
            else if (result == RT_EOK)
            {
                number = 1;
                rt_kprintf("car3 entered, used spaces: %d\n",3 - (task1_sem->value));
                rt_thread_mdelay(5000);
            }
        }

        if(number == 1)
        {
            number = 0;
            rt_sem_release(task1_sem);
            rt_kprintf("car3 left\n");
            rt_thread_mdelay(1000);
        }
    }
}
//
static char thread4_stack[1024];
static struct rt_thread thread4;
static void rt_thread4_entry(void *parameter)
{
    static rt_err_t result;
    static rt_uint8_t number = 0;
    while(1)
    {

        if(0 == number)
        {
           
            result = rt_sem_take(task1_sem, rt_tick_from_millisecond(2000));
            if (result == -RT_ETIMEOUT)
            {
               rt_kprintf("car4 waiting for a space\n");
               rt_thread_mdelay(2000);
            }
            else if (result == RT_EOK)
            {
                number = 1;
                rt_kprintf("car4 entered, used spaces: %d\n",3 - (task1_sem->value));
                rt_thread_mdelay(3000);
            }
        }


        if(number == 1)
        {
            number = 0;
            rt_sem_release(task1_sem);
            rt_kprintf("car4 left\n");
            rt_thread_mdelay(1000);
        }
    }
}
//
static char thread5_stack[1024];
static struct rt_thread thread5;
static void rt_thread5_entry(void *parameter)
{
    static rt_err_t result;
    static rt_uint8_t number = 0;
    while(1)
    {

        if(0 == number)
        {
            
            result = rt_sem_take(task1_sem, rt_tick_from_millisecond(2000));
            if (result == -RT_ETIMEOUT)
            {
               rt_kprintf("car5 waiting for a space\n");
               rt_thread_mdelay(2000);
            }
            else if (result == RT_EOK)
            {
                number = 1;
                rt_kprintf("car5 entered, used spaces: %d\n",3 - (task1_sem->value));
                rt_thread_mdelay(4000);
            }
        }


        if(number == 1)
        {
            number = 0;
            rt_sem_release(task1_sem);
            rt_kprintf("car5 left\n");
            rt_thread_mdelay(1000);
        }
    }
}

/* 信号量的初始化 */
int parking_demo()
{
    /* 创建一个动态信号量，初始值是3 */
    task1_sem = rt_sem_create("task1_sem", 3, RT_IPC_FLAG_PRIO);
    if (task1_sem == RT_NULL)
    {
        rt_kprintf("create task1_sem semaphore failed.\n");
        return -1;
    }
    else
    {
        rt_kprintf("create done. task1_sem semaphore value = 3.\n");
    }

    rt_thread_init(&thread1,
                   "thread1",
                   rt_thread1_entry,
                   RT_NULL,
                   &thread1_stack[0],
                   sizeof(thread1_stack),
                   TASK1_THREAD_PRIORITY, TASK1_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread1, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread1);

    rt_thread_init(&thread2,
                   "thread2",
                   rt_thread2_entry,
                   RT_NULL,
                   &thread2_stack[0],
                   sizeof(thread2_stack),
                   TASK1_THREAD_PRIORITY, TASK1_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread2, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread2);

    rt_thread_init(&thread3,
                   "thread3",
                   rt_thread3_entry,
                   RT_NULL,
                   &thread3_stack[0],
                   sizeof(thread3_stack),
                   TASK1_THREAD_PRIORITY, TASK1_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread3, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread3);

    rt_thread_init(&thread4,
                   "thread4",
                   rt_thread4_entry,
                   RT_NULL,
                   &thread4_stack[0],
                   sizeof(thread4_stack),
                   TASK1_THREAD_PRIORITY, TASK1_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread4, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread4);

    rt_thread_init(&thread5,
                   "thread5",
                   rt_thread5_entry,
                   RT_NULL,
                   &thread5_stack[0],
                   sizeof(thread5_stack),
                   TASK1_THREAD_PRIORITY, TASK1_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread5, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread5);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(parking_demo, task1 sample);

/***************************************题目二************************************************************/

#define TASK2_THREAD_PRIORITY         8
#define TASK2_THREAD_TIMESLICE        5

/* 指向互斥量的指针 */
static rt_mutex_t dynamic_mutex = RT_NULL;
static rt_uint8_t ticket = 20;

static char task2_thread1_stack[1024];
static struct rt_thread task2_thread1;
static void task2_rt_thread_entry1(void *parameter)
{
    while (1)
    {
        /* 线程1获取到互斥量后，售票，然后释放互斥量 */
        rt_mutex_take(dynamic_mutex, RT_WAITING_FOREVER);

        if(ticket == 0)
        {
            rt_kprintf("all tickets sold\n");
            rt_kprintf("remaining tickets: 0\n");
            break;
        }

        if(ticket > 0)
        {
            rt_kprintf("window A sold ticket %d\n",ticket);
            ticket -= 1;

        }

        rt_mutex_release(dynamic_mutex);
        rt_thread_mdelay(10);
    }

    rt_mutex_release(dynamic_mutex);
}

static char task2_thread2_stack[1024];
static struct rt_thread task2_thread2;
static void task2_rt_thread_entry2(void *parameter)
{
    while (1)
    {
        /* 线程2获取到互斥量后，售票，然后释放互斥量 */
        rt_mutex_take(dynamic_mutex, RT_WAITING_FOREVER);

        if(ticket == 0)
        {
            rt_kprintf("all tickets sold\n");
            rt_kprintf("remaining tickets: 0\n");
            break;
        }
        else if(ticket == 1)
        {
            rt_kprintf("window B sold ticket %d\n",ticket);
            ticket -= 1;

        }

        else
        {
            rt_kprintf("window B sold ticket %d\n",ticket);
            ticket -= 1;
            rt_kprintf("window B sold ticket %d\n",ticket);
            ticket -= 1;

        }

        rt_mutex_release(dynamic_mutex);
        rt_thread_mdelay(10);
    }

    rt_mutex_release(dynamic_mutex);
}

/* 互斥量示例的初始化 */
int ticket_demo(void)
{
    /* 创建一个动态互斥量 */
    dynamic_mutex = rt_mutex_create("dmutex", RT_IPC_FLAG_PRIO);
    if (dynamic_mutex == RT_NULL)
    {
        rt_kprintf("create dynamic mutex failed.\n");
        return -1;
    }

    rt_thread_init(&task2_thread1,
                   "task2_thread1",
                   task2_rt_thread_entry1,
                   RT_NULL,
                   &task2_thread1_stack[0],
                   sizeof(task2_thread1_stack),
                   TASK2_THREAD_PRIORITY, TASK2_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&task2_thread1, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&task2_thread1);

    rt_thread_init(&task2_thread2,
                   "task2_thread2",
                   task2_rt_thread_entry2,
                   RT_NULL,
                   &task2_thread2_stack[0],
                   sizeof(task2_thread2_stack),
                   TASK2_THREAD_PRIORITY, TASK2_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&task2_thread2, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&task2_thread2);
    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(ticket_demo, mutex sample);

/***********************************************第三题*******************************************/

#define task3_THREAD_PRIORITY      9
#define task3_THREAD_TIMESLICE     5

#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)

/* 事件控制块 */
static struct rt_event event;

static char network_thread_stack[1024];
static struct rt_thread network_thread;
/* network线程入口函数 */
static void network_send_event(void *param)
{
    rt_thread_mdelay(1000);
    rt_kprintf("network ready\n");
    rt_event_send(&event, EVENT_NET_READY);
}


static char sensor_thread_stack[1024];
static struct rt_thread sensor_thread;
/* sensor线程入口函数 */
static void sensor_send_event(void *param)
{
    rt_thread_mdelay(2000);
    rt_kprintf("sensor ready\n");
    rt_event_send(&event, EVENT_SENSOR_READY);
}


static char storage_thread_stack[1024];
static struct rt_thread storage_thread;
/* storage线程入口函数 */
static void storage_send_event(void *param)
{
    rt_thread_mdelay(5000);
    rt_kprintf("storage ready\n");
    rt_event_send(&event, EVENT_STORAGE_READY);
}


static char business_thread_stack[1024];
static struct rt_thread business_thread;
/* business线程入口函数 */
static void business_recv_event(void *param)
{
    rt_uint32_t e;
    rt_err_t event_err;

    while(1)
    {
        event_err = rt_event_recv(&event, (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY),
                              RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                              rt_tick_from_millisecond(3000), &e);
        if(event_err == -RT_ETIMEOUT)
        {
            if(!(event.set & EVENT_NET_READY) )
            {
                rt_kprintf("wait modules timeout, network is not ready\n");
            }

            if(!(event.set & EVENT_SENSOR_READY) )
            {
                rt_kprintf("wait modules timeout, sensor is not ready\n");
            }

            if(!(event.set & EVENT_STORAGE_READY) )
            {
                rt_kprintf("wait modules timeout, storage is not ready\n");
            }
        }

        if(event_err == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            rt_event_detach(&event);
            break;
        }

    }
}


int startup_demo(void)
{
    rt_err_t result;

    /* 初始化事件对象 */
    result = rt_event_init(&event, "event", RT_IPC_FLAG_PRIO);
    if (result != RT_EOK)
    {
        rt_kprintf("init event failed.\n");
        return -1;
    }

    rt_thread_init(&network_thread,
                   "network_thread",
                   network_send_event,
                   RT_NULL,
                   &network_thread_stack[0],
                   sizeof(network_thread_stack),
                   task3_THREAD_PRIORITY, task3_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&network_thread, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&network_thread);


    rt_thread_init(&sensor_thread,
                   "sensor_thread",
                   sensor_send_event,
                   RT_NULL,
                   &sensor_thread_stack[0],
                   sizeof(sensor_thread_stack),
                   task3_THREAD_PRIORITY, task3_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&sensor_thread, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&sensor_thread);


    rt_thread_init(&storage_thread,
                   "storage_thread",
                   storage_send_event,
                   RT_NULL,
                   &storage_thread_stack[0],
                   sizeof(storage_thread_stack),
                   task3_THREAD_PRIORITY, task3_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&storage_thread, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&storage_thread);


    rt_thread_init(&business_thread,
                   "business_thread",
                   business_recv_event,
                   RT_NULL,
                   &business_thread_stack[0],
                   sizeof(business_thread_stack),
                   task3_THREAD_PRIORITY, task3_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&business_thread, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&business_thread);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(startup_demo, event sample);

/*******************************************第四题**********************************************/

#define task4_THREAD_PRIORITY      25
#define task4_THREAD_TIMESLICE     10

//region = 1：华东
//region = 2：华南
//region = 3：华北

/* 消息队列控制块 */
static struct rt_messagequeue mq;
/* 消息队列中用到的放置消息的内存池 */
static rt_uint8_t msg_pool[2048];

typedef struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
}package_msg_t;


static char msgsend_thread1_stack[1024];
static struct rt_thread msgsend_thread1;
/* 线程1入口函数 */
static void msgsend_thread1_entry(void *parameter)
{

    package_msg_t packmeg[5] = {{1,1,500} , {2,2,600} , {3,3,700} , {4,1,800} , {5,2,900}};
    package_msg_t * buf = &packmeg[0];
    rt_uint8_t failcnt = 0,sendcnt = 0;
    while (1)
    {
        int result;
        /* 发送消息到消息队列中 */
        result = rt_mq_send(&mq, buf, sizeof(packmeg[0]));
        if (result == -RT_EFULL)
        {
            failcnt++;
            rt_kprintf("发送线程1发送失败次数：%d\n",failcnt);
        }
        if(result == RT_EOK)
        {
            switch(buf->region)
            {
                case 1: rt_kprintf("package %d -> east,weight: %d g\n",buf->id,buf->weight);break;
                case 2: rt_kprintf("package %d -> south,weight: %d g\n",buf->id,buf->weight);break;
                case 3: rt_kprintf("package %d -> north,weight: %d g\n",buf->id,buf->weight);break;
            }
            sendcnt++;
            if(sendcnt == 5)
            {
                break;
            }
            buf ++;
            rt_thread_mdelay(200);
        }
    }
}


static char msgsend_thread2_stack[1024];
static struct rt_thread msgsend_thread2;
/* 线程2入口函数 */
static void msgsend_thread2_entry(void *parameter)
{
    int result;
    package_msg_t packmeg[5] = {{6,3,550} , {7,1,650} , {8,2,750} , {9,3,850} , {10,1,950}};
    package_msg_t * buf = &packmeg[0];
    rt_uint8_t failcnt = 0,sendcnt = 0;
    while (1)
    {
        /* 发送消息到消息队列中 */
        result = rt_mq_send(&mq, buf, sizeof(packmeg[0]));
        if (result == -RT_EFULL)
        {
            failcnt++;
            rt_kprintf("发送线程1发送失败次数：%d\n",failcnt);
        }
        if(result == RT_EOK)
        {
            switch(buf->region)
            {
                case 1: rt_kprintf("package %d -> east,weight: %d g\n",buf->id,buf->weight);break;
                case 2: rt_kprintf("package %d -> south,weight: %d g\n",buf->id,buf->weight);break;
                case 3: rt_kprintf("package %d -> north,weight: %d g\n",buf->id,buf->weight);break;
            }
            sendcnt++;
            if(sendcnt == 5)
            {
                break;
            }
            buf++;
            rt_thread_mdelay(200);
        }
    }
}


static char msgrscv_thread_stack[1024];
static struct rt_thread msgrscv_thread;
/* 分拣线程入口函数 */
static void msgrscv_thread_entry(void *parameter)
{
    rt_uint8_t recvcnt = 0, eastcnt = 0, southcnt = 0, northcnt = 0;
    rt_uint32_t totalweigh = 0;
    package_msg_t packmeg;
    while (1)
    {
        if (rt_mq_recv(&mq, &packmeg, sizeof(packmeg), RT_WAITING_FOREVER) == RT_EOK)
        {
            recvcnt++;
            switch(packmeg.region)
            {
                case 1 :eastcnt++;break;
                case 2 :southcnt++;break;
                case 3 :northcnt++;break;
            }
            totalweigh += packmeg.weight;

            if(recvcnt == 10)
            {
                rt_kprintf("total packages: %d\n",recvcnt);
                rt_kprintf("east: %d, south: %d, north: %d\n",eastcnt,southcnt,northcnt);
                rt_kprintf("total weight: %d\n",totalweigh);
                rt_mq_detach(&mq);
                break;
            }
        }
    }
}


int package_demo(void)
{
    rt_err_t result;

    /* 初始化消息队列 */
    result = rt_mq_init(&mq,
                        "mqt",
                        &msg_pool[0],               /* 内存池指向msg_pool */
                        sizeof(package_msg_t),        /* 每个消息的大小 */
                        5 * sizeof(package_msg_t),           /* 内存池的大小 */
                        RT_IPC_FLAG_PRIO);          /* 如果有多个线程等待，按照先来先得到的方法分配消息 */

    if (result != RT_EOK)
    {
        rt_kprintf("init message queue failed.\n");
        return -1;
    }

    rt_thread_init(&msgsend_thread1,
                   "msgsend_thread1",
                   msgsend_thread1_entry,
                   RT_NULL,
                   &msgsend_thread1_stack[0],
                   sizeof(msgsend_thread1_stack),
                   task4_THREAD_PRIORITY, task4_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&msgsend_thread1, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&msgsend_thread1);


    rt_thread_init(&msgsend_thread2,
                   "msgsend_thread2",
                   msgsend_thread2_entry,
                   RT_NULL,
                   &msgsend_thread2_stack[0],
                   sizeof(msgsend_thread2_stack),
                   task4_THREAD_PRIORITY, task4_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&msgsend_thread2, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&msgsend_thread2);


    rt_thread_init(&msgrscv_thread,
                   "msgrscv_thread",
                   msgrscv_thread_entry,
                   RT_NULL,
                   &msgrscv_thread_stack[0],
                   sizeof(msgrscv_thread_stack),
                   task4_THREAD_PRIORITY, task4_THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&msgrscv_thread, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&msgrscv_thread);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(package_demo, msgq sample);










