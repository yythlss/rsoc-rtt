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
 * 程序清单：消息队列例程
 *
 * 这个程序会创建2个动态线程，一个线程会从消息队列中收取消息；一个线程会定时给消
 * 息队列发送 普通消息和紧急消息。
 */
#include <rtthread.h>

#define THREAD_PRIORITY      25
#define THREAD_TIMESLICE     5

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;//1：华东,2：华南,3：华北
    rt_uint16_t weight;
};

/* 消息队列控制块 */
static rt_mq_t  mq;
rt_uint32_t send_fail_cnt=0;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char thread1_stack[1024];
static struct rt_thread thread1;

static char thread2_stack[1024];
static struct rt_thread thread2;

static char thread3_stack[1024];
static struct rt_thread thread3;


/* 线程1入口函数 */
static void thread_rx_entry(void *parameter)
{


    struct package_msg rx_rr;
    rt_uint8_t east_cnt = 0,south_cnt = 0,north_cnt = 0,end_msg_received = 0;
    rt_uint32_t total_packages=0;
    rt_uint32_t total_weight=0;





    while (1)
    {



        /* 从消息队列中接收消息 */
#if (RTTHREAD_VERSION >= RT_VERSION_CHECK(5, 0, 1))
        if (rt_mq_recv(mq, &rx_rr, sizeof(rx_rr), RT_WAITING_FOREVER) > 0)
#else
        if (rt_mq_recv(mq, &rx_rr, sizeof(rx_rr), RT_WAITING_FOREVER) == RT_EOK)
#endif
        {

            if(rx_rr.id==0)
            {
                end_msg_received++;
                if(end_msg_received==2)
                {
                    break;
                }

                continue;
            }


            switch(rx_rr.region)
            {
            case 1:
                east_cnt++;

                rt_kprintf("package %d -> east, weight: %d g\n",rx_rr.id,rx_rr.weight);

                break;
            case 2:
                south_cnt++;
                rt_kprintf("package %d -> south, weight: %d g\n",rx_rr.id,rx_rr.weight);
                break;
            case 3:
                north_cnt++;
                rt_kprintf("package %d -> north, weight: %d g\n",rx_rr.id,rx_rr.weight);
                break;
            }

        }


        total_weight+=rx_rr.weight;
        total_packages++;


    }


    rt_kprintf("total packages:%d\n",total_packages);
    rt_kprintf("east: %d, south: %d, north: %d\n",east_cnt,south_cnt,north_cnt);
    rt_kprintf("total weight: %d g\n",total_weight);
    rt_kprintf("send failed: %d\n",send_fail_cnt);
    rt_mq_delete(mq);
}




/* 线程2入口 */
static void thread_tx_entry(void *parameter)
{

    int thread_id=(int)parameter;
    int result;
    struct package_msg tx_1;
    rt_uint8_t cnt = 0;

    while (1)
    {
        if (cnt >= 5)
        {

           break;
        }
        else
        {
            /* 发送消息到消息队列中 */
            tx_1.id=thread_id*1000+cnt;
            tx_1.region=(thread_id+cnt)%3+1;
            tx_1.weight=600+(cnt*60);


            result = rt_mq_send(mq, &tx_1, sizeof(tx_1));
            if (result != RT_EOK)
            {
                send_fail_cnt++;
                rt_kprintf("rt_mq_send ERR\n");
            }


        }

        cnt++;
        /* 延时9ms */
        rt_thread_mdelay(9);
    }

    struct package_msg end_msg={0,0,0};

    while (rt_mq_send(mq, &end_msg, sizeof(end_msg)) == -RT_EFULL)
        {
            rt_thread_mdelay(10);
        }

}








/* 消息队列示例的初始化 */
int msgq_sample(void)
{


    /* 初始化消息队列 */
    mq= rt_mq_create("mq5",               /* 队列名字 */
            sizeof(struct package_msg),                   /* 每条消息的最大长度 */
            5,                   /* 【核心参数】队列容量：精确的 5 条！ */
            RT_IPC_FLAG_PRIO);   /* 等待队列的排队规则 */

    if (mq == RT_NULL)
    {

        rt_kprintf("init message queue failed.\n");
        return -1;
    }

    rt_thread_init(&thread1,
                   "thread1",
                   thread_rx_entry,
                   RT_NULL,
                   &thread1_stack[0],
                   sizeof(thread1_stack),
                   THREAD_PRIORITY-1, THREAD_TIMESLICE);


#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread1, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread1);

    rt_thread_init(&thread2,
                   "thread2",
                   thread_tx_entry,
                   (void*)1,
                   &thread2_stack[0],
                   sizeof(thread2_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread2, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread2);


    rt_thread_init(&thread3,
                   "thread3",
                   thread_tx_entry,
                   (void*)2,
                   &thread3_stack[0],
                   sizeof(thread3_stack),
                   THREAD_PRIORITY, THREAD_TIMESLICE);
#ifdef RT_USING_SMP
    /* 绑定线程到同一个核上，避免启用多核时的输出混乱 */
    rt_thread_control(&thread3, RT_THREAD_CTRL_BIND_CPU, (void*)0);
#endif
    rt_thread_startup(&thread3);




    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT_ALIAS(msgq_sample,package_demo, msgq sample);
