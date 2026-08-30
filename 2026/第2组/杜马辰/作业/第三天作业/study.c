/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     DuMachen     DAY3 IPC sample implementations
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define DBG_TAG "day3_ipc"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ==========================================================================
 * 练习一：停车场车位管理（计数信号量 Semaphore）
 * 场景：停车场有 3 个空闲车位，5 辆车线程并发申请进入。
 * ========================================================================== */
#define PARK_TOTAL_SPACES       3
#define PARK_CAR_NUM            5
#define PARK_THREAD_PRIORITY    25
#define PARK_THREAD_TIMESLICE   5

static rt_sem_t g_park_sem = RT_NULL;
static struct rt_thread g_car_threads[PARK_CAR_NUM];
static rt_uint8_t g_car_stacks[PARK_CAR_NUM][1024];

static void car_thread_entry(void *parameter)
{
    rt_uint32_t car_id = (rt_uint32_t)parameter;
    rt_err_t res;

    while (1)
    {
        /* 尝试申请车位，等待最多 2000ms */
        res = rt_sem_take(g_park_sem, rt_tick_from_millisecond(2000));
        if (res == -RT_ETIMEOUT)
        {
            rt_kprintf("Car %u: Waiting for an available space (timeout 2s)...\n", car_id);
            rt_thread_mdelay(1000);
        }
        else if (res == RT_EOK)
        {
            rt_kprintf("Car %u: Entered successfully! [Current Used: %d / %d]\n",
                       car_id, PARK_TOTAL_SPACES - g_park_sem->value, PARK_TOTAL_SPACES);

            /* 模拟停车停留时间 (不同车辆不同停留时长) */
            rt_thread_mdelay(1500 + car_id * 500);

            /* 离开停车场，释放车位资源 */
            rt_sem_release(g_park_sem);
            rt_kprintf("Car %u: Left the parking lot. [Current Used: %d / %d]\n",
                       car_id, PARK_TOTAL_SPACES - g_park_sem->value, PARK_TOTAL_SPACES);

            /* 模拟下一次出行间隔 */
            rt_thread_mdelay(2000);
        }
    }
}

