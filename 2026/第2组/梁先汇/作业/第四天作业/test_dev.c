#include <rtthread.h>
#include <rtdevice.h>
#include "test_dev.h"
#include <ulog.h>

/* 框架层设备对象（对标 pin.c 的 _hw_pin） */
static struct rt_device_test _test_dev;

/* 分发函数：内核调到这里，再转手调驱动层的 ops */
static rt_size_t _test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct rt_device_test *test_dev = (struct rt_device_test *)dev;
    if (test_dev->ops == RT_NULL || test_dev->ops->read == RT_NULL) return 0;
    return test_dev->ops->read(dev, pos, buffer, size);
}

static rt_size_t _test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct rt_device_test *test_dev = (struct rt_device_test *)dev;
    if (test_dev->ops == RT_NULL || test_dev->ops->write == RT_NULL) return 0;
    return test_dev->ops->write(dev, pos, buffer, size);
}

static rt_err_t _test_control(rt_device_t dev, int cmd, void *args)
{
    struct rt_device_test *test_dev = (struct rt_device_test *)dev;
    if (test_dev->ops == RT_NULL || test_dev->ops->control == RT_NULL) return -RT_ENOSYS;
    return test_dev->ops->control(dev, cmd, args);
}

static rt_err_t _test_init(rt_device_t dev)
{
    struct rt_device_test *test_dev = (struct rt_device_test *)dev;
    if (test_dev->ops == RT_NULL || test_dev->ops->init == RT_NULL) return RT_EOK;
    return test_dev->ops->init(dev);
}

static rt_err_t _test_open(rt_device_t dev, rt_uint16_t oflag)
{
    struct rt_device_test *test_dev = (struct rt_device_test *)dev;
    if (test_dev->ops == RT_NULL || test_dev->ops->open == RT_NULL) return RT_EOK;
    return test_dev->ops->open(dev, oflag);
}

static rt_err_t _test_close(rt_device_t dev)
{
    struct rt_device_test *test_dev = (struct rt_device_test *)dev;
    if (test_dev->ops == RT_NULL || test_dev->ops->close == RT_NULL) return RT_EOK;
    return test_dev->ops->close(dev);
}

/* 注册 API：由驱动层调用 */
int rt_device_test_register(const char *name,
                            const struct rt_test_ops *ops,
                            void *user_data)
{
    _test_dev.parent.type = RT_Device_Class_Char;   /* 设备类型 */

    _test_dev.parent.init    = _test_init;          /* 内核接口 → 框架分发 */
    _test_dev.parent.open    = _test_open;
    _test_dev.parent.close   = _test_close;
    _test_dev.parent.read    = _test_read;
    _test_dev.parent.write   = _test_write;
    _test_dev.parent.control = _test_control;

    _test_dev.ops            = ops;                 /* 存驱动层方法表 */
    _test_dev.parent.user_data = user_data;

    return rt_device_register(&_test_dev.parent, name, RT_DEVICE_FLAG_RDWR);
}

