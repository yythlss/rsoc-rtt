
# RT‑Thread 线程间同步与通信实验报告
## 练习一：停车场车位管理
### 设计说明
1. 使用**计数信号量**，初始值3，代表3个车位。
2. 创建5个车辆线程，每个线程`rt_sem_take`申请车位，2秒超时；超时打印等待，稍后重试。
3. 获取成功打印进入；延时模拟停车，之后释放信号量离开。
4. 信号量最大计数3，保证同时在场车辆不超过3。
5. 全部线程结束后删除线程与信号量，防止重复运行异常。

### 代码

```c
#define PARK_CAR_NUM        5
#define PARK_SPACE_NUM      3
#define PARK_WAIT_TICK      RT_TICK_PER_SECOND * 2   /* 2秒超时 */

static rt_sem_t g_park_sem = RT_NULL;
static rt_thread_t g_car_thread[PARK_CAR_NUM] = {RT_NULL};

static void car_thread_entry(void *parameter)
{
    rt_uint32_t car_id = (rt_uint32_t)parameter;
    rt_err_t ret;

    while(1)
    {
        ret = rt_sem_take(g_park_sem, PARK_WAIT_TICK);
        if(ret == RT_EOK)
        {
            rt_uint32_t used = PARK_SPACE_NUM - g_park_sem->value;
            rt_kprintf("car%d entered, used spaces: %d\n", car_id, used);

            rt_thread_delay(5000);

            rt_kprintf("car%d left\n", car_id);
            rt_sem_release(g_park_sem);
            break;
        }
        else
        {
            rt_kprintf("car%d waiting for a space\n", car_id);
        }
    }
    rt_thread_self()->user_data = (void *)0;
}

static int parking_demo(int argc, char** argv)
{
    int i;
    rt_err_t err;

    /* 创建计数信号量，初始3个车位 */
    g_park_sem = rt_sem_create("park_sem", PARK_SPACE_NUM, RT_IPC_FLAG_FIFO);
    if(g_park_sem == RT_NULL)
    {
        rt_kprintf("create park sem failed!\n");
        return -RT_ENOMEM;
    }

    for(i = 0; i < PARK_CAR_NUM; i++)
    {
        g_car_thread[i] = rt_thread_create("car", car_thread_entry, (void*)(i+1),
                                          1024, 10, 20);
        if(g_car_thread[i] == RT_NULL)
        {
            rt_kprintf("create car%d thread failed\n", i+1);
            goto parking_clean;
        }
        rt_thread_startup(g_car_thread[i]);
    }

    return RT_EOK;

parking_clean:
    for(i = 0; i < PARK_CAR_NUM; i++)
    {
        if(g_car_thread[i] != RT_NULL)
        {
            rt_thread_delete(g_car_thread[i]);
            g_car_thread[i] = RT_NULL;
        }
    }
    rt_sem_delete(g_park_sem);
    g_park_sem = RT_NULL;
    return -RT_ERROR;
}
MSH_CMD_EXPORT(parking_demo, parking lot management demo);

```

### 串口输出

```plain
msh >parking_demo
car1 entered, used spaces: 1
car2 entered, used spaces: 2
car3 entered, used spaces: 3
car4 waiting for a space
car5 waiting for a space
car4 waiting for a space
car5 waiting for a space
car1 left
car4 entered, used spaces: 3
car2 left
car5 entered, used spaces: 3
car3 left
car4 left
car5 left
```

### 思考题：为什么使用计数信号量而不是互斥量？
> 互斥量用于**单一资源独占**，计数只能0/1，用于保护临界区，还具备所有权、优先级继承。
> 停车场是**多实例有限资源（3个相同车位）**，允许多个线程同时获取资源，最大同时3个。
> 计数信号量支持大于1的初值，可以允许多个线程同时获取资源，适合这类资源池场景。互斥量只能一个线程持有，无法实现3个车位同时占用。

## 练习二：多窗口售票系统
### 设计说明
1. 全局变量`g_ticket_count=20`，两个售票线程A、B。
2. **互斥量保护完整售票流程：判余票、延时模拟业务、票数自减、输出票号**，整个作为临界区。
3. 票数为0时线程退出。
4. 对比实验：注释掉互斥take/release，在判断票数和自减之间加入延时，会出现：票号重复、剩余票数小于0、卖出总票数大于20。开启互斥后现象消失。

### 代码

