/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     Leng Mengqi  Virtual test_dev hardware driver implementation
 */

#include "test_dev.h"
#include <string.h>

#define DBG_TAG "drv.test"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define TEST_DEV_BUFFER_SIZE  (256)

/* 虚拟设备底层私有数据结构体 (模拟硬件 FIFO 与状态) */
struct test_device_data
{
    rt_uint8_t  buffer[TEST_DEV_BUFFER_SIZE];  /* 内部 FIFO / 存储区 */
    rt_uint16_t read_index;                    /* 读指针 */
    rt_uint16_t write_index;                   /* 写指针 */
    rt_uint16_t data_len;                      /* 有效数据长度 */

    rt_uint8_t  mode;                          /* 工作模式 */
    rt_uint8_t  is_open;                       /* 打开状态 */
    rt_uint32_t total_tx;                      /* 发送计数 */
    rt_uint32_t total_rx;                      /* 接收计数 */
    rt_uint32_t error_count;                   /* 错误计数 */

    rt_mutex_t  lock;                          /* 互斥锁 */
};

/* 静态实例化 test_dev 设备与私有数据 */
static struct rt_test_device    _test_dev;
static struct test_device_data  _test_data;

/* ==================== 底层 Ops 函数实现 ==================== */

static rt_err_t _vtest_init(rt_test_device_t dev)
{
    struct test_device_data *data = (struct test_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    data->read_index  = 0;
    data->write_index = 0;
    data->data_len    = 0;
    data->mode        = RT_TEST_MODE_LOOPBACK; /* 默认回环模式 */
    data->is_open     = 0;
    data->total_tx    = 0;
    data->total_rx    = 0;
    data->error_count = 0;
    rt_memset(data->buffer, 0, TEST_DEV_BUFFER_SIZE);

    if (data->lock == RT_NULL)
    {
        data->lock = rt_mutex_create("tdev_lock", RT_IPC_FLAG_FIFO);
        if (data->lock == RT_NULL)
        {
            return -RT_ENOMEM;
        }
    }

    return RT_EOK;
}

static rt_err_t _vtest_open(rt_test_device_t dev, rt_uint16_t oflag)
{
    struct test_device_data *data = (struct test_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);
    data->is_open = 1;
    rt_mutex_release(data->lock);

    return RT_EOK;
}

static rt_err_t _vtest_close(rt_test_device_t dev)
{
    struct test_device_data *data = (struct test_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);
    data->is_open = 0;
    rt_mutex_release(data->lock);

    return RT_EOK;
}

static rt_size_t _vtest_read(rt_test_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_device_data *data = (struct test_device_data *)dev->user_data;
    rt_size_t read_bytes = 0;
    rt_uint8_t *ptr = (rt_uint8_t *)buffer;

    RT_ASSERT(data != RT_NULL);
    if (buffer == RT_NULL || size == 0) return 0;

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);

    if (data->mode == RT_TEST_MODE_PATTERN)
    {
        /* 模式 3: 信号发生模式 */
        for (rt_size_t i = 0; i < size; i++)
        {
            ptr[i] = (rt_uint8_t)('A' + ((data->total_rx + i) % 26));
        }
        read_bytes = size;
        data->total_rx += read_bytes;
    }
    else
    {
        /* 模式 0/1/2: 从 FIFO 读取数据 */
        while (read_bytes < size && data->data_len > 0)
        {
            ptr[read_bytes] = data->buffer[data->read_index];
            data->read_index = (data->read_index + 1) % TEST_DEV_BUFFER_SIZE;
            data->data_len--;
            read_bytes++;
        }
        data->total_rx += read_bytes;
    }

    rt_mutex_release(data->lock);
    return read_bytes;
}

