/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-21     godbear       the first version
 */
/*
作业要求：
1. 使用 **AHT10 软件包**采集温湿度，上传到 **EMQX**（或其它 MQTT 服务器）；
2. 梳理 **DFS 文件系统**启动流程，熟悉 shell 命令，把 **font 分区挂载**上，温湿度上传的同时**同步写入文件系统 `Data.txt`**（内容格式 `Temp: XX ; Humi: XX ; Count: N`，N 为自上电起采集次数）；
3. 利用**云端给开发板发送指令**实现**小灯翻转**；
*/


#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "mqttclient.h"
#include <dfs_posix.h>
#include <rtdbg.h>
#include <ulog.h>
#include <aht10.h>
#include "sensor_asair_aht10.h"

#define DBG_TAG "ath10"
#define DBG_LVL DBG_LOG
// #define AHT10_I2C_BUS  "i2c4"



#ifndef KAWAII_MQTT_HOST
#define KAWAII_MQTT_HOST               "broker.emqx.io"
#endif
#ifndef KAWAII_MQTT_PORT
#define KAWAII_MQTT_PORT               "1883"
#endif
#ifndef KAWAII_MQTT_CLIENTID
#define KAWAII_MQTT_CLIENTID           "godbear12138"
#endif
#ifndef KAWAII_MQTT_USERNAME
#define KAWAII_MQTT_USERNAME           "godbear12138"
#endif
#ifndef KAWAII_MQTT_PASSWORD
#define KAWAII_MQTT_PASSWORD           "godbear12138"
#endif
#ifndef KAWAII_MQTT_SUBTOPIC
#define KAWAII_MQTT_SUBTOPIC           "rtt-sub12138"
#endif
#ifndef KAWAII_MQTT_PUBTOPIC
#define KAWAII_MQTT_PUBTOPIC           "rtt-pub12138"
#endif

#define MAX_SAVE 10        /* 最多保留条数 */

/*          引脚定义                        */
#define LED_R_PIN       GET_PIN(F,12)
#define WIFI_CS         GET_PIN(F,10)
#define LED_ON          PIN_HIGH
#define LED_OFF         PIN_LOW
#define CMD_LED_TOGGLE     "led_toggle"     /* 云端下发这个词就翻转灯 */
static rt_uint8_t led_state = 1;            /* 记录灯当前状态 */


/*      定义一个数据结构体用于存放要存储和发送的信息：温度、湿度、发送次数             */
typedef struct{
    float temp;
    float humi;
    unsigned int Count;
} sensor_data_t;

/*                          全局变量                                                        */
static  rt_mq_t sensor_mqtt_mq= RT_NULL;         //  ATH10消息队列 发给服务器
static  rt_mq_t sensor_file_mq= RT_NULL;         //  ATH10消息队列 发给文件系统
static  rt_thread_t AHT10_read = RT_NULL;   //  ATH10温度采集
static  rt_thread_t MQTT_auto = RT_NULL;    //  MQTT信息发送
static  rt_thread_t File_save = RT_NULL;    //  FAT文件信息保存
mqtt_client_t *client = NULL;   // 全局 MQTT 客户端句柄，供发布函数和主线程共用


/* 线程1：每5秒采集一次温湿度，并发送给线程2服务端和线程3文件系统 */
void aht10_read_entry(void * par)
{
    aht10_device_t aht10 = aht10_init("i2c3"); // 初始化温湿度传感器对应的I2C接口
    sensor_data_t mq_data;
    mq_data.Count = 0;
    if(aht10 == RT_NULL)
    {
        LOG_E("aht10 init failed!\n");

    }

    while(1)
    {
        // 1.ath10采集数据，并把发送次数加一
        mq_data.temp = aht10_read_temperature(aht10);
        mq_data.humi = aht10_read_humidity(aht10);
        mq_data.Count +=1;
        // rt_kprintf("temp = %.2f, humi = %.2f, Count = %d\n",mq_data.temp,mq_data.humi,mq_data.Count);

        // 2.发送消息给线程2服务端和线程3文件系统
        if(rt_mq_send(sensor_mqtt_mq, &mq_data, sizeof(mq_data)) != RT_EOK)
        {
            rt_kprintf("[Sensor] MQ send to Upload Thread failed!\n");
        }
        mq_data.temp = aht10_read_temperature(aht10);
        mq_data.humi = aht10_read_humidity(aht10);
        mq_data.Count=mq_data.Count;
        if(rt_mq_send(sensor_file_mq, &mq_data, sizeof(mq_data)) != RT_EOK)
        {
            rt_kprintf("[Sensor] MQ save to Save Thread failed!\n");
        }
        // 3.等待三秒
        rt_thread_mdelay(3000);
    }
}



