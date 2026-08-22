/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     RT-Thread    first version
 */

#include <rtthread.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define THREAD_PRIORITY            16
#define THREAD_STACK_SIZE          1024
#define THREAD_TIMESLICE           5

/* ==================== 练习一：停车场车位管理（计数信号量） ==================== */
#define PARKING_SPACES             3
#define CAR_NUM                    5

static rt_sem_t parkings = RT_NULL;
static rt_sem_t parking_done = RT_NULL;
static rt_uint8_t parking_running = 0;

struct car_param
{
    int id;
    int enter_delay;
};

static void car_entry(void *parameter)
{
    struct car_param *car = (struct car_param *)parameter;

    rt_thread_mdelay(car->enter_delay);

    while (1)
    {
        if (rt_sem_take(parkings, rt_tick_from_millisecond(2000)) != RT_EOK)
        {
            rt_kprintf("car%d waiting for a space\n", car->id);
            continue;
        }

        rt_kprintf("car%d entered, used spaces: %d\n", car->id,
                   PARKING_SPACES - parkings->value);
        rt_thread_mdelay(4000);
        rt_kprintf("car%d left\n", car->id);
        rt_sem_release(parkings);
        break;
    }

    rt_sem_release(parking_done);
}

int parking_demo(void)
{
    int i, created = 0;
    struct car_param params[CAR_NUM] = {
        {1, 0}, {2, 500}, {3, 1000}, {4, 1200}, {5, 1300},
    };
    const char *car_names[CAR_NUM] = {"car1", "car2", "car3", "car4", "car5"};

    if (parking_running)
    {
        rt_kprintf("parking demo is running\n");
        return -RT_EBUSY;
    }

    parkings = rt_sem_create("p_sem", PARKING_SPACES, RT_IPC_FLAG_PRIO);
    if (parkings == RT_NULL)
    {
        rt_kprintf("create parking semaphore failed\n");
        return -RT_ENOMEM;
    }
    parking_done = rt_sem_create("p_done", 0, RT_IPC_FLAG_PRIO);
    if (parking_done == RT_NULL)
    {
        rt_kprintf("create parking done semaphore failed\n");
        rt_sem_delete(parkings);
        parkings = RT_NULL;
        return -RT_ENOMEM;
    }

    parking_running = 1;

    for (i = 0; i < CAR_NUM; i++)
    {
        rt_thread_t tid = rt_thread_create(car_names[i], car_entry, &params[i],
                                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
        if (tid == RT_NULL)
        {
            rt_kprintf("create %s failed\n", car_names[i]);
            continue;
        }
        rt_thread_startup(tid);
        created++;
    }

    /* 等待所有车辆进出完成后回收资源 */
    for (i = 0; i < created; i++)
        rt_sem_take(parking_done, RT_WAITING_FOREVER);

    rt_sem_delete(parking_done);
    rt_sem_delete(parkings);
    parkings = RT_NULL;
    parking_done = RT_NULL;
    parking_running = 0;

    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, parking demo with counting semaphore);

/* ==================== 练习二：多窗口售票系统（互斥量） ==================== */
#define TICKET_TOTAL               20

static rt_mutex_t ticket_mutex = RT_NULL;
static rt_sem_t ticket_done = RT_NULL;
static rt_uint8_t ticket_running = 0;
static int ticket_count = TICKET_TOTAL;

static void window_entry(void *parameter)
{
    int win = (int)parameter;

    while (1)
    {
        rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        if (ticket_count <= 0)
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        ticket_count--;
        rt_kprintf("window %c sold ticket %d\n", win, ticket_count + 1);
        if (ticket_count == 0)
        {
            rt_kprintf("all tickets sold\n");
            rt_kprintf("remaining tickets: 0\n");
        }

        rt_mutex_release(ticket_mutex);
    }

    rt_sem_release(ticket_done);
}

int ticket_demo(void)
{
    int created = 0;
    rt_thread_t tid;

    if (ticket_running)
    {
        rt_kprintf("ticket demo is running\n");
        return -RT_EBUSY;
    }

    ticket_mutex = rt_mutex_create("t_mutex", RT_IPC_FLAG_PRIO);
    if (ticket_mutex == RT_NULL)
    {
        rt_kprintf("create ticket mutex failed\n");
        return -RT_ENOMEM;
    }
    ticket_done = rt_sem_create("t_done", 0, RT_IPC_FLAG_PRIO);
    if (ticket_done == RT_NULL)
    {
        rt_kprintf("create ticket done semaphore failed\n");
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
        return -RT_ENOMEM;
    }

    ticket_count = TICKET_TOTAL;
    ticket_running = 1;

    tid = rt_thread_create("winA", window_entry, (void *)'A',
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        created++;
    }

    tid = rt_thread_create("winB", window_entry, (void *)'B',
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        created++;
    }

    /* 等待窗口线程售完退出 */
    while (created--)
        rt_sem_take(ticket_done, RT_WAITING_FOREVER);

    rt_sem_delete(ticket_done);
    rt_mutex_delete(ticket_mutex);
    ticket_mutex = RT_NULL;
    ticket_done = RT_NULL;
    ticket_running = 0;

    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, ticket demo with mutex);

/* 对比实验：不加互斥量，检查与修改之间加入延时，制造竞争 */
static int ticket_count_race = TICKET_TOTAL;

static void window_race_entry(void *parameter)
{
    int win = (int)parameter;

    while (1)
    {
        if (ticket_count_race <= 0)   /* 不加锁的“检查” */
            break;

        rt_thread_mdelay(5);          /* 扩大竞争窗口 */

        ticket_count_race--;          /* 不加锁的“修改” */
        rt_kprintf("window %c sold ticket %d (race)\n", win, ticket_count_race + 1);
    }
}

int ticket_demo_race(void)
{
    rt_thread_t tid;

    ticket_count_race = TICKET_TOTAL;

    tid = rt_thread_create("raceA", window_race_entry, (void *)'A',
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) rt_thread_startup(tid);

    tid = rt_thread_create("raceB", window_race_entry, (void *)'B',
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) rt_thread_startup(tid);

    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo_race, ticket demo WITHOUT mutex (race condition));

/* ==================== 练习三：系统启动条件检查（事件集） ==================== */
#define EVENT_NET_READY            (1U << 0)
#define EVENT_SENSOR_READY         (1U << 1)
#define EVENT_STORAGE_READY        (1U << 2)
#define EVENT_ALL_READY            (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t startup_events = RT_NULL;
static rt_sem_t startup_done = RT_NULL;
static rt_uint8_t startup_running = 0;

static void net_init_entry(void *parameter)
{
    rt_thread_mdelay(1000);
    rt_event_send(startup_events, EVENT_NET_READY);
    rt_kprintf("network ready\n");
    rt_sem_release(startup_done);
}

static void sensor_init_entry(void *parameter)
{
    rt_thread_mdelay(2000);
    rt_event_send(startup_events, EVENT_SENSOR_READY);
    rt_kprintf("sensor ready\n");
    rt_sem_release(startup_done);
}

static void storage_init_entry(void *parameter)
{
    rt_thread_mdelay(5000);
    rt_event_send(startup_events, EVENT_STORAGE_READY);
    rt_kprintf("storage ready\n");
    rt_sem_release(startup_done);
}

static void business_entry(void *parameter)
{
    rt_uint32_t recved = 0;

    /* 第一次：AND 模式等待三个模块，最多等 3 秒 */
    if (rt_event_recv(startup_events, EVENT_ALL_READY, RT_EVENT_FLAG_AND,
                      rt_tick_from_millisecond(3000), &recved) != RT_EOK)
    {
        rt_kprintf("wait modules timeout");
        /* 超时返回时 recved 不会被回填，逐个轮询判断谁还没就绪 */
        if (rt_event_recv(startup_events, EVENT_NET_READY, RT_EVENT_FLAG_AND, 0, &recved) != RT_EOK)
            rt_kprintf(", network is not ready");
        if (rt_event_recv(startup_events, EVENT_SENSOR_READY, RT_EVENT_FLAG_AND, 0, &recved) != RT_EOK)
            rt_kprintf(", sensor is not ready");
        if (rt_event_recv(startup_events, EVENT_STORAGE_READY, RT_EVENT_FLAG_AND, 0, &recved) != RT_EOK)
            rt_kprintf(", storage is not ready");
        rt_kprintf("\n");

        /* 继续等待全部就绪 */
        rt_event_recv(startup_events, EVENT_ALL_READY,
                      RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                      RT_WAITING_FOREVER, &recved);
    }

    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");

    rt_sem_release(startup_done);
}

int startup_demo(void)
{
    int i, created = 0;
    rt_thread_t tid;

    if (startup_running)
    {
        rt_kprintf("startup demo is running\n");
        return -RT_EBUSY;
    }

    startup_events = rt_event_create("s_event", RT_IPC_FLAG_PRIO);
    if (startup_events == RT_NULL)
    {
        rt_kprintf("create startup event failed\n");
        return -RT_ENOMEM;
    }
    startup_done = rt_sem_create("s_done", 0, RT_IPC_FLAG_PRIO);
    if (startup_done == RT_NULL)
    {
        rt_kprintf("create startup done semaphore failed\n");
        rt_event_delete(startup_events);
        startup_events = RT_NULL;
        return -RT_ENOMEM;
    }

    startup_running = 1;

    tid = rt_thread_create("net_init", net_init_entry, RT_NULL,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) { rt_thread_startup(tid); created++; }
    tid = rt_thread_create("sensor_init", sensor_init_entry, RT_NULL,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) { rt_thread_startup(tid); created++; }
    tid = rt_thread_create("storage_init", storage_init_entry, RT_NULL,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) { rt_thread_startup(tid); created++; }
    tid = rt_thread_create("business", business_entry, RT_NULL,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) { rt_thread_startup(tid); created++; }

    for (i = 0; i < created; i++)
        rt_sem_take(startup_done, RT_WAITING_FOREVER);

    rt_sem_delete(startup_done);
    rt_event_delete(startup_events);
    startup_events = RT_NULL;
    startup_done = RT_NULL;
    startup_running = 0;

    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, startup demo with event set);

/* ==================== 练习四：快递分拣中心（消息队列） ==================== */
#define PKG_PER_SENDER             5
#define PKG_END_REGION             0xFF

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};

