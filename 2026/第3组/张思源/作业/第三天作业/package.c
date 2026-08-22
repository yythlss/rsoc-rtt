#include <rtthread.h>

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

#define REGION_EAST   1
#define REGION_SOUTH  2
#define REGION_NORTH  3
#define REGION_END    0xFF //结束消息标记
#define MQ_SIZE       5    //队列容量

static rt_mq_t package_mq = RT_NULL;

static void sender_entry(void *parameter){
    rt_uint32_t base = (rt_uint32_t)parameter;
    rt_uint8_t fail_count = 0;
    struct package_msg msg;

    for (int i = 1; i <= 5; i++)
    {
        msg.id = base + i;
        msg.region = (i % 3) + 1;
        msg.weight = 300 * i;

        rt_err_t ret = rt_mq_send(package_mq, &msg, sizeof(msg));
        if (ret == -RT_EFULL)
        {
            fail_count++;
        }
        rt_thread_mdelay(100);
    }
    //发送完毕
    msg.id = 0;
    msg.region = REGION_END;
    rt_kprintf("sender %d finished, send failed: %d\n", base / 1000, fail_count);
    rt_mq_send(package_mq, &msg, sizeof(msg));
}

static void sorter_entry(void *parameter){

    rt_uint32_t total = 0;        //总数量
    rt_uint32_t east = 0, south = 0, north = 0;  //各区域计数
    rt_uint32_t total_weight = 0; //总重量
    rt_uint8_t end_count = 0;     //收到结束消息计数
    struct package_msg msg;

    while(1){
        rt_mq_recv(package_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
        if (msg.region == REGION_END){
            end_count++;
            if (end_count == 2)  //两个发送线程各发一条
                break;
        }
        else {
            total++;
            total_weight += msg.weight;

            char *region_name;
            if (msg.region == REGION_EAST) { region_name = "east"; east++; }
            else if (msg.region == REGION_SOUTH) { region_name = "south"; south++; }
            else { region_name = "north"; north++; }
            rt_kprintf("package %d -> %s, weight: %d g\n", msg.id, region_name, msg.weight);
        }
    }
    //打印总的信息
    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", east, south, north);
    rt_kprintf("total weight: %d g\n", total_weight);

    //回收
    rt_mq_delete(package_mq);
    package_mq = RT_NULL;
}

static int package_demo(void){
    if (package_mq != RT_NULL)
    {
        rt_kprintf("package demo already running\n");
        return -1;
    }
    else {
        package_mq = rt_mq_create("package", sizeof(struct package_msg), MQ_SIZE, RT_IPC_FLAG_PRIO);
        if (package_mq == RT_NULL){
            rt_kprintf("package_mq create failed!\n");
            return -1;
        }
        else {
            //创建sender1线程
            rt_thread_t sender1_thread = rt_thread_create("sender1", sender_entry, (void *)1000, 1024, 20, 10);
            if(sender1_thread == RT_NULL){
                rt_kprintf("sender1 thread create failed\n");
                return -1;
            }
            else {
                rt_thread_startup(sender1_thread);
            }

            //创建sender2线程
            rt_thread_t sender2_thread = rt_thread_create("sender2", sender_entry, (void *)2000, 1024, 20, 10);
            if(sender2_thread == RT_NULL){
                rt_kprintf("sender2 thread create failed\n");
                return -1;
            }
            else {
                rt_thread_startup(sender2_thread);
            }

            //创建sorter线程
            rt_thread_t sorter_thread = rt_thread_create("sorter", sorter_entry, (void *)0, 1024, 20, 10);
            if(sorter_thread == RT_NULL){
                rt_kprintf("sorter thread create failed\n");
                return -1;
            }
            else {
                rt_thread_startup(sorter_thread);
            }
        }
    }

    return 0;
}
MSH_CMD_EXPORT(package_demo, a package demo);
