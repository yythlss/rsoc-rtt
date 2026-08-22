/*
 * ============================================================
 * 练习四：快递分拣中心（消息队列）
 *
 * 场景：两个收件线程不断产生快递消息，一个分拣线程从消息
 *       队列中取出快递并按目的地区域分类统计。
 *
 * 设计思路：
 *   1. 消息队列容量 5 条（struct package_msg）
 *   2. 两个发件线程各产生 5 条消息（编号 1001~1005 / 2001~2005，不重复）
 *   3. 队列满时 rt_mq_send 返回失败 → 记录失败次数 + 延时重试，不崩溃
 *   4. 发完 5 条后各发一条"结束消息"（region=0 哨兵）
 *   5. 分拣线程收消息、打印、统计；收到 2 条结束消息后打印统计并退出
 *
 * MSH 命令：package_demo
 * ============================================================
 */

#include <rtthread.h>
#include <rtdevice.h>

#define MQ_MSG_COUNT    5                   /* 队列容量：5 条消息 */
#define MSG_PER_SENDER  5                   /* 每个发件线程产生 5 条 */
#define SENDER_COUNT    2                   /* 发件线程数量 */

#define REGION_EAST     1                   /* 华东 */
#define REGION_SOUTH    2                   /* 华南 */
#define REGION_NORTH    3                   /* 华北 */
#define MSG_END         0                   /* region=0 表示结束消息 */

/* 快递消息结构（作业给定） */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

static rt_mq_t pkg_mq = RT_NULL;            /* 快递消息队列 */
static volatile rt_uint32_t send_failed = 0;/* 队列满导致的发送失败次数 */
static volatile rt_uint8_t threads_done = 0;/* 已结束的线程数 */
static volatile rt_uint8_t task_running = 0;/* 任务是否在运行（防重复启动） */

/* 区域编号 → 名称 */
static const char *region_name(rt_uint8_t r)
{
    switch (r)
    {
    case REGION_EAST:  return "east";
    case REGION_SOUTH: return "south";
    case REGION_NORTH: return "north";
    default:           return "unknown";
    }
}

/* 发送一条消息：队列满就记录失败并延时重试，直到发出去（不崩溃） */
static void send_package(const struct package_msg *msg)
{
    while (rt_mq_send(pkg_mq, msg, sizeof(*msg)) != RT_EOK)
    {
        send_failed++;                      /* 队列满，记一次失败 */
        rt_thread_mdelay(50);               /* 等分拣线程腾出空间 */
    }
}

/* ------------------------------------------------------------
 * 发件线程：产生 5 条快递消息 + 1 条结束消息
 * param 传编号前缀（1000 或 2000），保证两线程编号不重复
 * ---------------------------------------------------------- */
static void sender_entry(void *param)
{
    int base = (int)(rt_ubase_t)param;
    struct package_msg msg;
    int i;

    for (i = 0; i < MSG_PER_SENDER; i++)
    {
        msg.id = (rt_uint32_t)(base + i + 1);        /* 1001..1005 / 2001..2005 */
        msg.region = (rt_uint8_t)((i % 3) + 1);      /* 区域轮流 华东/华南/华北 */
        msg.weight = (rt_uint16_t)(500 + (i * 137) % 2000);  /* 伪随机重量 */
        send_package(&msg);
    }

    /* 发完 5 条后，发送一条结束消息（region=0 哨兵） */
    msg.id = 0;
    msg.region = MSG_END;
    msg.weight = 0;
    send_package(&msg);

    threads_done++;
}

/* ------------------------------------------------------------
 * 分拣线程：收消息 → 打印 + 统计；收到全部结束消息后汇总退出
 * ---------------------------------------------------------- */
static void sorter_entry(void *param)
{
    struct package_msg msg;
    rt_uint32_t total = 0, total_weight = 0;
    rt_uint32_t east = 0, south = 0, north = 0;
    rt_uint8_t end_count = 0;               /* 收到的结束消息数 */

    while (end_count < SENDER_COUNT)
    {
        if (rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
            break;                          /* 异常兜底 */

        if (msg.region == MSG_END)          /* 结束消息：计数，不参与统计 */
        {
            end_count++;
            continue;
        }

        /* 正常快递：打印 + 统计 */
        total++;
        total_weight += msg.weight;
        if (msg.region == REGION_EAST)
            east++;
        else if (msg.region == REGION_SOUTH)
            south++;
        else if (msg.region == REGION_NORTH)
            north++;

        rt_kprintf("package %d -> %s, weight: %d g\n",
                   msg.id, region_name(msg.region), msg.weight);
    }

    /* 打印统计结果 */
    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", east, south, north);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", send_failed);

    threads_done++;
}

/* ------------------------------------------------------------
 * 控制线程：创建队列 + 3 个线程，等全部结束后清理
 * ---------------------------------------------------------- */
static void package_ctrl_entry(void *param)
{
    rt_thread_t sorter, s1, s2;

    /* 1. 创建消息队列（容量 5 条），检查创建结果 */
    pkg_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), MQ_MSG_COUNT,
                          RT_IPC_FLAG_PRIO);
    if (pkg_mq == RT_NULL)
    {
        rt_kprintf("package: create message queue failed\n");
        task_running = 0;
        return;
    }

    /* 2. 创建分拣线程（优先级高，尽快消费）+ 两个发件线程 */
    sorter = rt_thread_create("sorter", sorter_entry, RT_NULL, 2048, 15, 10);
    s1 = rt_thread_create("sender1", sender_entry, (void *)(rt_ubase_t)1000, 2048, 20, 10);
    s2 = rt_thread_create("sender2", sender_entry, (void *)(rt_ubase_t)2000, 2048, 20, 10);
    if (sorter == RT_NULL || s1 == RT_NULL || s2 == RT_NULL)
    {
        rt_kprintf("package: create thread failed\n");
        rt_mq_delete(pkg_mq);
        pkg_mq = RT_NULL;
        task_running = 0;
        return;
    }
    rt_thread_startup(sorter);
    rt_thread_startup(s1);
    rt_thread_startup(s2);

    /* 3. 等 3 个线程全部结束 */
    while (threads_done < 3)
        rt_thread_mdelay(50);

    /* 4. 收尾：删队列、复位状态 */
    rt_mq_delete(pkg_mq);
    pkg_mq = RT_NULL;
    send_failed = 0;
    threads_done = 0;
    task_running = 0;
    rt_kprintf("package: demo finished, resources released\n");
}

/* ------------------------------------------------------------
 * MSH 命令入口：package_demo
 * ---------------------------------------------------------- */
static void package_demo(void)
{
    rt_thread_t ctrl;

    if (task_running)
    {
        rt_kprintf("package_demo is already running\n");
        return;
    }
    task_running = 1;

    ctrl = rt_thread_create("package_ctrl", package_ctrl_entry, RT_NULL, 2048, 12, 10);
    if (ctrl == RT_NULL)
    {
        rt_kprintf("package_demo: create control thread failed\n");
        task_running = 0;
        return;
    }
    rt_thread_startup(ctrl);
}

/* 导出为 msh 命令 */
MSH_CMD_EXPORT(package_demo, package demo 2 senders 1 sorter queue size 5);
