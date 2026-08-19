/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     xlc       the first version
 */
#include <rtthread.h>

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

#define PACKAGE_QUEUE_LEN      5
#define PACKAGE_SEND_COUNT      5
#define PACKAGE_STACK_SIZE      1024
#define PACKAGE_END_REGION      0xFF

static rt_mq_t package_mq = RT_NULL;
static rt_sem_t sender_done_sem = RT_NULL;
static rt_sem_t receiver_done_sem = RT_NULL;
static rt_mutex_t stat_mutex = RT_NULL;

static struct rt_thread sender_a_thread;
static struct rt_thread sender_b_thread;
static struct rt_thread receiver_thread;

static rt_uint8_t sender_a_stack[PACKAGE_STACK_SIZE];
static rt_uint8_t sender_b_stack[PACKAGE_STACK_SIZE];
static rt_uint8_t receiver_stack[PACKAGE_STACK_SIZE];

static volatile rt_uint8_t demo_running = 0;

static rt_uint32_t total_count = 0;
static rt_uint32_t east_count = 0;
static rt_uint32_t south_count = 0;
static rt_uint32_t north_count = 0;
static rt_uint32_t total_weight = 0;
static rt_uint32_t send_failed = 0;

static const char *region_name(rt_uint8_t region)
{
    switch (region)
    {
    case 1: return "east";
    case 2: return "south";
    case 3: return "north";
    default: return "unknown";
    }
}

static void stat_add(rt_uint8_t region, rt_uint16_t weight)
{
    rt_mutex_take(stat_mutex, RT_WAITING_FOREVER);

    total_count++;
    total_weight += weight;

    if (region == 1) east_count++;
    else if (region == 2) south_count++;
    else if (region == 3) north_count++;

    rt_mutex_release(stat_mutex);
}

static void stat_add_send_failed(void)
{
    rt_mutex_take(stat_mutex, RT_WAITING_FOREVER);
    send_failed++;
    rt_mutex_release(stat_mutex);
}

static void print_stats(void)
{
    rt_uint32_t tc, ec, sc, nc, tw, sf;

    rt_mutex_take(stat_mutex, RT_WAITING_FOREVER);
    tc = total_count;
    ec = east_count;
    sc = south_count;
    nc = north_count;
    tw = total_weight;
    sf = send_failed;
    rt_mutex_release(stat_mutex);

    rt_kprintf("total packages: %u\n", tc);
    rt_kprintf("east: %u, south: %u, north: %u\n", ec, sc, nc);
    rt_kprintf("total weight: %u g\n", tw);
    rt_kprintf("send failed: %u\n", sf);
}

static void send_end_message(void)
{
    struct package_msg end_msg;

    end_msg.id = 0;
    end_msg.region = PACKAGE_END_REGION;
    end_msg.weight = 0;

    while (rt_mq_send(package_mq, &end_msg, sizeof(end_msg)) == -RT_EFULL)
    {
        rt_thread_mdelay(10);
    }
}

static void sender_thread_entry(void *parameter)
{
    rt_uint32_t sender_id = (rt_uint32_t)(rt_ubase_t)parameter;
    rt_uint32_t i;
    struct package_msg msg;

    rt_thread_mdelay(100);

    for (i = 0; i < PACKAGE_SEND_COUNT; i++)
    {
        if (sender_id == 1)
        {
            static const rt_uint8_t regions[PACKAGE_SEND_COUNT] = {1, 2, 3, 1, 2};
            static const rt_uint16_t weights[PACKAGE_SEND_COUNT] = {1200, 850, 960, 1100, 780};
            msg.id = 1000 + i + 1;
            msg.region = regions[i];
            msg.weight = weights[i];
        }
        else
        {
            static const rt_uint8_t regions[PACKAGE_SEND_COUNT] = {2, 3, 1, 3, 2};
            static const rt_uint16_t weights[PACKAGE_SEND_COUNT] = {920, 1040, 880, 1260, 790};
            msg.id = 2000 + i + 1;
            msg.region = regions[i];
            msg.weight = weights[i];
        }

        if (rt_mq_send(package_mq, &msg, sizeof(msg)) != RT_EOK)
        {
            stat_add_send_failed();
            rt_kprintf("sender %u send failed for package %u\n", sender_id, msg.id);
        }

        rt_thread_mdelay(60 + sender_id * 20);
    }

    rt_sem_release(sender_done_sem);
}

