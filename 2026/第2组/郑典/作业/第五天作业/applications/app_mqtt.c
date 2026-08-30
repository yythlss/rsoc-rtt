/*
 * Copyright (c) 2025
 *
 * MQTT 客户端（基于 kawaii-mqtt 软件包）
 *
 * 功能：
 *  1. 连接 EMQX（默认 broker.emqx.io:1883，无账号密码）；
 *  2. 周期性发布温湿度数据到主题 spark407/aht10/data；
 *  3. 订阅主题 spark407/led/cmd，接收云端指令（on/off/toggle）翻转红色 LED，
 *     并把状态回发到 spark407/led/status。
 *
 * 变更记录:
 * 2025        first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include <netdev_ipaddr.h>
#include <netdev.h>
#include <wlan_mgnt.h>
#include "mqttclient.h"
#include "app.h"

#define DBG_TAG "mqtt"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ================ 可修改配置 ================ */
#ifndef MQTT_HOST
#define MQTT_HOST        "broker.emqx.io"    /* EMQX 公共服务器 */
#endif
#ifndef MQTT_PORT
#define MQTT_PORT        "1883"
#endif
#ifndef MQTT_CLIENTID
#define MQTT_CLIENTID    "rtt-spark-407"     /* 公共服务器上建议改成唯一 ID */
#endif
#ifndef MQTT_USERNAME
#define MQTT_USERNAME    ""
#endif
#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD    ""
#endif

/* 主题定义（公共 broker 上加前缀避免与他人冲突） */
#define TOPIC_PUB_DATA    "spark407/aht10/data"
#define TOPIC_SUB_LED     "spark407/led/cmd"
#define TOPIC_PUB_STATUS  "spark407/led/status"

/* ================ 内部变量 ================ */
static mqtt_client_t *client = RT_NULL;

/* 去掉字符串首尾空白与换行（就地修改） */
static char *trim(char *s)
{
    char *end;

    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')
    {
        s++;
    }

    if (*s == '\0')
    {
        return s;
    }

    end = s + rt_strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
    {
        *end-- = '\0';
    }

    return s;
}

