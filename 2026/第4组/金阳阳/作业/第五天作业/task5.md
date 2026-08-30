## 一、项目概述

本工程基于 RT-Thread 操作系统运行在 STM32F407（Spark One 开发板）上，实现了完整的物联网数据采集与远程控制功能：

- **温湿度采集**：AHT10 传感器挂接在软件 I2C3 总线（PE0: SCL, PE1: SDA），每 5 秒采集一次
- **本地存储**：采集数据写入 SPI Flash (W25Q64) 上的 `/font/Data.txt` 文件
- **远程上报**：通过 MQTT 协议将数据发布到 `spark/aht10/data` 主题
- **远程控制**：订阅 `spark/aht10/cmd/led` 主题，支持 `toggle`/`on`/`off` 命令控制板载红色 LED (PF12)
- **无线通信**：通过 RW007 SPI WiFi 模块接入网络

## 二、项目结构

```
05_task/
├── applications/          # 应用层代码
│   ├── main.c             # 主程序（核心业务逻辑）
│   ├── aht10.h            # AHT10 头文件包装
│   ├── paho_mqtt.h        # MQTT 头文件包装
│   ├── MQTTPacket.h       # MQTTPacket 头文件包装
│   └── SConscript         # 构建脚本
├── board/                 # 板级配置
│   ├── board.c            # 系统时钟配置
│   ├── board.h            # 板级头文件
│   └── CubeMX_Config/     # CubeMX 配置
├── middleware_aht10/      # AHT10 传感器中间件
├── middleware_pahomqtt/   # Paho MQTT 中间件
├── middleware_rw007/      # RW007 WiFi 中间件
├── rtconfig.h             # RT-Thread 配置
└── SConstruct             # 顶层构建脚本
```

## 三、核心代码

### 3.1 主应用层 main.c（applications/main.c）

