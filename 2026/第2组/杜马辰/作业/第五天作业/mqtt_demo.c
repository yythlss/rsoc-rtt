/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     DuMachen     DAY5 MQTT client publish and cloud control
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "mqttclient.h"
#include "cJSON.h"

#define DBG_TAG "mqtt_app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define PIN_LED_R       GET_PIN(F, 12)

typedef struct
{
    float temperature;
    float humidity;
    rt_tick_t timestamp;
} aht10_data_t;

extern rt_mailbox_t g_sensor_mb;

/* MQTT Broker 与主题参数定义 */
#ifndef MQTT_SERVER_HOST
#define MQTT_SERVER_HOST        "broker.emqx.io"
#endif

#ifndef MQTT_SERVER_PORT
#define MQTT_SERVER_PORT        "1883"
#endif

#ifndef MQTT_CLIENT_ID
#define MQTT_CLIENT_ID          "dumachen_spark_01"
#endif

#define MQTT_PUB_TOPIC          "rtt/sensor/dumachen"
#define MQTT_SUB_TOPIC          "rtt/control/dumachen"

/* 下行控制消息订阅回调函数 */
static void on_mqtt_sub_callback(void *client, message_data_t *msg)
{
    (void)client;
    char payload_str[128];
    rt_size_t len = msg->message->payloadlen < (sizeof(payload_str) - 1) ?
                    msg->message->payloadlen : (sizeof(payload_str) - 1);

    rt_memcpy(payload_str, msg->message->payload, len);
    payload_str[len] = '\0';

    LOG_I(">> Received MQTT Message on Topic [%s]: %s", msg->topic_name, payload_str);

    /* 使用 cJSON 解析云端下行指令 */
    cJSON *root = cJSON_Parse(payload_str);
    if (root != RT_NULL)
    {
        cJSON *cmd_item = cJSON_GetObjectItem(root, "msg");
        if (cmd_item && cmd_item->valuestring)
        {
            if (rt_strcmp(cmd_item->valuestring, "led on") == 0)
            {
                rt_pin_write(PIN_LED_R, PIN_LOW);
                LOG_W(">> Cloud Command Executed: LED Turned ON <<");
            }
            else if (rt_strcmp(cmd_item->valuestring, "led off") == 0)
            {
                rt_pin_write(PIN_LED_R, PIN_HIGH);
                LOG_W(">> Cloud Command Executed: LED Turned OFF <<");
            }
        }
        cJSON_Delete(root);
    }
}

/* MQTT 客户端工作主线程 */
static void mqtt_thread_entry(void *parameter)
{
    mqtt_client_t *client = RT_NULL;
    rt_ubase_t mb_val;
    char pub_buf[128];

    /* 等待网络连接就绪 */
    rt_thread_mdelay(4000);
    mqtt_log_init();

    client = mqtt_lease();
    if (client == RT_NULL)
    {
        LOG_E("Failed to allocate MQTT client instance!");
        return;
    }

    mqtt_set_host(client, MQTT_SERVER_HOST);
    mqtt_set_port(client, MQTT_SERVER_PORT);
    mqtt_set_client_id(client, MQTT_CLIENT_ID);
    mqtt_set_clean_session(client, 1);

    LOG_I("Connecting to MQTT Broker: %s:%s (ClientID: %s)...",
          MQTT_SERVER_HOST, MQTT_SERVER_PORT, MQTT_CLIENT_ID);

    if (mqtt_connect(client) != 0)
    {
        LOG_E("MQTT connect failed!");
        return;
    }
    LOG_I("MQTT Connected successfully!");

    /* 订阅云端下行控制主题 */
    mqtt_subscribe(client, MQTT_SUB_TOPIC, QOS0, on_mqtt_sub_callback);
    LOG_I("Subscribed to control topic: %s", MQTT_SUB_TOPIC);

    while (1)
    {
        /* 从邮箱阻塞等待接收 AHT10 传感器新数据 */
        if (rt_mb_recv(g_sensor_mb, &mb_val, rt_tick_from_millisecond(1000)) == RT_EOK)
        {
            aht10_data_t *p_data = (aht10_data_t *)mb_val;

            /* 打包为 JSON 格式有效载荷 */
            int len = rt_snprintf(pub_buf, sizeof(pub_buf),
                                  "{\"client_id\":\"%s\",\"temp\":%.2f,\"humi\":%.2f,\"tick\":%u}",
                                  MQTT_CLIENT_ID, p_data->temperature, p_data->humidity, p_data->timestamp);

            mqtt_message_t pub_msg;
            rt_memset(&pub_msg, 0, sizeof(pub_msg));
            pub_msg.qos        = QOS0;
            pub_msg.payload    = (void *)pub_buf;
            pub_msg.payloadlen = len;

            mqtt_publish(client, MQTT_PUB_TOPIC, &pub_msg);
            LOG_I("Published Sensor Data -> [%s]: %s", MQTT_PUB_TOPIC, pub_buf);

            /* 释放动态分配的消息内存 */
            rt_free(p_data);
        }

        mqtt_sleep_ms(500);
    }
}

int mqtt_app_init(void)
{
    rt_thread_t tid = rt_thread_create("t_mqtt",
                                       mqtt_thread_entry,
                                       RT_NULL,
                                       3072,
                                       16,
                                       10);
    if (tid == RT_NULL)
    {
        LOG_E("Failed to create MQTT client thread!");
        return -RT_ERROR;
    }
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(mqtt_app_init, start MQTT client demo application);
