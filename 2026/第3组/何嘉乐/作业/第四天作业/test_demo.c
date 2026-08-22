#include <rtthread.h>
#include <rtdevice.h>

#include "test_dev.h"


#define TEST_REG_DEVICE_ID    0x00
#define TEST_REG_CONFIG       0x02


static void test_demo(void)
{
    rt_device_t device;
    struct rt_test_device *test_dev;

    rt_uint8_t value;
    rt_uint8_t write_buf[4] = {0x11, 0x22, 0x33, 0x44};
    rt_uint8_t read_buf[4]  = {0};

    rt_err_t result;
    rt_size_t i;

    /*
     * 查找 test_dev
     */
    device = rt_device_find("test_dev");

    if (device == RT_NULL)
    {
        rt_kprintf("[test_demo] test_dev not found\n");
        return;
    }

    rt_kprintf("[test_demo] test_dev found\n");

    /*
     * 转换为 test_dev 设备对象
     */
    test_dev = (struct rt_test_device *)device;


    /*
     * 1. 读取 DEVICE_ID
     */
    result = test_dev_read_reg(test_dev,
                               TEST_REG_DEVICE_ID,
                               &value);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_demo] read device id failed: %d\n",
                   result);
        return;
    }

    rt_kprintf("[test_demo] device id = 0x%02X\n",
               value);


    /*
     * 2. 单寄存器写入
     */
    result = test_dev_write_reg(test_dev,
                                TEST_REG_CONFIG,
                                0x55);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_demo] write config failed: %d\n",
                   result);
        return;
    }


    /*
     * 3. 单寄存器读回
     */
    result = test_dev_read_reg(test_dev,
                               TEST_REG_CONFIG,
                               &value);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_demo] read config failed: %d\n",
                   result);
        return;
    }

    rt_kprintf("[test_demo] config = 0x%02X\n",
               value);


    /*
     * 4. 连续写多个寄存器
     */
    result = test_dev_write_regs(test_dev,
                                 0x10,
                                 write_buf,
                                 4);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_demo] write regs failed: %d\n",
                   result);
        return;
    }


    /*
     * 5. 连续读多个寄存器
     */
    result = test_dev_read_regs(test_dev,
                                0x10,
                                read_buf,
                                4);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_demo] read regs failed: %d\n",
                   result);
        return;
    }

    rt_kprintf("[test_demo] read data:");

    for (i = 0; i < 4; i++)
    {
        rt_kprintf(" 0x%02X", read_buf[i]);
    }

    rt_kprintf("\n");


    /*
     * 6. 测试只读寄存器保护
     */
    rt_kprintf("[test_demo] test read-only register\n");

    result = test_dev_write_reg(test_dev,
                                TEST_REG_DEVICE_ID,
                                0x66);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_demo] read-only protection works\n");
    }
    else
    {
        rt_kprintf("[test_demo] read-only protection failed\n");
    }


    /*
     * 再次读取 DEVICE_ID，
     * 确认只读寄存器没有被修改
     */
    result = test_dev_read_reg(test_dev,
                               TEST_REG_DEVICE_ID,
                               &value);

    if (result == RT_EOK)
    {
        rt_kprintf("[test_demo] device id after write = 0x%02X\n",
                   value);
    }


    /*
     * 7. 测试寄存器边界保护
     *
     * 从 0xFE 开始写 4 Byte：
     * 0xFE
     * 0xFF
     * 0x100
     * 0x101
     *
     * 后两个地址已经越界
     */
    rt_kprintf("[test_demo] test boundary protection\n");

    result = test_dev_write_regs(test_dev,
                                 0xFE,
                                 write_buf,
                                 4);

    if (result != RT_EOK)
    {
        rt_kprintf("[test_demo] boundary protection works\n");
    }
    else
    {
        rt_kprintf("[test_demo] boundary protection failed\n");
    }
}


MSH_CMD_EXPORT(test_demo, test virtual i2c device);
