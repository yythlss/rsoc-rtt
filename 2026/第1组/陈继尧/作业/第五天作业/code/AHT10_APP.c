/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>

#include "aht10.h"
#include "../rt-thread/components/fal/inc/fal.h"
#include "../rt-thread/components/drivers/spi/sfud/inc/sfud.h"
#include "spi_flash_sfud.h"
#include "drv_spi.h"
#include "../rt-thread/components/dfs/include/dfs_fs.h"
#include "day5_storage.h"

#define DBG_TAG "day5.storage"
#define DBG_LVL DBG_LOG
#include <ulog.h>

#define AHT10_I2C_BUS_NAME     "i2c3"
#define W25Q64_SPI_BUS_NAME    "spi2"
#define W25Q64_SPI_DEVICE_NAME "spi20"
#define W25Q64_DEVICE_NAME     "W25Q64"
#define FONT_PARTITION_NAME    "font"
#define FONT_MOUNT_PATH        "/"
#define DATA_FILE_PATH         "/Data.txt"
#define W25Q64_CS_PIN          GET_PIN(B, 12)

static rt_thread_t aht10_thread = RT_NULL;
static sfud_flash_t sfud_dev = RT_NULL;
static rt_bool_t storage_ready = RT_FALSE;
static rt_bool_t storage_format_attempted = RT_FALSE;

static int w25q64_fal_init(void);
static int w25q64_fal_read(long offset, rt_uint8_t *buf, rt_size_t size);
static int w25q64_fal_write(long offset, const rt_uint8_t *buf, rt_size_t size);
static int w25q64_fal_erase(long offset, rt_size_t size);

struct fal_flash_dev w25q64 =
{
    .name = W25Q64_DEVICE_NAME,
    .addr = 0,
    .len = 8 * 1024 * 1024,
    .blk_size = 4096,
    .ops = {w25q64_fal_init, w25q64_fal_read, w25q64_fal_write, w25q64_fal_erase},
    .write_gran = 1,
};

static int w25q64_probe(void)
{
    int retry;

    if (rt_device_find(W25Q64_SPI_DEVICE_NAME) == RT_NULL)
    {
        __HAL_RCC_GPIOB_CLK_ENABLE();
        rt_pin_mode(W25Q64_CS_PIN, PIN_MODE_OUTPUT);
        rt_pin_write(W25Q64_CS_PIN, PIN_HIGH);

#if (RT_VER_NUM >= 0x50000)
        if (rt_hw_spi_device_attach(W25Q64_SPI_BUS_NAME,
                                    W25Q64_SPI_DEVICE_NAME,
                                    W25Q64_CS_PIN) != RT_EOK)
#else
        if (rt_hw_spi_device_attach(W25Q64_SPI_BUS_NAME,
                                    W25Q64_SPI_DEVICE_NAME,
                                    GPIOB,
                                    GPIO_PIN_12) != RT_EOK)
#endif
        {
            LOG_E("attach %s to %s failed", W25Q64_SPI_DEVICE_NAME, W25Q64_SPI_BUS_NAME);
            return -RT_ERROR;
        }
    }

    for (retry = 0; retry < 5; retry++)
    {
        if (rt_sfud_flash_probe(W25Q64_DEVICE_NAME, W25Q64_SPI_DEVICE_NAME) != RT_NULL)
        {
            LOG_I("W25Q64 probe success on %s", W25Q64_SPI_BUS_NAME);
            return RT_EOK;
        }
        rt_thread_mdelay(50);
    }

    LOG_E("W25Q64 probe failed");
    return -RT_ERROR;
}

static int w25q64_fal_init(void)
{
    sfud_dev = rt_sfud_flash_find_by_dev_name(W25Q64_DEVICE_NAME);
    if (sfud_dev == RT_NULL || !sfud_dev->init_ok)
    {
        return -1;
    }

    w25q64.blk_size = sfud_dev->chip.erase_gran;
    w25q64.len = sfud_dev->chip.capacity;
    return 0;
}

static int w25q64_fal_read(long offset, rt_uint8_t *buf, rt_size_t size)
{
    if (sfud_dev == RT_NULL || !sfud_dev->init_ok)
    {
        return -1;
    }

    return sfud_read(sfud_dev, w25q64.addr + offset, size, buf) == SFUD_SUCCESS ? (int)size : -1;
}

static int w25q64_fal_write(long offset, const rt_uint8_t *buf, rt_size_t size)
{
    if (sfud_dev == RT_NULL || !sfud_dev->init_ok)
    {
        return -1;
    }

    return sfud_write(sfud_dev, w25q64.addr + offset, size, buf) == SFUD_SUCCESS ? (int)size : -1;
}

