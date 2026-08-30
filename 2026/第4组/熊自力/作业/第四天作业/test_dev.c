/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     熊自力       first version: Day4 课后作业 test_dev 虚拟字符设备驱动
   *   说明：
   *   虚拟字符设备驱动:
 *     1) 线性缓冲区 → 环形缓冲 FIFO（模拟真实串口/FIFO 的读写指针语义，判空/判满）；
 *     2) 引用计数补全：open 加一、close 减一，计数归零才真正"关闭/释放"，open 每次进入驱动 open 并 ref_count++，但是close 只在 ref_count 归零那一次调用驱动 close，因此多开多关会出现open_count（TEST_DEV_CTRL_GET_STATUS）不归零的现象；
 *     3) 打开模式校验：open 时校验请求的打开模式与设备能力是否匹配，不匹配拒绝。
  *   编译通过后设备在系统初始化阶段自动注册，可用 msh 命令 test_dev_app 测试，test_dev_ref测试多开多关。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#define LOG_TAG     "drv.test_dev"
#define LOG_LVL     LOG_LVL_DBG
#include <ulog.h>

/* ========== 1. 私有数据结构 ========== */

#define TEST_DEV_BUF_SIZE   128u    /* 环形缓冲区大小（字节） */

/* 自定义 control 命令（用户自定义区间） */
#define TEST_DEV_CTRL_CLEAR       0x01   /* 清空内部缓冲区 */
#define TEST_DEV_CTRL_GET_STATUS  0x02   /* 查询设备状态 */

/* 虚拟设备的私有数据：环形缓冲区 + 读写指针 + 统计信息 */
struct test_dev_data
{
    rt_uint8_t  buffer[TEST_DEV_BUF_SIZE];  /* 环形缓冲区 */
    rt_uint16_t rd_pos;                     /* 读指针（指向下一个待读位置） */
    rt_uint16_t wr_pos;                     /* 写指针（指向下一个待写位置） */
    rt_uint32_t open_count;                 /* 当前打开次数（引用计数） */
    rt_uint32_t read_bytes;                 /* 累计读字节数 */
    rt_uint32_t write_bytes;                /* 累计写字节数 */
    rt_bool_t   initialized;                /* 初始化标志 */
};

static struct test_dev_data test_dev_info;

/* ========== 2. 环形缓冲辅助函数 ========== */

/*
 * test_dev_empty：判断环形缓冲是否为空
  * 空条件：读指针 == 写指针。此时没有数据可读。
 */
static rt_bool_t test_dev_empty(struct test_dev_data *info)
{
    return (info->rd_pos == info->wr_pos);
}

/*
 * test_dev_full：判断环形缓冲是否已满
  * 满条件：(写指针 + 1) % SIZE == 读指针。留一格空位用于区分"空"和"满"。
 */
static rt_bool_t test_dev_full(struct test_dev_data *info)
{
    return (((info->wr_pos + 1) % TEST_DEV_BUF_SIZE) == info->rd_pos);
}

/*
 * test_dev_readable：计算当前可读字节数
 * 公式：(写指针 - 读指针 + SIZE) % SIZE，环形差值的标准写法。
 */
static rt_size_t test_dev_readable(struct test_dev_data *info)
{
    return (info->wr_pos + TEST_DEV_BUF_SIZE - info->rd_pos) % TEST_DEV_BUF_SIZE;
}

/*
 * test_dev_writable：计算当前可写字节数
 * 公式：SIZE - 1 - 可读量（因为必须留一格用于判空/满，所以实际容量是 SIZE-1）。
 */
static rt_size_t test_dev_writable(struct test_dev_data *info)
{
    return TEST_DEV_BUF_SIZE - 1 - test_dev_readable(info);
}

/* ========== 3. ops 实现 ========== */

/*
 * test_dev_init：设备初始化
 * 挂接私有数据、清空环形缓冲、复位指针与统计、置初始化标志。
 */
static rt_err_t test_dev_init(rt_device_t dev)
{
    struct test_dev_data *info = (struct test_dev_data *)dev->user_data;

    /* 若 user_data 尚未挂接，则挂到全局私有数据 */
    if (info == RT_NULL)
    {
        info = &test_dev_info;
        dev->user_data = (void *)info;
    }

    memset(info->buffer, 0, sizeof(info->buffer));
    info->rd_pos        = 0;
    info->wr_pos        = 0;
    info->open_count    = 0;
    info->read_bytes    = 0;
    info->write_bytes   = 0;
    info->initialized   = RT_TRUE;

    LOG_I("test_dev init ok, ring buffer size = %d (usable %d)",
          TEST_DEV_BUF_SIZE, TEST_DEV_BUF_SIZE - 1);
    return RT_EOK;
}

