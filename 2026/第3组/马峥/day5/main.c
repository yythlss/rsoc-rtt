/*
 * day5 作业 - AHT10 温湿度 + MQTT + DFS
 *
 * 功能：
 *   1. WiFi 自动连接 mzshouji
 *   2. AHT10 采集温湿度（每 2 秒一次）
 *   3. MQTT 连接 broker.emqx.io:1883
 *   4. 发布数据到 rtt/data
 *   5. 订阅 rtt/cmd，接收云端 LED 控制指令
 *   6. 数据同步写入 /flash/Data.txt
 *
 * 硬件：星火号 STM32F407 + AHT10 + RW007 WiFi + W25Q64 SPI Flash
 * 作者：马峥（第3组） 2026-08-22
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dfs_fs.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>
#include <wlan_mgnt.h>
#include "paho_mqtt.h"
#include "aht10.h"

#define DBG_TAG "day5.main"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ===================== 配置参数 ===================== */
#define WIFI_SSID         "mzshouji"
#define WIFI_PASSWORD     "00000000"

#define MQTT_URI          "tcp://broker.emqx.io:1883"
#define MQTT_SUBTOPIC     "rtt/cmd"
#define MQTT_PUBTOPIC     "rtt/data"

#define AHT10_I2C_BUS     "i2c3"
#define DATA_FILE_PATH    "/flash/Data.txt"
#define FLASH_DEV_NAME    "W25Q64"

#define LED_PIN           GET_PIN(F, 12)   /* PF12 = LED_R */
#define PUBLISH_INTERVAL  2000             /* 发布间隔 2s */

/* ===================== 共享数据结构 ===================== */
typedef struct
{
    float temperature;
    float humidity;
    rt_uint32_t count;
    rt_mutex_t lock;
} sensor_data_t;

static sensor_data_t g_sensor = {0};
static MQTTClient g_mqtt_client;
static int g_mqtt_started = 0;
static rt_uint8_t g_led_state = 0;

/* ===================== 工具函数 ===================== */
static void led_set(rt_bool_t on)
{
    if (on)
    {
        rt_pin_write(LED_PIN, PIN_LOW);   /* 星火号 LED 低电平点亮 */
        g_led_state = 1;
        LOG_I("[LED] ON");
    }
    else
    {
        rt_pin_write(LED_PIN, PIN_HIGH);
        g_led_state = 0;
        LOG_I("[LED] OFF");
    }
}

static void led_toggle(void)
{
    led_set(g_led_state ? RT_FALSE : RT_TRUE);
}

/* ===================== AHT10 数据采集线程 ===================== */
static void aht10_thread_entry(void *parameter)
{
    aht10_device_t dev = (aht10_device_t)parameter;
    float t, h;

    while (1)
    {
        t = aht10_read_temperature(dev);
        h = aht10_read_humidity(dev);

        rt_mutex_take(g_sensor.lock, RT_WAITING_FOREVER);
        g_sensor.temperature = t;
        g_sensor.humidity    = h;
        g_sensor.count++;
        rt_mutex_release(g_sensor.lock);

        LOG_I("[AHT10] T=%.1f  H=%.1f%%  Count=%u", t, h, g_sensor.count);
        rt_thread_mdelay(PUBLISH_INTERVAL);
    }
}

static int aht10_init_module(void)
{
    aht10_device_t dev;

    g_sensor.lock = rt_mutex_create("aht10", RT_IPC_FLAG_FIFO);
    if (g_sensor.lock == RT_NULL)
    {
        LOG_E("[AHT10] mutex create failed");
        return -RT_ERROR;
    }

    /* 等待传感器上电稳定 */
    rt_thread_mdelay(2000);

    dev = aht10_init(AHT10_I2C_BUS);
    if (dev == RT_NULL)
    {
        LOG_E("[AHT10] init failed on bus %s", AHT10_I2C_BUS);
        return -RT_ERROR;
    }
    LOG_I("[AHT10] init OK on %s", AHT10_I2C_BUS);

    rt_thread_t tid = rt_thread_create("aht10",
                                        aht10_thread_entry,
                                        dev,
                                        1024, 15, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }

    return RT_EOK;
}
INIT_APP_EXPORT(aht10_init_module);