int parking_demo(void)
{
    char name[RT_NAME_MAX];

    /* 1. 创建计数信号量，初始可用资源为 3 */
    if (g_park_sem == RT_NULL)
    {
        g_park_sem = rt_sem_create("park_sem", PARK_TOTAL_SPACES, RT_IPC_FLAG_PRIO);
        if (g_park_sem == RT_NULL)
        {
            LOG_E("Failed to create parking semaphore!");
            return -RT_ERROR;
        }
    }

    rt_kprintf("\n[Parking Demo Started] Total spaces: %d, Cars: %d\n", PARK_TOTAL_SPACES, PARK_CAR_NUM);

    /* 2. 初始化并启动 5 个车辆线程 */
    for (rt_uint32_t i = 0; i < PARK_CAR_NUM; i++)
    {
        rt_snprintf(name, sizeof(name), "car%u", i + 1);
        rt_thread_init(&g_car_threads[i],
                       name,
                       car_thread_entry,
                       (void *)(i + 1),
                       &g_car_stacks[i][0],
                       sizeof(g_car_stacks[i]),
                       PARK_THREAD_PRIORITY,
                       PARK_THREAD_TIMESLICE);
        rt_thread_startup(&g_car_threads[i]);
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, counting semaphore parking lot sample);

/* ==========================================================================
 * 练习二：多窗口售票系统（互斥量 Mutex）
 * 场景：2 个售票窗口并发售卖 20 张票，使用互斥锁保护临界区防止超卖。
 * ========================================================================== */
#define TICKET_TOTAL_NUM        20
#define TICKET_THREAD_PRIO      20
#define TICKET_THREAD_SLICE     5

static rt_mutex_t g_ticket_mutex = RT_NULL;
static volatile rt_uint32_t g_remaining_tickets = TICKET_TOTAL_NUM;

static struct rt_thread g_window_a;
static struct rt_thread g_window_b;
static rt_uint8_t g_win_a_stack[1024];
static rt_uint8_t g_win_b_stack[1024];

static void ticket_seller_entry(void *parameter)
{
    const char *window_name = (const char *)parameter;

    while (1)
    {
        /* 获取互斥量，完整保护 "读取-判断-出票-自减" 临界区 */
        if (rt_mutex_take(g_ticket_mutex, RT_WAITING_FOREVER) == RT_EOK)
        {
            if (g_remaining_tickets == 0)
            {
                rt_kprintf("[%s] All tickets sold out! (Remaining: 0)\n", window_name);
                rt_mutex_release(g_ticket_mutex);
                break; // 票已售罄，安全退出
            }

            rt_kprintf("[%s] Sold ticket NO. %02u (Remaining: %02u)\n",
                       window_name, g_remaining_tickets, g_remaining_tickets - 1);
            g_remaining_tickets--;

            rt_mutex_release(g_ticket_mutex);
        }

        /* 锁外延时，模拟出票间隔并让出 CPU */
        rt_thread_mdelay(100);
    }
}

int ticket_demo(void)
{
    g_remaining_tickets = TICKET_TOTAL_NUM;

    if (g_ticket_mutex == RT_NULL)
    {
        g_ticket_mutex = rt_mutex_create("tick_mtx", RT_IPC_FLAG_PRIO);
        if (g_ticket_mutex == RT_NULL)
        {
            LOG_E("Failed to create ticket mutex!");
            return -RT_ERROR;
        }
    }

    rt_kprintf("\n[Ticket Demo Started] Total Tickets: %u\n", TICKET_TOTAL_NUM);

    rt_thread_init(&g_window_a,
                   "win_A",
                   ticket_seller_entry,
                   (void *)"Window A",
                   &g_win_a_stack[0],
                   sizeof(g_win_a_stack),
                   TICKET_THREAD_PRIO,
                   TICKET_THREAD_SLICE);
    rt_thread_startup(&g_window_a);

    rt_thread_init(&g_window_b,
                   "win_B",
                   ticket_seller_entry,
                   (void *)"Window B",
                   &g_win_b_stack[0],
                   sizeof(g_win_b_stack),
                   TICKET_THREAD_PRIO,
                   TICKET_THREAD_SLICE);
    rt_thread_startup(&g_window_b);

    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, mutex ticket selling sample);

/* ==========================================================================
 * 练习三：系统启动条件检查（事件集 Event）
 * 场景：网络(1s)、传感器(2s)、存储(5s) 初始化完成后，主业务线程 AND 触发启动。
 * ========================================================================== */
#define EVENT_NET_READY         (1U << 0)
#define EVENT_SENSOR_READY      (1U << 1)
#define EVENT_STORAGE_READY     (1U << 2)

static struct rt_event g_startup_event;
static struct rt_thread g_net_thread;
static struct rt_thread g_sensor_thread;
static struct rt_thread g_storage_thread;
static struct rt_thread g_business_thread;

static rt_uint8_t g_net_stack[1024];
static rt_uint8_t g_sensor_stack[1024];
static rt_uint8_t g_storage_stack[1024];
static rt_uint8_t g_business_stack[1024];

static void net_init_entry(void *param)
{
    rt_thread_mdelay(1000);
    rt_kprintf("[Init] Network module initialized ready.\n");
    rt_event_send(&g_startup_event, EVENT_NET_READY);
}

static void sensor_init_entry(void *param)
{
    rt_thread_mdelay(2000);
    rt_kprintf("[Init] Sensor module initialized ready.\n");
    rt_event_send(&g_startup_event, EVENT_SENSOR_READY);
}

static void storage_init_entry(void *param)
{
    rt_thread_mdelay(5000);
    rt_kprintf("[Init] Storage module initialized ready.\n");
    rt_event_send(&g_startup_event, EVENT_STORAGE_READY);
}

static void business_thread_entry(void *param)
{
    rt_uint32_t recved_event;
    rt_err_t res;

    while (1)
    {
        /* 使用 AND 模式等待所有位就绪，单次等待 3000ms */
        res = rt_event_recv(&g_startup_event,
                            (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY),
                            RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                            rt_tick_from_millisecond(3000),
                            &recved_event);

        if (res == -RT_ETIMEOUT)
        {
            rt_kprintf("[Business] Waiting modules timeout! Missing:\n");
            if (!(g_startup_event.set & EVENT_NET_READY))     rt_kprintf("  - Network NOT ready\n");
            if (!(g_startup_event.set & EVENT_SENSOR_READY))  rt_kprintf("  - Sensor NOT ready\n");
            if (!(g_startup_event.set & EVENT_STORAGE_READY)) rt_kprintf("  - Storage NOT ready\n");
        }
        else if (res == RT_EOK)
        {
            rt_kprintf("\n[Business] ALL modules are READY!\n");
            rt_kprintf("[Business] Main business task is NOW RUNNING...\n\n");
            break;
        }
    }
}

int startup_demo(void)
{
    rt_event_init(&g_startup_event, "start_evt", RT_IPC_FLAG_PRIO);

    rt_kprintf("\n[Startup Demo Started] Waiting for Net(1s), Sensor(2s), Storage(5s)...\n");

    rt_thread_init(&g_net_thread, "t_net", net_init_entry, RT_NULL, &g_net_stack[0], sizeof(g_net_stack), 15, 5);
    rt_thread_startup(&g_net_thread);

    rt_thread_init(&g_sensor_thread, "t_sensor", sensor_init_entry, RT_NULL, &g_sensor_stack[0], sizeof(g_sensor_stack), 15, 5);
    rt_thread_startup(&g_sensor_thread);

    rt_thread_init(&g_storage_thread, "t_storage", storage_init_entry, RT_NULL, &g_storage_stack[0], sizeof(g_storage_stack), 15, 5);
    rt_thread_startup(&g_storage_thread);

    rt_thread_init(&g_business_thread, "t_biz", business_thread_entry, RT_NULL, &g_business_stack[0], sizeof(g_business_stack), 14, 5);
    rt_thread_startup(&g_business_thread);

    return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, event multi-module startup check sample);

/* ==========================================================================
 * 练习四：快递分拣中心（消息队列 Message Queue）
 * 场景：2 个发件线程共发送 10 件快递（容量为 5），分拣线程统计各区域与重量。
 * ========================================================================== */
#define REGION_EAST             1   /* 华东 */
#define REGION_SOUTH            2   /* 华南 */
#define REGION_NORTH            3   /* 华北 */

typedef struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;
    rt_uint16_t weight;
} package_msg_t;

