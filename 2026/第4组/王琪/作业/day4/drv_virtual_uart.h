/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     wangqi       the first version
 */
#ifndef APPLICATIONS_DRV_VIRTUAL_UART_H_
#define APPLICATIONS_DRV_VIRTUAL_UART_H_

#include "test_dev.h"

/* 自定义控制命令 */
#define VUART_CMD_SET_BAUDRATE   0x01
#define VUART_CMD_GET_BAUDRATE   0x02
#define VUART_CMD_SET_DATA_BITS  0x03

int virtual_uart_init(void);

#endif /* APPLICATIONS_DRV_VIRTUAL_UART_H_ */