```c
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "aht10.h"
#include "paho_mqtt.h"

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define PIN_LED_R              GET_PIN(F, 12)
#define AHT10_I2C_BUS          "i2c3"

#define TELEMETRY_STACK_SIZE   2048
#define TELEMETRY_PRIORITY     25
#define TELEMETRY_TIMESLICE    5
#define TELEMETRY_PERIOD_MS    5000

#define DATA_FILE_PATH         "/font/Data.txt"
#define MQTT_DATA_TOPIC        "spark/aht10/data"
#define MQTT_LED_TOPIC         "spark/aht10/cmd/led"
#define MQTT_LED_STATUS_TOPIC  "spark/aht10/status/led"

static MQTTClient mqtt_client;
static volatile rt_bool_t mqtt_started;
static volatile rt_bool_t led_is_on;
static char mqtt_uri[128];
static char mqtt_username[64];
static char mqtt_password[64];
static char mqtt_client_id[32];

/* LED 控制：板载 RGB LED 为低电平点亮 */
static void led_set(rt_bool_t on)
{
    led_is_on = on;
    rt_pin_write(PIN_LED_R, on ? PIN_LOW : PIN_HIGH);
}

static void led_toggle(void)
{
    led_set(!led_is_on);
    LOG_I("red led: %s", led_is_on ? "on" : "off");
}

/* 数据格式化：将温湿度转为 "Time: ... ; Temp: xx.x ; Humi: xx.x ; Count: n\r\n" */
static void telemetry_format(char *buffer, rt_size_t size,
                             float temperature, float humidity,
                             rt_uint32_t count)
{
    time_t now;
    struct tm time_info;
    rt_int32_t temperature_x10 = (rt_int32_t)(temperature * 10.0f);
    rt_int32_t humidity_x10 = (rt_int32_t)(humidity * 10.0f);
    rt_int32_t temperature_abs = temperature_x10 < 0 ? -temperature_x10 : temperature_x10;
    rt_int32_t humidity_abs = humidity_x10 < 0 ? -humidity_x10 : humidity_x10;

    now = time(RT_NULL);
    localtime_r(&now, &time_info);

    rt_snprintf(buffer, size,
                "Time: %04d-%02d-%02d %02d:%02d:%02d ; Temp: %s%ld.%ld ; Humi: %s%ld.%ld ; Count: %lu\r\n",
                time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday,
                time_info.tm_hour, time_info.tm_min, time_info.tm_sec,
                temperature_x10 < 0 ? "-" : "",
                (long)(temperature_abs / 10), (long)(temperature_abs % 10),
                humidity_x10 < 0 ? "-" : "",
                (long)(humidity_abs / 10), (long)(humidity_abs % 10),
                (unsigned long)count);
}

/* 数据落盘：以追加方式写入 /font/Data.txt */
static void telemetry_store(const char *record)
{
    int fd;
    int length = rt_strlen(record);

    fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0);
    if (fd < 0)
    {
        LOG_E("open %s failed; format and mount the font partition first", DATA_FILE_PATH);
        return;
    }

    if (write(fd, record, length) != length)
    {
        LOG_E("write %s failed", DATA_FILE_PATH);
    }

    close(fd);
}

/* MQTT 文本发布工具函数 */
static void mqtt_publish_text(const char *topic, const char *text)
{
    MQTTMessage message;

    if (!mqtt_client.isconnected)
    {
        return;
    }

    rt_memset(&message, 0, sizeof(message));
    message.qos = QOS1;
    message.payload = (void *)text;
    message.payloadlen = rt_strlen(text);

    if (MQTTPublish(&mqtt_client, topic, &message) != PAHO_SUCCESS)
    {
        LOG_W("MQTT status publish failed");
    }
}

/* LED 命令回调：处理 toggle / on / off 指令 */
static void mqtt_led_command(MQTTClient *client, MessageData *message_data)
{
    const char *payload = (const char *)message_data->message->payload;
    size_t length = message_data->message->payloadlen;
    char status[16];

    (void)client;

    if (length == 6 && rt_memcmp(payload, "toggle", 6) == 0)
    {
        led_toggle();
    }
    else if (length == 2 && rt_memcmp(payload, "on", 2) == 0)
    {
        led_set(RT_TRUE);
    }
    else if (length == 3 && rt_memcmp(payload, "off", 3) == 0)
    {
        led_set(RT_FALSE);
    }
    else
    {
        LOG_W("unsupported LED command: %.*s", (int)length, payload);
        mqtt_publish_text(MQTT_LED_STATUS_TOPIC, "error: use toggle, on, or off");
        return;
    }

    rt_snprintf(status, sizeof(status), "red led: %s", led_is_on ? "on" : "off");
    LOG_I("MQTT command accepted, %s", status);
    mqtt_publish_text(MQTT_LED_STATUS_TOPIC, status);
}

/* MQTT 上线回调 */
static void mqtt_online(MQTTClient *client)
{
    (void)client;
    LOG_I("MQTT online, publish: %s, subscribe: %s", MQTT_DATA_TOPIC, MQTT_LED_TOPIC);
    mqtt_publish_text(MQTT_LED_STATUS_TOPIC, "red led: online");
}

/* 温湿度采集线程：每 5 秒读取一次并存储 / 发布 */
static void telemetry_thread_entry(void *parameter)
{
    aht10_device_t aht10;
    rt_uint32_t count = 0;

    (void)parameter;

    aht10 = aht10_init(AHT10_I2C_BUS);
    if (aht10 == RT_NULL)
    {
        LOG_E("AHT10 init failed on %s", AHT10_I2C_BUS);
        return;
    }

    while (RT_TRUE)
    {
        char record[96];
        float humidity = aht10_read_humidity(aht10);
        float temperature = aht10_read_temperature(aht10);

        count++;
        telemetry_format(record, sizeof(record), temperature, humidity, count);
        telemetry_store(record);
        LOG_I("%s", record);

        if (mqtt_started && mqtt_client.isconnected)
        {
            if (paho_mqtt_publish(&mqtt_client, QOS1, MQTT_DATA_TOPIC, record) != PAHO_SUCCESS)
            {
                LOG_W("MQTT publish failed");
            }
        }

        rt_thread_mdelay(TELEMETRY_PERIOD_MS);
    }
}

/* MSH 命令：启动 MQTT 客户端 */
static int iot_mqtt_start(int argc, char **argv)
{
    MQTTPacket_connectData connect_data = MQTTPacket_connectData_initializer;

    if (argc != 2 && argc != 4)
    {
        rt_kprintf("iot_mqtt_start <tcp://host:port> [username password]\n");
        return -RT_EINVAL;
    }

    if (mqtt_started)
    {
        rt_kprintf("MQTT client is already running.\n");
        return -RT_EBUSY;
    }

    rt_strncpy(mqtt_uri, argv[1], sizeof(mqtt_uri) - 1);
    mqtt_uri[sizeof(mqtt_uri) - 1] = '\0';
    rt_snprintf(mqtt_client_id, sizeof(mqtt_client_id), "spark%08lx",
                (unsigned long)rt_tick_get());

    rt_memset(&mqtt_client, 0, sizeof(mqtt_client));
    mqtt_client.uri = mqtt_uri;
    mqtt_client.condata = connect_data;
    mqtt_client.condata.clientID.cstring = mqtt_client_id;
    mqtt_client.condata.keepAliveInterval = 30;
    mqtt_client.condata.cleansession = 1;

    if (argc == 4)
    {
        rt_strncpy(mqtt_username, argv[2], sizeof(mqtt_username) - 1);
        mqtt_username[sizeof(mqtt_username) - 1] = '\0';
        rt_strncpy(mqtt_password, argv[3], sizeof(mqtt_password) - 1);
        mqtt_password[sizeof(mqtt_password) - 1] = '\0';
        mqtt_client.condata.username.cstring = mqtt_username;
        mqtt_client.condata.password.cstring = mqtt_password;
    }

    mqtt_client.buf_size = 1024;
    mqtt_client.readbuf_size = 1024;
    mqtt_client.buf = rt_calloc(1, mqtt_client.buf_size);
    mqtt_client.readbuf = rt_calloc(1, mqtt_client.readbuf_size);
    if (mqtt_client.buf == RT_NULL || mqtt_client.readbuf == RT_NULL)
    {
        rt_free(mqtt_client.buf);
        rt_free(mqtt_client.readbuf);
        rt_kprintf("No memory for MQTT buffers.\n");
        return -RT_ENOMEM;
    }

    mqtt_client.online_callback = mqtt_online;
    /* Paho 在断开时会释放 topicFilter，必须用堆内存 */
    mqtt_client.messageHandlers[0].topicFilter = rt_strdup(MQTT_LED_TOPIC);
    if (mqtt_client.messageHandlers[0].topicFilter == RT_NULL)
    {
        rt_free(mqtt_client.buf);
        rt_free(mqtt_client.readbuf);
        rt_kprintf("No memory for MQTT topic.\n");
        return -RT_ENOMEM;
    }
    mqtt_client.messageHandlers[0].callback = mqtt_led_command;
    mqtt_client.messageHandlers[0].qos = QOS1;

    if (paho_mqtt_start(&mqtt_client) != PAHO_SUCCESS)
    {
        rt_free(mqtt_client.messageHandlers[0].topicFilter);
        mqtt_client.messageHandlers[0].topicFilter = RT_NULL;
        rt_free(mqtt_client.buf);
        rt_free(mqtt_client.readbuf);
        rt_kprintf("MQTT client start failed.\n");
        return -RT_ERROR;
    }

    mqtt_started = RT_TRUE;
    rt_kprintf("MQTT worker started.\n");
    return RT_EOK;
}
MSH_CMD_EXPORT(iot_mqtt_start, start MQTT: iot_mqtt_start <uri> [user password]);

/* MSH 命令：查询 MQTT 状态 */
static void iot_mqtt_status(void)
{
    rt_kprintf("mqtt started=%d connected=%d\n", mqtt_started, mqtt_client.isconnected);
}
MSH_CMD_EXPORT(iot_mqtt_status, show MQTT state);

/* MSH 命令：手动切换 LED */
MSH_CMD_EXPORT(led_toggle, toggle the red LED);

/* main 函数：初始化 LED 并启动采集线程 */
int main(void)
{
    rt_thread_t telemetry_thread;

    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    led_set(RT_FALSE);

    telemetry_thread = rt_thread_create("telemetry",
                                        telemetry_thread_entry,
                                        RT_NULL,
                                        TELEMETRY_STACK_SIZE,
                                        TELEMETRY_PRIORITY,
                                        TELEMETRY_TIMESLICE);
    if (telemetry_thread == RT_NULL)
    {
        LOG_E("telemetry thread creation failed");
        return -RT_ENOMEM;
    }

    rt_thread_startup(telemetry_thread);
    return 0;
}
```

