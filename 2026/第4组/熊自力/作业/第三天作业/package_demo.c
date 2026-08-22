/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     godbear       练习四：快递分拣中心（消息队列）
 *1. 创建一个消息队列，队列容量为 5 条消息。
 *2. 创建两个发件线程，每个线程产生 5 条快递消息。
 *3. 快递编号不得重复。
 *4. 创建一个分拣线程接收消息，并打印快递编号、区域和重量。
 *5. 分拣完成后统计：
   - 总快递数量；
   - 各区域快递数量；
   - 所有快递的总重量。
 *6. 当队列已满时，发送线程不得崩溃，应记录发送失败次数。
 *7. 两个发送线程完成后，向分拣线程发送一条结束消息。
 *8. 分拣线程收到结束消息后打印统计结果并退出。
 */

#include <rtthread.h>
#include <finsh.h>

#define DBG_TAG "package"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define PKG_PER_SENDER       5                       /* 每个发件线程产生 5 条 */
#define PKG_QUEUE_SIZE       5                       /* 消息队列容量 5 条 */
#define REGION_END           0                       /* 结束消息标志 */

struct package_msg
{
    rt_uint32_t id;                                 /* 快递编号 */
    rt_uint8_t  region;                             /* 目的区域 1=华东 2=华南 3=华北 */
    rt_uint16_t weight;                             /* 重量(g) */
};

static rt_mq_t pkg_mq = RT_NULL;                    /* 消息队列控制块 */
static volatile rt_uint32_t send_failed = 0;        /* 队列满导致的发送失败次数 */
static volatile rt_uint8_t pkg_running = 0;

/* 发件线程：产生 5 条快递消息，最后发送一条结束消息 */
static void sender_entry(void *param)
{
    rt_uint32_t base = (rt_uint32_t)(rt_size_t)param;   /* 1000 或 2000 */
    struct package_msg msg;
    rt_uint32_t i;
    static const rt_uint8_t region_tab[PKG_PER_SENDER]  = {1, 2, 3, 1, 2};
    static const rt_uint16_t weight_tab[PKG_PER_SENDER] = {1200, 850, 960, 1500, 1000};

    for (i = 0; i < PKG_PER_SENDER; i++)
    {
        msg.id = base + i;
        msg.region = region_tab[i];
        msg.weight = weight_tab[i];

        /* 队列满时发送失败不崩溃，记录失败次数 */
        if (rt_mq_send(pkg_mq, &msg, sizeof(msg)) != RT_EOK)
        {
            rt_enter_critical();
            send_failed++;
            rt_exit_critical();
        }
        rt_thread_mdelay(rt_tick_from_millisecond(20));
    }

    /* 发送结束消息 */
    msg.id = 0;
    msg.region = REGION_END;
    msg.weight = 0;
    /* 发送结束消息：失败则让出 CPU 重试，直到成功 */
    while (rt_mq_send(pkg_mq, &msg, sizeof(msg)) != RT_EOK)
    {
        rt_thread_mdelay(rt_tick_from_millisecond(10));
    }
}

/* 分拣线程：接收并统计，收到两发件线程的结束消息后输出统计并退出 */
static void sorter_entry(void *param)
{
    (void)param;
    struct package_msg msg;
    rt_uint32_t total = 0, total_weight = 0;
    rt_uint32_t east = 0, south = 0, north = 0;
    rt_uint32_t end_received = 0;
    const char *region_name;

    while (1)
    {
        rt_err_t res = rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (res != RT_EOK)
        {
            rt_kprintf("mq recv failed\n");
            break;
        }

        if (msg.region == REGION_END)
        {
            end_received++;
            if (end_received >= 2)
                break;
            continue;
        }

        switch (msg.region)
        {
        case 1: region_name = "east";  east++;  break;
        case 2: region_name = "south"; south++; break;
        case 3: region_name = "north"; north++; break;
        default: region_name = "unknown"; break;
        }
        total++;
        total_weight += msg.weight;

        /* 模拟分拣耗时，使队列产生“满”的场景 */
        rt_thread_mdelay(rt_tick_from_millisecond(40));
        rt_kprintf("package %d -> %s, weight: %d g\n", msg.id, region_name, msg.weight);
    }

    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", east, south, north);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", send_failed);

    /* 清理资源，支持命令重复执行 */
    pkg_running = 0;
    if (pkg_mq)
    {
        rt_mq_delete(pkg_mq);
        pkg_mq = RT_NULL;
    }
}

static int package_demo(int argc, char *argv[])
{
    rt_thread_t tid;

    if (pkg_running)
    {
        rt_kprintf("package_demo is already running, please wait...\n");
        return -RT_EBUSY;
    }

    /* 创建容量为 5 条的消息队列 */
    pkg_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), PKG_QUEUE_SIZE, RT_IPC_FLAG_PRIO);
    if (pkg_mq == RT_NULL)
    {
        rt_kprintf("create package message queue failed\n");
        return -RT_ENOMEM;
    }

    send_failed = 0;
    pkg_running = 1;

    /* 发件线程 A（编号 1001~1005） */
    tid = rt_thread_create("sndA", sender_entry, (void *)(rt_size_t)1000,
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("create sender A thread failed\n");
    }

    /* 发件线程 B（编号 2001~2005） */
    tid = rt_thread_create("sndB", sender_entry, (void *)(rt_size_t)2000,
                           1024, RT_THREAD_PRIORITY_MAX - 3, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("create sender B thread failed\n");
    }

    /* 分拣线程 */
    tid = rt_thread_create("sorter", sorter_entry, RT_NULL,
                           1024, RT_THREAD_PRIORITY_MAX - 2, 10);
    if (tid)
    {
        rt_thread_startup(tid);
    }
    else
    {
        rt_kprintf("create sorter thread failed\n");
        pkg_running = 0;
        rt_mq_delete(pkg_mq);
        pkg_mq = RT_NULL;
        return -RT_ERROR;
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, express sorting demo with message queue);
