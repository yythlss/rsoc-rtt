#ifndef TEST_DEV_LED_H
#define TEST_DEV_LED_H

#include "test_dev.h"

/* 驱动层创建并注册一个虚拟 LED 设备 */
test_dev_t *test_dev_led_create(const char *name);

/* 虚拟 LED 的控制命令 */
#define TEST_LED_CMD_SET_BRIGHTNESS 0x10   /* args: uint8_t* (0~100) */
#define TEST_LED_CMD_TOGGLE          0x11   /* args: NULL */

#endif /* TEST_DEV_LED_H */
