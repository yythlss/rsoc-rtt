#include <rtthread.h>
#include <rtdevice.h>

/* 1. 定义虚拟设备的私有数据结构 */
struct test_dev
{
    struct rt_device parent;  /* 继承标准设备基类 */
    rt_uint32_t data;         /* 设备内部存储的数据 */
};

/* 2. 实现底层硬件操作函数（此处为虚拟操作） */
static rt_err_t test_dev_init(rt_device_t dev)
{
    struct test_dev *tdev = (struct test_dev *)dev;
    tdev->data = 0;
    rt_kprintf("test_dev: initialized.\n");
    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    rt_kprintf("test_dev: opened, flag: %d\n", oflag);
    return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    rt_kprintf("test_dev: closed.\n");
    return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_dev *tdev = (struct test_dev *)dev;
    /* 将设备内部数据拷贝到用户的 buffer 中 */
    rt_memcpy(buffer, &tdev->data, (size > sizeof(rt_uint32_t)) ? sizeof(rt_uint32_t) : size);
    rt_kprintf("test_dev: read data = %d\n", tdev->data);
    return sizeof(rt_uint32_t);
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_dev *tdev = (struct test_dev *)dev;
    /* 将用户写入的数据保存到设备内部 */
    rt_memcpy(&tdev->data, buffer, (size > sizeof(rt_uint32_t)) ? sizeof(rt_uint32_t) : size);
    rt_kprintf("test_dev: write data = %d\n", tdev->data);
    return sizeof(rt_uint32_t);
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    rt_kprintf("test_dev: control cmd = %d\n", cmd);
    return RT_EOK;
}

/* 3. 定义设备操作函数集 (ops) */
#ifdef RT_USING_DEVICE_OPS
static const struct rt_device_ops test_dev_ops =
{
    test_dev_init,
    test_dev_open,
    test_dev_close,
    test_dev_read,
    test_dev_write,
    test_dev_control
};
#endif

/* 4. 定义设备实例并实现注册函数 */
static struct test_dev _test_dev;

static int rt_hw_test_dev_init(void)
{
    /* 设置设备类型为字符设备 */
    _test_dev.parent.type = RT_Device_Class_Char;

#ifdef RT_USING_DEVICE_OPS
    /* 绑定操作函数集 */
    _test_dev.parent.ops = &test_dev_ops;
#else
    /* 兼容旧版本，直接绑定函数指针 */
    _test_dev.parent.init = test_dev_init;
    _test_dev.parent.open = test_dev_open;
    _test_dev.parent.close = test_dev_close;
    _test_dev.parent.read = test_dev_read;
    _test_dev.parent.write = test_dev_write;
    _test_dev.parent.control = test_dev_control;
#endif

    /* 将设备注册到 I/O 设备管理器，支持读写 */
    rt_err_t result = rt_device_register(&_test_dev.parent, "test_dev", RT_DEVICE_FLAG_RDWR);

    if (result == RT_EOK)
    {
        rt_kprintf("test_dev: registered successfully.\n");
    }
    return result;
}

/* 5. 使用自动初始化宏，确保系统启动时自动注册该设备 */
INIT_DEVICE_EXPORT(rt_hw_test_dev_init);

/* 6. (可选) 导出一个 MSH 命令用于测试读写 */
static void test_dev_demo(int argc, char **argv)
{
    rt_device_t dev = rt_device_find("test_dev");
    if (dev == RT_NULL)
    {
        rt_kprintf("Can't find test_dev!\n");
        return;
    }

    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);

    /* 写入测试数据 */
    rt_uint32_t write_val = 12345;
    rt_device_write(dev, 0, &write_val, sizeof(write_val));

    /* 读取测试数据 */
    rt_uint32_t read_val = 0;
    rt_device_read(dev, 0, &read_val, sizeof(read_val));

    rt_kprintf("Verify data: %d\n", read_val);
    rt_device_close(dev);
}
MSH_CMD_EXPORT(test_dev_demo, Test virtual device read/write);
int main(void)
{

    return 0;
}