/*
 * test_dev_open：打开设备（引用计数 + 模式校验）
 * ① 校验是否已初始化；② 校验打开模式与设备能力是否匹配；
 * ③ open_count++ 演示引用计数语义。
 */
static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    struct test_dev_data *info = (struct test_dev_data *)dev->user_data;
    rt_uint16_t req_mode = oflag & RT_DEVICE_OFLAG_MASK;   /* 取出请求的打开模式位 */
    rt_uint16_t dev_cap  = dev->flag & RT_DEVICE_OFLAG_MASK; /* 取出设备能力位 */

    if (info == RT_NULL || !info->initialized)
    {
        LOG_E("test_dev not initialized, open failed");
        return -RT_ERROR;
    }

    /* 模式校验：请求写但设备不支持写，或请求读但设备不支持读 → 拒绝 */
    if ((req_mode == RT_DEVICE_OFLAG_WRONLY) &&
        !(dev_cap & (RT_DEVICE_FLAG_WRONLY | RT_DEVICE_FLAG_RDWR)))
    {
        LOG_E("test_dev open failed: device not writable, oflag=0x%x", oflag);
        return -RT_EINVAL;
    }
    if ((req_mode == RT_DEVICE_OFLAG_RDONLY) &&
        !(dev_cap & (RT_DEVICE_FLAG_RDONLY | RT_DEVICE_FLAG_RDWR)))
    {
        LOG_E("test_dev open failed: device not readable, oflag=0x%x", oflag);
        return -RT_EINVAL;
    }

    info->open_count++;   /* 引用计数 +1 */
    LOG_I("test_dev open, oflag = 0x%x, open_count = %d", oflag, info->open_count);
    return RT_EOK;
}

/*
 * test_dev_close：关闭设备（引用计数 -1，归零才真正关闭）
 * 归零时打印"真正关闭"并复位内部状态，模拟真实驱动的断电/释放动作。
 */
static rt_err_t test_dev_close(rt_device_t dev)
{
    struct test_dev_data *info = (struct test_dev_data *)dev->user_data;

    if (info == RT_NULL)
    {
        return -RT_ERROR;
    }

    if (info->open_count > 0)
    {
        info->open_count--;
    }

    if (info->open_count == 0)
    {
        /* 计数归零：真正关闭，复位缓冲 */
        memset(info->buffer, 0, sizeof(info->buffer));
        info->rd_pos = 0;
        info->wr_pos = 0;
        LOG_I("test_dev really closed (open_count=0), buffer reset");
    }
    else
    {
        LOG_I("test_dev close, open_count = %d (still opened)", info->open_count);
    }
    return RT_EOK;
}

/*
 * test_dev_read：从环形缓冲读取数据（FIFO 语义，忽略 pos）
 * 一次最多读取当前可读量，返回实际读出的字节数。
 */
static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_dev_data *info = (struct test_dev_data *)dev->user_data;
    rt_size_t count = 0;

    if (info == RT_NULL || buffer == RT_NULL || size == 0)
    {
        LOG_E("test_dev read: invalid param");
        return -RT_EINVAL;
    }

    /* 空缓冲：无可读数据，返回 0 */
    if (test_dev_empty(info))
    {
        LOG_I("test_dev read: ring empty, return 0");
        return 0;
    }

    /* 可读量不足时按可读量截断 */
    count = test_dev_readable(info);
    if (size < count)
    {
        count = size;
    }

    /* 分两段拷贝：从 rd_pos 到缓冲区末尾，再到头部（环形回绕处理） */
    {
        rt_size_t first = TEST_DEV_BUF_SIZE - info->rd_pos;   /* 第一段长度：到环尾 */
        if (first > count)
        {
            first = count;
        }
        rt_memcpy(buffer, &info->buffer[info->rd_pos], first);

        if (count > first)   /* 第二段：从环头开始读剩下的 */
        {
            rt_memcpy((rt_uint8_t *)buffer + first,
                      &info->buffer[0], count - first);
        }
    }

    info->rd_pos = (rt_uint16_t)((info->rd_pos + count) % TEST_DEV_BUF_SIZE);
    info->read_bytes += count;

    LOG_I("test_dev read %d bytes, rd_pos=%d", count, info->rd_pos);
    return (rt_size_t)count;
}

