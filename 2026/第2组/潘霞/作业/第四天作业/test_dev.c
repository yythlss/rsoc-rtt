#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#define TEST_DEV_NAME      "test_dev"
#define TEST_DEV_BUF_SIZE  128

enum test_dev_cmd
{
    TEST_DEV_CMD_CLEAR = 0x10,
    TEST_DEV_CMD_GET_STATS,
};

struct test_dev_stats
{
    rt_uint32_t open_cnt;
    rt_uint32_t read_cnt;
    rt_uint32_t write_cnt;
    rt_uint32_t ctrl_cnt;
    rt_size_t   cache_len;
};

struct test_dev_priv
{
    rt_uint8_t opened;
    struct test_dev_stats stats;
    char cache[TEST_DEV_BUF_SIZE];
};

static struct test_dev_priv g_test_dev;

static rt_err_t test_dev_init(rt_device_t dev)
{
    rt_memset(&g_test_dev, 0, sizeof(g_test_dev));
    dev->user_data = &g_test_dev;
    rt_kprintf("test_dev init\n");
    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    struct test_dev_priv *priv = (struct test_dev_priv *)dev->user_data;
    priv->opened = 1;
    priv->stats.open_cnt++;
    rt_kprintf("test_dev open, oflag=%d\n", oflag);
    return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    struct test_dev_priv *priv = (struct test_dev_priv *)dev->user_data;
    priv->opened = 0;
    rt_kprintf("test_dev close\n");
    return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_dev_priv *priv = (struct test_dev_priv *)dev->user_data;
    rt_size_t copy_len;

    if (buffer == RT_NULL || size == 0)
        return 0;

    if (pos < 0 || (rt_size_t)pos >= priv->stats.cache_len)
        return 0;

    copy_len = size;
    if (copy_len > priv->stats.cache_len - pos)
        copy_len = priv->stats.cache_len - pos;

    rt_memcpy(buffer, priv->cache + pos, copy_len);
    priv->stats.read_cnt++;

    rt_kprintf("test_dev read, pos=%d, size=%d\n", pos, size);
    return copy_len;
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_dev_priv *priv = (struct test_dev_priv *)dev->user_data;
    rt_size_t copy_len;

    if (buffer == RT_NULL || size == 0)
        return 0;

    if (pos < 0 || (rt_size_t)pos >= TEST_DEV_BUF_SIZE)
        return 0;

    copy_len = size;
    if (copy_len > TEST_DEV_BUF_SIZE - pos)
        copy_len = TEST_DEV_BUF_SIZE - pos;

    rt_memcpy(priv->cache + pos, buffer, copy_len);
    if (priv->stats.cache_len < (rt_size_t)(pos + copy_len))
        priv->stats.cache_len = pos + copy_len;

    priv->stats.write_cnt++;

    rt_kprintf("test_dev write, pos=%d, size=%d\n", pos, size);
    return copy_len;
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    struct test_dev_priv *priv = (struct test_dev_priv *)dev->user_data;
    priv->stats.ctrl_cnt++;

    switch (cmd)
    {
    case TEST_DEV_CMD_CLEAR:
        rt_memset(priv->cache, 0, sizeof(priv->cache));
        priv->stats.cache_len = 0;
        rt_kprintf("test_dev control: clear\n");
        return RT_EOK;

    case TEST_DEV_CMD_GET_STATS:
        if (args == RT_NULL)
            return -RT_EINVAL;
        rt_memcpy(args, &priv->stats, sizeof(priv->stats));
        rt_kprintf("test_dev control: get stats\n");
        return RT_EOK;

    default:
        rt_kprintf("test_dev control: unknown cmd=%d\n", cmd);
        return -RT_EINVAL;
    }
}

int rt_test_dev_init(void)
{
    rt_device_t dev = rt_device_create(RT_Device_Class_Char, 0);
    if (dev == RT_NULL)
    {
        rt_kprintf("test_dev create failed\n");
        return -RT_ERROR;
    }

    dev->init = test_dev_init;
    dev->open = test_dev_open;
    dev->close = test_dev_close;
    dev->read = test_dev_read;
    dev->write = test_dev_write;
    dev->control = test_dev_control;

    if (rt_device_register(dev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("test_dev register failed\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}
INIT_BOARD_EXPORT(rt_test_dev_init);

static int test_dev_demo(void)
{
    rt_device_t dev;
    char buf[TEST_DEV_BUF_SIZE + 1];
    struct test_dev_stats stats;
    const char *msg = "hello test_dev";

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("find %s failed\n", TEST_DEV_NAME);
        return -RT_ERROR;
    }

    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("open %s failed\n", TEST_DEV_NAME);
        return -RT_ERROR;
    }

    rt_device_write(dev, 0, msg, rt_strlen(msg));

    rt_memset(buf, 0, sizeof(buf));
    rt_device_read(dev, 0, buf, sizeof(buf) - 1);
    rt_kprintf("read buffer: %s\n", buf);

    rt_memset(&stats, 0, sizeof(stats));
    rt_device_control(dev, TEST_DEV_CMD_GET_STATS, &stats);
    rt_kprintf("stats: open=%u read=%u write=%u ctrl=%u len=%u\n",
               stats.open_cnt, stats.read_cnt, stats.write_cnt,
               stats.ctrl_cnt, (unsigned int)stats.cache_len);

    rt_device_control(dev, TEST_DEV_CMD_CLEAR, RT_NULL);
    rt_device_close(dev);

    return RT_EOK;
}
MSH_CMD_EXPORT(test_dev_demo, test_dev_demo);
