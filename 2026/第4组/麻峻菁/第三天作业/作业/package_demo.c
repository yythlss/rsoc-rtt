/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     24527       the first version
 */

#include <rtthread.h>
#include <finsh.h>

#define SENDER_COUNT        2
#define PACKAGE_PER_SENDER  5
#define QUEUE_CAPACITY      5

#define THREAD_PRIORITY     25
#define THREAD_STACK_SIZE   1024
#define THREAD_TIMESLICE    5

#define END_PACKAGE_ID      0xFFFFFFFFU
#define SEND_RETRY_MS       50

/**
 * @brief 快递消息结构。
 */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

/**
 * @brief 发件线程参数。
 */
struct sender_info
{
    const char *name;
    rt_uint32_t base_id;
    rt_uint8_t region_offset;
};

/* 消息队列 */
static rt_mq_t package_mq = RT_NULL;

/* 两个发件线程和一个分拣线程 */
static rt_thread_t sender_threads[SENDER_COUNT] = {RT_NULL};
static rt_thread_t sorter_thread = RT_NULL;

/* 发件线程完成数量 */
static rt_uint8_t sender_finished = 0;

/* 消息发送失败次数 */
static rt_uint32_t send_failed = 0;

/* 防止重复启动 */
static rt_bool_t demo_running = RT_FALSE;

/* 两个发件线程参数 */
static struct sender_info senders[SENDER_COUNT] =
{
    {"A", 1001, 0},
    {"B", 2001, 1}
};

/**
 * @brief 获取区域名称。
 *
 * @param region 区域编号。
 *
 * @return 区域名称。
 */
static const char *region_name(rt_uint8_t region)
{
    if (region == 1)
    {
        return "east";
    }

    if (region == 2)
    {
        return "south";
    }

    return "north";
}

/**
 * @brief 发送一条快递消息。
 *
 * 当消息队列已满时，记录一次发送失败，
 * 延时后继续重试，直到消息成功进入队列。
 *
 * @param msg 待发送的快递消息。
 */
static void send_package(struct package_msg *msg)
{
    rt_err_t result;

    while (1)
    {
        result = rt_mq_send(package_mq,
                            msg,
                            sizeof(struct package_msg));

        if (result == RT_EOK)
        {
            break;
        }

        rt_enter_critical();
        send_failed++;
        rt_exit_critical();

        rt_kprintf("send package %d failed, retry later\n", msg->id);

        rt_thread_mdelay(SEND_RETRY_MS);
    }
}

/**
 * @brief 发件线程入口。
 *
 * 每个发件线程产生 5 条快递消息。
 * 两个线程使用不同编号段，因此快递编号不会重复。
 *
 * @param parameter 指向 sender_info 的线程参数。
 */
static void sender_entry(void *parameter)
{
    struct sender_info *sender;
    struct package_msg msg;
    struct package_msg end_msg;
    rt_uint8_t i;
    rt_bool_t last_sender = RT_FALSE;

    sender = (struct sender_info *)parameter;

    for (i = 0; i < PACKAGE_PER_SENDER; i++)
    {
        /* 生成唯一快递编号 */
        msg.id = sender->base_id + i;

        /* 模拟三个不同区域 */
        msg.region = ((i + sender->region_offset) % 3) + 1;

        /* 模拟快递重量 */
        msg.weight = 800 + ((msg.id + i) % 6) * 100;

        send_package(&msg);

        /* 模拟连续收件过程 */
        rt_thread_mdelay(20);
    }

    /*
     * 记录已经完成的发件线程数量。
     * 最后结束的发件线程负责发送结束消息。
     */
    rt_enter_critical();

    sender_finished++;

    if (sender_finished == SENDER_COUNT)
    {
        last_sender = RT_TRUE;
    }

    rt_exit_critical();

    if (last_sender == RT_TRUE)
    {
        end_msg.id = END_PACKAGE_ID;
        end_msg.region = 0;
        end_msg.weight = 0;

        /*
         * 结束消息必须成功送入队列，
         * 如果此时队列已满，则等待后重试。
         */
        while (rt_mq_send(package_mq,
                          &end_msg,
                          sizeof(struct package_msg)) != RT_EOK)
        {
            rt_thread_mdelay(SEND_RETRY_MS);
        }
    }

    rt_kprintf("sender %s finished\n", sender->name);
}

