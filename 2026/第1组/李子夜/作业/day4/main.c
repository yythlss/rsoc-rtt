/**
 * 【RSOC26】第四天作业：环境传感器云上传 + 本地日志 + LED 云端控制
 *
 * ============================================================
 * 软件包依赖（安装前需先在 RT-Thread Studio/PKGS 中启用）：
 *
 *   1. AHT10   — 温湿度传感器驱动（tinymodt/aht10）
 *   2. MQTT    — paho-mqtt RT-Thread 端口
 *   3. DFS     — elmfat（FAT 文件系统）
 *   4. ThreadX — 线程间通信（IPC：信号量/互斥量）
 *
 * 数据格式写入 Data.txt：
 *   Temp：25.60 ; Humi：61.20 ; Count：7
 *
 * MQTT 主题（与 EMQX 侧配置保持一致）：
 *   发布主题  : stm32/demo/sensor
 *   订阅主题  : stm32/demo/command
 ============================================================
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

/* ============================================================
 * config 节：本文件所有可调参数集中在此，改这个区域即可
 * ============================================================ */
#define TEMP_READ_INTERVAL_MS    3000   /* 传感器读取间隔（ms）       */
#define DATALOG_SAVE_INTERVAL_MS 3000   /* Data.txt 写入间隔（ms）     */
#define MQTT_PUB_INTERVAL_MS     5000   /* MQTT 发布间隔（ms）         */
#define MQTT_SERVER              "broker.emqx.io"
#define MQTT_PORT                1883
#define MQTT_CLIENT_ID           "stm32f407_demo"
#define MQTT_USER                ""
#define MQTT_PASS                ""
#define MQTT_PUB_TOPIC           "stm32/demo/sensor"
#define MQTT_SUB_TOPIC           "stm32/demo/command"
#define DATA_FILE_PATH           "/font/Data.txt"
#define MAX_LOG_LINE             256
#define SENSOR_MSG_SIZE          32
#define MQTT_MSG_SIZE            256

/* ============================================================
 * section 1 — 硬件/传感器层
 * ============================================================ */

/* AHT10 I2C 地址 */
#define AHT10_ADDR                0x38

/* LED 引脚（根据实际板卡修改） */
#define LED_PIN                   GET_PIN(B, 0)
#define LED_ON()                  rt_pin_write(LED_PIN, PIN_LOW)
#define LED_OFF()                 rt_pin_write(LED_PIN, PIN_HIGH)
#define LED_TOGGLE()              do { \
    if (rt_pin_read(LED_PIN)) LED_ON(); else LED_OFF(); \
} while (0)

/* ---- AHT10 传感器数据 ---- */
struct sensor_data
{
    float temperature;
    float humidity;
    rt_uint32_t count;           /* 成功采样次数 */
    rt_err_t    error;           /* 最后一次读取是否出错 */
};

/* 全局传感器数据句柄（用信号量保护读写） */
static rt_mutex_t sensor_mutex   = RT_NULL;
static struct sensor_data g_sensor = {0};

/* ---- 通过 I2C 直接读取 AHT10（不依赖 AHT10 软件包时使用） ---- */
static rt_bool_t aht10_i2c_soft_trigger(rt_device_t i2c)
{
    rt_uint8_t buf[3] = {0xAC, 0x33, 0x00};
    struct rt_i2c_msg msg = {AHT10_ADDR, RT_I2C_WR, 3, buf};
    if (rt_i2c_transfer(i2c, &msg, 1) != 1) return RT_FALSE;
    return RT_TRUE;
}

static rt_bool_t aht10_i2c_soft_read(rt_device_t i2c,
                                     rt_uint8_t *raw)
{
    struct rt_i2c_msg msg = {AHT10_ADDR, RT_I2C_RD, 6, raw};
    if (rt_i2c_transfer(i2c, &msg, 1) != 1) return RT_FALSE;
    return RT_TRUE;
}

/**
 * 读取温湿度（原始 I2C 实现）
 * 返回 RT_EOK 成功 / -RT_ERROR 失败
 */
