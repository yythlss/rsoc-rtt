/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-23     24527        the first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <board.h>
#include <wlan_mgnt.h>
#include "mqttclient.h"

/* Wi-Fi 参数，请修改成自己的热点名称和密码 */
#define WIFI_SSID            ""
#define WIFI_PASSWORD        ""

/* MQTT 参数，公共 EMQX 的 1883 端口允许匿名连接 */
#define MQTT_URL             ""
#define MQTT_PORT            ""
#define MQTT_CLIENT_ID       ""
#define MQTT_USERNAME        ""
#define MQTT_PASSWORD        ""

/* 云端向开发板发送 LED 控制指令的主题 */
#define MQTT_LED_CONTROL_TOPIC    "rtthread/led/control"

/* 线程参数 */
#define THREAD_PRIORITY           20
#define THREAD_STACK_SIZE         4096
#define THREAD_TIMESLICE          10
#define WIFI_RETRY_COUNT          5

/*
 * 板载 LED：
 * GPIO_LED_B -> PF11
 * GPIO_LED_R -> PF12
 */
#define GPIO_LED_B                GET_PIN(F, 11)
#define GPIO_LED_R                GET_PIN(F, 12)

/*
 * RT-Spark 板载 LED 通常为低电平点亮，因此默认写高电平关闭。
 * 如果你的实际硬件为高电平点亮，可将这里改为 PIN_LOW。
 */
#define LED_DEFAULT_LEVEL         PIN_HIGH

/* LED MQTT 控制线程句柄 */
static rt_thread_t mqtt_led_thread = RT_NULL;


/**
 * @brief 初始化板载 LED GPIO。
 *
 * 将 PF11 和 PF12 设置为推挽输出模式，
 * 并设置为默认关闭状态。
 */
static void led_gpio_init(void)
{
    rt_pin_mode(GPIO_LED_B, PIN_MODE_OUTPUT);
    rt_pin_mode(GPIO_LED_R, PIN_MODE_OUTPUT);

    rt_pin_write(GPIO_LED_B, LED_DEFAULT_LEVEL);
    rt_pin_write(GPIO_LED_R, LED_DEFAULT_LEVEL);
}


/**
 * @brief 翻转指定 LED 的输出电平。
 *
 * 读取 LED 当前 GPIO 输出电平，并写入相反电平，
 * 从而实现 LED 状态翻转。
 *
 * @param pin RT-Thread PIN 编号。
 */
static void led_toggle(rt_base_t pin)
{
    rt_base_t level;

    level = rt_pin_read(pin);

    if (level == PIN_HIGH)
    {
        rt_pin_write(pin, PIN_LOW);
    }
    else
    {
        rt_pin_write(pin, PIN_HIGH);
    }
}


/**
 * @brief 连接 Wi-Fi 并等待网络就绪。
 *
 * 如果网络已经连接，则直接返回。
 * 否则最多进行 WIFI_RETRY_COUNT 次连接尝试。
 *
 * @return RT_EOK 网络连接成功。
 * @return -RT_ERROR 网络连接失败。
 */
static rt_err_t wifi_connect(void)
{
    rt_err_t result;
    int retry;
    int wait_count;

    if (rt_wlan_is_ready() == RT_TRUE)
    {
        return RT_EOK;
    }

    for (retry = 1; retry <= WIFI_RETRY_COUNT; retry++)
    {
        rt_kprintf("connect Wi-Fi %s, attempt %d/%d.\n",
                   WIFI_SSID,
                   retry,
                   WIFI_RETRY_COUNT);

        result = rt_wlan_connect(WIFI_SSID, WIFI_PASSWORD);

        if (result == RT_EOK)
        {
            /*
             * 最多等待 10 秒，
             * 等待 DHCP 获取 IP 地址。
             */
            for (wait_count = 0; wait_count < 50; wait_count++)
            {
                if (rt_wlan_is_ready() == RT_TRUE)
                {
                    rt_kprintf("Wi-Fi connected.\n");
                    return RT_EOK;
                }

                rt_thread_mdelay(200);
            }
        }

        rt_kprintf("Wi-Fi connect failed, error = %d.\n", result);
        rt_thread_mdelay(2000);
    }

    return -RT_ERROR;
}


/**
 * @brief MQTT LED 控制主题消息回调。
 *
 * 云端向 MQTT_LED_CONTROL_TOPIC 发布以下字符串：
 *
 * blue 或 B：
 *     翻转 PF11 蓝灯。
 *
 * red 或 R：
 *     翻转 PF12 红灯。
 *
 * all 或 ALL：
 *     同时翻转 PF11 和 PF12。
 *
 * MQTT Payload 不保证以 '\0' 结尾，
 * 因此先根据 payloadlen 复制到本地缓冲区后再解析。
 *
 * @param client MQTT 客户端句柄。
 * @param msg MQTT 接收到的消息数据。
 */
