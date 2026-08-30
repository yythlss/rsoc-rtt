/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     RT-Thread    first version
 */
//应用层调用
#include <rtthread.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#include "test_dev.h"
#include "drv_virtual_uart.h"
#include <string.h>

int main(void)
{
    rt_kprintf("\n=== test_dev Framework Test ===\n\n");
    virtual_uart_init();
    test_dev_list();

    struct test_dev *dev = test_dev_find("vuart0");
    if (dev == RT_NULL) {    // NULL → RT_NULL
        rt_kprintf("ERROR: device not found!\n");
        return -1;
    }

    test_dev_open(dev, TEST_DEV_OFLAG_RDWR);

    const char *msg = "Hello, test_dev on Spark-1!";
    test_dev_write(dev, msg, (rt_uint32_t)strlen(msg));

    rt_uint32_t new_baud = 9600;
    test_dev_control(dev, VUART_CMD_SET_BAUDRATE, &new_baud);

    rt_uint32_t cur_baud = 0;
    test_dev_control(dev, VUART_CMD_GET_BAUDRATE, &cur_baud);
    rt_kprintf("current baudrate = %d\n", cur_baud);

    test_dev_close(dev);

    rt_kprintf("\n=== Test Done ===\n");
    return 0;
}
