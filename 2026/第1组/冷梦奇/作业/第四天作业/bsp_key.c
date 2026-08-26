/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-26     Leng Mengqi  Modular key driver for RT-Spark board
 */

#include "bsp_key.h"

#define DBG_TAG "bsp.key"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifdef RT_USING_PIN

#define DEBOUNCE_INTERVAL_MS   (200)

/* 星火一号 4 方向按键对象表 */
static struct bsp_key_info s_key_table[BSP_KEY_ID_MAX] =
{
    { BSP_KEY_ID_UP,    GET_PIN(C, 5), "KEY_UP",    0, 0, RT_NULL, RT_NULL },
    { BSP_KEY_ID_DOWN,  GET_PIN(C, 1), "KEY_DOWN",  0, 0, RT_NULL, RT_NULL },
    { BSP_KEY_ID_LEFT,  GET_PIN(C, 0), "KEY_LEFT",  0, 0, RT_NULL, RT_NULL },
    { BSP_KEY_ID_RIGHT, GET_PIN(C, 4), "KEY_RIGHT", 0, 0, RT_NULL, RT_NULL },
};

/* 统一通用中断分发与非阻塞时间戳消抖处理 */
static void _key_isr_dispatcher(void *args)
{
    struct bsp_key_info *key = (struct bsp_key_info *)args;
    rt_tick_t now = rt_tick_get();

    RT_ASSERT(key != RT_NULL);

    /* 软件时间戳滤波：过滤 200ms 内的机械振颤 */
    if (now - key->last_tick < rt_tick_from_millisecond(DEBOUNCE_INTERVAL_MS))
    {
        return;
    }
    key->last_tick = now;
    key->press_count++;

    int pin_val = rt_pin_read(key->pin);
    LOG_I("[%s] Pressed! Level=%d, Total Count=%u, Timestamp=%u ms",
          key->name, pin_val, (unsigned int)key->press_count, (unsigned int)rt_tick_get_millisecond());

    /* 触发用户注册的高阶事件回调 */
    if (key->callback != RT_NULL)
    {
        key->callback(key->id, key->user_data);
    }
}

/**
 * @brief 初始化星火一号 4 方向按键模块
 */
int bsp_key_init(void)
{
    for (int i = 0; i < BSP_KEY_ID_MAX; i++)
    {
        /* 1. 设置为上拉输入模式 */
        rt_pin_mode(s_key_table[i].pin, PIN_MODE_INPUT_PULLUP);

        /* 2. 绑定下降沿中断 */
        rt_pin_attach_irq(s_key_table[i].pin, PIN_IRQ_MODE_FALLING,
                          _key_isr_dispatcher, &s_key_table[i]);

        /* 3. 使能中断通道 */
        rt_pin_irq_enable(s_key_table[i].pin, PIN_IRQ_ENABLE);
    }

    LOG_I("Spark-1 4-Direction Key Driver initialized successfully (UP:PC5, DOWN:PC1, LEFT:PC0, RIGHT:PC4).");
    return RT_EOK;
}
INIT_DEVICE_EXPORT(bsp_key_init);

/**
 * @brief 注册按键触发事件回调
 */
rt_err_t bsp_key_register_callback(bsp_key_id_t key_id, bsp_key_callback_t cb, void *user_data)
{
    if (key_id >= BSP_KEY_ID_MAX)
    {
        return -RT_EINVAL;
    }
    s_key_table[key_id].callback  = cb;
    s_key_table[key_id].user_data = user_data;
    return RT_EOK;
}

/**
 * @brief 获取按键累计触发次数
 */
rt_uint32_t bsp_key_get_press_count(bsp_key_id_t key_id)
{
    if (key_id >= BSP_KEY_ID_MAX) return 0;
    return s_key_table[key_id].press_count;
}

/**
 * @brief 直接读取当前按键物理电平
 */
int bsp_key_read_pin(bsp_key_id_t key_id)
{
    if (key_id >= BSP_KEY_ID_MAX) return -1;
    return rt_pin_read(s_key_table[key_id].pin);
}

/* ==================== FinSH 调试命令 ==================== */
#ifdef RT_USING_FINSH
#include <finsh.h>

static void bsp_key_status(void)
{
    rt_kprintf("================ Spark-1 Key Status ================\n");
    for (int i = 0; i < BSP_KEY_ID_MAX; i++)
    {
        rt_kprintf("  [%-9s] Pin: %-3d | Level: %d | Press Count: %u\n",
                   s_key_table[i].name,
                   (int)s_key_table[i].pin,
                   rt_pin_read(s_key_table[i].pin),
                   (unsigned int)s_key_table[i].press_count);
    }
    rt_kprintf("====================================================\n");
}
MSH_CMD_EXPORT(bsp_key_status, Query Spark-1 4-direction keys real-time status);
#endif

#endif /* RT_USING_PIN */
