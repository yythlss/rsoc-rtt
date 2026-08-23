/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-21     yanran       the first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <cJSON.h>

#include "aht10.h"
#include "mqttclient.h"
#include <dfs_posix.h>

#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED

#define WIFI_CS     GET_PIN(F,10)
void WIFI_CS_PULL_UP(void)
{
    rt_pin_mode(WIFI_CS, PIN_MODE_OUTPUT);
    rt_pin_write(WIFI_CS, PIN_HIGH);
}
INIT_BOARD_EXPORT(WIFI_CS_PULL_UP);

/*********************************************************/
extern mqtt_client_t *client;
int aht10_entry(void *var)
{
    aht10_device_t aht10 = aht10_init("i2c3");
    if(aht10 == RT_NULL)
    {
        rt_kprintf("aht10 init fail\n");
        return -RT_ERROR;
    }

    rt_uint32_t count = 1;
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    char buf[64];
    float temp,humi;

    int fd;
    fd = open("/fal/Data.txt",O_RDWR | O_CREAT);
    while(1)
    {
        temp = aht10_read_temperature(aht10);
        humi = aht10_read_humidity(aht10);
        //rt_kprintf("temp = %.2f , humi = %.2f\n",temp,humi);
        //rt_kprintf("num %d\n",num);

        snprintf(buf, sizeof(buf), "temp:%.2f,humi:%.2f,count: %u\r\n", temp, humi,count);
        rt_kprintf("temp:%.2f,humi:%.2f,count: %u\n", temp, humi,count);

        msg.qos = QOS0;
        msg.payload = (void *)buf;
        mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);

        if(fd >= 0)
        {
            write(fd,(char *)buf,strlen(buf));
            if((count %10) == 0)
            {
                fsync(fd);
            }

            count++;

        }
        rt_thread_mdelay(500);
    }
    close(fd);
    return RT_EOK;
}

int aht10_thread(void)
{
    rt_thread_t AHT10_thread = rt_thread_create("aht10", aht10_entry, RT_NULL, 1024*5, 10, 10);
    if(AHT10_thread == RT_NULL)
    {
        rt_kprintf("aht10 thread create fail\n");
        return -RT_ERROR;
    }

    rt_thread_startup(AHT10_thread);
    return RT_EOK;
}

MSH_CMD_EXPORT(aht10_thread,aht10_thread);
/*************************************************************************/
void msg_to_led(message_data_t* message)
{

    if(strcmp(message->topic_name, "rtt-sub666") == 0)
    {

        rt_kprintf("topic ture\n");
        cJSON *root = cJSON_Parse((char*)message->message->payload);
        cJSON *msg = cJSON_GetObjectItem(root,"msg");

        if(msg && strcmp(msg->valuestring, "led on") == 0)
        {
            rt_pin_write(PIN_LED_R, PIN_LOW);
            rt_kprintf("led on\n");
        }
        else if(msg && strcmp(msg->valuestring, "led off") == 0)
        {
            rt_pin_write(PIN_LED_R, PIN_HIGH);
            rt_kprintf("led off\n");
        }
    }
    else
    {
        rt_kprintf("topic error\n");
    }
}

/*************************************************************************/





