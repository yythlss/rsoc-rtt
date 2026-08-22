/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     yanran       the first version
 */
#include <rtthread.h>
#include "virtu_dev.h"
#include <rtdevice.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/**************************************************************************************/
//实现stm32_led的bsp函数
static void stm32_led_init(struct rt_device *device, rt_base_t pin, rt_base_t mode)
{
    rt_kprintf("led init success\n");
    rt_pin_mode(pin,mode);
    return;
}
static void stm32_led_on (struct rt_device *device, rt_base_t pin)
{
    rt_kprintf("led on\n");
    rt_pin_write(pin,PIN_LOW);
    return;
}
static void stm32_led_off (struct rt_device *device, rt_base_t pin)
{
    rt_kprintf("led off\n");
    rt_pin_write(pin,PIN_HIGH);
    return;
}
static void stm32_led_toggle (struct rt_device *device, rt_base_t pin)
{
    //以闪烁10次为例子
    rt_kprintf("led toggle...\n");
    rt_uint8_t count = 0;
    while(count < 10)
    {
        rt_pin_write(pin,PIN_LOW);
        rt_thread_mdelay(500);
        rt_pin_write(pin,PIN_HIGH);
        rt_thread_mdelay(500);
        count++;
        rt_kprintf("count: %d\n",count);
    }
    return;
}

//定义led的bsp驱动层ops表
const static struct rt_led_ops _stm32_led_ops =
{
    stm32_led_init,
    stm32_led_on,
    stm32_led_off,
    stm32_led_toggle,
};

/********************************************************************************************/

static struct rt_device_led _hw_led;

//注册led设备
int rt_device_led_register(const char *name, const struct rt_led_ops *ops, void *user_data)
{
    _hw_led.parent.type         = RT_Device_Class_Unknown;
    _hw_led.parent.rx_indicate  = RT_NULL;
    _hw_led.parent.tx_complete  = RT_NULL;

    _hw_led.parent.init         = RT_NULL;
    _hw_led.parent.open         = RT_NULL;
    _hw_led.parent.close        = RT_NULL;
    _hw_led.parent.read         = RT_NULL;
    _hw_led.parent.write        = RT_NULL;
    _hw_led.parent.control      = RT_NULL;

    _hw_led.ops                 = ops;
    _hw_led.parent.user_data    = user_data;

    /* register a character device */
    rt_device_register(&_hw_led.parent, name, RT_DEVICE_FLAG_RDWR);
    return 0;
}

//上电后自动完成led设备的注册,修改这里的_stm32_led_ops实现调用不同芯片的bsp函数
int rt_hw_led_init(void)
{
    // 把BSP的硬件ops传给框架层 rt_device_led_register
    rt_device_led_register("led", &_stm32_led_ops, RT_NULL);
    return 0;
}
INIT_BOARD_EXPORT(rt_hw_led_init); // 系统上电自动执行！

/***********************************************************************************/
//RT-Thread Hardware LED APIs
//开放给rtthread使用的led api，实际上调用对应ops表指向的bsp函数
void rt_led_on(rt_base_t pin)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    _hw_led.ops->led_on(&_hw_led.parent, pin);
}

void rt_led_off(rt_base_t pin)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    _hw_led.ops->led_off(&_hw_led.parent, pin);
}

void rt_led_init(rt_base_t pin, rt_base_t mode)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    _hw_led.ops->led_init(&_hw_led.parent, pin, mode);
}

void rt_led_toggle(rt_base_t pin)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    _hw_led.ops->led_toggle(&_hw_led.parent, pin);
}
/************************************************************************/
//自定义的led设备的测试程序
#define PIN_LED_B       rt_pin_get("PF.11")
#define PIN_MODE        PIN_MODE_OUTPUT

static void virtu_dev_test(void)
{
    rt_led_init(PIN_LED_B,PIN_MODE);
    rt_led_on(PIN_LED_B);
    rt_thread_mdelay(2000);
    rt_led_off(PIN_LED_B);
    rt_thread_mdelay(2000);
    rt_led_toggle(PIN_LED_B);
}

MSH_CMD_EXPORT(virtu_dev_test, virtu_dev sample);

