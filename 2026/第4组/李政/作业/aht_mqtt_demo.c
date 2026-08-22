/*
 * AHT 温湿度采集 + 阿里云 MQTT 上云
 *
 * 传感器线程周期读取 tm-aht10 / hm-aht10，通过互斥量共享数据；
 * MQTT 线程连接 WiFi 后上报物模型属性 CurrentTemperature / CurrentHumidity。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>
#include "data_storage.h"

#if defined(PKG_USING_ALI_IOTKIT) && defined(PKG_USING_AHT10)

#ifdef RT_WLAN_MANAGE_ENABLE
#include <dev_wlan.h>
#endif
#include <drivers/sensor_v2.h>
#include <sensor_asair_aht10.h>
#include <mqtt_api.h>

/* AHT 传感器配置 */
#define AHT_I2C_BUS_NAME            "swi2c3"
#define AHT_SENSOR_NAME             "aht10"
#define TEMP_SENSOR_DEV_NAME        "tm-aht10"
#define HUMI_SENSOR_DEV_NAME        "hm-aht10"
#define SENSOR_READ_INTERVAL_MS     1000U
#define SENSOR_INIT_WAIT_MS         500U

/* MQTT 配置 */
#define MQTT_YIELD_MS               200U
#define MQTT_PUBLISH_INTERVAL       20U
#define MQTT_TOPIC_FMT              "/sys/%s/%s/thing/event/property/post"
#define MQTT_USER_GET_FMT           "/%s/%s/user/get"
#define MQTT_PAYLOAD_BUF_SIZE       256U
#define MQTT_TOPIC_BUF_SIZE         128U

/* 物模型属性标识符 */
#define PROP_CURRENT_TEMPERATURE    "CurrentTemperature"
#define PROP_CURRENT_HUMIDITY       "CurrentHumidity"

/* 线程配置 */
#define SENSOR_THREAD_STACK_SIZE    2048U
#define MQTT_THREAD_STACK_SIZE      4096U
#define SENSOR_THREAD_PRIORITY      22U
#define MQTT_THREAD_PRIORITY        20U
#define THREAD_TIMESLICE            10U
#define WAIT_THREAD_POLL_MS         100U
#define SENSOR_THREAD_NAME          "aht_sns"
#define MQTT_THREAD_NAME            "aht_mqtt"

typedef struct
{
    float temp;                 /* 当前温度(℃) */
    float humi;                 /* 当前湿度(%) */
    rt_bool_t valid;            /* 数据是否有效 */
} SensorShareData;

static SensorShareData g_sensorShare =
{
    0.0f, 0.0f, RT_FALSE
};
static rt_mutex_t g_pSensorShareMutex = RT_NULL;
static rt_bool_t g_demoRunning = RT_FALSE;
static rt_thread_t g_pSensorThread = RT_NULL;
static rt_thread_t g_pMqttThread = RT_NULL;

static char g_productKey[IOTX_PRODUCT_KEY_LEN + 1] = { 0 };
static char g_deviceName[IOTX_DEVICE_NAME_LEN + 1] = { 0 };

static void cleanupAhtMqttDemo(void);

extern void *HAL_Malloc(uint32_t size);
extern void HAL_Free(void *ptr);
extern void HAL_Printf(const char *fmt, ...);
extern int HAL_GetProductKey(char product_key[IOTX_PRODUCT_KEY_LEN + 1]);
extern int HAL_GetDeviceName(char device_name[IOTX_DEVICE_NAME_LEN + 1]);
extern int HAL_GetDeviceSecret(char device_secret[IOTX_DEVICE_SECRET_LEN]);
extern int HAL_Snprintf(char *str, const int len, const char *fmt, ...);

/************************************************************************
函数名   : updateSensorShare
功能描述:    更新共享温湿度数据
输入参数:    temp -- 温度
            humi -- 湿度
            valid -- 是否有效
输出参数:    无
返回值 :    无
************************************************************************/
static void updateSensorShare(float temp, float humi, rt_bool_t valid)
{
    if (RT_NULL != g_pSensorShareMutex)
    {
        if (RT_EOK == rt_mutex_take(g_pSensorShareMutex, RT_WAITING_FOREVER))
        {
            g_sensorShare.temp = temp;
            g_sensorShare.humi = humi;
            g_sensorShare.valid = valid;
            rt_mutex_release(g_pSensorShareMutex);
        }
        else
        {
            /* 互斥量获取失败，忽略本次更新 */
        }
    }
    else
    {
        /* 互斥量未创建，忽略本次更新 */
    }
}

