/*
 * day5_app.c - 第五天作业综合应用
 *
 * 作业要求：
 *   1. 使用AHT10软件包采集温湿度并上传到EMQX
 *   2. 挂载font分区，将数据同步写入 Data.txt
 *   3. 利用云端发送指令实现小灯翻转
 *
 * 本文件使用条件编译，未开启对应软件包时仍可编译通过。
 * 开启软件包后对应功能自动生效。
 *
 * 需要在 RT-Thread Settings 中开启：
 *   - AHT21(i2c3)     -> 自动启用 AHT10 软件包和 I2C3
 *   - RW007 (SPI2)     -> WiFi 网络模块
 *   - FAL filesystem   -> 自动启用 FAL、DFS、FatFS、W25Q64
 *   - kawaii-mqtt      -> MQTT 客户端，version 设为 4
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* ======================== 条件编译检查 ======================== */

#ifdef PKG_USING_AHT10
#include "aht10.h"
#endif

#ifdef PKG_USING_KAWAII_MQTT
#include "mqttclient.h"
#endif

#ifdef RT_USING_FAL
#include <fal.h>
#endif

#ifdef RT_USING_DFS
#include <dfs_fs.h>
#include <dfs_file.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif

/* ======================== 配置定义 ======================== */

#define PIN_LED_R            GET_PIN(F, 12)
#define AHT10_I2C_BUS        "i2c3"

/* MQTT 配置 */
#define MQTT_HOST            "broker.emqx.io"
#define MQTT_PORT            "1883"
#define MQTT_CLIENT_ID       "rtt-spark-05rtt"
#define MQTT_USER            "RT-Thread"
#define MQTT_PASS            "RT-Thread"

/* 主题：发布=板卡->MQTTX，订阅=MQTTX->板卡 */
#define TOPIC_PUB            "rsoc/spark05/device/up"
#define TOPIC_SUB            "rsoc/spark05/device/down"

/* 文件系统 */
#define FS_MOUNT_POINT       "/fal"
#define DATA_FILE            "/fal/Data.txt"
#define FONT_PART_NAME       "font"

/* 采集间隔 */
#define COLLECT_INTERVAL_MS  5000

/* ======================== 全局变量 ======================== */

static rt_uint32_t collect_count = 0;
static rt_bool_t  led_state      = RT_FALSE;

#ifdef PKG_USING_AHT10
static aht10_device_t aht10_dev = RT_NULL;
#endif

#ifdef PKG_USING_KAWAII_MQTT
static mqtt_client_t *mqtt_cli  = RT_NULL;
static rt_bool_t      mqtt_ready = RT_FALSE;
#endif

/* ======================== LED 控制（作业3） ======================== */

static void led_init(void)
{
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);
}

static void led_toggle(void)
{
    led_state = !led_state;
    rt_pin_write(PIN_LED_R, led_state ? PIN_LOW : PIN_HIGH);
    rt_kprintf("[LED] %s\n", led_state ? "ON" : "OFF");
}

/* ======================== MQTT 回调（作业3） ======================== */

#ifdef PKG_USING_KAWAII_MQTT
/* 收到云端消息后翻转 LED */
static void mqtt_rx_cb(void *client, message_data_t *msg)
{
    char buf[64];
    rt_size_t len = msg->message->payloadlen;
    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;
    rt_memcpy(buf, msg->message->payload, len);
    buf[len] = '\0';

    rt_kprintf("[MQTT RX] %s: %s\n", msg->topic_name, buf);
    led_toggle();
}

/* 连接 MQTT Broker 并订阅下行主题 */
static int mqtt_init(void)
{
    mqtt_cli = mqtt_lease();
    if (mqtt_cli == RT_NULL)
        return -1;

    mqtt_set_host(mqtt_cli, MQTT_HOST);
    mqtt_set_port(mqtt_cli, MQTT_PORT);
    mqtt_set_client_id(mqtt_cli, MQTT_CLIENT_ID);
    mqtt_set_user_name(mqtt_cli, MQTT_USER);
    mqtt_set_password(mqtt_cli, MQTT_PASS);
    mqtt_set_clean_session(mqtt_cli, 1);

    if (mqtt_connect(mqtt_cli) != 0)
    {
        rt_kprintf("[MQTT] connect failed\n");
        return -1;
    }

    mqtt_subscribe(mqtt_cli, TOPIC_SUB, QOS0, mqtt_rx_cb);
    rt_kprintf("[MQTT] connected, sub: %s\n", TOPIC_SUB);
    mqtt_ready = RT_TRUE;
    return 0;
}

