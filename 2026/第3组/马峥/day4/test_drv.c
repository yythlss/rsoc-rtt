/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20    Mazheng      day4 homework - virtual device driver
 */
#include <rtthread.h>
#include <rtdevice.h>

#define TEST_DEV_NAME       "test_dev"
#define TEST_DEV_MAGIC      0xDEADBEEF
#define TEST_DEV_VERSION    "v1.0.0"

/* 私有数据结构：模拟一个真实设备 */
struct test_device
{
    struct rt_device parent;
    rt_uint8_t buffer[64];
    rt_size_t data_len;
    rt_uint32_t magic;
};
static struct test_device *test_device = RT_NULL;

/* 控制命令 */
#define TEST_DEV_CTRL_GET_VERSION   0x100
#define TEST_DEV_CTRL_CLEAR_BUFFER  0x101
#define TEST_DEV_CTRL_ECHO          0x102

static rt_err_t drv_test_init(rt_device_t dev)
{
    struct test_device *test_dev = (struct test_device *)dev;
    rt_kprintf("[test_dev] driver init OK, magic=0x%08X\n", test_dev->magic);
    return RT_EOK;
}

static rt_err_t drv_test_open(rt_device_t dev, rt_uint16_t oflag)
{
    rt_kprintf("[test_dev] driver open, oflag=0x%04X\n", oflag);
    return RT_EOK;
}

static rt_err_t drv_test_close(rt_device_t dev)
{
    rt_kprintf("[test_dev] driver close\n");
    return RT_EOK;
}

static rt_size_t drv_test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_device *test_dev = (struct test_device *)dev;
    rt_size_t copy_size = (size > test_dev->data_len) ? test_dev->data_len : size;

    if (test_dev->data_len == 0)
    {
        rt_kprintf("[test_dev] read: buffer empty\n");
        return 0;
    }

    rt_memcpy(buffer, test_dev->buffer, copy_size);
    rt_kprintf("[test_dev] read: pos=%d, size=%d, copied=%d\n", pos, size, copy_size);
    return copy_size;
}

static rt_size_t drv_test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_device *test_dev = (struct test_device *)dev;
    rt_size_t copy_size = (size > sizeof(test_dev->buffer)) ? sizeof(test_dev->buffer) : size;

    rt_memcpy(test_dev->buffer, buffer, copy_size);
    test_dev->data_len = copy_size;
    rt_kprintf("[test_dev] write: pos=%d, size=%d, stored=%d\n", pos, size, copy_size);
    return copy_size;
}

static rt_err_t drv_test_control(rt_device_t dev, int cmd, void *args)
{
    struct test_device *test_dev = (struct test_device *)dev;
    rt_err_t ret = RT_EOK;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_CONFIG:
        rt_kprintf("[test_dev] control: RT_DEVICE_CTRL_CONFIG\n");
        break;
    case TEST_DEV_CTRL_GET_VERSION:
        if (args != RT_NULL)
        {
            rt_strncpy((char *)args, TEST_DEV_VERSION, 16);
        }
        rt_kprintf("[test_dev] control: GET_VERSION = %s\n", TEST_DEV_VERSION);
        break;
    case TEST_DEV_CTRL_CLEAR_BUFFER:
        test_dev->data_len = 0;
        rt_memset(test_dev->buffer, 0, sizeof(test_dev->buffer));
        rt_kprintf("[test_dev] control: BUFFER CLEARED\n");
        break;
    case TEST_DEV_CTRL_ECHO:
        rt_kprintf("[test_dev] control: ECHO cmd received\n");
        break;
    default:
        ret = -RT_EINVAL;
        rt_kprintf("[test_dev] control: unknown cmd %d\n", cmd);
        break;
    }
    return ret;
}

static int rt_drv_test_init(void)
{
    rt_err_t ret;

    /* 检查设备是否已注册，防止重复注册 */
    if (rt_device_find(TEST_DEV_NAME) != RT_NULL)
    {
        rt_kprintf("[test_dev] already registered, skip.\n");
        return RT_EOK;
    }

    /* 动态创建设备（参考 dev_test.c 课程示例） */
    rt_device_t dev = rt_device_create(RT_Device_Class_Char, sizeof(struct test_device));
    if (!dev)
    {
        rt_kprintf("[test_dev] create failed!\n");
        return -RT_ERROR;
    }

    /* 保存私有数据指针 */
    test_device = (struct test_device *)dev;
    test_device->magic = TEST_DEV_MAGIC;
    test_device->data_len = 0;
    rt_memset(test_device->buffer, 0, sizeof(test_device->buffer));

    /* 绑定回调函数（核心：device ops 指向 drv 层实现） */
    dev->init   = drv_test_init;
    dev->open   = drv_test_open;
    dev->close  = drv_test_close;
    dev->read   = drv_test_read;
    dev->write  = drv_test_write;
    dev->control = drv_test_control;

    /* 注册到设备框架 */
    ret = rt_device_register(dev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR);
    if (ret != RT_EOK)
    {
        rt_kprintf("[test_dev] register failed! ret=%d\n", ret);
        return ret;
    }

    rt_kprintf("[test_dev] registered successfully\n");
    return RT_EOK;
}
/* 自动初始化：INIT_DEVICE_EXPORT 在设备层初始化阶段执行 */
INIT_DEVICE_EXPORT(rt_drv_test_init);
