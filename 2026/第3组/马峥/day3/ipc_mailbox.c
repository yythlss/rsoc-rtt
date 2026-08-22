/*
 * 练习四：快递分拣中心
 * 机制：消息队列（rt_mq）+ 完成信号量
 *
 * 场景：2个收件线程产生快递，1个分拣线程分类统计
 * 原理：消息队列异步传递 package_msg 结构体，完成信号量同步线程退出
 */

#include <rtthread.h>

#define MQ_MAX_MSGS      5           /* 消息队列容量 */
#define SENDER_COUNT     2           /* 收件线程数 */
#define MSGS_PER_SENDER  5           /* 每个线程发 5 条消息 */
#define MQ_MSG_SIZE      8           /* package_msg 结构体大小（8字节对齐） */

/* 快递消息结构体 */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;   /* 1=华东, 2=华南, 3=华北 */
    rt_uint16_t weight;   /* 重量（克） */
};

/* 结束消息标记 */
#define MSG_END_MARKER  { 0, 0, 0 }

/* ========== 区域名称 ========== */
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

/* ========== IPC 对象（静态初始化） ========== */
/* 消息队列：静态内存池 */
static struct rt_messagequeue mq_pkg;
static rt_uint8_t mq_pool[MQ_MAX_MSGS * MQ_MSG_SIZE];

/* 完成信号量：等待收件线程全部完成 */
static struct rt_semaphore sem_done;

/* ========== 收件线程入口 ========== */
static void sender_entry(void *parameter)
{
    rt_uint32_t sender_no = (rt_uint32_t)parameter;

    for (int i = 0; i < MSGS_PER_SENDER; i++)
    {
        struct package_msg msg;

        msg.id = 0;  /* 由分拣线程分配实际编号 */
        msg.region = (sender_no == 1) ? ((i % 3) + 1) : (((i + 1) % 3) + 1);
        msg.weight = 500 + (rt_uint16_t)((sender_no * 100 + i * 37) % 1500);

        rt_err_t err = rt_mq_send(&mq_pkg, &msg, sizeof(msg));

        if (err == RT_EOK)
        {
            rt_kprintf("[sender%u] sent pkg (region=%s, weight=%u g)\n",
                       sender_no, region_name(msg.region), msg.weight);
        }
        else
        {
            send_failed++;
            rt_kprintf("[sender%u] send FAILED (queue full), failed=%u\n",
                       sender_no, send_failed);
            i--;  /* 重试本次 */
            rt_thread_mdelay(10);
        }

        rt_thread_mdelay(100);   /* 模拟产生间隔 */
    }

    rt_kprintf("[sender%u] all %d packages sent\n", sender_no, MSGS_PER_SENDER);

    /* 发件完成，释放完成信号量 */
    rt_sem_release(&sem_done);
}

/* ========== 分拣线程入口 ========== */
static void sorter_entry(void *parameter)
{
    (void)parameter;
    rt_err_t err;
    struct package_msg msg;
    rt_uint32_t end_markers_received = 0;
    const rt_uint32_t need_end_markers = SENDER_COUNT;
    rt_uint32_t next_pkg_id = 1000;

    while (end_markers_received < need_end_markers)
    {
        err = rt_mq_recv(&mq_pkg, &msg, sizeof(msg), RT_WAITING_FOREVER);

        if (err != RT_EOK)
            continue;

        /* 检查结束标记 */
        if (msg.id == 0 && msg.region == 0 && msg.weight == 0)
        {
            end_markers_received++;
            rt_kprintf("[sorter] received end marker (%u/%u)\n",
                       end_markers_received, need_end_markers);
            continue;
        }

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

        rt_kprintf("package %u -> %s, weight: %u g\n",
                   msg.id, region_name(msg.region), msg.weight);
    }

    /* 打印统计结果 */
    rt_kprintf("\n--- Sorting Statistics ---\n");
    rt_kprintf("total packages: %u\n", total_pkgs);
    rt_kprintf("east: %u, south: %u, north: %u\n",
               east_count, south_count, north_count);
    rt_kprintf("total weight: %u g\n", total_weight);
    rt_kprintf("send failed: %u\n", send_failed);
    rt_kprintf("-------------------------\n");

    rt_mq_detach(&mq_pkg);
}

/* ========== 线程控制块（需对齐） ========== */
#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static rt_uint8_t sender_stacks[SENDER_COUNT][768];
#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static rt_uint8_t sorter_stack[768];

static struct rt_thread sender_threads[SENDER_COUNT];
static struct rt_thread sorter_thread;

/* ========== MSH 命令入口 ========== */
int package_demo(void)
{
    rt_thread_t t;
    rt_err_t err;

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

    /* 初始化完成信号量（初始值 0） */
    err = rt_sem_init(&sem_done, "sem_done", 0, RT_IPC_FLAG_PRIO);
    if (err != RT_EOK)
    {
        rt_kprintf("[package_demo] ERROR: sem init failed: %d\n", err);
        return -1;
    }

    /* 初始化消息队列（静态） */
    err = rt_mq_init(&mq_pkg, "mq_pkg",
                     &mq_pool[0],
                     MQ_MSG_SIZE,
                     sizeof(mq_pool),
                     RT_IPC_FLAG_PRIO);
    if (err != RT_EOK)
    {
        rt_kprintf("[package_demo] ERROR: mq init failed: %d\n", err);
        return -1;
    }

    /* 创建分拣线程（优先级高于收件线程） */
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

    /* 主线程：等待所有收件线程完成（SENDER_COUNT 次 sem take） */
    for (int i = 0; i < SENDER_COUNT; i++)
    {
        rt_sem_take(&sem_done, RT_WAITING_FOREVER);
    }

    /* 向分拣线程发送结束标记 */
    struct package_msg end_msg = MSG_END_MARKER;
    for (int i = 0; i < SENDER_COUNT; i++)
    {
        rt_mq_send(&mq_pkg, &end_msg, sizeof(end_msg));
    }

    rt_sem_detach(&sem_done);
    rt_kprintf("[package_demo] all sender threads finished, end markers sent\n");
    return 0;
}
MSH_CMD_EXPORT(package_demo, express package sorting with message queue);
