/*
 * package_demo.c - 练习四：快递分拣中心（消息队列）
 *
 * 场景：两个发件线程不断产生快递，一个分拣线程从消息队列取出快递并按
 *       区域分类统计。队列满时发送线程不崩溃，记录发送失败次数。
 *
 * 消息结构：id 唯一编号（发件线程1 用 1001~1005，线程2 用 2001~2005）；
 *           region 1=华东 2=华南 3=华北；weight 重量。
 * 结束机制：每个发件线程发完 5 条后发一条 id=0 的结束消息，weight 字段
 *           带上本线程的发送失败次数，分拣线程收到 2 条结束消息后统计退出。
 *
 * 用法：MSH 中输入 package_demo 启动。
 */
#include <rtthread.h>

#define PKG_QUEUE_SIZE 5
#define PKG_PER_SENDER 5

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;
    rt_uint16_t weight;
};

static rt_mq_t pkg_mq = RT_NULL;

static void sender_entry(void *param)
{
    int sender = (int)(rt_ubase_t)param;
    int base = (sender == 1) ? 1001 : 2001;
    int i;
    rt_uint16_t failed = 0;

    for (i = 0; i < PKG_PER_SENDER; i++)
    {
        struct package_msg msg;

        msg.id = (rt_uint32_t)(base + i);
        msg.region = (rt_uint8_t)((i % 3) + 1);
        msg.weight = (rt_uint16_t)(500 + (i * 137) % 1500);

        while (1)
        {
            if (pkg_mq->entry >= pkg_mq->max_msgs)
            {
                failed++;
                rt_thread_mdelay(20);
            }
            else
            {
                rt_mq_send(pkg_mq, &msg, sizeof(msg));
                break;
            }
        }
        rt_thread_mdelay(10);
    }

    {
        struct package_msg end;

        end.id = 0;
        end.region = 0;
        end.weight = failed;

        while (pkg_mq->entry >= pkg_mq->max_msgs)
            rt_thread_mdelay(20);
        rt_mq_send(pkg_mq, &end, sizeof(end));
    }
}

static void sort_entry(void *param)
{
    struct package_msg msg;
    int end_count = 0;
    int total = 0, east = 0, south = 0, north = 0;
    rt_uint32_t total_weight = 0;
    rt_uint32_t total_failed = 0;

    while (1)
    {
        if (rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
            continue;

        if (msg.id == 0)
        {
            total_failed += msg.weight;
            end_count++;
            if (end_count >= 2)
                break;
            continue;
        }

        total++;
        total_weight += msg.weight;

        switch (msg.region)
        {
        case 1:
            east++;
            rt_kprintf("package %d -> east, weight: %d g\n", msg.id, msg.weight);
            break;
        case 2:
            south++;
            rt_kprintf("package %d -> south, weight: %d g\n", msg.id, msg.weight);
            break;
        case 3:
            north++;
            rt_kprintf("package %d -> north, weight: %d g\n", msg.id, msg.weight);
            break;
        default:
            break;
        }

        rt_thread_mdelay(30);
    }

    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", east, south, north);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", total_failed);
}

static void package_demo(int argc, char **argv)
{
    rt_thread_t t;

    if (pkg_mq != RT_NULL)
    {
        rt_kprintf("package demo is already running\n");
        return;
    }

    pkg_mq = rt_mq_create("pkgmq", sizeof(struct package_msg), PKG_QUEUE_SIZE, RT_IPC_FLAG_FIFO);
    if (pkg_mq == RT_NULL)
    {
        rt_kprintf("create message queue failed\n");
        return;
    }

    t = rt_thread_create("sender1", sender_entry, (void *)1, 1024, 20, 5);
    if (t != RT_NULL) rt_thread_startup(t);

    t = rt_thread_create("sender2", sender_entry, (void *)2, 1024, 20, 5);
    if (t != RT_NULL) rt_thread_startup(t);

    t = rt_thread_create("sorter", sort_entry, RT_NULL, 1024, 19, 5);
    if (t != RT_NULL) rt_thread_startup(t);
}
MSH_CMD_EXPORT(package_demo, package sorting demo with message queue);
