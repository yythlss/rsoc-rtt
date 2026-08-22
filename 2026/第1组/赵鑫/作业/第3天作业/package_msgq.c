#include <rtthread.h>
#include <rthw.h>               /* rt_hw_interrupt_disable / enable */

/* 快递消息结构 */
struct package_msg
{
    rt_uint32_t id;         /* 快递编号 */
    rt_uint8_t  region;     /* 目的地区域：1=华东 2=华南 3=华北 */
    rt_uint16_t weight;     /* 重量（g） */
};

#define MQ_SIZE             5          /* 消息队列容量（条） */
#define SENDER_NUM          2          /* 发件线程数 */
#define MSG_PER_SENDER      5          /* 每个发件线程产生的快递数 */
#define SENDER_PRIO         15         /* 发件线程优先级 */
#define SORTER_PRIO         12         /* 分拣线程优先级（高于发件，优先收件） */
#define THREAD_TIMESLICE    10
#define THREAD_STACK_SIZE   1024

#define SEND_INTERVAL       50         /* 发件间隔（ms） */
#define SEND_RETRY_DELAY    10         /* 队列满重试间隔（ms） */
#define SORT_INTERVAL       20         /* 分拣每条耗时（ms），慢于发件以制造队列满 */
#define END_MSG_ID          0xFFFFFFFF /* 结束消息标记 */

static rt_mq_t      pkg_mq      = RT_NULL;   /* 消息队列 */
static rt_sem_t     done_sem    = RT_NULL;   /* 完成信号量：命令等待全部线程退出 */
static rt_thread_t  sender_thread[SENDER_NUM];  /* 发件线程句柄 */
static rt_thread_t  sorter_thread = RT_NULL;    /* 分拣线程句柄 */
static volatile rt_uint8_t demo_running = 0;    /* 任务运行标志，防止重复启动 */

/* 发送失败次数（两个发件线程并发递增，关中断保护） */
static volatile rt_uint32_t send_failed = 0;

static void send_failed_inc(void)
{
    rt_base_t level = rt_hw_interrupt_disable();
    send_failed++;
    rt_hw_interrupt_enable(level);
}

/* 发件线程入口：产生 MSG_PER_SENDER 条快递，再发送结束消息 */
static void sender_entry(void *param)
{
    int base_id = (int)param;       /* 1001 或 2001：编号段互不重叠 */
    int sender_no = (base_id == 2001) ? 2 : 1;
    int i;

    for (i = 0; i < MSG_PER_SENDER; i++)
    {
        struct package_msg msg;
        rt_err_t ret;

        msg.id     = base_id + i;                       /* 编号不重复 */
        msg.region = (rt_uint8_t)(i % 3 + 1);           /* 区域轮换：1,2,3,1,2 */
        msg.weight = (rt_uint16_t)(900 + (msg.id % 10) * 50);   /* 确定性重量 */

        /* 发送：队列满则计数并重试，直到发送成功（不崩溃） */
        ret = rt_mq_send(pkg_mq, &msg, sizeof(msg));
        while (ret != RT_EOK)
        {
            send_failed_inc();
            rt_kprintf("sender%d: queue full, retry (failed %d)\n",
                       sender_no, send_failed);
            rt_thread_mdelay(SEND_RETRY_DELAY);
            ret = rt_mq_send(pkg_mq, &msg, sizeof(msg));
        }

        rt_thread_mdelay(SEND_INTERVAL);
    }

    /* 发送结束消息 */
    {
        struct package_msg end;
        rt_err_t ret;

        end.id     = END_MSG_ID;
        end.region = 0;
        end.weight = 0;

        ret = rt_mq_send(pkg_mq, &end, sizeof(end));
        while (ret != RT_EOK)
        {
            send_failed_inc();
            rt_thread_mdelay(SEND_RETRY_DELAY);
            ret = rt_mq_send(pkg_mq, &end, sizeof(end));
        }
    }
    rt_kprintf("sender%d finished\n", sender_no);
    rt_sem_release(done_sem);       /* 通知命令：本线程已退出 */
}

