/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     Leng Mengqi  Simple beginner-friendly test_dev virtual device
 */

#include "test_dev.h"
#include <string.h>

#define TEST_BUF_SIZE   64

/* 1. 定义一个设备对象和一块 64 字节的虚拟内存缓冲区 */
static struct rt_device s_test_dev;
static char s_buffer[TEST_BUF_SIZE];
static rt_size_t s_data_len = 0;

/* 2. 实现设备的 6 个基础操作函数 (简单好懂，没有复杂算法) */

/* 初始化函数：清空缓冲区 */
static rt_err_t test_dev_init(rt_device_t dev)
{
    rt_memset(s_buffer, 0, sizeof(s_buffer));
    s_data_len = 0;
    rt_kprintf("[test_dev] 1. 初始化成功 (init)\n");
    return RT_EOK;
}

/* 打开设备函数 */
static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    rt_kprintf("[test_dev] 2. 设备已打开 (open)\n");
    return RT_EOK;
}

/* 关闭设备函数 */
static rt_err_t test_dev_close(rt_device_t dev)
{
    rt_kprintf("[test_dev] 5. 设备已关闭 (close)\n");
    return RT_EOK;
}

/* 读取函数：把内部缓冲区的数据拷贝给用户 */
static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    if (buffer == RT_NULL || size == 0 || s_data_len == 0)
    {
        return 0;
    }

    /* 实际读取大小不能超过已有数据量 */
    rt_size_t read_bytes = (size < s_data_len) ? size : s_data_len;
    rt_memcpy(buffer, s_buffer, read_bytes);

    /* 读出后将剩余数据往前移 */
    s_data_len -= read_bytes;
    for (rt_size_t i = 0; i < s_data_len; i++)
    {
        s_buffer[i] = s_buffer[i + read_bytes];
    }

    rt_kprintf("[test_dev] 4. 读出数据 %d 字节\n", (int)read_bytes);
    return read_bytes;
}

/* 写入函数：把用户传进来的数据存到内部缓冲区 */
static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    if (buffer == RT_NULL || size == 0)
    {
        return 0;
    }

    /* 实际写入大小不能超过缓冲区剩余容量 */
    rt_size_t write_bytes = (size < (TEST_BUF_SIZE - s_data_len)) ? size : (TEST_BUF_SIZE - s_data_len);
    rt_memcpy(s_buffer + s_data_len, buffer, write_bytes);
    s_data_len += write_bytes;

    rt_kprintf("[test_dev] 3. 写入数据 %d 字节\n", (int)write_bytes);
    return write_bytes;
}

/* 控制函数：接收控制命令 */
static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    if (cmd == TEST_DEV_CMD_CLEAR)
    {
        rt_memset(s_buffer, 0, sizeof(s_buffer));
        s_data_len = 0;
        rt_kprintf("[test_dev] 缓冲区已清空 (control)\n");
        return RT_EOK;
    }
    return -RT_ENOSYS;
}

/* 3. 自动向系统注册 "test_dev" 设备 */
int test_dev_register(void)
{
    /* 填写设备结构体 */
    s_test_dev.type    = RT_Device_Class_Char; /* 字符设备 */
    s_test_dev.init    = test_dev_init;
    s_test_dev.open    = test_dev_open;
    s_test_dev.close   = test_dev_close;
    s_test_dev.read    = test_dev_read;
    s_test_dev.write   = test_dev_write;
    s_test_dev.control = test_dev_control;

    /* 注册设备名称为 "test_dev" */
    rt_err_t ret = rt_device_register(&s_test_dev, "test_dev", RT_DEVICE_FLAG_RDWR);
    if (ret == RT_EOK)
    {
        rt_kprintf("[test_dev] 虚拟设备 test_dev 注册成功!\n");
    }
    return ret;
}
INIT_DEVICE_EXPORT(test_dev_register);

/* 4. 简单明了的测试函数，在串口终端输入 test_dev_sample 即可执行 */
#ifdef RT_USING_FINSH
#include <finsh.h>

static void test_dev_sample(void)
{
    rt_device_t dev;
    char write_buf[] = "hello rt-thread";
    char read_buf[32] = {0};

    rt_kprintf("\n--- 开始测试 test_dev 设备 ---\n");

    /* 1. 查找设备 */
    dev = rt_device_find("test_dev");
    if (dev == RT_NULL)
    {
        rt_kprintf("错误: 找不到 test_dev 设备!\n");
        return;
    }

    /* 2. 打开设备 */
    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);

    /* 3. 写入数据 */
    rt_device_write(dev, 0, write_buf, sizeof(write_buf));

    /* 4. 读取数据 */
    rt_device_read(dev, 0, read_buf, sizeof(read_buf));
    rt_kprintf(">> 从设备中读出的字符串: [%s] <<\n", read_buf);

    /* 5. 关闭设备 */
    rt_device_close(dev);

    rt_kprintf("--- test_dev 测试成功! ---\n\n");
}
MSH_CMD_EXPORT(test_dev_sample, test test_dev virtual device);
#endif
