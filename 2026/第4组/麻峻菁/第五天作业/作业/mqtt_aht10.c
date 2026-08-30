/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-23     RT-Thread    first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <wlan_mgnt.h>
#include "aht10.h"
#include "aht10_data.h"
#include "mqttclient.h"

/* Wi-Fi 参数，请修改成自己的热点名称和密码 */
#define WIFI_SSID            ""
#define WIFI_PASSWORD        ""

/* MQTT 参数，公共 EMQX 的 1883 端口允许匿名连接 */
#define MQTT_URL             ""
#define MQTT_PORT            ""
#define MQTT_CLIENT_ID       ""
#define MQTT_USERNAME        ""
#define MQTT_PASSWORD        ""
#define MQTT_PUBLISH_TOPIC   "rtthread/aht10/data"

/* 传感器、线程和上传参数 */
#define AHT10_I2C_BUS_NAME   "i2c3"
#define THREAD_PRIORITY      20
#define THREAD_STACK_SIZE    4096
#define THREAD_TIMESLICE     10
#define WIFI_RETRY_COUNT     5
#define UPLOAD_PERIOD_MS     5000

/* 线程句柄用于防止重复启动 */
static rt_thread_t aht10_mqtt_thread = RT_NULL;

/**
 * @brief 连接 Wi-Fi 并等待 DHCP 分配网络地址。
 *
 * @return RT_EOK 网络连接成功，-RT_ERROR 网络连接失败。
 */
static rt_err_t wifi_connect(void)
{
    rt_err_t result;
    int retry;
    int wait_count;

    if (rt_wlan_is_ready() == RT_TRUE)
    {
        return RT_EOK;
    }

    for (retry = 1; retry <= WIFI_RETRY_COUNT; retry++)
    {
        rt_kprintf("connect Wi-Fi %s, attempt %d/%d.\n",
                   WIFI_SSID, retry, WIFI_RETRY_COUNT);
        result = rt_wlan_connect(WIFI_SSID, WIFI_PASSWORD);

        if (result == RT_EOK)
        {
            /* 最多等待 10 秒，直到无线网络获得 IP 地址 */
            for (wait_count = 0; wait_count < 50; wait_count++)
            {
                if (rt_wlan_is_ready() == RT_TRUE)
                {
                    rt_kprintf("Wi-Fi connected.\n");
                    return RT_EOK;
                }
                rt_thread_mdelay(200);
            }
        }

        rt_kprintf("Wi-Fi connect failed, error = %d.\n", result);
        rt_thread_mdelay(2000);
    }

    return -RT_ERROR;
}

/**
 * @brief 创建并连接 MQTT 客户端。
 *
 * @return MQTT 客户端句柄，失败时返回 RT_NULL。
 */
static mqtt_client_t *mqtt_client_connect(void)
{
    mqtt_client_t *client;
    int result;

    mqtt_log_init();
    client = mqtt_lease();
    if (client == RT_NULL)
    {
        rt_kprintf("create MQTT client failed.\n");
        return RT_NULL;
    }

    mqtt_set_host(client, MQTT_URL);
    mqtt_set_port(client, MQTT_PORT);
    mqtt_set_client_id(client, MQTT_CLIENT_ID);
    mqtt_set_clean_session(client, 1);

    /* 公共 Broker 无需认证，自建服务器可填写这两个宏 */
    if (MQTT_USERNAME[0] != '\0')
    {
        mqtt_set_user_name(client, MQTT_USERNAME);
    }
    if (MQTT_PASSWORD[0] != '\0')
    {
        mqtt_set_password(client, MQTT_PASSWORD);
    }

    result = mqtt_connect(client);
    if (result != KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("connect MQTT server failed, error = %d.\n", result);
        return RT_NULL;
    }

    rt_kprintf("MQTT connected: %s:%s, client id: %s.\n",
               MQTT_URL, MQTT_PORT, MQTT_CLIENT_ID);
    return client;
}

/**
 * @brief 采集一次温湿度并发布为 JSON 数据。
 *
 * 数据格式为 {"temperature":25.3,"humidity":61.8}。
 *
 * @param client MQTT 客户端句柄。
 * @param sensor AHT10 设备句柄。
 *
 * @return 0 发布成功，其他值表示失败。
 */
