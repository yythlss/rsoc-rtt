/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     wangqi       the first version
 */
//虚拟串口驱动实现
#include "drv_virtual_uart.h"
#include <stdio.h>
#include <string.h>
#include <rtthread.h>

/* 驱动私有数据 */
struct vuart_data {
    uint32_t baudrate;
    uint8_t  data_bits;
};

static int vuart_init(struct test_dev *dev)
{
    struct vuart_data *data = (struct vuart_data *)dev->user_data;
    data->baudrate  = 115200;
    data->data_bits = 8;
    rt_kprintf("[vuart] init: baud=%lu, bits=%u\n",
           (unsigned long)data->baudrate, data->data_bits);
    return 0;
}

static int vuart_open(struct test_dev *dev, int oflag)
{
    (void)dev; (void)oflag;
    rt_kprintf("[vuart] open\n");
    return 0;
}

static int vuart_close(struct test_dev *dev)
{
    (void)dev;
    rt_kprintf("[vuart] close\n");
    return 0;
}

static int vuart_read(struct test_dev *dev, void *buf, uint32_t size)
{
    (void)dev; (void)buf;
    rt_kprintf("[vuart] read %lu bytes\n", (unsigned long)size);
    return 0;
}

static int vuart_write(struct test_dev *dev, const void *buf, uint32_t size)
{
    (void)dev;
    rt_kprintf("[vuart] write %lu bytes: ", (unsigned long)size);
    for (uint32_t i = 0; i < size; i++) {
        putchar(((const char *)buf)[i]);
    }
    putchar('\n');
    return (int)size;
}

static int vuart_control(struct test_dev *dev, int cmd, void *arg)
{
    struct vuart_data *data = (struct vuart_data *)dev->user_data;

    switch (cmd) {
    case VUART_CMD_SET_BAUDRATE:
        data->baudrate = *(uint32_t *)arg;
        rt_kprintf("[vuart] set baudrate = %lu\n", (unsigned long)data->baudrate);
        break;
    case VUART_CMD_GET_BAUDRATE:
        *(uint32_t *)arg = data->baudrate;
        break;
    case VUART_CMD_SET_DATA_BITS:
        data->data_bits = *(uint8_t *)arg;
        rt_kprintf("[vuart] set data_bits = %u\n", data->data_bits);
        break;
    default:
        return -1;
    }
    return 0;
}

/* 操作函数表 */
static const struct test_dev_ops vuart_ops = {
    .init    = vuart_init,
    .open    = vuart_open,
    .close   = vuart_close,
    .read    = vuart_read,
    .write   = vuart_write,
    .control = vuart_control,
};

/* 私有数据 & 设备实例 */
static struct vuart_data  uart0_data;
static struct test_dev    uart0_dev = {
    .name      = "vuart0",
    .ops       = &vuart_ops,
    .user_data = &uart0_data,
};

/* 驱动初始化入口 */
int virtual_uart_init(void)
{
    int ret = test_dev_register(&uart0_dev);
    if (ret != 0) return ret;

    if (vuart_ops.init)
        return vuart_ops.init(&uart0_dev);

    return 0;
}
