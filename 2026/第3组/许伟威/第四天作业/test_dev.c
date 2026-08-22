/*
 * test_dev.c - 虚拟设备驱动框架层设计
 *
 * 自己实现一个虚拟字符设备 test_dev，走完整 IO 设备管理流程：
 *   实现 ops(init/open/close/read/write/control)
 *   -> rt_device_register 注册到设备链表
 *   -> rt_device_find 查找 -> rt_device_open 打开
 *   -> rt_device_read/write/control 读写控制 -> rt_device_close 关闭
 *
 * 设备内部维护一个 32 位计数值，write 设置初值，read 读出并自增，
 * control 支持重置/查询。设备在 INIT_DEVICE_EXPORT 阶段自动注册，
 * MSH 输入 test_dev_demo 演示完整调用流程。
 */
#include <rtthread.h>
#include <rtdevice.h>

#define TEST_DEV_NAME      "test_dev"

#define TEST_DEV_CMD_RESET   0x01
#define TEST_DEV_CMD_GET     0x02

struct test_dev
{
    struct rt_device parent;    /* 继承基类 rt_device */
    rt_uint32_t value;          /* 设备内部数据 */
};

static struct test_dev g_test_dev;

static rt_err_t test_dev_init(rt_device_t dev)
{
    struct test_dev *td = (struct test_dev *)dev;

    td->value = 0;
    rt_kprintf("test_dev init done\n");
    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    rt_kprintf("test_dev open, oflag=0x%04x\n", oflag);
    return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    rt_kprintf("test_dev close\n");
    return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_dev *td = (struct test_dev *)dev;

    if (size < sizeof(rt_uint32_t))
        return 0;

    *(rt_uint32_t *)buffer = td->value;
    td->value++;
    return sizeof(rt_uint32_t);
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_dev *td = (struct test_dev *)dev;

    if (size < sizeof(rt_uint32_t))
        return 0;

    td->value = *(const rt_uint32_t *)buffer;
    return sizeof(rt_uint32_t);
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    struct test_dev *td = (struct test_dev *)dev;

    switch (cmd)
    {
    case TEST_DEV_CMD_RESET:
        td->value = 0;
        rt_kprintf("test_dev reset\n");
        break;
    case TEST_DEV_CMD_GET:
        rt_kprintf("test_dev value: %d\n", td->value);
        break;
    default:
        return -RT_EINVAL;
    }
    return RT_EOK;
}

/* 设备注册：在设备初始化阶段自动注册到 IO 设备链表 */
static int test_dev_register(void)
{
    struct test_dev *td = &g_test_dev;

    td->parent.type    = RT_Device_Class_Char;
    td->parent.init    = test_dev_init;
    td->parent.open    = test_dev_open;
    td->parent.close   = test_dev_close;
    td->parent.read    = test_dev_read;
    td->parent.write   = test_dev_write;
    td->parent.control = test_dev_control;

    if (rt_device_register(&td->parent, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("register test_dev failed\n");
        return -1;
    }

    rt_kprintf("test_dev registered\n");
    return 0;
}
INIT_DEVICE_EXPORT(test_dev_register);

/* MSH 命令：演示完整设备调用流程 */
static void test_dev_demo(int argc, char **argv)
{
    rt_device_t dev = rt_device_find(TEST_DEV_NAME);
    rt_uint32_t val;

    if (dev == RT_NULL)
    {
        rt_kprintf("find test_dev failed\n");
        return;
    }

    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);

    val = 100;
    rt_device_write(dev, 0, &val, sizeof(val));
    rt_kprintf("write %d\n", val);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("read %d\n", val);

    rt_device_read(dev, 0, &val, sizeof(val));
    rt_kprintf("read %d\n", val);

    rt_device_control(dev, TEST_DEV_CMD_GET, RT_NULL);
    rt_device_control(dev, TEST_DEV_CMD_RESET, RT_NULL);
    rt_device_control(dev, TEST_DEV_CMD_GET, RT_NULL);

    rt_device_close(dev);
}
MSH_CMD_EXPORT(test_dev_demo, test_dev driver framework demo);
