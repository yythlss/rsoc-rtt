#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include "test_dev.h"

static const char test_dev_data[] = "RT-Thread test device";
struct test_dev_context { rt_uint32_t open_count; rt_bool_t initialized; };
static struct test_dev_context test_dev_ctx;

static rt_err_t test_dev_init(rt_device_t dev)
{ struct test_dev_context *ctx = dev->user_data; if (!ctx) return -RT_ERROR; ctx->initialized = RT_TRUE; rt_kprintf("test_dev init\n"); return RT_EOK; }
static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{ struct test_dev_context *ctx = dev->user_data; if (!ctx || !ctx->initialized) return -RT_ERROR; ctx->open_count++; rt_kprintf("test_dev open, count: %u, flags: 0x%x\n", ctx->open_count, oflag); return RT_EOK; }
static rt_err_t test_dev_close(rt_device_t dev)
{ RT_UNUSED(dev); rt_kprintf("test_dev close\n"); return RT_EOK; }
static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{ rt_size_t available, copied; RT_UNUSED(dev); if (!buffer || pos < 0) return 0; available = sizeof(test_dev_data) - 1; if ((rt_size_t)pos >= available) return 0; copied = size < (available - (rt_size_t)pos) ? size : (available - (rt_size_t)pos); rt_memcpy(buffer, test_dev_data + pos, copied); rt_kprintf("test_dev read, pos: %d, size: %d\n", pos, copied); return copied; }
static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{ char text[65]; rt_size_t copied; RT_UNUSED(dev); RT_UNUSED(pos); if (!buffer) return 0; copied = size < (sizeof(text) - 1) ? size : (sizeof(text) - 1); rt_memcpy(text, buffer, copied); text[copied] = '\0'; rt_kprintf("test_dev write: %s\n", text); return size; }
static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{ struct test_dev_context *ctx = dev->user_data; struct test_dev_status *status = args; if (!ctx) return -RT_ERROR; if (cmd != TEST_DEV_GET_STATUS) return -RT_ENOSYS; if (!status) return -RT_EINVAL; status->open_count = ctx->open_count; status->initialized = ctx->initialized; rt_kprintf("test_dev control: GET_STATUS\n"); return RT_EOK; }

static const struct rt_device_ops test_dev_ops = { test_dev_init, test_dev_open, test_dev_close, test_dev_read, test_dev_write, test_dev_control };

static int test_dev_register(void)
{ rt_device_t dev = rt_device_create(RT_Device_Class_Char, 0); if (!dev) { rt_kprintf("test_dev create failed\n"); return -RT_ENOMEM; } dev->ops = &test_dev_ops; dev->user_data = &test_dev_ctx; if (rt_device_register(dev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR) != RT_EOK) { rt_device_destroy(dev); rt_kprintf("test_dev register failed\n"); return -RT_ERROR; } return RT_EOK; }
INIT_DEVICE_EXPORT(test_dev_register);

static int test_dev_test(void)
{ rt_device_t dev = rt_device_find(TEST_DEV_NAME); struct test_dev_status status; char read_buffer[32] = {0}; const char write_data[] = "hello test_dev"; rt_size_t n; rt_err_t result; if (!dev) { rt_kprintf("test_dev find failed\n"); return -RT_ERROR; } if ((result = rt_device_init(dev)) != RT_EOK) return result; if ((result = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR)) != RT_EOK) return result; n = rt_device_read(dev, 0, read_buffer, sizeof(read_buffer) - 1); read_buffer[n] = '\0'; rt_kprintf("test_dev application read: %s\n", read_buffer); rt_device_write(dev, 0, write_data, sizeof(write_data) - 1); result = rt_device_control(dev, TEST_DEV_GET_STATUS, &status); if (result == RT_EOK) rt_kprintf("test_dev status: initialized=%d, open_count=%u\n", status.initialized, status.open_count); rt_device_close(dev); return result; }
MSH_CMD_EXPORT(test_dev_test, test test_dev I/O device operations);

