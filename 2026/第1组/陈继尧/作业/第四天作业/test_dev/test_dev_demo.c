#include <rtthread.h>
#include <rtdevice.h>
#include "test_dev.h"

static int test_dev_run(void)
{
    static const rt_uint8_t tx[] = {0x10, 0x22, 0x34, 0x48};
    rt_uint8_t rx[sizeof(tx)];
    struct test_dev_info info;
    rt_device_t dev;
    rt_size_t index;
    rt_int32_t result;
    int failed = 0;

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL || rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("test_dev_run: open failed\n");
        return -RT_ERROR;
    }

    rt_device_control(dev, TEST_DEV_CMD_CLEAR, RT_NULL);
    result = (rt_int32_t)rt_device_write(dev, 12, tx, sizeof(tx));
    if (result != (rt_int32_t)sizeof(tx))
    {
        rt_kprintf("test_dev_run: write failed (%d)\n", result);
        failed = 1;
    }

    rt_memset(rx, 0, sizeof(rx));
    result = (rt_int32_t)rt_device_read(dev, 12, rx, sizeof(rx));
    if (result != (rt_int32_t)sizeof(rx))
    {
        rt_kprintf("test_dev_run: read failed (%d)\n", result);
        failed = 1;
    }
    for (index = 0; index < sizeof(tx); index++)
    {
        if (rx[index] != tx[index])
        {
            rt_kprintf("test_dev_run: compare failed at %u\n", (unsigned int)index);
            failed = 1;
            break;
        }
    }

    result = (rt_int32_t)rt_device_write(dev, TEST_DEV_CAPACITY - 2, tx, sizeof(tx));
    if (result != 2)
    {
        rt_kprintf("test_dev_run: boundary failed (%d)\n", result);
        failed = 1;
    }
    rt_memset(&info, 0, sizeof(info));
    if (rt_device_control(dev, TEST_DEV_CMD_INFO, &info) != RT_EOK ||
        info.used != TEST_DEV_CAPACITY)
    {
        rt_kprintf("test_dev_run: info failed (used=%u)\n", (unsigned int)info.used);
        failed = 1;
    }

    rt_device_control(dev, TEST_DEV_CMD_CLEAR, RT_NULL);
    if (rt_device_control(dev, TEST_DEV_CMD_INFO, &info) != RT_EOK || info.used != 0)
    {
        rt_kprintf("test_dev_run: clear failed\n");
        failed = 1;
    }
    if (rt_device_read(dev, 0, rx, sizeof(rx)) != 0)
    {
        rt_kprintf("test_dev_run: empty read failed\n");
        failed = 1;
    }

    rt_device_close(dev);
    rt_kprintf("test_dev_run: %s\n", failed ? "FAIL" : "PASS");
    return failed ? -RT_ERROR : RT_EOK;
}
MSH_CMD_EXPORT(test_dev_run, run virtual device self test);
