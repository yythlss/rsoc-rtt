/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-21     DuMachen     DAY4 LED Device Driver Framework header
 */

#ifndef __VIRTU_DEV_H__
#define __VIRTU_DEV_H__

#include <rtthread.h>
#include <rtdevice.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- 1. LED 操作方法表结构体 ---------------- */
struct rt_led_ops
{
    void (*led_init)(struct rt_device *device, rt_base_t pin, rt_base_t mode);
    void (*led_on)(struct rt_device *device, rt_base_t pin);
    void (*led_off)(struct rt_device *device, rt_base_t pin);
    void (*led_toggle)(struct rt_device *device, rt_base_t pin);
};

/* ---------------- 2. 自定义 LED 设备结构体 ---------------- */
struct rt_device_led
{
    struct rt_device parent;            /* 继承 RT-Thread 标准设备基类 */
    const struct rt_led_ops *ops;       /* 指向底层硬件操作方法表的指针 */
};

/* ---------------- 3. 框架层对外驱动注册与控制 API ---------------- */
rt_err_t rt_device_led_register(const char *name, const struct rt_led_ops *ops, void *user_data);

void rt_led_init(rt_base_t pin, rt_base_t mode);
void rt_led_on(rt_base_t pin);
void rt_led_off(rt_base_t pin);
void rt_led_toggle(rt_base_t pin);

#ifdef __cplusplus
}
#endif

#endif /* __VIRTU_DEV_H__ */
