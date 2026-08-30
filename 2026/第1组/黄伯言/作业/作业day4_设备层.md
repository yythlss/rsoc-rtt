# 设备层设计

## 总体设计思想

  总体的设计思想是，要提供设备管理层基本的api。这个设计理念借鉴于linux系统的****“一切皆文件”****思想，极大的简化了设计的难度，从繁杂的设计中抽象出只需要设计“打开”“关闭”“读”“写”四个api，从而实现了硬件与软件的解耦。

**但是**，rt thread 为了满足广大工程师的开发，又在设备管理层中分出了io层和"外设通信层"。**即实现了常用的spi和i2c片间通信总线的实现，使得不用重复开发，提升开发效率。**

****可以理解为，这些实现是从io基类派生出来的具体的对象的实现；同时这些实现又是通过芯片硬件厂商提供的库函数进行实现****，由此硬件区别在这一层体现。

同时rt thread 还支持自定义设备。将设备分为块设备和字节设备，主要区分依据是读写的顺序的区别。



所以在本次实验中，仅仅实现基础的设备管理层函数，即 打开、读、写、关闭四个功能。

再往硬件层走，就是具体的io驱动。操作系统给io口提供触发方式和输出驱动方式函数，具体也是对硬件厂商的驱动库进行了包装，保证兼容操作系统的设计逻辑。



## 核心文件组成

***test_dev.h***：定义设备控制命令（如 `TEST_DEV_CMD_RESET`），声明初始化函数。

***test_dev.c***：实现 `rt_device_ops` 中的 `init/open/close/read/write/control` 函数，完成设备的创建与注册。

***test_dev_cmd.c***：导出 `test` 命令，提供在终端中打开、读写、控制设备的测试能力。

（因为版本较老，所以采用

## 核心文件内容

***test_dev.h***

```
#ifndef __TEST_DEV_H__
#define __TEST_DEV_H__

#include <rtthread.h>

/* 设备控制命令 */
#define TEST_DEV_CMD_RESET      0x01    /* 复位设备 */
#define TEST_DEV_CMD_GET_STATUS 0x02    /* 获取设备状态 */

/* 对外声明：驱动初始化函数 */
int rt_hw_test_dev_init(void);

#endif
```

***test_dev.c*** 

```
#include <rtthread.h>
#include <rtdevice.h>
#include "test_dev.h"

/* ========== 设备私有数据结构 ========== */
struct test_device
{
    struct rt_device parent;
    rt_uint32_t status;
    rt_uint8_t buffer[64];
    rt_size_t buffer_len;
};

static struct test_device test_dev;

/* ========== ops 函数实现 ========== */

static rt_err_t _test_init(rt_device_t dev)
{
    struct test_device *tdev = (struct test_device *)dev;
    tdev->status = 0x01;
    tdev->buffer_len = 0;
    rt_kprintf("test_dev: init, status=0x%02X\n", tdev->status);
    return RT_EOK;
}

static rt_err_t _test_open(rt_device_t dev, rt_uint16_t oflag)
{
    struct test_device *tdev = (struct test_device *)dev;
    rt_kprintf("test_dev: open, oflag=0x%04X\n", oflag);
    if (tdev->status == 0)
    {
        return -RT_EBUSY;
    }
    return RT_EOK;
}

static rt_err_t _test_close(rt_device_t dev)
{
    rt_kprintf("test_dev: close\n");
    return RT_EOK;
}

static rt_size_t _test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct test_device *tdev = (struct test_device *)dev;
    rt_size_t read_size = 0;

    if (pos >= tdev->buffer_len)
    {
        return 0;
    }

    read_size = (size < (tdev->buffer_len - pos)) ? size : (tdev->buffer_len - pos);
    rt_memcpy(buffer, &tdev->buffer[pos], read_size);

    rt_kprintf("test_dev: read, pos=%d, size=%d, actual=%d\n", (int)pos, (int)size, (int)read_size);
    return read_size;
}

static rt_size_t _test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct test_device *tdev = (struct test_device *)dev;
    rt_size_t write_size;

    if (pos >= sizeof(tdev->buffer))
    {
        return 0;
    }

    write_size = (size < (sizeof(tdev->buffer) - pos)) ? size : (sizeof(tdev->buffer) - pos);
    rt_memcpy(&tdev->buffer[pos], buffer, write_size);

    if (pos + write_size > tdev->buffer_len)
    {
        tdev->buffer_len = pos + write_size;
    }

    rt_kprintf("test_dev: write, pos=%d, size=%d, actual=%d\n", (int)pos, (int)size, (int)write_size);
    return write_size;
}

static rt_err_t _test_control(rt_device_t dev, int cmd, void *args)
{
    struct test_device *tdev = (struct test_device *)dev;

    switch (cmd)
    {
    case TEST_DEV_CMD_RESET:
        tdev->buffer_len = 0;
        rt_memset(tdev->buffer, 0, sizeof(tdev->buffer));
        rt_kprintf("test_dev: control, CMD_RESET\n");
        break;

    case TEST_DEV_CMD_GET_STATUS:
        if (args != RT_NULL)
        {
            *(rt_uint32_t *)args = tdev->status;
        }
        rt_kprintf("test_dev: control, CMD_GET_STATUS, status=0x%02X\n", tdev->status);
        break;

    default:
        rt_kprintf("test_dev: control, unknown cmd=0x%02X\n", cmd);
        return -RT_EINVAL;
    }

    return RT_EOK;
}

/* ========== ops 结构体定义 ========== */
static const struct rt_device_ops test_ops = {
    _test_init,
    _test_open,
    _test_close,
    _test_read,
    _test_write,
    _test_control
};

/* ========== 驱动注册函数 ========== */
int rt_hw_test_dev_init(void)
{
    rt_device_t dev = (rt_device_t)&test_dev;

    /* 1. 设置设备类型 */
    dev->type = RT_Device_Class_Char;

    /* 2. 将 ops 指针指向 test_ops（这是 RT_USING_DEVICE_OPS 模式下的唯一方式） */
    dev->ops = &test_ops;

    /* 3. 注册设备到系统 */
    if (rt_device_register(dev, "test_dev", RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("test_dev: register failed!\n");
        return -1;
    }

    rt_kprintf("test_dev: registered successfully!\n");
    return 0;
}

INIT_DEVICE_EXPORT(rt_hw_test_dev_init);
```