/* 分拣线程入口：接收快递 -> 分类统计，收到两条结束消息后打印统计并退出 */
static void sorter_entry(void *param)
{
    static const char *region_name[4] = { "", "east", "south", "north" };
    struct package_msg msg;
    int total = 0, east = 0, south = 0, north = 0;
    rt_uint32_t total_weight = 0;
    int end_count = 0;

    while (1)
    {
        if (rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
        {
            break;                  /* 队列异常（正常流程不会走到） */
        }

        if (msg.id == END_MSG_ID)   /* 结束消息：两个发件线程各发一条 */
        {
            end_count++;
            if (end_count >= SENDER_NUM)
            {
                break;              /* 全部发件线程已完成，退出 */
            }
            continue;
        }

        /* 分类统计 */
        rt_kprintf("package %d -> %s, weight: %d g\n",
                   msg.id, region_name[msg.region], msg.weight);
        total++;
        total_weight += msg.weight;
        if (msg.region == 1)
        {
            east++;
        }
        else if (msg.region == 2)
        {
            south++;
        }
        else if (msg.region == 3)
        {
            north++;
        }

        /* 慢速分拣：制造队列堆积，让发件线程体验队列满 */
        rt_thread_mdelay(SORT_INTERVAL);
    }

    /* 打印统计结果 */
    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", east, south, north);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", send_failed);

    rt_sem_release(done_sem);       /* 通知命令：分拣线程已退出 */
}

static int package_demo(void)
{
    int i;

    if (demo_running)
    {
        rt_kprintf("package_demo is already running\n");
        return -RT_ERROR;
    }

    /* 1. 创建消息队列：容量 5 条 */
    pkg_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), MQ_SIZE, RT_IPC_FLAG_FIFO);
    if (pkg_mq == RT_NULL)
    {
        rt_kprintf("create message queue failed\n");
        return -RT_ERROR;
    }

    /* 2. 创建完成信号量 */
    done_sem = rt_sem_create("done", 0, RT_IPC_FLAG_FIFO);
    if (done_sem == RT_NULL)
    {
        rt_mq_delete(pkg_mq);
        pkg_mq = RT_NULL;
        rt_kprintf("create done semaphore failed\n");
        return -RT_ERROR;
    }

    /* 3. 创建两个发件线程（暂不启动） */
    for (i = 0; i < SENDER_NUM; i++)
    {
        char name[RT_NAME_MAX];

        rt_snprintf(name, sizeof(name), "sender%d", i + 1);
        sender_thread[i] = rt_thread_create(name, sender_entry,
                                            (void *)(1001 + i * 1000),
                                            THREAD_STACK_SIZE, SENDER_PRIO, THREAD_TIMESLICE);
        if (sender_thread[i] == RT_NULL)
        {
            int j;

            rt_kprintf("create thread %s failed\n", name);
            for (j = 0; j < i; j++)
            {
                rt_thread_delete(sender_thread[j]);
            }
            rt_sem_delete(done_sem);
            rt_mq_delete(pkg_mq);
            done_sem = RT_NULL;
            pkg_mq   = RT_NULL;
            return -RT_ERROR;
        }
    }

    /* 4. 创建分拣线程 */
    sorter_thread = rt_thread_create("sorter", sorter_entry, RT_NULL,
                                     THREAD_STACK_SIZE, SORTER_PRIO, THREAD_TIMESLICE);
    if (sorter_thread == RT_NULL)
    {
        rt_kprintf("create thread sorter failed\n");
        for (i = 0; i < SENDER_NUM; i++)
        {
            rt_thread_delete(sender_thread[i]);
        }
        rt_sem_delete(done_sem);
        rt_mq_delete(pkg_mq);
        done_sem = RT_NULL;
        pkg_mq   = RT_NULL;
        return -RT_ERROR;
    }

    /* 5. 全部创建成功，统一启动（发件线程先启动，分拣后启动） */
    send_failed = 0;
    demo_running = 1;
    for (i = 0; i < SENDER_NUM; i++)
    {
        rt_thread_startup(sender_thread[i]);
    }
    rt_thread_startup(sorter_thread);

    /* 6. 等待 3 个线程全部退出 */
    for (i = 0; i < SENDER_NUM + 1; i++)
    {
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    }

    /* 7. 统一清理：动态线程退出后由空闲线程自动回收，无需手动删除 */
    rt_sem_delete(done_sem);
    rt_mq_delete(pkg_mq);
    done_sem = RT_NULL;
    pkg_mq   = RT_NULL;
    demo_running = 0;

    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, express package sorting demo with message queue);