static void led_control_handler(void *client, message_data_t *msg)
{
    char command[32];
    rt_size_t copy_len;

    RT_UNUSED(client);

    if ((msg == RT_NULL) ||
        (msg->message == RT_NULL) ||
        (msg->message->payload == RT_NULL))
    {
        return;
    }

    copy_len = msg->message->payloadlen;

    if (copy_len >= sizeof(command))
    {
        copy_len = sizeof(command) - 1;
    }

    rt_memcpy(command,
              msg->message->payload,
              copy_len);

    command[copy_len] = '\0';

    rt_kprintf("MQTT LED command: %s\n", command);

    /*
     * 蓝灯翻转命令。
     */
    if ((rt_strcmp(command, "blue") == 0) ||
        (rt_strcmp(command, "B") == 0))
    {
        led_toggle(GPIO_LED_B);
        rt_kprintf("GPIO_LED_B PF11 toggled.\n");
    }
    /*
     * 红灯翻转命令。
     */
    else if ((rt_strcmp(command, "red") == 0) ||
             (rt_strcmp(command, "R") == 0))
    {
        led_toggle(GPIO_LED_R);
        rt_kprintf("GPIO_LED_R PF12 toggled.\n");
    }
    /*
     * 两个 LED 同时翻转。
     */
    else if ((rt_strcmp(command, "all") == 0) ||
             (rt_strcmp(command, "ALL") == 0))
    {
        led_toggle(GPIO_LED_B);
        led_toggle(GPIO_LED_R);

        rt_kprintf("GPIO_LED_B PF11 and GPIO_LED_R PF12 toggled.\n");
    }
    else
    {
        rt_kprintf("unknown LED command: %s\n", command);
    }
}


/**
 * @brief 创建、连接并订阅 MQTT LED 控制主题。
 *
 * 使用与 mqtt_aht10.c 相同的 MQTT Broker，
 * 但使用独立的 MQTT_CLIENT_ID。
 *
 * @return MQTT 客户端句柄。
 * @return RT_NULL 创建、连接或订阅失败。
 */
static mqtt_client_t *mqtt_led_client_connect(void)
{
    mqtt_client_t *client;
    int result;

    mqtt_log_init();

    client = mqtt_lease();

    if (client == RT_NULL)
    {
        rt_kprintf("create MQTT client failed.\n");
        return RT_NULL;
    }

    mqtt_set_host(client, MQTT_URL);
    mqtt_set_port(client, MQTT_PORT);
    mqtt_set_client_id(client, MQTT_CLIENT_ID);
    mqtt_set_clean_session(client, 1);

    if (MQTT_USERNAME[0] != '\0')
    {
        mqtt_set_user_name(client, MQTT_USERNAME);
    }

    if (MQTT_PASSWORD[0] != '\0')
    {
        mqtt_set_password(client, MQTT_PASSWORD);
    }

    result = mqtt_connect(client);

    if (result != KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("connect MQTT server failed, error = %d.\n",
                   result);
        return RT_NULL;
    }

    rt_kprintf("MQTT connected: %s:%s, client id: %s.\n",
               MQTT_URL,
               MQTT_PORT,
               MQTT_CLIENT_ID);

    /*
     * 订阅 LED 控制主题。
     * 收到消息后由 led_control_handler() 处理。
     */
    result = mqtt_subscribe(client,
                            MQTT_LED_CONTROL_TOPIC,
                            QOS0,
                            led_control_handler);

    if (result != KAWAII_MQTT_SUCCESS_ERROR)
    {
        rt_kprintf("subscribe '%s' failed, error = %d.\n",
                   MQTT_LED_CONTROL_TOPIC,
                   result);
        return RT_NULL;
    }

    rt_kprintf("subscribe success: %s\n",
               MQTT_LED_CONTROL_TOPIC);

    return client;
}


/**
 * @brief MQTT LED 云端控制线程入口。
 *
 * 初始化 LED GPIO，连接 Wi-Fi，
 * 随后连接 MQTT Broker 并订阅 LED 控制主题。
 *
 * MQTT 客户端内部负责接收服务器消息，
 * 收到控制命令时自动调用 led_control_handler()。
 *
 * @param parameter 未使用。
 */
static void mqtt_led_thread_entry(void *parameter)
{
    mqtt_client_t *client;

    RT_UNUSED(parameter);

    led_gpio_init();

    if (wifi_connect() != RT_EOK)
    {
        rt_kprintf("network is not ready, MQTT LED demo stopped.\n");
        goto exit;
    }

    client = mqtt_led_client_connect();

    if (client == RT_NULL)
    {
        goto exit;
    }

    rt_kprintf("MQTT LED control ready.\n");

    /*
     * Kawaii MQTT 内部线程负责 MQTT 数据接收，
     * 当前线程保持运行即可。
     */
    while (1)
    {
        rt_thread_mdelay(1000);
    }

exit:

    mqtt_led_thread = RT_NULL;
}


/**
 * @brief 启动 MQTT 云端 LED 翻转实验。
 *
 * 创建 LED 控制线程，并通过 MQTT 订阅接收云端命令。
 *
 * @return RT_EOK 线程启动成功。
 * @return -RT_EBUSY 实验已经运行。
 * @return -RT_ERROR 创建或启动线程失败。
 */
static rt_err_t mqtt_led_toggle_demo(void)
{
    rt_err_t result;

    if (mqtt_led_thread != RT_NULL)
    {
        rt_kprintf("MQTT LED toggle demo is already running.\n");
        return -RT_EBUSY;
    }

    mqtt_led_thread = rt_thread_create(
        "mqttled",
        mqtt_led_thread_entry,
        RT_NULL,
        THREAD_STACK_SIZE,
        THREAD_PRIORITY,
        THREAD_TIMESLICE);

    if (mqtt_led_thread == RT_NULL)
    {
        rt_kprintf("create MQTT LED thread failed.\n");
        return -RT_ERROR;
    }

    result = rt_thread_startup(mqtt_led_thread);

    if (result != RT_EOK)
    {
        rt_kprintf("startup MQTT LED thread failed, error = %d.\n",
                   result);

        rt_thread_delete(mqtt_led_thread);
        mqtt_led_thread = RT_NULL;

        return -RT_ERROR;
    }

    return RT_EOK;
}


/* 导出为 MSH 命令 */
MSH_CMD_EXPORT(mqtt_led_toggle_demo, control PF11 and PF12 LEDs by MQTT);