/* 发布温湿度 JSON 到云端 */
static void mqtt_pub_data(float temp, float humi)
{
    char payload[128];
    mqtt_message_t msg;

    if (!mqtt_ready)
        return;

    rt_memset(&msg, 0, sizeof(msg));
    msg.qos = QOS0;
    rt_sprintf(payload,
               "{\"temperature\":%d.%d,\"humidity\":%d.%d,\"count\":%d}",
               (int)temp, (int)(temp * 10) % 10,
               (int)humi, (int)(humi * 10) % 10,
               collect_count);
    msg.payload    = payload;
    msg.payloadlen = rt_strlen(payload);
    mqtt_publish(mqtt_cli, TOPIC_PUB, &msg);
}
#endif /* PKG_USING_KAWAII_MQTT */

/* ======================== 文件系统（作业2） ======================== */

#ifdef RT_USING_DFS
/* 挂载 font 分区，失败则格式化后重试 */
static void fs_init(void)
{
#ifdef RT_USING_FAL
    fal_init();
    if (fal_blk_device_create(FONT_PART_NAME) == RT_NULL)
    {
        rt_kprintf("[FS] create blk for '%s' failed\n", FONT_PART_NAME);
        return;
    }
#endif

    if (dfs_mount(FONT_PART_NAME, FS_MOUNT_POINT, "elm", 0, 0) == 0)
    {
        rt_kprintf("[FS] mount -> %s ok\n", FS_MOUNT_POINT);
        return;
    }

    rt_kprintf("[FS] first mount failed, formatting...\n");
    dfs_mkfs("elm", FONT_PART_NAME);

    if (dfs_mount(FONT_PART_NAME, FS_MOUNT_POINT, "elm", 0, 0) == 0)
        rt_kprintf("[FS] mount -> %s ok (after format)\n", FS_MOUNT_POINT);
    else
        rt_kprintf("[FS] mount failed\n");
}

/* 追加写入 Data.txt，全角冒号用 GBK 字节 \xa3\xba */
static void save_to_file(float temp, float humi)
{
    int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0);
    char buf[128];

    if (fd < 0)
    {
        rt_kprintf("[FS] open %s failed\n", DATA_FILE);
        return;
    }

    rt_sprintf(buf,
               "Temp\xa3\xba%d.%d ; Humi\xa3\xba%d.%d ; Count\xa3\xba %d\n",
               (int)temp, (int)(temp * 10) % 10,
               (int)humi, (int)(humi * 10) % 10,
               collect_count);

    write(fd, buf, rt_strlen(buf));
    close(fd);
}
#endif /* RT_USING_DFS */

/* ======================== 主采集线程 ======================== */

static void sensor_thread(void *p)
{
    (void)p;
    float temp = 0.0f, humi = 0.0f;

    rt_kprintf("[APP] waiting for network...\n");
    rt_thread_mdelay(8000);

#ifdef PKG_USING_AHT10
    aht10_dev = aht10_init(AHT10_I2C_BUS);
    if (aht10_dev == RT_NULL)
    {
        rt_kprintf("[APP] AHT10 init failed on %s\n", AHT10_I2C_BUS);
        return;
    }
    rt_kprintf("[APP] AHT10 init ok\n");
#else
    rt_kprintf("[APP] AHT10 package not enabled!\n");
    rt_kprintf("[APP] Enable AHT21(i2c3) in RT-Thread Settings\n");
#endif

#ifdef PKG_USING_KAWAII_MQTT
    while (mqtt_init() != 0)
        rt_thread_mdelay(3000);
#else
    rt_kprintf("[APP] kawaii-mqtt not enabled!\n");
    rt_kprintf("[APP] Enable kawaii-mqtt in RT-Thread Settings\n");
#endif

    while (1)
    {
        collect_count++;

#ifdef PKG_USING_AHT10
        temp = aht10_read_temperature(aht10_dev);
        humi = aht10_read_humidity(aht10_dev);
#endif

        rt_kprintf("[DATA] Temp: %d.%d, Humi: %d.%d, Count: %d\n",
                    (int)temp, (int)(temp * 10) % 10,
                    (int)humi, (int)(humi * 10) % 10,
                    collect_count);

#ifdef RT_USING_DFS
        save_to_file(temp, humi);
#endif

#ifdef PKG_USING_KAWAII_MQTT
        mqtt_pub_data(temp, humi);
#endif

        rt_thread_mdelay(COLLECT_INTERVAL_MS);
    }
}

/* ======================== MSH 命令 ======================== */

static void start_app(void)
{
    led_init();

#ifdef RT_USING_DFS
    fs_init();
#else
    rt_kprintf("[APP] DFS not enabled, file logging disabled\n");
#endif

    rt_thread_t tid = rt_thread_create("sensor", sensor_thread, RT_NULL,
                                        8192, 15, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        rt_kprintf("[APP] sensor thread started\n");
    }
    else
        rt_kprintf("[APP] create thread failed\n");
}
MSH_CMD_EXPORT(start_app, start day5 homework);
