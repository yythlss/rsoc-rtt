/*
 * 虚拟设备驱动框架层 test_dev -- 框架实现
 *
 * 本文件实现了一个完整的 RT-Thread 字符设备驱动框架：
 *   1. 通过 rt_device_create 申请内核设备对象，附加空间存放驱动私有数据；
 *   2. 统一实现 init/open/close/read/write/control 六个 ops 并挂接到设备；
 *   3. 通过 rt_device_register 注册进内核 I/O 设备模型；
 *   4. 内部使用互斥锁保证多线程访问安全；
 *   5. 用一块 RAM 模拟真实硬件的存储（相当于一个虚拟 EEPROM）。
 *
 * 应用层无需知道内部实现，只通过 rt_device_* 标准接口访问设备。
 */

#include "test_dev.h"

#define DBG_TAG "test_dev.drv"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 驱动私有数据结构，保存在 rt_device 对象之后的附加空间里，
 * 通过 dev->user_data 指向它。真实驱动里这里会放寄存器基址、DMA 句柄等 */
struct test_dev
{
    rt_uint8_t *buf;            /* 模拟存储（虚拟硬件） */
    rt_uint32_t buf_size;       /* 存储容量 */
    rt_uint8_t  version;        /* 框架版本号 */
    rt_uint8_t  mode;           /* 工作模式 */
    rt_uint32_t open_count;     /* 累计打开次数 */
    rt_uint32_t read_count;     /* 累计读次数 */
    rt_uint32_t write_count;    /* 累计写次数 */
    rt_mutex_t  lock;           /* 保护 buf 的互斥锁 */
};
typedef struct test_dev *test_dev_t;

#define _TEST_DEV(t)    ((test_dev_t)(t)->user_data)

/* ============================ ops 实现 ============================ */

static rt_err_t test_dev_init(rt_device_t dev)
{
    test_dev_t tdev = _TEST_DEV(dev);

    /* 复位设备统计信息，等价于真实驱动里的"上电初始化" */
    tdev->open_count  = 0;
    tdev->read_count  = 0;
    tdev->write_count = 0;

    LOG_I("init: buf=%p size=%u", tdev->buf, tdev->buf_size);

    return RT_EOK;
}

static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    test_dev_t tdev = _TEST_DEV(dev);

    /* 本虚拟设备只支持读写方式打开 */
    if (!(oflag & RT_DEVICE_OFLAG_RDWR))
    {
        LOG_W("open: only RDWR oflag supported");
        return -RT_EINVAL;
    }

    /* 注册时带 RT_DEVICE_FLAG_STANDALONE，内核已拒绝重复打开，
     * 这里再检查一次作为驱动自身的防御 */
    if (dev->open_flag & RT_DEVICE_OFLAG_OPEN)
    {
        LOG_W("open: device already opened");
        return -RT_EBUSY;
    }

    tdev->open_count++;
    LOG_I("open: oflag=%04x, open times=%u", oflag, tdev->open_count);

    return RT_EOK;
}

static rt_err_t test_dev_close(rt_device_t dev)
{
    LOG_I("close: %s", dev->parent.name);

    return RT_EOK;
}

static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    test_dev_t tdev = _TEST_DEV(dev);
    rt_size_t len = size;

    /* 参数检查 */
    if (buffer == RT_NULL || size == 0)
    {
        LOG_W("read: invalid param, buffer=%p size=%u", buffer, size);
        return 0;
    }

    if (pos < 0 || pos >= (rt_off_t)tdev->buf_size)
    {
        LOG_W("read: pos %d out of range [0, %u)", pos, tdev->buf_size);
        return 0;
    }

    /* 越界部分裁剪，模拟真实硬件的容量限制 */
    if ((rt_uint32_t)pos + size > tdev->buf_size)
        len = tdev->buf_size - pos;

    rt_mutex_take(tdev->lock, RT_WAITING_FOREVER);
    rt_memcpy(buffer, tdev->buf + pos, len);
    tdev->read_count++;
    rt_mutex_release(tdev->lock);

    LOG_D("read: pos=%d size=%u -> %u (total %u)", pos, size, len, tdev->read_count);

    return len;
}

static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    test_dev_t tdev = _TEST_DEV(dev);
    rt_size_t len = size;

    /* 参数检查 */
    if (buffer == RT_NULL || size == 0)
    {
        LOG_W("write: invalid param, buffer=%p size=%u", buffer, size);
        return 0;
    }

    if (pos < 0 || pos >= (rt_off_t)tdev->buf_size)
    {
        LOG_W("write: pos %d out of range [0, %u)", pos, tdev->buf_size);
        return 0;
    }

    if ((rt_uint32_t)pos + size > tdev->buf_size)
        len = tdev->buf_size - pos;

    rt_mutex_take(tdev->lock, RT_WAITING_FOREVER);

    /* 静默模式：模拟"设备被禁用"，数据直接丢弃 */
    if (tdev->mode == TEST_DEV_MODE_SILENT)
    {
        LOG_W("write: silent mode, %u bytes dropped", size);
        rt_mutex_release(tdev->lock);
        return 0;
    }

    rt_memcpy(tdev->buf + pos, buffer, len);
    tdev->write_count++;
    rt_mutex_release(tdev->lock);

    LOG_D("write: pos=%d size=%u -> %u (total %u)", pos, size, len, tdev->write_count);

    return len;
}

