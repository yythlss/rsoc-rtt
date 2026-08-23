/* Day4: a small RT-Thread PIN-backed device framework. */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define TEST_DEV_NAME "test_dev"
#define TEST_OUT_PIN  GET_PIN(F, 12) /* RT-Spark LED_R */
#define TEST_IN_PIN   GET_PIN(C, 5)  /* RT-Spark KEY_UP */

struct test_dev_data
{
    struct rt_device device_object;
    rt_device_t device;
    rt_base_t out_pin;
    rt_base_t in_pin;
    rt_uint8_t out_value;
};

static struct test_dev_data test_data;

static rt_err_t test_dev_init(rt_device_t dev)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;
    rt_pin_mode(data->out_pin, PIN_MODE_OUTPUT);
    rt_pin_write(data->out_pin, PIN_HIGH);
    rt_pin_mode(data->in_pin, PIN_MODE_INPUT_PULLUP);
    data->out_value = 0;
    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    RT_UNUSED(dev); RT_UNUSED(oflag); return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    RT_UNUSED(dev); return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;
    rt_uint8_t value;
    RT_UNUSED(pos);
    if (buffer == RT_NULL || size < 1) return 0;
    value = (rt_uint8_t)rt_pin_read(data->in_pin);
    *(rt_uint8_t *)buffer = value;
    return 1;
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;
    RT_UNUSED(pos);
    if (buffer == RT_NULL || size < 1) return 0;
    data->out_value = *(const rt_uint8_t *)buffer ? 1 : 0;
    rt_pin_write(data->out_pin, data->out_value ? PIN_LOW : PIN_HIGH);
    return 1;
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;
    if (cmd == 0) /* read output latch */
    {
        if (args == RT_NULL) return -RT_EINVAL;
        *(rt_uint8_t *)args = data->out_value;
        return RT_EOK;
    }
    if (cmd == 1) /* toggle output */
    {
        rt_uint8_t value = data->out_value ? 0 : 1;
        return test_dev_write(dev, 0, &value, 1) == 1 ? RT_EOK : -RT_ERROR;
    }
    return -RT_ENOSYS;
}

static int test_dev_register(void)
{
    rt_err_t result;
    rt_memset(&test_data, 0, sizeof(test_data));
    test_data.out_pin = TEST_OUT_PIN;
    test_data.in_pin = TEST_IN_PIN;
    test_data.device = &test_data.device_object;
    test_data.device->type = RT_Device_Class_Miscellaneous;
    /* RT-Thread 4.1.x stores the operations directly in rt_device. */
    test_data.device->init = test_dev_init;
    test_data.device->open = test_dev_open;
    test_data.device->close = test_dev_close;
    test_data.device->read = test_dev_read;
    test_data.device->write = test_dev_write;
    test_data.device->control = test_dev_control;
    test_data.device->user_data = &test_data;
    result = rt_device_register(test_data.device, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR);
    if (result != RT_EOK) { rt_kprintf("register %s failed: %d\n", TEST_DEV_NAME, result); return result; }
    result = rt_device_init(test_data.device);
    if (result != RT_EOK) { rt_kprintf("init %s failed: %d\n", TEST_DEV_NAME, result); return result; }
    rt_kprintf("%s registered: OUT=PF12, IN=PC5\n", TEST_DEV_NAME);
    return RT_EOK;
}
INIT_APP_EXPORT(test_dev_register);

static int test_dev(int argc, char **argv)
{
    rt_device_t dev = rt_device_find(TEST_DEV_NAME);
    rt_uint8_t value, input;
    if (dev == RT_NULL) { rt_kprintf("%s not found\n", TEST_DEV_NAME); return -RT_ENOSYS; }
    if (argc < 2 || !rt_strcmp(argv[1], "read"))
    {
        rt_device_open(dev, RT_DEVICE_FLAG_RDWR);
        rt_device_read(dev, 0, &input, 1);
        rt_kprintf("test_dev read: KEY_UP=%u\n", input);
        rt_device_close(dev); return RT_EOK;
    }
    if (!rt_strcmp(argv[1], "on") || !rt_strcmp(argv[1], "off"))
    {
        value = !rt_strcmp(argv[1], "on");
        rt_device_open(dev, RT_DEVICE_FLAG_RDWR); rt_device_write(dev, 0, &value, 1); rt_device_close(dev);
        rt_kprintf("test_dev output %s\n", argv[1]); return RT_EOK;
    }
    if (!rt_strcmp(argv[1], "toggle"))
    {
        rt_device_control(dev, 1, RT_NULL); rt_kprintf("test_dev toggled\n"); return RT_EOK;
    }
    rt_kprintf("usage: test_dev [read|on|off|toggle]\n"); return -RT_EINVAL;
}
MSH_CMD_EXPORT(test_dev, test PIN-backed device: read/on/off/toggle);
