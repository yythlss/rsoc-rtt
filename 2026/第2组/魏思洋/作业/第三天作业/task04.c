/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     Vincent       the first version
 */
#include <rtthread.h>

#define THREAD_PRIORITY         25
#define THREAD_STACK_SIZE       512
#define THREAD_TIMESLICE        5
#define MSG_QUEUE_SIZE          5

/* 定义消息结束字段ID */
#define MSG_TYPE_END          1

/* 区域定义 */
#define REGION_EAST     1   /* 华东 */
#define REGION_SOUTH    2   /* 华南 */
#define REGION_NORTH    3   /* 华北 */

/* 快递消息结构 */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

/* 统计变量 */
static volatile rt_uint32_t total_packages = 0;
static volatile rt_uint32_t east_count = 0;
static volatile rt_uint32_t south_count = 0;
static volatile rt_uint32_t north_count = 0;
static volatile rt_uint32_t total_weight = 0;
static volatile rt_uint32_t send_failed = 0;

/* 快递编号起始值 */
#define SENDER1_START_ID    1000
#define SENDER2_START_ID    2000

/* 消息队列控制块 */
static rt_mq_t mq = RT_NULL;

static rt_thread_t tid1, tid2, tid3 = RT_NULL;

/* 线程 收件线程1 入口函数 */
static void rec1_entry(void *parameter)
{
    rt_uint8_t i;
    struct package_msg msg;

    /* 产生固定的5条快递消息 */
    for (i = 0; i < 5; i++)
    {
        msg.id = SENDER1_START_ID + i + 1;  /* 1001, 1002, 1003, 1004, 1005 */
        msg.region = (i % 3) + 1;            /* 1, 2, 3 循环 */
        msg.weight = 800 + i * 100;          /* 800, 900, 1000, 1100, 1200 */

        /* 发送消息，等待直到队列有空位 */
        if (rt_mq_send_wait(mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            send_failed++;
            rt_kprintf("sender1: send failed, id=%d\n", msg.id);
        }
        else
        {
            rt_kprintf("sender1: send package %d, region=%d, weight=%d\n",
                       msg.id, msg.region, msg.weight);
        }

        rt_thread_mdelay(100);  /* 模拟产生快递的间隔 */
    }

    /* 发送结束消息 */
    msg.id = MSG_TYPE_END;  /* 用 id 字段标记结束消息 */
    rt_mq_send_wait(mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
    rt_kprintf("rec1: finished\r\n");
}

/* 线程 收件线程2 入口 */
static void rec2_entry(void *parameter)
{
    rt_uint8_t i;
    struct package_msg msg;

    /* 产生固定的5条快递消息 */
    for (i = 0; i < 5; i++)
    {
        msg.id = SENDER1_START_ID + i + 1;  /* 2001, 2002, 2003, 2004, 2005 */
        msg.region = (i % 3) + 1;            /* 1, 2, 3 循环 */
        msg.weight = 800 + i * 100;          /* 800, 900, 1000, 1100, 1200 */

        /* 发送消息，等待直到队列有空位 */
        if (rt_mq_send_wait(mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            send_failed++;
            rt_kprintf("sender1: send failed, id=%d\n", msg.id);
        }
        else
        {
            rt_kprintf("sender1: send package %d, region=%d, weight=%d\n",
                       msg.id, msg.region, msg.weight);
        }

        rt_thread_mdelay(100);  /* 模拟产生快递的间隔 */
    }

    /* 发送结束消息 */
    msg.id = MSG_TYPE_END;  /* 用 id 字段标记结束消息 */
    rt_mq_send_wait(mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
    rt_kprintf("rec2: finished\r\n");
}

/* 线程 3 分拣线程 入口 */
static void select3_entry(void *parameter)
{
    struct package_msg msg;
    rt_uint8_t end_count = 0;

    while (1)
    {
        /* 接收消息，永久等待 */
        if (rt_mq_recv(mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }

        /* 检查是否是结束消息 */
        if (msg.id == MSG_TYPE_END)
        {
            end_count++;
            rt_kprintf("sorter: received end signal (%d/2)\n", end_count);

            /* 两个发件线程都结束了 */
            if (end_count >= 2)
            {
                break;
            }
            continue;
        }

        /* 分拣处理 */
        total_packages++;
        total_weight += msg.weight;

        switch (msg.region)
        {
            case REGION_EAST:
                east_count++;
                rt_kprintf("package %d -> east, weight: %d g\n", msg.id, msg.weight);
                break;
            case REGION_SOUTH:
                south_count++;
                rt_kprintf("package %d -> south, weight: %d g\n", msg.id, msg.weight);
                break;
            case REGION_NORTH:
                north_count++;
                rt_kprintf("package %d -> north, weight: %d g\n", msg.id, msg.weight);
                break;
            default:
                rt_kprintf("package %d -> unknown region, weight: %d g\n", msg.id, msg.weight);
                break;
        }
    }

    /* 打印统计结果 */
    rt_kprintf("\n========== Sorting Report ==========\n");
    rt_kprintf("total packages: %d\n", total_packages);
    rt_kprintf("east: %d, south: %d, north: %d\n", east_count, south_count, north_count);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", send_failed);
}

/* 消息队列示例的初始化 */
int package_demo(void)
{
    /* 初始化消息队列 */
    mq = rt_mq_create("package", sizeof(struct package_msg), MSG_QUEUE_SIZE, RT_IPC_FLAG_FIFO);
    if (mq == RT_NULL)
    {
        rt_kprintf("create mq failed!\n");
        return -1;
    }

    /* 重置统计变量 */
    total_packages = 0;
    east_count = 0;
    south_count = 0;
    north_count = 0;
    total_weight = 0;
    send_failed = 0;

    /* 动态创建两个收件线程,并且启动 */
    tid1 = rt_thread_create("rec1",
                            rec1_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid1 != RT_NULL)
        rt_thread_startup(tid1);

    tid2 = rt_thread_create("rec2",
                            rec2_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid2 != RT_NULL)
        rt_thread_startup(tid2);

    tid3 = rt_thread_create("select3",
                            select3_entry, RT_NULL,
                            THREAD_STACK_SIZE,
                            THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid3 != RT_NULL)
        rt_thread_startup(tid3);

    return 0;
}

/* 导出到 msh 命令列表中 */
MSH_CMD_EXPORT(package_demo, package demo);