/*
 * test_dev_write：向环形缓冲写入数据（FIFO 语义，忽略 pos）
 * 缓冲满时按可写量截断写入，返回实际写入的字节数。
 */
static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_dev_data *info = (struct test_dev_data *)dev->user_data;
    rt_size_t count = 0;

    if (info == RT_NULL || buffer == RT_NULL || size == 0)
    {
        LOG_E("test_dev write: invalid param");
        return -RT_EINVAL;
    }

    /* 满缓冲：无可写空间，返回 0 */
    if (test_dev_full(info))
    {
        LOG_I("test_dev write: ring full, return 0");
        return 0;
    }

    /* 可写量不足时按可写量截断 */
    count = test_dev_writable(info);
    if (size < count)
    {
        count = size;
    }

    /* 分两段写入（环形回绕处理） */
    {
        rt_size_t first = TEST_DEV_BUF_SIZE - info->wr_pos;   /* 第一段长度：到环尾 */
        if (first > count)
        {
            first = count;
        }
        rt_memcpy(&info->buffer[info->wr_pos], buffer, first);

        if (count > first)   /* 第二段：从环头开始写剩下的 */
        {
            rt_memcpy(&info->buffer[0],
                      (const rt_uint8_t *)buffer + first, count - first);
        }
    }

    info->wr_pos = (rt_uint16_t)((info->wr_pos + count) % TEST_DEV_BUF_SIZE);
    info->write_bytes += count;

    LOG_I("test_dev write %d bytes, wr_pos=%d", count, info->wr_pos);
    return (rt_size_t)count;
}

/*
 * test_dev_control：设备控制（自定义命令分发）
 * 支持：清空缓冲区 / 查询状态（含空/满/可读可写量）。
 */
static rt_err_t test_dev_control(rt_device_t dev, int cmd, void *args)
{
    struct test_dev_data *info = (struct test_dev_data *)dev->user_data;

    if (info == RT_NULL)
    {
        LOG_E("test_dev control: invalid device");
        return -RT_ERROR;
    }

    switch (cmd)
    {
    case TEST_DEV_CTRL_CLEAR:
        memset(info->buffer, 0, sizeof(info->buffer));
        info->rd_pos = 0;
        info->wr_pos = 0;
        LOG_I("test_dev buffer cleared");
        break;

    case TEST_DEV_CTRL_GET_STATUS:
        LOG_I("test_dev status: initialized=%d empty=%d full=%d "
              "rd=%d wr=%d readable=%d writable=%d open_count=%d read=%d write=%d",
              info->initialized,
              test_dev_empty(info), test_dev_full(info),
              info->rd_pos, info->wr_pos,
              test_dev_readable(info), test_dev_writable(info),
              info->open_count, info->read_bytes, info->write_bytes);
        break;

    default:
        LOG_E("test_dev unknown control cmd: 0x%x", cmd);
        return -RT_EINVAL;
    }

    return RT_EOK;
}

/* ========== 4. 驱动注册 ========== */

/*
 * test_dev_drv_init：创建设备并注册到 IO 设备管理器
 * 通过 INIT_DEVICE_EXPORT 在系统启动时自动执行。
 */
