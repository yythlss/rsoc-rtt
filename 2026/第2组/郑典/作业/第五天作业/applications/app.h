/*
 * Copyright (c) 2025
 *
 * 温湿度采集上云 + 文件系统 + 云控小灯 —— 公共接口声明
 *
 * 变更记录:
 * 2025        first version
 */

#ifndef __APP_H__
#define __APP_H__

#include <rtthread.h>

/* ===== 温湿度传感器 (ah10.c) ===== */

/* 初始化板载 AHT21（使用 aht10 软件包，软件 I2C3） */
int app_aht10_init(void);

/* 读取温湿度，成功返回 RT_EOK */
int app_aht10_read(float *temperature, float *humidity);

/* ===== LED (app_led.c) ===== */

void app_led_init(void);
void app_led_on(void);
void app_led_off(void);
void app_led_toggle(void);
rt_bool_t app_led_is_on(void);

/* ===== MQTT (app_mqtt.c) ===== */

/* 启动 MQTT 客户端线程 */
int app_mqtt_start(void);

/* 发布一条温湿度数据到云端（在客户端已连接时发送） */
int app_mqtt_publish_sensor(float temperature, float humidity, rt_uint32_t count);

/* ===== WiFi (main.c) ===== */

/* 连接 WiFi（带超时，非阻塞式返回） */
int app_wifi_connect(void);

#endif /* __APP_H__ */
