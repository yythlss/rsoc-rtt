/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @Description: Kawaii MQTT day5 application for AHT10, FatFS and LED control.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "mqttclient.h"
#include "aht10.h"
#include "day5_storage.h"

#ifndef KAWAII_MQTT_HOST
#define KAWAII_MQTT_HOST       "jiejie01.top"
#endif
#ifndef KAWAII_MQTT_PORT
#define KAWAII_MQTT_PORT       "1883"
#endif
#ifndef KAWAII_MQTT_CLIENTID
#define KAWAII_MQTT_CLIENTID   "rtthread001"
#endif
#ifndef KAWAII_MQTT_USERNAME
#define KAWAII_MQTT_USERNAME   "rt-thread"
#endif
#ifndef KAWAII_MQTT_PASSWORD
#define KAWAII_MQTT_PASSWORD   "rt-thread"
#endif
#ifndef KAWAII_MQTT_SUBTOPIC
#define KAWAII_MQTT_SUBTOPIC   "rtt-sub"
#endif
#ifndef KAWAII_MQTT_PUBTOPIC
#define KAWAII_MQTT_PUBTOPIC   "rtt-pub"
#endif

#define AHT10_I2C_BUS_NAME "i2c3"
#define LED_R_PIN          GET_PIN(F, 12)
#define SAMPLE_PERIOD_MS   5000

static rt_thread_t mqtt_thread = RT_NULL;
static rt_bool_t led_on = RT_FALSE;

static void sub_topic_handle1(void *client, message_data_t *msg)
{
    const char *payload;
    size_t payload_len;

    (void)client;
    if (msg == RT_NULL || msg->message == RT_NULL || msg->message->payload == RT_NULL)
    {
        return;
    }

    payload = (const char *)msg->message->payload;
    payload_len = msg->message->payloadlen;
    rt_kprintf("[MQTT] command topic=%s payload=%.*s\n",
               msg->topic_name, (int)payload_len, payload);

    if (payload_len == 6 && memcmp(payload, "toggle", 6) == 0)
    {
        led_on = !led_on;
        rt_pin_write(LED_R_PIN, led_on ? PIN_LOW : PIN_HIGH);
        rt_kprintf("[MQTT] LED_R toggled -> %s\n", led_on ? "ON" : "OFF");
    }
    else
    {
        rt_kprintf("[MQTT] unsupported command, send plain text: toggle\n");
    }
}

static int mqtt_publish_weather(mqtt_client_t *client,
                                float temp,
                                float humi,
                                rt_uint32_t count)
{
    char payload[128];
    int temp100 = (int)(temp * 100.0f + (temp >= 0.0f ? 0.5f : -0.5f));
    int humi100 = (int)(humi * 100.0f + 0.5f);
    int temp_abs = temp100 < 0 ? -temp100 : temp100;
    int payload_len;
    mqtt_message_t msg;

    payload_len = rt_snprintf(payload, sizeof(payload),
                              "{\"temp\":%s%d.%02d,\"humi\":%d.%02d,\"count\":%lu}",
                              temp100 < 0 ? "-" : "",
                              temp_abs / 100, temp_abs % 100,
                              humi100 / 100, humi100 % 100,
                              (unsigned long)count);
    if (payload_len <= 0 || payload_len >= (int)sizeof(payload))
    {
        return -RT_ERROR;
    }

    memset(&msg, 0, sizeof(msg));
    msg.qos = QOS0;
    msg.payload = payload;
    msg.payloadlen = (size_t)payload_len;

    if (mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg) != KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("[MQTT] publish failed\n");
        return -RT_ERROR;
    }

    rt_kprintf("[MQTT] published topic=%s payload=%s\n",
               KAWAII_MQTT_PUBTOPIC, payload);
    return RT_EOK;
}

static rt_bool_t aht10_value_is_valid(float temp, float humi)
{
    return temp >= -40.0f && temp <= 85.0f && humi >= 0.0f && humi <= 100.0f;
}

static void kawaii_mqtt_demo(void *parameter)
{
    mqtt_client_t *client;
    aht10_device_t aht10;
    rt_uint32_t sample_count = 0;

    (void)parameter;
    rt_thread_mdelay(6000);

    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, PIN_HIGH);

    aht10 = aht10_init(AHT10_I2C_BUS_NAME);
    if (aht10 == RT_NULL)
    {
        rt_kprintf("[AHT10] init failed on %s\n", AHT10_I2C_BUS_NAME);
        mqtt_thread = RT_NULL;
        return;
    }
    rt_kprintf("[AHT10] init success on %s\n", AHT10_I2C_BUS_NAME);

    if (day5_storage_init() != RT_EOK)
    {
        rt_kprintf("[FS] initialization failed, it will retry when saving\n");
    }

    mqtt_log_init();
    client = mqtt_lease();
    if (client == RT_NULL)
    {
        rt_kprintf("[MQTT] client allocation failed\n");
        mqtt_thread = RT_NULL;
        return;
    }

    mqtt_set_host(client, KAWAII_MQTT_HOST);
    mqtt_set_port(client, KAWAII_MQTT_PORT);
    mqtt_set_user_name(client, KAWAII_MQTT_USERNAME);
    mqtt_set_password(client, KAWAII_MQTT_PASSWORD);
    mqtt_set_client_id(client, KAWAII_MQTT_CLIENTID);
    mqtt_set_clean_session(client, 1);

    while (mqtt_connect(client) != KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("[MQTT] connect failed, retry in 3 seconds\n");
        mqtt_sleep_ms(3000);
    }
    rt_kprintf("[MQTT] connected to %s:%s\n", KAWAII_MQTT_HOST, KAWAII_MQTT_PORT);

    if (mqtt_subscribe(client, KAWAII_MQTT_SUBTOPIC, QOS0, sub_topic_handle1) !=
        KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("[MQTT] subscribe failed: %s\n", KAWAII_MQTT_SUBTOPIC);
    }
    else
    {
        rt_kprintf("[MQTT] subscribed: %s\n", KAWAII_MQTT_SUBTOPIC);
    }

    while (1)
    {
        float temp = aht10_read_temperature(aht10);
        float humi = aht10_read_humidity(aht10);

        if (!aht10_value_is_valid(temp, humi))
        {
            rt_kprintf("[AHT10] invalid data: temp=%.2f humi=%.2f\n", temp, humi);
            mqtt_sleep_ms(SAMPLE_PERIOD_MS);
            continue;
        }

        sample_count++;
        rt_kprintf("[AHT10] Temp: %.2f C ; Humi: %.2f %% ; Count: %lu\n",
                   temp, humi, (unsigned long)sample_count);
        mqtt_publish_weather(client, temp, humi, sample_count);
        day5_storage_append(temp, humi, sample_count);
        mqtt_sleep_ms(SAMPLE_PERIOD_MS);
    }
}

int ka_mqtt(void)
{
    if (mqtt_thread != RT_NULL)
    {
        rt_kprintf("[MQTT] day5 application is already running\n");
        return -RT_EBUSY;
    }

    mqtt_thread = rt_thread_create("day5_iot", kawaii_mqtt_demo, RT_NULL,
                                   4096, 17, 10);
    if (mqtt_thread == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_thread_startup(mqtt_thread);
    return RT_EOK;
}
MSH_CMD_EXPORT(ka_mqtt, start AHT10 MQTT filesystem and LED application);