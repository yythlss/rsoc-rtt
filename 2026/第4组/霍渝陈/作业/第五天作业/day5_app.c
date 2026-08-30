/*
 * Copyright (c) 2026, 霍渝陈 (RSOC 第4组)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author    Notes
 * 2026-08-23     霍渝陈    Day5 作业：AHT10 温湿度 MQTT 上云 + 文件系统落盘 + 云端控灯
 *
 * 作业要求：
 *  1. 使用 AHT10 软件包采集温湿度并上传到 EMQX（broker.emqx.io）
 *  2. 梳理文件系统启动流程，将 font 分区挂载上，数据同步写入 /font/Data.txt
 *     （格式：Temp: XX ; Humi: XX ; Count: N，N 为自上电起采集次数）
 *  3. 利用云端给开发板发送指令实现小灯翻转
 *  4. 熟悉其他软件包（见笔记第六章）
 *
 * 使用条件编译，未开启对应软件包时仍可编译通过，方便逐项联调。
 * RT-Thread Settings 需开启：
 *  - AHT21(swi2c3)   → 自动启用 AHT10 软件包 + Sensor 框架 + 软件 I2C3
 *  - RW007 (SPI2)    → WiFi 网络模块（需修正引脚）
 *  - FAL + SPI FLASH → W25Q64 + 分区表（含 font 分区）
 *  - DFS + elm       → 文件系统
 *  - kawaii-mqtt     → MQTT 客户端
 *  - rt_vsnprintf_full（可选，用整数拼串则不强依赖 %f）
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* ======================== 条件编译头文件 ======================== */
#ifdef PKG_USING_AHT10
#include "aht10.h"
#endif

#ifdef PKG_USING_KAWAII_MQTT
#include "mqttclient.h"
#endif

#ifdef RT_USING_DFS
#include <dfs_posix.h>
#include <fcntl.h>
#endif

/* ======================== 配置定义 ======================== */

/* 板载外设引脚 */
#define PIN_LED_R            GET_PIN(F, 12)   /* 板载红灯：云端指令翻转它 */
#define PIN_WIFI_CS          GET_PIN(F, 10)   /* RW007 片选：初始化时上拉，避免与 W25Q64 抢 SPI2 */

/* AHT10 挂在软件 I2C3（注册名 "i2c3"） */
#define AHT10_I2C_BUS        "i2c3"

/* MQTT 配置：EMQX 免费公共服务器 */
#define MQTT_HOST            "broker.emqx.io"
#define MQTT_PORT            "1883"
#define MQTT_CLIENT_ID       "rsoc-huoyuchen-001"      /* 必须全局唯一 */
#define MQTT_USER            "huoyuchen"
#define MQTT_PASS            "huoyuchen"

/* 主题：up = 板卡上行发布，down = 云端下行订阅（错开命名避免干扰） */
#define TOPIC_PUB            "rsoc/huoyuchen/device/up"
#define TOPIC_SUB            "rsoc/huoyuchen/device/down"

/* 云端下发指令关键字：收到即翻转小灯 */
#define CMD_LED_TOGGLE       "led_toggle"

/* 文件系统：挂载 font 分区到 /font，数据文件 Data.txt */
#define FONT_PART_NAME       "font"
#define FS_MOUNT_POINT       "/font"
#define DATA_FILE            "/font/Data.txt"

/* 采集间隔 */
#define COLLECT_INTERVAL_MS  5000

/* ======================== 全局变量 ======================== */

static rt_uint32_t collect_count = 0;   /* 自上电起采集次数（Count） */
static rt_bool_t   led_state      = RT_FALSE;

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
    rt_pin_write(PIN_LED_R, PIN_HIGH);   /* 初始灭 */
}

static void led_toggle(void)
{
    led_state = !led_state;
    rt_pin_write(PIN_LED_R, led_state ? PIN_LOW : PIN_HIGH);
    rt_kprintf("[LED] toggle -> %s\n", led_state ? "ON" : "OFF");
}

/* ======================== MQTT（作业1 + 作业3） ======================== */

#ifdef PKG_USING_KAWAII_MQTT
/* 订阅回调：云端下发指令 → 翻转小灯（回调内不做耗时操作） */
static void mqtt_rx_cb(void *client, message_data_t *msg)
{
    const char *payload = (const char *)msg->message->payload;

    rt_kprintf("[MQTT RX] %s: %s\n", msg->topic_name, payload);
    /* 包含匹配，兼容 JSON 包裹 {"msg":"led_toggle"} */
    if (payload != RT_NULL && strstr(payload, CMD_LED_TOGGLE) != RT_NULL)
    {
        led_toggle();
    }
}

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
    rt_kprintf("[MQTT] connected to %s, sub: %s\n", MQTT_HOST, TOPIC_SUB);
    mqtt_ready = RT_TRUE;
    return 0;
}

