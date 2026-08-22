#include <rtthread.h>
#include <rtdevice.h>

void i2c_sample_single_byte_write(void)
{
    struct rt_i2c_bus_device *i2c_bus;
    struct rt_i2c_msg msgs;
    rt_uint8_t buf[2];

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c2");
    if(i2c_bus == RT_NULL)
    {
        rt_kprintf("can't find %s device!\n", "i2c2");
    }

    buf[0] = 0x6B;

    msgs.addr = 0x68;
    msgs.flags = RT_I2C_WR;
    msgs.buf = buf;
    msgs.len = 1;

    if(rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
        rt_kprintf("single byte write success!\n");
    else
        rt_kprintf("single byte write failed...\n");
}
MSH_CMD_EXPORT(i2c_sample_single_byte_write, i2c_sample_single_byte_write);

void i2c_sample_multi_byte_write(void)
{
    struct rt_i2c_bus_device *i2c_bus;
    struct rt_i2c_msg msgs;
    rt_uint8_t buf[3];

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c2");
    if(i2c_bus == RT_NULL)
    {
        rt_kprintf("can't find %s device!\n", "i2c2");
    }

    buf[0] = 0x01;
    buf[1] = 0x02;
    buf[2] = 0x03;

    msgs.addr = 0x68;
    msgs.flags = RT_I2C_WR;
    msgs.buf = buf;
    msgs.len = 3;

    if(rt_i2c_transfer(i2c_bus, &msgs, 1) == 1)
        rt_kprintf("multi byte write success!\n");
    else
        rt_kprintf("multi byte write failed...\n");
}
MSH_CMD_EXPORT(i2c_sample_multi_byte_write, i2c_sample_multi_byte_write);

void i2c_sample_single_byte_read(void)
{
    struct rt_i2c_bus_device *i2c_bus;
    struct rt_i2c_msg msgs[2];
    rt_uint8_t send_buf[1], recv_buf[1];

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c2");
    if(i2c_bus == RT_NULL)
    {
        rt_kprintf("can't find %s device!\n", "i2c2");
    }

    send_buf[0] = 0x6B;
    recv_buf[0] = 0x6A;

    msgs[0].addr = 0x68;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = send_buf;
    msgs[0].len = 1;

    msgs[1].addr = 0x68;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = recv_buf;
    msgs[1].len = 1;

    if(rt_i2c_transfer(i2c_bus, msgs, 2) == 2)
        rt_kprintf("single byte read: 0x%02x success!\n", recv_buf[0]);
    else
        rt_kprintf("single byte read failed...\n");
}
MSH_CMD_EXPORT(i2c_sample_single_byte_read, i2c_sample_single_byte_read);

void i2c_sample_error(void)
{
    rt_err_t ret = RT_EOK;
    struct rt_i2c_bus_device *i2c_bus;
    struct rt_i2c_msg msgs[2];
    rt_uint8_t send_buf[1], recv_buf[1];

    i2c_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c2");
    if(i2c_bus == RT_NULL)
    {
        rt_kprintf("can't find i2c device!\n");
    }

    send_buf[0] = 0x6B;
    recv_buf[0] = 0;

    msgs[0].addr = 0x68;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = send_buf;
    msgs[0].len = 1;

    msgs[1].addr = 0x6D;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = recv_buf;
    msgs[1].len = 1;

    ret = rt_i2c_transfer(i2c_bus, msgs, 2);
    if(ret == 2)
        rt_kprintf("single byte read: %02x success!\n", recv_buf[0]);
    else
        rt_kprintf("single byte read %d failed...\n", ret);
}
MSH_CMD_EXPORT(i2c_sample_error, i2c_sample_error);