/* ===================== 文件系统初始化 ===================== */
static int filesystem_init(void)
{
    /* 确保挂载点存在 */
    mkdir("/flash", 0);

    /* 尝试挂载 FAT */
    if (dfs_mount(FLASH_DEV_NAME, "/flash", "elm", 0, 0) == 0)
    {
        LOG_I("[FS] mount %s -> /flash OK", FLASH_DEV_NAME);
    }
    else
    {
        LOG_W("[FS] mount failed, try mkfs...");
        if (dfs_mkfs("elm", FLASH_DEV_NAME) == 0)
        {
            if (dfs_mount(FLASH_DEV_NAME, "/flash", "elm", 0, 0) == 0)
            {
                LOG_I("[FS] mount OK after mkfs");
            }
            else
            {
                LOG_E("[FS] mount failed after mkfs!");
                return -RT_ERROR;
            }
        }
        else
        {
            LOG_E("[FS] mkfs failed!");
            return -RT_ERROR;
        }
    }

    /* 初始化/截断数据文件 */
    int fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd >= 0)
    {
        const char *header = "# RSOC26 day5 AHT10 Data Log\r\n";
        write(fd, header, rt_strlen(header));
        close(fd);
        LOG_I("[FS] Data file ready: %s", DATA_FILE_PATH);
    }
    else
    {
        LOG_E("[FS] cannot create %s", DATA_FILE_PATH);
    }
    return RT_EOK;
}

/* ===================== 数据写入文件 ===================== */
static void data_log_append(float t, float h, rt_uint32_t count)
{
    char line[96];
    int fd;

    rt_snprintf(line, sizeof(line),
                "Temp:%.1f ; Humi:%.1f ; Count:%u\r\n",
                t, h, count);

    fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND);
    if (fd >= 0)
    {
        write(fd, line, rt_strlen(line));
        close(fd);
    }
    else
    {
        LOG_E("[LOG] file write failed");
    }
}

/* ===================== MQTT 回调 ===================== */
static void mqtt_sub_callback(MQTTClient *c, MessageData *msg_data)
{
    char buf[64] = {0};
    rt_size_t len = msg_data->message->payloadlen;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    rt_memcpy(buf, msg_data->message->payload, len);
    buf[len] = '\0';

    LOG_I("[MQTT SUB] %.*s -> %s",
          msg_data->topicName->lenstring.len,
          msg_data->topicName->lenstring.data,
          buf);

    if (rt_strcmp(buf, "LED_ON") == 0)
    {
        led_set(RT_TRUE);
    }
    else if (rt_strcmp(buf, "LED_OFF") == 0)
    {
        led_set(RT_FALSE);
    }
    else if (rt_strcmp(buf, "LED_TOGGLE") == 0)
    {
        led_toggle();
    }
    else
    {
        LOG_W("[MQTT] unknown cmd: %s", buf);
    }
}

static void mqtt_sub_default_callback(MQTTClient *c, MessageData *msg_data)
{
    char *p = (char *)msg_data->message->payload;
    p[msg_data->message->payloadlen] = '\0';
    LOG_D("[MQTT DEF] %s", p);
}

static void mqtt_connect_callback(MQTTClient *c)
{
    LOG_I("[MQTT] connecting...");
}

static void mqtt_online_callback(MQTTClient *c)
{
    LOG_I("[MQTT] online, subscribed: %s", MQTT_SUBTOPIC);
}

static void mqtt_offline_callback(MQTTClient *c)
{
    LOG_W("[MQTT] offline");
}

/* ===================== MQTT 启动 ===================== */
static void mqtt_start(void)
{
    MQTTPacket_connectData condata = MQTTPacket_connectData_initializer;
    static char cid[32] = {0};
    static int local_started = 0;

    if (local_started)
    {
        return;
    }
    local_started = 1;

    g_mqtt_client.isconnected = 0;
    g_mqtt_client.uri = MQTT_URI;

    rt_snprintf(cid, sizeof(cid), "rtt_mazheng_%d", (int)rt_tick_get());
    rt_memcpy(&g_mqtt_client.condata, &condata, sizeof(condata));
    g_mqtt_client.condata.clientID.cstring = cid;
    g_mqtt_client.condata.keepAliveInterval = 60;
    g_mqtt_client.condata.cleansession = 1;
    g_mqtt_client.condata.username.cstring = "";
    g_mqtt_client.condata.password.cstring = "";
    g_mqtt_client.condata.willFlag = 0;

    g_mqtt_client.buf_size = g_mqtt_client.readbuf_size = 1024;
    g_mqtt_client.buf = rt_malloc(g_mqtt_client.buf_size);
    g_mqtt_client.readbuf = rt_malloc(g_mqtt_client.readbuf_size);
    if (!g_mqtt_client.buf || !g_mqtt_client.readbuf)
    {
        LOG_E("[MQTT] no memory");
        return;
    }

    g_mqtt_client.connect_callback  = mqtt_connect_callback;
    g_mqtt_client.online_callback   = mqtt_online_callback;
    g_mqtt_client.offline_callback  = mqtt_offline_callback;
    g_mqtt_client.defaultMessageHandler = mqtt_sub_default_callback;

    /* 订阅 cmd 主题 */
    g_mqtt_client.messageHandlers[0].topicFilter = rt_strdup(MQTT_SUBTOPIC);
    g_mqtt_client.messageHandlers[0].callback    = mqtt_sub_callback;
    g_mqtt_client.messageHandlers[0].qos         = QOS1;

    LOG_I("[MQTT] starting: %s", MQTT_URI);
    paho_mqtt_start(&g_mqtt_client);
    g_mqtt_started = 1;
}