```c
#define TICKET_TOTAL        20
static rt_mutex_t g_ticket_mtx = RT_NULL;
static rt_int32_t g_ticket_count = TICKET_TOTAL;
static rt_thread_t g_winA_thread = RT_NULL;
static rt_thread_t g_winB_thread = RT_NULL;

static void window_thread_entry(void *parameter)
{
    char *win_name = (char *)parameter;

    while(1)
    {
        rt_err_t ret = rt_mutex_take(g_ticket_mtx, RT_WAITING_FOREVER);
        if(ret != RT_EOK) break;

        if(g_ticket_count <= 0)
        {
            rt_mutex_release(g_ticket_mtx);
            break;
        }

        /* 模拟业务耗时 */
        rt_thread_delay(2);

        rt_int32_t curr_ticket = g_ticket_count;
        g_ticket_count --;
        rt_kprintf("%s sold ticket %d\n", win_name, curr_ticket);

        rt_mutex_release(g_ticket_mtx);

        rt_thread_delay(1);
    }

    rt_thread_self()->user_data = (void *)0;
}

static int ticket_demo(int argc, char** argv)
{
    if(g_ticket_mtx != RT_NULL)
    {
        rt_kprintf("ticket demo already running!\n");
        return -RT_EBUSY;
    }

    g_ticket_count = TICKET_TOTAL;

    g_ticket_mtx = rt_mutex_create("ticket_mtx", RT_IPC_FLAG_PRIO);
    if(g_ticket_mtx == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed\n");
        return -RT_ENOMEM;
    }

    g_winA_thread = rt_thread_create("winA", window_thread_entry, "window A",
                                     1024, 10, 20);
    g_winB_thread = rt_thread_create("winB", window_thread_entry, "window B",
                                     1024, 10, 20);

    if(g_winA_thread == RT_NULL || g_winB_thread == RT_NULL)
    {
        rt_kprintf("create window thread failed\n");
        rt_mutex_delete(g_ticket_mtx);
        g_ticket_mtx = RT_NULL;
        return -RT_ENOMEM;
    }

    rt_thread_startup(g_winA_thread);
    rt_thread_startup(g_winB_thread);

    /* 等待两个售票线程执行完毕 */
    while(g_winA_thread->stat != RT_THREAD_CLOSE ||
            g_winB_thread->stat != RT_THREAD_CLOSE)
    {
        rt_thread_delay(RT_TICK_PER_SECOND / 10);
    }

    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", g_ticket_count);

    rt_mutex_delete(g_ticket_mtx);
    g_ticket_mtx = RT_NULL;

    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, multi window ticket sale demo);

```

### 串口输出

```plain
msh >ticket_demo
window A sold ticket 20
window B sold ticket 19
window A sold ticket 18
window B sold ticket 17
window A sold ticket 16
window B sold ticket 15
window A sold ticket 14
window B sold ticket 13
window A sold ticket 12
window B sold ticket 11
window A sold ticket 10
window B sold ticket 9
window A sold ticket 8
window B sold ticket 7
window A sold ticket 6
window B sold ticket 5
window A sold ticket 4
window B sold ticket 3
window A sold ticket 2
window B sold ticket 1
all tickets sold
remaining tickets: 0
```

### 思考题：为什么只给 ticket_count-- 这一行加锁仍然不安全？

> 售票逻辑是：读取票数 → 判断是否>0 → 票数减1。
> 如果只对`ticket_count--`加锁，**读取票数、判断票数的代码没有保护**。
> 线程A读取票数=1，还没执行自减；线程B也读到票数=1；两个线程都判断有票，都执行减1。最终会卖出2张票，票数变成‑1，出现超卖。
> 临界区必须把**读取‑判断‑修改这一整套业务逻辑全部包裹**，而不是只保护赋值语句。

## 练习三：系统启动条件检查
### 设计说明
1. 事件集三个bit位分别代表网络、传感器、存储模块就绪。
2. 三个初始化线程，不同延时，发送对应事件位。存储模块延时5s。
3. 主业务线程使用`RT_EVENT_FLAG_AND`等待**全部3个事件同时置位**，最大等待3s。
4. 超时打印哪些模块未就绪，继续循环等待；全部就绪打印业务启动。

### 代码

