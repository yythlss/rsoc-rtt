#include "test_dev.h"


/*
 * 虚拟寄存器定义
 */
#define VIRTUAL_REG_DEVICE_ID    0x00
#define VIRTUAL_REG_STATUS       0x01
#define VIRTUAL_REG_CONFIG       0x02
#define VIRTUAL_REG_DATA         0x03


/*
 * 虚拟 I2C 设备私有数据
 */
struct virtual_i2c_data
{
    rt_uint8_t addr;
    rt_uint8_t regs[TEST_DEV_REG_SIZE];
};


/*
 * test_dev 设备对象
 */
static struct rt_test_device virtual_i2c_dev;


/*
 * 虚拟 I2C 私有数据
 *
 * 模拟一个地址为 0x50 的 I2C 从设备
 */
static struct virtual_i2c_data virtual_i2c =
{
    .addr = 0x50,
};


/*
 * 虚拟 I2C 设备初始化
 */
static rt_err_t virtual_i2c_init(struct rt_device *dev)
{
    struct virtual_i2c_data *data;

    RT_ASSERT(dev != RT_NULL);

    data = (struct virtual_i2c_data *)dev->user_data;

    if (data == RT_NULL)
    {
        return -RT_ERROR;
    }

    /*
     * 清空虚拟寄存器
     */
    rt_memset(data->regs,
              0,
              sizeof(data->regs));

    /*
     * 设置默认寄存器值
     */
    data->regs[VIRTUAL_REG_DEVICE_ID] = 0xA5;
    data->regs[VIRTUAL_REG_STATUS]    = 0x01;
    data->regs[VIRTUAL_REG_CONFIG]    = 0x00;
    data->regs[VIRTUAL_REG_DATA]      = 0x00;

    rt_kprintf("[test_dev] virtual i2c init\n");
    rt_kprintf("[test_dev] addr = 0x%02X\n",
               data->addr);
    rt_kprintf("[test_dev] device id = 0x%02X\n",
               data->regs[VIRTUAL_REG_DEVICE_ID]);

    return RT_EOK;
}


/*
 * 写单个虚拟寄存器
 */
static rt_err_t virtual_i2c_write_reg(struct rt_device *dev,
                                      rt_uint8_t reg,
                                      rt_uint8_t value)
{
    struct virtual_i2c_data *data;

    RT_ASSERT(dev != RT_NULL);

    data = (struct virtual_i2c_data *)dev->user_data;

    if (data == RT_NULL)
    {
        return -RT_ERROR;
    }

    /*
     * DEVICE_ID 为只读寄存器
     */
    if (reg == VIRTUAL_REG_DEVICE_ID)
    {
        rt_kprintf("[test_dev] reg 0x%02X is read only\n",
                   reg);

        return -RT_ERROR;
    }

    /*
     * 写入虚拟寄存器
     */
    data->regs[reg] = value;

    rt_kprintf("[test_dev] write reg[0x%02X] = 0x%02X\n",
               reg,
               value);

    return RT_EOK;
}


/*
 * 读单个虚拟寄存器
 */
static rt_err_t virtual_i2c_read_reg(struct rt_device *dev,
                                     rt_uint8_t reg,
                                     rt_uint8_t *value)
{
    struct virtual_i2c_data *data;

    RT_ASSERT(dev != RT_NULL);

    data = (struct virtual_i2c_data *)dev->user_data;

    if (data == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (value == RT_NULL)
    {
        return -RT_EINVAL;
    }

    /*
     * 读取虚拟寄存器
     */
    *value = data->regs[reg];

    rt_kprintf("[test_dev] read reg[0x%02X] = 0x%02X\n",
               reg,
               *value);

    return RT_EOK;
}


/*
 * 连续写多个虚拟寄存器
 */
static rt_err_t virtual_i2c_write_regs(struct rt_device *dev,
                                       rt_uint8_t reg,
                                       const rt_uint8_t *buffer,
                                       rt_size_t size)
{
    struct virtual_i2c_data *data;
    rt_size_t i;

    RT_ASSERT(dev != RT_NULL);

    data = (struct virtual_i2c_data *)dev->user_data;

    if (data == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (buffer == RT_NULL || size == 0)
    {
        return -RT_EINVAL;
    }

    /*
     * 防止访问超过虚拟寄存器空间
     */
    if (size > (TEST_DEV_REG_SIZE - reg))
    {
        return -RT_EINVAL;
    }

    /*
     * 连续写入寄存器
     */
    for (i = 0; i < size; i++)
    {
        /*
         * DEVICE_ID 为只读寄存器
         */
        if ((reg + i) == VIRTUAL_REG_DEVICE_ID)
        {
            rt_kprintf("[test_dev] reg 0x%02X is read only\n",
                       (rt_uint8_t)(reg + i));

            return -RT_ERROR;
        }

        data->regs[reg + i] = buffer[i];
    }

    rt_kprintf("[test_dev] write %d regs from 0x%02X\n",
               (int)size,
               reg);

    return RT_EOK;
}


/*
 * 连续读多个虚拟寄存器
 */
static rt_err_t virtual_i2c_read_regs(struct rt_device *dev,
                                      rt_uint8_t reg,
                                      rt_uint8_t *buffer,
                                      rt_size_t size)
{
    struct virtual_i2c_data *data;
    rt_size_t i;

    RT_ASSERT(dev != RT_NULL);

    data = (struct virtual_i2c_data *)dev->user_data;

    if (data == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (buffer == RT_NULL || size == 0)
    {
        return -RT_EINVAL;
    }

    /*
     * 防止访问超过虚拟寄存器空间
     */
    if (size > (TEST_DEV_REG_SIZE - reg))
    {
        return -RT_EINVAL;
    }

    /*
     * 连续读取寄存器
     */
    for (i = 0; i < size; i++)
    {
        buffer[i] = data->regs[reg + i];
    }

    rt_kprintf("[test_dev] read %d regs from 0x%02X\n",
               (int)size,
               reg);

    return RT_EOK;
}


/*
 * 虚拟 I2C 操作接口表
 *
 * 将 test_dev 框架接口
 * 与具体虚拟 I2C 实现连接起来
 */
static const struct rt_test_ops virtual_i2c_ops =
{
    .init       = virtual_i2c_init,
    .write_reg  = virtual_i2c_write_reg,
    .read_reg   = virtual_i2c_read_reg,
    .write_regs = virtual_i2c_write_regs,
    .read_regs  = virtual_i2c_read_regs,
};


/*
 * 虚拟 I2C 设备注册
 */
static int virtual_i2c_hw_init(void)
{
    rt_err_t result;

    /*
     * 注册 test_dev
     */
    result = test_dev_register(&virtual_i2c_dev,
                               "test_dev",
                               &virtual_i2c_ops,
                               &virtual_i2c);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_dev] register failed: %d\n",
                   result);

        return result;
    }

    /*
     * 初始化虚拟 I2C 设备
     */
    result = test_dev_init(&virtual_i2c_dev);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_dev] init failed: %d\n",
                   result);

        return result;
    }

    rt_kprintf("[test_dev] register success\n");

    return RT_EOK;
}


/*
 * RT-Thread 自动初始化
 */
INIT_DEVICE_EXPORT(virtual_i2c_hw_init);
