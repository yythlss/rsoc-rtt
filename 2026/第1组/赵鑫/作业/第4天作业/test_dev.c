#include <rtthread.h>
#include <rtdevice.h>

rt_device_t dev = RT_NULL;
static char read_buf[64] = {0};
static char write_buf[64] = {0};

/* 驱动函数声明 */
static rt_err_t devtest_init(rt_device_t dev)
{
    rt_kprintf("devtest init ok!\n");
    return RT_EOK;
}

static rt_err_t devtest_open(rt_device_t dev, rt_uint16_t oflag)
{
    rt_kprintf("device open flag : %d\n", oflag);
    return RT_EOK;
}

static rt_err_t devtest_close(rt_device_t dev)
{
    rt_kprintf("device closed!\n");
    return RT_EOK;
}

static rt_size_t devtest_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    const char *str = "this is device read test!";
    rt_size_t len = rt_strlen(str);

    if (size < len) len = size;

    rt_memcpy(buffer, str, len);
    rt_kprintf("read test : %s\n", (char *)buffer);

    return len; 
}

static rt_size_t devtest_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    rt_kprintf("write test : %s\n", (char *)buffer);
    return size; 
}

static rt_err_t devtest_control(rt_device_t dev, int cmd, void *args)
{
    switch(cmd)
    {
    case 0:
        rt_kprintf("this is first cmd!\n");
        break;
    case 1:
        rt_kprintf("this is second cmd!\n");
        break;
    default:
        break;
    }
    return RT_EOK;
}

/* 设备注册 */
int rt_devtest_init(void)
{
    rt_device_t devtest = RT_NULL;
    devtest = rt_device_create(RT_Device_Class_Char, 16);
    if(devtest == RT_NULL)
    {
        rt_kprintf("IO device create failed\n");
        return RT_ERROR;
    }
    devtest->init    = devtest_init;
    devtest->open    = devtest_open;
    devtest->close   = devtest_close;
    devtest->read    = devtest_read;
    devtest->write   = devtest_write;
    devtest->control = devtest_control;

    rt_device_register(devtest, "devtest", RT_DEVICE_FLAG_RDWR);
    return RT_EOK;
}
INIT_BOARD_EXPORT(rt_devtest_init);

int main(void)
{
    dev = rt_device_find("devtest");
    if (dev == RT_NULL) {
        rt_kprintf("Device not found!\n");
        return -RT_ERROR;
    }

    rt_device_init(dev);
    rt_thread_mdelay(1000);


    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    rt_thread_mdelay(1000);

    rt_device_read(dev, 0, read_buf, 20); /* 传入真实数组 */
    rt_thread_mdelay(1000);

    const char *msg = "write test data!";
    rt_device_write(dev, 0, msg, rt_strlen(msg)); /* 传入真实数据 */
    rt_thread_mdelay(1000);

    rt_device_control(dev, 0, RT_NULL);
    rt_thread_mdelay(1000);

    rt_device_close(dev);
    return RT_EOK;
}