```c
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY    (1U << 1)
#define EVENT_STORAGE_READY   (1U << 2)
#define ALL_MODULE_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t g_sys_event = RT_NULL;
static rt_thread_t g_th_net = RT_NULL;
static rt_thread_t g_th_sensor = RT_NULL;
static rt_thread_t g_th_storage = RT_NULL;
static rt_thread_t g_th_business = RT_NULL;

static void net_init_thread(void *param)
{
    RT_UNUSED(param);
    rt_thread_delay(RT_TICK_PER_SECOND * 1);
    rt_event_send(g_sys_event, EVENT_NET_READY);
    rt_kprintf("network ready\n");
}

static void sensor_init_thread(void *param)
{
    RT_UNUSED(param);
    rt_thread_delay(RT_TICK_PER_SECOND * 2);
    rt_event_send(g_sys_event, EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
}

static void storage_init_thread(void *param)
{
    RT_UNUSED(param);
    rt_thread_delay(RT_TICK_PER_SECOND * 5);
    rt_event_send(g_sys_event, EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
}

static void business_thread(void *param)
{
    RT_UNUSED(param);
    rt_uint32_t recv;
    rt_err_t ret;

    while(1)
    {
        ret = rt_event_recv(g_sys_event, ALL_MODULE_READY,
                            RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                            RT_TICK_PER_SECOND * 3, &recv);
        if(ret == RT_EOK)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
            break;
        }
        else
        {
            rt_kprintf("wait modules timeout, ");
            if( !(recv & EVENT_NET_READY) ) rt_kprintf("network ");
            if( !(recv & EVENT_SENSOR_READY) ) rt_kprintf("sensor ");
            if( !(recv & EVENT_STORAGE_READY) ) rt_kprintf("storage ");
            rt_kprintf("is not ready\n");
        }
    }
}

static int startup_demo(int argc, char** argv)
{
    if(g_sys_event != RT_NULL)
    {
        rt_kprintf("startup demo already running!\n");
        return -RT_EBUSY;
    }

    g_sys_event = rt_event_create("sys_event", RT_IPC_FLAG_FIFO);
    if(g_sys_event == RT_NULL)
    {
        rt_kprintf("create sys event failed\n");
        return -RT_ENOMEM;
    }

    g_th_net = rt_thread_create("net_init", net_init_thread, RT_NULL, 1024, 10, 20);
    g_th_sensor = rt_thread_create("sen_init", sensor_init_thread, RT_NULL, 1024, 10, 20);
    g_th_storage = rt_thread_create("sto_init", storage_init_thread, RT_NULL, 1024, 10, 20);
    g_th_business = rt_thread_create("business", business_thread, RT_NULL, 1024, 10, 20);

    if(!g_th_net || !g_th_sensor || !g_th_storage || !g_th_business)
    {
        rt_kprintf("create startup thread fail\n");
        rt_event_delete(g_sys_event);
        g_sys_event = RT_NULL;
        return -RT_ENOMEM;
    }

    rt_thread_startup(g_th_net);
    rt_thread_startup(g_th_sensor);
    rt_thread_startup(g_th_storage);
    rt_thread_startup(g_th_business);

    /* 等待业务线程结束 */
    while(g_th_business->stat != RT_THREAD_CLOSE)
    {
        rt_thread_delay(RT_TICK_PER_SECOND/10);
    }

    rt_event_delete(g_sys_event);
    g_sys_event = RT_NULL;

    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, system startup event check demo);
```

### 串口输出

```plain
msh >startup_demo
network ready
sensor ready
wait modules timeout, network sensor storage is not ready
storage ready
all modules are ready
business task started
```

### 思考题：用三个独立信号量实现对比事件集，有什么区别？

1. 事件集：**一个对象管理多个条件位**，支持AND（全部就绪）/OR（任意就绪）；可以一次性等待多个条件；事件位可以被清除；不需要每个条件维护一个IPC对象。
2. 三个独立信号量：需要依次take三个信号量，才能代表全部就绪；无法原子等待“三者同时就绪”；take顺序会带来逻辑麻烦；每个条件占用一个信号量对象；无法方便获知哪一个条件未就绪。
3. 事件集非常适合多模块初始化、多条件就绪这种场景；信号量适合资源计数。

## 练习四：快递分拣中心
### 设计说明
1. 消息队列容量5，消息结构体包含id、region、weight。
2. 2个发送线程，每个生成5条快递，id不重复；队列满时发送失败计数累加，重试。
3. 分拣线程阻塞接收消息，解析区域打印信息；统计总件数、各区域数量、总重量。
4. 两个发送线程全部结束，发送2条结束标记消息；分拣线程收到两个结束标记，输出统计并退出。
5. 全部完成释放队列与线程资源。

### 代码

