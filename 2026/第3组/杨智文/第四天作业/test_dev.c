/*
 * Day4 作业：虚拟字符设备驱动框架 test_dev
 *
 * 设备管理层：rt_device_register("test_dev")
 * 应用访问层：rt_device_find/open/read/write/control/close
 * 驱动实现层：test_dev_ops 中的回调函数
 */

#include <rtthread.h>
#include <finsh.h>

#define TEST_DEV_NAME             "test_dev"
#define TEST_DEV_BUFFER_SIZE      128

/* 自定义 control 命令，避开 RT-Thread 已定义的通用命令范围。 */
#define TEST_DEV_CTRL_CLEAR       0x90
#define TEST_DEV_CTRL_GET_STATS   0x91

struct test_dev_stats
{
    rt_uint32_t open_count;
    rt_uint32_t close_count;
    rt_uint32_t read_count;
    rt_uint32_t write_count;
    rt_uint32_t control_count;
    rt_size_t data_length;
};

struct test_dev_data
{
    char buffer[TEST_DEV_BUFFER_SIZE];
    rt_size_t length;
    struct test_dev_stats stats;
};

static rt_device_t test_dev = RT_NULL;
static rt_mutex_t test_dev_lock = RT_NULL;
static struct test_dev_data test_dev_data;

static rt_err_t test_dev_take_lock(void)
{
    if (test_dev_lock == RT_NULL)
    {
        return -RT_ERROR;
    }

    return rt_mutex_take(test_dev_lock, RT_WAITING_FOREVER);
}

static rt_err_t test_dev_init(rt_device_t dev)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;

    if (data == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (test_dev_take_lock() != RT_EOK)
    {
        return -RT_ERROR;
    }
    rt_memset(data->buffer, 0, sizeof(data->buffer));
    data->length = 0;
    rt_memset(&data->stats, 0, sizeof(data->stats));
    rt_mutex_release(test_dev_lock);

    rt_kprintf("[test_dev] initialized\n");
    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;

    RT_UNUSED(oflag);
    if ((data == RT_NULL) || (test_dev_take_lock() != RT_EOK))
    {
        return -RT_ERROR;
    }
    data->stats.open_count++;
    rt_mutex_release(test_dev_lock);
    return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;

    if ((data == RT_NULL) || (test_dev_take_lock() != RT_EOK))
    {
        return -RT_ERROR;
    }
    data->stats.close_count++;
    rt_mutex_release(test_dev_lock);
    return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos,
                               void *buffer, rt_size_t size)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;
    rt_size_t copy_size;

    if ((data == RT_NULL) || (buffer == RT_NULL) || (pos < 0) ||
        (test_dev_take_lock() != RT_EOK))
    {
        return 0;
    }

    if ((rt_size_t)pos >= data->length)
    {
        rt_mutex_release(test_dev_lock);
        return 0;
    }

    copy_size = data->length - (rt_size_t)pos;
    if (copy_size > size)
    {
        copy_size = size;
    }
    rt_memcpy(buffer, &data->buffer[pos], copy_size);
    data->stats.read_count++;
    rt_mutex_release(test_dev_lock);

    return copy_size;
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos,
                                const void *buffer, rt_size_t size)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;
    rt_size_t copy_size;

    if ((data == RT_NULL) || (buffer == RT_NULL) || (pos < 0) ||
        (test_dev_take_lock() != RT_EOK))
    {
        return 0;
    }

    if ((rt_size_t)pos >= TEST_DEV_BUFFER_SIZE)
    {
        rt_mutex_release(test_dev_lock);
        return 0;
    }

    copy_size = TEST_DEV_BUFFER_SIZE - (rt_size_t)pos;
    if (copy_size > size)
    {
        copy_size = size;
    }

    rt_memcpy(&data->buffer[pos], buffer, copy_size);
    if (data->length < ((rt_size_t)pos + copy_size))
    {
        data->length = (rt_size_t)pos + copy_size;
    }
    data->stats.write_count++;
    data->stats.data_length = data->length;
    rt_mutex_release(test_dev_lock);

    return copy_size;
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    struct test_dev_data *data = (struct test_dev_data *)dev->user_data;
    rt_err_t result = RT_EOK;

    if ((data == RT_NULL) || (test_dev_take_lock() != RT_EOK))
    {
        return -RT_ERROR;
    }

    data->stats.control_count++;
    switch (cmd)
    {
    case TEST_DEV_CTRL_CLEAR:
        rt_memset(data->buffer, 0, sizeof(data->buffer));
        data->length = 0;
        data->stats.data_length = 0;
        break;

    case TEST_DEV_CTRL_GET_STATS:
        if (args == RT_NULL)
        {
            result = -RT_EINVAL;
        }
        else
        {
            data->stats.data_length = data->length;
            rt_memcpy(args, &data->stats, sizeof(data->stats));
        }
        break;

    default:
        result = -RT_EINVAL;
        break;
    }
    rt_mutex_release(test_dev_lock);

    return result;
}