static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    test_dev_t tdev = _TEST_DEV(dev);

    switch (cmd)
    {
    case TEST_DEV_CMD_CLEAR:
        rt_mutex_take(tdev->lock, RT_WAITING_FOREVER);
        rt_memset(tdev->buf, 0, tdev->buf_size);
        rt_mutex_release(tdev->lock);
        LOG_I("control: CLEAR, %u bytes erased", tdev->buf_size);
        break;

    case TEST_DEV_CMD_GET_INFO:
        if (args == RT_NULL)
            return -RT_EINVAL;
        {
            struct test_dev_info *info = (struct test_dev_info *)args;
            info->version     = tdev->version;
            info->mode        = tdev->mode;
            info->buf_size    = tdev->buf_size;
            info->open_count  = tdev->open_count;
            info->read_count  = tdev->read_count;
            info->write_count = tdev->write_count;
        }
        break;

    case TEST_DEV_CMD_SET_MODE:
        tdev->mode = (rt_uint8_t)((rt_uint32_t)args & 0xff);
        LOG_I("control: SET_MODE -> %u", tdev->mode);
        break;

    case TEST_DEV_CMD_GET_VERSION:
        if (args == RT_NULL)
            return -RT_EINVAL;
        *(rt_uint8_t *)args = tdev->version;
        break;

    case RT_DEVICE_CTRL_CONFIG:
        /* 内核通用命令：演示驱动可以处理通用命令 */
        LOG_I("control: RT_DEVICE_CTRL_CONFIG, args=%p", args);
        break;

    default:
        LOG_W("control: unsupported cmd 0x%02x", cmd);
        return -RT_EINVAL;
    }

    return RT_EOK;
}

/* ============================ 框架层接口 ============================ */

/**
 * @brief 注册一个 test_dev 虚拟设备实例
 *
 * @param name 设备名（rt_device_find 使用，最长 RT_NAME_MAX）
 *
 * @return RT_EOK 成功，其它值失败
 */
rt_err_t rt_test_dev_register(const char *name)
{
    rt_device_t dev;
    test_dev_t tdev;

    RT_ASSERT(name != RT_NULL);

    /* 创建内核设备对象，附加空间存放驱动私有数据 */
    dev = rt_device_create(RT_Device_Class_Char, sizeof(struct test_dev));
    if (dev == RT_NULL)
    {
        LOG_E("create failed: no memory");
        return -RT_ENOMEM;
    }

    /* 初始化私有数据（模拟"探测硬件"的过程） */
    tdev = (test_dev_t)(dev + 1);
    rt_memset(tdev, 0, sizeof(struct test_dev));
    dev->user_data = tdev;

    tdev->buf = rt_malloc(TEST_DEV_BUF_SIZE);
    if (tdev->buf == RT_NULL)
    {
        LOG_E("alloc buf failed: no memory");
        rt_device_destroy(dev);
        return -RT_ENOMEM;
    }
    rt_memset(tdev->buf, 0, TEST_DEV_BUF_SIZE);
    tdev->buf_size = TEST_DEV_BUF_SIZE;
    tdev->version  = 0x01;
    tdev->mode     = TEST_DEV_MODE_NORMAL;

    tdev->lock = rt_mutex_create(name, RT_IPC_FLAG_PRIO);
    if (tdev->lock == RT_NULL)
    {
        LOG_E("create mutex failed");
        rt_free(tdev->buf);
        rt_device_destroy(dev);
        return -RT_ERROR;
    }

    /* 挂接统一 ops */
    dev->init    = test_dev_init;
    dev->open    = test_dev_open;
    dev->close   = test_dev_close;
    dev->read    = test_dev_read;
    dev->write   = test_dev_write;
    dev->control = test_dev_control;

    /* 注册进内核设备模型。
     *    STANDALONE：同一时刻只允许一个应用打开 */
    if (rt_device_register(dev, name, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE) != RT_EOK)
    {
        LOG_E("register failed");
        rt_mutex_delete(tdev->lock);
        rt_free(tdev->buf);
        rt_device_destroy(dev);
        return -RT_ERROR;
    }

    LOG_I("register: name=%s buf=%u bytes", name, tdev->buf_size);

    return RT_EOK;
}

/**
 * @brief 注销一个 test_dev 虚拟设备实例
 *
 * @param dev rt_device_find 得到的设备句柄
 */
void rt_test_dev_unregister(rt_device_t dev)
{
    test_dev_t tdev;

    RT_ASSERT(dev != RT_NULL);
    tdev = _TEST_DEV(dev);

    rt_device_unregister(dev);

    rt_mutex_delete(tdev->lock);
    rt_free(tdev->buf);
    rt_device_destroy(dev);

    LOG_I("unregister done");
}

/* ============================ 设备实例 ============================ */

/* 自动注册一个名为 "test_dev" 的设备实例（设备初始化阶段执行） */
static int test_dev_auto_register(void)
{
    return rt_test_dev_register(TEST_DEV_NAME);
}
INIT_DEVICE_EXPORT(test_dev_auto_register);
