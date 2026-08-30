#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include <aht10.h>
#include <dfs_fs.h>
#include <fal.h>
#include <mqttclient.h>
#include <netdev_ipaddr.h>
#include <netdev.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define DBG_TAG                 "day5"
#define DBG_LVL                 DBG_INFO
#include <rtdbg.h>

#define LED_R_PIN               GET_PIN(F, 12)

static rt_thread_t day5_thread = RT_NULL;

static void mqtt_control_callback(void *client, message_data_t *msg)
{
    char command[16];
    rt_size_t length;

    RT_UNUSED(client);

    if ((msg == RT_NULL) || (msg->message == RT_NULL))
    {
        return;
    }

    length = msg->message->payloadlen;
    if ((length == 0) || (length >= sizeof(command)) ||
        (msg->message->payload == RT_NULL))
    {
        LOG_W("invalid LED command, length=%u", (unsigned int)length);
        return;
    }

    rt_memcpy(command, msg->message->payload, length);
    command[length] = '\0';

    if (rt_strcasecmp(command, "ON") == 0)
    {
        rt_pin_write(LED_R_PIN, PIN_LOW);
        LOG_I("LED ON");
    }
    else if (rt_strcasecmp(command, "OFF") == 0)
    {
        rt_pin_write(LED_R_PIN, PIN_HIGH);
        LOG_I("LED OFF");
    }
    else if (rt_strcasecmp(command, "TOGGLE") == 0)
    {
        rt_pin_write(LED_R_PIN,
                     rt_pin_read(LED_R_PIN) == PIN_LOW ? PIN_HIGH : PIN_LOW);
        LOG_I("LED TOGGLE");
    }
    else
    {
        LOG_W("unknown LED command: %s", command);
    }
}

static int mount_font_partition(void)
{
    rt_device_t device;
    const char *mounted_path;

    fal_init();

    device = rt_device_find("font");
    if (device == RT_NULL)
    {
        device = fal_blk_device_create("font");
    }
    if (device == RT_NULL)
    {
        LOG_E("create font block device failed");
        return -RT_ERROR;
    }

    mounted_path = dfs_filesystem_get_mounted_path(device);
    if (mounted_path != RT_NULL)
    {
        if (rt_strcmp(mounted_path, "/fal") == 0)
        {
            LOG_I("font partition is already mounted at /fal");
            return RT_EOK;
        }

        LOG_E("font partition is mounted at %s", mounted_path);
        return -RT_ERROR;
    }

    if (dfs_mount("font", "/fal", "elm", 0, RT_NULL) == RT_EOK)
    {
        LOG_I("font partition mounted at /fal");
        return RT_EOK;
    }

    LOG_W("formatting font partition");
    if (dfs_mkfs("elm", "font") != RT_EOK)
    {
        LOG_E("format font partition failed");
        return -RT_ERROR;
    }

    if (dfs_mount("font", "/fal", "elm", 0, RT_NULL) != RT_EOK)
    {
        LOG_E("mount font partition failed");
        return -RT_ERROR;
    }

    LOG_I("font partition formatted and mounted at /fal");
    return RT_EOK;
}

static rt_bool_t wifi_is_ready(void)
{
    return (netdev_default != RT_NULL) &&
           netdev_is_up(netdev_default) &&
           netdev_is_link_up(netdev_default) &&
           !ip_addr_isany(&netdev_default->ip_addr);
}

static void save_sample(float temp, float humi, rt_uint32_t count)
{
    int fd;
    int length;
    char line[96];

    length = rt_snprintf(line, sizeof(line),
                         "Temp: %.2f ; Humi: %.2f ; Count: %lu\n",
                         temp, humi, (unsigned long)count);

    fd = open("/fal/Data.txt", O_WRONLY | O_CREAT | O_APPEND, 0);
    if (fd < 0)
    {
        LOG_E("open /fal/Data.txt failed");
        return;
    }

    if (write(fd, line, length) != length)
    {
        LOG_E("write /fal/font/Data.txt failed");
    }
    close(fd);
}

