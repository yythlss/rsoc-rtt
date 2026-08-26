/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-26     Leng Mengqi  I2C peripheral communication sample
 */

#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "i2c.sample"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#ifdef RT_USING_I2C

#define I2C_BUS_NAME    "i2c2"
#define SLAVE_ADDR      (0x68)  /* 典型 7 位从机地址 (如 ICM20608 / MPU6050) */

/**
 * @brief I2C 单字节写入示例
 */
void i2c_sample_single_byte_write(void)
{
    struct rt_i2c_bus_device *i2c_bus;
    struct rt_i2c_msg msgs;
    rt_uint8_t buf[2];

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(I2C_BUS_NAME);
    if (i2c_bus == RT_NULL)
    {
        rt_kprintf("[I2C] Can't find I2C bus '%s'!\n", I2C_BUS_NAME);
        return;
    }

    buf[0] = 0x6B; /* 目标寄存器地址 */

    msgs.addr  = SLAVE_ADDR;
    msgs.flags = RT_I2C_WR;
    msgs.buf   = buf;
    msgs.len   = 1;

    if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
    {
        rt_kprintf("[I2C] Single byte write to reg 0x%02X succeeded!\n", buf[0]);
    }
    else
    {
        rt_kprintf("[I2C] Single byte write failed.\n");
    }
}

/**
 * @brief I2C 多字节连续写入示例
 */
void i2c_sample_multi_byte_write(void)
{
    struct rt_i2c_bus_device *i2c_bus;
    struct rt_i2c_msg msgs;
    rt_uint8_t buf[4];

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(I2C_BUS_NAME);
    if (i2c_bus == RT_NULL)
    {
        rt_kprintf("[I2C] Can't find I2C bus '%s'!\n", I2C_BUS_NAME);
        return;
    }

    buf[0] = 0x19; /* 起始寄存器地址 (SMPLRT_DIV) */
    buf[1] = 0x07; /* 数据 1 */
    buf[2] = 0x06; /* 数据 2 */
    buf[3] = 0x01; /* 数据 3 */

    msgs.addr  = SLAVE_ADDR;
    msgs.flags = RT_I2C_WR;
    msgs.buf   = buf;
    msgs.len   = 4;

    if (rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
    {
        rt_kprintf("[I2C] Multi-byte (4 bytes) write succeeded!\n");
    }
    else
    {
        rt_kprintf("[I2C] Multi-byte write failed.\n");
    }
}

/**
 * @brief I2C 组合时序读取示例 (先写寄存器地址，再读数据)
 */
void i2c_sample_single_byte_read(void)
{
    struct rt_i2c_bus_device *i2c_bus;
    struct rt_i2c_msg msgs[2];
    rt_uint8_t send_buf[1];
    rt_uint8_t recv_buf[1];

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(I2C_BUS_NAME);
    if (i2c_bus == RT_NULL)
    {
        rt_kprintf("[I2C] Can't find I2C bus '%s'!\n", I2C_BUS_NAME);
        return;
    }

    send_buf[0] = 0x75; /* WHO_AM_I 寄存器 */
    recv_buf[0] = 0x00;

    /* 消息 1: 写入目标寄存器地址 */
    msgs[0].addr  = SLAVE_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf   = send_buf;
    msgs[0].len   = 1;

    /* 消息 2: 重新发起起始位并读取返回数据 */
    msgs[1].addr  = SLAVE_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf   = recv_buf;
    msgs[1].len   = 1;

    if (rt_i2c_transfer(i2c_bus, msgs, 2) == 2)
    {
        rt_kprintf("[I2C] Single byte read from reg 0x75: 0x%02X (WHO_AM_I) succeeded!\n", recv_buf[0]);
    }
    else
    {
        rt_kprintf("[I2C] Single byte read failed.\n");
    }
}

#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT(i2c_sample_single_byte_write, I2C single byte write test);
MSH_CMD_EXPORT(i2c_sample_multi_byte_write, I2C multi byte write test);
MSH_CMD_EXPORT(i2c_sample_single_byte_read, I2C combined write-read test);
#endif

#endif /* RT_USING_I2C */
