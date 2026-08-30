/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     LengMengqi   Day 3 Homework: Inter-Thread Synchronization & Communication (IPC)
 *                             Target: STM32F407 (Spark 1 / 星火一号) & RT-Thread Studio
 */

#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "ipc_hw"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ============================================================================
 * 练习一：停车场车位管理 (计数信号量 Counting Semaphore)
 * ============================================================================
 * 场景：3 个车位，5 辆车并发竞争。
 * 机制：基于计数值为 3 的信号量进行资源配额管理，支持 2 秒阻塞超时与重试。
 * ============================================================================ */
#define TOTAL_SPACES 3
#define TOTAL_CARS   5

static rt_sem_t   g_park_sem = RT_NULL;
static rt_mutex_t g_park_mutex = RT_NULL;
static int        g_used_spaces = 0;
static int        g_cars_finished = 0;
static rt_bool_t  g_parking_running = RT_FALSE;

static void car_thread_entry(void *parameter)
{
    rt_ubase_t car_id = (rt_ubase_t)parameter;
    rt_err_t result;

    rt_kprintf("[Car %d] arriving at parking lot...\n", (int)car_id);

    /* 尝试申请车位，超时时间设置为 2 秒 (2000 ms) */
    result = rt_sem_take(g_park_sem, rt_tick_from_millisecond(2000));
    if (result == -RT_ETIMEOUT)
    {
        rt_kprintf("car%d waiting for a space (timeout 2s), retry later...\n", (int)car_id);
        /* 等待片刻后再次发起阻塞申请 (永久等待直到获得车位) */
        rt_thread_mdelay(800);
        result = rt_sem_take(g_park_sem, RT_WAITING_FOREVER);
    }

    if (result == RT_EOK)
    {
        /* 成功获得车位，在互斥保护下更新已用计数并打印 */
        rt_mutex_take(g_park_mutex, RT_WAITING_FOREVER);
        g_used_spaces++;
        rt_kprintf("car%d entered, used spaces: %d\n", (int)car_id, g_used_spaces);
        rt_mutex_release(g_park_mutex);

        /* 模拟在停车场内的驻留时间 (1500ms ~ 2300ms) */
        rt_thread_mdelay(1500 + (car_id * 200));

        /* 车辆离开，更新计数并释放信号量 */
        rt_mutex_take(g_park_mutex, RT_WAITING_FOREVER);
        g_used_spaces--;
        rt_kprintf("car%d left, remaining spaces: %d\n", (int)car_id, TOTAL_SPACES - g_used_spaces);
        rt_mutex_release(g_park_mutex);

        /* 归还信号量资源，唤醒等待队列中的阻塞车辆 */
        rt_sem_release(g_park_sem);
    }

    /* 统计已完成的车辆数量，全部完成后回收动态资源并复位标志 */
    rt_mutex_take(g_park_mutex, RT_WAITING_FOREVER);
    g_cars_finished++;
    if (g_cars_finished >= TOTAL_CARS)
    {
        rt_kprintf(">>> [Parking Demo] All %d cars completed parking! <<<\n", TOTAL_CARS);
        rt_mutex_release(g_park_mutex);

        /* 稍作延时确保各线程完全脱离 */
        rt_thread_mdelay(100);
        if (g_park_sem != RT_NULL)
        {
            rt_sem_delete(g_park_sem);
            g_park_sem = RT_NULL;
        }
        if (g_park_mutex != RT_NULL)
        {
            rt_mutex_delete(g_park_mutex);
            g_park_mutex = RT_NULL;
        }
        g_parking_running = RT_FALSE;
    }
    else
    {
        rt_mutex_release(g_park_mutex);
    }
}