```c
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;      /* 1华东 2华南 3华北 */
    rt_uint16_t weight;
};

#define PACKAGE_QUEUE_LEN   5
#define PER_SENDER_CNT      5
#define END_MSG_ID          0xFFFFFFFFU

static rt_mq_t g_package_mq = RT_NULL;
static rt_thread_t g_sender1 = RT_NULL;
static rt_thread_t g_sender2 = RT_NULL;
static rt_thread_t g_sorter = RT_NULL;

static rt_uint32_t g_send_fail_cnt = 0;

static void sender_thread_entry(void *param)
{
    rt_uint32_t base_id = (rt_uint32_t)param;
    struct package_msg msg;
    int i;

    for(i = 0; i < PER_SENDER_CNT; i++)
    {
        msg.id = base_id + i;
        msg.region = 1 + rand() % 3;
        msg.weight = 200 + rand() % 1500;

        rt_err_t ret = rt_mq_send(g_package_mq, &msg, sizeof(struct package_msg));
        if(ret != RT_EOK)
        {
            g_send_fail_cnt ++;
            rt_thread_delay(10);
            i --;   /* 发送失败重试 */
            continue;
        }
        rt_thread_delay(10);
    }
}

static void sorter_thread_entry(void *param)
{
    RT_UNUSED(param);
    struct package_msg msg;
    rt_err_t ret;

    rt_uint32_t total = 0;
    rt_uint32_t east_cnt = 0, south_cnt = 0, north_cnt = 0;
    rt_uint32_t total_weight = 0;
    int end_flag = 0;
    int end_recv = 0;

    while(1)
    {
        ret = rt_mq_recv(g_package_mq, &msg, sizeof(struct package_msg), RT_WAITING_FOREVER);
        if(ret != RT_EOK) continue;

        if(msg.id == END_MSG_ID)
        {
            end_recv ++;
            if(end_recv >= 2) break;
            continue;
        }

        total ++;
        total_weight += msg.weight;
        const char *area;
        switch(msg.region)
        {
            case 1: east_cnt++; area = "east"; break;
            case 2: south_cnt++; area = "south"; break;
            case 3: north_cnt++; area = "north"; break;
            default: area = "unknown"; break;
        }
        rt_kprintf("package %d -> %s, weight: %d g\n", msg.id, area, msg.weight);
    }

    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", east_cnt, south_cnt, north_cnt);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", g_send_fail_cnt);
}

static int package_demo(int argc, char** argv)
{
    if(g_package_mq != RT_NULL)
    {
        rt_kprintf("package demo already running!\n");
        return -RT_EBUSY;
    }
    g_send_fail_cnt = 0;

    g_package_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), PACKAGE_QUEUE_LEN, RT_IPC_FLAG_FIFO);
    if(g_package_mq == RT_NULL)
    {
        rt_kprintf("create package mq failed\n");
        return -RT_ENOMEM;
    }

    g_sender1 = rt_thread_create("send1", sender_thread_entry, (void*)1001, 1024, 10, 20);
    g_sender2 = rt_thread_create("send2", sender_thread_entry, (void*)2001, 1024, 10, 20);
    g_sorter = rt_thread_create("sorter", sorter_thread_entry, RT_NULL, 2048, 10, 20);

    if(!g_sender1 || !g_sender2 || !g_sorter)
    {
        rt_kprintf("create package thread failed\n");
        rt_mq_delete(g_package_mq);
        g_package_mq = RT_NULL;
        return -RT_ENOMEM;
    }

    rt_thread_startup(g_sender1);
    rt_thread_startup(g_sender2);
    rt_thread_startup(g_sorter);

    /* 等待两个发送线程执行完毕 */
    while(g_sender1->stat != RT_THREAD_CLOSE ||
            g_sender2->stat != RT_THREAD_CLOSE)
    {
        rt_thread_delay(RT_TICK_PER_SECOND / 10);
    }

    /* 两个发送线程完成，发送结束标记消息 */
    struct package_msg end_msg = {END_MSG_ID,0,0};
    rt_mq_send(g_package_mq, &end_msg, sizeof(end_msg));
    rt_mq_send(g_package_mq, &end_msg, sizeof(end_msg));

    /* 等待分拣线程结束 */
    while(g_sorter->stat != RT_THREAD_CLOSE)
    {
        rt_thread_delay(RT_TICK_PER_SECOND / 10);
    }

    rt_mq_delete(g_package_mq);
    g_package_mq = RT_NULL;
    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, express package sort demo);
```

### 串口输出

```plain
msh >package_demo
package 1001 -> east, weight: 960 g
package 2001 -> north, weight: 820 g
package 1002 -> south, weight: 1132 g
package 2002 -> south, weight: 313 g
package 1003 -> south, weight: 709 g
package 2003 -> north, weight: 605 g
package 1004 -> east, weight: 1249 g
package 2004 -> east, weight: 1419 g
package 1005 -> north, weight: 1024 g
package 2005 -> east, weight: 1349 g
total packages: 10
east: 4, south: 3, north: 3
total weight: 9580 g
send failed: 0
```
