/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-26     Leng Mengqi  Key interrupt & debouncing sample on STM32F407
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_common.h>

#define DBG_TAG "key.sample"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifdef RT_USING_PIN

/* 星火一号 4 方向按键引脚定义 */
#define KEY_UP       GET_PIN(C, 5)
#define KEY_DOWN     GET_PIN(C, 1)
#define KEY_LEFT     GET_PIN(C, 0)
#define KEY_RIGHT    GET_PIN(C, 4)

/* 记录上一次按键触发的时间戳（用于软件时间戳消抖） */
static rt_tick_t s_last_tick_up    = 0;
static rt_tick_t s_last_tick_down  = 0;
static rt_tick_t s_last_tick_left  = 0;
static rt_tick_t s_last_tick_right = 0;

#define DEBOUNCE_TIME_MS  (200)

static void key_up_callback(void *args)
{
    rt_tick_t cur_tick = rt_tick_get();
    if (cur_tick - s_last_tick_up < rt_tick_from_millisecond(DEBOUNCE_TIME_MS))
    {
        return; /* 抖动过滤 */
    }
    s_last_tick_up = cur_tick;

    int value = rt_pin_read(KEY_UP);
    rt_kprintf("[KEY IRQ] KEY_UP (PC5) Pressed! Pin Level = %d (Tick=%u)\n", value, (unsigned int)cur_tick);
}

static void key_down_callback(void *args)
{
    rt_tick_t cur_tick = rt_tick_get();
    if (cur_tick - s_last_tick_down < rt_tick_from_millisecond(DEBOUNCE_TIME_MS))
    {
        return;
    }
    s_last_tick_down = cur_tick;

    int value = rt_pin_read(KEY_DOWN);
    rt_kprintf("[KEY IRQ] KEY_DOWN (PC1) Pressed! Pin Level = %d (Tick=%u)\n", value, (unsigned int)cur_tick);
}

static void key_left_callback(void *args)
{
    rt_tick_t cur_tick = rt_tick_get();
    if (cur_tick - s_last_tick_left < rt_tick_from_millisecond(DEBOUNCE_TIME_MS))
    {
        return;
    }
    s_last_tick_left = cur_tick;

    int value = rt_pin_read(KEY_LEFT);
    rt_kprintf("[KEY IRQ] KEY_LEFT (PC0) Pressed! Pin Level = %d (Tick=%u)\n", value, (unsigned int)cur_tick);
}

static void key_right_callback(void *args)
{
    rt_tick_t cur_tick = rt_tick_get();
    if (cur_tick - s_last_tick_right < rt_tick_from_millisecond(DEBOUNCE_TIME_MS))
    {
        return;
    }
    s_last_tick_right = cur_tick;

    int value = rt_pin_read(KEY_RIGHT);
    rt_kprintf("[KEY IRQ] KEY_RIGHT (PC4) Pressed! Pin Level = %d (Tick=%u)\n", value, (unsigned int)cur_tick);
}

/**
 * @brief 配置并使能星火一号 4 方向按键中断
 */
int irq_key_enable(void)
{
    /* 1. 设置引脚模式为上拉输入 */
    rt_pin_mode(KEY_UP,    PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY_DOWN,  PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY_LEFT,  PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(KEY_RIGHT, PIN_MODE_INPUT_PULLUP);

    /* 2. 绑定下降沿中断回调函数 */
    rt_pin_attach_irq(KEY_UP,    PIN_IRQ_MODE_FALLING, key_up_callback,    RT_NULL);
    rt_pin_attach_irq(KEY_DOWN,  PIN_IRQ_MODE_FALLING, key_down_callback,  RT_NULL);
    rt_pin_attach_irq(KEY_LEFT,  PIN_IRQ_MODE_FALLING, key_left_callback,  RT_NULL);
    rt_pin_attach_irq(KEY_RIGHT, PIN_IRQ_MODE_FALLING, key_right_callback, RT_NULL);

    /* 3. 使能中断通道 */
    rt_pin_irq_enable(KEY_UP,    PIN_IRQ_ENABLE);
    rt_pin_irq_enable(KEY_DOWN,  PIN_IRQ_ENABLE);
    rt_pin_irq_enable(KEY_LEFT,  PIN_IRQ_ENABLE);
    rt_pin_irq_enable(KEY_RIGHT, PIN_IRQ_ENABLE);

    LOG_I("Spark-1 4-Direction Key IRQ attached & enabled successfully (UP:PC5, DOWN:PC1, LEFT:PC0, RIGHT:PC4).");
    return RT_EOK;
}

#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT(irq_key_enable, Enable 4-Direction key interrupt with debouncing);
#endif

#endif /* RT_USING_PIN */
