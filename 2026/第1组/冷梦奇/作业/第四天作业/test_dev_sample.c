/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     Leng Mengqi  Application sample & verification for test_dev framework
 */

#include "test_dev.h"
#include <string.h>

#define DBG_TAG "test_dev.app"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ==================== 1. 标准 I/O 设备接口测试 ==================== */
static rt_err_t test_standard_io(void)
{
    rt_device_t dev = rt_device_find("test_dev");
    if (!dev) return -RT_ERROR;

    if (rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK) return -RT_ERROR;

    /* 设置回环模式 */
    rt_uint8_t mode = RT_TEST_MODE_LOOPBACK;
    rt_device_control(dev, RT_TEST_CTRL_SET_MODE, &mode);

    /* 写入并回读验证 */
    char tx_buf[] = "Hello RT-Thread!";
    char rx_buf[32] = {0};
    rt_size_t len = rt_strlen(tx_buf);

    rt_device_write(dev, 0, tx_buf, len);
    rt_device_read(dev, 0, rx_buf, len);

    rt_device_close(dev);

    return (rt_strcmp(tx_buf, rx_buf) == 0) ? RT_EOK : -RT_ERROR;
}

/* ==================== 2. 专用高阶框架接口测试 ==================== */
static rt_err_t test_framework_api(void)
{
    rt_test_device_t tdev = rt_test_dev_find("test_dev");
    if (!tdev) return -RT_ERROR;

    rt_test_dev_open(tdev, RT_DEVICE_FLAG_RDWR);

    /* 信号发生器模式测试 */
    rt_test_dev_set_mode(tdev, RT_TEST_MODE_PATTERN);
    char buf[16] = {0};
    rt_test_dev_read(tdev, 0, buf, 5);

    rt_test_dev_reset(tdev);
    rt_test_dev_close(tdev);

    return (rt_strncmp(buf, "ABCDE", 5) == 0) ? RT_EOK : -RT_ERROR;
}

/* ==================== 3. 异步中断回调通知测试 ==================== */
static rt_sem_t s_rx_sem = RT_NULL;

static rt_err_t _test_rx_ind(rt_device_t dev, rt_size_t size)
{
    if (s_rx_sem) rt_sem_release(s_rx_sem);
    return RT_EOK;
}

static rt_err_t test_async_callback(void)
{
    rt_device_t dev = rt_device_find("test_dev");
    if (!dev) return -RT_ERROR;

    if (!s_rx_sem) s_rx_sem = rt_sem_create("tdev_rx", 0, RT_IPC_FLAG_FIFO);

    rt_device_open(dev, RT_DEVICE_FLAG_RDWR);
    rt_device_set_rx_indicate(dev, _test_rx_ind);

    /* 发送数据触发回调 */
    rt_device_write(dev, 0, "PING", 4);

    /* 等待回调释放信号量 */
    rt_err_t res = rt_sem_take(s_rx_sem, rt_tick_from_millisecond(500));

    rt_device_close(dev);
    return res;
}

/**
 * @brief 执行简明测试套件
 */
int test_dev_sample_run(void)
{
    rt_kprintf("\n--- test_dev Framework Verification ---\n");

    rt_kprintf("[1/3] Standard I/O loopback test : %s\n",
               test_standard_io() == RT_EOK ? "PASS" : "FAIL");

    rt_kprintf("[2/3] Framework dedicated API test: %s\n",
               test_framework_api() == RT_EOK ? "PASS" : "FAIL");

    rt_kprintf("[3/3] Async rx_indicate callback : %s\n",
               test_async_callback() == RT_EOK ? "PASS" : "FAIL");

    rt_kprintf("---------------------------------------\n\n");
    return RT_EOK;
}

#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT_ALIAS(test_dev_sample_run, test_dev_sample, Run test_dev verification suite);
#endif