/* ===================== MQTT 发布线程 ===================== */
static void mqtt_pub_thread_entry(void *parameter)
{
    char payload[128];
    float t, h;
    rt_uint32_t cnt;

    /* 等 MQTT 起来 */
    rt_thread_mdelay(5000);

    while (1)
    {
        if (g_sensor.lock != RT_NULL)
        {
            rt_mutex_take(g_sensor.lock, RT_WAITING_FOREVER);
            t   = g_sensor.temperature;
            h   = g_sensor.humidity;
            cnt = g_sensor.count;
            rt_mutex_release(g_sensor.lock);
        }
        else
        {
            t = h = 0.0f;
            cnt = 0;
        }

        /* 写文件 */
        data_log_append(t, h, cnt);

        /* MQTT 发布 */
        if (g_mqtt_client.isconnected)
        {
            rt_snprintf(payload, sizeof(payload),
                        "{\"count\":%u,\"temp\":%.1f,\"humidity\":%.1f}",
                        cnt, t, h);

            MQTTMessage msg = {0};
            msg.qos = QOS1;
            msg.retained = 0;
            msg.payload = payload;
            msg.payloadlen = rt_strlen(payload);

            if (MQTTPublish(&g_mqtt_client, MQTT_PUBTOPIC, &msg) == 0)
            {
                LOG_I("[MQTT PUB] %s", payload);
            }
            else
            {
                LOG_W("[MQTT PUB] failed");
            }
        }
        else
        {
            LOG_W("[MQTT PUB] not connected, data only saved to file");
        }

        rt_thread_mdelay(PUBLISH_INTERVAL);
    }
}

static int mqtt_pub_thread_init(void)
{
    rt_thread_t tid = rt_thread_create("mqtt_pub",
                                        mqtt_pub_thread_entry,
                                        RT_NULL,
                                        2048, 14, 10);
    if (tid != RT_NULL) rt_thread_startup(tid);
    return RT_EOK;
}
INIT_APP_EXPORT(mqtt_pub_thread_init);

/* ===================== WiFi 事件 ===================== */
static void wlan_ready_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    if (buff && buff->len == sizeof(struct rt_wlan_info))
    {
        LOG_I("[WiFi] ready, ssid=%s", ((struct rt_wlan_info *)buff->data)->ssid.val);
    }
    /* WiFi 就绪后启动 MQTT */
    mqtt_start();
}

static void wlan_disconnect_handler(int event, struct rt_wlan_buff *buff, void *parameter)
{
    LOG_W("[WiFi] disconnected");
}

/* ===================== 主函数 ===================== */
int main(void)
{
    LOG_I("=========================================");
    LOG_I(" day5 - AHT10 + MQTT + DFS Demo");
    LOG_I(" Author: Mazheng | Group 3");
    LOG_I("=========================================");

    /* LED 初始化 */
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    led_set(RT_FALSE);

    /* 文件系统初始化 */
    filesystem_init();

    /* WiFi 事件注册 */
    rt_wlan_register_event_handler(RT_WLAN_EVT_READY, wlan_ready_handler, RT_NULL);
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED, wlan_disconnect_handler, RT_NULL);

    /* 设置 station 模式 + 自动重连 */
    rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    rt_wlan_config_autoreconnect(RT_TRUE);

    /* 连接 WiFi */
    LOG_I("[WiFi] connecting to %s ...", WIFI_SSID);
    rt_err_t r = rt_wlan_connect(WIFI_SSID, WIFI_PASSWORD);
    if (r != RT_EOK)
    {
        LOG_E("[WiFi] connect failed, ret=%d", r);
    }

    /* AHT10 初始化在 INIT_APP_EXPORT 阶段已完成（早于 main） */
    LOG_I("[MAIN] init done, modules running.");

    return 0;
}

/* ===================== MSH 测试命令 ===================== */
static void msh_led(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("Usage: led <on|off|toggle>\n");
        return;
    }
    if (!rt_strcmp(argv[1], "on"))       led_set(RT_TRUE);
    else if (!rt_strcmp(argv[1], "off")) led_set(RT_FALSE);
    else if (!rt_strcmp(argv[1], "toggle")) led_toggle();
}
MSH_CMD_EXPORT(msh_led, led <on|off|toggle>);

static void msh_show_data(int argc, char **argv)
{
    rt_kprintf("Count=%u  T=%.1f  H=%.1f%%\n",
               g_sensor.count, g_sensor.temperature, g_sensor.humidity);
}
MSH_CMD_EXPORT(msh_show_data, show current sensor data);

static void msh_cat_data(int argc, char **argv)
{
    /* 用 system("cat /flash/Data.txt") 打印 */
    extern int system(const char *);
    system("cat /flash/Data.txt");
}
MSH_CMD_EXPORT(msh_cat_data, cat /flash/Data.txt);