static void parking_demo(void)
{
    if (g_parking_running)
    {
        rt_kprintf("[Parking Demo] Already running! Please wait for it to complete.\n");
        return;
    }

    g_parking_running = RT_TRUE;
    g_used_spaces = 0;
    g_cars_finished = 0;

    /* 创建初始值为 3 的计数信号量 */
    g_park_sem = rt_sem_create("park_sem", TOTAL_SPACES, RT_IPC_FLAG_FIFO);
    if (g_park_sem == RT_NULL)
    {
        rt_kprintf("Error: Failed to create park_sem!\n");
        g_parking_running = RT_FALSE;
        return;
    }

    /* 创建内部互斥量用于状态打印与计数保护 */
    g_park_mutex = rt_mutex_create("park_mtx", RT_IPC_FLAG_PRIO);
    if (g_park_mutex == RT_NULL)
    {
        rt_kprintf("Error: Failed to create park_mutex!\n");
        rt_sem_delete(g_park_sem);
        g_park_sem = RT_NULL;
        g_parking_running = RT_FALSE;
        return;
    }

    rt_kprintf("=== Starting Parking Demo (Capacity: %d, Cars: %d) ===\n", TOTAL_SPACES, TOTAL_CARS);

    /* 创建 5 个车辆线程 */
    for (rt_ubase_t i = 1; i <= TOTAL_CARS; i++)
    {
        char name[RT_NAME_MAX];
        rt_snprintf(name, sizeof(name), "car%d", (int)i);

        rt_thread_t th = rt_thread_create(name, car_thread_entry, (void *)i, 1024, 15, 10);
        if (th != RT_NULL)
        {
            rt_thread_startup(th);
        }
        else
        {
            rt_kprintf("Error: Failed to create thread %s!\n", name);
        }
    }
}
MSH_CMD_EXPORT(parking_demo, Parking lot simulation with counting semaphore);

/* ============================================================================
 * 练习二：多窗口售票系统 (互斥量 Mutex 与竞态对比)
 * ============================================================================
 * 场景：2 个窗口并发售卖 20 张车票。
 * 对比：
 *   1. ticket_demo        : 开启互斥量加锁，保证 Check-then-Act 复合操作原子性；
 *   2. ticket_demo unsafe : 无锁模式，放大竞态窗口，直观展示重复出票与超卖负数。
 * ============================================================================ */
#define INITIAL_TICKETS 20

static rt_mutex_t g_ticket_mutex = RT_NULL;
static int        g_ticket_count = INITIAL_TICKETS;
static rt_bool_t  g_ticket_running = RT_FALSE;
static rt_bool_t  g_ticket_unsafe_mode = RT_FALSE;
static int        g_sellers_finished = 0;

static void ticket_seller_entry(void *parameter)
{
    const char *win_name = (const char *)parameter;

    while (1)
    {
        if (!g_ticket_unsafe_mode)
        {
            /* 【安全模式】：互斥量保护完整的 Check-then-Act 临界区闭环 */
            rt_mutex_take(g_ticket_mutex, RT_WAITING_FOREVER);

            if (g_ticket_count > 0)
            {
                int current_ticket = g_ticket_count;
                /* 模拟出票与打印系统处理延迟 */
                rt_thread_mdelay(15);
                g_ticket_count--;
                rt_kprintf("window %s sold ticket %02d (remaining: %d)\n", win_name, current_ticket, g_ticket_count);
                rt_mutex_release(g_ticket_mutex);

                /* 模拟接待下一位乘客的间隔，主动让出 CPU 调度机会 */
                rt_thread_mdelay(25);
            }
            else
            {
                rt_mutex_release(g_ticket_mutex);
                break;
            }
        }
        else
        {
            /* 【不安全对比模式】：不加锁，在检查与修改间插入延时暴露竞态漏洞 */
            if (g_ticket_count > 0)
            {
                int current_ticket = g_ticket_count;
                rt_thread_mdelay(20); /* 延时放大竞态窗口 */
                g_ticket_count--;
                rt_kprintf("[UNSAFE] window %s sold ticket %02d (remaining: %d)\n", win_name, current_ticket, g_ticket_count);
                rt_thread_mdelay(20);
            }
            else
            {
                break;
            }
        }
    }

    g_sellers_finished++;
    if (g_sellers_finished >= 2)
    {
        rt_kprintf("----------------------------------------\n");
        rt_kprintf("all tickets sold\n");
        rt_kprintf("remaining tickets: %d\n", g_ticket_count);
        rt_kprintf("----------------------------------------\n");

        if (g_ticket_mutex != RT_NULL)
        {
            rt_mutex_delete(g_ticket_mutex);
            g_ticket_mutex = RT_NULL;
        }
        g_ticket_running = RT_FALSE;
    }
}

