/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-26     Leng Mengqi  Modular key driver for RT-Spark board
 */

#ifndef __BSP_KEY_H__
#define __BSP_KEY_H__

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_common.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 按键 ID 枚举 */
typedef enum
{
    BSP_KEY_ID_UP = 0,
    BSP_KEY_ID_DOWN,
    BSP_KEY_ID_LEFT,
    BSP_KEY_ID_RIGHT,
    BSP_KEY_ID_MAX
} bsp_key_id_t;

/* 按键回调函数类型 */
typedef void (*bsp_key_callback_t)(bsp_key_id_t key_id, void *user_data);

/* 按键信息结构体 */
struct bsp_key_info
{
    bsp_key_id_t       id;
    rt_base_t          pin;
    const char         *name;
    rt_tick_t          last_tick;
    rt_uint32_t        press_count;
    bsp_key_callback_t callback;
    void               *user_data;
};

/* 按键驱动对外接口 */
int bsp_key_init(void);
rt_err_t bsp_key_register_callback(bsp_key_id_t key_id, bsp_key_callback_t cb, void *user_data);
rt_uint32_t bsp_key_get_press_count(bsp_key_id_t key_id);
int bsp_key_read_pin(bsp_key_id_t key_id);

#ifdef __cplusplus
}
#endif

#endif /* __BSP_KEY_H__ */
