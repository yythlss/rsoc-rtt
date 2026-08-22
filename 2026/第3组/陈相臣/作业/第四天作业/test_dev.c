/*
 * test_dev.c
 * 虚拟设备驱动框架示例
 *
 * 分层关系：
 *   应用层 -> rt_device_open/read/write/control
 *   IO 管理层 -> rt_device_register/rt_device_find
 *   驱动层 -> test_dev 的 init/open/close/read/write/control
 */
#include <rtthread.h>

#define TEST_DEV_NAME               "test_dev"
#define TEST_DEV_CTRL_GET_VALUE     (RT_DEVICE_CTRL_BASE(Char) + 1)
#define TEST_DEV_CTRL_SET_VALUE     (RT_DEVICE_CTRL_BASE(Char) + 2)
#define TEST_DEV_CTRL_RESET         (RT_DEVICE_CTRL_BASE(Char) + 3)

struct test_dev_data
{
    rt_uint32_t value;
};

static struct test_dev_data test_data;

static rt_err_t test_dev_init(rt_device_t dev)
{
    test_data.value = 0;
    dev->user_data = &test_data;
    rt_kprintf("test_dev init\n");
    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    (void)dev;
    (void)oflag;
    rt_kprintf("test_dev open\n");
    return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    (void)dev;
    rt_kprintf("test_dev close\n");
    return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos,
                               void *buffer, rt_size_t size)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;

    if (buffer == RT_NULL || pos != 0 || size < sizeof(data->value))
        return 0;

    rt_memcpy(buffer, &data->value, sizeof(data->value));
    return sizeof(data->value);
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos,
                                const void *buffer, rt_size_t size)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;

    (void)pos;
    if (buffer == RT_NULL || size < sizeof(data->value))
        return 0;

    rt_memcpy(&data->value, buffer, sizeof(data->value));
    return sizeof(data->value);
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;

    switch (cmd)
    {
    case TEST_DEV_CTRL_GET_VALUE:
        if (args == RT_NULL)
            return -RT_ERROR;
        *(rt_uint32_t *)args = data->value;
        break;
    case TEST_DEV_CTRL_SET_VALUE:
        if (args == RT_NULL)
            return -RT_ERROR;
        data->value = *(rt_uint32_t *)args;
        break;
    case TEST_DEV_CTRL_RESET:
        data->value = 0;
        break;
    default:
        return -RT_ENOSYS;
    }

    return RT_EOK;
}

static int test_dev_register(void)
{
    rt_device_t dev = rt_device_create(RT_Device_Class_Char, 0);

    if (dev == RT_NULL)
        return -RT_ERROR;

    dev->init = test_dev_init;
    dev->open = test_dev_open;
    dev->close = test_dev_close;
    dev->read = test_dev_read;
    dev->write = test_dev_write;
    dev->control = test_dev_control;

    if (rt_device_register(dev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_device_destroy(dev);
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_DEVICE_EXPORT(test_dev_register);

static void test_dev_demo(int argc, char **argv)
{
    rt_device_t dev;
    rt_uint32_t value;

    (void)argc;
    (void)argv;

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("test_dev not found\n");
        return;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
        return;

    value = 1234;
    rt_device_write(dev, 0, &value, sizeof(value));
    value = 0;
    rt_device_read(dev, 0, &value, sizeof(value));
    rt_kprintf("write then read: %u\n", value);

    value = 5678;
    rt_device_control(dev, TEST_DEV_CTRL_SET_VALUE, &value);
    value = 0;
    rt_device_control(dev, TEST_DEV_CTRL_GET_VALUE, &value);
    rt_kprintf("control get: %u\n", value);

    rt_device_control(dev, TEST_DEV_CTRL_RESET, RT_NULL);
    rt_device_control(dev, TEST_DEV_CTRL_GET_VALUE, &value);
    rt_kprintf("after reset: %u\n", value);

    rt_device_close(dev);
}
MSH_CMD_EXPORT(test_dev_demo, test virtual device framework);
