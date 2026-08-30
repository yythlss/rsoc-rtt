/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-21     DuMachen     DAY4 custom LED device driver framework implementation
 */

#include "virtu_dev.h"
#include <board.h>

#define DBG_TAG "led_dev"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 板载硬件引脚定义 (STM32F407 Spark-1) */
#define PIN_LED_B       GET_PIN(F, 11)  /* PF11: 蓝色 LED */
#define PIN_LED_R       GET_PIN(F, 12)  /* PF12: 红色 LED */

/* 全局 LED 设备单例 */
static struct rt_device_led _hw_led;

/* ==========================================================================
 * 第一部分：设备驱动框架层（Framework Layer）
 * 职责：定义接口契约，管理设备注册，对外提供统一语义化 API
 * ========================================================================== */

rt_err_t rt_device_led_register(const char *name, const struct rt_led_ops *ops, void *user_data)
{
    struct rt_device *device;

    RT_ASSERT(ops != RT_NULL);
    device = &(_hw_led.parent);

    device->type        = RT_Device_Class_Miscellaneous;
    device->rx_indicate = RT_NULL;
    device->tx_complete = RT_NULL;

    device->init        = RT_NULL;
    device->open        = RT_NULL;
    device->close       = RT_NULL;
    device->read        = RT_NULL;
    device->write       = RT_NULL;
    device->control     = RT_NULL;

    device->user_data   = user_data;
    _hw_led.ops         = ops;

    return rt_device_register(device, name, RT_DEVICE_FLAG_RDWR);
}

void rt_led_init(rt_base_t pin, rt_base_t mode)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    if (_hw_led.ops->led_init)
    {
        _hw_led.ops->led_init(&_hw_led.parent, pin, mode);
    }
}

void rt_led_on(rt_base_t pin)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    if (_hw_led.ops->led_on)
    {
        _hw_led.ops->led_on(&_hw_led.parent, pin);
    }
}

void rt_led_off(rt_base_t pin)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    if (_hw_led.ops->led_off)
    {
        _hw_led.ops->led_off(&_hw_led.parent, pin);
    }
}

void rt_led_toggle(rt_base_t pin)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    if (_hw_led.ops->led_toggle)
    {
        _hw_led.ops->led_toggle(&_hw_led.parent, pin);
    }
}

/* ==========================================================================
 * 第二部分：BSP 底层硬件驱动实现（Board Support Package Layer）
 * 职责：对接具体 STM32 硬件外设与引脚，实现操作方法表
 * ========================================================================== */

static void stm32_led_init(struct rt_device *device, rt_base_t pin, rt_base_t mode)
{
    rt_pin_mode(pin, mode);
    /* 默认熄灭 (高电平熄灭) */
    rt_pin_write(pin, PIN_HIGH);
    rt_kprintf("[BSP LED] Pin %u initialized to mode %u.\n", (rt_uint32_t)pin, (rt_uint32_t)mode);
}

static void stm32_led_on(struct rt_device *device, rt_base_t pin)
{
    /* 低电平点亮 */
    rt_pin_write(pin, PIN_LOW);
    rt_kprintf("[BSP LED] Pin %u turned ON (Low level).\n", (rt_uint32_t)pin);
}

static void stm32_led_off(struct rt_device *device, rt_base_t pin)
{
    /* 高电平熄灭 */
    rt_pin_write(pin, PIN_HIGH);
    rt_kprintf("[BSP LED] Pin %u turned OFF (High level).\n", (rt_uint32_t)pin);
}

static void stm32_led_toggle(struct rt_device *device, rt_base_t pin)
{
    rt_uint32_t count = 0;
    rt_kprintf("[BSP LED] Toggling Pin %u for 10 cycles...\n", (rt_uint32_t)pin);

    for (int i = 0; i < 10; i++)
    {
        rt_pin_write(pin, !rt_pin_read(pin));
        count++;
        rt_kprintf("  - Toggle Count: %u, Current Level: %u\n", count, rt_pin_read(pin));
        rt_thread_mdelay(200);
    }
}

/* 操作方法表实例 */
static const struct rt_led_ops _stm32_led_ops =
{
    .led_init   = stm32_led_init,
    .led_on     = stm32_led_on,
    .led_off    = stm32_led_off,
    .led_toggle = stm32_led_toggle
};

/* 板级自动初始化函数：注册 LED 设备至系统设备管理器 */
int rt_hw_led_init(void)
{
    int result = rt_device_led_register("led", &_stm32_led_ops, RT_NULL);
    if (result == RT_EOK)
    {
        LOG_I("LED Device Driver registered successfully via INIT_BOARD_EXPORT.");
    }
    return result;
}
INIT_BOARD_EXPORT(rt_hw_led_init);

/* ==========================================================================
 * 第三部分：应用层功能测试命令（Application Layer Test）
 * ========================================================================== */

static void virtu_dev_test(int argc, char **argv)
{
    rt_kprintf("\n================ [Virtual LED Device Driver Test] ================\n");

    /* 1. 初始化引脚 */
    rt_led_init(PIN_LED_B, PIN_MODE_OUTPUT);

    /* 2. 点亮 LED 并延时 2 秒 */
    rt_led_on(PIN_LED_B);
    rt_thread_mdelay(2000);

    /* 3. 熄灭 LED 并延时 2 秒 */
    rt_led_off(PIN_LED_B);
    rt_thread_mdelay(2000);

    /* 4. 循环翻转 LED */
    rt_led_toggle(PIN_LED_B);

    rt_kprintf("==================================================================\n\n");
}
MSH_CMD_EXPORT(virtu_dev_test, test custom LED device driver framework);
