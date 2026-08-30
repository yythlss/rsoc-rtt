/*
 * 【RSOC26】第3天作业 - 练习四：快递分拣中心
 *
 * 场景：两个发件线程产生快递信息，一个分拣线程通过消息队列接收并按区域分类。
 * 消息结构包含区域字段，三个区域：华东(1)、华南(2)、华北(3)。
 */

#include <rtthread.h>

#define SENDER_COUNT    2
#define MSG_PER_SENDER  5
#define QUEUE_SIZE      5
#define MSG_TIMEOUT     500    /* 发送超时（ms） */
#define REGION_EAST     1
#define REGION_SOUTH    2
#define REGION_NORTH    3

#define REGION_NAME(r)  ((r) == 1 ? "east" : (r) == 2 ? "south" : "north")

/* 快递消息结构 */
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;
    rt_uint16_t weight;
};

/* 统计（使用原子操作保证线程安全） */
static rt_atomic_t total_packages = 0;
static rt_atomic_t total_weight   = 0;
static rt_atomic_t east_count     = 0;
static rt_atomic_t south_count    = 0;
static rt_atomic_t north_count    = 0;
static rt_atomic_t send_failed    = 0;

/* 结束信号量：发件线程全部完成后通知分拣线程 */
static rt_sem_t done_sem = RT_NULL;
static rt_atomic_t senders_finished = 0;

/*
 * 发件线程
 */
static void sender_thread_entry(void *param)
{
    rt_uint32_t base_id = (rt_uint32_t)(rt_uintptr_t)param;
    struct package_msg msg;
    int i;

    for (i = 0; i < MSG_PER_SENDER; i++)
    {
        msg.id     = base_id + i;
        msg.region = 1 + (i % 3);                       /* 轮换 1/2/3 */
        msg.weight = 200 + (rt_uint32_t)(rt_tick_get() % 800);  /* 200~999g */

        if (rt_mq_send(package_mq, &msg, sizeof(msg)) == RT_EOK)
        {
            rt_kprintf("sent package %u -> %s, weight: %d g\n",
                       msg.id, REGION_NAME(msg.region), msg.weight);
        }
        else
        {
            rt_atomic_inc(&send_failed);
            rt_kprintf("send failed for package %u (queue full)\n", msg.id);
        }

        rt_thread_mdelay(200);
    }

    rt_atomic_inc(&senders_finished);
    if (rt_atomic_load(&senders_finished) >= SENDER_COUNT)
    {
        rt_sem_release(done_sem);
    }
}

/*
 * 分拣线程
 */
static void sorter_thread_entry(void *param)
{
    struct package_msg msg;
    rt_err_t result;
    int expected = SENDER_COUNT * MSG_PER_SENDER;

    rt_kprintf("sorter: waiting for packages...\n");

    while (1)
    {
        result = rt_mq_recv(package_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (result != RT_EOK)
        {
            rt_kprintf("sorter: recv error %d\n", result);
            continue;
        }

        /* 统计 */
        rt_atomic_inc(&total_packages);
        rt_atomic_add(&total_weight, msg.weight);

        if      (msg.region == REGION_EAST)  rt_atomic_inc(&east_count);
        else if (msg.region == REGION_SOUTH) rt_atomic_inc(&south_count);
        else                                  rt_atomic_inc(&north_count);

        rt_kprintf("package %u -> %s, weight: %d g\n",
                   msg.id, REGION_NAME(msg.region), msg.weight);

        /* 检查是否已收到所有预期消息 */
        if (rt_atomic_load(&total_packages) >= expected)
        {
            /* 等发件线程全部确认结束 */
            rt_sem_take(done_sem, 5000);

            rt_kprintf("\n=== Sorter Statistics ===\n");
            rt_kprintf("total packages: %d\n",  rt_atomic_load(&total_packages));
            rt_kprintf("east: %d, south: %d, north: %d\n",
                       rt_atomic_load(&east_count),
                       rt_atomic_load(&south_count),
                       rt_atomic_load(&north_count));
            rt_kprintf("total weight: %d g\n",  rt_atomic_load(&total_weight));
            rt_kprintf("send failed: %d\n",      rt_atomic_load(&send_failed));
            break;
        }
    }
}

/* 消息队列句柄（全局） */
static rt_mq_t package_mq = RT_NULL;

static void package_demo(int argc, char **argv)
{
    rt_thread_t sender[2], sorter;

    if (package_mq != RT_NULL)
    {
        rt_kprintf("package_demo already running\n");
        return;
    }

    /* 创建消息队列：5 条，每条 sizeof(struct package_msg) */
    package_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg),
                               QUEUE_SIZE, RT_IPC_FLAG_FIFO);
    if (package_mq == RT_NULL)
    {
        rt_kprintf("Failed to create message queue\n");
        return;
    }

    /* 创建结束信号量 */
    done_sem = rt_sem_create("done_sem", 0, RT_IPC_FLAG_PRIO);
    if (done_sem == RT_NULL)
    {
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        return;
    }

    rt_kprintf("=== Package Sorting Demo: %d senders, %d msgs each ===\n",
               SENDER_COUNT, MSG_PER_SENDER);

    sender[0] = rt_thread_create("sender1", sender_thread_entry,
                                 (void *)1001, 1024, 22, 10);
    sender[1] = rt_thread_create("sender2", sender_thread_entry,
                                 (void *)2001, 1024, 22, 10);
    sorter   = rt_thread_create("sorter", sorter_thread_entry,
                                 RT_NULL, 1024, 21, 10);

    if (sender[0]) rt_thread_startup(sender[0]);
    if (sender[1]) rt_thread_startup(sender[1]);
    if (sorter)    rt_thread_startup(sorter);
}

MSH_CMD_EXPORT(package_demo, package sorting with message queue);