/*          收到订阅主题的消息时，在串口打印出来                          */
static void sub_topic_handle1(void* client, message_data_t* msg)
{
    (void) client;  // 消除"未使用参数"编译器警告。因为 client 参数在本函数里用不到
    const char *payload = (const char *)msg->message->payload;              //  (char*)msg->message->payload    消息正文（payload 是 void*，强转成 char* 才能打印）
    KAWAII_MQTT_LOG_I("recv topic=%s : msg=%s\n", msg->topic_name, payload);    //  msg->topic_name 消息来自哪个主题
    /* 云端下发特定命令 → 翻转小灯 */
    /* 用 strstr 做"包含"匹配，兼容 JSON 格式的 {"msg":"led_toggle"} */
    if (strstr(payload, CMD_LED_TOGGLE) != RT_NULL)
    {
        led_state = rt_pin_read(LED_R_PIN);
        rt_pin_write(LED_R_PIN, led_state ? LED_OFF : LED_ON);
        rt_kprintf("LED toggle -> %s\n", led_state ? "OFF" : "ON");

    }
}


//      可命令行调用的"发消息"函数，在控制台输入命令即可手动发布一条 MQTT 消息
static int mqtt_pu_h1(int num, char* *str) // num :传入的参数个数
{
    (void) num;              // 明确告诉编译器：我不打算用它
    mqtt_message_t msg;
    if (num < 2) {                    /* 至少要有 1 个参数，否则 str[1] 越界 */
        rt_kprintf("usage: mqtt_pu_h1 <message>\n");
        return -RT_ERROR;
    }
    memset(&msg, 0, sizeof(msg));                                   // 消息结构体清零，避免残留垃圾数据
    msg.qos = QOS0;                                                 // 服务质量设为 QOS0（最多发一次，不确认）
    msg.payload = (void *)str[1];                                   // 把命令行第 2 个参数作为要发送的消息内容
    return mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);        // 发布到发布主题
}

/*          MQTT入口函数                            */
static void kawaii_mqtt_entry(void *par)
{
    mqtt_message_t msg;
    sensor_data_t data;
    char buf[128];
    rt_thread_delay(6000);                        // 线程启动后先延时 6 秒，等网络就绪
    mqtt_log_init();                              // 初始化 MQTT 日志系统
    client = mqtt_lease();                        // 申请/创建 MQTT 客户端实例
    mqtt_set_host(client, KAWAII_MQTT_HOST);      // 设置服务器地址
    mqtt_set_port(client, KAWAII_MQTT_PORT);      // 设置服务器端口（默认 1883）
    mqtt_set_user_name(client, KAWAII_MQTT_USERNAME);  // 设置账号
    mqtt_set_password(client, KAWAII_MQTT_PASSWORD);  // 设置密码
    mqtt_set_client_id(client, KAWAII_MQTT_CLIENTID); // 设置客户端唯一 ID
    mqtt_set_clean_session(client, 1);            // 清会话标志：重连后不保留旧会话
    KAWAII_MQTT_LOG_I("The ID of the Kawaii client is: %s .\n", KAWAII_MQTT_CLIENTID);
    mqtt_connect(client);                         // 连接服务器
    mqtt_subscribe(client, KAWAII_MQTT_SUBTOPIC, QOS0, sub_topic_handle1); // 订阅主题+绑定回调
    while(1)
    {
        /*
         *  rt_mq_recv 用 500ms 超时，而不是死等(RT_WAITING_FOREVER)
         *  这样即使长时间没有新数据，也能保证下面的 mqtt_sleep_ms
         *  被周期性调用，MQTT 心跳不会饿死、不会被服务器踢下线。
         */
        if(rt_mq_recv(sensor_mqtt_mq, &data, sizeof(data),rt_tick_from_millisecond(500)) == RT_EOK)
        {
            /* 将输入的内容 + 板子上的温湿度拼成一句话 */
            snprintf(buf, sizeof(buf), "Temp: %.2f; Humi: %.2f; Count: %d",data.temp,data.humi,data.Count);
            memset(&msg, 0, sizeof(msg));                                   // 消息结构体清零，避免残留垃圾数据
            msg.qos = QOS0;                                                 // 服务质量设为 QOS0（最多发一次，不确认）
            msg.payload = (void *)buf;                                      // 把命令行第 2 个参数作为要发送的消息内容
            mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);
        }
        /* 让 Kawaii 处理收发与心跳保活 */
        mqtt_sleep_ms(500);
    }
}


