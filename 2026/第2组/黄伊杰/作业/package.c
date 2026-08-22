/*
 * package.c - 练习四：快递分拣中心（消息队列）
 *
 * 芯片     : STM32F407ZGT6
 * 开发板   : 星火1号 (RT-Thread STM32F407-RT-SPARK)
 * 开发环境 : RT-Thread Studio
 *
 * 场景:
 *   两个收件线程不断产生快递消息，一个分拣线程从消息队列取出并按目的区域分类。
 *
 * 设计:
 *   - 消息队列容量 5 条（消息大小 = sizeof(struct package_msg)）。
 *   - 两个发件线程，各自产生 5 条快递消息（id 不重复）。
 *   - 分拣线程 rt_mq_recv 接收，打印编号/区域/重量，并累计各项统计。
 *   - 队列满时 rt_mq_send 失败不崩溃，send_failed++ 并稍后重发（记录发送失败次数）。
 *   - 每个发件线程完成后发送一条结束消息（id==0，共 SENDER_NUM 条）。
 *   - 分拣线程收到 SENDER_NUM 条结束消息后打印统计结果并退出、释放资源。
 *
 * 命令行: package_demo
 */

#include <rtthread.h>
#include <rtdevice.h>

#define SENDER_NUM          2
#define MSG_PER_SENDER      5
#define MQ_CAPACITY         5               /* 队列最多 5 条消息 */

#define PKG_STACK           1024
#define PKG_PRIORITY        25
#define PKG_TIMESLICE       5
#define SORTER_PRIORITY     15              /* 高于发件，及时取走防堆积 */

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;      /* 1=华东 east, 2=华南 south, 3=华北 north */
    rt_uint16_t weight;      /* 单位: 克 */
};

static rt_mq_t pkg_mq = RT_NULL;
static volatile rt_uint32_t send_failed = 0;
static volatile rt_uint8_t pkg_running = 0;

static const char *region_name(rt_uint8_t r)
{
    switch (r)
    {
    case 1: return "east";
    case 2: return "south";
    default: return "north";
    }
}

static void sender_entry(void *param)
{
    rt_int32_t sender_no = (rt_int32_t)param;
    rt_uint32_t base = (sender_no == 1) ? 1001 : 2001;
    /* 区域分布: 使两组最终得到 east=4, south=3, north=3 */
    static const rt_uint8_t region_map[MSG_PER_SENDER] = {1, 2, 3, 1, 2};
    struct package_msg msg;
    int i;

    for (i = 0; i < MSG_PER_SENDER; i++)
    {
        msg.id     = base + i;
        msg.region = region_map[i];
        msg.weight = (rt_uint16_t)(1000 + ((msg.id % 100) * 20));  /* 确定性重量 */

        /* 非阻塞发送；队列满时不崩溃，记录失败次数并稍后重发 */
        while (rt_mq_send(pkg_mq, &msg, sizeof(msg)) != RT_EOK)
        {
            send_failed++;
            rt_thread_mdelay(10);           /* 让分拣线程有机会取走 */
        }
    }

    /* 发送结束标记 */
    rt_memset(&msg, 0, sizeof(msg));
    rt_mq_send(pkg_mq, &msg, sizeof(msg));

    return;                              /* 发件线程退出（内核自动清理） */
}

static void sorter_entry(void *param)
{
    struct package_msg msg;
    rt_uint32_t total = 0, total_east = 0, total_south = 0, total_north = 0;
    rt_uint32_t total_weight = 0;
    rt_uint32_t end_count = 0;
    rt_err_t    ret;

    while (1)
    {
        ret = rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (ret != RT_EOK)
        {
            rt_kprintf("mq recv error 0x%x\n", ret);
            continue;
        }

        if (msg.id == 0)                     /* 结束消息 */
        {
            if (++end_count >= SENDER_NUM)
                break;
            continue;
        }

        rt_kprintf("package %u -> %s, weight: %u g\n",
                   msg.id, region_name(msg.region), msg.weight);
        total++;
        total_weight += msg.weight;
        switch (msg.region)
        {
        case 1: total_east++;  break;
        case 2: total_south++; break;
        default: total_north++; break;
        }
    }

    rt_kprintf("total packages: %u\n", total);
    rt_kprintf("east: %u, south: %u, north: %u\n", total_east, total_south, total_north);
    rt_kprintf("total weight: %u g\n", total_weight);
    rt_kprintf("send failed: %u\n", send_failed);

    /* 结束：主动释放资源，命令可重复执行 */
    send_failed = 0;
    rt_mq_delete(pkg_mq);
    pkg_mq      = RT_NULL;
    pkg_running = 0;
    return;                              /* 分拣线程退出（内核自动清理） */
}

int package_demo(void)
{
    rt_thread_t tid_sender[SENDER_NUM];
    rt_thread_t tid_sorter;
    rt_uint32_t i;

    if (pkg_running)
    {
        rt_kprintf("package_demo is still running, ignore\n");
        return -RT_EBUSY;
    }
    pkg_running = 1;
    send_failed = 0;

    /* 消息队列: 容量 MQ_CAPACITY 条，每条消息大小 sizeof(struct package_msg) */
    pkg_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), MQ_CAPACITY, RT_IPC_FLAG_FIFO);
    if (pkg_mq == RT_NULL)
    {
        rt_kprintf("create pkg mq failed\n");
        pkg_running = 0;
        return -RT_ENOMEM;
    }

    tid_sorter = rt_thread_create("pkg_sort", sorter_entry, RT_NULL,
                                  PKG_STACK, SORTER_PRIORITY, PKG_TIMESLICE);
    if (tid_sorter == RT_NULL)
    {
        rt_mq_delete(pkg_mq); pkg_mq = RT_NULL;
        pkg_running = 0;
        rt_kprintf("create sorter thread failed\n");
        return -RT_ENOMEM;
    }

    for (i = 0; i < SENDER_NUM; i++)
    {
        tid_sender[i] = rt_thread_create("pkg_send", sender_entry, (void *)(i + 1),
                                         PKG_STACK, PKG_PRIORITY, PKG_TIMESLICE);
        if (tid_sender[i] == RT_NULL)
        {
            rt_kprintf("create sender thread failed\n");
            while (i--)
                rt_thread_delete(tid_sender[i]);
            rt_thread_delete(tid_sorter);
            rt_mq_delete(pkg_mq); pkg_mq = RT_NULL;
            pkg_running = 0;
            return -RT_ENOMEM;
        }
        rt_thread_startup(tid_sender[i]);
    }

    rt_thread_startup(tid_sorter);
    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, package demo: message queue 2 senders 1 sorter);