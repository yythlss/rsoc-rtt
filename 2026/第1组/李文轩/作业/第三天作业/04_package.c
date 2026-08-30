/*
 * 练习四：快递分拣中心（消息队列）
 *
 * 场景：两个收件线程不断产生快递消息，一个分拣线程从消息队列取出快递
 *       并按照目的地区域分类统计。
 * 使用消息队列（容量 5 条）在生产者与消费者之间传递快递信息；
 * 队列满时发送线程不崩溃，而是记录发送失败次数；
 * 两个发送线程全部完成后，由命令线程发送一条结束消息，
 * 分拣线程收到结束消息后打印统计结果并退出。
 *
 * MSH 命令：package_demo
 *
 * 注意：region 取值 1=华东(east)、2=华南(south)、3=华北(north)；
 *       region = 0 为约定好的"结束消息"标记（正常快递不会使用该值）。
 */

#include <rtthread.h>

#define PKG_QUEUE_CAP       5  /* 消息队列容量 */
#define PKG_PER_SENDER      5  /* 每个发送线程产生的快递数量 */
#define PKG_END_REGION      0  /* 结束消息标记（有效区域为 1~3） */
#define PKG_BASE_A          1000 /* 发送线程 A 的快递编号基数 */
#define PKG_BASE_B          2000 /* 发送线程 B 的快递编号基数 */

/* 快递消息结构 */
struct package_msg
{
    rt_uint32_t id;     /* 快递编号 */
    rt_uint8_t  region; /* 目的地区域：1=华东 2=华南 3=华北 */
    rt_uint16_t weight; /* 重量（g） */
};

static rt_mq_t s_pkg_mq = RT_NULL;    /* 消息队列 */
static rt_sem_t s_send_done = RT_NULL; /* 发送线程完成计数信号量 */
static rt_sem_t s_sort_done = RT_NULL; /* 分拣线程完成信号量 */
static volatile int s_fail_a = 0;      /* 发送线程 A 的发送失败次数 */
static volatile int s_fail_b = 0;      /* 发送线程 B 的发送失败次数 */
static volatile rt_bool_t s_running = RT_FALSE; /* 防重复启动 */

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

/* 收件（发送）线程：产生 5 条快递消息，队列满时记录失败次数 */
static void sender_thread(void *param)
{
    rt_uint32_t base = (rt_uint32_t)(rt_ubase_t)param;
    int fail = 0;
    int i;

    for (i = 1; i <= PKG_PER_SENDER; i++)
    {
        struct package_msg msg;

        msg.id     = base + i;                  /* 编号不重复：1001~1005 / 2001~2005 */
        msg.region = (rt_uint8_t)(i % 3) + 1;   /* 区域 1/2/3 循环 */
        msg.weight = (rt_uint16_t)(500 + (msg.id * 37) % 1000);

        if (rt_mq_send(s_pkg_mq, &msg, sizeof(msg)) != RT_EOK)
        {
            fail++; /* 队列已满：记录失败，不崩溃 */
        }
        rt_thread_mdelay(20); /* 收件间隔 */
    }

    if (base == PKG_BASE_A)
    {
        s_fail_a = fail;
    }
    else
    {
        s_fail_b = fail;
    }

    /* 通知命令线程：本发送线程已完成 */
    rt_sem_release(s_send_done);
}

/* 分拣线程：接收快递消息并分类统计，收到结束消息后退出 */
static void sorter_thread(void *param)
{
    int total = 0;
    int weight_sum = 0;
    int cnt[3] = {0, 0, 0};

    while (RT_TRUE)
    {
        struct package_msg msg;
        rt_err_t res = rt_mq_recv(s_pkg_mq, &msg, sizeof(msg), 2000);

        if (res != RT_EOK)
        {
            continue; /* 暂时没有消息，继续等待 */
        }
        if (msg.region == PKG_END_REGION)
        {
            break; /* 收到结束消息，退出 */
        }

        total++;
        if (msg.region >= 1 && msg.region <= 3)
        {
            cnt[msg.region - 1]++;
        }
        weight_sum += msg.weight;
        rt_kprintf("package %d -> %s, weight: %d g\n",
                   msg.id, region_name(msg.region), msg.weight);
        rt_thread_mdelay(50); /* 分拣耗时 */
    }

    /* 打印统计结果 */
    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", cnt[0], cnt[1], cnt[2]);
    rt_kprintf("total weight: %d g\n", weight_sum);

    /* 通知命令线程：分拣完成 */
    rt_sem_release(s_sort_done);
}

