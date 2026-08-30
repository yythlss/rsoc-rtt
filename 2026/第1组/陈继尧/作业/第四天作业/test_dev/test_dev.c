#include <rtthread.h>
#include <rtdevice.h>
#include "test_dev.h"

struct test_dev_core
{
    struct rt_device device;
    struct rt_mutex lock;
    rt_uint8_t memory[TEST_DEV_CAPACITY];
    rt_size_t used;
    rt_uint32_t opened;
    rt_uint32_t read_count;
    rt_uint32_t write_count;
    rt_uint32_t read_bytes;
    rt_uint32_t write_bytes;
};

static struct test_dev_core test_dev0;

rt_err_t test_dev_open(void)
{
    rt_mutex_take(&test_dev0.lock, RT_WAITING_FOREVER);
    test_dev0.opened++;
    rt_mutex_release(&test_dev0.lock);
    return RT_EOK;
}

rt_err_t test_dev_close(void)
{
    rt_mutex_take(&test_dev0.lock, RT_WAITING_FOREVER);
    if (test_dev0.opened == 0)
    {
        rt_mutex_release(&test_dev0.lock);
        return -RT_ERROR;
    }
    test_dev0.opened--;
    rt_mutex_release(&test_dev0.lock);
    return RT_EOK;
}

rt_int32_t test_dev_read(rt_off_t pos, void *buffer, rt_size_t size)
{
    rt_size_t available;

    if (pos < 0 || buffer == RT_NULL || size == 0)
    {
        return -RT_EINVAL;
    }
    rt_mutex_take(&test_dev0.lock, RT_WAITING_FOREVER);
    if (test_dev0.opened == 0 || (rt_size_t)pos >= test_dev0.used)
    {
        rt_mutex_release(&test_dev0.lock);
        return 0;
    }
    available = test_dev0.used - (rt_size_t)pos;
    if (size > available)
    {
        size = available;
    }
    rt_memcpy(buffer, &test_dev0.memory[pos], size);
    test_dev0.read_count++;
    test_dev0.read_bytes += (rt_uint32_t)size;
    rt_mutex_release(&test_dev0.lock);
    return (rt_int32_t)size;
}

rt_int32_t test_dev_write(rt_off_t pos, const void *buffer, rt_size_t size)
{
    rt_size_t available;
    rt_size_t end;

    if (pos < 0 || buffer == RT_NULL || size == 0)
    {
        return -RT_EINVAL;
    }
    rt_mutex_take(&test_dev0.lock, RT_WAITING_FOREVER);
    if (test_dev0.opened == 0 || (rt_size_t)pos >= TEST_DEV_CAPACITY)
    {
        rt_mutex_release(&test_dev0.lock);
        return 0;
    }
    available = TEST_DEV_CAPACITY - (rt_size_t)pos;
    if (size > available)
    {
        size = available;
    }
    rt_memcpy(&test_dev0.memory[pos], buffer, size);
    end = (rt_size_t)pos + size;
    if (end > test_dev0.used)
    {
        test_dev0.used = end;
    }
    test_dev0.write_count++;
    test_dev0.write_bytes += (rt_uint32_t)size;
    rt_mutex_release(&test_dev0.lock);
    return (rt_int32_t)size;
}

rt_err_t test_dev_control(int command, void *args)
{
    struct test_dev_info *info;

    rt_mutex_take(&test_dev0.lock, RT_WAITING_FOREVER);
    switch (command)
    {
    case TEST_DEV_CMD_CLEAR:
        rt_memset(test_dev0.memory, 0, sizeof(test_dev0.memory));
        test_dev0.used = 0;
        break;
    case TEST_DEV_CMD_INFO:
        if (args == RT_NULL)
        {
            rt_mutex_release(&test_dev0.lock);
            return -RT_EINVAL;
        }
        info = (struct test_dev_info *)args;
        info->capacity = TEST_DEV_CAPACITY;
        info->used = test_dev0.used;
        info->open_count = test_dev0.opened;
        info->read_count = test_dev0.read_count;
        info->write_count = test_dev0.write_count;
        info->read_bytes = test_dev0.read_bytes;
        info->write_bytes = test_dev0.write_bytes;
        break;
    case TEST_DEV_CMD_RESET_STATS:
        test_dev0.read_count = 0;
        test_dev0.write_count = 0;
        test_dev0.read_bytes = 0;
        test_dev0.write_bytes = 0;
        break;
    default:
        rt_mutex_release(&test_dev0.lock);
        return -RT_ENOSYS;
    }
    rt_mutex_release(&test_dev0.lock);
    return RT_EOK;
}

static rt_err_t test_dev_rtt_init(rt_device_t dev)
{
    RT_UNUSED(dev);
    return RT_EOK;
}

static rt_err_t test_dev_rtt_open(rt_device_t dev, rt_uint16_t oflag)
{
    RT_UNUSED(dev);
    RT_UNUSED(oflag);
    return test_dev_open();
}

static rt_err_t test_dev_rtt_close(rt_device_t dev)
{
    RT_UNUSED(dev);
    return test_dev_close();
}

static rt_size_t test_dev_rtt_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    rt_int32_t result;
    RT_UNUSED(dev);
    result = test_dev_read(pos, buffer, size);
    return result > 0 ? (rt_size_t)result : 0;
}

static rt_size_t test_dev_rtt_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    rt_int32_t result;
    RT_UNUSED(dev);
    result = test_dev_write(pos, buffer, size);
    return result > 0 ? (rt_size_t)result : 0;
}

static rt_err_t test_dev_rtt_control(rt_device_t dev, int command, void *args)
{
    RT_UNUSED(dev);
    return test_dev_control(command, args);
}

int test_dev_start(void)
{
    rt_memset(&test_dev0, 0, sizeof(test_dev0));
    rt_mutex_init(&test_dev0.lock, "tdlock", RT_IPC_FLAG_PRIO);
    test_dev0.device.type = RT_Device_Class_Char;
    test_dev0.device.init = test_dev_rtt_init;
    test_dev0.device.open = test_dev_rtt_open;
    test_dev0.device.close = test_dev_rtt_close;
    test_dev0.device.read = test_dev_rtt_read;
    test_dev0.device.write = test_dev_rtt_write;
    test_dev0.device.control = test_dev_rtt_control;
    return rt_device_register(&test_dev0.device, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR);
}
INIT_DEVICE_EXPORT(test_dev_start);
