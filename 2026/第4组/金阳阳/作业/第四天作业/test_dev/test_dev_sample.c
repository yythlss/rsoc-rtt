/*
 * 虚拟设备驱动框架层 test_dev -- 应用层示例
 *
 * 应用层只使用内核标准设备接口访问 test_dev：
 *   rt_device_find -> rt_device_open -> rt_device_control/read/write
 *   -> rt_device_close
 *
 * msh 命令：
 *   test_dev_demo               跑一遍完整的 I/O 流程
 *   test_dev_read  <pos> <size> 读取并打印存储内容
 *   test_dev_write <string>     向设备写入字符串
 *   test_dev_info               打印设备信息
 *   test_dev_thread             启动一个周期读设备的后台线程
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "test_dev.h"

#define DBG_TAG "test_dev.app"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 打印一段内存的十六进制内容，方便观察设备存储 */
static void _hex_dump(const char *title, const rt_uint8_t *buf, rt_size_t len)
{
    rt_size_t i;

    LOG_I("%s (%u bytes):", title, len);
    for (i = 0; i < len; i++)
    {
        rt_kprintf("%02x ", buf[i]);
        if ((i + 1) % 16 == 0)
            rt_kprintf("\n");
    }
    if (len % 16)
        rt_kprintf("\n");
}

/* 完整 I/O 流程演示：open -> control -> write -> read -> control -> close */
static void test_dev_demo(void)
{
    rt_device_t dev;
    struct test_dev_info info;
    rt_uint8_t version = 0;
    rt_uint8_t wbuf[32], rbuf[32];
    int i, ret;

    /* 1. 查找设备：对象容器里按名字找，拿到的是驱动框架注册的设备句柄 */
    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        LOG_E("can not find device: %s", TEST_DEV_NAME);
        return;
    }
    LOG_I("[1/6] find device: %s", TEST_DEV_NAME);

    /* 2. 打开设备：框架 open 接口被调用，统计 open 次数 */
    ret = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    if (ret != RT_EOK)
    {
        LOG_E("open failed: %d", ret);
        return;
    }
    LOG_I("[2/6] open device ok");

    /* 3. control：读取设备信息与版本号 */
    rt_device_control(dev, TEST_DEV_CMD_GET_INFO, &info);
    rt_device_control(dev, TEST_DEV_CMD_GET_VERSION, &version);
    LOG_I("[3/6] control: version=%u mode=%u size=%u open=%u read=%u write=%u",
          info.version, info.mode, info.buf_size,
          info.open_count, info.read_count, info.write_count);

    /* 4. write：写入一段递增数据 */
    for (i = 0; i < 16; i++)
        wbuf[i] = 0xA0 + i;
    ret = rt_device_write(dev, 0, wbuf, 16);
    LOG_I("[4/6] write %d bytes @ pos 0", ret);

    /* 5. read：读回来并校验 */
    ret = rt_device_read(dev, 0, rbuf, 16);
    LOG_I("[5/6] read %d bytes @ pos 0", ret);
    if (ret == 16 && rt_memcmp(wbuf, rbuf, 16) == 0)
        LOG_I("      data check PASS");
    else
        LOG_E("      data check FAIL");
    _hex_dump("content", rbuf, 16);

    /* 演示控制命令：清空后读出的数据应全为 0 */
    rt_device_control(dev, TEST_DEV_CMD_CLEAR, RT_NULL);
    rt_device_read(dev, 0, rbuf, 8);
    _hex_dump("after CLEAR", rbuf, 8);

    /* 演示静默模式：write 被丢弃 */
    rt_device_control(dev, TEST_DEV_CMD_SET_MODE, (void *)TEST_DEV_MODE_SILENT);
    ret = rt_device_write(dev, 0, wbuf, 16);
    LOG_I("      silent mode write -> %d (dropped)", ret);
    rt_device_control(dev, TEST_DEV_CMD_SET_MODE, (void *)TEST_DEV_MODE_NORMAL);

    /* 6. 关闭设备：框架 close 接口被调用 */
    rt_device_close(dev);
    LOG_I("[6/6] close device ok");
}
MSH_CMD_EXPORT(test_dev_demo, test_dev full io flow demo);

/* 读指定位置的数据并打印 */
static void test_dev_read(rt_uint32_t pos, rt_uint32_t size)
{
    rt_device_t dev;
    rt_uint8_t buf[64];
    int ret;

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        LOG_E("can not find device: %s", TEST_DEV_NAME);
        return;
    }

    if (size > sizeof(buf))
        size = sizeof(buf);

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("open failed");
        return;
    }

    ret = rt_device_read(dev, pos, buf, size);
    if (ret > 0)
        _hex_dump("read", buf, ret);
    else
        LOG_W("read pos %u failed", pos);

    rt_device_close(dev);
}
MSH_CMD_EXPORT_ALIAS(test_dev_read, dev_read, test_dev_read pos size);

/* 向设备写入一个字符串 */
static void test_dev_write(const char *text)
{
    rt_device_t dev;
    int ret;

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        LOG_E("can not find device: %s", TEST_DEV_NAME);
        return;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("open failed");
        return;
    }

    ret = rt_device_write(dev, 0, text, rt_strlen(text));
    LOG_I("write %d bytes", ret);

    rt_device_close(dev);
}
MSH_CMD_EXPORT(test_dev_write, test_dev_write string);

/* 打印设备信息 */
static void test_dev_info(void)
{
    rt_device_t dev;
    struct test_dev_info info;

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        LOG_E("can not find device: %s", TEST_DEV_NAME);
        return;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("open failed");
        return;
    }

    rt_device_control(dev, TEST_DEV_CMD_GET_INFO, &info);
    rt_kprintf("name=%s version=%u mode=%u size=%u open=%u read=%u write=%u\n",
               TEST_DEV_NAME, info.version, info.mode, info.buf_size,
               info.open_count, info.read_count, info.write_count);

    rt_device_close(dev);
}
MSH_CMD_EXPORT(test_dev_info, show test_dev info);

/* 后台线程：周期读设备，模拟真实应用（如传感器轮询） */
static void test_dev_poll_thread(void *parameter)
{
    rt_device_t dev;
    rt_uint8_t buf[8];
    int count = 0;

    dev = (rt_device_t)parameter;

    while (count++ < 10)
    {
        rt_device_read(dev, 0, buf, sizeof(buf));
        rt_thread_mdelay(1000);
    }

    LOG_I("poll thread exit");
    rt_device_close(dev);
}

static void test_dev_thread(void)
{
    rt_device_t dev;
    rt_thread_t tid;

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        LOG_E("can not find device: %s", TEST_DEV_NAME);
        return;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("open failed");
        return;
    }

    tid = rt_thread_create("devpoll", test_dev_poll_thread, dev, 1024, 20, 10);
    if (tid == RT_NULL)
    {
        LOG_E("create thread failed");
        rt_device_close(dev);
        return;
    }
    rt_thread_startup(tid);

    LOG_I("poll thread started, read device every 1s (10 times)");
}
MSH_CMD_EXPORT(test_dev_thread, start a poll thread on test_dev);
