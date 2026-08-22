#include "test_dev_led.h"
#include <stdlib.h>
#include <stdio.h>

/*
 * 虚拟 LED 的"硬件寄存器"——纯内存模拟，不碰任何真实外设。
 * 在真实 RT-Thread 里，这里会是 GPIO 寄存器基址 / PWM 句柄。
 */
typedef struct {
    uint8_t on;          /* 0=灭 1=亮，模拟 GPIO 输出位 */
    uint8_t brightness;  /* 0~100，模拟 PWM 占空比 */
} virt_led_hw_t;

static int led_init(test_dev_t *dev)
{
    virt_led_hw_t *hw = (virt_led_hw_t *)dev->user_data;
    hw->on = 0;
    hw->brightness = 100;
    printf("[virt_led] %s init: off, brightness=100%%\n", dev->name);
    return 0;
}

static int led_deinit(test_dev_t *dev)
{
    virt_led_hw_t *hw = (virt_led_hw_t *)dev->user_data;
    hw->on = 0;
    printf("[virt_led] %s deinit\n", dev->name);
    return 0;
}

static int led_read(test_dev_t *dev, void *buf, size_t len)
{
    virt_led_hw_t *hw = (virt_led_hw_t *)dev->user_data;
    if (len < sizeof(uint8_t))
        return -1;
    *(uint8_t *)buf = hw->on;
    return sizeof(uint8_t);
}

static int led_write(test_dev_t *dev, const void *buf, size_t len)
{
    virt_led_hw_t *hw = (virt_led_hw_t *)dev->user_data;
    if (len < sizeof(uint8_t))
        return -1;
    hw->on = (*(const uint8_t *)buf) ? 1 : 0;
    printf("[virt_led] %s -> %s\n", dev->name, hw->on ? "ON" : "OFF");
    return sizeof(uint8_t);
}

static int led_control(test_dev_t *dev, int cmd, void *args)
{
    virt_led_hw_t *hw = (virt_led_hw_t *)dev->user_data;
    switch (cmd) {
    case TEST_LED_CMD_SET_BRIGHTNESS:
        hw->brightness = *(const uint8_t *)args;
        printf("[virt_led] %s brightness=%u%%\n", dev->name, hw->brightness);
        return 0;
    case TEST_LED_CMD_TOGGLE:
        hw->on = !hw->on;
        printf("[virt_led] %s toggle -> %s\n", dev->name, hw->on ? "ON" : "OFF");
        return 0;
    default:
        return -1;
    }
}

/* 驱动层实现：把上面的函数填进回调表 */
static const test_dev_driver_ops_t led_ops = {
    .init   = led_init,
    .deinit = led_deinit,
    .read   = led_read,
    .write  = led_write,
    .control= led_control,
};

/* 创建虚拟 LED：分配自己的私有硬件状态，再交给框架层托管 */
test_dev_t *test_dev_led_create(const char *name)
{
    virt_led_hw_t *hw = (virt_led_hw_t *)calloc(1, sizeof(virt_led_hw_t));
    if (!hw)
        return NULL;
    return test_dev_create(name, TEST_DEV_TYPE_LED, &led_ops, hw);
}