#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops test_dev_ops =
{
    test_dev_init,
    test_dev_open,
    test_dev_close,
    test_dev_read,
    test_dev_write,
    test_dev_control,
};
#endif

static int test_dev_register(void)
{
    rt_err_t result;

    if (rt_device_find(TEST_DEV_NAME) != RT_NULL)
    {
        rt_kprintf("[test_dev] device already registered\n");
        return RT_EOK;
    }

    test_dev_lock = rt_mutex_create("td_lock", RT_IPC_FLAG_PRIO);
    if (test_dev_lock == RT_NULL)
    {
        rt_kprintf("[test_dev] create mutex failed\n");
        return -RT_ENOMEM;
    }

    test_dev = rt_device_create(RT_Device_Class_Char, 0);
    if (test_dev == RT_NULL)
    {
        rt_kprintf("[test_dev] create device failed\n");
        rt_mutex_delete(test_dev_lock);
        test_dev_lock = RT_NULL;
        return -RT_ENOMEM;
    }

#ifdef RT_USING_DEVICE_OPS
    /* 新版框架通过 ops 表把统一 API 分派到具体驱动。 */
    test_dev->ops = &test_dev_ops;
#else
    /* 当前 BSP 未启用 RT_USING_DEVICE_OPS，使用同等语义的兼容成员。 */
    test_dev->init = test_dev_init;
    test_dev->open = test_dev_open;
    test_dev->close = test_dev_close;
    test_dev->read = test_dev_read;
    test_dev->write = test_dev_write;
    test_dev->control = test_dev_control;
#endif
    test_dev->user_data = &test_dev_data;
    result = rt_device_register(test_dev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR);
    if (result != RT_EOK)
    {
        rt_kprintf("[test_dev] register failed: %d\n", result);
        rt_free(test_dev);
        test_dev = RT_NULL;
        rt_mutex_delete(test_dev_lock);
        test_dev_lock = RT_NULL;
        return result;
    }

    rt_kprintf("[test_dev] registered as character device\n");
    return RT_EOK;
}
INIT_APP_EXPORT(test_dev_register);

static int test_dev_demo(int argc, char **argv)
{
    rt_device_t dev;
    const char *message = "Day4 virtual device framework";
    char read_buffer[TEST_DEV_BUFFER_SIZE] = {0};
    struct test_dev_stats stats;
    rt_size_t written;
    rt_size_t read;
    rt_err_t result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("test_dev not found\n");
        return -RT_ERROR;
    }

    result = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        rt_kprintf("test_dev open failed: %d\n", result);
        return result;
    }

    result = rt_device_control(dev, TEST_DEV_CTRL_CLEAR, RT_NULL);
    if (result != RT_EOK)
    {
        rt_kprintf("test_dev clear failed: %d\n", result);
        rt_device_close(dev);
        return result;
    }

    written = rt_device_write(dev, 0, message, rt_strlen(message));
    read = rt_device_read(dev, 0, read_buffer, sizeof(read_buffer) - 1);
    read_buffer[read] = '\0';

    result = rt_device_control(dev, TEST_DEV_CTRL_GET_STATS, &stats);
    rt_device_close(dev);
    if (result != RT_EOK)
    {
        rt_kprintf("test_dev get stats failed: %d\n", result);
        return result;
    }

    rt_kprintf("[test_dev_demo] wrote=%u read=%u data=\"%s\"\n",
               (unsigned int)written, (unsigned int)read, read_buffer);
    rt_kprintf("[test_dev_demo] open=%lu close=%lu read=%lu write=%lu control=%lu length=%u\n",
               (unsigned long)stats.open_count,
               (unsigned long)stats.close_count,
               (unsigned long)stats.read_count,
               (unsigned long)stats.write_count,
               (unsigned long)stats.control_count,
               (unsigned int)stats.data_length);
    return RT_EOK;
}
MSH_CMD_EXPORT(test_dev_demo, run virtual character device read write demo);

static int test_dev_status(int argc, char **argv)
{
    rt_device_t dev;
    struct test_dev_stats stats;
    rt_err_t result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("test_dev not found\n");
        return -RT_ERROR;
    }

    result = rt_device_control(dev, TEST_DEV_CTRL_GET_STATS, &stats);
    if (result != RT_EOK)
    {
        return result;
    }
    rt_kprintf("[test_dev] open=%lu close=%lu read=%lu write=%lu control=%lu length=%u\n",
               (unsigned long)stats.open_count,
               (unsigned long)stats.close_count,
               (unsigned long)stats.read_count,
               (unsigned long)stats.write_count,
               (unsigned long)stats.control_count,
               (unsigned int)stats.data_length);
    return RT_EOK;
}
MSH_CMD_EXPORT(test_dev_status, show virtual device statistics);