static void ticket_demo(int argc, char **argv)
{
    if (g_ticket_running)
    {
        rt_kprintf("[Ticket Demo] Already running! Please wait for completion.\n");
        return;
    }

    g_ticket_unsafe_mode = RT_FALSE;
    if (argc >= 2 && rt_strcmp(argv[1], "unsafe") == 0)
    {
        g_ticket_unsafe_mode = RT_TRUE;
        rt_kprintf("=== Starting Ticket Demo [UNSAFE MODE: NO MUTEX] ===\n");
    }
    else
    {
        rt_kprintf("=== Starting Ticket Demo [SAFE MODE: WITH MUTEX] ===\n");
    }

    g_ticket_running = RT_TRUE;
    g_ticket_count = INITIAL_TICKETS;
    g_sellers_finished = 0;

    g_ticket_mutex = rt_mutex_create("tick_mtx", RT_IPC_FLAG_PRIO);
    if (g_ticket_mutex == RT_NULL)
    {
        rt_kprintf("Error: Failed to create ticket mutex!\n");
        g_ticket_running = RT_FALSE;
        return;
    }

    rt_thread_t th_a = rt_thread_create("win_A", ticket_seller_entry, "A", 1024, 16, 5);
    rt_thread_t th_b = rt_thread_create("win_B", ticket_seller_entry, "B", 1024, 16, 5);

    if (th_a != RT_NULL && th_b != RT_NULL)
    {
        rt_thread_startup(th_a);
        rt_thread_startup(th_b);
    }
    else
    {
        rt_kprintf("Error: Failed to create ticket seller threads!\n");
        if (th_a != RT_NULL) rt_thread_delete(th_a);
        if (th_b != RT_NULL) rt_thread_delete(th_b);
        rt_mutex_delete(g_ticket_mutex);
        g_ticket_mutex = RT_NULL;
        g_ticket_running = RT_FALSE;
    }
}
MSH_CMD_EXPORT(ticket_demo, Multi-window ticket selling simulation (Usage: ticket_demo [unsafe]));