### 3.2 AHT10 传感器驱动核心代码（middleware_aht10/aht10.c.reference 关键片段）

```c
#define AHT10_ADDR 0x38              // I2C 设备地址（GND 连接）
#define AHT10_CALIBRATION_CMD 0xE1   // 校准命令
#define AHT10_NORMAL_CMD 0xA8        // 普通模式命令
#define AHT10_GET_DATA 0xAC         // 读取数据命令

/* I2C 写寄存器 */
static rt_err_t write_reg(struct rt_i2c_bus_device *bus, rt_uint8_t reg, rt_uint8_t *data)
{
    rt_uint8_t buf[3];
    buf[0] = reg;
    buf[1] = data[0];
    buf[2] = data[1];

    if (rt_i2c_master_send(bus, AHT10_ADDR, 0, buf, 3) == 3)
        return RT_EOK;
    else
        return -RT_ERROR;
}

/* I2C 读数据 */
static rt_err_t read_regs(struct rt_i2c_bus_device *bus, rt_uint8_t len, rt_uint8_t *buf)
{
    struct rt_i2c_msg msgs;
    msgs.addr = AHT10_ADDR;
    msgs.flags = RT_I2C_RD;
    msgs.buf = buf;
    msgs.len = len;

    if (rt_i2c_transfer(bus, &msgs, 1) == 1)
        return RT_EOK;
    else
        return -RT_ERROR;
}

/* 传感器初始化：先发普通命令，再发校准命令 */
static rt_err_t sensor_init(aht10_device_t dev)
{
    rt_uint8_t temp[2] = {0, 0};

    write_reg(dev->i2c, AHT10_NORMAL_CMD, temp);
    rt_thread_delay(rt_tick_from_millisecond(500));

    temp[0] = 0x08;
    temp[1] = 0x00;
    write_reg(dev->i2c, AHT10_CALIBRATION_CMD, temp);
    rt_thread_delay(rt_tick_from_millisecond(450));

    return RT_EOK;
}

/* 校准使能检测 */
static rt_uint8_t calibration_enabled(aht10_device_t dev)
{
    rt_uint8_t val = 0;
    read_regs(dev->i2c, 1, &val);

    if ((val & 0x68) == 0x08)
        return RT_EOK;
    else
        return RT_ERROR;
}

/* 读取温度（带互斥锁保护） */
static float read_hw_temperature(aht10_device_t dev)
{
    rt_uint8_t temp[6];
    float cur_temp = -50.0;
    rt_err_t result;

    RT_ASSERT(dev);

    result = rt_mutex_take(dev->lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        rt_uint8_t cmd[2] = {0, 0};
        write_reg(dev->i2c, AHT10_GET_DATA, cmd);

        result = calibration_enabled(dev);
        if (result != RT_EOK)
        {
            rt_thread_mdelay(1500);
            sensor_init(dev);
            LOG_E("The aht10 is under an abnormal status. Please try again");
        }
        else
        {
            read_regs(dev->i2c, 6, temp);
            /* 温度数据转换公式：原始值 * 200 / 2^20 - 50 */
            cur_temp = ((temp[3] & 0xf) << 16 | temp[4] << 8 | temp[5]) * 200.0 / (1 << 20) - 50;
        }
    }
    rt_mutex_release(dev->lock);

    return cur_temp;
}

/* 读取湿度（带互斥锁保护） */
static float read_hw_humidity(aht10_device_t dev)
{
    rt_uint8_t temp[6];
    float cur_humi = 0.0;
    rt_err_t result;

    RT_ASSERT(dev);

    result = rt_mutex_take(dev->lock, RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        rt_uint8_t cmd[2] = {0, 0};
        write_reg(dev->i2c, AHT10_GET_DATA, cmd);

        result = calibration_enabled(dev);
        if (result != RT_EOK)
        {
            rt_thread_mdelay(1500);
            sensor_init(dev);
            LOG_E("The aht10 is under an abnormal status. Please try again");
        }
        else
        {
            read_regs(dev->i2c, 6, temp);
            /* 湿度数据转换公式：原始值 * 100 / 2^20 */
            cur_humi = (temp[1] << 12 | temp[2] << 4 | (temp[3] & 0xf0) >> 4) * 100.0 / (1 << 20);
        }
    }
    rt_mutex_release(dev->lock);

    return cur_humi;
}

/* AHT10 设备初始化入口 */
aht10_device_t aht10_init(const char *i2c_bus_name)
{
    aht10_device_t dev;
    RT_ASSERT(i2c_bus_name);

    dev = rt_calloc(1, sizeof(struct aht10_device));
    if (dev == RT_NULL)
    {
        LOG_E("Can't allocate memory for aht10 device on '%s' ", i2c_bus_name);
        return RT_NULL;
    }

    dev->i2c = rt_i2c_bus_device_find(i2c_bus_name);
    if (dev->i2c == RT_NULL)
    {
        LOG_E("Can't find aht10 device on '%s' ", i2c_bus_name);
        rt_free(dev);
        return RT_NULL;
    }

    dev->lock = rt_mutex_create("mutex_aht10", RT_IPC_FLAG_FIFO);
    if (dev->lock == RT_NULL)
    {
        LOG_E("Can't create mutex for aht10 device on '%s' ", i2c_bus_name);
        rt_free(dev);
        return RT_NULL;
    }

    sensor_init(dev);
    return dev;
}
```

