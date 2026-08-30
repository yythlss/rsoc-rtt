/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2023-5-10      ShiHao       first version
 */

#include <rtthread.h>
#include <stdio.h>
#include <rtdevice.h>
#include <board.h>
#define DBG_TAG "main"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>

/* 配置 LED 灯引脚 */
#define PIN_LED_B              GET_PIN(F, 11)      // PF11 :  LED_B        --> LED
#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED
rt_mailbox_t mqtt_mailbox=NULL;
int main(void)
{
    unsigned int count = 1;

    /* 设置 LED 引脚为输出模式 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);

    while (count > 0)
    {
        /* LED 灯亮 */
        rt_pin_write(PIN_LED_R, PIN_LOW);
//        LOG_D("led on, count: %d", count);
        rt_thread_mdelay(500);

        /* LED 灯灭 */
        rt_pin_write(PIN_LED_R, PIN_HIGH);
//        LOG_D("led off");
        rt_thread_mdelay(500);

        count++;
    }

    return 0;
}


int app_init(void)
{
    extern int mqtt(void);
    extern int ath10_init(void);

    mqtt_mailbox = rt_mb_create ("mqtt_mailbox", 20, RT_IPC_FLAG_PRIO);
    if(NULL == mqtt_mailbox)
    {
        return RT_ERROR;
    }
    if(RT_EOK != mqtt())
    {
        return RT_ERROR;
    }
    if(RT_EOK != ath10_init())
    {
        return RT_ERROR;
    }
    return RT_EOK;
}

MSH_CMD_EXPORT(app_init, app_init);
