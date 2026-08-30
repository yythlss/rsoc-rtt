/*
 * Copyright (c) 2025
 *
 * LED 控制（板载 RGB 红色 LED，PF12，共阳，低电平点亮）
 *
 * 变更记录:
 * 2025        first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "app.h"

#define DBG_TAG "led"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* 板载红色 LED 引脚（共阳，低电平点亮） */
#define PIN_LED_R   GET_PIN(F, 12)

/* 当前 LED 状态：RT_TRUE = 点亮，RT_FALSE = 熄灭 */
static rt_bool_t led_state = RT_FALSE;

void app_led_init(void)
{
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);   /* 上电默认熄灭 */
    led_state = RT_FALSE;
    LOG_I("LED (PF12, red) initialized, state = off");
}

void app_led_on(void)
{
    rt_pin_write(PIN_LED_R, PIN_LOW);
    led_state = RT_TRUE;
}

void app_led_off(void)
{
    rt_pin_write(PIN_LED_R, PIN_HIGH);
    led_state = RT_FALSE;
}

void app_led_toggle(void)
{
    if (led_state == RT_TRUE)
    {
        app_led_off();
    }
    else
    {
        app_led_on();
    }
}

rt_bool_t app_led_is_on(void)
{
    return led_state;
}
