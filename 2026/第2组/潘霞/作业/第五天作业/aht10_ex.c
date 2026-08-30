#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include <fal.h>
#include <aht10.h>
#include <dfs_fs.h>
#include <dfs_posix.h>
#include <fcntl.h>
#include "mqttclient.h"

#define DBG_TAG "aht10_mqtt"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define AHT10_I2C_BUS   "i2c3"

#define MQTT_HOST       "broker.emqx.io"
#define MQTT_PORT       "1883"
#define MQTT_CLIENT_ID  "spark1_001_board"
#define MQTT_TOPIC_UP   "rsoc/spark1_001/device/up"
#define MQTT_TOPIC_DOWN "rsoc/spark1_001/device/down"

#define THREAD_STACK_SIZE 4096
#define THREAD_PRIORITY   20
#define THREAD_TIMESLICE  10

#define LED_PIN GET_PIN(F, 12)
#define FONT_PARTITION_NAME "font"
#define FONT_MOUNT_POINT    "/fal/font"
#define FONT_DATA_FILE      "/fal/font/Data.txt"

static aht10_device_t g_aht10 = RT_NULL;
static mqtt_client_t *g_mqtt = RT_NULL;
static rt_thread_t g_thread = RT_NULL;
static rt_uint32_t g_count = 0;
static struct rt_device *g_font_blk = RT_NULL;

static int font_fs_prepare(void)
{
    int ret;

    if (g_font_blk != RT_NULL)
        return RT_EOK;

    if (mkdir(FONT_MOUNT_POINT, 0) < 0)
    {
        /* 目录已存在时忽略 */
    }

    fal_init();
    g_font_blk = fal_blk_device_create(FONT_PARTITION_NAME);
    if (g_font_blk == RT_NULL)
    {
        rt_kprintf("font block device create failed\n");
        return -RT_ERROR;
    }

    ret = dfs_mount(g_font_blk->parent.name, FONT_MOUNT_POINT, "elm", 0, 0);
    if (ret != RT_EOK)
    {
        rt_kprintf("font mount failed, please mkfs manually first\n");
        return -RT_ERROR;
    }

    rt_kprintf("font partition mounted on %s\n", FONT_MOUNT_POINT);
    return RT_EOK;
}

static void save_sample_to_file(float temp, float humi, rt_uint32_t count)
{
    int fd;
    char line[128];
    int len;

    fd = open(FONT_DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
    {
        rt_kprintf("open %s failed\n", FONT_DATA_FILE);
        return;
    }

    len = rt_snprintf(line, sizeof(line),
                      "Temp：%d.%02d ; Humi：%d.%02d ; Count： %u\r\n",
                      (int)temp, (int)(temp * 100) % 100,
                      (int)humi, (int)(humi * 100) % 100,
                      count);
    if (len > 0)
    {
        write(fd, line, len);
    }

    close(fd);
}

static void mqtt_down_cb(void *client, message_data_t *msg)
{
    char buf[32] = {0};
    size_t len = msg->message->payloadlen;

    (void)client;

    if (len >= sizeof(buf))
        len = sizeof(buf) - 1;

    memcpy(buf, msg->message->payload, len);
    buf[len] = '\0';

    rt_kprintf("recv topic: %s\n", msg->topic_name);
    rt_kprintf("recv payload: %s\n", buf);

    if (!strcmp(buf, "toggle"))
    {
        rt_pin_write(LED_PIN, !rt_pin_read(LED_PIN));
    }
    else if (!strcmp(buf, "on"))
    {
        rt_pin_write(LED_PIN, PIN_LOW);
    }
    else if (!strcmp(buf, "off"))
    {
        rt_pin_write(LED_PIN, PIN_HIGH);
    }
}

static int mqtt_connect_emqx(void)
{
    g_mqtt = mqtt_lease();
    if (g_mqtt == RT_NULL)
        return -1;

    mqtt_set_host(g_mqtt, MQTT_HOST);
    mqtt_set_port(g_mqtt, MQTT_PORT);
    mqtt_set_client_id(g_mqtt, MQTT_CLIENT_ID);
    mqtt_set_clean_session(g_mqtt, 1);
    mqtt_set_version(g_mqtt, 4);
    mqtt_set_keep_alive_interval(g_mqtt, 60);
    mqtt_set_cmd_timeout(g_mqtt, 4000);
    mqtt_set_read_buf_size(g_mqtt, 1024);
    mqtt_set_write_buf_size(g_mqtt, 1024);

    if (mqtt_connect(g_mqtt) != 0)
    {
        rt_kprintf("mqtt connect failed\n");
        mqtt_release(g_mqtt);
        g_mqtt = RT_NULL;
        return -2;
    }

    if (mqtt_subscribe(g_mqtt, MQTT_TOPIC_DOWN, QOS0, mqtt_down_cb) != 0)
    {
        rt_kprintf("mqtt subscribe failed\n");
    }

    rt_kprintf("mqtt connected\n");
    return 0;
}

static void aht10_mqtt_entry(void *parameter)
{
    float temp = 0;
    float humi = 0;
    char payload[128];
    mqtt_message_t msg;

    (void)parameter;

    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN, PIN_HIGH);

    if (font_fs_prepare() != RT_EOK)
    {
        rt_kprintf("font filesystem init failed\n");
        return;
    }

    g_aht10 = aht10_init(AHT10_I2C_BUS);
    if (g_aht10 == RT_NULL)
    {
        rt_kprintf("AHT10 init failed, check i2c bus name\n");
        return;
    }

    while (mqtt_connect_emqx() != 0)
    {
        rt_thread_mdelay(3000);
    }

    while (1)
    {
        temp = aht10_read_temperature(g_aht10);
        humi = aht10_read_humidity(g_aht10);
        g_count++;

        rt_snprintf(payload, sizeof(payload),
                    "{\"temperature\":%d.%02d,\"humidity\":%d.%02d,\"count\":%u}",
                    (int)temp, (int)(temp * 100) % 100,
                    (int)humi, (int)(humi * 100) % 100,
                    g_count);

        memset(&msg, 0, sizeof(msg));
        msg.qos = QOS0;
        msg.payload = payload;
        msg.payloadlen = strlen(payload);

        if (mqtt_publish(g_mqtt, MQTT_TOPIC_UP, &msg) != 0)
        {
            rt_kprintf("mqtt publish failed\n");
        }
        else
        {
            rt_kprintf("pub: %s\n", payload);
        }

        save_sample_to_file(temp, humi, g_count);
        mqtt_keep_alive(g_mqtt);
        rt_thread_mdelay(1000);
    }
}

int aht10_mqtt_start(void)
{
    g_thread = rt_thread_create("ahtmqtt",
                                aht10_mqtt_entry,
                                RT_NULL,
                                THREAD_STACK_SIZE,
                                THREAD_PRIORITY,
                                THREAD_TIMESLICE);
    if (g_thread == RT_NULL)
    {
        rt_kprintf("thread create failed\n");
        return -RT_ERROR;
    }

    rt_thread_startup(g_thread);
    return RT_EOK;
}

MSH_CMD_EXPORT(aht10_mqtt_start, start AHT10 and publish to EMQX);
