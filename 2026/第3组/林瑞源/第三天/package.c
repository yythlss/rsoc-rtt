/*
 * 练习四：快递分拣中心（消息队列）
 * 场景：两个收件线程产生快递，分拣线程按区域（华东/华南/华北）分类统计。
 * 要求：队列容量 5；队列满时记录发送失败次数（非阻塞发送 + 重试）；
 *      两发送线程完成后各发一条结束消息，分拣线程统计后退出。
 * 命令：package_demo
 */
#include <rtthread.h>
#include <string.h>

#define MQ_CAP           5
#define PKG_PER_SENDER   5
#define REGION_EAST      1
#define REGION_SOUTH     2
#define REGION_NORTH     3
#define PKG_END_ID       0   /* 结束消息标记 */

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;
    rt_uint16_t weight;
};

static rt_mq_t      pkg_mq = RT_NULL;
static rt_sem_t    done_sem = RT_NULL;
static rt_thread_t  send_t[2];
static rt_thread_t  sort_t = RT_NULL;
static rt_thread_t  pkg_mgr = RT_NULL;
static volatile rt_bool_t pkg_run = RT_FALSE;

static const char *region_name(rt_uint8_t r)
{
    if (r == REGION_EAST)  return "east";
    if (r == REGION_SOUTH) return "south";
    return "north";
}

static void sender_entry(void *p)
{
    rt_uint32_t base = (rt_uint32_t)p;          /* 1000 或 2000 */
    rt_uint32_t failed = 0;
    struct package_msg msg;

    for (int i = 1; i <= PKG_PER_SENDER; i++)
    {
        msg.id     = base + i;
        msg.region = (i % 3) + 1;               /* 1/2/3 轮换：华东/华南/华北 */
        msg.weight = 800 + (base / 100 + i) * 37;

        if (rt_mq_send_wait(pkg_mq, &msg, sizeof(msg), 0) != RT_EOK)   /* 非阻塞发送 */
        {
            failed++;                          /* 队列满：记录失败并重试本条 */
            rt_thread_mdelay(50);
            i--;
            continue;
        }
        rt_thread_mdelay(30);
    }

    /* 发送结束消息 */
    msg.id = PKG_END_ID; msg.region = 0; msg.weight = 0;
    rt_mq_send_wait(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
    rt_kprintf("sender %d finished, send failed: %d\n", base / 1000, failed);
    rt_sem_release(done_sem);
}

static void sorter_entry(void *p)
{
    struct package_msg msg;
    rt_uint32_t total = 0, east = 0, south = 0, north = 0, total_w = 0, ends = 0;

    while (ends < 2)
    {
        if (rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) == RT_EOK)
        {
            if (msg.id == PKG_END_ID) { ends++; continue; }   /* 结束消息，不计入统计 */
            total++;
            if (msg.region == REGION_EAST) east++;
            else if (msg.region == REGION_SOUTH) south++;
            else north++;
            total_w += msg.weight;
            rt_kprintf("package %d -> %s, weight: %d g\n", msg.id, region_name(msg.region), msg.weight);
        }
    }
    rt_kprintf("total packages: %d\neast: %d, south: %d, north: %d\ntotal weight: %d g\n",
               total, east, south, north, total_w);
    rt_sem_release(done_sem);
}

static void pkg_mgr_entry(void *p)
{
    for (int i = 0; i < 2; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    rt_sem_take(done_sem, RT_WAITING_FOREVER);   /* sorter */
    for (int i = 0; i < 2; i++)
    {
        if (send_t[i]) { rt_thread_delete(send_t[i]); send_t[i] = RT_NULL; }
    }
    if (sort_t) { rt_thread_delete(sort_t); sort_t = RT_NULL; }
    if (pkg_mq) { rt_mq_delete(pkg_mq); pkg_mq = RT_NULL; }
    if (done_sem) { rt_sem_delete(done_sem); done_sem = RT_NULL; }
    pkg_run = RT_FALSE;
    rt_kprintf("package demo finished\n");
    rt_thread_delete(rt_thread_self());
}

static int package_demo(int argc, char **argv)
{
    if (pkg_run) { rt_kprintf("package_demo is already running\n"); return -RT_EBUSY; }
    pkg_run = RT_TRUE;

    pkg_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), MQ_CAP, RT_IPC_FLAG_FIFO);
    done_sem = rt_sem_create("pkg_done", 0, RT_IPC_FLAG_FIFO);
    if (!pkg_mq || !done_sem) { rt_kprintf("create failed\n"); pkg_run = RT_FALSE; return -RT_ENOMEM; }

    memset(send_t, 0, sizeof(send_t));
    sort_t = RT_NULL;
    send_t[0] = rt_thread_create("sender1", sender_entry, (void *)1000, 1024, 20, 10);
    send_t[1] = rt_thread_create("sender2", sender_entry, (void *)2000, 1024, 20, 10);
    sort_t    = rt_thread_create("sorter",  sorter_entry, RT_NULL, 1536, 15, 10);

    if (send_t[0]) rt_thread_startup(send_t[0]); else rt_sem_release(done_sem);
    if (send_t[1]) rt_thread_startup(send_t[1]); else rt_sem_release(done_sem);
    if (sort_t)    rt_thread_startup(sort_t);    else rt_sem_release(done_sem);

    pkg_mgr = rt_thread_create("pkg_mgr", pkg_mgr_entry, RT_NULL, 1024, 15, 10);
    if (pkg_mgr) rt_thread_startup(pkg_mgr);
    return 0;
}
MSH_CMD_EXPORT(package_demo, express sorting: message queue demo);
