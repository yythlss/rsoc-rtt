/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-21     24527       the first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>

#define TEST_DEV_NAME           "test_dev"
#define TEST_DEV_BUFFER_SIZE    128

/* 自定义控制命令 */
#define TEST_DEV_CTRL_CLEAR     0x01
#define TEST_DEV_CTRL_GET_LEN   0x02

/**
 * @brief test_dev 虚拟设备对象。
 *
 * parent 为 RT-Thread 标准设备对象，
 * buffer 用于模拟设备内部的数据缓冲区，
 * data_len 表示当前有效数据长度。
 */
struct test_device
{
    struct rt_device parent;

    rt_uint8_t buffer[TEST_DEV_BUFFER_SIZE];
    rt_size_t data_len;
};

/* test_dev 设备实例 */
static struct test_device test_dev;

/**
 * @brief 初始化 test_dev 虚拟设备。
 *
 * 清空内部缓冲区，并将有效数据长度设置为 0。
 *
 * @param dev RT-Thread 设备对象。
 *
 * @return RT_EOK 初始化成功。
 */
static rt_err_t test_dev_init(rt_device_t dev)
{
    struct test_device *device;

    device = (struct test_device *)dev;

    rt_memset(device->buffer, 0, sizeof(device->buffer));
    device->data_len = 0;

    rt_kprintf("test_dev init\n");

    return RT_EOK;
}

/**
 * @brief 打开 test_dev 虚拟设备。
 *
 * @param dev RT-Thread 设备对象。
 * @param oflag 设备打开标志。
 *
 * @return RT_EOK 打开成功。
 */
static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    RT_UNUSED(dev);
    RT_UNUSED(oflag);

    rt_kprintf("test_dev open\n");

    return RT_EOK;
}

/**
 * @brief 关闭 test_dev 虚拟设备。
 *
 * @param dev RT-Thread 设备对象。
 *
 * @return RT_EOK 关闭成功。
 */
static rt_err_t test_dev_close(rt_device_t dev)
{
    RT_UNUSED(dev);

    rt_kprintf("test_dev close\n");

    return RT_EOK;
}

/**
 * @brief 从 test_dev 虚拟设备读取数据。
 *
 * 根据 pos 指定的偏移位置，从内部缓冲区读取数据。
 *
 * @param dev RT-Thread 设备对象。
 * @param pos 数据读取偏移。
 * @param buffer 接收数据的缓冲区。
 * @param size 希望读取的数据长度。
 *
 * @return 实际读取的数据长度。
 */
static rt_size_t test_dev_read(rt_device_t dev,
                                rt_off_t pos,
                                void *buffer,
                                rt_size_t size)
{
    struct test_device *device;
    rt_size_t read_size;

    device = (struct test_device *)dev;

    if ((buffer == RT_NULL) || (size == 0))
    {
        return 0;
    }

    if ((pos < 0) || ((rt_size_t)pos >= device->data_len))
    {
        return 0;
    }

    read_size = device->data_len - (rt_size_t)pos;

    if (read_size > size)
    {
        read_size = size;
    }

    rt_memcpy(buffer,
              &device->buffer[pos],
              read_size);

    return (rt_size_t)read_size;
}

/**
 * @brief 向 test_dev 虚拟设备写入数据。
 *
 * 根据 pos 指定的偏移位置写入内部缓冲区。
 *
 * @param dev RT-Thread 设备对象。
 * @param pos 数据写入偏移。
 * @param buffer 待写入的数据。
 * @param size 希望写入的数据长度。
 *
 * @return 实际写入的数据长度。
 */
static rt_size_t test_dev_write(rt_device_t dev,
                                 rt_off_t pos,
                                 const void *buffer,
                                 rt_size_t size)
{
    struct test_device *device;
    rt_size_t write_size;
    rt_size_t end_pos;

    device = (struct test_device *)dev;

    if ((buffer == RT_NULL) || (size == 0))
    {
        return 0;
    }

    if ((pos < 0) || ((rt_size_t)pos >= TEST_DEV_BUFFER_SIZE))
    {
        return 0;
    }

    write_size = TEST_DEV_BUFFER_SIZE - (rt_size_t)pos;

    if (write_size > size)
    {
        write_size = size;
    }

    rt_memcpy(&device->buffer[pos],
              buffer,
              write_size);

    end_pos = (rt_size_t)pos + write_size;

    if (end_pos > device->data_len)
    {
        device->data_len = end_pos;
    }

    return (rt_size_t)write_size;
}