static int w25q64_fal_erase(long offset, rt_size_t size)
{
    if (sfud_dev == RT_NULL || !sfud_dev->init_ok)
    {
        return -1;
    }

    return sfud_erase(sfud_dev, w25q64.addr + offset, size) == SFUD_SUCCESS ? (int)size : -1;
}

int day5_storage_init(void)
{
    int result;

    if (storage_ready)
    {
        return RT_EOK;
    }

    if (w25q64_probe() != RT_EOK)
    {
        return -RT_ERROR;
    }

    if (fal_init() <= 0)
    {
        LOG_E("FAL init failed");
        return -RT_ERROR;
    }

    if (rt_device_find(FONT_PARTITION_NAME) == RT_NULL &&
        fal_blk_device_create(FONT_PARTITION_NAME) == RT_NULL)
    {
        LOG_E("create FAL block device %s failed", FONT_PARTITION_NAME);
        return -RT_ERROR;
    }

    result = dfs_mount(FONT_PARTITION_NAME, FONT_MOUNT_PATH, "elm", 0, RT_NULL);
    if (result != RT_EOK && !storage_format_attempted)
    {
        storage_format_attempted = RT_TRUE;
        LOG_W("font mount failed, formatting once");
        if (dfs_mkfs("elm", FONT_PARTITION_NAME) != RT_EOK)
        {
            LOG_E("mkfs %s failed", FONT_PARTITION_NAME);
            return -RT_ERROR;
        }
        result = dfs_mount(FONT_PARTITION_NAME, FONT_MOUNT_PATH, "elm", 0, RT_NULL);
    }

    if (result != RT_EOK)
    {
        LOG_E("mount %s at %s failed: %d", FONT_PARTITION_NAME, FONT_MOUNT_PATH, result);
        return result;
    }

    storage_ready = RT_TRUE;
    LOG_I("font mounted at %s", FONT_MOUNT_PATH);
    return RT_EOK;
}

int day5_storage_append(float temp, float humi, rt_uint32_t count)
{
    char line[96];
    int fd;
    int len;
    int temp100 = (int)(temp * 100.0f + (temp >= 0.0f ? 0.5f : -0.5f));
    int humi100 = (int)(humi * 100.0f + 0.5f);
    int temp_abs = temp100 < 0 ? -temp100 : temp100;

    if (!storage_ready && day5_storage_init() != RT_EOK)
    {
        return -RT_ERROR;
    }

    len = rt_snprintf(line, sizeof(line),
                      "Temp: %s%d.%02d ; Humi: %d.%02d ; Count: %lu\n",
                      temp100 < 0 ? "-" : "",
                      temp_abs / 100, temp_abs % 100,
                      humi100 / 100, humi100 % 100,
                      (unsigned long)count);
    if (len <= 0 || len >= (int)sizeof(line))
    {
        return -RT_ERROR;
    }

    fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0);
    if (fd < 0)
    {
        LOG_E("open %s failed", DATA_FILE_PATH);
        return -RT_ERROR;
    }

    if (write(fd, line, (size_t)len) != len)
    {
        close(fd);
        LOG_E("write %s failed", DATA_FILE_PATH);
        return -RT_ERROR;
    }

    close(fd);
    LOG_I("saved: %s", line);
    return RT_EOK;
}

static void aht10_read_entry(void *parameter)
{
    aht10_device_t aht10 = aht10_init(AHT10_I2C_BUS_NAME);

    if (aht10 == RT_NULL)
    {
        LOG_E("AHT10 init failed on %s", AHT10_I2C_BUS_NAME);
        aht10_thread = RT_NULL;
        return;
    }

    LOG_I("AHT10 init success");
    while (1)
    {
        float temp = aht10_read_temperature(aht10);
        float humi = aht10_read_humidity(aht10);
        rt_kprintf("Temp: %.2f C ; Humi: %.2f %%\n", temp, humi);
        rt_thread_mdelay(500);
    }
}

int aht10_thread_init(void)
{
    if (aht10_thread != RT_NULL)
    {
        LOG_W("AHT10 thread is already running");
        return -RT_EBUSY;
    }

    aht10_thread = rt_thread_create("aht10_th", aht10_read_entry, RT_NULL,
                                    2048, 10, 10);
    if (aht10_thread == RT_NULL)
    {
        LOG_E("AHT10 thread create failed");
        return -RT_ERROR;
    }

    rt_thread_startup(aht10_thread);
    return RT_EOK;
}
MSH_CMD_EXPORT(aht10_thread_init, start AHT10 read-only test thread);
