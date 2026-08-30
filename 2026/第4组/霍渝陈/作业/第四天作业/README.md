# 第四天作业：虚拟设备驱动框架层（test_dev）

> 目标平台：STM32F407 星火号（stm32f407-rtspark）· RT-Thread 4.1.1 · Studio 2.3.1 + ST-LINK（COM7/115200）
> 提交人：霍渝陈（第 4 组）
> 理论部分见《笔记/第四天笔记.md》，本目录为实现 + 实验报告。

## 一、实现内容

| 文件 | 作用 |
|---|---|
| `test_drv.c` | **驱动层**：虚拟字符设备 `test_dev`。`struct test_device`（继承 `rt_device` + 私有 `buf[64]`/`data_len`/`magic`）→ `rt_device_create` → 绑定 6 个 ops → `rt_device_register`；`INIT_DEVICE_EXPORT` 自动注册 |
| `test_dev.c` | **应用层**：MSH 命令 `test_dev_demo`，走通 find → open → write → read → control → close 完整生命周期 |

## 二、使用方法

1. 把 `test_drv.c`、`test_dev.c` 拷入工程 `applications/`，编译下载（无需任何外接硬件，纯内存模拟）；
2. 启动后串口出现注册日志：`[test_dev] driver registered: test_dev`，`list_device` 可见 `test_dev`；
3. msh 执行：`test_dev_demo`。

## 三、预期输出

```
msh />test_dev_demo

====== test_dev Demo ======
[1] find: OK -> test_dev
[test_dev] init ok
[test_dev] open, oflag=0x0003
[2] open: OK
[test_dev] write: pos=0 size=32 stored=32
[3] write: sent=32 returned=32
[test_dev] read: pos=0 size=64 copied=32
[4] read: got=32 bytes, data="Hello RT-Thread virtual device!"
[test_dev] control: GET_VERSION=v1.0.0
[5] control GET_VERSION: ret=0 version=v1.0.0
[test_dev] control: echo, data_len=32
[6] control ECHO: ret=0
[test_dev] control: buffer cleared
[7] control CLEAR_BUFFER: ret=0
[test_dev] read: buffer empty
[8] read after clear: got=0 bytes (expect 0)
[test_dev] close
[9] close: OK
====== test_dev Demo Done ======
```

## 四、设计要点与踩坑

1. **私有字段必须手动初始化**：`rt_device_create` 源码只清零基类，私有结构（buf/data_len/magic）是未初始化内存，`magic` 魔数校验就是为抓这个错误。
2. **init 是懒初始化**：注册时不调用，第一次 `rt_device_open` 时才被框架触发（`RT_DEVICE_FLAG_ACTIVATED` 标志）。
3. **自定义 control 命令避开内核保留值**：0x01~0x04（RESUME/SUSPEND/CONFIG/CLOSE）、0x10~0x12（SET/CLR/GET_INT）已占用，本作业从 0x20 起。
4. **ops 绑定兼容两种内核配置**：`#ifdef RT_USING_DEVICE_OPS` 区分 `dev->ops = &...` 与直接函数指针，换工程不用改。
5. **应用层零硬件接触**：`test_dev.c` 里没有任何寄存器/HAL 调用，数据只在内存缓冲区流转——「设备驱动分离」的直接体验。