/************************************************************************
函数名   : copySensorShare
功能描述:    拷贝共享温湿度数据
输入参数:    pOut -- 输出缓冲区
输出参数:    pOut -- 拷贝结果
返回值 :    RT_TRUE 成功，RT_FALSE 失败
************************************************************************/
static rt_bool_t copySensorShare(SensorShareData *pOut)
{
    rt_bool_t copied = RT_FALSE;

    if (RT_NULL == pOut)
    {
        copied = RT_FALSE;
    }
    else if (RT_NULL == g_pSensorShareMutex)
    {
        copied = RT_FALSE;
    }
    else if (RT_EOK != rt_mutex_take(g_pSensorShareMutex, RT_WAITING_FOREVER))
    {
        copied = RT_FALSE;
    }
    else
    {
        pOut->temp = g_sensorShare.temp;
        pOut->humi = g_sensorShare.humi;
        pOut->valid = g_sensorShare.valid;
        rt_mutex_release(g_pSensorShareMutex);
        copied = RT_TRUE;
    }

    return copied;
}

/************************************************************************
函数名   : waitThreadNameReleased
功能描述:    等待指定名称的线程被 idle 回收
输入参数:    pName -- 线程名
输出参数:    无
返回值 :    无
************************************************************************/
static void waitThreadNameReleased(const char *pName)
{
    char threadName[RT_NAME_MAX];

    if (RT_NULL != pName)
    {
        rt_strncpy(threadName, pName, sizeof(threadName));
        threadName[sizeof(threadName) - 1U] = '\0';
        while (RT_NULL != rt_thread_find(threadName))
        {
            rt_thread_mdelay(WAIT_THREAD_POLL_MS);
        }
    }
    else
    {
        /* 无效线程名，不等待 */
    }
}