static void receiver_thread_entry(void *parameter)
{
    struct package_msg msg;
    rt_err_t result;

    RT_UNUSED(parameter);

    while (1)
    {
        result = rt_mq_recv(package_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (result == RT_EOK)
        {
            if (msg.id == 0 && msg.region == PACKAGE_END_REGION)
            {
                break;
            }

            rt_kprintf("package %u -> %s, weight: %u g\n",
                       msg.id, region_name(msg.region), msg.weight);
            stat_add(msg.region, msg.weight);
        }
    }

    print_stats();
    rt_sem_release(receiver_done_sem);
}

int package_demo(int argc, char **argv)
{
    rt_err_t result;
    rt_uint8_t started_sender_cnt = 0;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    if (demo_running)
    {
        rt_kprintf("package demo is already running\n");
        return -RT_ERROR;
    }

    demo_running = 1;

    package_mq = rt_mq_create("pkgmq", sizeof(struct package_msg), PACKAGE_QUEUE_LEN, RT_IPC_FLAG_PRIO);
    if (package_mq == RT_NULL)
    {
        rt_kprintf("create package mq failed\n");
        demo_running = 0;
        return -RT_ERROR;
    }

    sender_done_sem = rt_sem_create("snddn", 0, RT_IPC_FLAG_PRIO);
    if (sender_done_sem == RT_NULL)
    {
        rt_kprintf("create sender done sem failed\n");
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        demo_running = 0;
        return -RT_ERROR;
    }

    receiver_done_sem = rt_sem_create("rcvdn", 0, RT_IPC_FLAG_PRIO);
    if (receiver_done_sem == RT_NULL)
    {
        rt_kprintf("create receiver done sem failed\n");
        rt_sem_delete(sender_done_sem);
        sender_done_sem = RT_NULL;
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        demo_running = 0;
        return -RT_ERROR;
    }

    stat_mutex = rt_mutex_create("pkgmtx", RT_IPC_FLAG_PRIO);
    if (stat_mutex == RT_NULL)
    {
        rt_kprintf("create stat mutex failed\n");
        rt_sem_delete(receiver_done_sem);
        receiver_done_sem = RT_NULL;
        rt_sem_delete(sender_done_sem);
        sender_done_sem = RT_NULL;
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        demo_running = 0;
        return -RT_ERROR;
    }

    total_count = 0;
    east_count = 0;
    south_count = 0;
    north_count = 0;
    total_weight = 0;
    send_failed = 0;

    result = rt_thread_init(&receiver_thread,
                            "pkgrcv",
                            receiver_thread_entry,
                            RT_NULL,
                            receiver_stack,
                            sizeof(receiver_stack),
                            20,
                            10);
    if (result != RT_EOK)
    {
        rt_kprintf("init receiver thread failed, err=%d\n", result);
        goto fail;
    }

    result = rt_thread_init(&sender_a_thread,
                            "sndA",
                            sender_thread_entry,
                            (void *)(rt_ubase_t)1,
                            sender_a_stack,
                            sizeof(sender_a_stack),
                            21,
                            10);
    if (result != RT_EOK)
    {
        rt_kprintf("init sender A thread failed, err=%d\n", result);
        goto fail;
    }

    result = rt_thread_init(&sender_b_thread,
                            "sndB",
                            sender_thread_entry,
                            (void *)(rt_ubase_t)2,
                            sender_b_stack,
                            sizeof(sender_b_stack),
                            21,
                            10);
    if (result != RT_EOK)
    {
        rt_kprintf("init sender B thread failed, err=%d\n", result);
        goto fail;
    }

    result = rt_thread_startup(&receiver_thread);
    if (result != RT_EOK)
    {
        rt_kprintf("startup receiver thread failed, err=%d\n", result);
        goto fail;
    }

    result = rt_thread_startup(&sender_a_thread);
    if (result == RT_EOK)
        started_sender_cnt++;
    else
        rt_kprintf("startup sender A thread failed, err=%d\n", result);

    result = rt_thread_startup(&sender_b_thread);
    if (result == RT_EOK)
        started_sender_cnt++;
    else
        rt_kprintf("startup sender B thread failed, err=%d\n", result);

    while (started_sender_cnt > 0)
    {
        rt_sem_take(sender_done_sem, RT_WAITING_FOREVER);
        started_sender_cnt--;
    }

    send_end_message();
    rt_sem_take(receiver_done_sem, RT_WAITING_FOREVER);

    rt_mutex_delete(stat_mutex);
    stat_mutex = RT_NULL;

    rt_sem_delete(receiver_done_sem);
    receiver_done_sem = RT_NULL;

    rt_sem_delete(sender_done_sem);
    sender_done_sem = RT_NULL;

    rt_mq_delete(package_mq);
    package_mq = RT_NULL;

    demo_running = 0;

    rt_kprintf("package demo finished\n");
    return RT_EOK;

fail:
    if (stat_mutex != RT_NULL)
    {
        rt_mutex_delete(stat_mutex);
        stat_mutex = RT_NULL;
    }

    if (receiver_done_sem != RT_NULL)
    {
        rt_sem_delete(receiver_done_sem);
        receiver_done_sem = RT_NULL;
    }

    if (sender_done_sem != RT_NULL)
    {
        rt_sem_delete(sender_done_sem);
        sender_done_sem = RT_NULL;
    }

    if (package_mq != RT_NULL)
    {
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
    }

    demo_running = 0;
    return result;
}

MSH_CMD_EXPORT(package_demo, package sort demo);
