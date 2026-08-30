#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <fcntl.h>
#include <unistd.h>
#include <wlan_mgnt.h>
#include "app.h"

#define DBG_TAG "main"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ============ 可修改配置 ============ */
#ifndef WIFI_SSID
#define WIFI_SSID       "503_5G"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD   "zheng9210"
#endif

/* 采集周期（ms） */
#define SENSOR_PERIOD_MS    2000

/* Data.txt 存放路径（挂载在 font 分区上） */
#define DATA_FILE_PATH      "/font/Data.txt"

/**
 * 连接 WiFi（带超时，非阻塞式返回）。
 */
int app_wifi_connect(void)
{
    int i;

    /* WLAN 设备未就绪（RW007 模块异常/未烧固件）时直接返回，
     * 避免调用 rt_wlan_set_mode/connect 触发框架重初始化导致内核断言崩溃 */
    if (rt_wlan_is_ready() != RT_TRUE)
    {
        LOG_W("WLAN device not ready, skip WiFi connect (RW007 abnormal?)");
        return -RT_ERROR;
    }

    /* 已连接（自动连接或之前 wifi join 成功）则直接返回 */
    if (rt_wlan_is_connected() == RT_TRUE)
    {
        LOG_I("WiFi already connected");
        return RT_EOK;
    }

    LOG_I("connecting to WiFi: %s", WIFI_SSID);
    if (rt_wlan_connect(WIFI_SSID, WIFI_PASSWORD) != RT_EOK)
    {
        LOG_W("WiFi connect request failed, please run: wifi join <ssid> <password>");
        return -RT_ERROR;
    }

    /* 最多等待 15 秒 */
    for (i = 0; i < 30; i++)
    {
        if (rt_wlan_is_connected() == RT_TRUE)
        {
            LOG_I("WiFi connected");
            return RT_EOK;
        }
        rt_thread_mdelay(500);
    }

    LOG_W("WiFi connect timeout, please run: wifi join <ssid> <password>");
    return -RT_ERROR;
}

/**
 * 采样线程：周期读取温湿度 → 写文件 + 上云。
 */
static void sensor_thread_entry(void *parameter)
{
    rt_uint32_t count = 0;
    float temperature = 0.0f;
    float humidity = 0.0f;
    int fd = -1;
    char line[128];
    int len;

    (void)parameter;

    if (app_aht10_init() != RT_EOK)
    {
        LOG_E("AHT10 init failed, sensor thread exit");
        return;
    }

    /* 传感器上电后需要一段时间稳定，首次读取前等待 */
    rt_thread_mdelay(2000);

    while (1)
    {
        if (app_aht10_read(&temperature, &humidity) != RT_EOK)
        {
            LOG_E("read temperature/humidity failed");
            rt_thread_mdelay(SENSOR_PERIOD_MS);
            continue;
        }
        count++;

        /* 1) 写文件系统 /font/Data.txt（每次上电覆盖重写，Count 从上电起从 1 计数） */
        if (fd < 0)
        {
            fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0);
            if (fd < 0)
            {
                LOG_E("open %s failed, make sure /font is mounted "
                      "(first run: 'mkfs -t elm font' then reboot)", DATA_FILE_PATH);
            }
        }

        if (fd >= 0)
        {
            len = rt_snprintf(line, sizeof(line),
                              "Temp：%.1f ; Humi：%.1f ; Count： %u\n",
                              temperature, humidity, (unsigned int)count);
            write(fd, line, len);
            fsync(fd);   /* 立即落盘 */
        }

        /* 2) 上云 */
        app_mqtt_publish_sensor(temperature, humidity, count);

        LOG_I("Temp=%.1fC Humi=%.1f%% Count=%u", temperature, humidity, (unsigned int)count);

        rt_thread_mdelay(SENSOR_PERIOD_MS);
    }
}

int main(void)
{
    rt_thread_t sensor_tid;

    /* 初始化 LED */
    app_led_init();

    /* 启动采样线程（不依赖网络，本地写文件可先行） */
    sensor_tid = rt_thread_create("sensor", sensor_thread_entry, RT_NULL, 2048, 15, 20);
    if (sensor_tid != RT_NULL)
    {
        rt_thread_startup(sensor_tid);
    }

    /* 启动 MQTT 客户端线程（内部会等待网络就绪） */
    app_mqtt_start();

    /* 连接 WiFi（带超时） */
    app_wifi_connect();

    return 0;
}
