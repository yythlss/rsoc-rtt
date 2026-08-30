/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     DuMachen     DAY5 comprehensive IoT application entry
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <dfs_posix.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 板载 LED 引脚 */
#define PIN_LED_R       GET_PIN(F, 12)

/* 全局邮箱句柄，用于 AHT10 采集线程与 MQTT 发布线程间传递温湿度数据 */
rt_mailbox_t g_sensor_mb = RT_NULL;

int main(void)
{
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);

    LOG_I("System boot complete. STM32F407 Spark-1 IoT node running.");
    return RT_EOK;
}

/* 综合应用初始化命令 */
int app_init(void)
{
    extern int aht10_app_init(void);
    extern int mqtt_app_init(void);

    /* 1. 创建传感器数据传输邮箱 (容量 16) */
    g_sensor_mb = rt_mb_create("sens_mb", 16, RT_IPC_FLAG_PRIO);
    if (g_sensor_mb == RT_NULL)
    {
        LOG_E("Failed to create sensor mailbox!");
        return -RT_ERROR;
    }

    /* 2. 启动 MQTT 客户端连接与发布线程 */
    if (mqtt_app_init() != RT_EOK)
    {
        LOG_E("Failed to start MQTT service!");
        return -RT_ERROR;
    }

    /* 3. 启动 AHT10 传感器采集与文件落盘线程 */
    if (aht10_app_init() != RT_EOK)
    {
        LOG_E("Failed to start AHT10 service!");
        return -RT_ERROR;
    }

    LOG_I("IoT Comprehensive App initialized successfully.");
    return RT_EOK;
}
MSH_CMD_EXPORT(app_init, start comprehensive IoT sensor & MQTT application);
