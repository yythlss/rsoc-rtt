/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-27     Leng Mengqi  Day 5: AHT10 + Kawaii-MQTT + DFS/FAL Data.txt + Cloud LED Toggle
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_gpio.h>
#include <dfs_posix.h>
#include <fal.h>
#include <aht10.h>
#include "mqttclient.h"

#define DBG_TAG "day5.app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* ========================== 硬件与引脚定义 ========================== */
/* 星火一号开发板红色 LED 引脚：PF12 */
#define LED_PIN             GET_PIN(F, 12)
/* AHT10 挂载的 I2C 总线名称 */
#define AHT10_I2C_BUS_NAME  "i2c3"
/* 数据落盘存储文件路径 */
#define DATA_FILE_PATH      "/font/Data.txt"
#define FALLBACK_FILE_PATH  "/fal/Data.txt"

/* ========================== MQTT 配置 ========================== */
#ifndef KAWAII_MQTT_HOST
#define KAWAII_MQTT_HOST        "broker.emqx.io"
#endif
#ifndef KAWAII_MQTT_PORT
#define KAWAII_MQTT_PORT        "1883"
#endif
#ifndef KAWAII_MQTT_CLIENTID
#define KAWAII_MQTT_CLIENTID    "rtthread0721"
#endif
#ifndef KAWAII_MQTT_USERNAME
#define KAWAII_MQTT_USERNAME    "rtthread"
#endif
#ifndef KAWAII_MQTT_PASSWORD
#define KAWAII_MQTT_PASSWORD    "rtthread"
#endif
#ifndef KAWAII_MQTT_SUBTOPIC
#define KAWAII_MQTT_SUBTOPIC    "rtt-sub0721"
#endif
#ifndef KAWAII_MQTT_PUBTOPIC
#define KAWAII_MQTT_PUBTOPIC    "rtt-pub0721"
#endif

/* 全局计数器：记录自上电以来的采集次数 */
static rt_uint32_t g_collect_count = 0;
static rt_thread_t g_day5_thread = RT_NULL;
static mqtt_client_t *g_mqtt_client = RT_NULL;

/* ========================== 1. LED 控制模块 ========================== */
static void led_init(void)
{
    /* 设置 PF12 引脚为输出模式，默认输出高电平（灭） */
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN, PIN_HIGH);
}

void led_toggle(void)
{
    rt_base_t current = rt_pin_read(LED_PIN);
    rt_pin_write(LED_PIN, !current);
    rt_kprintf("[LED] >> 小灯状态已翻转! 当前状态: %s (电平: %d)\n",
               (!current == PIN_LOW) ? "点亮" : "熄灭", !current);
}
MSH_CMD_EXPORT(led_toggle, Toggle board LED_R on PF12);

/* ========================== 2. 文件系统与 font 分区挂载 ========================== */
int mount_font_partition(void)
{
    extern struct rt_device *fal_blk_device_create(const char *parition_name);
    
    /* 1. 初始化 FAL Flash 抽象层 */
    fal_init();

    /* 2. 检查或创建 font 分区对应的块设备 */
    struct rt_device *font_dev = rt_device_find("font");
    if (font_dev == RT_NULL)
    {
        font_dev = fal_blk_device_create("font");
        if (font_dev == RT_NULL)
        {
            LOG_E("创建 'font' 分区块设备失败，请检查 FAL 分区表配置!");
            return -RT_ERROR;
        }
        LOG_I("成功在 'font' 分区创建块设备: %s", font_dev->parent.name);
    }

    /* 3. 尝试将 Elm-FatFS 文件系统挂载到 /font 目录 */
    if (dfs_mount("font", "/font", "elm", 0, 0) == 0)
    {
        LOG_I("文件系统已成功挂载到 '/font' 分区!");
        return RT_EOK;
    }

    /* 4. 若挂载失败（通常为首次使用未格式化），自动执行格式化并重新挂载 */
    LOG_W("'/font' 分区未检测到有效 FatFS，正在执行格式化 (mkfs)...");
    if (dfs_mkfs("elm", "font") == 0)
    {
        if (dfs_mount("font", "/font", "elm", 0, 0) == 0)
        {
            LOG_I("格式化完成，已成功将 'font' 分区挂载至 '/font'!");
            return RT_EOK;
        }
        else
        {
            LOG_E("格式化后重新挂载 '/font' 仍然失败!");
            return -RT_ERROR;
        }
    }
    else
    {
        LOG_E("对 'font' 分区执行 mkfs 格式化失败!");
        return -RT_ERROR;
    }
}
MSH_CMD_EXPORT(mount_font_partition, Mount or format font partition to /font);

