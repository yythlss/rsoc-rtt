/*
 * 虚拟设备驱动框架层 test_dev -- 对外接口
 *
 * 分层结构：
 *   ----------------------------------------------------------
 *           应用层   test_dev_sample.c
 *   rt_device_find/open/read/write/control/close
 *   ----------------------------------------------------------
 *            框架层   test_dev.c（本文件定义接口）                       |
 *            统一 ops 挂接 + 注册/注销接口 + 私有数据 + 线程安全 |
 *   ----------------------------------------------------------
 *           内核层   rt_device I/O 设备模型（rt_device_create/register）|
 *   ----------------------------------------------------------
 */

#ifndef __TEST_DEV_H__
#define __TEST_DEV_H__

#include <rtthread.h>
#include <rtdevice.h>

#define TEST_DEV_NAME            "test_dev"  /* 设备名，rt_device_find 使用      */
#define TEST_DEV_BUF_SIZE        256         /* 模拟存储容量（字节）            */

/* 自定义控制命令：内核占用 0x01~0x12，自定义命令参考内核做法，
 * 以设备类型为基地址偏移，避免与内核命令冲突 */
#define TEST_DEV_CMD_BASE        (RT_DEVICE_CTRL_BASE(Char) + 0x10)
#define TEST_DEV_CMD_CLEAR       (TEST_DEV_CMD_BASE + 0)   /* 清空模拟存储      */
#define TEST_DEV_CMD_GET_INFO    (TEST_DEV_CMD_BASE + 1)   /* 获取设备信息      */
#define TEST_DEV_CMD_SET_MODE    (TEST_DEV_CMD_BASE + 2)   /* 设置工作模式      */
#define TEST_DEV_CMD_GET_VERSION (TEST_DEV_CMD_BASE + 3)   /* 获取框架版本号    */

/* 设备工作模式 */
#define TEST_DEV_MODE_NORMAL     0   /* 普通模式：读写正常                     */
#define TEST_DEV_MODE_SILENT     1   /* 静默模式：write 直接丢弃数据           */

/* 设备信息，TEST_DEV_CMD_GET_INFO 命令的输出 */
struct test_dev_info
{
    rt_uint8_t  version;        /* 框架版本号 */
    rt_uint8_t  mode;           /* 当前工作模式 */
    rt_uint32_t buf_size;       /* 模拟存储容量 */
    rt_uint32_t open_count;     /* 累计打开次数 */
    rt_uint32_t read_count;     /* 累计读次数 */
    rt_uint32_t write_count;    /* 累计写次数 */
};

/* 框架层导出接口：注册/注销一个 test_dev 虚拟设备实例，支持多实例 */
rt_err_t rt_test_dev_register(const char *name);
void     rt_test_dev_unregister(rt_device_t dev);

#endif /* __TEST_DEV_H__ */
