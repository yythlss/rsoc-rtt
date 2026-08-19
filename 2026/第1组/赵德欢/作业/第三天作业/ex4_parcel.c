/*
 * 练习四：快递分拣中心（消息队列）
 * 与 ex1/ex2/ex3 互斥，applications/ 下同一时刻只留一个。
 */
#include <rtthread.h>

#define MQ_CAPACITY      5
#define SENDER_NUM       2
#define PKG_PER_SENDER   5
#define REGION_END       0      /* 结束消息：region=0 表示某发件线程已发完 */

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;   /* 1 华东, 2 华南, 3 华北 */
    rt_uint16_t weight;
};

static rt_mq_t    pkg_mq = RT_NULL;
static rt_mutex_t sf_mutex = RT_NULL;       /* 保护 send_failed 计数 */
static rt_uint32_t send_failed = 0;

static const char *region_str(rt_uint8_t r)
{
    switch (r)
    {
    case 1: return "east";
    case 2: return "south";
    case 3: return "north";
    default: return "?";
    }
}

static void sender_entry(void *param)
{
    rt_uint32_t sender = (rt_uint32_t)param;
    rt_uint32_t base = (sender == 1) ? 1000 : 2000;
    struct package_msg pkg;
    rt_uint32_t i;

    for (i = 1; i <= PKG_PER_SENDER; i++)
    {
        pkg.id     = base + i;
        pkg.region = (rt_uint8_t)((i % 3) + 1);                 /* 简单分布 1/2/3 */
        pkg.weight = (rt_uint16_t)(800 + (sender * 100) + i * 37); /* 任意重量 */

        if (rt_mq_send(pkg_mq, &pkg, sizeof(pkg)) != RT_EOK)
        {
            /* 队列满，记一次失败，原地重试本条（i-- 保证同一条只成功发一次） */
            rt_mutex_take(sf_mutex, RT_WAITING_FOREVER);
            send_failed++;
            rt_mutex_release(sf_mutex);
            i--;
            rt_thread_mdelay(100);
        }
    }

    /* 本线程所有快递发完，再发一条结束消息 */
    pkg.id = 0; pkg.region = REGION_END; pkg.weight = 0;
    while (rt_mq_send(pkg_mq, &pkg, sizeof(pkg)) != RT_EOK)
        rt_thread_mdelay(100);
}

static void sorter_entry(void *param)
{
    struct package_msg pkg;
    rt_uint32_t total = 0, east = 0, south = 0, north = 0, weight = 0;
    rt_uint32_t end_count = 0;

    while (1)
    {
        if (rt_mq_recv(pkg_mq, &pkg, sizeof(pkg), RT_WAITING_FOREVER) != RT_EOK)
            continue;

        if (pkg.region == REGION_END)
        {
            end_count++;
            if (end_count >= SENDER_NUM) break;   /* 两个发件线程都结束 */
            continue;
        }

        total++;
        weight += pkg.weight;
        if (pkg.region == 1)      east++;
        else if (pkg.region == 2) south++;
        else if (pkg.region == 3) north++;

        rt_kprintf("package %u -> %s, weight: %u g\n", pkg.id, region_str(pkg.region), pkg.weight);
    }

    rt_kprintf("total packages: %u\n", total);
    rt_kprintf("east: %u, south: %u, north: %u\n", east, south, north);
    rt_kprintf("total weight: %u g\n", weight);
    rt_mutex_take(sf_mutex, RT_WAITING_FOREVER);
    rt_kprintf("send failed: %u\n", send_failed);
    rt_mutex_release(sf_mutex);
}

static int parcel_init(void)
{
    rt_thread_t t;

    pkg_mq = rt_mq_create("pkg", sizeof(struct package_msg), MQ_CAPACITY, RT_IPC_FLAG_FIFO);
    if (pkg_mq == RT_NULL) return -RT_ENOMEM;

    sf_mutex = rt_mutex_create("sf", RT_IPC_FLAG_FIFO);
    if (sf_mutex == RT_NULL) return -RT_ENOMEM;

    t = rt_thread_create("sender1", sender_entry, (void *)1, 1024, 15, 5); if (t) rt_thread_startup(t);
    t = rt_thread_create("sender2", sender_entry, (void *)2, 1024, 15, 5); if (t) rt_thread_startup(t);
    t = rt_thread_create("sorter",  sorter_entry, RT_NULL, 2048, 10, 5);   if (t) rt_thread_startup(t);
    return RT_EOK;
}
INIT_APP_EXPORT(parcel_init);
