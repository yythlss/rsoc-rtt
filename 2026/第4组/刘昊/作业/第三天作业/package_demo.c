#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>

#define PACKAGE_PER_SENDER  5
#define PACKAGE_TOTAL       10
#define THREAD_STACK        1024
#define THREAD_PRIORITY     18

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

struct sender_info
{
    rt_uint8_t id;
};

static rt_mq_t package_mq;
static rt_sem_t package_done_sem;
static struct sender_info senders[2] = {{1}, {2}};
static rt_uint32_t send_failed;
static rt_bool_t package_running;

static const char *region_name(rt_uint8_t region)
{
    static const char *const names[] = {"invalid", "east", "south", "north"};
    return (region <= 3) ? names[region] : names[0];
}

static void sender_entry(void *parameter)
{
    struct sender_info *sender = (struct sender_info *)parameter;
    rt_uint8_t i;

    for (i = 1; i <= PACKAGE_PER_SENDER; i++)
    {
        struct package_msg msg;

        msg.id = sender->id * 1000 + i;
        msg.region = ((sender->id + i - 2) % 3) + 1;
        msg.weight = 700 + sender->id * 100 + i * 50;

        while (rt_mq_send(package_mq, &msg, sizeof(msg)) != RT_EOK)
        {
            rt_base_t level = rt_hw_interrupt_disable();
            send_failed++;
            rt_hw_interrupt_enable(level);
            rt_kprintf("queue full, package %u retry\n", msg.id);
            rt_thread_mdelay(100);
        }
        rt_kprintf("sender%u send package %u\n", sender->id, msg.id);
    }

    rt_sem_release(package_done_sem);
}

static void sorter_entry(void *parameter)
{
    rt_uint16_t region_count[3] = {0};
    rt_uint16_t total_count = 0;
    rt_uint32_t total_weight = 0;

    RT_UNUSED(parameter);
    rt_thread_mdelay(500); /* 先让队列装满，演示发送失败和重试。 */

    while (1)
    {
        struct package_msg msg;

        rt_mq_recv(package_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (msg.id == 0)
            break;

        region_count[msg.region - 1]++;
        total_count++;
        total_weight += msg.weight;
        rt_kprintf("package %u -> %s, weight: %u g\n",
                   msg.id, region_name(msg.region), msg.weight);
        rt_thread_mdelay(180);
    }

    rt_kprintf("total packages: %u\n", total_count);
    rt_kprintf("east: %u, south: %u, north: %u\n",
               region_count[0], region_count[1], region_count[2]);
    rt_kprintf("total weight: %u g\n", total_weight);
    rt_kprintf("send failed: %u, result: %s\n", send_failed,
               (total_count == PACKAGE_TOTAL && total_weight == 10000) ?
               "PASS" : "FAIL");
    rt_sem_release(package_done_sem);
}

static void package_task(void *parameter)
{
    rt_thread_t tid[3] = {RT_NULL};
    struct package_msg end_msg = {0};
    rt_uint8_t i;

    RT_UNUSED(parameter);
    send_failed = 0;

    package_mq = rt_mq_create("pkgque", sizeof(struct package_msg), 5,
                              RT_IPC_FLAG_FIFO);
    package_done_sem = rt_sem_create("pkgdone", 0, RT_IPC_FLAG_FIFO);
    if ((package_mq == RT_NULL) || (package_done_sem == RT_NULL))
    {
        rt_kprintf("create package IPC failed\n");
        goto cleanup;
    }

    tid[0] = rt_thread_create("pkgsnd1", sender_entry, &senders[0],
                              THREAD_STACK, THREAD_PRIORITY, 5);
    tid[1] = rt_thread_create("pkgsnd2", sender_entry, &senders[1],
                              THREAD_STACK, THREAD_PRIORITY, 5);
    tid[2] = rt_thread_create("pkgsort", sorter_entry, RT_NULL,
                              THREAD_STACK, THREAD_PRIORITY, 5);
    if ((tid[0] == RT_NULL) || (tid[1] == RT_NULL) || (tid[2] == RT_NULL))
    {
        rt_kprintf("create package thread failed\n");
        for (i = 0; i < 3; i++)
            if (tid[i] != RT_NULL)
                rt_thread_delete(tid[i]);
        goto cleanup;
    }

    rt_kprintf("package demo start\n");
    for (i = 0; i < 3; i++)
        rt_thread_startup(tid[i]);

    /* 两个发送线程结束后，再通知分拣线程退出。 */
    rt_sem_take(package_done_sem, RT_WAITING_FOREVER);
    rt_sem_take(package_done_sem, RT_WAITING_FOREVER);
    while (rt_mq_send(package_mq, &end_msg, sizeof(end_msg)) != RT_EOK)
        rt_thread_mdelay(50);
    rt_sem_take(package_done_sem, RT_WAITING_FOREVER);

cleanup:
    if (package_done_sem != RT_NULL)
    {
        rt_sem_delete(package_done_sem);
        package_done_sem = RT_NULL;
    }
    if (package_mq != RT_NULL)
    {
        rt_mq_delete(package_mq);
        package_mq = RT_NULL;
    }
    package_running = RT_FALSE;
}

int package_demo(void)
{
    rt_thread_t tid;

    if (package_running)
    {
        rt_kprintf("package demo is running\n");
        return -RT_EBUSY;
    }

    package_running = RT_TRUE;
    tid = rt_thread_create("pqcoord", package_task, RT_NULL,
                           1280, THREAD_PRIORITY - 1, 5);
    if (tid == RT_NULL)
    {
        package_running = RT_FALSE;
        return -RT_ENOMEM;
    }
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, message queue package demo);