### 3.3 RW007 WiFi STM32 移植代码（rw007_stm32_port.c）

```c
#include <rtthread.h>

#ifdef RW007_USING_STM32_DRIVERS
#include <rtdevice.h>
#include <drv_spi.h>
#include <board.h>
#include <spi_wifi_rw007.h>

extern void spi_wifi_isr(int vector);

/* RW007 GPIO 初始化：复位、就绪引脚配置 */
static void rw007_gpio_init(void)
{
    rt_pin_mode(RW007_RST_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(RW007_INT_BUSY_PIN, PIN_MODE_INPUT_PULLDOWN);

    /* 复位 rw007 并配置模式 */
    rt_pin_write(RW007_RST_PIN, PIN_LOW);
    rt_thread_delay(rt_tick_from_millisecond(100));
    rt_pin_write(RW007_RST_PIN, PIN_HIGH);

    /* 等待 rw007 就绪（退出 busy 状态） */
    while(!rt_pin_read(RW007_INT_BUSY_PIN))
    {
        rt_thread_delay(5);
    }

    rt_thread_delay(rt_tick_from_millisecond(200));
    rt_pin_mode(RW007_INT_BUSY_PIN, PIN_MODE_INPUT_PULLUP);
}

/* WiFi SPI 设备初始化 */
int wifi_spi_device_init(void)
{
    char sn_version[32];
    
    GPIO_TypeDef *cs_gpiox;
    uint16_t cs_pin;

    cs_gpiox = (GPIO_TypeDef *)((rt_base_t)GPIOA + (rt_base_t)(RW007_CS_PIN / 16) * 0x0400UL);
    cs_pin = (uint16_t)(1 << RW007_CS_PIN % 16);
    
    rw007_gpio_init();
    rt_hw_spi_device_attach(RW007_SPI_BUS_NAME, "wspi", cs_gpiox, cs_pin);
    rt_hw_wifi_init("wspi");

    /* 配置 STA + AP 双模式 */
    rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
    rt_wlan_set_mode(RT_WLAN_DEVICE_AP_NAME, RT_WLAN_AP);

    rw007_sn_get(sn_version);
    rt_kprintf("\nrw007  sn: [%s]\n", sn_version);
    rw007_version_get(sn_version);
    rt_kprintf("rw007 ver: [%s]\n\n", sn_version);

    return 0;
}
INIT_APP_EXPORT(wifi_spi_device_init);

/* SPI WiFi 中断处理 */
static void int_wifi_irq(void * p)
{
    ((void)p);
    spi_wifi_isr(0);
}

void spi_wifi_hw_init(void)
{
    rt_pin_attach_irq(RW007_INT_BUSY_PIN, PIN_IRQ_MODE_FALLING, int_wifi_irq, 0);
    rt_pin_irq_enable(RW007_INT_BUSY_PIN, RT_TRUE);
}

#endif /* RW007_USING_STM32_DRIVERS */
```

