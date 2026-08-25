/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     Leng Mengqi  Implementation of virtual test_dev framework
 */

#include "test_dev.h"
#include <string.h>
#include <stdlib.h>

#define DBG_TAG "test_dev"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* I/O 设备管理层与底层 Ops 的适配层函数 */
static rt_err_t _test_device_init(rt_device_t dev)
{
    rt_test_device_t tdev = (rt_test_device_t)dev;
    RT_ASSERT(tdev != RT_NULL);

    if (tdev->ops && tdev->ops->init)
    {
        return tdev->ops->init(tdev);
    }
    return RT_EOK;
}

static rt_err_t _test_device_open(rt_device_t dev, rt_uint16_t oflag)
{
    rt_test_device_t tdev = (rt_test_device_t)dev;
    RT_ASSERT(tdev != RT_NULL);

    if (tdev->ops && tdev->ops->open)
    {
        return tdev->ops->open(tdev, oflag);
    }
    return RT_EOK;
}

static rt_err_t _test_device_close(rt_device_t dev)
{
    rt_test_device_t tdev = (rt_test_device_t)dev;
    RT_ASSERT(tdev != RT_NULL);

    if (tdev->ops && tdev->ops->close)
    {
        return tdev->ops->close(tdev);
    }
    return RT_EOK;
}

static rt_size_t _test_device_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    rt_test_device_t tdev = (rt_test_device_t)dev;
    RT_ASSERT(tdev != RT_NULL);

    if (tdev->ops && tdev->ops->read)
    {
        return tdev->ops->read(tdev, pos, buffer, size);
    }
    return 0;
}

static rt_size_t _test_device_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    rt_test_device_t tdev = (rt_test_device_t)dev;
    RT_ASSERT(tdev != RT_NULL);

    if (tdev->ops && tdev->ops->write)
    {
        return tdev->ops->write(tdev, pos, buffer, size);
    }
    return 0;
}

static rt_err_t _test_device_control(rt_device_t dev, int cmd, void *args)
{
    rt_test_device_t tdev = (rt_test_device_t)dev;
    RT_ASSERT(tdev != RT_NULL);

    if (tdev->ops && tdev->ops->control)
    {
        return tdev->ops->control(tdev, cmd, args);
    }
    return -RT_ENOSYS;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops _test_dev_ops =
{
    _test_device_init,
    _test_device_open,
    _test_device_close,
    _test_device_read,
    _test_device_write,
    _test_device_control
};
#endif

/**
 * @brief 注册一个 test_dev 设备到 RT-Thread I/O 设备管理器中
 *
 * @param dev       指向 rt_test_device 结构体的指针
 * @param name      设备注册名称（如 "vtest0"）
 * @param flags     设备标志（如 RT_DEVICE_FLAG_RDWR）
 * @param user_data 私有驱动数据指针
 * @return rt_err_t RT_EOK 成功，其他为错误码
 */
rt_err_t rt_device_test_register(struct rt_test_device *dev,
                                 const char *name,
                                 rt_uint32_t flags,
                                 void *user_data)
{
    rt_device_t device;

    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(name != RT_NULL);
    RT_ASSERT(dev->ops != RT_NULL);

    device = &(dev->parent);

    device->type        = RT_Device_Class_Char;
    device->rx_indicate = RT_NULL;
    device->tx_complete = RT_NULL;

#ifdef RT_USING_DEVICE_OPS
    device->ops         = &_test_dev_ops;
#else
    device->init        = _test_device_init;
    device->open        = _test_device_open;
    device->close       = _test_device_close;
    device->read        = _test_device_read;
    device->write       = _test_device_write;
    device->control     = _test_device_control;
#endif

    device->user_data   = user_data;
    dev->user_data      = user_data;

    /* 将设备挂载进 RT-Thread 全局设备对象容器 */
    return rt_device_register(device, name, (rt_uint16_t)flags);
}

/* ==================== Framework High-Level APIs ==================== */

rt_test_device_t rt_test_dev_find(const char *name)
{
    rt_device_t dev = rt_device_find(name);
    return (rt_test_device_t)dev;
}

rt_err_t rt_test_dev_open(rt_test_device_t dev, rt_uint16_t oflag)
{
    RT_ASSERT(dev != RT_NULL);
    return rt_device_open(&(dev->parent), oflag);
}

rt_err_t rt_test_dev_close(rt_test_device_t dev)
{
    RT_ASSERT(dev != RT_NULL);
    return rt_device_close(&(dev->parent));
}

rt_size_t rt_test_dev_read(rt_test_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    RT_ASSERT(dev != RT_NULL);
    return rt_device_read(&(dev->parent), pos, buffer, size);
}

rt_size_t rt_test_dev_write(rt_test_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    RT_ASSERT(dev != RT_NULL);
    return rt_device_write(&(dev->parent), pos, buffer, size);
}

rt_err_t rt_test_dev_control(rt_test_device_t dev, int cmd, void *args)
{
    RT_ASSERT(dev != RT_NULL);
    return rt_device_control(&(dev->parent), cmd, args);
}

rt_err_t rt_test_dev_set_mode(rt_test_device_t dev, rt_uint8_t mode)
{
    RT_ASSERT(dev != RT_NULL);
    return rt_device_control(&(dev->parent), RT_TEST_CTRL_SET_MODE, &mode);
}

rt_err_t rt_test_dev_reset(rt_test_device_t dev)
{
    RT_ASSERT(dev != RT_NULL);
    return rt_device_control(&(dev->parent), RT_TEST_CTRL_RESET, RT_NULL);
}

rt_err_t rt_test_dev_get_status(rt_test_device_t dev, struct rt_test_status *status)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(status != RT_NULL);
    return rt_device_control(&(dev->parent), RT_TEST_CTRL_GET_STATUS, status);
}

