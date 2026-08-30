/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     13945       the first version
 */

#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "test_drv"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static rt_err_t drv_test_init(rt_device_t dev)
{
    LOG_I("test drv init\n");
    return RT_EOK;
}

static rt_err_t drv_test_open(rt_device_t dev, rt_uint16_t oflag)
{
    LOG_I("test drv open flag = %d\n", oflag);
    return RT_EOK;
}

static rt_err_t drv_test_close(rt_device_t dev)
{
    LOG_I("test drv close\n");
    return RT_EOK;
}

static rt_size_t drv_test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    LOG_I("test drv read pos = %d, size = %d\n", pos, size);
    return size;
}

static rt_size_t drv_test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    LOG_I("test drv write pos = %d, size = %d\n", pos, size);
    return size;
}

static rt_err_t drv_test_control(rt_device_t dev, int cmd, void *args)
{
    LOG_I("test drv control cmd = %d\n", cmd);
    return RT_EOK;
}

int rt_drv_test_init(void)
{
    rt_device_t test_dev = rt_device_create(RT_Device_Class_Char, 0);
    if (!test_dev)
    {
        LOG_E("test drv create failed!\n");
        return -RT_ERROR;
    }

    test_dev->init = drv_test_init;
    test_dev->open = drv_test_open;
    test_dev->close = drv_test_close;
    test_dev->read = drv_test_read;
    test_dev->write = drv_test_write;
    test_dev->control = drv_test_control;

    if (rt_device_register(test_dev, "test_drv", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("test drv register failed!\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_BOARD_EXPORT(rt_drv_test_init);

/* 应用验证入口：在 msh 里输入 test_drv_app 触发 OPS 调用 */
static int drv_test_app(void)
{
    rt_device_t test_dev = rt_device_find("test_drv");
    if (test_dev == RT_NULL)
    {
        LOG_E("can not find test drv!\n");
        return -RT_ERROR;
    }

    rt_device_open(test_dev, RT_DEVICE_OFLAG_RDWR);
    rt_device_control(test_dev, RT_DEVICE_CTRL_CONFIG, RT_NULL);
    rt_device_write(test_dev, 100, RT_NULL, 1024);
    rt_device_read(test_dev, 20, RT_NULL, 128);
    rt_device_close(test_dev);

    return RT_EOK;
}
MSH_CMD_EXPORT(drv_test_app, test drv app: open/ctl/write/read/close);
