/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     RT-Thread    first version
 */
#ifndef TEST_DEV_H__
#define TEST_DEV_H__

#include <rtthread.h>
#include <rtdevice.h>

/* 框架层与驱动层的抽象接口 */
struct rt_test_ops
{
    rt_err_t   (*init)(rt_device_t dev);
    rt_err_t   (*open)(rt_device_t dev, rt_uint16_t oflag);
    rt_err_t   (*close)(rt_device_t dev);
    rt_size_t (*read)(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);
    rt_size_t (*write)(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
    rt_err_t   (*control)(rt_device_t dev, int cmd, void *args);
};

/* 框架层设备对象 */
struct rt_device_test
{
    struct rt_device parent;   /* 内核设备，挂到设备管理器 */
    const struct rt_test_ops *ops;
};

/* 框架层注册 API —— 对标 rt_device_pin_register */
int rt_device_test_register(const char *name,
                            const struct rt_test_ops *ops,
                            void *user_data);

#endif /* TEST_DEV_H__ */