/************************************************************************
函数名   : ensureAht10Init
功能描述:    确保 AHT 传感器设备已注册
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int ensureAht10Init(void)
{
    int result = RT_EOK;
    struct rt_sensor_config cfg;

    if (RT_NULL != rt_device_find(TEMP_SENSOR_DEV_NAME))
    {
        result = RT_EOK;
    }
    else
    {
        cfg.intf.dev_name = AHT_I2C_BUS_NAME;
        cfg.intf.arg = (void *)AHT10_I2C_ADDR;
        if (0 != rt_hw_aht10_init(AHT_SENSOR_NAME, &cfg))
        {
            rt_kprintf("aht10 init failed.\n");
            result = -RT_ERROR;
        }
        else
        {
            rt_thread_mdelay(SENSOR_INIT_WAIT_MS);
            if (RT_NULL == rt_device_find(TEMP_SENSOR_DEV_NAME))
            {
                rt_kprintf("temp sensor device not found.\n");
                result = -RT_ERROR;
            }
            else if (RT_NULL == rt_device_find(HUMI_SENSOR_DEV_NAME))
            {
                rt_kprintf("humi sensor device not found.\n");
                result = -RT_ERROR;
            }
            else
            {
                result = RT_EOK;
            }
        }
    }

    return result;
}

/************************************************************************
函数名   : readSensorValue
功能描述:    从指定 sensor 设备读取浮点值
输入参数:    pDevName -- 设备名
            sensorType -- 传感器类型
            pValue -- 输出值指针
输出参数:    pValue -- 读取结果
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int readSensorValue(const char *pDevName,
                           rt_uint8_t sensorType,
                           float *pValue)
{
    int result = RT_EOK;
    rt_device_t pDev = RT_NULL;
    struct rt_sensor_data data;
    rt_ssize_t readLen = 0;

    if ((RT_NULL == pDevName) || (RT_NULL == pValue))
    {
        result = -RT_ERROR;
    }
    else
    {
        pDev = rt_device_find(pDevName);
        if (RT_NULL == pDev)
        {
            result = -RT_ERROR;
        }
        else if (RT_EOK != rt_device_open(pDev, RT_DEVICE_FLAG_RDONLY))
        {
            result = -RT_ERROR;
        }
        else
        {
            readLen = rt_device_read(pDev, 0, &data, 1);
            if (1 != readLen)
            {
                result = -RT_ERROR;
            }
            else if (RT_SENSOR_TYPE_TEMP == sensorType)
            {
                *pValue = data.data.temp;
            }
            else if (RT_SENSOR_TYPE_HUMI == sensorType)
            {
                *pValue = data.data.humi;
            }
            else
            {
                result = -RT_ERROR;
            }

            rt_device_close(pDev);
        }
    }

    return result;
}

/************************************************************************
函数名   : sensorThreadEntry
功能描述:    传感器采集线程入口
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void sensorThreadEntry(void *pParameter)
{
    float temp = 0.0f;
    float humi = 0.0f;
    rt_bool_t readOk = RT_FALSE;

    RT_UNUSED(pParameter);

    while (RT_TRUE == g_demoRunning)
    {
        readOk = RT_TRUE;
        if (RT_EOK != readSensorValue(TEMP_SENSOR_DEV_NAME, RT_SENSOR_TYPE_TEMP, &temp))
        {
            readOk = RT_FALSE;
        }
        else if (RT_EOK != readSensorValue(HUMI_SENSOR_DEV_NAME, RT_SENSOR_TYPE_HUMI, &humi))
        {
            readOk = RT_FALSE;
        }
        else
        {
            /* 温湿度读取成功 */
        }

        if (RT_TRUE == readOk)
        {
            updateSensorShare(temp, humi, RT_TRUE);
            if (RT_EOK != dataStorageAppendSample(temp, humi))
            {
                /* 写文件失败不影响采集与上云 */
            }
            else
            {
                /* 数据已写入 /fal/Data.txt */
            }
            rt_kprintf("sensor: temp=%d.%02d C, humi=%d.%02d %%\n",
                       (int)temp, (int)((temp - (float)(int)temp) * 100.0f),
                       (int)humi, (int)((humi - (float)(int)humi) * 100.0f));
        }
        else
        {
            updateSensorShare(0.0f, 0.0f, RT_FALSE);
            rt_kprintf("sensor read failed.\n");
        }

        rt_thread_mdelay(SENSOR_READ_INTERVAL_MS);
    }
}

/************************************************************************
函数名   : mqttMessageArrive
功能描述:    MQTT 订阅消息到达回调
输入参数:    pContext -- 用户上下文
            pClient -- MQTT 客户端
            pMsg -- 事件消息
输出参数:    无
返回值 :    无
************************************************************************/
static void mqttMessageArrive(void *pContext,
                              void *pClient,
                              iotx_mqtt_event_msg_pt pMsg)
{
    iotx_mqtt_topic_info_t *pTopicInfo = RT_NULL;

    RT_UNUSED(pContext);
    RT_UNUSED(pClient);

    if (RT_NULL == pMsg)
    {
        /* 无效消息，忽略 */
    }
    else if (IOTX_MQTT_EVENT_PUBLISH_RECEIVED == pMsg->event_type)
    {
        pTopicInfo = (iotx_mqtt_topic_info_pt)pMsg->msg;
        if (RT_NULL != pTopicInfo)
        {
            rt_kprintf("mqtt recv topic: %.*s payload: %.*s\n",
                       pTopicInfo->topic_len, pTopicInfo->ptopic,
                       pTopicInfo->payload_len, pTopicInfo->payload);
        }
        else
        {
            /* 主题信息为空，忽略 */
        }
    }
    else
    {
        /* 其他事件类型，忽略 */
    }
}

