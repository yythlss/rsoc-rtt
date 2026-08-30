/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     DuMachen     DAY5 AHT10 sampling and DFS storage
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <dfs_posix.h>
#include "aht10.h"

#define DBG_TAG "aht10_app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 温湿度数据传递结构体 */
typedef struct
{
    float temperature;
    float humidity;
    rt_tick_t timestamp;
} aht10_data_t;

extern rt_mailbox_t g_sensor_mb;

/* AHT10 采集与文件存储线程入口 */
static void aht10_thread_entry(void *parameter)
{
    aht10_device_t dev;
    int fd = -1;
    rt_uint32_t write_count = 0;
    char record_buf[128];

    /* 1. 初始化 AHT10 传感器 (绑定 I2C3 总线) */
    dev = aht10_init("i2c3");
    if (dev == RT_NULL)
    {
        LOG_E("Failed to initialize AHT10 sensor on i2c3!");
        return;
    }
    LOG_I("AHT10 sensor initialized successfully on i2c3.");

    /* 2. 打开或创建本地日志文件 (/fal/Data.txt) */
    fd = open("/fal/Data.txt", O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0)
    {
        LOG_W("Open /fal/Data.txt failed, fallback to /Data.txt");
        fd = open("/Data.txt", O_WRONLY | O_CREAT | O_APPEND);
    }

    while (1)
    {
        /* 采集温湿度 */
        float temp = aht10_read_temperature(dev);
        float humi = aht10_read_humidity(dev);

        /* 动态分配消息结构体传递给 MQTT 线程 */
        aht10_data_t *p_data = (aht10_data_t *)rt_malloc(sizeof(aht10_data_t));
        if (p_data != RT_NULL)
        {
            p_data->temperature = temp;
            p_data->humidity    = humi;
            p_data->timestamp   = rt_tick_get();

            if (rt_mb_send(g_sensor_mb, (rt_ubase_t)p_data) != RT_EOK)
            {
                LOG_W("Sensor mailbox full, dropping frame.");
                rt_free(p_data);
            }
        }

        /* 本地文件持久化写入 */
        if (fd >= 0)
        {
            int len = rt_snprintf(record_buf, sizeof(record_buf),
                                  "[Tick %u] Temp: %.2f C, Humi: %.2f %%\n",
                                  rt_tick_get(), temp, humi);
            write(fd, record_buf, len);
            write_count++;

            /* 每写入 10 条数据强制执行一次 fsync 落盘 */
            if (write_count % 10 == 0)
            {
                fsync(fd);
                LOG_D("Flushed 10 records to Flash filesystem via fsync.");
            }
        }

        LOG_I("[Sensor Sample] Temp: %.2f C, Humi: %.2f %%", temp, humi);
        rt_thread_mdelay(2000); // 采样周期 2 秒
    }

    if (fd >= 0)
    {
        close(fd);
    }
}

int aht10_app_init(void)
{
    rt_thread_t tid = rt_thread_create("t_aht10",
                                       aht10_thread_entry,
                                       RT_NULL,
                                       2048,
                                       18,
                                       10);
    if (tid == RT_NULL)
    {
        LOG_E("Failed to create AHT10 thread!");
        return -RT_ERROR;
    }
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(aht10_app_init, start AHT10 sampling and filesystem logger);
