/*
 * Copyright (c) 2025
 *
 * 板载温湿度传感器驱动（AHT21，使用 aht10 软件包，软件 I2C3）
 *
 * 说明：
 *   本工程板载传感器为 AHT21，RT-Thread 官方 aht10 软件包兼容驱动该器件，
 *   挂载在软件模拟 I2C3（PE0 = SCL, PE1 = SDA）上。
 *
 * 变更记录:
 * 2025        first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "aht10.h"
#include "app.h"

#define DBG_TAG "aht10"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static aht10_device_t aht10_dev = RT_NULL;

/**
 * 初始化板载 AHT21 温湿度传感器。
 *
 * @return RT_EOK 成功；-RT_ERROR 失败
 */
int app_aht10_init(void)
{
    if (aht10_dev != RT_NULL)
    {
        return RT_EOK;
    }

    aht10_dev = aht10_init(PKG_AHT10_I2C_BUS_NAME);
    if (aht10_dev == RT_NULL)
    {
        LOG_E("AHT10 (AHT21) initialize failure on %s", PKG_AHT10_I2C_BUS_NAME);
        return -RT_ERROR;
    }

    LOG_I("AHT10 (AHT21) initialized on %s", PKG_AHT10_I2C_BUS_NAME);
    return RT_EOK;
}

/**
 * 读取温湿度。
 *
 * @param temperature 输出温度（℃，保留 1 位小数）
 * @param humidity    输出相对湿度（%，保留 1 位小数）
 *
 * @return RT_EOK 成功；-RT_ERROR 失败
 */
int app_aht10_read(float *temperature, float *humidity)
{
    if (aht10_dev == RT_NULL || temperature == RT_NULL || humidity == RT_NULL)
    {
        return -RT_ERROR;
    }

    *humidity    = aht10_read_humidity(aht10_dev);
    *temperature = aht10_read_temperature(aht10_dev);

    return RT_EOK;
}
