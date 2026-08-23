
/*
 * @Author: jiejie
 * @Github: https://github.com/jiejieTop
 * @LastEditTime: 2020-06-17 14:35:29
 * @Description: the code belongs to jiejie, please keep the author information and source code according to the license.
 */
#include <stdio.h>
#include <stdint.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "mqttclient.h"



#include "ath10_msg.h"
extern rt_mailbox_t mqtt_mailbox;




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






static int mqtt_publish_handle1(mqtt_client_t *client) {
    rt_ubase_t recv_addr;
    rt_err_t ret = rt_mb_recv(mqtt_mailbox, &recv_addr, 500);
    if (ret != RT_EOK) {
        return 0;   // 无数据，本次不发布
    }
    ath10_msg_t *p_msg = (ath10_msg_t *)recv_addr;
    char data[64];
    sprintf(data, "temp=[%.2f] humi=[%.2f%%]", p_msg->temp, p_msg->humid);
    rt_free(p_msg);   // 释放内存

    mqtt_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.qos = QOS0;
    msg.payload = (void *)data;
    msg.payloadlen = strlen(data);
    return mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);
}

static void mqtt_demo(void *parameter)
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

        mqtt_sleep_ms(1000);
//        mqtt_sleep_ms(4 * 1000);
    }
}

int mqtt(void)
{
    rt_thread_t tid_mqtt;

    tid_mqtt = rt_thread_create("kawaii_demo", mqtt_demo, RT_NULL, 2048, 17, 10);
    if (tid_mqtt == RT_NULL) {
        return -RT_ERROR;
    }

    rt_thread_startup(tid_mqtt);

    return RT_EOK;
}
MSH_CMD_EXPORT(mqtt, Kawaii MQTT client demo program);