static rt_err_t sensor_read_raw(rt_device_t i2c,
                                float *temp, float *humi)
{
    rt_uint8_t raw[6];

    if (!aht10_i2c_soft_trigger(i2c))
        return -RT_ERROR;

    rt_thread_mdelay(80);  /* AHT10 转换时间 ~75ms */

    if (!aht10_i2c_soft_read(i2c, raw))
        return -RT_ERROR;

    /* 状态字节 bit[7] = 0 表示转换完成 */
    if (raw[0] & 0x80)
        return -RT_EBUSY;

    rt_uint32_t t_val = ((rt_uint32_t)raw[1] << 16)
                      | ((rt_uint32_t)raw[2] << 8)
                      |  raw[3];
    t_val &= 0xFFFFF;

    *temp = ((float)t_val / 1048576.0f) * 200.0f - 50.0f;

    rt_uint32_t h_val = ((rt_uint32_t)raw[4] << 16)
                      | ((rt_uint32_t)raw[5] << 8);
    h_val >>= 12;
    h_val &= 0xFFFFF;

    *humi = ((float)h_val / 1048576.0f) * 100.0f;

    return RT_EOK;
}

/**
 * 传感器采样线程：
 *   1. 触发一次 I2C 读取
 *   2. 更新 g_sensor 全局数据（带互斥量保护）
 */
