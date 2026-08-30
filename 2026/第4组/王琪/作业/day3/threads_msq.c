/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-24     wangqi       the first version
 */
#ifndef APPLICATIONS_THREADS_MSQ_C_
#define APPLICATIONS_THREADS_MSQ_C_

#include <rtthread.h>

#define MSG_POOL_SIZE   (sizeof(struct package_msg) * 5)

#define REGION_END      0
#define REGION_EAST     1
#define REGION_SOUTH    2
#define REGION_NORTH    3

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;
    rt_uint16_t weight;
};

static rt_mq_t package_mq = RT_NULL;
static rt_mutex_t fail_mutex = RT_NULL;
static int send_failed_count = 0;

static void record_send_fail(void)
{
    rt_mutex_take(fail_mutex, RT_WAITING_FOREVER);
    send_failed_count++;
    rt_mutex_release(fail_mutex);
}

static int get_send_fail_count(void)
{
    int count;
    rt_mutex_take(fail_mutex, RT_WAITING_FOREVER);
    count = send_failed_count;
    rt_mutex_release(fail_mutex);
    return count;
}

/*
 * parameter 编码方式（32 位）：
 *   [31:24] 消息数量（最多 255 条）
 *   [23:16] 起始 ID 高 8 位
 *   [15: 8] 起始 ID 低 8 位（合起来支持 0~65535 的起始 ID）
 *   [ 7: 0] region 编号
 */
#define PACK_PARAM(count, start_id, region) \
    (void *)(((rt_uint32_t)(count) << 24) | ((rt_uint32_t)(start_id) << 8) | (region))

static void sender_entry(void *parameter)
{
    rt_uint32_t param = (rt_uint32_t)(rt_ubase_t)parameter;
    int         count    = (int)((param >> 24) & 0xFF);
    rt_uint32_t start_id = (param >> 8) & 0xFFFF;
    rt_uint8_t  region   = (rt_uint8_t)(param & 0xFF);

    const char *region_name;
    switch (region)
    {
        case REGION_EAST:  region_name = "east";  break;
        case REGION_SOUTH: region_name = "south"; break;
        case REGION_NORTH: region_name = "north"; break;
        default:           region_name = "unknown"; break;
    }

    for (int i = 0; i < count; i++)
    {
        struct package_msg msg;
        msg.id     = start_id + i;
        msg.region = region;
        msg.weight = 500 + (rt_uint16_t)((msg.id * 37) % 1500);

        rt_kprintf("sender [%s] created package %d, weight: %d g\n",
                   region_name, msg.id, msg.weight);

        rt_err_t ret = rt_mq_send(package_mq, &msg, sizeof(msg));
        if (ret != RT_EOK)
        {
            rt_kprintf("sender [%s] send package %d FAILED (queue full)\n",
                       region_name, msg.id);
            record_send_fail();
        }

        rt_thread_mdelay(50);
    }

    rt_kprintf("sender [%s] finished, sent %d packages\n", region_name, count);
}

/* 分拣线程 */
static void sorter_entry(void *parameter)
{
    int total_count = 0;
    int east_count  = 0;
    int south_count = 0;
    int north_count = 0;
    rt_uint32_t total_weight = 0;

    while (1)
    {
        struct package_msg msg;
        rt_err_t ret = rt_mq_recv(package_mq, &msg, sizeof(msg),
                                  RT_WAITING_FOREVER);
        if (ret != RT_EOK)
            continue;

        if (msg.region == REGION_END)
            break;

        total_count++;
        total_weight += msg.weight;

        const char *region_name;
        switch (msg.region)
        {
            case REGION_EAST:
                east_count++;
                region_name = "east";
                break;
            case REGION_SOUTH:
                south_count++;
                region_name = "south";
                break;
            case REGION_NORTH:
                north_count++;
                region_name = "north";
                break;
            default:
                region_name = "unknown";
                break;
        }

        rt_kprintf("package %d -> %s, weight: %d g\n",
                   msg.id, region_name, msg.weight);
    }

    rt_kprintf("\n===== sorting result =====\n");
    rt_kprintf("total packages: %d\n", total_count);
    rt_kprintf("east: %d, south: %d, north: %d\n",
               east_count, south_count, north_count);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", get_send_fail_count());
    rt_kprintf("==========================\n");
}

/* 结束信号线程：等待所有发件线程退出后发送结束消息 */
static void end_signal_entry(void *parameter)
{
    rt_thread_t t_e, t_s, t_n;

    while (1)
    {
        t_e = rt_thread_find("sender_e");
        t_s = rt_thread_find("sender_s");
        t_n = rt_thread_find("sender_n");

        if ((t_e == RT_NULL || t_e->stat == RT_THREAD_CLOSE) &&
            (t_s == RT_NULL || t_s->stat == RT_THREAD_CLOSE) &&
            (t_n == RT_NULL || t_n->stat == RT_THREAD_CLOSE))
            break;

        rt_thread_mdelay(100);
    }

    struct package_msg end_msg;
    end_msg.id     = 0;
    end_msg.region = REGION_END;
    end_msg.weight = 0;

    rt_mq_send(package_mq, &end_msg, sizeof(end_msg));
    rt_kprintf("end signal sent to sorter\n");
}

static int package_test_init(void)
{
    rt_thread_t tid;

    package_mq = rt_mq_create("pkg_mq",
                              sizeof(struct package_msg),
                              MSG_POOL_SIZE,
                              RT_IPC_FLAG_PRIO);
    if (package_mq == RT_NULL)
    {
        rt_kprintf("create message queue failed!\n");
        return -1;
    }

    fail_mutex = rt_mutex_create("fail_mtx", RT_IPC_FLAG_PRIO);
    if (fail_mutex == RT_NULL)
    {
        rt_kprintf("create fail mutex failed!\n");
        return -1;
    }
    send_failed_count = 0;

    /* 分拣线程 */
    tid = rt_thread_create("sorter", sorter_entry, RT_NULL,
                           1024, 19, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    /*
     * 三个发件线程，分别发送 3、5、9 条消息：
     *   华东：ID 1001~1003，共 3 条
     *   华南：ID 2001~2005，共 5 条
     *   华北：ID 3001~3009，共 9 条
     */
    tid = rt_thread_create("sender_e",
                           sender_entry,
                           PACK_PARAM(3, 1001, REGION_EAST),
                           1024, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    tid = rt_thread_create("sender_s",
                           sender_entry,
                           PACK_PARAM(5, 2001, REGION_SOUTH),
                           1024, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    tid = rt_thread_create("sender_n",
                           sender_entry,
                           PACK_PARAM(9, 3001, REGION_NORTH),
                           1024, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    /* 结束信号线程 */
    tid = rt_thread_create("end_sig", end_signal_entry, RT_NULL,
                           1024, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);

    return 0;
}

MSH_CMD_EXPORT_ALIAS(package_test_init, package_test, package sorting test);

#endif /* APPLICATIONS_THREADS_MSQ_C_ */