***test_dev_cmd.c***

```
#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>     /* ← 添加：提供 atoi() 函数声明 */
#include "test_dev.h"

static rt_device_t test_dev_handle = RT_NULL;

/* ============================================================
 * 测试命令: test_open
 * ============================================================ */
static void cmd_test_open(int argc, char **argv)
{
    if (test_dev_handle == RT_NULL)
    {
        test_dev_handle = rt_device_find("test_dev");
        if (test_dev_handle == RT_NULL)
        {
            rt_kprintf("test_dev: device not found!\n");
            return;
        }
    }

    if (rt_device_open(test_dev_handle, RT_DEVICE_OFLAG_RDWR) == RT_EOK)
    {
        rt_kprintf("test_dev: open success\n");
    }
    else
    {
        rt_kprintf("test_dev: open failed\n");
    }
}
MSH_CMD_EXPORT(cmd_test_open, open test_dev);

/* ============================================================
 * 测试命令: test_close
 * ============================================================ */
static void cmd_test_close(int argc, char **argv)
{
    if (test_dev_handle == RT_NULL)
    {
        rt_kprintf("test_dev: device not opened\n");
        return;
    }

    rt_device_close(test_dev_handle);
    rt_kprintf("test_dev: closed\n");
}
MSH_CMD_EXPORT(cmd_test_close, close test_dev);

/* ============================================================
 * 测试命令: test_write
 * ============================================================ */
static void cmd_test_write(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("Usage: test_write <data>\n");
        return;
    }

    if (test_dev_handle == RT_NULL)
    {
        rt_kprintf("test_dev: device not opened\n");
        return;
    }

    rt_size_t len = rt_strlen(argv[1]);
    /* 修复4：rt_ssize_t → rt_size_t */
    rt_size_t written = rt_device_write(test_dev_handle, 0, argv[1], len);
    rt_kprintf("test_dev: write %d bytes\n", (int)written);
}
MSH_CMD_EXPORT(cmd_test_write, write data to test_dev);

/* ============================================================
 * 测试命令: test_read
 * ============================================================ */
static void cmd_test_read(int argc, char **argv)
{
    rt_size_t read_len = 64;
    rt_uint8_t buffer[64] = {0};

    if (argc >= 2)
    {
        read_len = atoi(argv[1]);   /* 现在 atoi 有声明了 */
        if (read_len > sizeof(buffer))
        {
            read_len = sizeof(buffer);
        }
    }

    if (test_dev_handle == RT_NULL)
    {
        rt_kprintf("test_dev: device not opened\n");
        return;
    }

    /* 修复5：rt_ssize_t → rt_size_t */
    rt_size_t readed = rt_device_read(test_dev_handle, 0, buffer, read_len);
    if (readed > 0)
    {
        rt_kprintf("test_dev: read %d bytes: %.*s\n", (int)readed, (int)readed, buffer);
    }
    else
    {
        rt_kprintf("test_dev: read %d bytes (no data)\n", (int)readed);
    }
}
MSH_CMD_EXPORT(cmd_test_read, read data from test_dev);

/* ============================================================
 * 测试命令: test_ctrl
 * ============================================================ */
static void cmd_test_ctrl(int argc, char **argv)
{
    rt_uint32_t status = 0;

    if (argc < 2)
    {
        rt_kprintf("Usage: test_ctrl <cmd>\n");
        rt_kprintf("  cmd 1: reset\n");
        rt_kprintf("  cmd 2: get status\n");
        return;
    }

    if (test_dev_handle == RT_NULL)
    {
        rt_kprintf("test_dev: device not opened\n");
        return;
    }

    int cmd = atoi(argv[1]);
    switch (cmd)
    {
    case 1:
        rt_device_control(test_dev_handle, TEST_DEV_CMD_RESET, RT_NULL);
        break;

    case 2:
        rt_device_control(test_dev_handle, TEST_DEV_CMD_GET_STATUS, &status);
        rt_kprintf("test_dev: status = 0x%08X\n", (unsigned int)status);
        break;

    default:
        rt_kprintf("test_dev: unknown cmd %d\n", cmd);
        break;
    }
}
MSH_CMD_EXPORT(cmd_test_ctrl, control test_dev: test_ctrl <cmd>);

```