static rt_size_t _vtest_write(rt_test_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_device_data *data = (struct test_device_data *)dev->user_data;
    rt_size_t write_bytes = 0;
    const rt_uint8_t *ptr = (const rt_uint8_t *)buffer;

    RT_ASSERT(data != RT_NULL);
    if (buffer == RT_NULL || size == 0) return 0;

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);

    while (write_bytes < size)
    {
        if (data->data_len >= TEST_DEV_BUFFER_SIZE)
        {
            data->read_index = (data->read_index + 1) % TEST_DEV_BUFFER_SIZE;
            data->data_len--;
            data->error_count++;
        }

        data->buffer[data->write_index] = ptr[write_bytes];
        data->write_index = (data->write_index + 1) % TEST_DEV_BUFFER_SIZE;
        data->data_len++;
        write_bytes++;
    }

    data->total_tx += write_bytes;

    if (data->mode == RT_TEST_MODE_ECHO)
    {
        rt_kprintf("[test_dev ECHO] %.*s\n", (int)write_bytes, ptr);
    }

    rt_mutex_release(data->lock);

    /* 触发 rx_indicate 回调通知 */
    if (dev->parent.rx_indicate != RT_NULL)
    {
        dev->parent.rx_indicate(&(dev->parent), write_bytes);
    }

    return write_bytes;
}

static rt_err_t _vtest_control(rt_test_device_t dev, int cmd, void *args)
{
    struct test_device_data *data = (struct test_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);

    switch (cmd)
    {
    case RT_TEST_CTRL_GET_STATUS:
    {
        if (args == RT_NULL) { rt_mutex_release(data->lock); return -RT_EINVAL; }
        struct rt_test_status *status = (struct rt_test_status *)args;
        status->mode           = data->mode;
        status->is_open        = data->is_open;
        status->buffer_size    = TEST_DEV_BUFFER_SIZE;
        status->data_len       = data->data_len;
        status->total_tx_bytes = data->total_tx;
        status->total_rx_bytes = data->total_rx;
        status->error_count    = data->error_count;
        break;
    }
    case RT_TEST_CTRL_SET_MODE:
    {
        if (args == RT_NULL) { rt_mutex_release(data->lock); return -RT_EINVAL; }
        rt_uint8_t mode = *(rt_uint8_t *)args;
        if (mode > RT_TEST_MODE_PATTERN) { rt_mutex_release(data->lock); return -RT_EINVAL; }
        data->mode = mode;
        break;
    }
    case RT_TEST_CTRL_RESET:
    {
        data->read_index  = 0;
        data->write_index = 0;
        data->data_len    = 0;
        rt_memset(data->buffer, 0, TEST_DEV_BUFFER_SIZE);
        break;
    }
    case RT_TEST_CTRL_GET_BUFSIZE:
    {
        if (args == RT_NULL) { rt_mutex_release(data->lock); return -RT_EINVAL; }
        *(rt_uint16_t *)args = TEST_DEV_BUFFER_SIZE;
        break;
    }
    case RT_TEST_CTRL_SELF_TEST:
    {
        if (data->lock == RT_NULL) { rt_mutex_release(data->lock); return -RT_ERROR; }
        break;
    }
    default:
        rt_mutex_release(data->lock);
        return -RT_ENOSYS;
    }

    rt_mutex_release(data->lock);
    return RT_EOK;
}

static const struct rt_test_ops _test_ops =
{
    _vtest_init,
    _vtest_open,
    _vtest_close,
    _vtest_read,
    _vtest_write,
    _vtest_control
};

/**
 * @brief 注册名为 "test_dev" 的虚拟设备
 */
static int drv_test_dev_hw_init(void)
{
    _test_dev.ops = &_test_ops;

    /* 注册为 "test_dev" 设备 */
    if (rt_device_test_register(&_test_dev, "test_dev", RT_DEVICE_FLAG_RDWR, &_test_data) != RT_EOK)
    {
        LOG_E("Failed to register 'test_dev'!");
        return -RT_ERROR;
    }

    LOG_I("Device 'test_dev' registered successfully.");
    return RT_EOK;
}
INIT_DEVICE_EXPORT(drv_test_dev_hw_init);
