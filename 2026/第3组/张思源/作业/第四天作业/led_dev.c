#include "led_dev.h"

static struct rt_device_led _hw_led;  // 全局设备实例

int rt_device_led_register(const char *name, const struct rt_led_ops *ops, void *user_data)
{
    _hw_led.parent.type         = RT_Device_Class_Miscellaneous;
    _hw_led.parent.rx_indicate  = RT_NULL;
    _hw_led.parent.tx_complete  = RT_NULL;

    _hw_led.ops                 = ops;
    _hw_led.parent.user_data    = user_data;

    rt_device_register(&_hw_led.parent, name, RT_DEVICE_FLAG_RDWR);
    return 0;
}

rt_err_t rt_led_init(void)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    return _hw_led.ops->led_init(&_hw_led.parent);
}

rt_err_t rt_led_on(rt_uint8_t led_id)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    return _hw_led.ops->led_on(&_hw_led.parent, led_id);
}

rt_err_t rt_led_off(rt_uint8_t led_id)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    return _hw_led.ops->led_off(&_hw_led.parent, led_id);
}

rt_err_t rt_led_toggle(rt_uint8_t led_id)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    return _hw_led.ops->led_toggle(&_hw_led.parent, led_id);
}

rt_err_t rt_led_set_brightness(rt_uint8_t led_id, rt_uint8_t led_level)
{
    RT_ASSERT(_hw_led.ops != RT_NULL);
    return _hw_led.ops->led_set_brightness(&_hw_led.parent, led_id, led_level);
}

static rt_err_t virtual_led_init(struct rt_device *device)
{
    rt_kprintf("[virtual_led] LED device init\n");
    return RT_EOK;
}

static rt_err_t virtual_led_on(struct rt_device *device, rt_uint8_t led_id)
{
    rt_kprintf("[virtual_led] LED %d ON\n", led_id);
    return RT_EOK;
}

static rt_err_t virtual_led_off(struct rt_device *device, rt_uint8_t led_id)
{
    rt_kprintf("[virtual_led] LED %d OFF\n", led_id);
    return RT_EOK;
}

static rt_err_t virtual_led_toggle(struct rt_device *device, rt_uint8_t led_id)
{
    rt_kprintf("[virtual_led] LED %d TOGGLE\n", led_id);
    return RT_EOK;
}

static rt_err_t virtual_led_set_brightness(struct rt_device *device, rt_uint8_t led_id, rt_uint8_t led_level)
{
    rt_kprintf("[virtual_led] LED %d SET BRIGHTNESS %d\n", led_id, led_level);
    return RT_EOK;
}

const static struct rt_led_ops _virtual_led_ops =
{
    virtual_led_init,
    virtual_led_on,
    virtual_led_off,
    virtual_led_toggle,
    virtual_led_set_brightness,
};

int rt_hw_led_init(void)
{
    return rt_device_led_register("led", &_virtual_led_ops, RT_NULL);
}
INIT_BOARD_EXPORT(rt_hw_led_init);