static struct rt_messagequeue g_pkg_mq;
static rt_uint8_t g_pkg_msg_pool[5 * (sizeof(package_msg_t) + sizeof(void *))];

static struct rt_thread g_sender1;
static struct rt_thread g_sender2;
static struct rt_thread g_receiver;
static rt_uint8_t g_send1_stack[1024];
static rt_uint8_t g_send2_stack[1024];
static rt_uint8_t g_recv_stack[1024];

static void sender1_entry(void *param)
{
    package_msg_t pkgs[5] = {
        {101, REGION_EAST,  500},
        {102, REGION_SOUTH, 600},
        {103, REGION_NORTH, 700},
        {104, REGION_EAST,  800},
        {105, REGION_SOUTH, 900}
    };
    rt_uint32_t fail_cnt = 0;
    rt_err_t res;

    for (int i = 0; i < 5;)
    {
        res = rt_mq_send(&g_pkg_mq, &pkgs[i], sizeof(package_msg_t));
        if (res == -RT_EFULL)
        {
            fail_cnt++;
            rt_kprintf("[Sender 1] Queue FULL! Fail count: %u, retry after 100ms...\n", fail_cnt);
            rt_thread_mdelay(100);
        }
        else if (res == RT_EOK)
        {
            rt_kprintf("[Sender 1] Sent Package #%d -> Region %d, Weight %dg\n",
                       pkgs[i].id, pkgs[i].region, pkgs[i].weight);
            i++;
            rt_thread_mdelay(150);
        }
    }
}

