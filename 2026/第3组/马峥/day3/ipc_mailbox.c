/*
 * 练习四：快递分拣中心
 * 机制：消息队列（rt_mq）
 *
 * 场景：2个收件线程产生快递，1个分拣线程分类统计
 * 原理：消息队列异步传递 package_msg 结构体
 */

#include <rtthread.h>

#define MQ_MAX_MSGS    5           /* 消息队列容量 */
#define SENDER_COUNT  2           /* 收件线程数 */
#define MSGS_PER_SENDER 5         /* 每个线程发 5 条消息 */

/* 快递消息结构体 */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;   /* 1=华东, 2=华南, 3=华北 */
    rt_uint16_t weight;   /* 重量（克） */
};

/* 结束消息标记（region=0, id=0 表示结束） */
#define MSG_END_MARKER  { 0, 0, 0 }

/* 区域名称 */
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

/* ========== 全局统计 ========== */
static rt_uint32_t total_pkgs    = 0;
static rt_uint32_t east_count    = 0;
static rt_uint32_t south_count  = 0;
static rt_uint32_t north_count  = 0;
static rt_uint32_t total_weight = 0;
static rt_uint32_t send_failed  = 0;

/* 快递 ID 计数器（全局原子递增，简化处理） */
static rt_uint32_t pkg_id_counter = 1000;

/* 消息队列 */
static rt_mq_t mq_package = RT_NULL;

/* ========== 收件线程入口 ========== */
static void sender_entry(void *parameter)
{
    rt_uint32_t sender_no = (rt_uint32_t)parameter;

    for (int i = 0; i < MSGS_PER_SENDER; i++)
    {
        struct package_msg msg;

        /* 生成快递编号（1001~2000，每条+1，由接收线程递增） */
        msg.id = 0; /* 占位，接收线程分配实际 ID */

        /* 随机选择区域和重量（简化：交替分配） */
        msg.region = (sender_no == 1) ? ((i % 3) + 1) : (((i + 1) % 3) + 1);
        msg.weight = 500 + (rt_uint16_t)((sender_no * 100 + i * 37) % 1500); /* 500~2000g */

        /* 发送到消息队列，非阻塞 */
        rt_err_t err = rt_mq_send(mq_package, &msg, sizeof(msg));

        if (err == RT_EOK)
        {
            rt_kprintf("[sender%d] sent pkg (region=%s, weight=%d g)\n",
                       sender_no, region_name(msg.region), msg.weight);
        }
        else
        {
            /* 队列满，记录失败次数，继续尝试 */
            send_failed++;
            rt_kprintf("[sender%d] send FAILED (queue full), failed_count=%d\n",
                       sender_no, send_failed);
            i--;  /* 重试本次 */
            rt_thread_mdelay(10);
        }

        /* 模拟产生间隔 */
        rt_thread_mdelay(100);
    }

    rt_kprintf("[sender%d] all %d packages sent\n", sender_no, MSGS_PER_SENDER);
}