/* 忽略大小写比较 */
static rt_bool_t str_equals_ignore_case(const char *a, const char *b)
{
    while (*a && *b)
    {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return RT_FALSE;
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

/* 回发 LED 状态 */
static void publish_led_status(void)
{
    mqtt_message_t msg;
    const char *status;

    if (client == RT_NULL || client->mqtt_client_state != CLIENT_STATE_CONNECTED)
    {
        return;
    }

    status = app_led_is_on() ? "on" : "off";

    rt_memset(&msg, 0, sizeof(msg));
    msg.qos = QOS1;
    msg.payload = (void *)status;
    msg.payloadlen = rt_strlen(status);

    mqtt_publish(client, TOPIC_PUB_STATUS, &msg);
}

/* 订阅主题的回调：收到云端 LED 指令 */
static void led_cmd_handler(void *c, message_data_t *msg)
{
    char cmd[16];
    size_t n;

    (void)c;

    if (msg == RT_NULL || msg->message == RT_NULL || msg->message->payload == RT_NULL)
    {
        return;
    }

    /* MQTT payload 不保证以 '\0' 结尾，先拷贝到局部缓冲区并补 '\0' */
    n = msg->message->payloadlen;
    if (n >= sizeof(cmd))
    {
        n = sizeof(cmd) - 1;
    }
    rt_memset(cmd, 0, sizeof(cmd));
    rt_memcpy(cmd, msg->message->payload, n);
    cmd[n] = '\0';
    trim(cmd);

    LOG_I("recv LED cmd: topic=%s payload=%s", msg->topic_name, cmd);

    if (str_equals_ignore_case(cmd, "on"))
    {
        app_led_on();
    }
    else if (str_equals_ignore_case(cmd, "off"))
    {
        app_led_off();
    }
    else if (str_equals_ignore_case(cmd, "toggle") || str_equals_ignore_case(cmd, "t"))
    {
        app_led_toggle();
    }
    else
    {
        LOG_W("unknown LED cmd: %s (support: on/off/toggle)", cmd);
    }

    publish_led_status();
}

/* 等待 WiFi 已连接且拿到 IP（最多等待 timeout_ms） */
static int wait_network_ready(rt_uint32_t timeout_ms)
{
    rt_uint32_t waited = 0;
    const rt_uint32_t step = 500;

    while (waited < timeout_ms)
    {
        if (rt_wlan_is_connected() == RT_TRUE &&
            netdev_default != RT_NULL && netdev_is_up(netdev_default))
        {
            return RT_EOK;
        }
        rt_thread_mdelay(step);
        waited += step;
    }

    return -RT_ERROR;
}

/* MQTT 客户端线程 */
static void mqtt_thread_entry(void *parameter)
{
    (void)parameter;

    mqtt_log_init();

    client = mqtt_lease();
    if (client == RT_NULL)
    {
        LOG_E("mqtt_lease() failed");
        return;
    }

    mqtt_set_host(client, MQTT_HOST);
    mqtt_set_port(client, MQTT_PORT);
    mqtt_set_user_name(client, MQTT_USERNAME);
    mqtt_set_password(client, MQTT_PASSWORD);
    mqtt_set_client_id(client, MQTT_CLIENTID);
    mqtt_set_clean_session(client, 1);

    while (1)
    {
        /* 首次连接成功后，kawaii-mqtt 内部线程会接管
         * keepalive、断线重连与重订阅，这里只需空闲等待 */
        if (client->mqtt_thread != RT_NULL)
        {
            rt_thread_mdelay(1000);
            continue;
        }

        /* 尚未连接成功过：等待网络就绪后重试首次连接 */
        if (wait_network_ready(3000) != RT_EOK)
        {
            rt_thread_mdelay(1000);
            continue;
        }

        LOG_I("connecting to MQTT broker %s:%s ...", MQTT_HOST, MQTT_PORT);
        if (mqtt_connect(client) == KAWAII_MQTT_SUCCESS_ERROR)
        {
            LOG_I("MQTT connected, subscribe topic: %s", TOPIC_SUB_LED);
            if (mqtt_subscribe(client, TOPIC_SUB_LED, QOS1, led_cmd_handler) != KAWAII_MQTT_SUCCESS_ERROR)
            {
                LOG_W("subscribe topic %s failed", TOPIC_SUB_LED);
            }
        }
        else
        {
            LOG_W("MQTT connect failed, retry after 3s");
            rt_thread_mdelay(3000);
        }
    }
}

/* 启动 MQTT 客户端线程 */
int app_mqtt_start(void)
{
    rt_thread_t tid = rt_thread_create("appmqtt", mqtt_thread_entry, RT_NULL, 2048, 16, 20);
    if (tid == RT_NULL)
    {
        LOG_E("create mqtt thread failed");
        return -RT_ERROR;
    }

    rt_thread_startup(tid);
    return RT_EOK;
}

/* 发布一条温湿度数据（客户端已连接时才发送） */
int app_mqtt_publish_sensor(float temperature, float humidity, rt_uint32_t count)
{
    mqtt_message_t msg;
    char payload[128];

    if (client == RT_NULL || client->mqtt_client_state != CLIENT_STATE_CONNECTED)
    {
        return -RT_ERROR;
    }

    rt_snprintf(payload, sizeof(payload),
                "Temp:%.1f;Humi:%.1f;Count:%u",
                temperature, humidity, (unsigned int)count);

    rt_memset(&msg, 0, sizeof(msg));
    msg.qos = QOS1;
    msg.payload = payload;
    msg.payloadlen = rt_strlen(payload);

    return mqtt_publish(client, TOPIC_PUB_DATA, &msg);
}
