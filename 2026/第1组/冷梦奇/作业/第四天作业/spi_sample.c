/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-26     Leng Mengqi  SPI bus-device attach and transmission sample
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stm32f4xx_hal.h>
#include <board.h>
#include "drv_spi.h"

#define DBG_TAG "spi.sample"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifdef RT_USING_SPI

#define SPI_BUS_NAME    "spi2"
#define SPI_DEV_NAME    "spi20"

/**
 * @brief 动态将 SPI 从设备挂载到 SPI2 总线上 (片选引脚 PB12)
 */
static int spi_device_attach(void)
{
    rt_err_t ret = rt_hw_spi_device_attach(SPI_BUS_NAME, SPI_DEV_NAME, GPIOB, GPIO_PIN_12);
    if (ret == RT_EOK)
    {
        LOG_I("SPI device '%s' attached to bus '%s' (CS=PB12) successfully.", SPI_DEV_NAME, SPI_BUS_NAME);
    }
    else
    {
        LOG_E("Failed to attach SPI device '%s' to bus '%s' (ret=%d)!", SPI_DEV_NAME, SPI_BUS_NAME, ret);
    }
    return ret;
}
INIT_DEVICE_EXPORT(spi_device_attach);

/**
 * @brief SPI 单字节全双工交换数据示例
 */
void spi_sample_transfer_byte(void)
{
    struct rt_spi_device *spi_dev = (struct rt_spi_device *)rt_device_find(SPI_DEV_NAME);
    if (spi_dev == RT_NULL)
    {
        rt_kprintf("[SPI] Cannot find device '%s'!\n", SPI_DEV_NAME);
        return;
    }

    /* 1. 配置 SPI 参数 (1MHz, 8bit, Mode 0, MSB) */
    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode       = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz     = 1 * 1000 * 1000;
    rt_spi_configure(spi_dev, &cfg);

    /* 2. 全双工传输 1 字节 */
    rt_uint8_t send_val = 0x9F; /* 典型 Flash JEDEC ID 命令 */
    rt_uint8_t recv_val = 0x00;
    rt_err_t ret = rt_spi_transfer(spi_dev, &send_val, &recv_val, 1);
    rt_kprintf("[SPI] rt_spi_transfer: sent 0x%02X, received 0x%02X (ret=%d)\n", send_val, recv_val, ret);
}

/**
 * @brief SPI 连续发送后接收组合时序 (例如: 发送寄存器地址后接收数据)
 */
void spi_sample_send_then_recv(void)
{
    struct rt_spi_device *spi_dev = (struct rt_spi_device *)rt_device_find(SPI_DEV_NAME);
    if (spi_dev == RT_NULL)
    {
        rt_kprintf("[SPI] Cannot find device '%s'!\n", SPI_DEV_NAME);
        return;
    }

    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode       = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz     = 1 * 1000 * 1000;
    rt_spi_configure(spi_dev, &cfg);

    rt_uint8_t cmd[4]  = {0x03, 0x00, 0x00, 0x00}; /* Flash 读数据指令 + 24位地址 */
    rt_uint8_t data[4] = {0};

    rt_err_t ret = rt_spi_send_then_recv(spi_dev, cmd, 4, data, 4);
    if (ret == RT_EOK)
    {
        rt_kprintf("[SPI] rt_spi_send_then_recv SUCCESS! Data: %02X %02X %02X %02X\n",
                   data[0], data[1], data[2], data[3]);
    }
    else
    {
        rt_kprintf("[SPI] rt_spi_send_then_recv FAILED (ret=%d)\n", ret);
    }
}

#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT(spi_sample_transfer_byte, SPI single byte transfer test);
MSH_CMD_EXPORT(spi_sample_send_then_recv, SPI send-then-recv composite transmission test);
#endif

#endif /* RT_USING_SPI */
