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

#define DBG_TAG "drv.vtest"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define VTEST_BUFFER_SIZE  (256)

/* 虚拟设备底层私有数据结构体 (模拟硬件控制器与寄存器状态) */
struct vtest_device_data
{
    rt_uint8_t  buffer[VTEST_BUFFER_SIZE];  /* 内部硬件 FIFO / 存储区 */
    rt_uint16_t read_index;                 /* 读指针 */
    rt_uint16_t write_index;                /* 写指针 */
    rt_uint16_t data_len;                   /* 当前有效数据长度 */

    rt_uint8_t  mode;                       /* 工作模式 */
    rt_uint8_t  is_open;                    /* 打开状态 */
    rt_uint32_t total_tx;                   /* 发送统计 */
    rt_uint32_t total_rx;                   /* 接收统计 */
    rt_uint32_t error_count;                /* 错误计数 */

    rt_mutex_t  lock;                       /* 硬件互斥锁 (保证多线程安全) */
};

/* 静态实例化虚拟设备与私有数据 */
static struct rt_test_device   _vtest0_dev;
static struct vtest_device_data _vtest0_data;

/* ==================== 底层 Ops 函数实现 ==================== */

static rt_err_t _vtest_init(rt_test_device_t dev)
{
    struct vtest_device_data *data = (struct vtest_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    LOG_D("Virtual test device '%s' initializing...", dev->parent.parent.name);

    data->read_index  = 0;
    data->write_index = 0;
    data->data_len    = 0;
    data->mode        = RT_TEST_MODE_LOOPBACK; /* 默认开启回环模式 */
    data->is_open     = 0;
    data->total_tx    = 0;
    data->total_rx    = 0;
    data->error_count = 0;
    rt_memset(data->buffer, 0, VTEST_BUFFER_SIZE);

    if (data->lock == RT_NULL)
    {
        data->lock = rt_mutex_create("vtest_lock", RT_IPC_FLAG_FIFO);
        if (data->lock == RT_NULL)
        {
            LOG_E("Failed to create mutex for virtual test device!");
            return -RT_ENOMEM;
        }
    }

    return RT_EOK;
}

static rt_err_t _vtest_open(rt_test_device_t dev, rt_uint16_t oflag)
{
    struct vtest_device_data *data = (struct vtest_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);
    data->is_open = 1;
    LOG_D("Virtual test device '%s' opened with flag 0x%04X.", dev->parent.parent.name, oflag);
    rt_mutex_release(data->lock);

    return RT_EOK;
}

static rt_err_t _vtest_close(rt_test_device_t dev)
{
    struct vtest_device_data *data = (struct vtest_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);
    data->is_open = 0;
    LOG_D("Virtual test device '%s' closed.", dev->parent.parent.name);
    rt_mutex_release(data->lock);

    return RT_EOK;
}

static rt_size_t _vtest_read(rt_test_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct vtest_device_data *data = (struct vtest_device_data *)dev->user_data;
    rt_size_t read_bytes = 0;
    rt_uint8_t *ptr = (rt_uint8_t *)buffer;

    RT_ASSERT(data != RT_NULL);
    if (buffer == RT_NULL || size == 0)
    {
        return 0;
    }

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);

    if (data->mode == RT_TEST_MODE_PATTERN)
    {
        /* 模式 3: 信号发生模式，生成递增 ASCII 字符序列 */
        for (rt_size_t i = 0; i < size; i++)
        {
            ptr[i] = (rt_uint8_t)('A' + ((data->total_rx + i) % 26));
        }
        read_bytes = size;
        data->total_rx += read_bytes;
    }
    else
    {
        /* 模式 0/1/2: 从 FIFO 环形缓冲区中提取已存入数据 */
        while (read_bytes < size && data->data_len > 0)
        {
            ptr[read_bytes] = data->buffer[data->read_index];
            data->read_index = (data->read_index + 1) % VTEST_BUFFER_SIZE;
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
    struct vtest_device_data *data = (struct vtest_device_data *)dev->user_data;
    rt_size_t write_bytes = 0;
    const rt_uint8_t *ptr = (const rt_uint8_t *)buffer;

    RT_ASSERT(data != RT_NULL);
    if (buffer == RT_NULL || size == 0)
    {
        return 0;
    }

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);

    /* 将输入数据写入 FIFO 环形缓冲区 */
    while (write_bytes < size)
    {
        if (data->data_len >= VTEST_BUFFER_SIZE)
        {
            /* 缓冲区满，覆盖最旧的数据或产生溢出警告 */
            data->read_index = (data->read_index + 1) % VTEST_BUFFER_SIZE;
            data->data_len--;
            data->error_count++;
        }

        data->buffer[data->write_index] = ptr[write_bytes];
        data->write_index = (data->write_index + 1) % VTEST_BUFFER_SIZE;
        data->data_len++;
        write_bytes++;
    }

    data->total_tx += write_bytes;

    if (data->mode == RT_TEST_MODE_ECHO)
    {
        rt_kprintf("[vtest0 ECHO] ");
        for (rt_size_t i = 0; i < write_bytes; i++)
        {
            rt_kprintf("%c", ptr[i]);
        }
        rt_kprintf("\n");
    }

    rt_mutex_release(data->lock);

    /* 模拟硬件中断触发 rx_indicate 回调通知上层应用 */
    if (dev->parent.rx_indicate != RT_NULL)
    {
        dev->parent.rx_indicate(&(dev->parent), write_bytes);
    }

    return write_bytes;
}

static rt_err_t _vtest_control(rt_test_device_t dev, int cmd, void *args)
{
    struct vtest_device_data *data = (struct vtest_device_data *)dev->user_data;
    RT_ASSERT(data != RT_NULL);

    rt_mutex_take(data->lock, RT_WAITING_FOREVER);

    switch (cmd)
    {
    case RT_TEST_CTRL_GET_STATUS:
    {
        if (args == RT_NULL)
        {
            rt_mutex_release(data->lock);
            return -RT_EINVAL;
        }
        struct rt_test_status *status = (struct rt_test_status *)args;
        status->mode           = data->mode;
        status->is_open        = data->is_open;
        status->buffer_size    = VTEST_BUFFER_SIZE;
        status->data_len       = data->data_len;
        status->total_tx_bytes = data->total_tx;
        status->total_rx_bytes = data->total_rx;
        status->error_count    = data->error_count;
        break;
    }

    case RT_TEST_CTRL_SET_MODE:
    {
        if (args == RT_NULL)
        {
            rt_mutex_release(data->lock);
            return -RT_EINVAL;
        }
        rt_uint8_t mode = *(rt_uint8_t *)args;
        if (mode > RT_TEST_MODE_PATTERN)
        {
            rt_mutex_release(data->lock);
            return -RT_EINVAL;
        }
        data->mode = mode;
        LOG_I("Device '%s' mode switched to %d.", dev->parent.parent.name, mode);
        break;
    }

    case RT_TEST_CTRL_RESET:
    {
        data->read_index  = 0;
        data->write_index = 0;
        data->data_len    = 0;
        rt_memset(data->buffer, 0, VTEST_BUFFER_SIZE);
        LOG_I("Device '%s' buffer reset successfully.", dev->parent.parent.name);
        break;
    }

    case RT_TEST_CTRL_GET_BUFSIZE:
    {
        if (args == RT_NULL)
        {
            rt_mutex_release(data->lock);
            return -RT_EINVAL;
        }
        *(rt_uint16_t *)args = VTEST_BUFFER_SIZE;
        break;
    }

    case RT_TEST_CTRL_SELF_TEST:
    {
        LOG_I("Running self-test for '%s'...", dev->parent.parent.name);
        /* 自检流程：检查互斥锁、缓冲区完整性 */
        if (data->lock == RT_NULL)
        {
            rt_mutex_release(data->lock);
            return -RT_ERROR;
        }
        break;
    }

    default:
        rt_mutex_release(data->lock);
        return -RT_ENOSYS;
    }

    rt_mutex_release(data->lock);
    return RT_EOK;
}

/* 绑定虚拟设备底层操作函数集 */
static const struct rt_test_ops _vtest_ops =
{
    _vtest_init,
    _vtest_open,
    _vtest_close,
    _vtest_read,
    _vtest_write,
    _vtest_control
};

/**
 * @brief 虚拟硬件设备板级自动初始化与注册
 */
static int drv_vtest_hw_init(void)
{
    rt_err_t ret;

    _vtest0_dev.ops = &_vtest_ops;

    /* 注册 vtest0 到 RT-Thread I/O 设备管理器 */
    ret = rt_device_test_register(&_vtest0_dev, "vtest0", RT_DEVICE_FLAG_RDWR, &_vtest0_data);
    if (ret != RT_EOK)
    {
        LOG_E("Failed to register virtual test device 'vtest0' (err=%d)!", ret);
        return -RT_ERROR;
    }

    LOG_I("Virtual test device 'vtest0' registered successfully.");
    return RT_EOK;
}
INIT_DEVICE_EXPORT(drv_vtest_hw_init);
