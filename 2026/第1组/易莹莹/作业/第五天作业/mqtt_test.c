
/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @LastEditTime: 2020-06-17 14:35:29
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
extern float temp;
extern float humi;

#include <stdio.h>
#include <stdint.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "mqttclient.h"

#include <ulog.h>

//#include <dfs_posix.h>
#include <dfs_file.h>
#include <unistd.h>
#include <stdio.h> /* rename() */
#include <sys/stat.h>
#include <sys/statfs.h> /* statfs() */

static uint32_t sample_count = 0;
static char file_buf[128];
static char mqtt_buf[128];

#define PIN_LED_B              GET_PIN(F, 11)      // PF11 :  LED_B        --> LED
#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED


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

char send_buf[128];

static void sub_topic_handle1(void* client, message_data_t* msg)
{
    (void) client;
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------");
    KAWAII_MQTT_LOG_I("%s:%d %s()...\ntopic: %s\nmessage:%s", __FILE__, __LINE__, __FUNCTION__, msg->topic_name, (char*)msg->message->payload);
    KAWAII_MQTT_LOG_I("-----------------------------------------------------------------------------------");

    //解析指令控制小灯
    char *rcvd_cmd = (char*)msg->message->payload;
    if(rt_strcmp(rcvd_cmd, "led_toggle") == 0)
    {
        rt_kprintf("toggle LED!\n");
        rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
    }
    else if(rt_strcmp(rcvd_cmd, "led_on") == 0)
    {
        rt_kprintf("LED On!\n");
        rt_pin_write(PIN_LED_R, PIN_LOW);
    }
    else if(rt_strcmp(rcvd_cmd, "led_off") == 0)
    {
        rt_kprintf("LED Off!\n");
        rt_pin_write(PIN_LED_R, PIN_HIGH);
    }
}

void save_data_to_file(float temp, float humi, uint32_t count)
{
    int fd;
    int w_len;

    fd = open("/font/Data.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if(fd < 0)
    {
        rt_kprintf("open /font/Data.txt FAILED!\n");
        return;
    }

    w_len = rt_snprintf(file_buf, sizeof(file_buf),"Temp: %.2f ; Humi: %.2f ; Count: %d\r\n", temp, humi, count);
    write(fd, file_buf, w_len);
    close(fd);
}


static int mqtt_publish_handle1(mqtt_client_t *client)
{
    sample_count ++;

    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));

    rt_snprintf(send_buf, sizeof(send_buf), "temp = %.2f,humi = %.2f\n", temp, humi);
    msg.payload = (void *)send_buf;

    msg.qos = QOS0;

    mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);
    save_data_to_file(temp, humi, sample_count);

    return RT_EOK;
}

static void kawaii_mqtt_demo(void *parameter)
{
    mqtt_client_t *client = NULL;
    
    rt_thread_delay(6000);
    


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
    
    while (1) {
        mqtt_publish_handle1(client);
                               
        mqtt_sleep_ms(4 * 1000);
    }
}

int ka_mqtt(void)
{
    rt_thread_t tid_mqtt;

    tid_mqtt = rt_thread_create("kawaii_demo", kawaii_mqtt_demo, RT_NULL, 4096, 17, 10);
    if (tid_mqtt == RT_NULL) {
        return -RT_ERROR;
    }

    rt_thread_startup(tid_mqtt);

    return RT_EOK;
}
MSH_CMD_EXPORT(ka_mqtt, Kawaii MQTT client test program);
