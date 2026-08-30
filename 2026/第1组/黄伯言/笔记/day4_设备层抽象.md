# [DAY4]RT-Thread IO 设备驱动框架

## 总体设计思想

总体的设计思想是，要提供设备管理层基本的api。这个设计理念借鉴于linux系统的****“一切皆文件”****思想，极大的简化了设计的难度，从繁杂的设计中抽象出只需要设计“打开”“关闭”“读”“写”四个api，从而实现了硬件与软件的解耦。

**但是**，rt thread 为了满足广大工程师的开发，又在设备管理层中分出了io层和"外设通信层"。**即实现了常用的spi和i2c片间通信总线的实现，使得不用重复开发，提升开发效率。**

****可以理解为，这些实现是从io基类派生出来的具体的对象的实现；同时这些实现又是通过芯片硬件厂商提供的库函数进行实现****，由此硬件区别在这一层体现。

同时rt thread 还支持自定义设备。将设备分为块设备和字节设备，主要区分依据是读写的顺序的区别。

## 1. 设备驱动框架解决的问题

- **痛点**：不同厂商的 MCU 外设（如 SPI、I2C）操作 API 不同，导致代码移植困难、重复开发。
- **解决方案**：RT-Thread 提供了一套统一的 IO 设备驱动框架，将底层硬件驱动与上层应用分离。
- **优势**：
  - 提高代码复用率，应用层 API 统一。
  - 降低学习成本和开发周期。
  - 减少碎片化开发。
- **劣势**：
  - 驱动框架本身会增加代码的复杂度。
  - 会占用稍多的 Flash 空间。

## 2. 设备驱动框架核心设计

### 面向对象与继承

- RT-Thread 设备驱动采用面向对象设计。
- 所有具体设备（如 UART、I2C、SPI、PIN）都继承自基础的 `rt_device` 基类。

### 设备管理层

- 应用层通过统一的 `open`、`read`、`write`、`control` 接口操作设备，不直接接触硬件寄存器。
- 设备驱动框架层实现了具体的 `ops`（操作函数集），如 `spi_ops`、`i2c_ops`。
- 底层 BSP 驱动层负责对接具体的 MCU 硬件库（如 STM32 HAL 库），并填充 `ops` 结构体。

## 设备操作集：`ops`

`ops` 是 RT-Thread 设备驱动框架的核心。它是一个结构体，包含了一组操作硬件设备的函数指针，如 `init`、`open`、`close`、`read`、`write` 和 `control` 等。

应用层调用 `rt_device_read()` 这类通用 API 时，框架会通过设备对象的 `ops` 指针，最终调用到由具体驱动实现的硬件操作函数，从而实现了“应用层统一接口，底层各自实现”的解耦。

---

## 设备分类

RT-Thread 将设备划分为不同类型，以便进行统一管理和操作。

| 设备类型       | 特点                      | 举例            |
|:---------- |:----------------------- |:------------- |
| **字符设备**   | 顺序读写，以字节/字符流方式访问。       | 串口、键盘         |
| **块设备**    | 随机读写，以固定大小的块（如 512B）访问。 | SD 卡、Flash、硬盘 |
| **网络接口设备** | 用于网络通信。                 | 网卡            |
| **内存设备**   | 用于内存管理。                 | RAM 模拟的块设备    |

### 分类的意义

1. **支持日志重定向**：可将系统日志（`printf` 输出）重定向到任意字符设备，例如显示在 LCD 屏幕上。
2. **支持文件系统**：文件系统依赖于底层的块设备进行数据的存储与管理。

---

## IO 设备管理核心 API

| API 函数                                    | 功能                    | 关键参数 / 返回值                              |
|:----------------------------------------- |:--------------------- |:--------------------------------------- |
| `rt_device_create(type, size)`            | 动态创建一个设备对象。           | `type`: 设备类型（如 `RT_Device_Class_Char`）。 |
| `rt_device_register(dev, name, flags)`    | 将设备注册到 IO 设备管理器。      | `name`: 设备名称；`flags`: 打开模式（只读、读写、中断等）。  |
| `rt_device_find(name)`                    | 通过名称查找设备。             | 返回设备句柄，失败返回 `RT_NULL`。                  |
| `rt_device_init(dev)`                     | 初始化设备。                | 成功返回 `RT_EOK`。                          |
| `rt_device_open(dev, flags)`              | 以指定模式打开设备。            | `flags`: 如 `RT_DEVICE_OFLAG_RDWR`。      |
| `rt_device_read(dev, pos, buffer, size)`  | 从设备读取数据。              | `pos`: 对于块设备有效，表示偏移地址。                  |
| `rt_device_write(dev, pos, buffer, size)` | 向设备写入数据。              |                                         |
| `rt_device_control(dev, cmd, arg)`        | 控制设备，如设置串口波特率、SPI速率等。 | `cmd`: 具体命令；`arg`: 参数。                  |
| `rt_device_set_rx_indicate(dev, rx_cb)`   | 设置接收回调函数。             | 用于中断/DMA 方式接收数据。                        |
| `rt_device_set_tx_complete(dev, tx_cb)`   | 设置发送完成回调函数。           | 用于 DMA 方式发送完成通知。                        |
| `rt_device_close(dev)`                    | 关闭设备。                 |                                         |

---

## 重点实验：创建和注册一个字符设备

### 实验目标

1. 创建并注册一个名为 `test_dv` 的字符设备。
2. 在终端中使用 `list_device` 命令查看该设备。

### 核心代码示例

#### 课堂实验1：注册设备实验

static int test_device_init(void)
{
    rt_device_t test_dev = RT_NULL;

    /* 1. 创建字符设备 */
    test_dev = rt_device_create(RT_Device_Class_Char, 0);
    if (test_dev == RT_NULL)
    {
        rt_kprintf("test device create failed\n");
        return -1;
    }
    
    /* 2. 注册设备到 IO 管理器 */
    if (rt_device_register(test_dev, "test_dv", RT_DEVICE_OFLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("test device register failed\n");
        return -1;
    }
    
    rt_kprintf("test device registered successfully!\n");
    return 0;
    }
    
    /* 使用自动初始化机制，系统启动时自动执行 */
    INIT_APP_EXPORT(test_device_init);

输出结果展示：

![](C:/Users/HUANG/AppData/Roaming/marktext/images/2026-08-20-23-38-37-image.png)

可以看到，设备管理器中已经出现需要注册的设备，表明设备注册成功

![](C:/Users/HUANG/AppData/Roaming/marktext/images/2026-08-20-23-59-06-image.png)
