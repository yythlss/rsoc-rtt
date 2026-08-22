/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20    Mazheng      day4 homework - virtual device test (app layer)
 */
#include <rtthread.h>
#include <rtdevice.h>

#define TEST_DEV_NAME       "test_dev"
#define TEST_DEV_CTRL_GET_VERSION   0x100
#define TEST_DEV_CTRL_CLEAR_BUFFER  0x101
#define TEST_DEV_CTRL_ECHO          0x102

/*
 * MSH 命令：test_dev_demo
 * 功能：查找 test_dev 设备 → 打开 → write → read → control → 关闭
 */
static void test_dev_demo(int argc, char **argv)
{
    rt_device_t dev;
    rt_err_t ret;
    rt_uint8_t wbuf[32] = "Hello RT-Thread Device!";
    rt_uint8_t rbuf[64] = {0};
    rt_size_t rlen;
    char version[16] = {0};

    rt_kprintf("\n========== test_dev Demo ==========\n");

    /* 第1步：查找设备 */
    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("ERROR: cannot find device [%s]!\n", TEST_DEV_NAME);
        rt_kprintf("Make sure test_drv.c is compiled and running.\n");
        return;
    }
    rt_kprintf("[1] Find device: OK -> %s\n", TEST_DEV_NAME);

    /* 第2步：打开设备 */
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK)
    {
        rt_kprintf("ERROR: open failed! ret=%d\n", ret);
        return;
    }
    rt_kprintf("[2] Open device: OK\n");

    /* 第3步：write 测试 */
    rlen = rt_device_write(dev, 0, wbuf, rt_strlen((const char *)wbuf));
    rt_kprintf("[3] Write: sent=%d bytes, ret=%d\n", rt_strlen((const char *)wbuf), rlen);

    /* 第4步：read 测试 */
    rt_memset(rbuf, 0, sizeof(rbuf));
    rlen = rt_device_read(dev, 0, rbuf, sizeof(rbuf));
    rt_kprintf("[4] Read: got=%d bytes, data=\"%s\"\n", rlen, rbuf);

    /* 第5步：control - 获取版本 */
    ret = rt_device_control(dev, TEST_DEV_CTRL_GET_VERSION, version);
    rt_kprintf("[5] Control GET_VERSION: ret=%d, version=%s\n", ret, version);

    /* 第6步：control - ECHO */
    ret = rt_device_control(dev, TEST_DEV_CTRL_ECHO, RT_NULL);
    rt_kprintf("[6] Control ECHO: ret=%d\n", ret);

    /* 第7步：control - 清空缓冲区 */
    ret = rt_device_control(dev, TEST_DEV_CTRL_CLEAR_BUFFER, RT_NULL);
    rt_kprintf("[7] Control CLEAR_BUFFER: ret=%d\n", ret);

    /* 第8步：再次 read（验证清空） */
    rt_memset(rbuf, 0, sizeof(rbuf));
    rlen = rt_device_read(dev, 0, rbuf, sizeof(rbuf));
    rt_kprintf("[8] Read after clear: got=%d bytes (should be 0)\n", rlen);

    /* 第9步：关闭设备 */
    rt_device_close(dev);
    rt_kprintf("[9] Close device: OK\n");

    rt_kprintf("========== test_dev Demo Done ==========\n\n");
}
/* 导出 MSH 命令：test_dev_demo */
MSH_CMD_EXPORT(test_dev_demo, test_dev virtual device driver demo);
