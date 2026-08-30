#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include "mqttclient.h"

#define DBG_TAG "mqtt_led"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define LED_PIN         GET_PIN(F, 11)
#define MQTT_HOST       "broker.emqx.io"
#define MQTT_PORT       "1883"
#define MQTT_CLIENT_ID  "spark1_001_led"
#define MQTT_TOPIC_DOWN "rsoc/spark1_001/device/down"

#define THREAD_STACK_SIZE 2048
#define THREAD_PRIORITY   20
#define THREAD_TIMESLICE  10

static mqtt_client_t *g_mqtt = RT_NULL;
static rt_thread_t g_thread = RT_NULL;

static void led_set(rt_bool_t on)
{
    rt_pin_write(LED_PIN, on ? PIN_LOW : PIN_HIGH);
}

static void mqtt_down_cb(void *client, message_data_t *msg)
{
    char buf[32] = {0};
    size_t len = msg->message->payloadlen;

    (void)client;

    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;

    memcpy(buf, msg->message->payload, len);
    buf[len] = '\0';

    rt_kprintf("recv topic: %s\n", msg->topic_name);
    rt_kprintf("recv payload: %s\n", buf);

    if (strstr(buf, "toggle") != RT_NULL)
    {
        rt_pin_write(LED_PIN, !rt_pin_read(LED_PIN));
    }
    else if (strstr(buf, "on") != RT_NULL)
    {
        led_set(RT_TRUE);
    }
    else if (strstr(buf, "off") != RT_NULL)
    {
        led_set(RT_FALSE);
    }
}

static int mqtt_connect_emqx(void)
{
    g_mqtt = mqtt_lease();
    if (g_mqtt == RT_NULL)
        return -1;

    mqtt_set_host(g_mqtt, MQTT_HOST);
    mqtt_set_port(g_mqtt, MQTT_PORT);
    mqtt_set_client_id(g_mqtt, MQTT_CLIENT_ID);
    mqtt_set_clean_session(g_mqtt, 1);
    mqtt_set_version(g_mqtt, 4);
    mqtt_set_keep_alive_interval(g_mqtt, 60);
    mqtt_set_cmd_timeout(g_mqtt, 4000);
    mqtt_set_read_buf_size(g_mqtt, 1024);
    mqtt_set_write_buf_size(g_mqtt, 1024);

    if (mqtt_connect(g_mqtt) != 0)
    {
        rt_kprintf("mqtt connect failed\n");
        mqtt_release(g_mqtt);
        g_mqtt = RT_NULL;
        return -2;
    }

    if (mqtt_subscribe(g_mqtt, MQTT_TOPIC_DOWN, QOS0, mqtt_down_cb) != 0)
    {
        rt_kprintf("mqtt subscribe failed\n");
    }

    rt_kprintf("mqtt connected\n");
    return 0;
}

static void mqtt_led_entry(void *parameter)
{
    (void)parameter;

    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    led_set(RT_FALSE);

    while (mqtt_connect_emqx() != 0)
    {
        rt_thread_mdelay(3000);
    }

    while (1)
    {
        mqtt_keep_alive(g_mqtt);
        rt_thread_mdelay(1000);
    }
}

int mqtt_led_start(void)
{
    g_thread = rt_thread_create("mqttled",
                                mqtt_led_entry,
                                RT_NULL,
                                THREAD_STACK_SIZE,
                                THREAD_PRIORITY,
                                THREAD_TIMESLICE);
    if (g_thread == RT_NULL)
    {
        rt_kprintf("thread create failed\n");
        return -RT_ERROR;
    }

    rt_thread_startup(g_thread);
    return RT_EOK;
}

MSH_CMD_EXPORT(mqtt_led_start, start MQTT LED control);
