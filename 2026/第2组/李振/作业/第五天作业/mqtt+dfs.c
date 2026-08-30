#include <rtthread.h>
#include "aht10.h"
#include <rtdevice.h>
#include <board.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "mqttclient.h"

#define DBG_TAG "main"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>
#define WIFI_CS GET_PIN (F, 10)
#define WIFI_CS GET_PIN(F, 10)

void WIFI_CS_PULL_DOWN(void)
{
    rt_pin_mode(WIFI_CS, PIN_MODE_OUTPUT);
    rt_pin_write(WIFI_CS, PIN_LOW);
}
INIT_BOARD_EXPORT(WIFI_CS_PULL_DOWN);

#ifndef KAWAII_MQTT_HOST
#define KAWAII_MQTT_HOST               "jiejie01.top"
#endif
#ifndef KAWAII_MQTT_PORT
#define KAWAII_MQTT_PORT               "1883"
#endif
#ifndef KAWAII_MQTT_CLIENTID
#define KAWAII_MQTT_CLIENTID           "rtthread001"
#endif
#ifndef KAWAII_MQTT_USERNAME
#define KAWAII_MQTT_USERNAME           "rt-thread"
#endif
#ifndef KAWAII_MQTT_PASSWORD
#define KAWAII_MQTT_PASSWORD           "rt-thread"
#endif
#ifndef KAWAII_MQTT_SUBTOPIC
#define KAWAII_MQTT_SUBTOPIC           "rtt-sub"
#endif
#ifndef KAWAII_MQTT_PUBTOPIC
#define KAWAII_MQTT_PUBTOPIC           "rtt-pub"
#endif

static void sub_topic_handle1(void* client, message_data_t* msg)
{
    (void) client;
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------");
    KAWAII_MQTT_LOG_I("%s:%d %s()...\ntopic: %s\nmessage:%s", __FILE__, __LINE__, __FUNCTION__, msg->topic_name, (char*)msg->message->payload);
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------");
}

/*独立MQTT发布函数*/
static int mqtt_publish_temp_humi(mqtt_client_t *client, float temp, float humi, unsigned int count)
{
    char mqtt_buf[128] = {0};
    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));

    rt_snprintf(mqtt_buf, sizeof(mqtt_buf), "Temp %.1f ; Humi %.1f ; Count %d\n", temp, humi, count);
    msg.qos = QOS0;
    msg.payload = mqtt_buf;
    return mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);
}

/*新增：独立写入本地文件函数，一次写一行，追加模式*/
static void save_data_to_file(float temp, float humi, unsigned int count)
{
    int fd;
    char file_buf[128] = {0};
    rt_snprintf(file_buf, sizeof(file_buf), "Temp %.1f ; Humi %.1f ; Count %d\n", temp, humi, count);
    fd = open("/fal/Data.txt", O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0)
    {
        rt_kprintf("open /fal/Data.txt failed\n");
        return;
    }
    write(fd, file_buf, rt_strlen(file_buf));
    close(fd);
}


static void kawaii_mqtt_demo(void *parameter)
{
    mqtt_client_t *client = NULL;
    aht10_device_t dev;
    float humidity,temperature;
    unsigned int count = 1;
    int is_connected = 0;

    rt_thread_delay(6000);

    dev = aht10_init("i2c3");
    if(dev == RT_NULL)
    {
        rt_kprintf("AHT init failed!\n");
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

    while (1)
    {
        if(!is_connected)
        {
            if(mqtt_connect(client) == RT_EOK)
            {
                is_connected = 1;
                mqtt_subscribe(client, KAWAII_MQTT_SUBTOPIC, QOS0, sub_topic_handle1);
            }
            else
            {
                mqtt_sleep_ms(2000);
                continue;
            }
        }

        humidity = aht10_read_humidity(dev);
        temperature = aht10_read_temperature(dev);


        mqtt_publish_temp_humi(client, temperature, humidity, count);
        /*调用新增写文件函数，一次写入一行*/
        save_data_to_file(temperature, humidity, count);

        count++;
        mqtt_sleep_ms(4 * 1000);
    }
}

int main(void)
{
    rt_thread_t tid_mqtt;
    tid_mqtt = rt_thread_create("kawaii_demo", kawaii_mqtt_demo, RT_NULL, 2048, 17, 10);
    if (tid_mqtt != RT_NULL)
    {
        rt_thread_startup(tid_mqtt);
    }
    return 0;
}