static void sender2_entry(void *param)
{
    package_msg_t pkgs[5] = {
        {201, REGION_NORTH, 550},
        {202, REGION_EAST,  650},
        {203, REGION_SOUTH, 750},
        {204, REGION_NORTH, 850},
        {205, REGION_EAST,  950}
    };
    rt_uint32_t fail_cnt = 0;
    rt_err_t res;

    for (int i = 0; i < 5;)
    {
        res = rt_mq_send(&g_pkg_mq, &pkgs[i], sizeof(package_msg_t));
        if (res == -RT_EFULL)
        {
            fail_cnt++;
            rt_kprintf("[Sender 2] Queue FULL! Fail count: %u, retry after 100ms...\n", fail_cnt);
            rt_thread_mdelay(100);
        }
        else if (res == RT_EOK)
        {
            rt_kprintf("[Sender 2] Sent Package #%d -> Region %d, Weight %dg\n",
                       pkgs[i].id, pkgs[i].region, pkgs[i].weight);
            i++;
            rt_thread_mdelay(150);
        }
    }
}

static void receiver_entry(void *param)
{
    package_msg_t msg;
    rt_uint32_t total_pkgs = 0;
    rt_uint32_t east_cnt = 0, south_cnt = 0, north_cnt = 0;
    rt_uint32_t total_weight = 0;

    while (1)
    {
        if (rt_mq_recv(&g_pkg_mq, &msg, sizeof(package_msg_t), RT_WAITING_FOREVER) == RT_EOK)
        {
            total_pkgs++;
            total_weight += msg.weight;

            switch (msg.region)
            {
            case REGION_EAST:  east_cnt++;  break;
            case REGION_SOUTH: south_cnt++; break;
            case REGION_NORTH: north_cnt++; break;
            default: break;
            }

            rt_kprintf("  >> [Sorter] Received #%d (Region: %d, Weight: %dg) | Processed: %u/10\n",
                       msg.id, msg.region, msg.weight, total_pkgs);

            if (total_pkgs == 10)
            {
                rt_kprintf("\n================ [Sorting Center Summary] ================\n");
                rt_kprintf("Total Packages Processed : %u\n", total_pkgs);
                rt_kprintf("  - East Region (华东)   : %u\n", east_cnt);
                rt_kprintf("  - South Region (华南)  : %u\n", south_cnt);
                rt_kprintf("  - North Region (华北)  : %u\n", north_cnt);
                rt_kprintf("Total Weight             : %u g\n", total_weight);
                rt_kprintf("==========================================================\n\n");
                break;
            }
        }
    }
}

int package_demo(void)
{
    rt_err_t res;

    res = rt_mq_init(&g_pkg_mq,
                     "pkg_mq",
                     &g_pkg_msg_pool[0],
                     sizeof(package_msg_t),
                     sizeof(g_pkg_msg_pool),
                     RT_IPC_FLAG_PRIO);
    if (res != RT_EOK)
    {
        LOG_E("Failed to initialize package message queue!");
        return -RT_ERROR;
    }

    rt_kprintf("\n[Package Sorting Demo Started] Queue Capacity: 5, Total Packages: 10\n");

    rt_thread_init(&g_receiver, "t_sort", receiver_entry, RT_NULL, &g_recv_stack[0], sizeof(g_recv_stack), 20, 5);
    rt_thread_startup(&g_receiver);

    rt_thread_init(&g_sender1, "t_send1", sender1_entry, RT_NULL, &g_send1_stack[0], sizeof(g_send1_stack), 22, 5);
    rt_thread_startup(&g_sender1);

    rt_thread_init(&g_sender2, "t_send2", sender2_entry, RT_NULL, &g_send2_stack[0], sizeof(g_send2_stack), 22, 5);
    rt_thread_startup(&g_sender2);

    return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, message queue package sorting sample);
