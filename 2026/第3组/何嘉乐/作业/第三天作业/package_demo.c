#include <rtthread.h>

#define PACKAGE_QUEUE_SIZE      5
#define PACKAGE_PER_SENDER      5
#define PACKAGE_END_ID          0xFFFFFFFFU

#define THREAD_STACK_SIZE       1024
#define SORT_PRIORITY           19
#define SEND_PRIORITY           20
#define THREAD_TIMESLICE        10

#define REGION_EAST             1
#define REGION_SOUTH            2
#define REGION_NORTH            3

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

struct sender_info
{
    rt_uint32_t base_id;
    rt_uint8_t sender_id;
};

static rt_mq_t package_mq = RT_NULL;

static rt_uint8_t demo_running = 0;
static rt_uint8_t sender_finished = 0;

static rt_uint32_t send_failed = 0;

static struct sender_info sender_info[2] =
{
    {1000, 1},
    {2000, 2}
};


/* 获取区域名称 */
static const char *get_region_name(rt_uint8_t region)
{
    switch (region)
    {
    case REGION_EAST:
        return "east";

    case REGION_SOUTH:
        return "south";

    case REGION_NORTH:
        return "north";

    default:
        return "unknown";
    }
}


/* 发送结束消息 */
static void send_end_message(void)
{
    struct package_msg msg;
    rt_err_t result;

    msg.id = PACKAGE_END_ID;
    msg.region = 0;
    msg.weight = 0;

    while (1)
    {
        result = rt_mq_send(package_mq,
                            &msg,
                            sizeof(msg));

        if (result == RT_EOK)
        {
            break;
        }

        rt_thread_mdelay(20);
    }
}


/* 快递发送线程 */
static void sender_entry(void *parameter)
{
    struct sender_info *info;
    struct package_msg msg;

    rt_uint8_t i;
    rt_uint8_t finished;
    rt_err_t result;

    info = (struct sender_info *)parameter;

    for (i = 1; i <= PACKAGE_PER_SENDER; i++)
    {
        /* 生成快递信息 */
        msg.id = info->base_id + i;
        msg.region = ((i + info->sender_id - 2) % 3) + 1;
        msg.weight = 700 + info->sender_id * 100 + i * 80;

        while (1)
        {
            /* 发送快递消息 */
            result = rt_mq_send(package_mq,
                                &msg,
                                sizeof(msg));

            if (result == RT_EOK)
            {
                break;
            }

            if (result == -RT_EFULL)
            {
                /* 记录队列满 */
                rt_enter_critical();
                send_failed++;
                rt_exit_critical();

                rt_thread_mdelay(50);
            }
            else
            {
                rt_kprintf("package %d send error: %d\n",
                           msg.id,
                           result);

                rt_thread_mdelay(50);
            }
        }

        rt_thread_mdelay(50);
    }

    /* 记录发送线程完成 */
    rt_enter_critical();

    sender_finished++;
    finished = sender_finished;

    rt_exit_critical();

    /* 最后一个发送线程发送结束消息 */
    if (finished == 2)
    {
        send_end_message();
    }
}


/* 快递分拣线程 */
static void sorter_entry(void *parameter)
{
    struct package_msg msg;

    rt_uint32_t total_packages = 0;
    rt_uint32_t east_count = 0;
    rt_uint32_t south_count = 0;
    rt_uint32_t north_count = 0;
    rt_uint32_t total_weight = 0;

    rt_uint32_t failed;
    rt_err_t result;

    while (1)
    {
        /* 等待快递消息 */
        result = rt_mq_recv(package_mq,
                            &msg,
                            sizeof(msg),
                            RT_WAITING_FOREVER);

        if (result != RT_EOK)
        {
            rt_kprintf("receive package failed: %d\n",
                       result);
            continue;
        }

        /* 检查结束消息 */
        if (msg.id == PACKAGE_END_ID)
        {
            break;
        }

        rt_kprintf("package %d -> %s, weight: %d g\n",
                   msg.id,
                   get_region_name(msg.region),
                   msg.weight);

        /* 统计快递 */
        total_packages++;
        total_weight += msg.weight;

        switch (msg.region)
        {
        case REGION_EAST:
            east_count++;
            break;

        case REGION_SOUTH:
            south_count++;
            break;

        case REGION_NORTH:
            north_count++;
            break;

        default:
            break;
        }

        /* 模拟分拣过程 */
        rt_thread_mdelay(100);
    }

    rt_enter_critical();
    failed = send_failed;
    rt_exit_critical();

    /* 打印统计结果 */
    rt_kprintf("total packages: %d\n",
               total_packages);

    rt_kprintf("east: %d, south: %d, north: %d\n",
               east_count,
               south_count,
               north_count);

    rt_kprintf("total weight: %d g\n",
               total_weight);

    rt_kprintf("send failed: %d\n",
               failed);

    /* 删除消息队列 */
    if (package_mq != RT_NULL)
    {
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
    }

    rt_enter_critical();

    sender_finished = 0;
    send_failed = 0;
    demo_running = 0;

    rt_exit_critical();

    rt_kprintf("package demo finished\n");
}


/* 快递分拣实验 */
static void package_demo(void)
{
    rt_thread_t sender1_tid;
    rt_thread_t sender2_tid;
    rt_thread_t sorter_tid;

    /* 防止重复启动 */
    rt_enter_critical();

    if (demo_running)
    {
        rt_exit_critical();

        rt_kprintf("package demo is already running\n");
        return;
    }

    demo_running = 1;
    sender_finished = 0;
    send_failed = 0;

    rt_exit_critical();

    /* 创建消息队列 */
    package_mq = rt_mq_create("pkg_mq",
                              sizeof(struct package_msg),
                              PACKAGE_QUEUE_SIZE,
                              RT_IPC_FLAG_FIFO);

    if (package_mq == RT_NULL)
    {
        rt_kprintf("create package queue failed\n");

        demo_running = 0;
        return;
    }

    /* 创建发送线程 1 */
    sender1_tid = rt_thread_create("pkg_send1",
                                   sender_entry,
                                   &sender_info[0],
                                   THREAD_STACK_SIZE,
                                   SEND_PRIORITY,
                                   THREAD_TIMESLICE);

    if (sender1_tid == RT_NULL)
    {
        rt_kprintf("create sender1 thread failed\n");

        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        demo_running = 0;

        return;
    }

    /* 创建发送线程 2 */
    sender2_tid = rt_thread_create("pkg_send2",
                                   sender_entry,
                                   &sender_info[1],
                                   THREAD_STACK_SIZE,
                                   SEND_PRIORITY,
                                   THREAD_TIMESLICE);

    if (sender2_tid == RT_NULL)
    {
        rt_kprintf("create sender2 thread failed\n");

        rt_thread_delete(sender1_tid);

        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        demo_running = 0;

        return;
    }

    /* 创建分拣线程 */
    sorter_tid = rt_thread_create("pkg_sort",
                                  sorter_entry,
                                  RT_NULL,
                                  THREAD_STACK_SIZE,
                                  SORT_PRIORITY,
                                  THREAD_TIMESLICE);

    if (sorter_tid == RT_NULL)
    {
        rt_kprintf("create sorter thread failed\n");

        rt_thread_delete(sender1_tid);
        rt_thread_delete(sender2_tid);

        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
        demo_running = 0;

        return;
    }

    rt_kprintf("package demo start\n");

    /* 启动分拣线程 */
    rt_thread_startup(sorter_tid);

    /* 启动发送线程 */
    rt_thread_startup(sender1_tid);
    rt_thread_startup(sender2_tid);
}

MSH_CMD_EXPORT(package_demo, package message queue demo);