/* ==================== FinSH / MSH Interactive Command ==================== */
#ifdef RT_USING_FINSH
#include <finsh.h>

static rt_test_device_t g_current_dev = RT_NULL;

static void _test_dev_print_help(void)
{
    rt_kprintf("Usage: test_dev [option] [args...]\n");
    rt_kprintf("  test_dev probe <dev_name>  : Find and open test device (e.g. test_dev probe vtest0)\n");
    rt_kprintf("  test_dev read [len]        : Read bytes from current device\n");
    rt_kprintf("  test_dev write <string>    : Write string data to current device\n");
    rt_kprintf("  test_dev mode <0|1|2|3>    : Set mode (0:Normal, 1:Loopback, 2:Echo, 3:Pattern)\n");
    rt_kprintf("  test_dev status            : Query device status and statistical registers\n");
    rt_kprintf("  test_dev reset             : Reset device buffer and counters\n");
    rt_kprintf("  test_dev selftest          : Trigger device self-test\n");
    rt_kprintf("  test_dev close             : Close current probed device\n");
    rt_kprintf("  test_dev help              : Show this help message\n");
}

static void test_dev_cmd(int argc, char **argv)
{
    if (argc < 2)
    {
        _test_dev_print_help();
        return;
    }

    if (rt_strcmp(argv[1], "probe") == 0)
    {
        const char *name = (argc >= 3) ? argv[2] : "vtest0";
        if (g_current_dev != RT_NULL)
        {
            rt_test_dev_close(g_current_dev);
            g_current_dev = RT_NULL;
        }
        g_current_dev = rt_test_dev_find(name);
        if (g_current_dev == RT_NULL)
        {
            rt_kprintf("[test_dev] Device '%s' not found!\n", name);
            return;
        }
        if (rt_test_dev_open(g_current_dev, RT_DEVICE_FLAG_RDWR) == RT_EOK)
        {
            rt_kprintf("[test_dev] Successfully probed and opened '%s'.\n", name);
        }
        else
        {
            rt_kprintf("[test_dev] Failed to open device '%s'.\n", name);
            g_current_dev = RT_NULL;
        }
    }
    else if (rt_strcmp(argv[1], "read") == 0)
    {
        rt_size_t len = 16;
        char buf[64];

        if (g_current_dev == RT_NULL)
        {
            rt_kprintf("[test_dev] No device probed. Please run 'test_dev probe <name>' first!\n");
            return;
        }
        if (argc >= 3)
        {
            len = (rt_size_t)atoi(argv[2]);
            if (len > sizeof(buf) - 1) len = sizeof(buf) - 1;
        }
        rt_memset(buf, 0, sizeof(buf));
        rt_size_t read_bytes = rt_test_dev_read(g_current_dev, 0, buf, len);
        rt_kprintf("[test_dev] Read %d bytes: '%s'\n", (int)read_bytes, buf);
    }
    else if (rt_strcmp(argv[1], "write") == 0)
    {
        if (g_current_dev == RT_NULL)
        {
            rt_kprintf("[test_dev] No device probed. Please run 'test_dev probe <name>' first!\n");
            return;
        }
        if (argc < 3)
        {
            rt_kprintf("[test_dev] Missing data string. Usage: test_dev write <str>\n");
            return;
        }
        rt_size_t len = rt_strlen(argv[2]);
        rt_size_t written = rt_test_dev_write(g_current_dev, 0, argv[2], len);
        rt_kprintf("[test_dev] Wrote %d bytes: \"%s\"\n", (int)written, argv[2]);
    }
    else if (rt_strcmp(argv[1], "mode") == 0)
    {
        if (g_current_dev == RT_NULL)
        {
            rt_kprintf("[test_dev] No device probed. Please run 'test_dev probe <name>' first!\n");
            return;
        }
        if (argc < 3)
        {
            rt_kprintf("[test_dev] Usage: test_dev mode <0:Normal|1:Loopback|2:Echo|3:Pattern>\n");
            return;
        }
        rt_uint8_t mode = (rt_uint8_t)atoi(argv[2]);
        if (rt_test_dev_set_mode(g_current_dev, mode) == RT_EOK)
        {
            rt_kprintf("[test_dev] Operating mode updated to %d.\n", mode);
        }
        else
        {
            rt_kprintf("[test_dev] Failed to set mode!\n");
        }
    }
    else if (rt_strcmp(argv[1], "status") == 0)
    {
        struct rt_test_status status;
        if (g_current_dev == RT_NULL)
        {
            rt_kprintf("[test_dev] No device probed. Please run 'test_dev probe <name>' first!\n");
            return;
        }
        if (rt_test_dev_get_status(g_current_dev, &status) == RT_EOK)
        {
            rt_kprintf("================ Test Device Status ================\n");
            rt_kprintf("  Device Name : %s\n", g_current_dev->parent.parent.name);
            rt_kprintf("  Open Status : %s (Ref Count: %d)\n", status.is_open ? "OPEN" : "CLOSED", g_current_dev->parent.ref_count);
            rt_kprintf("  Work Mode   : %d (%s)\n", status.mode,
                       status.mode == RT_TEST_MODE_NORMAL ? "NORMAL" :
                       status.mode == RT_TEST_MODE_LOOPBACK ? "LOOPBACK" :
                       status.mode == RT_TEST_MODE_ECHO ? "ECHO" : "PATTERN");
            rt_kprintf("  Buffer Info : %d / %d Bytes used\n", status.data_len, status.buffer_size);
            rt_kprintf("  Total TX    : %u Bytes\n", (unsigned int)status.total_tx_bytes);
            rt_kprintf("  Total RX    : %u Bytes\n", (unsigned int)status.total_rx_bytes);
            rt_kprintf("  Error Count : %u\n", (unsigned int)status.error_count);
            rt_kprintf("====================================================\n");
        }
        else
        {
            rt_kprintf("[test_dev] Failed to get device status!\n");
        }
    }
    else if (rt_strcmp(argv[1], "reset") == 0)
    {
        if (g_current_dev == RT_NULL)
        {
            rt_kprintf("[test_dev] No device probed. Please run 'test_dev probe <name>' first!\n");
            return;
        }
        rt_test_dev_reset(g_current_dev);
        rt_kprintf("[test_dev] Device reset completed.\n");
    }
    else if (rt_strcmp(argv[1], "selftest") == 0)
    {
        if (g_current_dev == RT_NULL)
        {
            rt_kprintf("[test_dev] No device probed. Please run 'test_dev probe <name>' first!\n");
            return;
        }
        rt_err_t res = rt_test_dev_control(g_current_dev, RT_TEST_CTRL_SELF_TEST, RT_NULL);
        rt_kprintf("[test_dev] Self-test result: %s (%d)\n", res == RT_EOK ? "PASS" : "FAIL", res);
    }
    else if (rt_strcmp(argv[1], "close") == 0)
    {
        if (g_current_dev != RT_NULL)
        {
            rt_test_dev_close(g_current_dev);
            rt_kprintf("[test_dev] Probed device closed.\n");
            g_current_dev = RT_NULL;
        }
    }
    else
    {
        _test_dev_print_help();
    }
}
MSH_CMD_EXPORT_ALIAS(test_dev_cmd, test_dev, Control and test virtual test_dev device);
#endif /* RT_USING_FINSH */
