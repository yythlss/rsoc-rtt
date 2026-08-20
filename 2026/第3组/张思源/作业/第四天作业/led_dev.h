#ifndef LED_DEV_H__
#define LED_DEV_H__

#include <rtthread.h>

struct rt_led_ops
{
    rt_err_t (*led_init)(struct rt_device *device);
    rt_err_t (*led_on)(struct rt_device *device, rt_uint8_t led_id);
    rt_err_t (*led_off)(struct rt_device *device, rt_uint8_t led_id);
    rt_err_t (*led_toggle)(struct rt_device *device, rt_uint8_t led_id);
    rt_err_t (*led_set_brightness)(struct rt_device *device, rt_uint8_t led_id, rt_uint8_t led_level);
};

struct rt_device_led
{
    struct rt_device parent;
    const struct rt_led_ops *ops;
};

int rt_device_led_register(const char *name, const struct rt_led_ops *ops, void *user_data);

rt_err_t rt_led_init(void);
rt_err_t rt_led_on(rt_uint8_t led_id);
rt_err_t rt_led_off(rt_uint8_t led_id);
rt_err_t rt_led_toggle(rt_uint8_t led_id);
rt_err_t rt_led_set_brightness(rt_uint8_t led_id, rt_uint8_t led_level);

#endif