static void day5_mqtt_entry(void *parameter)
{
    aht10_device_t aht10;
    mqtt_client_t *mqtt;
    mqtt_message_t message;
    rt_uint32_t count = 0;
    rt_bool_t file_ready;
    rt_bool_t mqtt_ready = RT_FALSE;

    RT_UNUSED(parameter);

    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, PIN_HIGH);

    file_ready = (mount_font_partition() == RT_EOK);

    aht10 = aht10_init("i2c3");
    while (aht10 == RT_NULL)
    {
        LOG_E("AHT10 init failed, retry after 5 seconds");
        rt_thread_mdelay(5000);
        aht10 = aht10_init("i2c3");
    }

    while (!wifi_is_ready())
    {
        LOG_W("Wi-Fi is not connected, please run wifi join");
        rt_thread_mdelay(5000);
    }

    mqtt_log_init();
    mqtt = mqtt_lease();
    if (mqtt == RT_NULL)
    {
        LOG_E("create MQTT client failed");
        return;
    }

    mqtt_set_host(mqtt, "broker.emqx.io");
    mqtt_set_port(mqtt, "1883");
    mqtt_set_client_id(mqtt, "day5_mqtt");
    mqtt_set_clean_session(mqtt, 1);

    while (1)
    {
        float temp;
        float humi;
        char json[96];
        int result;

        if (!mqtt_ready && wifi_is_ready())
        {
            result = mqtt_connect(mqtt);
            if (result == KAWAII_MQTT_SUCCESS_ERROR)
            {
                result = mqtt_subscribe(mqtt, "rtt-sublh",
                                        QOS0, mqtt_control_callback);
                if (result == KAWAII_MQTT_SUCCESS_ERROR)
                {
                    mqtt_ready = RT_TRUE;
                    LOG_I("MQTT connected and subscribed");
                }
            }

            if (!mqtt_ready)
            {
                LOG_E("MQTT connect/subscribe failed: %d", result);
            }
        }

        temp = aht10_read_temperature(aht10);
        humi = aht10_read_humidity(aht10);

        if ((temp >= -40.0f) && (temp <= 85.0f) &&
            (humi >= 0.0f) && (humi <= 100.0f))
        {
            count++;
            LOG_I("temp=%.2f, humi=%.2f, count=%lu",
                  temp, humi, (unsigned long)count);

            if (file_ready)
            {
                save_sample(temp, humi, count);
            }

            rt_snprintf(json, sizeof(json),
                        "{\"temp\":%.2f,\"humi\":%.2f,\"count\":%lu}",
                        temp, humi, (unsigned long)count);

            rt_memset(&message, 0, sizeof(message));
            message.qos = QOS0;
            message.payload = json;
            message.payloadlen = rt_strlen(json);

            result = mqtt_publish(mqtt, "rtt-publh", &message);
            if (result != KAWAII_MQTT_SUCCESS_ERROR)
            {
                LOG_E("MQTT publish failed: %d", result);
            }
        }
        else
        {
            LOG_W("invalid AHT10 data: temp=%.2f, humi=%.2f", temp, humi);
        }

        rt_thread_mdelay(5000);
    }
}

int day5_start(void)
{
    if (day5_thread != RT_NULL)
    {
        LOG_W("day5 task is already running");
        return -RT_EBUSY;
    }

    day5_thread = rt_thread_create("day5mq", day5_mqtt_entry, RT_NULL,
                                   4096, 17, 10);
    if (day5_thread == RT_NULL)
    {
        LOG_E("create day5 task failed");
        return -RT_ERROR;
    }

    rt_thread_startup(day5_thread);
    return RT_EOK;
}
MSH_CMD_EXPORT(day5_start, start day5 MQTT task);
