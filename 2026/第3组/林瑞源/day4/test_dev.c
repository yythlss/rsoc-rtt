/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22    LinRuiyuan   day4 作业：虚拟设备应用层测试（test_dev_demo）
 *
 * 说明：
 * 应用层通过标准 rt_device_* 接口使用 test_drv.c 注册的虚拟设备，
 * 完整走通 find -> open -> write -> read -> control -> close 流程，
 * 直观展示「应用层与底层驱动通过 IO 管理层解耦」的设计。
 */
#include <rtthread.h>
#include <rtdevice.h>

#define TEST_DEV_NAME        "test_dev"
/* 自定义控制命令：与 test_drv.c 保持一致，避开内核已用值 */
#define TEST_DEV_CTRL_GET_VERSION    0x20
#define TEST_DEV_CTRL_CLEAR_BUFFER   0x21
#define TEST_DEV_CTRL_ECHO           0x22

/*
 * MSH 命令：test_dev_demo
 * 演示虚拟设备完整生命周期
 */
static void test_dev_demo(int argc, char **argv)
{
    rt_device_t dev;
    rt_err_t ret;
    rt_uint8_t wbuf[] = "Hello RT-Thread virtual device!";
    rt_uint8_t rbuf[64] = {0};
    rt_size_t rlen;
    char version[16] = {0};

    rt_kprintf("\n====== test_dev Demo ======\n");

    /* [1] 查找设备：IO 管理层在设备链表中按名检索 */
    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("ERROR: cannot find [%s], 请确认 test_drv.c 已编译进固件\n", TEST_DEV_NAME);
        return;
    }
    rt_kprintf("[1] find: OK -> %s\n", TEST_DEV_NAME);

    /* [2] 打开设备：触发 drv_test_open，引用计数 +1 */
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK)
    {
        rt_kprintf("ERROR: open failed, ret=%d\n", ret);
        return;
    }
    rt_kprintf("[2] open: OK\n");

    /* [3] 写入：触发 drv_test_write，数据存入设备缓冲区 */
    rlen = rt_device_write(dev, 0, wbuf, rt_strlen((const char *)wbuf));
    rt_kprintf("[3] write: sent=%d ret=%d\n", rt_strlen((const char *)wbuf), rlen);

    /* [4] 读出：触发 drv_test_read，回读缓冲区内容 */
    rlen = rt_device_read(dev, 0, rbuf, sizeof(rbuf));
    rt_kprintf("[4] read: got=%d bytes, data=\"%s\"\n", rlen, rbuf);

    /* [5] control 取版本 */
    ret = rt_device_control(dev, TEST_DEV_CTRL_GET_VERSION, version);
    rt_kprintf("[5] control GET_VERSION: ret=%d version=%s\n", ret, version);

    /* [6] control echo */
    ret = rt_device_control(dev, TEST_DEV_CTRL_ECHO, RT_NULL);
    rt_kprintf("[6] control ECHO: ret=%d\n", ret);

    /* [7] control 清空缓冲区 */
    ret = rt_device_control(dev, TEST_DEV_CTRL_CLEAR_BUFFER, RT_NULL);
    rt_kprintf("[7] control CLEAR_BUFFER: ret=%d\n", ret);

    /* [8] 再次读，应得到 0 字节（验证清空生效） */
    rt_memset(rbuf, 0, sizeof(rbuf));
    rlen = rt_device_read(dev, 0, rbuf, sizeof(rbuf));
    rt_kprintf("[8] read after clear: got=%d bytes (expect 0)\n", rlen);

    /* [9] 关闭：引用计数 -1，归零后触发 drv_test_close */
    rt_device_close(dev);
    rt_kprintf("[9] close: OK\n");

    rt_kprintf("====== test_dev Demo Done ======\n\n");
}
MSH_CMD_EXPORT(test_dev_demo, test virtual device driver framework demo);