/* 发布温湿度到云端（用整数拼串，不依赖 %f 浮点打印支持） */
static void mqtt_pub_data(float temp, float humi)
{
    char payload[128];
    mqtt_message_t msg;

    if (!mqtt_ready)
        return;

    rt_memset(&msg, 0, sizeof(msg));
    msg.qos = QOS0;
    rt_sprintf(payload,
               "Temp: %d.%d ; Humi: %d.%d ; Count: %d",
               (int)temp, (int)(temp * 10) % 10,
               (int)humi, (int)(humi * 10) % 10,
               collect_count);
    msg.payload    = payload;
    msg.payloadlen = rt_strlen(payload);
    mqtt_publish(mqtt_cli, TOPIC_PUB, &msg);
    rt_kprintf("[MQTT UP] %s\n", payload);
}
#endif /* PKG_USING_KAWAII_MQTT */

/* ======================== 文件系统（作业2） ======================== */

#ifdef RT_USING_DFS
/* 挂载 font 分区：创建块设备 → 挂载，失败先格式化再重试 */
static void fs_init(void)
{
#ifdef RT_USING_FAL
    fal_init();
    if (fal_blk_device_create(FONT_PART_NAME) == RT_NULL)
    {
        rt_kprintf("[FS] create blk device for '%s' failed\n", FONT_PART_NAME);
        return;
    }
#endif

    if (dfs_mount(FONT_PART_NAME, FS_MOUNT_POINT, "elm", 0, 0) == 0)
    {
        rt_kprintf("[FS] mount %s -> %s ok\n", FONT_PART_NAME, FS_MOUNT_POINT);
        return;
    }

    rt_kprintf("[FS] first mount failed, formatting %s ...\n", FONT_PART_NAME);
    dfs_mkfs("elm", FONT_PART_NAME);

    if (dfs_mount(FONT_PART_NAME, FS_MOUNT_POINT, "elm", 0, 0) == 0)
        rt_kprintf("[FS] mount %s -> %s ok (after format)\n", FONT_PART_NAME, FS_MOUNT_POINT);
    else
        rt_kprintf("[FS] mount %s failed\n", FONT_PART_NAME);
}

/* 追加写入 Data.txt：Temp: XX ; Humi: XX ; Count: N */
static void save_to_file(float temp, float humi)
{
    int fd;
    char buf[128];

    fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0)
    {
        rt_kprintf("[FS] open %s failed\n", DATA_FILE);
        return;
    }

    rt_sprintf(buf,
               "Temp: %d.%d ; Humi: %d.%d ; Count: %d\n",
               (int)temp, (int)(temp * 10) % 10,
               (int)humi, (int)(humi * 10) % 10,
               collect_count);

    write(fd, buf, rt_strlen(buf));   /* 只写实际长度，防垃圾字节 */
    close(fd);                        /* ★close 触发 flush，数据真正落盘 */
    rt_kprintf("[FS] saved: %s", buf);
}
#endif /* RT_USING_DFS */

/* ======================== 主采集线程 ======================== */

static void sensor_thread(void *p)
{
    (void)p;
    float temp = 0.0f, humi = 0.0f;

    /* 等网络就绪（WiFi 连接 + DHCP 需要时间） */
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
#endif

#ifdef PKG_USING_KAWAII_MQTT
    while (mqtt_init() != 0)
        rt_thread_mdelay(3000);       /* 连不上重试 */
#else
    rt_kprintf("[APP] kawaii-mqtt not enabled!\n");
#endif

    while (1)
    {
        collect_count++;              /* 自上电起采集次数 */

#ifdef PKG_USING_AHT10
        temp = aht10_read_temperature(aht10_dev);
        humi = aht10_read_humidity(aht10_dev);
#endif
        rt_kprintf("[DATA] Temp: %d.%d, Humi: %d.%d, Count: %d\n",
                   (int)temp, (int)(temp * 10) % 10,
                   (int)humi, (int)(humi * 10) % 10,
                   collect_count);

#ifdef RT_USING_DFS
        save_to_file(temp, humi);     /* 落盘：/font/Data.txt 追加 */
#endif

#ifdef PKG_USING_KAWAII_MQTT
        mqtt_pub_data(temp, humi);    /* 上云：EMQX 发布 */
#endif

        rt_thread_mdelay(COLLECT_INTERVAL_MS);
    }
}

/* ======================== MSH 启动命令 ======================== */

static void start_app(void)
{
    rt_thread_t tid;

    /* WiFi 片选上拉，避免与 W25Q64 抢 SPI2 总线（两设备共用 SPI2） */
    rt_pin_mode(PIN_WIFI_CS, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_WIFI_CS, PIN_HIGH);

    led_init();

#ifdef RT_USING_DFS
    fs_init();                        /* 挂载 font 分区 */
#else
    rt_kprintf("[APP] DFS not enabled, file logging disabled\n");
#endif

    tid = rt_thread_create("sensor", sensor_thread, RT_NULL,
                           8192, 15, 10);   /* kawaii-mqtt 栈需求大，给足 8K */
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        rt_kprintf("[APP] sensor thread started\n");
    }
    else
        rt_kprintf("[APP] create thread failed\n");
}
MSH_CMD_EXPORT(start_app, start day5 homework app);

/* 需要上电自动运行则取消下一行注释（不需要时注释掉，用 msh 手动启动） */
/* INIT_APP_EXPORT(start_app); */
