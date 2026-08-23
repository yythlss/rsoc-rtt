/*
 * Copyright (c) 2006-2024, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22    LinRuiyuan   day4 作业：虚拟字符设备驱动层（test_dev）
 *
 * 说明：
 * 本文件实现一个「虚拟字符设备」的驱动层，演示 RT-Thread 设备驱动框架的
 * 标准对接流程：rt_device_create() 动态创建设备对象 -> 绑定 ops 函数指针
 * -> rt_device_register() 挂入内核设备链表。应用层通过 rt_device_find()
 * 即可拿到句柄并 open/read/write/control。
 *
 * 所谓「虚拟」：设备不对应任何真实硬件，数据只在内存缓冲区中流转，
 * 因此可直接在星火 1 号上跑通，无需外接传感器/总线。
 */
#include <rtthread.h>
#include <rtdevice.h>

#define TEST_DEV_NAME        "test_dev"
#define TEST_DEV_MAGIC       0x55AA55AAu
#define TEST_DEV_VERSION     "v1.0.0-lry"

/* 私有设备结构：必须以 struct rt_device 开头（继承），其余为自定义字段 */
struct test_device
{
    struct rt_device parent;      /* RT-Thread 设备基类，必须放第一 */
    rt_uint8_t  buf[64];          /* 模拟设备内部数据缓冲区 */
    rt_size_t   data_len;         /* 当前有效数据长度 */
    rt_uint32_t magic;            /* 魔数，用于 init 时校验对象合法 */
};
static struct test_device *g_test_dev = RT_NULL;

/* 自定义控制命令：避开内核已用 0x01~0x04 / 0x10~0x12，从 0x20 起分配 */
#define TEST_DEV_CTRL_GET_VERSION    0x20
#define TEST_DEV_CTRL_CLEAR_BUFFER   0x21
#define TEST_DEV_CTRL_ECHO           0x22

/* ------------------------- 6 个 ops 实现 ------------------------- */

static rt_err_t drv_test_init(rt_device_t dev)
{
    struct test_device *td = (struct test_device *)dev;
    rt_kprintf("[test_dev] init ok, magic=0x%08X\n", td->magic);
    return RT_EOK;
}

static rt_err_t drv_test_open(rt_device_t dev, rt_uint16_t oflag)
{
    rt_kprintf("[test_dev] open, oflag=0x%04X\n", oflag);
    return RT_EOK;
}

static rt_err_t drv_test_close(rt_device_t dev)
{
    rt_kprintf("[test_dev] close\n");
    return RT_EOK;
}

/* read：把内部缓冲区内容拷贝给用户，返回实际拷贝字节数 */
static rt_size_t drv_test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_device *td = (struct test_device *)dev;
    rt_size_t copy = (size > td->data_len) ? td->data_len : size;

    if (td->data_len == 0)
    {
        rt_kprintf("[test_dev] read: buffer empty\n");
        return 0;
    }
    rt_memcpy(buffer, td->buf, copy);
    rt_kprintf("[test_dev] read: pos=%d size=%d copied=%d\n", pos, size, copy);
    return copy;
}

/* write：把用户数据存入内部缓冲区，返回实际存入字节数 */
static rt_size_t drv_test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_device *td = (struct test_device *)dev;
    rt_size_t copy = (size > sizeof(td->buf)) ? sizeof(td->buf) : size;

    rt_memcpy(td->buf, buffer, copy);
    td->data_len = copy;
    rt_kprintf("[test_dev] write: pos=%d size=%d stored=%d\n", pos, size, copy);
    return copy;
}

static rt_err_t drv_test_control(rt_device_t dev, int cmd, void *args)
{
    struct test_device *td = (struct test_device *)dev;
    rt_err_t ret = RT_EOK;

    switch (cmd)
    {
    case RT_DEVICE_CTRL_CONFIG:
        rt_kprintf("[test_dev] control: RT_DEVICE_CTRL_CONFIG\n");
        break;
    case TEST_DEV_CTRL_GET_VERSION:
        if (args != RT_NULL)
            rt_strncpy((char *)args, TEST_DEV_VERSION, 16);
        rt_kprintf("[test_dev] control: GET_VERSION=%s\n", TEST_DEV_VERSION);
        break;
    case TEST_DEV_CTRL_CLEAR_BUFFER:
        td->data_len = 0;
        rt_memset(td->buf, 0, sizeof(td->buf));
        rt_kprintf("[test_dev] control: buffer cleared\n");
        break;
    case TEST_DEV_CTRL_ECHO:
        rt_kprintf("[test_dev] control: echo received, len=%d\n", td->data_len);
        break;
    default:
        ret = -RT_EINVAL;
        rt_kprintf("[test_dev] control: unknown cmd %d\n", cmd);
        break;
    }
    return ret;
}

/* ------------------------- 自动初始化（驱动注册） ------------------------- */

static int rt_drv_test_init(void)
{
    rt_err_t ret;

    /* 防重复注册：若已存在同名设备则跳过（自动初始化可能被执行多次） */
    if (rt_device_find(TEST_DEV_NAME) != RT_NULL)
    {
        rt_kprintf("[test_dev] already registered, skip\n");
        return RT_EOK;
    }

    /* 1) 动态创建设备：类型 Char，size 含私有结构（基类 + buf + 字段） */
    rt_device_t dev = rt_device_create(RT_Device_Class_Char,
                                       sizeof(struct test_device));
    if (dev == RT_NULL)
    {
        rt_kprintf("[test_dev] create failed!\n");
        return -RT_ENOMEM;
    }

    /* 2) 初始化私有字段 */
    g_test_dev = (struct test_device *)dev;
    g_test_dev->magic    = TEST_DEV_MAGIC;
    g_test_dev->data_len = 0;
    rt_memset(g_test_dev->buf, 0, sizeof(g_test_dev->buf));

    /* 3) 绑定 ops 函数指针（核心：把驱动实现挂到设备对象上） */
    dev->init    = drv_test_init;
    dev->open    = drv_test_open;
    dev->close   = drv_test_close;
    dev->read    = drv_test_read;
    dev->write   = drv_test_write;
    dev->control = drv_test_control;

    /* 4) 注册进内核设备链表，应用层即可 rt_device_find("test_dev") */
    ret = rt_device_register(dev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR);
    if (ret != RT_EOK)
    {
        rt_kprintf("[test_dev] register failed! ret=%d\n", ret);
        return ret;
    }

    rt_kprintf("[test_dev] registered: %s\n", TEST_DEV_NAME);
    return RT_EOK;
}
/* 设备层初始化阶段自动执行（INIT_DEVICE_EXPORT 在 rt_thread_startup 前跑） */
INIT_DEVICE_EXPORT(rt_drv_test_init);
