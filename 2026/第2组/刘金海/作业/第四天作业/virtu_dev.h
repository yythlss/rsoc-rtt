/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     yanran       the first version
 */
#ifndef APPLICATIONS_VIRTU_DEV_H_
#define APPLICATIONS_VIRTU_DEV_H_

#include <rtthread.h>

//自定义的led_ops表
struct rt_led_ops
{
    void (*led_init)(struct rt_device *device, rt_base_t pin, rt_base_t mode);
    void (*led_on)(struct rt_device *device, rt_base_t pin);
    void (*led_off)(struct rt_device *device, rt_base_t pin);
    void (*led_toggle)(struct rt_device *device, rt_base_t pin);
};

//定义led设备类型
struct rt_device_led
{
    struct rt_device parent;
    const struct rt_led_ops *ops;
};

//函数声明
void rt_led_on(rt_base_t pin);
void rt_led_off(rt_base_t pin);
void rt_led_init(rt_base_t pin, rt_base_t mode);
void rt_led_toggle(rt_base_t pin);


#endif /* APPLICATIONS_VIRTU_DEV_H_ */