## 命令行测试

| 命令                 | 功能            | 预期输出                                                                                |
|:------------------ |:------------- |:----------------------------------------------------------------------------------- |
| `test_open`        | 打开设备          | `test_dev: open, oflag=0x0003`<br>`test_dev: open success`                          |
| `test_write hello` | 写入字符串 `hello` | `test_dev: write, pos=0, size=5, actual=5`<br>`test_dev: write 5 bytes`             |
| `test_read 10`     | 读取 10 字节      | `test_dev: read, pos=0, size=10, actual=5`<br>`test_dev: read 5 bytes: hello`       |
| `test_write world` | 再次写入 `world`  | `test_dev: write, pos=0, size=5, actual=5`<br>`test_dev: write 5 bytes`             |
| `test_read 20`     | 读取 20 字节      | `test_dev: read 10 bytes: world`（覆盖写入）                                              |
| `test_ctrl 1`      | 复位设备          | `test_dev: control, CMD_RESET`<br>缓冲区清空                                             |
| `test_read 20`     | 复位后读取         | `test_dev: read 0 bytes (no data)`                                                  |
| `test_ctrl 2`      | 获取状态          | `test_dev: control, CMD_GET_STATUS, status=0x01`<br>`test_dev: status = 0x00000001` |
| `test_close`       | 关闭设备          | `test_dev: close`<br>`test_dev: closed`                                             |

##输出

```
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 21 2026 00:48:51
 2006 - 2022 Copyright by RT-Thread team
test_dev: registered successfully!
msh >test_open
test_open: command not found.
msh >cmd_test_open
test_dev: init, status=0x01
test_dev: open, oflag=0x0003
test_dev: open success
msh >cmd_test_write hello
test_dev: write, pos=0, size=5, actual=5
test_dev: write 5 bytes
msh >test read 10
test: command not found.
msh >cmd_test_read 10
test_dev: read, pos=0, size=10, actual=5
test_dev: read 5 bytes: hello
msh >cmd_test_write world
test_dev: write, pos=0, size=5, actual=5
test_dev: write 5 bytes
msh >cmd_test_read 10
test_dev: read, pos=0, size=10, actual=5
test_dev: read 5 bytes: world
msh >cmd_test_read 20
test_dev: read, pos=0, size=20, actual=5
test_dev: read 5 bytes: world
msh >cmd_test_ctrl 1
test_dev: control, CMD_RESET
msh >cmd_test_read 20
test_dev: read 0 bytes (no data)
msh >cmd_test_ctrl 2
test_dev: control, CMD_GET_STATUS, status=0x01
test_dev: status = 0x00000001
msh >cmd_test_close
test_dev: close
test_dev: closed
```


