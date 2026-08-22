/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2024-07-23     Wangyuqiang  first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>

#define LOG_TAG     "drv_test.app"
#define LOG_LVL     LOG_LVL_DBG
#include <ulog.h>

#define KEY_UP       GET_PIN(C, 5)
#define KEY_DOWN     GET_PIN(C, 1)
#define KEY_LEFT     GET_PIN(C, 0)
#define KEY_RIGHT    GET_PIN(C, 4)

static int rt_dev_test_init(void)
{
    rt_device_t test_dev = rt_device_create(RT_Device_Class_Char, 0);
    if(!test_dev)
    {
        LOG_E("test_dev create failed");
        return -RT_ERROR;
    }

    if(rt_device_register(test_dev, "test_dev", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("test_dev register failed!");
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_dev_test_init);
//list device 
static int drv_test_app(void)
{
    rt_device_t test_dev = rt_device_find("test_drv");
    if(test_dev == RT_NULL)
    {
        LOG_E("can not find test drv!");
        return -RT_ERROR;
    }
    
    rt_device_open(test_dev, RT_DEVICE_OFLAG_RDWR);
    rt_device_control(test_dev, RT_DEVICE_CTRL_CONFIG, RT_NULL);
    rt_device_write(test_dev, 100, RT_NULL, 1024);
    rt_device_read(test_dev, 20, RT_NULL, 128);

    rt_device_close(test_dev);

    return RT_EOK;
}
// MSH_CMD_EXPORT(drv_test_app, enable test drv app);

void key_up_callback(void *args)
{
    int value = rt_pin_read(KEY_UP);
    LOG_I("key up! %d", value);
}

void key_down_callback(void *args)
{
    int value = rt_pin_read(KEY_DOWN);
    LOG_I("key down! %d", value);
}

void key_left_callback(void *args)
{
    int value = rt_pin_read(KEY_LEFT);
    LOG_I("key left! %d", value);
}

void key_right_callback(void *args)
{
    int value = rt_pin_read(KEY_RIGHT);
    LOG_I("key right! %d", value);
}

void irq_key_enable()
{
    rt_pin_mode(KEY_UP,   PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY_DOWN, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY_LEFT, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY_DOWN, PIN_MODE_INPUT_PULLUP);

    rt_pin_attach_irq(KEY_UP,    PIN_IRQ_MODE_FALLING, key_up_callback,   RT_NULL);
    rt_pin_attach_irq(KEY_DOWN,  PIN_IRQ_MODE_FALLING, key_down_callback, RT_NULL);
    rt_pin_attach_irq(KEY_LEFT,  PIN_IRQ_MODE_FALLING, key_left_callback, RT_NULL);
    rt_pin_attach_irq(KEY_RIGHT, PIN_IRQ_MODE_FALLING, key_right_callback, RT_NULL);

    rt_pin_irq_enable(KEY_UP,    PIN_IRQ_ENABLE);
    rt_pin_irq_enable(KEY_DOWN,  PIN_IRQ_ENABLE);
    rt_pin_irq_enable(KEY_LEFT,  PIN_IRQ_ENABLE);
    rt_pin_irq_enable(KEY_RIGHT, PIN_IRQ_ENABLE);
}
// MSH_CMD_EXPORT(irq_key_enable, enable key irq);