### 3.4 板级时钟配置（board/board.c）

```c
#include "board.h"

/* STM32F407 系统时钟配置：HSE + PLL = 168MHz */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /* 振荡器配置：HSE + LSE + LSI */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /* 时钟分频：AHB=/1, APB1=/4, APB2=/2 */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
  /* RTC 时钟源选择 LSE */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}
```

### 3.5 应用层头文件包装（applications/*.h）

```c
/* applications/aht10.h */
#ifndef APPLICATION_AHT10_WRAPPER_H
#define APPLICATION_AHT10_WRAPPER_H
#include "../packages/aht10-latest/inc/aht10.h"
#endif

/* applications/paho_mqtt.h */
#ifndef APPLICATION_PAHO_MQTT_WRAPPER_H
#define APPLICATION_PAHO_MQTT_WRAPPER_H
#include "../middleware_pahomqtt/MQTTClient-RT/paho_mqtt.h"
#endif

/* applications/MQTTPacket.h */
#ifndef APPLICATION_MQTT_PACKET_WRAPPER_H
#define APPLICATION_MQTT_PACKET_WRAPPER_H
#include "../middleware_pahomqtt/MQTTPacket/src/MQTTPacket.h"
#endif
```

## 四、关键运行流程

### 4.1 启动流程

1. **系统时钟**：`SystemClock_Config()` 将 STM32F407 配置为 168MHz
2. **WiFi 初始化**：`INIT_APP_EXPORT(wifi_spi_device_init)` 自动注册 RW007 SPI 设备
3. **文件系统**：`filesystem_mount()` 挂载 ROMFS 与 FAL 分区（filesystem / font）
4. **主线程**：`main()` 配置 LED 引脚并启动 `telemetry` 线程