/* ============================================================================
 * 练习三：系统启动条件检查 (事件集 Event Set)
 * ============================================================================
 * 场景：网络、传感器、存储三个模块就绪后才允许业务线程启动。
 * 机制：使用 32 位事件位图与 AND 逻辑进行多源事件聚合等待与超时诊断。
 * ============================================================================ */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY      (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

static rt_event_t g_startup_event = RT_NULL;
static rt_bool_t  g_startup_running = RT_FALSE;

static void net_init_thread_entry(void *parameter)
{
    /* 模拟网络模块初始化耗时 1 秒 */
    rt_thread_mdelay(1000);
    rt_kprintf("network ready\n");
    rt_event_send(g_startup_event, EVENT_NET_READY);
}

static void sensor_init_thread_entry(void *parameter)
{
    /* 模拟传感器模块初始化耗时 2 秒 */
    rt_thread_mdelay(2000);
    rt_kprintf("sensor ready\n");
    rt_event_send(g_startup_event, EVENT_SENSOR_READY);
}

static void storage_init_thread_entry(void *parameter)
{
    /* 模拟存储模块初始化耗时 5 秒 (用于触发主业务线程 3 秒超时) */
    rt_thread_mdelay(5000);
    rt_kprintf("storage ready\n");
    rt_event_send(g_startup_event, EVENT_STORAGE_READY);
}

static void business_thread_entry(void *parameter)
{
    rt_uint32_t recved = 0;
    rt_err_t result;

    /* 第一次等待：3 秒超时 (3000ms)，AND 模式，不清除已就绪标志位 */
    result = rt_event_recv(g_startup_event,
                           EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND,
                           rt_tick_from_millisecond(3000),
                           &recved);

    if (result == -RT_ETIMEOUT)
    {
        /* 查询当前事件集位图，精确诊断未完成模块 */
        rt_uint32_t current_set = g_startup_event->set;
        rt_kprintf("wait modules timeout, [");
        if (!(current_set & EVENT_NET_READY))     rt_kprintf(" network");
        if (!(current_set & EVENT_SENSOR_READY))  rt_kprintf(" sensor");
        if (!(current_set & EVENT_STORAGE_READY)) rt_kprintf(" storage");
        rt_kprintf(" ] is not ready\n");

        /* 第二次等待：永久阻塞等待所有模块全部就绪，并在唤醒后自动清除 */
        result = rt_event_recv(g_startup_event,
                               EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               RT_WAITING_FOREVER,
                               &recved);
    }

    if (result == RT_EOK)
    {
        rt_kprintf("all modules are ready\n");
        rt_kprintf("business task started\n");
    }

    /* 业务启动完毕，清理事件集对象 */
    rt_thread_mdelay(200);
    if (g_startup_event != RT_NULL)
    {
        rt_event_delete(g_startup_event);
        g_startup_event = RT_NULL;
    }
    g_startup_running = RT_FALSE;
}

static void startup_demo(void)
{
    if (g_startup_running)
    {
        rt_kprintf("[Startup Demo] Already running! Please wait for completion.\n");
        return;
    }

    g_startup_running = RT_TRUE;

    g_startup_event = rt_event_create("start_ev", RT_IPC_FLAG_FIFO);
    if (g_startup_event == RT_NULL)
    {
        rt_kprintf("Error: Failed to create startup event set!\n");
        g_startup_running = RT_FALSE;
        return;
    }

    rt_kprintf("=== Starting System Startup Check Demo ===\n");

    rt_thread_t th_net     = rt_thread_create("th_net", net_init_thread_entry, RT_NULL, 1024, 14, 5);
    rt_thread_t th_sensor  = rt_thread_create("th_sensor", sensor_init_thread_entry, RT_NULL, 1024, 14, 5);
    rt_thread_t th_storage = rt_thread_create("th_storage", storage_init_thread_entry, RT_NULL, 1024, 14, 5);
    rt_thread_t th_biz     = rt_thread_create("th_biz", business_thread_entry, RT_NULL, 1536, 12, 10);

    if (th_net && th_sensor && th_storage && th_biz)
    {
        rt_thread_startup(th_net);
        rt_thread_startup(th_sensor);
        rt_thread_startup(th_storage);
        rt_thread_startup(th_biz);
    }
    else
    {
        rt_kprintf("Error: Failed to create startup threads!\n");
        if (th_net)     rt_thread_delete(th_net);
        if (th_sensor)  rt_thread_delete(th_sensor);
        if (th_storage) rt_thread_delete(th_storage);
        if (th_biz)     rt_thread_delete(th_biz);
        rt_event_delete(g_startup_event);
        g_startup_event = RT_NULL;
        g_startup_running = RT_FALSE;
    }
}
MSH_CMD_EXPORT(startup_demo, System startup condition check using event set);

/* ============================================================================
 * 练习四：快递分拣中心 (消息队列 Message Queue 与紧急消息)
 * ============================================================================
 * 场景：2 个发件线程产生快递数据，1 个分拣线程接收并按区域分类统计。
 * 特性：值拷贝结构体传递、队列满非阻塞捕获与重试、终止消息帧协议。
 * ============================================================================ */
#define REGION_EAST   1
#define REGION_SOUTH  2
#define REGION_NORTH  3

struct package_msg
{
    rt_uint32_t id;      /* 快递单号，0 表示结束协议帧 */
    rt_uint8_t  region;  /* 1: 华东, 2: 华南, 3: 华北 */
    rt_uint16_t weight;  /* 重量 (g) */
};

static rt_mq_t   g_pkg_mq = RT_NULL;
static rt_bool_t g_package_running = RT_FALSE;
static int       g_send_failed_count = 0;
static int       g_senders_finished = 0;

static const char *get_region_name(rt_uint8_t region)
{
    switch (region)
    {
    case REGION_EAST:  return "east";
    case REGION_SOUTH: return "south";
    case REGION_NORTH: return "north";
    default:           return "unknown";
    }
}

static void sender1_thread_entry(void *parameter)
{
    struct package_msg pkgs[5] = {
        {1001, REGION_EAST,  1200},
        {1002, REGION_NORTH, 960},
        {1003, REGION_SOUTH, 850},
        {1004, REGION_EAST,  1500},
        {1005, REGION_SOUTH, 620}
    };

    for (int i = 0; i < 5; i++)
    {
        /* 尝试普通发送入队尾 */
        rt_err_t res = rt_mq_send(g_pkg_mq, &pkgs[i], sizeof(struct package_msg));
        if (res != RT_EOK)
        {
            /* 队列满时安全记录失败次数，并不崩溃，稍后带超时重试 */
            g_send_failed_count++;
            rt_thread_mdelay(100);
            rt_mq_send_wait(g_pkg_mq, &pkgs[i], sizeof(struct package_msg), RT_WAITING_FOREVER);
        }
        rt_thread_mdelay(150);
    }

    g_senders_finished++;
    if (g_senders_finished >= 2)
    {
        /* 最后一个发送者负责发送结束标志帧 (id = 0) */
        struct package_msg end_msg = {0, 0, 0};
        rt_mq_send_wait(g_pkg_mq, &end_msg, sizeof(end_msg), RT_WAITING_FOREVER);
    }
}

static void sender2_thread_entry(void *parameter)
{
    struct package_msg pkgs[5] = {
        {2001, REGION_SOUTH, 1100},
        {2002, REGION_EAST,  780},
        {2003, REGION_NORTH, 2300},
        {2004, REGION_NORTH, 650},
        {2005, REGION_EAST,  1490}
    };

    for (int i = 0; i < 5; i++)
    {
        rt_err_t res = rt_mq_send(g_pkg_mq, &pkgs[i], sizeof(struct package_msg));
        if (res != RT_EOK)
        {
            g_send_failed_count++;
            rt_thread_mdelay(100);
            rt_mq_send_wait(g_pkg_mq, &pkgs[i], sizeof(struct package_msg), RT_WAITING_FOREVER);
        }
        rt_thread_mdelay(200);
    }

    g_senders_finished++;
    if (g_senders_finished >= 2)
    {
        struct package_msg end_msg = {0, 0, 0};
        rt_mq_send_wait(g_pkg_mq, &end_msg, sizeof(end_msg), RT_WAITING_FOREVER);
    }
}

static void sorter_thread_entry(void *parameter)
{
    struct package_msg msg;
    rt_uint32_t total_packages = 0;
    rt_uint32_t east_count = 0;
    rt_uint32_t south_count = 0;
    rt_uint32_t north_count = 0;
    rt_uint32_t total_weight = 0;

    while (1)
    {
        rt_err_t result = rt_mq_recv(g_pkg_mq, &msg, sizeof(struct package_msg), RT_WAITING_FOREVER);
        if (result == RT_EOK)
        {
            /* 识别结束协议标志帧 */
            if (msg.id == 0)
            {
                break;
            }

            rt_kprintf("package %d -> %s, weight: %d g\n", msg.id, get_region_name(msg.region), msg.weight);

            total_packages++;
            total_weight += msg.weight;

            if (msg.region == REGION_EAST)       east_count++;
            else if (msg.region == REGION_SOUTH) south_count++;
            else if (msg.region == REGION_NORTH) north_count++;
        }
    }

    /* 输出最终汇总统计报表 */
    rt_kprintf("----------------------------------------\n");
    rt_kprintf("total packages: %d\n", total_packages);
    rt_kprintf("east: %d, south: %d, north: %d\n", east_count, south_count, north_count);
    rt_kprintf("total weight: %d g\n", total_weight);
    rt_kprintf("send failed: %d\n", g_send_failed_count);
    rt_kprintf("----------------------------------------\n");

    /* 销毁消息队列并释放运行标志 */
    if (g_pkg_mq != RT_NULL)
    {
        rt_mq_delete(g_pkg_mq);
        g_pkg_mq = RT_NULL;
    }
    g_package_running = RT_FALSE;
}

static void package_demo(void)
{
    if (g_package_running)
    {
        rt_kprintf("[Package Demo] Already running! Please wait for completion.\n");
        return;
    }

    g_package_running = RT_TRUE;
    g_send_failed_count = 0;
    g_senders_finished = 0;

    /* 创建容量为 5 条消息的消息队列 */
    g_pkg_mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), 5, RT_IPC_FLAG_FIFO);
    if (g_pkg_mq == RT_NULL)
    {
        rt_kprintf("Error: Failed to create package message queue!\n");
        g_package_running = RT_FALSE;
        return;
    }

    rt_kprintf("=== Starting Package Sorting Center Demo ===\n");

    rt_thread_t th_sender1 = rt_thread_create("sender1", sender1_thread_entry, RT_NULL, 1024, 15, 5);
    rt_thread_t th_sender2 = rt_thread_create("sender2", sender2_thread_entry, RT_NULL, 1024, 15, 5);
    rt_thread_t th_sorter  = rt_thread_create("sorter",  sorter_thread_entry,  RT_NULL, 1536, 14, 5);

    if (th_sender1 && th_sender2 && th_sorter)
    {
        rt_thread_startup(th_sender1);
        rt_thread_startup(th_sender2);
        rt_thread_startup(th_sorter);
    }
    else
    {
        rt_kprintf("Error: Failed to create package threads!\n");
        if (th_sender1) rt_thread_delete(th_sender1);
        if (th_sender2) rt_thread_delete(th_sender2);
        if (th_sorter)  rt_thread_delete(th_sorter);
        rt_mq_delete(g_pkg_mq);
        g_pkg_mq = RT_NULL;
        g_package_running = RT_FALSE;
    }
}
MSH_CMD_EXPORT(package_demo, Express package sorting demo using message queue);