/************************************************************************
函数名   : mqttSubscribeUserGet
功能描述:    订阅 user/get 主题
输入参数:    pClient -- MQTT 客户端
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int mqttSubscribeUserGet(void *pClient)
{
    int result = RT_EOK;
    char topic[MQTT_TOPIC_BUF_SIZE];
    int topicLen = 0;
    int subResult = 0;

    if (RT_NULL == pClient)
    {
        result = -RT_ERROR;
    }
    else
    {
        topicLen = HAL_Snprintf(topic, sizeof(topic), MQTT_USER_GET_FMT,
                                g_productKey, g_deviceName);
        if ((0 >= topicLen) || ((int)sizeof(topic) <= topicLen))
        {
            result = -RT_ERROR;
        }
        else
        {
            subResult = IOT_MQTT_Subscribe(pClient, topic, IOTX_MQTT_QOS0,
                                           mqttMessageArrive, RT_NULL);
            if (0 > subResult)
            {
                rt_kprintf("mqtt subscribe failed.\n");
                result = -RT_ERROR;
            }
            else
            {
                rt_kprintf("mqtt subscribed: %s\n", topic);
            }
        }
    }

    return result;
}

/************************************************************************
函数名   : mqttPublishProperty
功能描述:    上报温湿度物模型属性
输入参数:    pClient -- MQTT 客户端
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int mqttPublishProperty(void *pClient)
{
    int result = RT_EOK;
    char topic[MQTT_TOPIC_BUF_SIZE];
    char payload[MQTT_PAYLOAD_BUF_SIZE];
    SensorShareData shareData;
    rt_bool_t copied = RT_FALSE;
    int topicLen = 0;
    int payloadLen = 0;
    int pubResult = 0;

    if (RT_NULL == pClient)
    {
        result = -RT_ERROR;
    }
    else
    {
        copied = copySensorShare(&shareData);
        if (RT_FALSE == copied)
        {
            result = -RT_ERROR;
        }
        else if (RT_FALSE == shareData.valid)
        {
            rt_kprintf("sensor data invalid, skip publish.\n");
            result = -RT_ERROR;
        }
        else
        {
            topicLen = HAL_Snprintf(topic, sizeof(topic), MQTT_TOPIC_FMT,
                                    g_productKey, g_deviceName);
            payloadLen = HAL_Snprintf(payload, sizeof(payload),
                                      "{\"id\":\"%u\",\"version\":\"1.0\","
                                      "\"params\":{\"%s\":%.2f,\"%s\":%.2f},"
                                      "\"method\":\"thing.event.property.post\"}",
                                      (unsigned int)(rt_tick_get() % 1000000U),
                                      PROP_CURRENT_TEMPERATURE, shareData.temp,
                                      PROP_CURRENT_HUMIDITY, shareData.humi);
            if ((0 >= topicLen) || ((int)sizeof(topic) <= topicLen))
            {
                result = -RT_ERROR;
            }
            else if ((0 >= payloadLen) || ((int)sizeof(payload) <= payloadLen))
            {
                result = -RT_ERROR;
            }
            else
            {
                pubResult = IOT_MQTT_Publish_Simple(pClient, topic, IOTX_MQTT_QOS0,
                                                    payload, (int)strlen(payload));
                if (0 > pubResult)
                {
                    rt_kprintf("mqtt publish failed, res=%d\n", pubResult);
                    result = -RT_ERROR;
                }
                else
                {
                    rt_kprintf("mqtt publish: temp=%.2f, humi=%.2f\n",
                               shareData.temp, shareData.humi);
                }
            }
        }
    }

    return result;
}

/************************************************************************
函数名   : mqttThreadEntry
功能描述:    MQTT 上云线程入口
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void mqttThreadEntry(void *pParameter)
{
    void *pClient = RT_NULL;
    iotx_mqtt_param_t mqttParams;
    rt_uint32_t loopCnt = 0U;

    RT_UNUSED(pParameter);

    HAL_GetProductKey(g_productKey);
    HAL_GetDeviceName(g_deviceName);

    if (RT_TRUE != rt_wlan_is_connected())
    {
        rt_kprintf("mqtt thread exit: wifi not connected.\n");
    }
    else
    {
        rt_memset(&mqttParams, 0, sizeof(mqttParams));
        pClient = IOT_MQTT_Construct(&mqttParams);
        if (RT_NULL == pClient)
        {
            rt_kprintf("mqtt construct failed.\n");
        }
        else
        {
            rt_kprintf("mqtt connected to aliyun.\n");
            if (RT_EOK == mqttSubscribeUserGet(pClient))
            {
                /* 订阅成功，继续上报 */
            }
            else
            {
                /* 订阅失败不影响属性上报 */
            }

            while (RT_TRUE == g_demoRunning)
            {
                if (0U == (loopCnt % MQTT_PUBLISH_INTERVAL))
                {
                    mqttPublishProperty(pClient);
                }
                else
                {
                    /* 未到上报周期 */
                }

                IOT_MQTT_Yield(pClient, MQTT_YIELD_MS);
                loopCnt++;
            }

            IOT_MQTT_Destroy(&pClient);
        }
    }

    g_demoRunning = RT_FALSE;
    waitThreadNameReleased(SENSOR_THREAD_NAME);
    cleanupAhtMqttDemo();
}