/* MSH 命令：package_demo */
static int package_demo(int argc, char *argv[])
{
    rt_thread_t send_a = RT_NULL;
    rt_thread_t send_b = RT_NULL;
    rt_thread_t sorter = RT_NULL;

    if (s_running)
    {
        rt_kprintf("package_demo is already running\n");
        return -1;
    }
    s_running = RT_TRUE;
    s_fail_a = 0;
    s_fail_b = 0;

    /* 1. 创建容量为 5 的消息队列 */
    s_pkg_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), PKG_QUEUE_CAP,
                            RT_IPC_FLAG_FIFO);
    if (s_pkg_mq == RT_NULL)
    {
        rt_kprintf("create package message queue failed\n");
        s_running = RT_FALSE;
        return -1;
    }

    /* 2. 创建两个完成信号量 */
    s_send_done = rt_sem_create("pk_send", 0, RT_IPC_FLAG_FIFO);
    s_sort_done = rt_sem_create("pk_sort", 0, RT_IPC_FLAG_FIFO);
    if (s_send_done == RT_NULL || s_sort_done == RT_NULL)
    {
        rt_kprintf("create done semaphore failed\n");
        if (s_send_done) rt_sem_delete(s_send_done);
        if (s_sort_done) rt_sem_delete(s_sort_done);
        rt_mq_delete(s_pkg_mq);
        s_send_done = RT_NULL;
        s_sort_done = RT_NULL;
        s_pkg_mq = RT_NULL;
        s_running = RT_FALSE;
        return -1;
    }

    /* 3. 创建 2 个发送线程 + 1 个分拣线程，并检查创建结果 */
    send_a = rt_thread_create("send_a", sender_thread, (void *)(rt_ubase_t)PKG_BASE_A,
                              1024, 8, 10);
    send_b = rt_thread_create("send_b", sender_thread, (void *)(rt_ubase_t)PKG_BASE_B,
                              1024, 8, 10);
    sorter = rt_thread_create("sorter", sorter_thread, RT_NULL, 1024, 9, 10);
    if (send_a == RT_NULL || send_b == RT_NULL || sorter == RT_NULL)
    {
        rt_kprintf("create package thread failed\n");
        if (send_a) rt_thread_delete(send_a);
        if (send_b) rt_thread_delete(send_b);
        if (sorter) rt_thread_delete(sorter);
        rt_sem_delete(s_send_done);
        rt_sem_delete(s_sort_done);
        rt_mq_delete(s_pkg_mq);
        s_send_done = RT_NULL;
        s_sort_done = RT_NULL;
        s_pkg_mq = RT_NULL;
        s_running = RT_FALSE;
        return -1;
    }

    /* 4. 启动分拣线程与两个发送线程 */
    rt_thread_startup(sorter);
    rt_thread_startup(send_a);
    rt_thread_startup(send_b);

    /* 5. 等待两个发送线程完成 */
    if (rt_sem_take(s_send_done, 20000) != RT_EOK)
    {
        rt_kprintf("package_demo wait sender timeout\n");
    }
    if (rt_sem_take(s_send_done, 20000) != RT_EOK)
    {
        rt_kprintf("package_demo wait sender timeout\n");
    }

    /* 6. 两个发送线程完成后，发送一条结束消息（失败则重试，保证分拣线程能退出） */
    {
        struct package_msg end_msg = {0, PKG_END_REGION, 0};
        while (rt_mq_send(s_pkg_mq, &end_msg, sizeof(end_msg)) != RT_EOK)
        {
            rt_thread_mdelay(100);
        }
    }

    /* 7. 等待分拣线程完成 */
    if (rt_sem_take(s_sort_done, 20000) != RT_EOK)
    {
        rt_kprintf("package_demo wait sorter timeout\n");
    }

    /* 8. 打印发送失败统计并释放资源 */
    rt_kprintf("send failed: %d\n", s_fail_a + s_fail_b);

    rt_mq_delete(s_pkg_mq);
    rt_sem_delete(s_send_done);
    rt_sem_delete(s_sort_done);
    s_pkg_mq = RT_NULL;
    s_send_done = RT_NULL;
    s_sort_done = RT_NULL;
    s_running = RT_FALSE;

    return 0;
}
MSH_CMD_EXPORT(package_demo, express sorting center with message queue);