/* ========== 分拣线程入口 ========== */
static void sorter_entry(void *parameter)
{
    (void)parameter;
    rt_err_t err;
    struct package_msg msg;
    rt_size_t recv_size;
    rt_uint32_t end_markers_received = 0;
    const rt_uint32_t need_end_markers = SENDER_COUNT;

    /* 分配快递编号 */
    rt_uint32_t next_pkg_id = 1000;

    while (end_markers_received < need_end_markers)
    {
        /* 从消息队列接收，永久等待 */
        err = rt_mq_recv(mq_package, &msg, sizeof(msg), RT_WAITING_FOREVER);

        if (err != RT_EOK)
            continue;

        /* 检查是否结束标记 */
        if (msg.id == 0 && msg.region == 0 && msg.weight == 0)
        {
            end_markers_received++;
            rt_kprintf("[sorter] received end marker (%u/%u)\n",
                       end_markers_received, need_end_markers);
            continue;
        }

        /* 分配实际快递编号 */
        next_pkg_id++;
        msg.id = next_pkg_id;

        /* 统计 */
        total_pkgs++;
        total_weight += msg.weight;

        switch (msg.region)
        {
        case 1: east_count++;  break;
        case 2: south_count++; break;
        case 3: north_count++; break;
        default: break;
        }

        /* 打印分拣信息 */
        rt_kprintf("package %" RT_DEBUG_NOTAB "u -> %s, weight: %" RT_DEBUG_NOTAB "u g\n",
                   msg.id, region_name(msg.region), msg.weight);
    }

    /* 打印统计结果 */
    rt_kprintf("\n--- Sorting Statistics ---\n");
    rt_kprintf("total packages: %" RT_DEBUG_NOTAB "u\n", total_pkgs);
    rt_kprintf("east: %" RT_DEBUG_NOTAB "u, south: %" RT_DEBUG_NOTAB "u, north: %" RT_DEBUG_NOTAB "u\n",
               east_count, south_count, north_count);
    rt_kprintf("total weight: %" RT_DEBUG_NOTAB "u g\n", total_weight);
    rt_kprintf("send failed: %" RT_DEBUG_NOTAB "u\n", send_failed);
    rt_kprintf("-------------------------\n");
}

/* ========== 线程控制块 ========== */
static struct rt_thread sender_threads[SENDER_COUNT];
static struct rt_thread sorter_thread;
static rt_uint8_t sender_stacks[SENDER_COUNT][768];
static rt_uint8_t sorter_stack[768];

/* ========== MSH 命令入口 ========== */
static int package_demo(void)
{
    rt_thread_t t;

    /* 防止重复执行 */
    t = rt_thread_find("sorter");
    if (t != RT_NULL)
    {
        rt_kprintf("[package_demo] demo already running, skip\n");
        return -1;
    }

    /* 重置统计 */
    total_pkgs = total_weight = send_failed = 0;
    east_count = south_count = north_count = 0;
    pkg_id_counter = 1000;

    /* 创建消息队列 */
    mq_package = rt_mq_create("mq_pkg",
                               sizeof(struct package_msg),
                               MQ_MAX_MSGS,
                               RT_IPC_FLAG_FIFO);
    if (mq_package == RT_NULL)
    {
        rt_kprintf("[package_demo] ERROR: mq create failed\n");
        return -1;
    }

    rt_err_t err;

    /* 创建分拣线程（优先级高于收件线程，确保及时消费） */
    err = rt_thread_init(&sorter_thread, "sorter", sorter_entry,
                         RT_NULL, sorter_stack, sizeof(sorter_stack), 9, 10);
    if (err != RT_EOK) { rt_kprintf("sorter init failed\n"); return -1; }
    rt_thread_startup(&sorter_thread);

    /* 创建 2 个收件线程 */
    for (int i = 0; i < SENDER_COUNT; i++)
    {
        char name[10];
        rt_snprintf(name, sizeof(name), "sender%d", i + 1);

        err = rt_thread_init(&sender_threads[i], name, sender_entry,
                            (void *)(rt_uint32_t)(i + 1),
                            sender_stacks[i], sizeof(sender_stacks[i]),
                            12, 5);
        if (err != RT_EOK) { rt_kprintf("%s init failed\n", name); continue; }
        rt_thread_startup(&sender_threads[i]);
    }

    /* 主线程：等待所有收件线程结束，然后发送结束标记 */
    for (int i = 0; i < SENDER_COUNT; i++)
    {
        char name[10];
        rt_snprintf(name, sizeof(name), "sender%d", i + 1);
        rt_thread_t t = rt_thread_find(name);
        if (t != RT_NULL)
            rt_thread_join_wait(t, 3000);
    }

    /* 向分拣线程发送 SENDER_COUNT 个结束标记 */
    struct package_msg end_msg = MSG_END_MARKER;
    for (int i = 0; i < SENDER_COUNT; i++)
    {
        rt_mq_send(mq_package, &end_msg, sizeof(end_msg));
    }

    rt_kprintf("[package_demo] all sender threads finished, end markers sent\n");
    return 0;
}
MSH_CMD_EXPORT(package_demo, express package sorting with message queue);
