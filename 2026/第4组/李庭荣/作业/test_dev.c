#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG "test_dev"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

static rt_err_t drv_test_init(rt_device_t dev)
{
    return RT_EOK;
}

static rt_err_t drv_test_open(rt_device_t dev, rt_uint16_t oflag)
{
    return RT_EOK;
}

static rt_err_t drv_test_close(rt_device_t dev)
{
    return RT_EOK;
}

static rt_size_t drv_test_read(rt_device_t dev,
                               rt_off_t pos,
                               void *buffer,
                               rt_size_t size)
{
    return size;
}

static rt_size_t drv_test_write(rt_device_t dev,
                                rt_off_t pos,
                                const void *buffer,
                                rt_size_t size)
{
    return size;
}

static rt_err_t drv_test_control(rt_device_t dev, int cmd, void *args)
{
    return RT_EOK;
}

int rt_drv_test_init(void)
{
    rt_device_t test_dev = rt_device_create(RT_Device_Class_Char, 0);

    if (!test_dev)
    {
        LOG_E("test drv create failed");
        return -RT_ENOMEM;
    }

    test_dev->init = drv_test_init;
    test_dev->open = drv_test_open;
    test_dev->close = drv_test_close;
    test_dev->read = drv_test_read;
    test_dev->write = drv_test_write;
    test_dev->control = drv_test_control;

    if (rt_device_register(test_dev, "test_drv", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("test drv register failed");
        rt_device_destroy(test_dev);
        return -RT_ERROR;
    }

    return RT_EOK;
}

INIT_BOARD_EXPORT(rt_drv_test_init);

static int test_drv_cmd(void)
{
    rt_device_t dev;
    char tx_buf[] = "hello";
    char rx_buf[8] = {0};
    rt_err_t result;

    dev = rt_device_find("test_drv");
    if (dev == RT_NULL)
    {
        rt_kprintf("find test_drv failed\n");
        return -RT_ERROR;
    }

    result = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    rt_kprintf("open = %d\n", result);

    rt_kprintf("write = %d\n",
               rt_device_write(dev, 0, tx_buf, sizeof(tx_buf) - 1));

    rt_kprintf("read = %d\n",
               rt_device_read(dev, 0, rx_buf, sizeof(rx_buf)));

    rt_kprintf("control = %d\n",
               rt_device_control(dev, 0, RT_NULL));

    result = rt_device_close(dev);
    rt_kprintf("close = %d\n", result);

    return RT_EOK;
}
MSH_CMD_EXPORT(test_drv_cmd, test test_drv device);
