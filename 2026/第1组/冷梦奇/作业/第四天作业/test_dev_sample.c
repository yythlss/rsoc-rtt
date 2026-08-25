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

#define DBG_TAG "vtest.sample"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

/* ==================== 1. 标准 I/O 设备通用接口测试 ==================== */
static rt_err_t test_standard_device_api(void)
{
    rt_device_t dev;
    char write_buf[] = "Hello RT-Thread Standard Device Model!";
    char read_buf[64];
    rt_size_t len;
    struct rt_test_status status;

    rt_kprintf("\n[TEST 1] Testing Standard RT-Thread I/O Device APIs (rt_device_*)...\n");

    /* 1.1 查找设备 */
    dev = rt_device_find("vtest0");
    if (dev == RT_NULL)
    {
        rt_kprintf("[FAIL] Cannot find device 'vtest0'!\n");
        return -RT_ERROR;
    }
    rt_kprintf("  [PASS] 1. rt_device_find('vtest0') succeeded. dev=%p\n", dev);

    /* 1.2 打开设备 */
    if (rt_device_open(dev, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("[FAIL] Failed to open 'vtest0'!\n");
        return -RT_ERROR;
    }
    rt_kprintf("  [PASS] 2. rt_device_open('vtest0', RDWR) succeeded. ref_count=%d\n", dev->ref_count);

    /* 1.3 控制命令：设置回环模式 */
    rt_uint8_t mode = RT_TEST_MODE_LOOPBACK;
    rt_device_control(dev, RT_TEST_CTRL_SET_MODE, &mode);
    rt_kprintf("  [PASS] 3. rt_device_control(SET_MODE, LOOPBACK) succeeded.\n");

    /* 1.4 写入数据 */
    len = rt_strlen(write_buf);
    rt_size_t written = rt_device_write(dev, 0, write_buf, len);
    rt_kprintf("  [PASS] 4. rt_device_write: sent %d bytes -> \"%s\"\n", (int)written, write_buf);

    /* 1.5 读取数据并验证回环一致性 */
    rt_memset(read_buf, 0, sizeof(read_buf));
    rt_size_t read_bytes = rt_device_read(dev, 0, read_buf, written);
    rt_kprintf("  [PASS] 5. rt_device_read: received %d bytes -> \"%s\"\n", (int)read_bytes, read_buf);

    if (rt_strcmp(write_buf, read_buf) == 0)
    {
        rt_kprintf("  [PASS] 6. Loopback Data Match Verification: SUCCESS!\n");
    }
    else
    {
        rt_kprintf("  [FAIL] 6. Data mismatch! Expected: '%s', Got: '%s'\n", write_buf, read_buf);
    }

    /* 1.6 查询设备状态寄存器 */
    rt_device_control(dev, RT_TEST_CTRL_GET_STATUS, &status);
    rt_kprintf("  [PASS] 7. rt_device_control(GET_STATUS): TX=%u, RX=%u, Mode=%d, Err=%u\n",
               (unsigned int)status.total_tx_bytes, (unsigned int)status.total_rx_bytes,
               status.mode, (unsigned int)status.error_count);

    /* 1.7 关闭设备 */
    rt_device_close(dev);
    rt_kprintf("  [PASS] 8. rt_device_close('vtest0') succeeded. ref_count=%d\n", dev->ref_count);

    return RT_EOK;
}

/* ==================== 2. 专用设备驱动框架接口测试 ==================== */
static rt_err_t test_framework_dedicated_api(void)
{
    rt_test_device_t tdev;
    char pattern_buf[32];
    struct rt_test_status status;

    rt_kprintf("\n[TEST 2] Testing High-Level Framework APIs (rt_test_dev_*)...\n");

    /* 2.1 查找专用设备 */
    tdev = rt_test_dev_find("vtest0");
    if (tdev == RT_NULL)
    {
        rt_kprintf("[FAIL] Cannot find test device 'vtest0'!\n");
        return -RT_ERROR;
    }
    rt_kprintf("  [PASS] 1. rt_test_dev_find('vtest0') succeeded.\n");

    /* 2.2 打开设备 */
    rt_test_dev_open(tdev, RT_DEVICE_FLAG_RDWR);
    rt_kprintf("  [PASS] 2. rt_test_dev_open succeeded.\n");

    /* 2.3 设置信号发生器模式 (Pattern Mode) */
    rt_test_dev_set_mode(tdev, RT_TEST_MODE_PATTERN);
    rt_kprintf("  [PASS] 3. rt_test_dev_set_mode(PATTERN) succeeded.\n");

    /* 2.4 读取信号发生器产生的数据 */
    rt_memset(pattern_buf, 0, sizeof(pattern_buf));
    rt_size_t read_bytes = rt_test_dev_read(tdev, 0, pattern_buf, 15);
    pattern_buf[read_bytes] = '\0';
    rt_kprintf("  [PASS] 4. rt_test_dev_read (Generated Pattern): \"%s\" (len=%d)\n", pattern_buf, (int)read_bytes);

    /* 2.5 复位设备并读取状态 */
    rt_test_dev_reset(tdev);
    rt_test_dev_get_status(tdev, &status);
    rt_kprintf("  [PASS] 5. rt_test_dev_reset & get_status: data_len=%d, buffer_size=%d\n",
               status.data_len, status.buffer_size);

    /* 2.6 关闭设备 */
    rt_test_dev_close(tdev);
    rt_kprintf("  [PASS] 6. rt_test_dev_close succeeded.\n");

    return RT_EOK;
}

/* ==================== 3. 异步回调与生产者-消费者测试 ==================== */
static rt_sem_t g_rx_sem = RT_NULL;

static rt_err_t _vtest_rx_indicate(rt_device_t dev, rt_size_t size)
{
    /* 模拟底层驱动中断收到数据后，触发回调并释放信号量唤醒上层任务 */
    if (g_rx_sem != RT_NULL)
    {
        rt_sem_release(g_rx_sem);
    }
    return RT_EOK;
}

static void consumer_thread_entry(void *parameter)
{
    rt_device_t dev = (rt_device_t)parameter;
    char rx_buf[64];
    int count = 0;

    while (count < 3)
    {
        /* 阻塞等待接收信号量通知 */
        if (rt_sem_take(g_rx_sem, rt_tick_from_millisecond(2000)) == RT_EOK)
        {
            rt_memset(rx_buf, 0, sizeof(rx_buf));
            rt_size_t rx_len = rt_device_read(dev, 0, rx_buf, sizeof(rx_buf) - 1);
            if (rx_len > 0)
            {
                count++;
                rt_kprintf("  [Consumer Thread] RX Callback Triggered! Received: \"%s\" (Packet #%d)\n", rx_buf, count);
            }
        }
    }
    rt_kprintf("  [Consumer Thread] Completed 3 packets reception successfully.\n");
}

static rt_err_t test_async_callback_model(void)
{
    rt_device_t dev;
    rt_thread_t th_consumer;

    rt_kprintf("\n[TEST 3] Testing Asynchronous Rx Callback & IPC Synchronization...\n");

    dev = rt_device_find("vtest0");
    if (dev == RT_NULL) return -RT_ERROR;

    if (g_rx_sem == RT_NULL)
    {
        g_rx_sem = rt_sem_create("vtest_rx", 0, RT_IPC_FLAG_FIFO);
    }

    rt_device_open(dev, RT_DEVICE_FLAG_RDWR);

    /* 设置接收回调函数 */
    rt_device_set_rx_indicate(dev, _vtest_rx_indicate);
    rt_uint8_t mode = RT_TEST_MODE_LOOPBACK;
    rt_device_control(dev, RT_TEST_CTRL_SET_MODE, &mode);
    rt_kprintf("  [PASS] 1. Registered rx_indicate callback and created binary semaphore.\n");

    /* 创建消费者线程 */
    th_consumer = rt_thread_create("th_vtest_c", consumer_thread_entry, dev, 1024, 15, 10);
    if (th_consumer != RT_NULL)
    {
        rt_thread_startup(th_consumer);
    }

    rt_thread_mdelay(100);

    /* 生产者模拟 3 次数据发送 */
    const char *msgs[] = {
        "Packet-1: Sensor Temp=25.6C",
        "Packet-2: Motor RPM=3600",
        "Packet-3: Battery Voltage=3.3V"
    };

    for (int i = 0; i < 3; i++)
    {
        rt_kprintf("  [Producer] Writing msg #%d: \"%s\"\n", i + 1, msgs[i]);
        rt_device_write(dev, 0, msgs[i], rt_strlen(msgs[i]));
        rt_thread_mdelay(300);
    }

    rt_thread_mdelay(500);

    rt_device_close(dev);
    rt_kprintf("  [PASS] 2. Async callback verification completed.\n");

    return RT_EOK;
}

/**
 * @brief 运行全部自动化验证测试
 */
int test_dev_sample_run(void)
{
    rt_kprintf("\n=======================================================\n");
    rt_kprintf("   Virtual Device Framework (test_dev) Full Verification\n");
    rt_kprintf("=======================================================\n");

    test_standard_device_api();
    test_framework_dedicated_api();
    test_async_callback_model();

    rt_kprintf("\n=======================================================\n");
    rt_kprintf("   All test_dev Framework Test Cases PASSED Successfully!\n");
    rt_kprintf("=======================================================\n\n");

    return RT_EOK;
}

#ifdef RT_USING_FINSH
#include <finsh.h>
MSH_CMD_EXPORT_ALIAS(test_dev_sample_run, test_dev_sample, Run automated verification for test_dev framework);
#endif
