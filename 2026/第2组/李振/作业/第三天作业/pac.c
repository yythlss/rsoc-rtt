#include <rtthread.h>
#include <stdlib.h>

#define MSG_QUEUE_MAX        5
#define SEND_THREAD_CNT      2
#define PER_THREAD_PACK_CNT  5

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};
#define MSG_END_FLAG_REGION   0

static rt_mq_t package_mq = RT_NULL;

static rt_uint32_t global_pkg_id = 1000;
static rt_uint32_t send_fail_cnt = 0;
static struct rt_mutex id_mtx;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static char sender_stack[SEND_THREAD_CNT][1024];
static struct rt_thread sender_thread[SEND_THREAD_CNT];
static char sort_stack[1024];
static struct rt_thread sort_thread;

static void sender_entry(void *param)
{
    int i;
    struct package_msg msg;
    rt_err_t ret;

    for(i = 0; i < PER_THREAD_PACK_CNT; i++)
    {
        rt_mutex_take(&id_mtx, RT_WAITING_FOREVER);
        global_pkg_id++;
        msg.id = global_pkg_id;
        rt_mutex_release(&id_mtx);

        msg.region = (rt_uint8_t)(rand() % 3 + 1);
        msg.weight = 500 + (rand() % 1500);

        ret = rt_mq_send(package_mq, &msg, sizeof(struct package_msg));
        if(ret != RT_EOK)
        {
            send_fail_cnt++;
        }
        rt_thread_mdelay(200);
    }
}

static void sort_entry(void *param)
{
    struct package_msg msg;
    rt_err_t ret;
    rt_uint32_t total_pkg = 0;
    rt_uint32_t east_cnt = 0, south_cnt = 0, north_cnt = 0;
    rt_uint32_t total_weight = 0;
    char *region_name;

    while(1)
    {
        ret = rt_mq_recv(package_mq, &msg, sizeof(struct package_msg), RT_WAITING_FOREVER);
        if(ret != RT_EOK)
            continue;

        if(msg.region == MSG_END_FLAG_REGION)
        {
            break;
        }

        total_pkg++;
        total_weight += msg.weight;
        switch(msg.region)
        {
            case 1:
                region_name = "east";
                east_cnt++;
                break;
            case 2:
                region_name = "south";
                south_cnt++;
                break;
            case 3:
                region_name = "north";
                north_cnt++;
                break;
            default:
                region_name = "unknown";
                break;
        }
        rt_kprintf("package %d -> %s, weight: %d g\n", msg.id, region_name, msg.weight);
    }

    rt_kprintf("total packages: %d\n", total_pkg);
    rt_kprintf("east: %d, south: %d, north: %d\n", east_cnt, south_cnt, north_cnt);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", send_fail_cnt);
}

int express_sort_sample(void)
{
    rt_err_t res;
    int i;
    struct package_msg end_msg;

    rt_mutex_init(&id_mtx, "id_mtx", RT_IPC_FLAG_PRIO);

    package_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), MSG_QUEUE_MAX, RT_IPC_FLAG_PRIO);
    if(package_mq == RT_NULL)
    {
        rt_kprintf("create mq failed\n");
        return -1;
    }

    global_pkg_id = 1000;
    send_fail_cnt = 0;

    for(i = 0; i < SEND_THREAD_CNT; i++)
    {
        res = rt_thread_init(&sender_thread[i],
                              RT_NULL,
                              sender_entry,
                              RT_NULL,
                              &sender_stack[i][0],
                              sizeof(sender_stack[i]),
                              22,
                              5);
        if(res == RT_EOK)
        {
            rt_thread_startup(&sender_thread[i]);
        }
    }

    res = rt_thread_init(&sort_thread,
                          RT_NULL,
                          sort_entry,
                          RT_NULL,
                          sort_stack,
                          sizeof(sort_stack),
                          21,
                          5);
    if(res == RT_EOK)
    {
        rt_thread_startup(&sort_thread);
    }

    rt_thread_mdelay(3000);
    rt_memset(&end_msg, 0, sizeof(end_msg));
    end_msg.region = MSG_END_FLAG_REGION;
    rt_mq_send(package_mq, &end_msg, sizeof(struct package_msg));

    return 0;
}
MSH_CMD_EXPORT(express_sort_sample, express sort practice);