/*              温湿度信息保存线程入口函数                       */
void files_save_entry(void * par)
{

    //int fd;
    sensor_data_t data;
    char cache[MAX_SAVE][64];   /* 内存缓存：最近 10 条 */
    int count = 0;              /* 当前缓存条数 */
    // 先把文件清零，防止之前存储的文件污染信息
    if (unlink("/fal/Data.txt") == 0) {
        rt_kprintf("File deleted successfully.\n");
    } else {
        rt_kprintf("Failed to delete file.\n");
    }

    while(1)
    {
        if(rt_mq_recv(sensor_file_mq, &data, sizeof(data), RT_WAITING_FOREVER) != RT_EOK)
        {
            rt_kprintf("mq recv failed\n");
            break;
        }
        /* ① 缓存已满：整体前移一位，丢掉最旧的一条 */
        if (count >= MAX_SAVE)
        {
            for (int i = 0; i < MAX_SAVE - 1; i++)
                strcpy(cache[i], cache[i + 1]);
            count = MAX_SAVE - 1;
        }
        /* 只写实际拼出的长度，不要写满 sizeof()（会带出垃圾字节） */
        snprintf(cache[count], sizeof(cache[count]),"Temp: %.2f; Humi: %.2f; Count: %d\r\n",data.temp, data.humi, data.Count);
        count++;
        /* ③ 把缓存整体重写进文件：只能保留10条信息 */
        /* 已存满 10 条：把消息从队列取走但不写文件 */
        /*if (data.Count > 10)
            continue;*/
        /*fd = open("/fal/Data.txt", O_CREAT | O_RDWR | O_APPEND);
        if (fd < 0)
        {
            rt_kprintf("[Save] open Data.txt failed\n");
            break;
        }*/
        /* ③ 把缓存整体重写进文件：O_TRUNC 先截断旧内容 */
        int fd = open("/fal/Data.txt", O_CREAT | O_RDWR | O_TRUNC);
        if (fd >= 0)
        {
            for (int i = 0; i < count; i++)
                write(fd, cache[i], rt_strlen(cache[i]));// 用函数算长度
            close(fd);          /* 落盘 */// ← close 触发 flush，内容立即进磁盘
        }
        //write(fd, buf, rt_strlen(buf));
        /* 将输入的内容 + 板子上的温湿度拼成一句话 */
        // snprintf(buf, sizeof(buf), "Temp: %.2f; Humi: %.2f; Count: %d",data.temp,data.humi,data.Count);
        //rt_kprintf(buf);
        //int w = write(fd, buf, rt_strlen(buf)); // 用函数算长度
        //rt_kprintf("[Save] write len=%d written=%d\n", rt_strlen(buf), w);       // ③ write 写了多少
        //close(fd);
    }
}

// 拉高RW007芯片SPI片选，防止文件系统注册失败
static int WIFI_CS_PULL_DOWN(void)
{
    rt_pin_mode(WIFI_CS, PIN_MODE_OUTPUT);
    rt_pin_write(WIFI_CS, PIN_HIGH);
    return 0;
}


static int mqtt_aht10(void)
{

    // 1. 创建消息队列
    // 参数说明: 名称, 最大消息数, 每个消息的大小(字节), 标志
    sensor_mqtt_mq = rt_mq_create("sensor_mqtt_mq",
                                sizeof(sensor_data_t),  // 消息大小：结构体大小
                                5, // 队列深度：最多存5条消息
                                RT_IPC_FLAG_FIFO);   // FIFO 模式
    sensor_file_mq = rt_mq_create("sensor_file_mq",
                                sizeof(sensor_data_t),  // 消息大小：结构体大小
                                5, // 队列深度：最多存5条消息
                                RT_IPC_FLAG_FIFO);   // FIFO 模式
    if (sensor_mqtt_mq == RT_NULL)
    {
        rt_kprintf("create mqtt message queue failed\n");
        return -RT_ENOMEM;
    }
    if (sensor_file_mq == RT_NULL)
    {
        rt_kprintf("Create file message queue failed.\n");
        return -1;
    }

    // 2. 创建线程1：采集 (优先级 10)
    AHT10_read = rt_thread_create("AHT10_read", aht10_read_entry, RT_NULL, 1024, 10, 10);
    if (AHT10_read != RT_NULL)
        rt_thread_startup(AHT10_read);

    // 3. 创建线程2：保存 (优先级 15)
    File_save = rt_thread_create("File_save", files_save_entry, RT_NULL, 2048, 15, 10);
    if (File_save != RT_NULL)
        rt_thread_startup(File_save);

    // 4. 创建线程3：上传 (优先级 15)
    MQTT_auto = rt_thread_create("MQTT_auto", kawaii_mqtt_entry, RT_NULL, 5120, 15, 10);
    if (MQTT_auto != RT_NULL)
        rt_thread_startup(MQTT_auto);

    rt_kprintf("mqtt_aht10 App Started.\n");
    return 0;

}

static int led_hw_init(void)
{
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, LED_OFF);
    return  0;
}


INIT_BOARD_EXPORT(WIFI_CS_PULL_DOWN);   //wifi和wq芯片文件用的同一个SPI所以要拉高其中一个的片选才能使用另一个
INIT_APP_EXPORT(led_hw_init);

MSH_CMD_EXPORT(mqtt_aht10,send AHT10 msg to file and mqtt);
MSH_CMD_EXPORT(mqtt_pu_h1, mqtt_publish_handle1); // 注册为 MSH 命令行命令 可以自己向服务端发送信息
