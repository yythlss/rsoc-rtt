/*
 * Copyright (c) 2026, 霍渝陈 (RSOC 第4组)
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author    Notes
 * 2026-08-23     霍渝陈    Day4 作业：虚拟设备驱动框架层（test_dev）
 *
 * 说明：
 * 本文件实现一个「虚拟字符设备」的驱动层，完整走一遍 RT-Thread 设备驱动
 * 框架的标准对接流程：
 *     rt_device_create() 动态创建设备对象
 *     -> 绑定 ops 函数指针（init/open/close/read/write/control）
 *     -> rt_device_register() 挂入内核设备链表
 * 所谓「虚拟」：设备不对应任何真实硬件，数据只在内核内存缓冲区中流转，
 * 因此无需外接任何传感器/总线即可在星火 1 号上跑通，用于理解
 * 「驱动层实现 ops -> IO 管理层注册 -> 应用层通过 rt_device_* 调用」的分层思想。
 */
#include <rtthread.h>
#include <rtdevice.h>

#define TEST_DEV_NAME         "test_dev"
#define TEST_DEV_VERSION      "v1.0.0"
#define TEST_DEV_MAGIC        0x55AA55AAu

/* 自定义 control 命令字：内核已用 0x01~0x04 / 0x10~0x12，自定义从 0x20 起 */
#define TEST_DEV_CTRL_GET_VERSION   0x20
#define TEST_DEV_CTRL_ECHO          0x21
#define TEST_DEV_CTRL_CLEAR_BUFFER  0x22

/*
 * 私有设备结构：首成员必须是 struct rt_device（继承基类），
 * 后面追加的 buf / data_len / magic 是驱动私有字段。
 * 通过 rt_device_create(type, sizeof(struct test_device)) 一次性分配。
 */
struct test_device
{
    struct rt_device parent;      /* 基类，必须放第一个成员 */
    rt_uint8_t  buf[64];          /* 虚拟设备内部缓冲区（模拟硬件数据区） */
    rt_size_t   data_len;         /* 当前有效数据长度 */
    rt_uint32_t magic;            /* 魔数：init 时校验对象是否被正确初始化 */
};

/* ------------------------- 6 个 ops 实现 ------------------------- */

/* init：第一次 open 时被框架自动调用（懒初始化） */
static rt_err_t drv_test_init(rt_device_t dev)
{
    struct test_device *td = (struct test_device *)dev;

    if (td->magic != TEST_DEV_MAGIC)
    {
        rt_kprintf("[test_dev] init: magic check FAILED\n");
        return -RT_ERROR;
    }
    rt_kprintf("[test_dev] init ok\n");
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
    rt_size_t copy;

    if (td->data_len == 0)
    {
        rt_kprintf("[test_dev] read: buffer empty\n");
        return 0;
    }
    copy = (size > td->data_len) ? td->data_len : size;
    rt_memcpy(buffer, td->buf, copy);
    rt_kprintf("[test_dev] read: pos=%d size=%d copied=%d\n", pos, size, copy);
    return copy;
}

/* write：把用户数据存入内部缓冲区，返回实际存入字节数 */
static rt_size_t drv_test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_device *td = (struct test_device *)dev;
    rt_size_t copy;

    copy = (size > sizeof(td->buf)) ? sizeof(td->buf) : size;
    rt_memcpy(td->buf, buffer, copy);
    td->data_len = copy;
    rt_kprintf("[test_dev] write: pos=%d size=%d stored=%d\n", pos, size, copy);
    return copy;
}

/* control：命令分发，自定义命令从 0x20 起 */
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
    case TEST_DEV_CTRL_ECHO:
        rt_kprintf("[test_dev] control: echo, data_len=%d\n", td->data_len);
        break;
    case TEST_DEV_CTRL_CLEAR_BUFFER:
        td->data_len = 0;
        rt_memset(td->buf, 0, sizeof(td->buf));
        rt_kprintf("[test_dev] control: buffer cleared\n");
        break;
    default:
        ret = -RT_EINVAL;
        rt_kprintf("[test_dev] control: unknown cmd=%d\n", cmd);
        break;
    }
    return ret;
}

/* ops 绑定方式取决于内核配置：RT_USING_DEVICE_OPS 开启时收进 ops 结构体 */
#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops test_dev_ops =
{
    drv_test_init,
    drv_test_open,
    drv_test_close,
    drv_test_read,
    drv_test_write,
    drv_test_control
};
#endif /* RT_USING_DEVICE_OPS */

/* ------------------------- 驱动注册（自动初始化） ------------------------- */

static int rt_drv_test_init(void)
{
    rt_device_t dev;
    struct test_device *td;
    rt_err_t ret;

    /* 防重复注册：已存在同名设备则跳过 */
    if (rt_device_find(TEST_DEV_NAME) != RT_NULL)
    {
        rt_kprintf("[test_dev] already registered, skip\n");
        return RT_EOK;
    }

    /* 1) 动态创建设备对象：类型 Char，size 包含基类 + 私有结构 */
    dev = rt_device_create(RT_Device_Class_Char, sizeof(struct test_device));
    if (dev == RT_NULL)
    {
        rt_kprintf("[test_dev] create failed (out of memory)\n");
        return -RT_ENOMEM;
    }

    /* 2) 初始化私有字段（rt_device_create 只清零基类，私有部分要自己初始化！） */
    td = (struct test_device *)dev;
    td->magic    = TEST_DEV_MAGIC;
    td->data_len = 0;
    rt_memset(td->buf, 0, sizeof(td->buf));

    /* 3) 绑定 ops：把驱动实现挂到设备对象上（核心一步） */
#ifdef RT_USING_DEVICE_OPS
    dev->ops = &test_dev_ops;
#else
    dev->init    = drv_test_init;
    dev->open    = drv_test_open;
    dev->close   = drv_test_close;
    dev->read    = drv_test_read;
    dev->write   = drv_test_write;
    dev->control = drv_test_control;
#endif /* RT_USING_DEVICE_OPS */

    /* 4) 注册进内核设备链表，此后应用层可 rt_device_find("test_dev") */
    ret = rt_device_register(dev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR);
    if (ret != RT_EOK)
    {
        rt_kprintf("[test_dev] register failed, ret=%d\n", ret);
        return ret;
    }

    rt_kprintf("[test_dev] driver registered: %s\n", TEST_DEV_NAME);
    return RT_EOK;
}
/* 设备层初始化阶段自动执行（INIT_DEVICE_EXPORT = .rti_fn.3 段） */
INIT_DEVICE_EXPORT(rt_drv_test_init);