static rt_mq_t pkg_mq = RT_NULL;
static rt_sem_t pkg_done = RT_NULL;
static rt_uint8_t pkg_running = 0;
static rt_uint32_t pkg_send_failed = 0;

static void sender_entry(void *parameter)
{
    int base = (int)parameter;
    struct package_msg msg;
    int i;
    rt_err_t result;

    for (i = 0; i < PKG_PER_SENDER; i++)
    {
        msg.id = base + i + 1;
        msg.region = (rt_uint8_t)(i % 3) + 1;
        msg.weight = (rt_uint16_t)(800 + (msg.id % 500));

        do
        {
            result = rt_mq_send(pkg_mq, &msg, sizeof(msg));
            if (result != RT_EOK)   /* 队列满：记录失败并稍后重发 */
            {
                pkg_send_failed++;
                rt_thread_mdelay(10);
            }
        } while (result != RT_EOK);
    }

    /* 结束消息：必须确保发送成功，分拣线程才能退出 */
    msg.id = 0;
    msg.region = PKG_END_REGION;
    msg.weight = 0;
    do
    {
        result = rt_mq_send(pkg_mq, &msg, sizeof(msg));
        if (result != RT_EOK)
            rt_thread_mdelay(10);
    } while (result != RT_EOK);

    rt_sem_release(pkg_done);
}

