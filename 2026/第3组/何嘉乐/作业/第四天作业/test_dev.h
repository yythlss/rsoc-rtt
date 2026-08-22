#ifndef __TEST_DEV_H__
#define __TEST_DEV_H__

#include <rtthread.h>
#include <rtdevice.h>


/*
 * test_dev 虚拟寄存器空间大小
 */
#define TEST_DEV_REG_SIZE    256


/*
 * test_dev 操作接口
 *
 * 框架层只定义接口，
 * 具体实现由底层虚拟 I2C 驱动完成。
 */
struct rt_test_ops
{
    rt_err_t (*init)(struct rt_device *dev);

    rt_err_t (*write_reg)(struct rt_device *dev,
                          rt_uint8_t reg,
                          rt_uint8_t value);

    rt_err_t (*read_reg)(struct rt_device *dev,
                         rt_uint8_t reg,
                         rt_uint8_t *value);

    rt_err_t (*write_regs)(struct rt_device *dev,
                           rt_uint8_t reg,
                           const rt_uint8_t *buffer,
                           rt_size_t size);

    rt_err_t (*read_regs)(struct rt_device *dev,
                          rt_uint8_t reg,
                          rt_uint8_t *buffer,
                          rt_size_t size);
};


/*
 * test_dev 设备对象
 */
struct rt_test_device
{
    /*
     * 继承 RT-Thread 通用设备
     */
    struct rt_device parent;

    /*
     * test_dev 专用操作接口
     */
    const struct rt_test_ops *ops;
};


/*
 * 注册 test_dev
 */
rt_err_t test_dev_register(struct rt_test_device *dev,
                           const char *name,
                           const struct rt_test_ops *ops,
                           void *user_data);


/*
 * 初始化设备
 */
rt_err_t test_dev_init(struct rt_test_device *dev);


/*
 * 单寄存器写
 */
rt_err_t test_dev_write_reg(struct rt_test_device *dev,
                            rt_uint8_t reg,
                            rt_uint8_t value);


/*
 * 单寄存器读
 */
rt_err_t test_dev_read_reg(struct rt_test_device *dev,
                           rt_uint8_t reg,
                           rt_uint8_t *value);


/*
 * 连续写多个寄存器
 */
rt_err_t test_dev_write_regs(struct rt_test_device *dev,
                             rt_uint8_t reg,
                             const rt_uint8_t *buffer,
                             rt_size_t size);


/*
 * 连续读多个寄存器
 */
rt_err_t test_dev_read_regs(struct rt_test_device *dev,
                            rt_uint8_t reg,
                            rt_uint8_t *buffer,
                            rt_size_t size);


#endif /* __TEST_DEV_H__ */