static int sensor_data_publish(mqtt_client_t *client, aht10_device_t sensor)
{
    mqtt_message_t message;
    char payload[96];
    int temperature_x10;
    int humidity_x10;
    int temperature_decimal;
    int humidity_decimal;
    int result;

    /*
     * 将浮点数据扩大 10 倍转成整数，
     * 避免嵌入式 C 库没有开启浮点格式化。
     */
    humidity_x10 = (int)(aht10_read_humidity(sensor) * 10.0f);
    temperature_x10 = (int)(aht10_read_temperature(sensor) * 10.0f);

    /* 通知其他应用模块已经完成一次采集 */
    aht10_data_notify(temperature_x10, humidity_x10);

    humidity_decimal = humidity_x10 % 10;
    temperature_decimal = temperature_x10 % 10;

    if (humidity_decimal < 0)
    {
        humidity_decimal = -humidity_decimal;
    }
    if (temperature_decimal < 0)
    {
        temperature_decimal = -temperature_decimal;
    }

    rt_snprintf(payload, sizeof(payload),
                "{%ctemperature%c:%d.%d,%chumidity%c:%d.%d}",
                34, 34, temperature_x10 / 10, temperature_decimal,
                34, 34, humidity_x10 / 10, humidity_decimal);

    rt_memset(&message, 0, sizeof(message));
    message.qos = QOS0;
    message.payload = payload;
    message.payloadlen = rt_strlen(payload);
    result = mqtt_publish(client, MQTT_PUBLISH_TOPIC, &message);

    if (result == KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("publish success: %s\n", payload);
    }
    else
    {
        rt_kprintf("publish failed, error = %d.\n", result);
    }

    return result;
}

/**
 * @brief 温湿度采集与 MQTT 上传线程入口。
 *
 * 依次连接 Wi-Fi、初始化 AHT10、连接 EMQX，
 * 然后每隔 5 秒采集并上传一次数据。
 *
 * @param parameter 保留参数，当前未使用。
 */
static void aht10_mqtt_thread_entry(void *parameter)
{
    aht10_device_t sensor = RT_NULL;
    mqtt_client_t *client;

    RT_UNUSED(parameter);

    if (wifi_connect() != RT_EOK)
    {
        rt_kprintf("network is not ready, demo stopped.\n");
        goto exit;
    }

    sensor = aht10_init(AHT10_I2C_BUS_NAME);
    if (sensor == RT_NULL)
    {
        rt_kprintf("initialize AHT10 on %s failed.\n",
                   AHT10_I2C_BUS_NAME);
        goto exit;
    }

    rt_kprintf("AHT10 initialized on %s.\n", AHT10_I2C_BUS_NAME);
    client = mqtt_client_connect();
    if (client == RT_NULL)
    {
        goto exit;
    }

    while (1)
    {
        sensor_data_publish(client, sensor);
        rt_thread_mdelay(UPLOAD_PERIOD_MS);
    }

exit:
    if (sensor != RT_NULL)
    {
        aht10_deinit(sensor);
    }
    aht10_mqtt_thread = RT_NULL;
}

/**
 * @brief 启动 AHT10 温湿度采集与 MQTT 上传实验。
 *
 * @return RT_EOK 线程启动成功。
 * @return -RT_EBUSY 实验线程已经在运行。
 * @return -RT_ERROR 线程创建或启动失败。
 */
int aht10_mqtt_demo(void)
{
    rt_err_t result;

    if (aht10_mqtt_thread != RT_NULL)
    {
        rt_kprintf("AHT10 MQTT demo is already running.\n");
        return -RT_EBUSY;
    }

    aht10_mqtt_thread = rt_thread_create(
        "ahtmqtt",
        aht10_mqtt_thread_entry,
        RT_NULL,
        THREAD_STACK_SIZE,
        THREAD_PRIORITY,
        THREAD_TIMESLICE);

    if (aht10_mqtt_thread == RT_NULL)
    {
        rt_kprintf("create AHT10 MQTT thread failed.\n");
        return -RT_ERROR;
    }

    result = rt_thread_startup(aht10_mqtt_thread);
    if (result != RT_EOK)
    {
        rt_kprintf("startup AHT10 MQTT thread failed, error = %d.\n",
                   result);
        rt_thread_delete(aht10_mqtt_thread);
        aht10_mqtt_thread = RT_NULL;
        return -RT_ERROR;
    }

    return RT_EOK;
}


/* 导出为 MSH 命令 */
MSH_CMD_EXPORT(aht10_mqtt_demo, collect AHT10 data and publish to EMQX);