static void sorter_entry(void *parameter)
{
    struct package_msg msg;
    int end_count = 0, total = 0;
    int east = 0, south = 0, north = 0;
    rt_uint32_t total_weight = 0;

    while (end_count < 2)   /* 收到两个发件线程的结束消息后统计退出 */
    {
        if (rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER) != RT_EOK)
            continue;

        if (msg.region == PKG_END_REGION)
        {
            end_count++;
            continue;
        }

        total++;
        total_weight += msg.weight;
        switch (msg.region)
        {
        case 1:
            east++;
            rt_kprintf("package %d -> east, weight: %d g\n", msg.id, msg.weight);
            break;
        case 2:
            south++;
            rt_kprintf("package %d -> south, weight: %d g\n", msg.id, msg.weight);
            break;
        case 3:
            north++;
            rt_kprintf("package %d -> north, weight: %d g\n", msg.id, msg.weight);
            break;
        default:
            rt_kprintf("package %d -> unknown, weight: %d g\n", msg.id, msg.weight);
            break;
        }
    }

    rt_kprintf("total packages: %d\n", total);
    rt_kprintf("east: %d, south: %d, north: %d\n", east, south, north);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", pkg_send_failed);

    rt_sem_release(pkg_done);
}

int package_demo(void)
{
    int i, created = 0;
    rt_thread_t tid;

    if (pkg_running)
    {
        rt_kprintf("package demo is running\n");
        return -RT_EBUSY;
    }

    pkg_mq = rt_mq_create("pk_mq", sizeof(struct package_msg), 5, RT_IPC_FLAG_PRIO);
    if (pkg_mq == RT_NULL)
    {
        rt_kprintf("create package message queue failed\n");
        return -RT_ENOMEM;
    }
    pkg_done = rt_sem_create("pk_done", 0, RT_IPC_FLAG_PRIO);
    if (pkg_done == RT_NULL)
    {
        rt_kprintf("create package done semaphore failed\n");
        rt_mq_delete(pkg_mq);
        pkg_mq = RT_NULL;
        return -RT_ENOMEM;
    }

    pkg_send_failed = 0;
    pkg_running = 1;

    tid = rt_thread_create("sender1", sender_entry, (void *)1000,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) { rt_thread_startup(tid); created++; }
    tid = rt_thread_create("sender2", sender_entry, (void *)2000,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) { rt_thread_startup(tid); created++; }
    tid = rt_thread_create("sorter", sorter_entry, RT_NULL,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid != RT_NULL) { rt_thread_startup(tid); created++; }

    for (i = 0; i < created; i++)
        rt_sem_take(pkg_done, RT_WAITING_FOREVER);

    rt_sem_delete(pkg_done);
    rt_mq_delete(pkg_mq);
    pkg_mq = RT_NULL;
    pkg_done = RT_NULL;
    pkg_running = 0;

    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, package demo with message queue);

int main(void)
{
    int count = 1;

    while (count++)
    {
        //LOG_D("Hello RT-Thread!");
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}
