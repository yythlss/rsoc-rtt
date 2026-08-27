/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     Leng Mengqi  Key interrupt example for STM32F407
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_common.h>

#ifdef RT_USING_PIN

/* 星火一号按键引脚：KEY_UP (PC5) */
#define KEY_UP_PIN   GET_PIN(C, 5)

/* 按键中断回调函数 */
static void key_up_callback(void *args)
{
    rt_kprintf(">> [中断提醒] KEY_UP 按键被按下了! (PC5) <<\n");
}

/* 初始化并开启按键中断 */
int key_init(void)
{
    /* 1. 设置引脚模式为上拉输入 (默认高电平，按下变低电平) */
    rt_pin_mode(KEY_UP_PIN, PIN_MODE_INPUT_PULLUP);

    /* 2. 绑定中断回调函数：下降沿触发 */
    rt_pin_attach_irq(KEY_UP_PIN, PIN_IRQ_MODE_FALLING, key_up_callback, RT_NULL);

    /* 3. 使能中断通道 */
    rt_pin_irq_enable(KEY_UP_PIN, PIN_IRQ_ENABLE);

    rt_kprintf("[key] 按键中断初始化完成 (引脚 PC5)\n");
    return RT_EOK;
}
INIT_APP_EXPORT(key_init);

#endif /* RT_USING_PIN */
