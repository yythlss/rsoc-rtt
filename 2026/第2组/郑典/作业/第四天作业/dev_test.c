/*
 * 自定义 I/O 设备示例：创建、注册一个字符设备，并实现其操作方法
 *
 * 说明：
 * 1. 本工程未开启 RT_USING_DEVICE_OPS，struct rt_device 直接包含
 *    init/open/close/read/write/control 六个函数指针。
 * 2. read/write 的返回类型是 rt_size_t（读写字节数），不是 rt_err_t。
 * 3. 使用 ulog 输出日志：先定义 LOG_TAG / LOG_LVL，再 #include <ulog.h>。
 */
#include <rtthread.h>
#include <rtdevice.h>

#define LOG_TAG "drv_test_app"
#define LOG_LVL LOG_LVL_DBG
#include <ulog.h>

/* ---------- 设备操作方法实现 ---------- */

static rt_err_t test_dev_init(rt_device_t dev)
{
    LOG_I("test_dev init.");
    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    LOG_I("test_dev open, oflag = %d", oflag);
    return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    LOG_I("test_dev close.");
    return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    LOG_I("test_dev read, pos = %ld, size = %u", pos, size);
    return size;    /* 模拟读取 size 个字节 */
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    LOG_I("test_dev write, pos = %ld, size = %u", pos, size);
    return size;    /* 模拟写入 size 个字节 */
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    LOG_I("test_dev control, cmd = %d", cmd);
    return RT_EOK;
}

/* ---------- 设备创建与注册 ---------- */

static int rt_dev_test_init(void)
{
    rt_device_t test_dev = rt_device_create(RT_Device_Class_Char, 0);
    if (test_dev == RT_NULL)
    {
        rt_kprintf("test_dev create failed\n");
        return -RT_ERROR;
    }

    /* 挂接操作方法 */
    test_dev->init    = test_dev_init;
    test_dev->open    = test_dev_open;
    test_dev->close   = test_dev_close;
    test_dev->read    = test_dev_read;
    test_dev->write   = test_dev_write;
    test_dev->control = test_dev_control;

    /* 注册设备，供应用层通过名称查找 */
    if (rt_device_register(test_dev, "test_dev", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("test_dev register failed\n");
        return -RT_ERROR;
    }

    LOG_I("test_dev register success.");
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_dev_test_init);
