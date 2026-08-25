/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     Leng Mengqi  First version of virtual test_dev framework
 */

#ifndef __TEST_DEV_H__
#define __TEST_DEV_H__

#include <rtthread.h>
#include <rtdevice.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Test Device Control Commands */
#define RT_TEST_CTRL_GET_STATUS     (0x10)  /* 获取设备当前运行状态及统计 */
#define RT_TEST_CTRL_SET_MODE       (0x11)  /* 设置设备工作模式 */
#define RT_TEST_CTRL_RESET          (0x12)  /* 复位设备内部缓冲区与寄存器 */
#define RT_TEST_CTRL_GET_BUFSIZE    (0x13)  /* 获取设备内部缓冲区大小 */
#define RT_TEST_CTRL_SELF_TEST      (0x14)  /* 触发硬件/驱动自检 */

/* Test Device Operating Modes */
#define RT_TEST_MODE_NORMAL         (0x00)  /* 普通模式：读写独立的内存存储区 */
#define RT_TEST_MODE_LOOPBACK       (0x01)  /* 回环模式：写入的数据直接进入读取队列 */
#define RT_TEST_MODE_ECHO           (0x02)  /* 自动回显模式：写入时自动打印回显 */
#define RT_TEST_MODE_PATTERN        (0x03)  /* 信号发生模式：读取时产生规律递增伪数据 */

/* Test Device Status Structure */
struct rt_test_status
{
    rt_uint8_t  mode;           /* 当前工作模式 */
    rt_uint8_t  is_open;        /* 打开标志与引用状态 */
    rt_uint16_t buffer_size;    /* 内部缓冲区总容量 */
    rt_uint16_t data_len;       /* 当前缓冲区已有数据字节数 */
    rt_uint32_t total_tx_bytes; /* 累计写入字节数 */
    rt_uint32_t total_rx_bytes; /* 累计读取字节数 */
    rt_uint32_t error_count;    /* 异常/错误计数 */
};

/* Forward Declaration */
struct rt_test_device;
typedef struct rt_test_device *rt_test_device_t;

/* Test Device Operations Function Pointer Table */
struct rt_test_ops
{
    rt_err_t  (*init)(rt_test_device_t dev);
    rt_err_t  (*open)(rt_test_device_t dev, rt_uint16_t oflag);
    rt_err_t  (*close)(rt_test_device_t dev);
    rt_size_t (*read)(rt_test_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);
    rt_size_t (*write)(rt_test_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
    rt_err_t  (*control)(rt_test_device_t dev, int cmd, void *args);
};

/* Test Device Object Structure (Inherits struct rt_device) */
struct rt_test_device
{
    struct rt_device parent;            /* 继承 RT-Thread I/O 设备基类 */
    const struct rt_test_ops *ops;      /* 底层驱动实现的 ops 函数集 */
    void *user_data;                    /* 设备私有驱动数据指针 */
};

/* Framework Management & Registration API */
rt_err_t rt_device_test_register(struct rt_test_device *dev,
                                 const char *name,
                                 rt_uint32_t flags,
                                 void *user_data);

/* High-Level User Friendly APIs */
rt_test_device_t rt_test_dev_find(const char *name);
rt_err_t  rt_test_dev_open(rt_test_device_t dev, rt_uint16_t oflag);
rt_err_t  rt_test_dev_close(rt_test_device_t dev);
rt_size_t rt_test_dev_read(rt_test_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);
rt_size_t rt_test_dev_write(rt_test_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
rt_err_t  rt_test_dev_control(rt_test_device_t dev, int cmd, void *args);
rt_err_t  rt_test_dev_set_mode(rt_test_device_t dev, rt_uint8_t mode);
rt_err_t  rt_test_dev_reset(rt_test_device_t dev);
rt_err_t  rt_test_dev_get_status(rt_test_device_t dev, struct rt_test_status *status);

#ifdef __cplusplus
}
#endif

#endif /* __TEST_DEV_H__ */
