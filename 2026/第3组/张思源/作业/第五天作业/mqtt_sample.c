#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "mqttclient.h"
#include "aht10.h"
#include <fal.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <fcntl.h>

#define PIN_LED_R    GET_PIN(F, 12)

#define MQTT_HOST    "broker.emqx.io"
#define MQTT_PORT    "1883"
#define MQTT_CLIENT_ID    "rsoc-zhangsiyuan"
#define PUB_TOPIC    "rsoc/zhangsiyuan/device/up"
#define SUB_TOPIC    "rsoc/zhangsiyuan/device/down"

static mqtt_client_t *mqtt_client = RT_NULL;
static aht10_device_t aht10_dev = RT_NULL;
static rt_uint32_t count = 0;

static rt_bool_t led_state = RT_FALSE;

static void sub_callback(void *client, message_data_t *msg)
{
    rt_kprintf("recv topic: %s, payload: %.*s\n", msg->topic_name, msg->message->payloadlen, (char *)msg->message->payload);

    if (msg->message->payloadlen > 0)
    {
        char *payload = (char *)msg->message->payload;
        if (rt_strstr(payload, "toggle") || rt_strstr(payload, "led"))
        {
            led_state = !led_state;
            rt_pin_write(PIN_LED_R, led_state ? PIN_LOW : PIN_HIGH);
            rt_kprintf("LED %s\n", led_state ? "ON" : "OFF");
        }
        else if (rt_strstr(payload, "on"))
        {
            led_state = RT_TRUE;
            rt_pin_write(PIN_LED_R, PIN_LOW);
            rt_kprintf("LED ON\n");
        }
        else if (rt_strstr(payload, "off"))
        {
            led_state = RT_FALSE;
            rt_pin_write(PIN_LED_R, PIN_HIGH);
            rt_kprintf("LED OFF\n");
        }
    }
}

static int font_mount(void)
{
    extern struct rt_device *fal_blk_device_create(const char *parition_name);

    struct rt_device *font_dev = fal_blk_device_create("font");
    if (font_dev == RT_NULL)
    {
        rt_kprintf("font blk device create failed!\n");
        return -RT_ERROR;
    }

    if (dfs_mount("font", "/font", "elm", 0, 0) != 0)
    {
        dfs_mkfs("elm", "font");
        if (dfs_mount("font", "/font", "elm", 0, 0) == 0)
        {
            rt_kprintf("font partition mount success!\n");
        }
        else
        {
            rt_kprintf("font partition mount failed!\n");
            return -RT_ERROR;
        }
    }
    else
    {
        rt_kprintf("font partition already mounted!\n");
    }

    return RT_EOK;
}

static void mqtt_aht10_thread(void *parameter)
{
    float temp, humi;
    char payload_buf[128];
    mqtt_message_t msg;

    rt_thread_mdelay(10000);

    font_mount();

    mqtt_log_init();
    mqtt_client = mqtt_lease();

    mqtt_set_host(mqtt_client, MQTT_HOST);
    mqtt_set_port(mqtt_client, MQTT_PORT);
    mqtt_set_client_id(mqtt_client, MQTT_CLIENT_ID);
    mqtt_set_clean_session(mqtt_client, 1);

    if (mqtt_connect(mqtt_client) != 0)
    {
        rt_kprintf("mqtt connect failed!\n");
        return;
    }
    rt_kprintf("mqtt connect success!\n");

    mqtt_subscribe(mqtt_client, SUB_TOPIC, QOS0, sub_callback);

    aht10_dev = aht10_init("i2c3");
    if (aht10_dev == RT_NULL)
    {
        rt_kprintf("aht10 init failed!\n");
        return;
    }
    rt_kprintf("aht10 init success!\n");

    while (1)
    {
        humi = aht10_read_humidity(aht10_dev);
        temp = aht10_read_temperature(aht10_dev);

        rt_snprintf(payload_buf, sizeof(payload_buf),
                     "Temp: %d.%d ; Humi: %d.%d ; Count: %d",
                     (int)temp, (int)(temp * 10) % 10,
                     (int)humi, (int)(humi * 10) % 10,
                     ++count);

        rt_kprintf("publish: %s\n", payload_buf);

        memset(&msg, 0, sizeof(msg));
        msg.qos = QOS0;
        msg.payload = (void *)payload_buf;
        msg.payloadlen = rt_strlen(payload_buf);

        mqtt_publish(mqtt_client, PUB_TOPIC, &msg);

        int fd = open("/font/Data.txt", O_WRONLY | O_CREAT | O_APPEND, 0);
        if (fd >= 0)
        {
            write(fd, payload_buf, rt_strlen(payload_buf));
            write(fd, "\n", 1);
            close(fd);
        }
        else
        {
            rt_kprintf("open Data.txt failed!\n");
        }

        rt_thread_mdelay(5000);
    }
}

int mqtt_aht10_init(void)
{
    rt_thread_t tid;

    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);

    tid = rt_thread_create("mqtt_aht10", mqtt_aht10_thread, RT_NULL, 4096, 15, 10);
    if (tid == RT_NULL)
    {
        rt_kprintf("mqtt_aht10 thread create failed!\n");
        return -RT_ERROR;
    }

    rt_thread_startup(tid);
    return RT_EOK;
}

MSH_CMD_EXPORT(mqtt_aht10_init, mqtt_aht10_init);
