/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     Student      Package sorting center
 *
 * 练习四：快递分拣中心（消息队列）
 *
 * 两个收件线程不断产生快递信息，一个分拣线程从消息队列中取出快递
 * 并按目的地区域分类统计。
 *
 * MSH 命令: package_demo
 */

#include <rtthread.h>

#define DBG_TAG "package"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ============ 快递消息结构 ============ */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;
    rt_uint16_t weight;
};

/* ============ 分拣中心配置 ============ */
#define MQ_CAPACITY      5      /* 消息队列容量(条消息) */
#define PKG_PER_SENDER   5      /* 每个发件线程产生的快递数 */
#define SENDER_COUNT     2      /* 发件线程数量 */
#define REGION_END       0      /* 结束消息标志: region = 0 */
#define SEND_TIMEOUT_MS  10     /* 队列满时发送等待超时(ms) */
#define SORT_DELAY_MS    50     /* 模拟单件分拣耗时(ms)，使队列容易满 */

#define THREAD_PRIORITY   15
#define THREAD_STACK_SIZE 512

/* ============ 全局变量 ============ */
static rt_mq_t g_pkg_mq = RT_NULL;      /* 快递消息队列 */
static volatile int g_running = 0;      /* 演示运行标志 */
static volatile int g_send_failed = 0;  /* 发送失败次数(队列满) */

/* ============ 发件线程参数 ============ */
typedef struct
{
    rt_uint32_t base_id;              /* 快递编号基准 */
    const rt_uint8_t *regions;        /* 各快递目的区域表 */
    const rt_uint16_t *weights;       /* 各快递重量表 */
} sender_arg_t;

/* 发件线程 A：编号 1001~1005 */
static const rt_uint8_t snd1_regions[PKG_PER_SENDER] = {1, 2, 1, 3, 1};
static const rt_uint16_t snd1_weights[PKG_PER_SENDER] = {1200, 850, 960, 1100, 700};
static sender_arg_t g_sender_a = {1001, snd1_regions, snd1_weights};

/* 发件线程 B：编号 2001~2005，与 A 不重复 */
static const rt_uint8_t snd2_regions[PKG_PER_SENDER] = {2, 3, 3, 1, 2};
static const rt_uint16_t snd2_weights[PKG_PER_SENDER] = {900, 1300, 800, 950, 1050};
static sender_arg_t g_sender_b = {2001, snd2_regions, snd2_weights};

/*
 * 记录一次发送失败（队列满导致的超时）
 */
static void record_send_failed(void)
{
    rt_enter_critical();
    g_send_failed++;
    rt_exit_critical();
}

/*
 * 发送一条消息：队列满时记录失败次数并重试，保证线程不崩溃
 */
static void send_with_retry(struct package_msg *msg)
{
    while (rt_mq_send_wait(g_pkg_mq, msg, sizeof(*msg),
                           rt_tick_from_millisecond(SEND_TIMEOUT_MS)) != RT_EOK)
    {
        record_send_failed();
    }
}

/*
 * 发件线程入口
 * 产生 5 条快递消息，全部发出后再发一条结束消息(region = 0)
 */
static void sender_entry(void *parameter)
{
    sender_arg_t *arg = (sender_arg_t *)parameter;
    struct package_msg msg;
    int i;

    for (i = 0; i < PKG_PER_SENDER; i++)
    {
        msg.id = arg->base_id + i;
        msg.region = arg->regions[i];
        msg.weight = arg->weights[i];

        send_with_retry(&msg);
    }

    /* 发送结束消息，通知分拣线程本发件线程已完成 */
    msg.id = 0;
    msg.region = REGION_END;
    msg.weight = 0;
    send_with_retry(&msg);
}

/*
 * 区域名映射
 */
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

/*
 * 分拣线程入口
 * 从消息队列取出快递并分类；收到两个结束消息后打印统计结果并退出
 */
