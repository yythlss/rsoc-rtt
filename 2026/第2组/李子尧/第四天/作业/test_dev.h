#ifndef __TEST_DEV_H__
#define __TEST_DEV_H__
#include <rtthread.h>
#include <rtdevice.h>
#define TEST_DEV_NAME "test_dev"
#define TEST_DEV_GET_STATUS 0x01
struct test_dev_status { rt_uint32_t open_count; rt_bool_t initialized; };
#endif

