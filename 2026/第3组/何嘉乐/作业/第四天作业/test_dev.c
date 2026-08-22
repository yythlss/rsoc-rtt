#include "test_dev.h"


/*
 * 注册 test_dev
 */
rt_err_t test_dev_register(struct rt_test_device *dev,
                           const char *name,
                           const struct rt_test_ops *ops,
                           void *user_data)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(name != RT_NULL);
    RT_ASSERT(ops != RT_NULL);

    /*
     * 设置为通用杂项设备
     */
    dev->parent.type = RT_Device_Class_Miscellaneous;

    /*
     * 当前不使用接收/发送完成回调
     */
    dev->parent.rx_indicate = RT_NULL;
    dev->parent.tx_complete = RT_NULL;

#ifdef RT_USING_DEVICE_OPS
    /*
     * test_dev 使用自己的 rt_test_ops，
     * 不使用 rt_device 通用 ops
     */
    dev->parent.ops = RT_NULL;
#else
    dev->parent.init    = RT_NULL;
    dev->parent.open    = RT_NULL;
    dev->parent.close   = RT_NULL;
    dev->parent.read    = RT_NULL;
    dev->parent.write   = RT_NULL;
    dev->parent.control = RT_NULL;
#endif

    /*
     * 保存底层操作接口
     */
    dev->ops = ops;

    /*
     * 保存设备私有数据
     */
    dev->parent.user_data = user_data;

    /*
     * 注册到 RT-Thread 设备管理器
     */
    return rt_device_register(&dev->parent,
                              name,
                              RT_DEVICE_FLAG_RDWR);
}


/*
 * 初始化设备
 */
rt_err_t test_dev_init(struct rt_test_device *dev)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(dev->ops != RT_NULL);

    if (dev->ops->init == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    return dev->ops->init(&dev->parent);
}


/*
 * 写单个寄存器
 */
rt_err_t test_dev_write_reg(struct rt_test_device *dev,
                            rt_uint8_t reg,
                            rt_uint8_t value)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(dev->ops != RT_NULL);

    if (dev->ops->write_reg == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    return dev->ops->write_reg(&dev->parent,
                               reg,
                               value);
}


/*
 * 读单个寄存器
 */
rt_err_t test_dev_read_reg(struct rt_test_device *dev,
                           rt_uint8_t reg,
                           rt_uint8_t *value)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(dev->ops != RT_NULL);

    if (value == RT_NULL)
    {
        return -RT_EINVAL;
    }

    if (dev->ops->read_reg == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    return dev->ops->read_reg(&dev->parent,
                              reg,
                              value);
}


/*
 * 连续写多个寄存器
 */
rt_err_t test_dev_write_regs(struct rt_test_device *dev,
                             rt_uint8_t reg,
                             const rt_uint8_t *buffer,
                             rt_size_t size)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(dev->ops != RT_NULL);

    if (buffer == RT_NULL || size == 0)
    {
        return -RT_EINVAL;
    }

    /*
     * 防止访问超过 256 Byte 寄存器空间
     */
    if (size > (TEST_DEV_REG_SIZE - reg))
    {
        return -RT_EINVAL;
    }

    if (dev->ops->write_regs == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    return dev->ops->write_regs(&dev->parent,
                                reg,
                                buffer,
                                size);
}


/*
 * 连续读多个寄存器
 */
rt_err_t test_dev_read_regs(struct rt_test_device *dev,
                            rt_uint8_t reg,
                            rt_uint8_t *buffer,
                            rt_size_t size)
{
    RT_ASSERT(dev != RT_NULL);
    RT_ASSERT(dev->ops != RT_NULL);

    if (buffer == RT_NULL || size == 0)
    {
        return -RT_EINVAL;
    }

    /*
     * 防止访问超过 256 Byte 寄存器空间
     */
    if (size > (TEST_DEV_REG_SIZE - reg))
    {
        return -RT_EINVAL;
    }

    if (dev->ops->read_regs == RT_NULL)
    {
        return -RT_ENOSYS;
    }

    return dev->ops->read_regs(&dev->parent,
                               reg,
                               buffer,
                               size);
}