/************************************************************************
函数名   : cleanupAhtMqttDemo
功能描述:    清理 demo 资源
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void cleanupAhtMqttDemo(void)
{
    if (RT_NULL != g_pSensorShareMutex)
    {
        rt_mutex_delete(g_pSensorShareMutex);
        g_pSensorShareMutex = RT_NULL;
    }
    else
    {
        /* 互斥量未创建，无需删除 */
    }

    g_pSensorThread = RT_NULL;
    g_pMqttThread = RT_NULL;
    g_demoRunning = RT_FALSE;
}

/************************************************************************
函数名   : ahtMqttStart
功能描述:    启动温湿度采集与 MQTT 上云 demo
输入参数:    无
输出参数:    无
返回值 :    0 成功，-1 失败
************************************************************************/
static int ahtMqttStart(void)
{
    int result = 0;

    if (RT_TRUE == g_demoRunning)
    {
        rt_kprintf("aht mqtt demo already running.\n");
        result = -1;
    }
    else
    {
        waitThreadNameReleased(SENSOR_THREAD_NAME);
        waitThreadNameReleased(MQTT_THREAD_NAME);
        cleanupAhtMqttDemo();

        if (RT_EOK != ensureAht10Init())
        {
            result = -1;
        }
        else
        {
            g_pSensorShareMutex = rt_mutex_create("aht_mtx", RT_IPC_FLAG_PRIO);
            if (RT_NULL == g_pSensorShareMutex)
            {
                rt_kprintf("create sensor mutex failed.\n");
                result = -1;
            }
            else
            {
                g_demoRunning = RT_TRUE;
                updateSensorShare(0.0f, 0.0f, RT_FALSE);
                if (RT_EOK != dataStorageSessionStart())
                {
                    rt_kprintf("data storage init failed, file log disabled.\n");
                }
                else
                {
                    /* 文件存储会话已启动，Count 从 1 开始 */
                }

                g_pSensorThread = rt_thread_create(SENSOR_THREAD_NAME,
                                                   sensorThreadEntry,
                                                   RT_NULL,
                                                   SENSOR_THREAD_STACK_SIZE,
                                                   SENSOR_THREAD_PRIORITY,
                                                   THREAD_TIMESLICE);
                if (RT_NULL == g_pSensorThread)
                {
                    rt_kprintf("create sensor thread failed.\n");
                    result = -1;
                }
                else
                {
                    g_pMqttThread = rt_thread_create(MQTT_THREAD_NAME,
                                                     mqttThreadEntry,
                                                     RT_NULL,
                                                     MQTT_THREAD_STACK_SIZE,
                                                     MQTT_THREAD_PRIORITY,
                                                     THREAD_TIMESLICE);
                    if (RT_NULL == g_pMqttThread)
                    {
                        rt_kprintf("create mqtt thread failed.\n");
                        result = -1;
                    }
                    else
                    {
                        rt_thread_startup(g_pSensorThread);
                        rt_thread_startup(g_pMqttThread);
                        rt_kprintf("aht mqtt demo started.\n");
                        result = 0;
                    }
                }
            }
        }
    }

    if (0 != result)
    {
        g_demoRunning = RT_FALSE;
        cleanupAhtMqttDemo();
    }
    else
    {
        /* 启动成功 */
    }

    return result;
}

int aht_mqtt_start(void)
{
    return ahtMqttStart();
}

MSH_CMD_EXPORT(aht_mqtt_start, start aht mqtt cloud demo);

#endif /* PKG_USING_ALI_IOTKIT && PKG_USING_AHT10 */
