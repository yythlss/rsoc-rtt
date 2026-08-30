/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-23     24527        the first version
 * 2026-08-23     24527        mount font partition and save AHT10 data
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <fal.h>
#include <dfs_fs.h>
#include <fcntl.h>
#include <unistd.h>
#include "aht10_data.h"

#define DBG_TAG "mount.demo"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define FONT_PARTITION_NAME    "font"
#define FONT_MOUNT_PATH        "/font"
#define DATA_FILE_PATH         "/font/Data.txt"

static rt_bool_t font_mounted = RT_FALSE;
static rt_uint32_t sample_count = 0;

/**
 * @brief 接收一次温湿度采样结果并追加写入 Data.txt。
 *
 * mqtt_aht10.c 每完成一次 AHT10 采集后会调用本函数。
 * 因此同一份数据在上传 MQTT 的同时，也会保存到文件系统。
 *
 * @param temperature_x10 温度扩大 10 倍后的整数。
 * @param humidity_x10 湿度扩大 10 倍后的整数。
 */
void aht10_data_notify(int temperature_x10, int humidity_x10)
{
    char line[96];
    int temperature_decimal;
    int humidity_decimal;
    int length;
    int file;
    int write_size;

    sample_count++;

    if (font_mounted == RT_FALSE)
    {
        LOG_W("font filesystem not mounted, skip sample %u",
              (unsigned int)sample_count);
        return;
    }

    temperature_decimal = temperature_x10 % 10;
    humidity_decimal = humidity_x10 % 10;

    if (temperature_decimal < 0)
    {
        temperature_decimal = -temperature_decimal;
    }

    if (humidity_decimal < 0)
    {
        humidity_decimal = -humidity_decimal;
    }

    length = rt_snprintf(
        line,
        sizeof(line),
        "Temp: %d.%d ; Humi: %d.%d ; Count: %u\r\n",
        temperature_x10 / 10,
        temperature_decimal,
        humidity_x10 / 10,
        humidity_decimal,
        (unsigned int)sample_count);

    if ((length <= 0) || (length >= (int)sizeof(line)))
    {
        LOG_E("format data failed");
        return;
    }

    file = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0);

    if (file < 0)
    {
        LOG_E("open '%s' failed", DATA_FILE_PATH);
        return;
    }

    write_size = write(file, line, length);
    close(file);

    if (write_size != length)
    {
        LOG_E("write '%s' failed", DATA_FILE_PATH);
        return;
    }

    LOG_D("save sample %u to %s",
          (unsigned int)sample_count,
          DATA_FILE_PATH);
}

/**
 * @brief 将 W25Q64 的 font 分区挂载到 /font。
 *
 * 如果 FAL 尚未创建 font 块设备，则调用
 * fal_blk_device_create() 创建块设备，然后使用
 * Elm-FatFs 挂载到 /font。
 *
 * 注意：如果 font 分区尚未格式化，需要先在 MSH 中执行：
 * mkfs -t elm font
 *
 * @return RT_EOK 挂载成功。
 * @return -RT_ERROR 创建块设备或挂载失败。
 */
static rt_err_t font_filesystem_mount(void)
{
    rt_device_t flash_dev;
    int result;

    if (font_mounted == RT_TRUE)
    {
        return RT_EOK;
    }

    fal_init();

    flash_dev = rt_device_find(FONT_PARTITION_NAME);

    if (flash_dev == RT_NULL)
    {
        flash_dev = fal_blk_device_create(FONT_PARTITION_NAME);
    }

    if (flash_dev == RT_NULL)
    {
        LOG_E("can't create block device on '%s'",
              FONT_PARTITION_NAME);
        return -RT_ERROR;
    }

    result = dfs_mount(
        flash_dev->parent.name,
        FONT_MOUNT_PATH,
        "elm",
        0,
        RT_NULL);

    if (result != RT_EOK)
    {
        LOG_E("mount '%s' to '%s' failed, error = %d",
              FONT_PARTITION_NAME,
              FONT_MOUNT_PATH,
              result);
        LOG_I("if this partition is new, run: mkfs -t elm font");
        return -RT_ERROR;
    }

    font_mounted = RT_TRUE;

    LOG_I("partition '%s' mounted to '%s'",
          FONT_PARTITION_NAME,
          FONT_MOUNT_PATH);
    LOG_I("AHT10 data file: %s", DATA_FILE_PATH);

    return RT_EOK;
}

/**
 * @brief 手动挂载 font 分区。
 *
 * 当自动挂载失败时，可以完成格式化后再次通过 MSH 执行本命令。
 *
 * @return RT_EOK 挂载成功。
 * @return -RT_ERROR 挂载失败。
 */
static rt_err_t mount_demo(void)
{
    rt_err_t result;

    result = font_filesystem_mount();

    if (result != RT_EOK)
    {
        LOG_E("failed to initialize font filesystem");
        return result;
    }

    LOG_I("font filesystem initialized successfully");

    return RT_EOK;
}

/**
 * @brief 上电后自动尝试挂载 font 分区。
 *
 * 自动挂载失败不会阻止 MQTT 模块继续启动，
 * 之后可以在 MSH 中格式化并手动重新挂载。
 *
 * @return RT_EOK 始终返回成功，避免阻塞其他应用初始化。
 */
static int font_filesystem_auto_init(void)
{
    if (font_filesystem_mount() != RT_EOK)
    {
        LOG_W("font filesystem auto mount failed");
        LOG_W("format it if needed, then run 'mount_demo'");
    }

    return RT_EOK;
}

INIT_APP_EXPORT(font_filesystem_auto_init);

MSH_CMD_EXPORT(mount_demo, mount font partition to /font);
