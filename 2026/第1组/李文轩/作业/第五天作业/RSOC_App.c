/*
 * RSOC_App.c - 综合实验：AHT10 采集 + MQTT 上传 EMQX + 文件系统落盘 + 云端指令控制 LED
 *
 * 功能：
 *  1. 每 5 秒读取 AHT10 温湿度
 *  2. 通过 kawaii-mqtt 发布 JSON 到 EMQX：rsoc/liwenxuan/device/up
 *  3. 同时追加写入文件系统 (W25Q64 /fal 分区) 的 Data.txt
 *  4. 订阅 rsoc/liwenxuan/device/down，收到云端指令翻转 LED (PF12)
 *
 * 使用：msh > rsoc_app  （启动综合程序）
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdio.h>
#include <fcntl.h>

#include "aht10.h"
#include "mqttclient.h"
/* DFS POSIX API 头文件在 RT-Thread 4.1.1 的 legacy 兼容层 */
#include "../rt-thread/components/legacy/dfs/dfs_posix.h"
#include <dfs_fs.h>   /* dfs_mount: /fal 挂载兜底 */

/* ============ 配置（按需修改） ============ */
#define AHT10_I2C_BUS   "i2c3"
#define MQTT_HOST       "broker.emqx.io"
#define MQTT_PORT       "1883"
#define MQTT_CLIENTID   "rsoc_liwenxuan_board"     /* 必须唯一 */
#define PUB_TOPIC       "rsoc/liwenxuan/device/up"   /* 板子发布 */
#define SUB_TOPIC       "rsoc/liwenxuan/device/down" /* 板子订阅（收指令） */
#define DATA_FILE       "/fal/Data.txt"              /* 文件系统落盘路径 */
#define LED_PIN         GET_PIN(F, 12)               /* 星火一号红色 LED (PF12) */
#define SAMPLE_PERIOD_MS 5000                        /* 采集周期 */

/* ============ 全局变量 ============ */
static mqtt_client_t *client = RT_NULL;
static int led_state = 0;
static int sample_count = 0;

/* ---- 云端指令回调：LED 翻转 ---- */
static void cmd_handle(void *c, message_data_t *msg)
{
    (void)c;
    rt_kprintf("\n[CMD] received: %s\n", (char *)msg->message->payload);

    led_state = !led_state;
    rt_pin_write(LED_PIN, led_state ? PIN_LOW : PIN_HIGH);   /* LOW=亮（低电平点亮） */
    rt_kprintf("[CMD] LED toggled -> %s\n", led_state ? "ON" : "OFF");
}

/* ---- 追加写一行到 Data.txt ---- */
static void save_to_data_file(float temp, float humi, int cnt)
{
    int fd;
    char line[128];
    int len;

    /* 打开（不存在则创建），追加模式 */
    fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
    {
        rt_kprintf("[FS] open %s failed! (not mounted?)\n", DATA_FILE);
        return;
    }

    /* 不依赖 %f（需要 rt_vsprintf_full 包），用整数+小数位拆分 */
    len = snprintf(line, sizeof(line),
        "Temp： %d.%d ; Humi： %d.%d ; Count： %d\n",
        (int)temp, (int)(temp * 10) % 10,
        (int)humi, (int)(humi * 10) % 10,
        cnt);

    write(fd, line, len);
    close(fd);

    rt_kprintf("[FS] saved: %s", line);
}

/* ---- MQTT 发布 JSON ---- */
static int publish_weather(float temp, float humi, int cnt)
{
    char payload[128];
    mqtt_message_t msg;
    int len;

    len = snprintf(payload, sizeof(payload),
        "{\"temperature\":%d.%d,\"humidity\":%d.%d,\"count\":%d}",
        (int)temp, (int)(temp * 10) % 10,
        (int)humi, (int)(humi * 10) % 10,
        cnt);

    msg.qos = QOS0;
    msg.payload = payload;
    msg.payloadlen = len;

    if (mqtt_publish(client, PUB_TOPIC, &msg) == KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("[MQTT] published: %s\n", payload);
        return 0;
    }
    return -1;
}

/* ---- 挂载兜底：/fal 自动挂载偶发失败时补挂一次 ---- */
static void ensure_mount(void)
{
    if (dfs_mount("filesyst", "/fal", "elm", 0, 0) == 0)
    {
        rt_kprintf("[FS] mounted /fal (fallback)\n");
    }
}

/* ---- 综合任务函数 ---- */
static void rsoc_app_entry(void *parameter)
{
    aht10_device_t dev;
    float temp, humi;

    /* 0. 文件系统挂载兜底 */
    ensure_mount();

    /* 1. LED 初始化 */
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN, PIN_HIGH);   /* 初始熄灭 */

    /* 2. AHT10 初始化 */
    dev = aht10_init(AHT10_I2C_BUS);
    if (dev == RT_NULL)
    {
        rt_kprintf("[AHT10] init fail on %s!\n", AHT10_I2C_BUS);
        return;
    }
    rt_kprintf("[AHT10] init ok on %s\n", AHT10_I2C_BUS);

    /* 3. MQTT 连接 */
    client = mqtt_lease();
    mqtt_set_host(client, MQTT_HOST);
    mqtt_set_port(client, MQTT_PORT);
    mqtt_set_user_name(client, "");
    mqtt_set_password(client, "");
    mqtt_set_client_id(client, MQTT_CLIENTID);
    mqtt_set_clean_session(client, 1);

    if (mqtt_connect(client) != KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("[MQTT] connect failed!\n");
        return;
    }
    rt_kprintf("[MQTT] connect success: %s\n", MQTT_HOST);

    /* 4. 订阅指令主题（收到消息 -> cmd_handle -> LED 翻转） */
    mqtt_subscribe(client, SUB_TOPIC, QOS0, cmd_handle);
    rt_kprintf("[MQTT] subscribed: %s\n", SUB_TOPIC);

    /* 5. 主循环：采集 -> 上传 -> 落盘 */
    while (1)
    {
        humi = aht10_read_humidity(dev);
        temp = aht10_read_temperature(dev);
        sample_count++;

        publish_weather(temp, humi, sample_count);
        save_to_data_file(temp, humi, sample_count);

        rt_thread_mdelay(SAMPLE_PERIOD_MS);
    }
}

/* ---- msh 启动命令（防重复启动） ---- */
void rsoc_app(void)
{
    static rt_bool_t started = RT_FALSE;
    rt_thread_t tid;

    if (started)
    {
        rt_kprintf("[RSOC] app already running!\n");
        return;
    }
    started = RT_TRUE;

    tid = rt_thread_create("rsoc_app", rsoc_app_entry, RT_NULL, 4096, 20, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);
    else
        rt_kprintf("[RSOC] create thread failed!\n");
}
MSH_CMD_EXPORT(rsoc_app, RSOC app: AHT10 + MQTT(EMQX) + Filesystem + LED control);
