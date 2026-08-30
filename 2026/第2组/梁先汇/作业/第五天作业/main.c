/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023-5-10      ShiHao       first version
 * 2026-8-23      梁先汇       DAY5: AHT10 上云 + 文件系统 + 云端控制 LED
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "sensor_asair_aht10.h"
#include "mqttclient.h"
#include <lwip/sockets.h>
#include <netdev.h>
#include <wlan_mgnt.h>
#include <dfs_posix.h>
#include <fal.h>

#define DBG_TAG "main"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

#define AHT10_I2C_BUS         "i2c3"              // AHT10 I2C总线设备名称

/* 配置 LED 灯引脚 */
#define PIN_LED_B              GET_PIN(F, 11)      // PF11 :  LED_B        --> LED
#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED

mqtt_client_t *mqtt_client_g;   // 全局

/* ============================== 主函数 ============================== */

int main(void)
{
    /* 任务2：挂载 font 分区（/fal 已由 INIT_APP_EXPORT 自动挂载 filesystem 分区） */
    fal_blk_device_create("font");
    mkdir("/fal/font", 0);                        // 挂载点目录（/fal 已挂载可写）
    dfs_mount("font", "/fal/font", "elm", 0, 0);

    aht10_read_init();     // 任务1+2：启动温湿度采集线程（上云 + 写 Data.txt）
    mqtt_task_init();      // 任务1+3：启动 MQTT 线程（上云 + 订阅控制 LED）

    while (1)
    {
        rt_thread_mdelay(1000);   // main 线程保活
    }
}

/* ============================== 任务1/2：AHT10 采集 ============================== */

void aht10_read_task(void *parameter)
{
    float Humi, Temp;
    static int count = 0;
    rt_device_open(rt_device_find("temp_aht10"), RT_DEVICE_FLAG_RDONLY);
    rt_device_open(rt_device_find("humi_aht10"), RT_DEVICE_FLAG_RDONLY);
    struct rt_sensor_data aht10_data;

    while (1)
    {
        rt_device_read(rt_device_find("temp_aht10"), 0, &aht10_data, 1);  // 写 data.temp
        Temp = aht10_data.data.temp / 10.0f;   // 立刻取出，防止被覆盖

        rt_device_read(rt_device_find("humi_aht10"), 0, &aht10_data, 1);  // 写 data.humi
        Humi = aht10_data.data.humi / 10.0f;

        /* 任务2：追加写入 Data.txt */
        char line[128];
        count++;
        rt_snprintf(line, sizeof(line),
                    "Temp:%.1f; Humi:%.1f; Count:%d\n", Temp, Humi, count);

        int fd = open("/fal/font/Data.txt", O_RDWR | O_CREAT | O_APPEND, 0);
        if (fd >= 0)
        {
            write(fd, line, rt_strlen(line));
            close(fd);
        }

        /* 任务1：组装 JSON 并发布到 EMQX */
        char buf[128];
        rt_snprintf(buf, sizeof(buf), "{\"temp\":%.1f,\"humi\":%.1f}", Temp, Humi);

        mqtt_message_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.qos = QOS0;
        msg.payloadlen = rt_strlen(buf);
        msg.payload = buf;
        if (mqtt_client_g != RT_NULL)
        {
            mqtt_publish(mqtt_client_g, KAWAII_MQTT_PUBTOPIC, &msg);
        }
        rt_thread_mdelay(3000);
    }
}

int aht10_read_init(void)
{
    struct rt_sensor_config cfg;
    cfg.intf.dev_name  = AHT10_I2C_BUS;
    cfg.intf.user_data = (void *)AHT10_I2C_ADDR;

    if (RT_EOK != rt_hw_aht10_init("aht10", &cfg))
    {
        LOG_E("aht10 init failed");
        return -RT_ERROR;
    }

    rt_thread_t ath10 = rt_thread_create("aht10_read", aht10_read_task, RT_NULL, 2048, 25, 10);
    if (ath10 == RT_NULL)
    {
        LOG_E("aht10_read thread create failed");
        return -RT_ERROR;
    }
    rt_thread_startup(ath10);
    return RT_EOK;

}
MSH_CMD_EXPORT(aht10_read_init, aht10_read_init);

/* ============================== 任务1/3：MQTT 任务 ============================== */

/* 订阅回调：云端下发指令到这里处理 */
static void mqtt_sub_callback(void *client, message_data_t *msg)
{
    /* 任务3：解析 payload 翻转 LED，注意 payload 可能无 '\0' 结尾，用 payloadlen */
    if (msg->message->payloadlen == 10 &&
        rt_strncmp((char *)msg->message->payload, "led_toggle", 10) == 0)
    {
        /* 翻转 LED */
        rt_pin_write(PIN_LED_B, !rt_pin_read(PIN_LED_B));
    }
    LOG_I("mqtt recv: topic=%s msg=%.*s", msg->topic_name,
          (int)msg->message->payloadlen, (char *)msg->message->payload);
}

static void mqtt_task_entry(void *parameter)
{
    struct netdev *dev = netdev_get_by_name(RT_WLAN_DEVICE_STA_NAME);
    int ret = -1;

    /* 等待网络就绪 */
    for (int i = 0; i < 30; i++)
    {
        if (dev && netdev_is_internet_up(dev))
            break;
        rt_thread_mdelay(1000);
    }

    mqtt_client_g = mqtt_lease();
    mqtt_set_host(mqtt_client_g, KAWAII_MQTT_HOST);
    mqtt_set_port(mqtt_client_g, KAWAII_MQTT_PORT);
    mqtt_set_client_id(mqtt_client_g, KAWAII_MQTT_CLIENTID);
    mqtt_set_user_name(mqtt_client_g, KAWAII_MQTT_USERNAME);
    mqtt_set_password(mqtt_client_g, KAWAII_MQTT_PASSWORD);
    mqtt_set_clean_session(mqtt_client_g, 1);

    /* 连接失败自动重试 */
    do
    {
        ret = mqtt_connect(mqtt_client_g);
        if (ret != 0)
        {
            LOG_E("mqtt connect failed: %d, retry...", ret);
            rt_thread_mdelay(5000);
        }
    } while (ret != 0);

    LOG_I("mqtt connected");
    mqtt_subscribe(mqtt_client_g, KAWAII_MQTT_SUBTOPIC, QOS0, mqtt_sub_callback);

    while (1)
    {
        rt_thread_mdelay(5000);
    }
}

int mqtt_task_init(void)
{
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_B, PIN_LOW);

    rt_thread_t tid = rt_thread_create("mqtt_task", mqtt_task_entry, RT_NULL,
                                       4096, 16, 10);
    if (tid == RT_NULL)
        return -RT_ERROR;
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(mqtt_task_init, mqtt_task init);