static void sorter_entry(void *parameter)
{
    struct package_msg msg;
    rt_base_t level;
    int failed;
    int total = 0;                 /* 总快递数量 */
    int end_count = 0;             /* 收到的结束消息数 */
    int region_cnt[4] = {0, 0, 0, 0};  /* 下标 1/2/3 对应华东/华南/华北 */
    rt_uint32_t total_weight = 0;  /* 总重量 */

    (void)parameter;

    while (1)
    {
        if (rt_mq_recv(g_pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            LOG_E("sorter: failed to receive message");
            continue;
        }

        /* 结束消息：两个发件线程都结束时打印统计并退出 */
        if (msg.region == REGION_END)
        {
            end_count++;
            if (end_count >= SENDER_COUNT)
            {
                break;
            }
            continue;
        }

        /* 分类统计 */
        total++;
        region_cnt[msg.region]++;
        total_weight += msg.weight;

        rt_kprintf("package %u -> %s, weight: %u g\n",
                   msg.id, region_name(msg.region), msg.weight);

        /* 模拟分拣耗时，让发送线程有机会遇到队列满 */
        rt_thread_mdelay(SORT_DELAY_MS);
    }

    /* 打印统计结果 */
    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n",
               region_cnt[1], region_cnt[2], region_cnt[3]);
    rt_kprintf("total weight: %u g\n", total_weight);

    level = rt_hw_interrupt_disable();
    failed = g_send_failed;
    rt_hw_interrupt_enable(level);
    rt_kprintf("send failed: %d\n", failed);

    /* 分拣结束：释放消息队列并复位运行标志 */
    rt_mq_delete(g_pkg_mq);
    g_pkg_mq = RT_NULL;
    g_running = 0;
}

/*
 * MSH 命令：package_demo
 */
static int package_demo(int argc, char *argv[])
{
    rt_thread_t tid_a = RT_NULL;
    rt_thread_t tid_b = RT_NULL;
    rt_thread_t tid_sorter = RT_NULL;

    /* 已在运行则拒绝重复启动 */
    if (g_running)
    {
        rt_kprintf("package demo is already running, please wait...\n");
        return RT_EBUSY;
    }

    g_send_failed = 0;

    /* 创建消息队列：容量 5 条消息 */
    g_pkg_mq = rt_mq_create("pkg_q", sizeof(struct package_msg),
                            MQ_CAPACITY, RT_IPC_FLAG_FIFO);
    if (g_pkg_mq == RT_NULL)
    {
        rt_kprintf("Error: failed to create message queue!\n");
        return RT_ERROR;
    }

    /* 创建两个发件线程 */
    tid_a = rt_thread_create("send_a", sender_entry, &g_sender_a,
                             THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_a == RT_NULL)
    {
        rt_kprintf("Error: failed to create sender A thread!\n");
        goto cleanup_fail;
    }
    tid_b = rt_thread_create("send_b", sender_entry, &g_sender_b,
                             THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_b == RT_NULL)
    {
        rt_kprintf("Error: failed to create sender B thread!\n");
        goto cleanup_fail;
    }

    /* 创建分拣线程 */
    tid_sorter = rt_thread_create("sorter", sorter_entry, RT_NULL,
                                  THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid_sorter == RT_NULL)
    {
        rt_kprintf("Error: failed to create sorter thread!\n");
        goto cleanup_fail;
    }

    /* 启动所有线程 */
    if (rt_thread_startup(tid_a) != RT_EOK ||
        rt_thread_startup(tid_b) != RT_EOK ||
        rt_thread_startup(tid_sorter) != RT_EOK)
    {
        rt_kprintf("Error: failed to start threads!\n");
        goto cleanup_fail;
    }

    g_running = 1;
    rt_kprintf("package demo: 2 senders, %d packages each, queue capacity %d\n",
               PKG_PER_SENDER, MQ_CAPACITY);
    return RT_EOK;

cleanup_fail:
    /* 创建/启动失败：清理已创建的对象。
     * 已退出的线程为 CLOSE 状态，rt_thread_delete 为空操作，可安全重复调用。 */
    if (tid_a != RT_NULL)
        rt_thread_delete(tid_a);
    if (tid_b != RT_NULL)
        rt_thread_delete(tid_b);
    if (tid_sorter != RT_NULL)
        rt_thread_delete(tid_sorter);
    if (g_pkg_mq != RT_NULL)
    {
        rt_mq_delete(g_pkg_mq);
        g_pkg_mq = RT_NULL;
    }
    return RT_ERROR;
}
MSH_CMD_EXPORT(package_demo, package sorting center demo);

/*
 * 主函数
 */
int main(void)
{
    rt_kprintf("Package sorting center demo ready.\n");
    rt_kprintf("Command: package_demo\n");
    return RT_EOK;
}