static int test_dev_drv_init(void)
{
    rt_device_t dev = rt_device_create(RT_Device_Class_Char, 0);
    if (dev == RT_NULL)
    {
        LOG_E("test_dev create failed");
        return -RT_ERROR;
    }

    /* 挂接私有数据 */
    dev->user_data = (void *)&test_dev_info;

    /* 对接六个 ops */
    dev->init    = test_dev_init;
    dev->open    = test_dev_open;
    dev->close   = test_dev_close;
    dev->read    = test_dev_read;
    dev->write   = test_dev_write;
    dev->control = test_dev_control;

    if (rt_device_register(dev, "test_dev", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("test_dev register failed");
        return -RT_ERROR;
    }

    LOG_I("test_dev driver registered, please run `test_dev_app` in msh");
    return RT_EOK;
}
INIT_DEVICE_EXPORT(test_dev_drv_init);

/* ========== 5. msh 测试命令 ========== */

/*
 * test_dev_app：演示完整生命周期 find → open → write → read → control → close
 * 并额外演示：引用计数（多次 open/close）、环形回绕（写入超容量数据）、满/空状态。
 * msh 下输入：test_dev_app
 */
static void test_dev_app(void)
{
    rt_device_t dev = RT_NULL;
    char wbuf[64] = "Hello RT-Thread!";
    char rbuf[64] = {0};
    rt_size_t n = 0;

    /* 1. 查找设备 */
    dev = rt_device_find("test_dev");
    if (dev == RT_NULL)
    {
        LOG_E("can not find test_dev");
        return;
    }

    /* 2. 打开设备（open 自动触发 init） */
    if (rt_device_open(dev, RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        LOG_E("test_dev open failed");
        return;
    }
    LOG_I("--- after open x1, open_count should be 1 ---");
    rt_device_control(dev, TEST_DEV_CTRL_GET_STATUS, RT_NULL);

    /* 3. 写入数据到环形缓冲 */
    n = rt_device_write(dev, 0, wbuf, (rt_size_t)rt_strlen(wbuf));
    LOG_I("write %d bytes", n);

    /* 4. 从环形缓冲读回数据（FIFO 顺序） */
    n = rt_device_read(dev, 0, rbuf, sizeof(rbuf) - 1);
    if (n > 0)
    {
        rbuf[n] = '\0';
        LOG_I("read back: %s", rbuf);
    }

    /* 5. 演示环形回绕：连续写入 200 字节（超过 127 可用容量，应被截断） */
    {
        char big[200];
        memset(big, 'A', sizeof(big));
        n = rt_device_write(dev, 0, big, sizeof(big));
        LOG_I("write 200 bytes request, actually stored %d bytes (ring wrap test)", n);
    }
    LOG_I("--- after ring wrap write, should be full or near full ---");
    rt_device_control(dev, TEST_DEV_CTRL_GET_STATUS, RT_NULL);

    /* 6. 清空缓冲 */
    rt_device_control(dev, TEST_DEV_CTRL_CLEAR, RT_NULL);
    rt_device_control(dev, TEST_DEV_CTRL_GET_STATUS, RT_NULL);

    /* 7. 关闭设备：单开单关下系统 ref 1→0，驱动 close 被调用 → open_count 1→0，真正关闭并复位缓冲（打印 "really closed"） */
    rt_device_close(dev);
    LOG_I("--- after close x1, open_count should be 0 (really closed) ---");
    rt_device_control(dev, TEST_DEV_CTRL_GET_STATUS, RT_NULL);

    LOG_I("test_dev_app done");
}
MSH_CMD_EXPORT(test_dev_app, run test_dev virtual device demo);



/* ========== 5.1 引用计数专用演示命令 ========== */

/*
 * test_dev_ref：演示 RT-Thread 4.x 引用计数的"不对称"语义。
  * 现象（注意系统 ref_count 与驱动 open_count 并不严格配对）：
 *   - open x3：驱动 open 被调用 3 次（open_count 1→2→3），系统 ref 0→3
 *   - close #1 / #2：系统 ref 3→1，未归零，驱动 close 不被调用（无日志）
 *   - close #3：系统 ref 1→0，驱动 close 被调用 1 次，open_count 3→2
  * 结论：驱动 open 每次都被调用，而驱动 close 只在系统引用计数归零那一刻被调用一次。
 */
static void test_dev_ref(void)
{
    rt_device_t dev = RT_NULL;

    dev = rt_device_find("test_dev");
    if (dev == RT_NULL)
    {
        LOG_E("can not find test_dev");
        return;
    }

    /* 连续打开三次：每次都会进入驱动 open，open_count 1→2→3 */
    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    LOG_I("--- open x3: driver open called 3 times, open_count = 3 ---");
    rt_device_control(dev, TEST_DEV_CTRL_GET_STATUS, RT_NULL);

    /* 连续关闭三次：只有最后一次（ref 归零）才会调用驱动 close */
    rt_device_close(dev);   /* ref 3→2，未归零，驱动 close 不被调用 */
    LOG_I("--- close #1: ref 3->2, driver close NOT called ---");
    rt_device_close(dev);   /* ref 2→1，未归零，驱动 close 不被调用 */
    LOG_I("--- close #2: ref 2->1, driver close NOT called ---");
    rt_device_close(dev);   /* ref 1→0，归零，驱动 close 被调用一次，open_count 3→2 */
    LOG_I("--- close #3: ref 1->0, driver close called once, open_count 3->2 ---");
    rt_device_control(dev, TEST_DEV_CTRL_GET_STATUS, RT_NULL);

    LOG_I("test_dev_ref done");
}
MSH_CMD_EXPORT(test_dev_ref, run test_dev reference count demo);