### 4.2 数据流

```
AHT10 传感器
    │ (I2C3, 每 5 秒)
    ▼
telemetry_thread
    ├──► /font/Data.txt  (本地存储)
    └──► MQTT publish    (spark/aht10/data 远程上报)
                                │
                                ▼
                    EMQX Broker → MQTTX 客户端
```

### 4.3 控制流

```
MQTTX 发布 "toggle" 到 spark/aht10/cmd/led
    │
    ▼
mqtt_led_command 回调
    │
    ▼
led_toggle() → PF12 电平翻转 → 红色 LED 切换
    │
    ▼
mqtt_publish_text() 上报状态到 spark/aht10/status/led
```

## 五、常用 MSH 命令

| 命令 | 说明 |
| --- | --- |
| `wifi join <SSID> <PASSWORD>` | 连接 WiFi |
| `wifi status` | 查询 WiFi 状态 |
| `ifconfig` | 查询网络接口 |
| `mount` | 挂载文件系统 |
| `ls /font` | 列出 /font 目录 |
| `cat /font/Data.txt` | 查看采集数据 |
| `mkfs -t elm font` | 格式化 font 分区（首次使用） |
| `iot_mqtt_start tcp://host:1883` | 启动 MQTT 客户端 |
| `iot_mqtt_start tcp://host:1883 <user> <pass>` | 启动带认证的 MQTT |
| `iot_mqtt_status` | 查看 MQTT 状态 |
| `led_toggle` | 手动切换 LED |

## 六、MQTT 主题约定

| 主题 | 方向 | Payload | 说明 |
| --- | --- | --- | --- |
| `spark/aht10/data` | 设备 → 云 | `Time: ... ; Temp: ... ; Humi: ... ; Count: ...` | 温湿度上报 |
| `spark/aht10/cmd/led` | 云 → 设备 | `toggle` / `on` / `off` | LED 控制命令 |
| `spark/aht10/status/led` | 设备 → 云 | `red led: on/off` / `red led: online` | LED 状态反馈 |

## 七、硬件资源占用

| 资源 | 引脚/总线 | 用途 |
| --- | --- | --- |
| PF12 | GPIO 输出 | 红色 LED（低电平点亮） |
| PE0 / PE1 | 软件 I2C3 | AHT10 传感器 (SCL / SDA) |
| SPI2 | 硬件 SPI | W25Q64 外部 Flash |
| SPI | RW007 CS/INT/RST | RW007 WiFi 模块 |
| LSE | 32.768kHz | RTC 时钟源 |