/**
 * @brief 控制 test_dev 虚拟设备。
 *
 * 支持清空内部缓冲区和获取当前有效数据长度。
 *
 * @param dev RT-Thread 设备对象。
 * @param cmd 控制命令。
 * @param args 控制参数。
 *
 * @return RT_EOK 控制成功。
 * @return -RT_ERROR 参数或命令错误。
 */
static rt_err_t test_dev_control(rt_device_t dev,
                                 int cmd,
                                 void *args)
{
    struct test_device *device;

    device = (struct test_device *)dev;

    switch (cmd)
    {
    case TEST_DEV_CTRL_CLEAR:
        rt_memset(device->buffer, 0, sizeof(device->buffer));
        device->data_len = 0;
        break;

    case TEST_DEV_CTRL_GET_LEN:
        if (args == RT_NULL)
        {
            return -RT_ERROR;
        }

        *(rt_size_t *)args = device->data_len;
        break;

    default:
        return -RT_ERROR;
    }

    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS

/**
 * @brief test_dev 设备操作接口表。
 */
static const struct rt_device_ops test_dev_ops =
{
    test_dev_init,
    test_dev_open,
    test_dev_close,
    test_dev_read,
    test_dev_write,
    test_dev_control
};

#endif

/**
 * @brief 注册 test_dev 虚拟设备。
 *
 * 将 test_dev 注册到 RT-Thread 设备框架，
 * 注册完成后可以通过 rt_device_find("test_dev")
 * 查找到该设备。
 *
 * @return RT_EOK 注册成功。
 * @return 其他错误码 注册失败。
 */
static int test_dev_register(void)
{
    rt_err_t result;
    rt_device_t dev;

    dev = &test_dev.parent;

    /* 设置设备类型 */
    dev->type = RT_Device_Class_Char;

#ifdef RT_USING_DEVICE_OPS

    /* 使用统一设备操作接口 */
    dev->ops = &test_dev_ops;

#else

    /* 兼容旧版本 RT-Thread 设备接口 */
    dev->init = test_dev_init;
    dev->open = test_dev_open;
    dev->close = test_dev_close;
    dev->read = test_dev_read;
    dev->write = test_dev_write;
    dev->control = test_dev_control;

#endif

    dev->rx_indicate = RT_NULL;
    dev->tx_complete = RT_NULL;

    /* 注册虚拟设备 */
    result = rt_device_register(
        dev,
        TEST_DEV_NAME,
        RT_DEVICE_FLAG_RDWR);

    if (result != RT_EOK)
    {
        rt_kprintf("register test_dev failed, error = %d\n", result);
        return result;
    }

    rt_kprintf("register test_dev success\n");

    return RT_EOK;
}

/* 在设备初始化阶段自动注册 test_dev */
INIT_DEVICE_EXPORT(test_dev_register);

/**
 * @brief test_dev 测试命令。
 *
 * 查找、打开 test_dev，写入字符串后再读取，
 * 用于验证虚拟设备驱动框架是否工作正常。
 *
 * @return RT_EOK 测试成功。
 * @return -RT_ERROR 测试失败。
 */
static int test_dev_test(void)
{
    rt_device_t dev;
    rt_err_t result;
    rt_size_t length;
    rt_size_t data_len;
    const char write_data[] = "hello rt-thread";
    char read_data[32] = {0};

    /* 查找设备 */
    dev = rt_device_find(TEST_DEV_NAME);

    if (dev == RT_NULL)
    {
        rt_kprintf("cannot find %s\n", TEST_DEV_NAME);
        return -RT_ERROR;
    }

    /* 打开设备 */
    result = rt_device_open(dev, RT_DEVICE_FLAG_RDWR);

    if (result != RT_EOK)
    {
        rt_kprintf("open %s failed\n", TEST_DEV_NAME);
        return result;
    }

    /* 写入测试数据 */
    length = rt_device_write(
        dev,
        0,
        write_data,
        sizeof(write_data) - 1);

    rt_kprintf("write length: %d\n", length);

    /* 获取设备当前数据长度 */
    result = rt_device_control(
        dev,
        TEST_DEV_CTRL_GET_LEN,
        &data_len);

    if (result == RT_EOK)
    {
        rt_kprintf("device data length: %d\n", data_len);
    }

    /* 读取测试数据 */
    length = rt_device_read(
        dev,
        0,
        read_data,
        sizeof(read_data) - 1);

    if (length > 0)
    {
        read_data[length] = '\0';

        rt_kprintf(
            "read length: %d, data: %s\n",
            length,
            read_data);
    }

    /* 关闭设备 */
    rt_device_close(dev);

    return RT_EOK;
}

/* 导出 MSH 测试命令 */
MSH_CMD_EXPORT(test_dev_test, test virtual device);

