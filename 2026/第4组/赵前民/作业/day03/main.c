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
#include <rtdevice.h>
#define DBG_TAG "main"
#define LOG_LVL LOG_LVL_DBG
#include <rtdbg.h>
#include <ulog.h>
#include <board.h>

/* ===================== 练习一：停车场车位管理 parking_demo ===================== */
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

//            rt_tick_t stay_tick = RT_TICK_PER_SECOND / 2 + rt_rand() % RT_TICK_PER_SECOND;
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

/* ===================== 练习二：多窗口售票系统 ticket_demo ===================== */
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

/* ===================== 练习三：系统启动条件检查 startup_demo ===================== */
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

/* ===================== 练习四：快递分拣中心 package_demo ===================== */
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

int main(void)
{
    int count = 1;
    while (count++)
    {
//        LOG_D("Hello RT-Thread!");
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}