/**
 * @brief 分拣线程入口。
 *
 * 从消息队列中持续接收快递，
 * 根据区域分类并统计数量和总重量。
 *
 * 收到结束消息后打印最终统计结果并退出。
 *
 * @param parameter 未使用。
 */
static void sorter_entry(void *parameter)
{
    struct package_msg msg;
    rt_uint32_t total = 0;
    rt_uint32_t east_count = 0;
    rt_uint32_t south_count = 0;
    rt_uint32_t north_count = 0;
    rt_uint32_t total_weight = 0;
    rt_err_t result;

    while (1)
    {
        result = rt_mq_recv(package_mq,
                            &msg,
                            sizeof(struct package_msg),
                            RT_WAITING_FOREVER);

        if (result != RT_EOK)
        {
            rt_kprintf("receive package failed\n");
            continue;
        }

        /* 收到结束消息，结束分拣 */
        if (msg.id == END_PACKAGE_ID)
        {
            break;
        }

        total++;
        total_weight += msg.weight;

        if (msg.region == 1)
        {
            east_count++;
        }
        else if (msg.region == 2)
        {
            south_count++;
        }
        else if (msg.region == 3)
        {
            north_count++;
        }

        rt_kprintf(
            "package %d -> %s, weight: %d g\n",
            msg.id,
            region_name(msg.region),
            msg.weight);

        /*
         * 模拟分拣处理时间。
         * 让生产速度可能快于消费速度，
         * 从而更容易观察到队列已满。
         */
        rt_thread_mdelay(100);
    }

    rt_kprintf("\n");
    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n",
               east_count,
               south_count,
               north_count);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", send_failed);

    rt_mq_delete(package_mq);
    package_mq = RT_NULL;
    demo_running = RT_FALSE;
}

/**
 * @brief 启动快递分拣中心实验。
 *
 * 创建容量为 5 条消息的消息队列，
 * 创建两个发件线程和一个分拣线程。
 *
 * @return RT_EOK 启动成功。
 * @return -RT_ERROR 消息队列或线程创建失败。
 */
static rt_err_t package_demo(void)
{
    rt_err_t result;
    int i;

    if (demo_running == RT_TRUE)
    {
        rt_kprintf("package demo is already running\n");
        return RT_EOK;
    }

    sender_finished = 0;
    send_failed = 0;

    /* 创建容量为 5 条消息的消息队列 */
    package_mq = rt_mq_create(
        "pkgmq",
        sizeof(struct package_msg),
        QUEUE_CAPACITY,
        RT_IPC_FLAG_FIFO);

    if (package_mq == RT_NULL)
    {
        rt_kprintf("create package message queue failed\n");
        return -RT_ERROR;
    }

    demo_running = RT_TRUE;

    /* 创建分拣线程 */
    sorter_thread = rt_thread_create(
        "sorter",
        sorter_entry,
        RT_NULL,
        THREAD_STACK_SIZE,
        THREAD_PRIORITY,
        THREAD_TIMESLICE);

    if (sorter_thread == RT_NULL)
    {
        rt_kprintf("create sorter thread failed\n");
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        demo_running = RT_FALSE;
        return -RT_ERROR;
    }

    /* 创建两个发件线程 */
    for (i = 0; i < SENDER_COUNT; i++)
    {
        sender_threads[i] = rt_thread_create(
            i == 0 ? "sender_a" : "sender_b",
            sender_entry,
            &senders[i],
            THREAD_STACK_SIZE,
            THREAD_PRIORITY,
            THREAD_TIMESLICE);

        if (sender_threads[i] == RT_NULL)
        {
            rt_kprintf("create sender %d thread failed\n", i + 1);
            return -RT_ERROR;
        }
    }

    /* 启动分拣线程 */
    result = rt_thread_startup(sorter_thread);

    if (result != RT_EOK)
    {
        rt_kprintf("startup sorter thread failed\n");
        return -RT_ERROR;
    }

    /* 启动两个发件线程 */
    for (i = 0; i < SENDER_COUNT; i++)
    {
        result = rt_thread_startup(sender_threads[i]);

        if (result != RT_EOK)
        {
            rt_kprintf("startup sender %d thread failed\n", i + 1);
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}

/* 导出为 MSH 命令 */
MSH_CMD_EXPORT(package_demo, package demo);
