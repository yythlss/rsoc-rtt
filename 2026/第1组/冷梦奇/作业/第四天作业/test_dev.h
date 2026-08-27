/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     Leng Mengqi  Simple beginner-friendly test_dev header
 */

#ifndef __TEST_DEV_H__
#define __TEST_DEV_H__

#include <rtthread.h>
#include <rtdevice.h>

/* 简单的控制命令：清空缓冲区 */
#define TEST_DEV_CMD_CLEAR   (1)

/* 注册与测试函数 */
int test_dev_register(void);

#endif /* __TEST_DEV_H__ */
