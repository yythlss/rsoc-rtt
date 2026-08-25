#include <rtthread.h>
#include <rtdevice.h>
#include <aht10.h>
#include <dfs_file.h>
#include <fal.h>
#include <fcntl.h>
#include <unistd.h>
#include "mqttclient.h"

#define DBG_TAG "aht10"
#define DBG_LVL DBG_LOG
#include <ulog.h>

#define PIN_LED_B    GET_PIN(F, 11)
#define PIN_LED_R    GET_PIN(F, 12)

extern mqtt_client_t *client;
extern rt_bool_t mqtt_ready;

#define DATA_FILE_PATH "/font/Data.txt"
#define FONT_PART_NAME  "font"
#define FONT_MOUNT_POINT "/font"

static rt_uint32_t data_count = 0;
static aht10_device_t aht10_dev = RT_NULL;
static rt_device_t fs_blk_dev = RT_NULL;

static void aht10_save_to_file(float temp, float humi)
{
    char buf[96];
    int fd, len;

    len = rt_snprintf(buf, sizeof(buf), "Temp：%.2f ; Humi：%.2f ; Count：%d\n",
                      temp, humi, data_count);
    if (len <= 0)
        return;

    fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0)
    {
        LOG_W("open %s failed: %d", DATA_FILE_PATH, fd);
        return;
    }

    write(fd, buf, (size_t)len);
    close(fd);
}

static void aht10_publish_mqtt(float temp, float humi)
{
    char buf[128];

    if (!mqtt_ready || client == RT_NULL)
        return;

    rt_snprintf(buf, sizeof(buf), "{\"temp\":%.2f,\"humi\":%.2f,\"count\":%d}",
                temp, humi, data_count);

    mqtt_message_t msg;
    rt_memset(&msg, 0, sizeof(msg));
    msg.qos = QOS0;
    msg.payload = (void *)buf;

    mqtt_publish(client, "aht10-send", &msg);
}

void aht10_led_toggle(void)
{
    static rt_uint8_t led_state = 0;
    led_state = !led_state;
    rt_pin_write(PIN_LED_R, led_state ? PIN_LOW : PIN_HIGH);
    LOG_I("LED toggled, state = %d", led_state);
}

static int aht10_read_entry(void *par)
{
    float temp, humi;

    aht10_dev = aht10_init("i2c3");
    if (aht10_dev == RT_NULL)
    {
        LOG_E("aht10 init fail");
        return -RT_ERROR;
    }

    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);

    while (1)
    {
        temp = aht10_read_temperature(aht10_dev);
        humi = aht10_read_humidity(aht10_dev);

        rt_kprintf("temp = %.2f humi = %.2f\n", temp, humi);

        /* 过滤传感器首次读到的无效值（如 -50.00 / 0.00），只上报有效数据 */
        if (temp > -40.0f && temp < 85.0f && humi >= 0.0f && humi <= 100.0f)
        {
            data_count++;
            aht10_publish_mqtt(temp, humi);
            aht10_save_to_file(temp, humi);
        }

        rt_thread_mdelay(2000);
    }
}

int aht10_thread_init(void)
{
    rt_thread_t tid = rt_thread_create("aht10", aht10_read_entry, RT_NULL, 4096, 10, 10);
    if (tid == RT_NULL)
    {
        LOG_E("aht10 thread create fail");
        return -RT_ERROR;
    }
    rt_thread_startup(tid);
    return RT_EOK;
}
MSH_CMD_EXPORT(aht10_thread_init, aht10 thread init);

int fs_mount_data(void)
{
    rt_err_t ret;

    fal_init();

    /* 先尝试查找已存在的块设备（drv_filesystem.c 可能已经创建了） */
    fs_blk_dev = rt_device_find(FONT_PART_NAME);
    if (fs_blk_dev == RT_NULL)
    {
        /* 设备不存在，创建新的 */
        fs_blk_dev = fal_blk_device_create(FONT_PART_NAME);
        if (fs_blk_dev == RT_NULL)
        {
            LOG_E("create %s block device failed", FONT_PART_NAME);
            return -RT_ERROR;
        }
    }
    LOG_I("%s block device ready", FONT_PART_NAME);

    /* 挂载前需要先创建挂载点目录（dfs_mount 会检查路径是否存在） */
    mkdir(FONT_MOUNT_POINT, 0);
    LOG_I("mount point %s created", FONT_MOUNT_POINT);

    /* 将 font 分区挂载到 /font 目录 */
    LOG_I("trying to mount %s to %s ...", FONT_PART_NAME, FONT_MOUNT_POINT);
    ret = dfs_mount(FONT_PART_NAME, FONT_MOUNT_POINT, "elm", 0, 0);
    if (ret != RT_EOK)
    {
        LOG_W("mount failed (ret=%d), try to format...", ret);

        LOG_I("formatting %s with elm filesystem...", FONT_PART_NAME);
        ret = dfs_mkfs("elm", FONT_PART_NAME);
        if (ret != RT_EOK)
        {
            LOG_E("mkfs failed: %d", ret);
            return ret;
        }
        LOG_I("format success, retry mount...");

        rt_thread_mdelay(100);

        ret = dfs_mount(FONT_PART_NAME, FONT_MOUNT_POINT, "elm", 0, 0);
        if (ret != RT_EOK)
        {
            LOG_E("mount after format failed: %d", ret);
            return ret;
        }
        LOG_I("format and mount success");
    }
    else
    {
        LOG_I("mount success");
    }

    return RT_EOK;
}
MSH_CMD_EXPORT(fs_mount_data, mount font partition);