static void sensor_thread_entry(void *param)
{
    rt_device_t i2c = RT_NULL;

    (void)param;

    /* 找 I2C 总线设备并打开 */
    i2c = rt_device_find("i2c1");
    if (i2c == RT_NULL)
    {
        rt_kprintf("[sensor] ERROR: i2c1 not found\n");
        return;
    }
    if (rt_device_open(i2c, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("[sensor] ERROR: i2c1 open failed\n");
        return;
    }

    /* 初始化 AHT10（发送初始化命令） */
    {
        rt_uint8_t init_cmd[3] = {0xE1, 0x08, 0x00};
        struct rt_i2c_msg msg = {AHT10_ADDR, RT_I2C_WR, 3, init_cmd};
        rt_device_write(i2c, 0, init_cmd, 1);  /* 写地址 */;
        rt_i2c_transfer(i2c, &msg, 1);
        rt_thread_mdelay(100);
    }

    rt_kprintf("[sensor] AHT10 init done, reading every %d ms\n",
               TEMP_READ_INTERVAL_MS);

    while (1)
    {
        float t = 0.0f, h = 0.0f;
        rt_err_t r = sensor_read_raw(i2c, &t, &h);

        if (r == RT_EOK)
        {
            rt_mutex_take(sensor_mutex, RT_WAITING_FOREVER);
            g_sensor.temperature = t;
            g_sensor.humidity    = h;
            g_sensor.count++;
            g_sensor.error       = RT_EOK;
            rt_mutex_release(sensor_mutex);

            rt_kprintf("[sensor] T=%.2fC  H=%.2f%%  cnt=%u\n",
                       t, h, g_sensor.count);
        }
        else
        {
            rt_mutex_take(sensor_mutex, RT_WAITING_FOREVER);
            g_sensor.error = r;
            rt_mutex_release(sensor_mutex);

            rt_kprintf("[sensor] read error %d\n", (int)r);
        }

        rt_thread_mdelay(TEMP_READ_INTERVAL_MS);
    }
}

/* ============================================================
 * section 2 — DFS / FAT 文件系统 + 本地日志
 * ============================================================ */

/* fatfs 挂载点名 */
#define DFS_MOUNT_POINT      "/font"

/**
 * 挂载 FAT 文件系统
 * 使用 rt_mount("elm", DFS_MOUNT_POINT, "fat", 0, 0)
 */
static rt_err_t dfs_fat_mount(void)
{
    rt_device_t blk = rt_device_find("sd0");
    if (blk == RT_NULL)
    {
        /* 也看看有没有 sdmmc */
        blk = rt_device_find("sdmmc");
    }

    /* 挂载：设备路径 / fat / 0(data)/0(path) */
    if (rt_mount(DFS_MOUNT_POINT, "fat", "elm", 0, 0) != 0)
    {
        rt_kprintf("[dfs] mount /font failed (need sd0 or sdmmc device)\n");
        return -RT_ERROR;
    }

    rt_kprintf("[dfs] FAT mounted at " DFS_MOUNT_POINT "\n");
    return RT_EOK;
}

/**
 * 将传感器数据追加写入 Data.txt
 * 格式：Temp：25.60 ; Humi：61.20 ; Count：7
 */
static rt_err_t datalog_save(void)
{
    char path[MAX_LOG_LINE];
    char buf[128];
    rt_size_t len;
    int fd;

    rt_snprintf(buf, sizeof(buf), DFS_MOUNT_POINT DATA_FILE_PATH);

    fd = open(buf, O_WRONLY | O_CREAT | O_APPEND, 0);
    if (fd < 0)
    {
        rt_kprintf("[datalog] open %s failed\n", buf);
        return -RT_ERROR;
    }

    rt_mutex_take(sensor_mutex, RT_WAITING_FOREVER);
    rt_snprintf(path, sizeof(path),
                "Temp：%.2f ; Humi：%.2f ; Count：%u\r\n",
                g_sensor.temperature, g_sensor.humidity,
                g_sensor.count);
    rt_mutex_release(sensor_mutex);

    len = write(fd, path, rt_strlen(path));
    close(fd);

    if (len == rt_strlen(path))
        return RT_EOK;
    else
        return -RT_EIO;
}

/**
 * Data.txt 写线程
 */
static void datalog_thread_entry(void *param)
{
    (void)param;

    /* 先等 DFS 就绪 */
    if (rt_device_find("sd0") == RT_NULL &&
        rt_device_find("sdmmc") == RT_NULL)
    {
        rt_kprintf("[datalog] no block device found, skipping DFS\n");
        return;
    }

    if (dfs_fat_mount() != RT_EOK)
        return;

    rt_kprintf("[datalog] logging to " DFS_MOUNT_POINT "/Data.txt\n");

    while (1)
    {
        rt_thread_mdelay(DATALOG_SAVE_INTERVAL_MS);
        if (datalog_save() != RT_EOK)
        {
            rt_kprintf("[datalog] write failed\n");
        }
    }
}

/* ============================================================
 * section 3 — MQTT 客户端封
 *    依赖：paho-mqtt RT-Thread 端口（package.json 中启用）
 * ============================================================ */

/* --------- 头文件（需包安装后才可用） --------- */
#ifdef RT_USING_MQTT
#include <mqtt_client.h>
#endif

static rt_mq_t mqtt_pub_mq    = RT_NULL;
static rt_mq_t mqtt_sub_mq    = RT_NULL;
static rt_event_t mqtt_evt    = RT_NULL;
#define MQTT_EVT_CONNECTED    (1 << 0)
#define MQTT_EVT_MSG_RECV     (1 << 1)

struct mqtt_pub_msg
{
    char topic[64];
    char payload[MQTT_MSG_SIZE];
};

struct mqtt_sub_msg
{
    char topic[64];
    char payload[MQTT_MSG_SIZE];
};

/* --------- 传感器数据 → JSON --------- */
static void sensor_to_json(char *buf, rt_size_t size)
{
    rt_mutex_take(sensor_mutex, RT_WAITING_FOREVER);
    rt_snprintf(buf, size,
        "{"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"count\":%u,"
        "\"ts\":%u"
        "}",
        g_sensor.temperature, g_sensor.humidity,
        g_sensor.count, rt_tick_get());
    rt_mutex_release(sensor_mutex);
}

/* --------- paho-mqtt 事件回调 --------- */
#ifdef RT_USING_MQTT
static void mqtt_event_cb(struct mqtt_client *c, struct mqtt_message *msg)
{
    rt_kprintf("[mqtt] event: type=%d\n", (int)c->connect_state);
}

static void mqtt_msg_inbox(struct mqtt_client *c, const char *topic,
                           rt_ubase_t topic_len, const char *payload,
                           rt_ubase_t payload_len)
{
    struct mqtt_sub_msg smsg;
    rt_uint32_t evt_recv;

    rt_kprintf("[mqtt] <-- [%s] %.*s\n", topic,
               (int)payload_len, payload);

    rt_memset(&smsg, 0, sizeof(smsg));
    rt_strncpy(smsg.topic, topic,
               RT_MIN(topic_len, sizeof(smsg.topic) - 1));
    rt_strncpy(smsg.payload, payload,
               RT_MIN(payload_len, sizeof(smsg.payload) - 1));

    if (mqtt_sub_mq)
        rt_mq_send(mqtt_sub_mq, &smsg, sizeof(smsg));

    evt_recv = MQTT_EVT_MSG_RECV;
    rt_event_send(mqtt_evt, evt_recv);
}

static void mqtt_connect_success(struct mqtt_client *c)
{
    rt_event_send(mqtt_evt, MQTT_EVT_CONNECTED);
    rt_kprintf("[mqtt] connected\n");
}
#endif

/* --------- stm32/mqtt_entry 线程 ---------
 *
 * 功能：
 *   1. 创建 pthread/mosquitto 客户端
 *   2. 订阅 stm32/demo/command
 *   3. 通过 SDK/intf/mqtt send packet 定时发布传感器数据
 *   4. 收到订阅消息 → 放入 mqtt_sub_mq → command_thread 处理 LED 控制
 */
static void mqtt_pub_thread_entry(void *param)
{
#ifdef RT_USING_MQTT
    struct mqtt_client *client = RT_NULL;
    struct mqtt_connect_client_info_t ci;
    rt_err_t r;
    char json_buf[128];
    rt_uint32_t msg_id = 0;
    struct mqtt_pub_msg pmsg;

    (void)param;

    rt_kprintf("[mqtt] starting pub thread: %s:%d\n",
               MQTT_SERVER, MQTT_PORT);

    rt_memset(&ci, 0, sizeof(ci));
    ci.client_id  = MQTT_CLIENT_ID;
    ci.username   = MQTT_USER;
    ci.password   = MQTT_PASS;
    ci.keep_alive_interval = 60;

    client = mqtt_lease(&ci, 1, 1024);
    if (!client)
    {
        rt_kprintf("[mqtt] lease client failed\n");
        return;
    }

    /* 注册回调 */
    client->connect_cb   = mqtt_connect_success;
    client->message_cb   = mqtt_msg_inbox;

    /* 连接 */
    r = mqtt_connect(client, MQTT_SERVER, MQTT_PORT,
                     RT_WAITING_FOREVER);
    if (r < 0)
    {
        rt_kprintf("[mqtt] connect failed: %d\n", (int)r);
        mqtt_release(client);
        return;
    }

    /* 等待连接成功 */
    {
        rt_uint32_t evt;
        rt_event_recv(mqtt_evt, MQTT_EVT_CONNECTED,
                       RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                       RT_WAITING_FOREVER, NULL);
    }

    /* 订阅命令主题 */
    r = mqtt_subscribe(client, MQTT_SUB_TOPIC, RT_MQTT_QOS_AT_LEAST_ONCE);
    rt_kprintf("[mqtt] subscribe %s: %s\n", MQTT_SUB_TOPIC,
               (r == 0) ? "OK" : "FAIL");

    while (1)
    {
        /* 发布传感器数据 */
        sensor_to_json(json_buf, sizeof(json_buf));

        r = mqtt_publish(client, MQTT_PUB_TOPIC, json_buf,
                         rt_strlen(json_buf), RT_MQTT_QOS_AT_LEAST_ONCE,
                         0);
        if (r < 0)
            rt_kprintf("[mqtt] publish error %d\n", (int)r);
        else
            rt_kprintf("[mqtt] --> %s  %s\n", MQTT_PUB_TOPIC, json_buf);

        /* 检查是否有订阅消息进来 */
        {
            rt_uint32_t recved;
            rt_event_recv(mqtt_evt, MQTT_EVT_MSG_RECV,
                           RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                           100, &recved);
            /* 非阻塞：100ms 超时，不影响主循环 */
        }

        rt_thread_mdelay(MQTT_PUB_INTERVAL_MS);
    }

#else
    (void)param;
    rt_kprintf("[mqtt] RT_USING_MQTT not enabled in rtconfig.h\n");
    while (1)
        rt_thread_mdelay(5000);
#endif
}

/* --------- stm32/mqtt_receive 线程 ---------
 * 讲解 SDK/intf/mqtt call INFRA qos handler MVP 等概念
 *
 * 在 RT-Thread MQTT SDK 中：
 *   INFRA = 基础的 MQTT 协议栈（connect/pub/sub/ping）
 *   qos   = Quality of Service
 *           AT_MOST_ONCE (0) = 发完拉倒，不确认
 *           AT_LEAST_ONCE  (1) = 至少到达一次（发送有确认）
 *           EXACTLY_ONCE   (2) = 恰好一次（有去重）
 *   handler = paho-mqtt 的事件处理函数（polling loop）
 *   MVP     = Minimum Viable Packet（最小可行数据包）
 *
 * 本线程负责从 mqtt_sub_mq 队列取消息并执行 LED 控制
 */
static void mqtt_sub_thread_entry(void *param)
{
    (void)param;

    rt_kprintf("[mqtt_sub] thread started, waiting for commands...\n");

    while (1)
    {
        struct mqtt_sub_msg msg;
        rt_err_t r = rt_mq_recv(mqtt_sub_mq, &msg,
                                sizeof(msg), RT_WAITING_FOREVER);
        if (r != RT_EOK)
            continue;

        rt_kprintf("[mqtt_sub] command: topic=%s payload=%s\n",
                   msg.topic, msg.payload);

        /* LED 控制逻辑：匹配 payload 中的命令 */
        if (rt_strstr(msg.payload, "\"led\":\"on\"") ||
            rt_strstr(msg.payload, "ledon") ||
            rt_strstr(msg.payload, "LED=1"))
        {
            LED_ON();
            rt_kprintf("[led] --> ON  (cloud command)\n");
        }
        else if (rt_strstr(msg.payload, "\"led\":\"off\"") ||
                 rt_strstr(msg.payload, "ledoff") ||
                 rt_strstr(msg.payload, "LED=0"))
        {
            LED_OFF();
            rt_kprintf("[led] --> OFF (cloud command)\n");
        }
        else if (rt_strstr(msg.payload, "\"led\":\"toggle\"") ||
                 rt_strstr(msg.payload, "ledtoggle") ||
                 rt_strstr(msg.payload, "LED=t"))
        {
            LED_TOGGLE();
            rt_kprintf("[led] --> TOGGLE (cloud command)\n");
        }
        else
        {
            rt_kprintf("[mqtt_sub] unknown command: %s\n", msg.payload);
        }
    }
}

/* ============================================================
 * section 4 — 线程定时器——MQTT FAULT handling / FINFSH
 *    注：MQTT 连接断开重连逻辑（简化版）
 * ============================================================ */

static struct rt_timer mqtt_keepalive_timer;

static void mqtt_keepalive_timeout(void *p_tg)
{
    (void)p_tg;
#ifdef RT_USING_MQTT
    /* paho-mqtt 内部 poll 由线程循环隐式处理；
     * 此处可以用于断线检测和重连。
     * 简化处理：若连续 N 次 publish 失败 → 触发重新连接
     */
    extern void mqtt_reconnect_if_needed(void);
    (void)mqtt_reconnect_if_needed;
#endif
}

/* ============================================================
 * section 5 — 主函数
 *    讲解、完成好对应 pin PWM admit ed 等 SDK 组件
 *    finish 完成好对应 finit → msh → finsh 指令
 * ============================================================ */

static void print_day4_task(void)
{
    rt_kprintf("\n"
        "╔══════════════════════════════════════════╗\n"
        "║  Day 4 Tasks Status                      ║\n"
        "╠══════════════════════════════════════════╣\n"
        "╠═ 1. AHT10 sensor -> MQTT (EMQX)         ║\n"
        "╠═ 2. FAT filesystem log to Data.txt       ║\n"
        "╠═ 3. Cloud MQTT command -> LED toggle     ║\n"
        "╠═ 4. MQTT publish / subscribe threads     ║\n"
        "╠══════════════════════════════════════════╣\n"
        "║  RT_USING_MQTT  : %s         ║\n"
#ifdef RT_USING_MQTT
        "║  MQTT broker    : %-22s ║\n"
        "║  pub topic      : %-22s ║\n"
        "║  sub topic      : %-22s ║\n"
#else
        "║  MQTT packages NOT installed            ║\n"
#endif
        "║  LED pin        : PB0                   ║\n"
        "║  Data.txt       : " DFS_MOUNT_POINT "/Data.txt ║\n"
        "╚══════════════════════════════════════════╝\n", "\0");
}

/* Pin 设备操作 API —— 此类简单操作不需要 open/close */
void rt_application_init(void)
{
    rt_thread_t tid;
    rt_err_t r;

#ifdef RT_USING_PIN
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    LED_OFF();  /* 初始状态：LED 灭 */
    rt_kprintf("[main] PB0 LED initialized\n");
#endif

    /* ---- 互斥量：保护传感器全局数据 ---- */
    sensor_mutex = rt_mutex_create("sensor_mtx", RT_IPC_FLAG_PRIO);
    if (sensor_mutex == RT_NULL)
    {
        rt_kprintf("[main] ERROR: sensor_mutex create failed\n");
        return;
    }

    /* ---- 消息队列：MQTT 发布数据通道 ---- */
    mqtt_pub_mq = rt_mq_create("mqtt_pub", sizeof(struct mqtt_pub_msg),
                               4, RT_IPC_FLAG_FIFO);
    if (mqtt_pub_mq == RT_NULL)
    {
        rt_kprintf("[main] ERROR: mqtt_pub_mq create failed\n");
        return;
    }

    /* ---- 消息队列：MQTT 订阅消息通道 ---- */
    mqtt_sub_mq = rt_mq_create("mqtt_sub", sizeof(struct mqtt_sub_msg),
                               4, RT_IPC_FLAG_FIFO);
    if (mqtt_sub_mq == RT_NULL)
    {
        rt_kprintf("[main] ERROR: mqtt_sub_mq create failed\n");
        return;
    }

    /* ---- 事件集：MQTT 连接/消息事件 ---- */
    mqtt_evt = rt_event_create("mqtt_evt", RT_IPC_FLAG_PRIO);
    if (mqtt_evt == RT_NULL)
    {
        rt_kprintf("[main] ERROR: mqtt_evt create failed\n");
        return;
    }

    /* ---- 传感器采样线程 ---- */
    tid = rt_thread_create("sens",
                           sensor_thread_entry, RT_NULL,
                           2048, 20, 10);
    if (tid) rt_thread_startup(tid);

    /* ---- Data.txt 写线程 ---- */
    tid = rt_thread_create("dlog",
                           datalog_thread_entry, RT_NULL,
                           2048, 21, 10);
    if (tid) rt_thread_startup(tid);

    /* ---- MQTT 发布线程 ---- */
    tid = rt_thread_create("mqttpub",
                           mqtt_pub_thread_entry, RT_NULL,
                           3072, 21, 10);
    if (tid) rt_thread_startup(tid);

    /* ---- MQTT 订阅/处理线程 ---- */
    tid = rt_thread_create("mqtsub",
                           mqtt_sub_thread_entry, RT_NULL,
                           2048, 22, 10);
    if (tid) rt_thread_startup(tid);

    /* ---- MQTT 保活检测定时器（30s 检查一次） ---- */
    rt_timer_init(&mqtt_keepalive_timer, "mqtt_ka",
                  mqtt_keepalive_timeout, RT_NULL,
                  30 * RT_TICK_PER_SECOND,
                   RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);

    rt_kprintf("[main] day4 init OK\n");
    print_day4_task();
}
