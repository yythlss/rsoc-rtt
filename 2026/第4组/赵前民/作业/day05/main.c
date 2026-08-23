#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "mqttclient.h"
#include <aht10.h>
#include <dfs_posix.h>

#define LED_R_PIN       GET_PIN(F,12)

/* ===================== KAWAII MQTT 配置 ===================== */
#define KAWAII_MQTT_HOST               "broker.emqx.io"
#define KAWAII_MQTT_PORT               "1883"
#define KAWAII_MQTT_CLIENTID           "loongDev"
#define KAWAII_MQTT_USERNAME           "rt-thread"
#define KAWAII_MQTT_PASSWORD           "rt-thread"
#define KAWAII_MQTT_SUBTOPIC           "rtt-sub"
#define KAWAII_MQTT_PUBTOPIC           "rtt-pub"

static struct aht10_dev *aht10_dev = RT_NULL;

static void sub_topic_handle1(void* client, message_data_t* msg)
{
    const char *payload = (const char *)msg->message->payload;
    KAWAII_MQTT_LOG_I("recv topic=%s : msg=%s\n", msg->topic_name, payload);
    if (strstr(payload, "led_toggle") != RT_NULL)
    {
        int led_state = rt_pin_read(LED_R_PIN);
        rt_pin_write(LED_R_PIN, led_state ? PIN_LOW : PIN_HIGH);
        rt_kprintf("LED toggle -> %s\n", led_state ? "ON" : "OFF");

    }
}

/* MQTT发布封装 */
static int mqtt_publish_handle1(mqtt_client_t *client, const char *payload)
{
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.qos = QOS0;
    msg.payload = (void *)payload;
    msg.payloadlen = strlen(payload);
    return mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);
}

/* 保存温湿度数据到文件 */
static void save_data_to_file(float temp, float humi)
{
    char buf[128];
    int fd = open("/fal/Data.txt", O_CREAT | O_RDWR | O_TRUNC);
    if (fd >= 0)
    {
        write(fd, buf, rt_strlen(buf));
        close(fd);
    }
}

static void kawaii_mqtt_demo(void *parameter)
{
    mqtt_client_t *client = NULL;
    char pub_buf[128];
    float temp, humi;
    int sample_count = 0;
    rt_thread_delay(6000);

    aht10_dev = aht10_init("i2c3");
    if (aht10_dev == RT_NULL)
    {
        rt_kprintf("aht10 create failed!\n");
        return;
    }

    mqtt_log_init();
    client = mqtt_lease();
    mqtt_set_host(client, KAWAII_MQTT_HOST);
    mqtt_set_port(client, KAWAII_MQTT_PORT);
    mqtt_set_user_name(client, KAWAII_MQTT_USERNAME);
    mqtt_set_password(client, KAWAII_MQTT_PASSWORD);
    mqtt_set_client_id(client, KAWAII_MQTT_CLIENTID);
    mqtt_set_clean_session(client, 1);
    KAWAII_MQTT_LOG_I("The ID of the Kawaii client is: %s ", KAWAII_MQTT_CLIENTID);

    mqtt_connect(client);
    mqtt_subscribe(client, KAWAII_MQTT_SUBTOPIC, QOS0, sub_topic_handle1);

    while (1)
    {
        temp = aht10_read_temperature(aht10_dev);
        humi = aht10_read_humidity(aht10_dev);
        sample_count++;
        rt_kprintf("Temp:%.2f C, Humi:%.2f %%RH, Count:%d\n", temp, humi, sample_count);
        save_data_to_file(temp, humi);
        snprintf(pub_buf, sizeof(pub_buf), "{\"temp\":\"%.2f\",\"humi\":\"%.2f\"}", temp, humi);
        mqtt_publish_handle1(client, pub_buf);
        mqtt_sleep_ms(5 * 1000);
    }
}

int mqtt(void)
{
    rt_thread_t tid_mqtt;
    tid_mqtt = rt_thread_create("kawaii_demo", kawaii_mqtt_demo, RT_NULL, 2048, 17, 10);
    if (tid_mqtt == RT_NULL)
    {
        return -RT_ERROR;
    }
    rt_thread_startup(tid_mqtt);
    return RT_EOK;
}
MSH_CMD_EXPORT(mqtt, Kawaii MQTT + AHT10 test);

#define WIFI_CS GET_PIN(F, 10)
void WIFI_CS_PULL_DOWM(void)
{
    rt_pin_mode(WIFI_CS, PIN_MODE_OUTPUT);
    rt_pin_write(WIFI_CS, PIN_HIGH);
}
INIT_BOARD_EXPORT(WIFI_CS_PULL_DOWM);

int main(void)
{
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, PIN_LOW);

    while (1)
    {
        rt_thread_mdelay(500);
    }
    return 0;
}