/* ========================== 3. 同步落盘写入 Data.txt ========================== */
static rt_err_t write_data_to_file(float temp, float humi, rt_uint32_t count)
{
    char record_buf[128];
    int fd = -1;

    /* 严格按照作业要求的格式进行格式化 */
    /* 格式：Temp：XX ; Humi：XX ; Count： N */
    rt_snprintf(record_buf, sizeof(record_buf), "Temp：%.2f ; Humi：%.2f ; Count： %d\r\n",
                temp, humi, count);

    /* 优先追加写入 /font/Data.txt */
    fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
    {
        /* 若 /font 打开失败，尝试回退到 /fal/Data.txt */
        fd = open(FALLBACK_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    }

    if (fd >= 0)
    {
        rt_size_t len = rt_strlen(record_buf);
        rt_size_t written = write(fd, record_buf, len);
        close(fd);

        if (written == len)
        {
            rt_kprintf("[FS Log] 同步落盘成功: %s", record_buf);
            return RT_EOK;
        }
        else
        {
            LOG_E("写入文件数据不完整! 期望: %d, 实际: %d", len, written);
            return -RT_ERROR;
        }
    }
    else
    {
        LOG_E("打开落盘文件 %s 失败! 请确保 /font 或 /fal 已正确挂载。", DATA_FILE_PATH);
        return -RT_ERROR;
    }
}

/* ========================== 4. MQTT 订阅与云端指令响应 ========================== */
static void cloud_message_arrive_callback(void *client, message_data_t *msg)
{
    (void)client;
    char payload_str[128];
    rt_size_t payload_len = msg->message->payloadlen;

    if (payload_len >= sizeof(payload_str))
    {
        payload_len = sizeof(payload_str) - 1;
    }
    rt_memcpy(payload_str, msg->message->payload, payload_len);
    payload_str[payload_len] = '\0';

    rt_kprintf("\n==================================================\n");
    rt_kprintf("[MQTT Cloud] 收到云端下发指令:\n");
    rt_kprintf("  Topic  : %s\n", msg->topic_name);
    rt_kprintf("  Payload: %s\n", payload_str);
    rt_kprintf("==================================================\n");

    /* 指令匹配与 LED 翻转控制 */
    if (rt_strstr(payload_str, "toggle") || rt_strstr(payload_str, "TOGGLE") || rt_strstr(payload_str, "flip"))
    {
        led_toggle();
    }
    else if (rt_strstr(payload_str, "on") || rt_strcmp(payload_str, "1") == 0)
    {
        rt_pin_write(LED_PIN, PIN_LOW);
        rt_kprintf("[Cloud CMD] >> LED 点亮 (ON)\n");
    }
    else if (rt_strstr(payload_str, "off") || rt_strcmp(payload_str, "0") == 0)
    {
        rt_pin_write(LED_PIN, PIN_HIGH);
        rt_kprintf("[Cloud CMD] >> LED 熄灭 (OFF)\n");
    }
    else
    {
        /* 默认指令行为：收到任意控制报文均执行小灯翻转 */
        led_toggle();
    }
}

/* ========================== 5. 主任务线程：采集 + 上云 + 落盘 ========================== */
static void day5_task_entry(void *parameter)
{
    aht10_device_t aht_dev = RT_NULL;
    float temp = 0.0f;
    float humi = 0.0f;
    char mqtt_payload[128];
    mqtt_message_t pub_msg;

    LOG_I("Day5 综合应用任务启动中，等待系统网络就绪 (约 6 秒)...");
    rt_thread_mdelay(6000);

    /* 1. 初始化板载 LED */
    led_init();

    /* 2. 挂载 font 分区文件系统 */
    mount_font_partition();

    /* 每次开机复位启动新会话时，清空旧的 Data.txt，确保自本次上电起从 Count: 1 干净记录 */
    int init_fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (init_fd >= 0)
    {
        close(init_fd);
    }

    /* 3. 初始化 AHT10 传感器 */
    aht_dev = aht10_init(AHT10_I2C_BUS_NAME);
    if (aht_dev == RT_NULL)
    {
        LOG_E("AHT10 传感器初始化失败 (总线: %s)，请检查接线及 I2C 配置!", AHT10_I2C_BUS_NAME);
    }
    else
    {
        LOG_I("AHT10 传感器在 %s 总线初始化成功!", AHT10_I2C_BUS_NAME);
    }

    /* 4. 初始化并连接 Kawaii-MQTT 客户端 */
    mqtt_log_init();
    g_mqtt_client = mqtt_lease();
    if (g_mqtt_client == RT_NULL)
    {
        LOG_E("申请 MQTT 客户端句柄失败!");
        return;
    }

    mqtt_set_host(g_mqtt_client, KAWAII_MQTT_HOST);
    mqtt_set_port(g_mqtt_client, KAWAII_MQTT_PORT);
    mqtt_set_user_name(g_mqtt_client, KAWAII_MQTT_USERNAME);
    mqtt_set_password(g_mqtt_client, KAWAII_MQTT_PASSWORD);
    mqtt_set_client_id(g_mqtt_client, KAWAII_MQTT_CLIENTID);
    mqtt_set_clean_session(g_mqtt_client, 1);

    LOG_I("正在连接 MQTT 服务器: %s:%s (ClientID: %s)...",
          KAWAII_MQTT_HOST, KAWAII_MQTT_PORT, KAWAII_MQTT_CLIENTID);

    if (mqtt_connect(g_mqtt_client) == 0)
    {
        LOG_I("MQTT Broker 连接成功!");
        /* 订阅下行控制主题 */
        mqtt_subscribe(g_mqtt_client, KAWAII_MQTT_SUBTOPIC, QOS0, cloud_message_arrive_callback);
        LOG_I("已成功订阅云端指令主题: %s", KAWAII_MQTT_SUBTOPIC);
    }
    else
    {
        LOG_E("MQTT 连接失败，将在采集循环中自动重连...");
    }

    /* 5. 周期性采集、上报与落盘主循环 */
    while (1)
    {
        /* 5.0 检查 MQTT 连接状态，若断开或尚未连上则在后台持续尝试重连 */
        if (g_mqtt_client != RT_NULL && g_mqtt_client->mqtt_client_state != CLIENT_STATE_CONNECTED)
        {
            if (mqtt_connect(g_mqtt_client) == 0)
            {
                LOG_I("MQTT Broker 连接成功!");
                mqtt_subscribe(g_mqtt_client, KAWAII_MQTT_SUBTOPIC, QOS0, cloud_message_arrive_callback);
                LOG_I("已成功订阅云端指令主题: %s", KAWAII_MQTT_SUBTOPIC);
            }
        }

        if (aht_dev != RT_NULL)
        {
            temp = aht10_read_temperature(aht_dev);
            humi = aht10_read_humidity(aht_dev);
        }
        else
        {
            /* 若传感器不可用，尝试重新获取句柄 */
            aht_dev = aht10_init(AHT10_I2C_BUS_NAME);
        }

        /* 计数自增 */
        g_collect_count++;

        /* 终端实时打印 */
        rt_kprintf("\n[Sensor #%d] 温度: %.2f ℃ , 湿度: %.2f %%\n", g_collect_count, temp, humi);

        /* 5.1 同步写入文件系统 Data.txt */
        write_data_to_file(temp, humi, g_collect_count);

        /* 5.2 构造 JSON 报文并通过 MQTT 上传至 EMQX */
        rt_snprintf(mqtt_payload, sizeof(mqtt_payload),
                    "{\"temperature\":%.2f,\"humidity\":%.2f,\"count\":%d}",
                    temp, humi, g_collect_count);

        rt_memset(&pub_msg, 0, sizeof(pub_msg));
        pub_msg.qos = QOS0;
        pub_msg.payload = (void *)mqtt_payload;

        if (g_mqtt_client != RT_NULL && g_mqtt_client->mqtt_client_state == CLIENT_STATE_CONNECTED)
        {
            if (mqtt_publish(g_mqtt_client, KAWAII_MQTT_PUBTOPIC, &pub_msg) == 0)
            {
                rt_kprintf("[MQTT Pub] 成功上传至主题 [%s]: %s\n", KAWAII_MQTT_PUBTOPIC, mqtt_payload);
            }
            else
            {
                LOG_W("[MQTT Pub] 上传失败，将在下个周期重试...");
            }
        }
        else
        {
            LOG_W("[MQTT Pub] 正在等待 MQTT 连接 (请确保 WiFi 已联网: wifi join <ssid> <password>)...");
        }

        /* 延时 3 秒进行下一次采样与上报 */
        mqtt_sleep_ms(3000);
    }
}

/* ========================== 6. MSH 辅助与测试命令 ========================== */
int day5_start(void)
{
    if (g_day5_thread != RT_NULL)
    {
        rt_kprintf("Day 5 主任务已经在运行中!\n");
        return RT_EOK;
    }

    g_day5_thread = rt_thread_create("day5_app", day5_task_entry, RT_NULL, 4096, 16, 10);
    if (g_day5_thread != RT_NULL)
    {
        rt_thread_startup(g_day5_thread);
        rt_kprintf("Day 5 任务已成功启动!\n");
        return RT_EOK;
    }
    else
    {
        LOG_E("创建 Day 5 线程失败!");
        return -RT_ERROR;
    }
}
MSH_CMD_EXPORT(day5_start, Start Day5 AHT10 + MQTT + FileSystem task);

/* 查看落盘文件 Data.txt 内容 */
static void cat_data(void)
{
    char buf[64];
    int fd = open(DATA_FILE_PATH, O_RDONLY, 0);
    if (fd < 0)
    {
        fd = open(FALLBACK_FILE_PATH, O_RDONLY, 0);
    }

    if (fd < 0)
    {
        rt_kprintf("无法打开 Data.txt 文件，可能尚未落盘或分区未挂载!\n");
        return;
    }

    rt_kprintf("\n--- 查看 Data.txt 文件内容 ---\n");
    int bytes = 0;
    while ((bytes = read(fd, buf, sizeof(buf) - 1)) > 0)
    {
        buf[bytes] = '\0';
        rt_kprintf("%s", buf);
    }
    close(fd);
    rt_kprintf("--- Data.txt 读取结束 ---\n\n");
}
MSH_CMD_EXPORT(cat_data, Read and print Data.txt contents);

/* 清空落盘文件 Data.txt */
static void clear_data(void)
{
    unlink(DATA_FILE_PATH);
    unlink(FALLBACK_FILE_PATH);
    rt_kprintf("已删除历史 Data.txt 文件。\n");
}
MSH_CMD_EXPORT(clear_data, Delete Data.txt log file);

/* 系统开机自动启动 Day 5 任务 */
static int day5_auto_init(void)
{
    day5_start();
    return RT_EOK;
}
INIT_APP_EXPORT(day5_auto_init);
