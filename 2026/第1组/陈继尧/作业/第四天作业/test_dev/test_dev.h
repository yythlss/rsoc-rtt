#ifndef __TEST_DEV_H__
#define __TEST_DEV_H__

#include <rtthread.h>

#define TEST_DEV_NAME            "test_dev"
#define TEST_DEV_CAPACITY        256
#define TEST_DEV_CMD_CLEAR       0x1001
#define TEST_DEV_CMD_INFO        0x1002
#define TEST_DEV_CMD_RESET_STATS 0x1003

struct test_dev_info
{
    rt_size_t capacity;
    rt_size_t used;
    rt_uint32_t open_count;
    rt_uint32_t read_count;
    rt_uint32_t write_count;
    rt_uint32_t read_bytes;
    rt_uint32_t write_bytes;
};

int test_dev_start(void);
rt_err_t test_dev_open(void);
rt_err_t test_dev_close(void);
rt_int32_t test_dev_read(rt_off_t pos, void *buffer, rt_size_t size);
rt_int32_t test_dev_write(rt_off_t pos, const void *buffer, rt_size_t size);
rt_err_t test_dev_control(int command, void *args);

#endif
