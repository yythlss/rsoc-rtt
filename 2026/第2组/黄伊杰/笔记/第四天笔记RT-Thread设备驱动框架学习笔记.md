# RT-Thread 设备驱动框架 学习笔记

> 基于《day4-驱动框架》培训材料整理，并参考 RT-Thread 官方文档进行完善
> 覆盖：I/O 设备框架概念、I/O API、GPIO 外设、I2C 外设、SPI 外设，以及设备驱动框架（DM）进阶

---

## 目录

- [培训1：RT-Thread I/O 设备框架概念](#培训1rt-thread-io-设备框架概念)
- [培训2：RT-Thread I/O API](#培训2rt-thread-io-api)
- [培训3：GPIO（PIN）外设开发](#培训3gpio-pin外设开发)
- [培训3：I2C 外设开发](#培训3i2c-外设开发)
- [培训4：SPI 外设开发](#培训4spi-外设开发)
- [进阶：设备驱动框架（Driver Model）](#进阶设备驱动框架driver-model)
- [总结](#总结)
- [参考资料](#参考资料)

---

## 培训1：RT-Thread I/O 设备框架概念

### 1.1 问题引入：驱动开发的碎片化

不同厂家的 `SPI` API 设计各不相同，例如：

| 厂家/芯片 | SPI 数据传输接口 |
|-----------|------------------|
| 兆易创新 GD | `spi_i2s_data_transmit` |
| ST | `HAL_SPI_Transmit` |
| NXP | `LPSPI_MasterTransferBlocking` |
| LPC | `SPI_MasterTransferBlocking` |

**常见开发痛点场景：**

- 项目A：`STM32 + SPI + W25Q128 + FATFS`
- 项目B：换芯片为 LPC（由于直接使用 SPI 编写 W25Q128，基本等于重写）
- 项目C：`LPC + SPI + RW007(WiFi) + Lwip`

**由此产生的疑问：**

> 这些工作能够复用吗？对团队效率高吗？

**两个核心问题：**
1. **驱动开发碎片化**：代码复用率低，都是非通用的驱动设备代码。
2. **学习成本高**：同一个工程师需要花时间学习不同厂家的 API 设计。

### 1.2 解决思路：驱动与设备分离 + 统一 API

问：不同厂家同一外设的开发逻辑和 API 是否相同？

核心思想——**驱动代码与设备驱动代码分离**：

```
    RW007设备驱动    W25Q128设备驱动
            \           /
             \         /
          ★ SPI 统一 API ★
            /         \
   STM32 SPI对接驱动   LPC SPI对接驱动
```

**分离后的优势：**
- 更换 MCU 只需改变对应的「对接驱动」。
- 重新驱动设备，只需重写设备驱动相关代码。
- 同一 API 接口，学习成本低。
- 设备驱动可以**入库**，供公司其他项目使用，减少碎片化开发、防止反复造轮子。
- 代码框架会变复杂，但从优点看是值得的。

### 1.3 框架演进 → 再演进

**第一版框架演进（引入 I/O 层）：**

```
应用层： RW007设备驱动 | W25Q128设备驱动 | CS43L22设备驱动 | SHT30设备驱动
                |             |               |               |
I/O 层：    SPI 统一 API  |               I2C 统一 API
                |             |               |               |
硬件对接： STM32 SPI | LPC SPI | STM32 I2C | LPC I2C（对接驱动）
```

- 第一层改进：把设备驱动与硬件都隔离开，中间定义统一 API。

**第二版框架再演进（拆成 BSP 与 Package）：**

- 应用层可做成 `Package`（软件包）——`RW007/W25Q128/…` 驱动做成软件包复用。
- I/O 层提供 **设备框架接口**（SPI 设备框架接口、I2C 设备框架接口）。
- 硬件层做成**特定平台的 BSP 驱动**（STM32 SPI、LPC SPI、STM32 I2C、LPC I2C…）。

### 1.4 RT-Thread I/O 设备模型框架（官方 I/O 设备介绍）

绝大部分嵌入式系统都包括一些 I/O（Input/Output，输入/输出）设备，例如：
- 仪器上的数据显示屏
- 工业设备上的串口通信
- 数据采集设备上用于保存数据的 Flash 或 SD 卡
- 网络设备的以太网接口

**官方三层模型：** RT-Thread 提供了一套简单的 I/O 设备模型框架，位于硬件和应用程序之间，从上到下分为三层：

```
┌───────────────────────────────┐
│        I/O 设备管理层         │  封装设备驱动，向上提供标准接口
├───────────────────────────────┤
│       设备驱动框架层          │  同一类设备驱动的抽象，抽取共性、留出接口
├───────────────────────────────┤
│        设备驱动层             │  创建并注册 I/O 设备，驱动具体硬件
└───────────────────────────────┘
```

- **I/O 设备管理层**：实现对设备驱动程序的封装。应用程序通过标准接口（如 `open/read/write/control/close`）访问底层设备，设备驱动程序的升级、更替不会影响上层应用，降低耦合与复杂性。
- **设备驱动框架层**：对同类硬件设备驱动的抽象，将不同厂家同类硬件驱动中相同的部分抽取出来，不同部分留出接口由驱动实现。
- **设备驱动层**：一组驱使硬件设备工作的程序，负责**创建和注册 I/O 设备**。

**两种注册/访问路径：**

1. **简单设备**（逻辑简单，不需经过驱动框架层）：设备驱动根据设备模型定义创建设备实例 → 通过 `rt_device_register()` 注册到 I/O 设备管理器 → 应用程序用 `rt_device_find()` 查找后通过 I/O 管理接口访问硬件。

2. **复杂设备**（如看门狗）：设备驱动先创建设备实例 → 注册到对应的**设备驱动框架**（如 `rt_hw_watchdog_register()`）→ 再由驱动框架通过 `rt_device_register()` 注册到 I/O 设备管理器 → 应用程序访问。

**头文件组织：**
- 具体驱动头文件逐步整理到 `drivers/dev_*.h` 路径下，如 `drivers/dev_spi.h`、`drivers/dev_can.h`、`drivers/dev_rtc.h`、`drivers/dev_pin.h`。
- 应用层仍建议包含聚合头文件 `#include <rtdevice.h>`，由它根据配置引入对应设备头文件。

### 1.5 I/O 设备模型与设备对象

RT-Thread 的设备模型建立在**内核对象模型**基础上，设备被视为一类对象纳入对象管理器，每个设备对象由基对象派生，可继承父类属性并派生出私有属性。

设备对象 `struct rt_device`（部分关键成员）：

```c
struct rt_device {
    struct rt_object parent;           /* 内核对象基类 */
    struct rt_bus            *bus;     /* 挂载的总线 (RT_USING_DM) */
    struct rt_driver         *drv;     /* 设备驱动 */
    enum rt_device_class_type type;    /* 设备类型 */
    rt_uint16_t flag;                  /* 设备参数 */
    rt_uint16_t open_flag;             /* 设备打开标志 */
    rt_uint8_t ref_count;              /* 设备被引用次数 */
    rt_uint8_t device_id;              /* 设备 ID, 0-255 */
    /* 数据收发回调函数 */
    rt_err_t (*rx_indicate)(rt_device_t dev, rt_size_t size);
    rt_err_t (*tx_complete)(rt_device_t dev, void *buffer);
    const struct rt_device_ops *ops;   /* 设备操作方法 */
    ...
    void *user_data;                   /* 设备私有数据 */
};
```

> 开启 `RT_USING_DM` 后，设备对象会额外记录电源域单元、主设备标识等；开启 `RT_USING_DMA` 时还会记录 DMA 操作表。

### 1.6 I/O 支持的设备类型（完整枚举）

| 类型宏 | 说明 |
|--------|------|
| `RT_Device_Class_Char` | 字符设备 |
| `RT_Device_Class_Block` | 块设备 |
| `RT_Device_Class_NetIf` | 网络接口设备 |
| `RT_Device_Class_MTD` | 内存设备 |
| `RT_Device_Class_RTC` | RTC 设备 |
| `RT_Device_Class_Sound` | 声音设备（音频编解码 CS43L22、录音驱动） |
| `RT_Device_Class_Graphic` | 图形设备（LCD 屏幕） |
| `RT_Device_Class_I2CBUS` | I2C 总线设备 |
| `RT_Device_Class_USBDevice` | USB device 设备 |
| `RT_Device_Class_USBHost` | USB host 设备 |
| `RT_Device_Class_SPIBUS` | SPI 总线设备 |
| `RT_Device_Class_SPIDevice` | SPI 设备 |
| `RT_Device_Class_SDIO` | SDIO 设备 |
| `RT_Device_Class_Miscellaneous` | 杂类设备 |
| `RT_Device_Class_Pin` | GPIO（PIN）设备 |

（完整定义见 `rt-thread/include/rtdef.h` 的 `rt_device_class_type` 枚举。）

### 1.7 字符设备与块设备

**字符设备特点：**
- 提供连续的数据流，应用程序顺序读取，通常不支持随机存取。
- 支持按字节/字符读写。
- 典型例子：键盘、串口、Modem。
- 字符模式设备允许非结构化的数据传输（串行形式，每次一个字节）。

**块设备特点：**
- 应用程序可随机访问，程序自行确定读取位置。
- 数据的读写只能以块为单位（通常是 512B 的倍数）。
- 典型例子：硬盘、软盘、CD-ROM、闪存、SD 卡。
- 块设备驱动对读/写可能必须执行附加工作：当写入数据大小不是整块时，需要先读出对应块，把写入数据"覆盖"到读出数据上合成新块，再作为一个整块写回。

| 对比项 | 字符设备 | 块设备 |
|--------|----------|--------|
| 数据访问 | 连续数据流，顺序访问 | 可随机访问 |
| 读写单位 | 字节/字符 | 块（512B 倍数） |
| 根本区别 | —— | 能否被随机访问 |

### 1.8 为什么要对设备分类

- MSH 可以重定向到任意字符设备上，例如将 LCD 模拟成字符设备，把打印输出到 LCD；或实现空字符设备把 msh 重定向。
- 文件系统依赖块设备驱动（如 SD、Flash），也可用 RAM 模拟块设备。
- 不同组件和应用依赖不同设备，分类后可对一类设备做同样的控制。

---

## 培训2：RT-Thread I/O API

### 2.1 创建 / 销毁设备

| 功能 | 接口 |
|------|------|
| 创建设备 | `rt_device_t rt_device_create(int type, int attach_size)` |
| 销毁设备 | `void rt_device_destroy(rt_device_t device)` |

**`rt_device_create()` 参数：**

| 参数 | 描述 |
|------|------|
| `type` | 设备类型，取 `RT_Device_Class_*` 值 |
| `attach_size` | 附加在设备结构后的用户数据大小 |

返回：创建设备句柄（成功）；`RT_NULL`（失败，动态内存分配失败）。

> 系统会从动态堆中分配一块「`struct rt_device` + attach_size」大小的设备控制块。设备被创建后，需要实现它访问硬件的操作方法 `ops`。可用 `rt_device_destroy()` 释放动态创建的设备。

### 2.2 设备操作方法 `struct rt_device_ops`

```c
struct rt_device_ops {
    rt_err_t   (*init)   (rt_device_t dev);
    rt_err_t   (*open)   (rt_device_t dev, rt_uint16_t oflag);
    rt_err_t   (*close)  (rt_device_t dev);
    rt_ssize_t (*read)   (rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);
    rt_ssize_t (*write)  (rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
    rt_err_t   (*control)(rt_device_t dev, int cmd, void *args);
};
```

**各方法说明：**

| 方法 | 说明 |
|------|------|
| `init` | 初始化设备，成功后 `flag` 置为已激活状态（`RT_DEVICE_FLAG_ACTIVATED`）；若已激活则再次调用会立即返回 |
| `open` | 打开设备，建议在 open 时才使能设备（避免上层未准备好就默认接收数据） |
| `close` | 关闭设备。设备控制块维护打开计数，open 时 +1、close 时 -1，当计数变 0 时才真正关闭 |
| `read` | 从设备读取数据。`pos` 为偏移量（串口类可忽略）；块设备中 `pos`/`size` 以数据块大小为单位。返回 `rt_ssize_t`（读到的字节数/块数），<0 表示错误 |
| `write` | 向设备写入数据，语义与 read 对称 |
| `control` | 根据 `cmd` 命令控制设备，命令由底层驱动自定义实现（如 `RT_DEVICE_CTRL_BLK_GETGEOME` 获取块设备大小） |

### 2.3 注册 / 注销设备

```c
rt_err_t rt_device_register(rt_device_t dev, const char *name, rt_uint16_t flags);
rt_err_t rt_device_unregister(rt_device_t dev);
```

**`rt_device_register()` 参数：**

| 参数 | 描述 |
|------|------|
| `dev` | 设备句柄 |
| `name` | 设备名称，最大长度由 `RT_NAME_MAX` 决定，多余部分自动截掉 |
| `flags` | 设备模式标志 |

返回：`RT_EOK`（成功）；`-RT_ERROR`（失败，`dev` 为空或 `name` 已存在）。

> 应当避免重复注册已经注册的设备，以及注册相同名字的设备。注销设备后其从设备管理器移除，无法再被查找；注销**不会释放**设备控制块占用的内存。

**`flags` 完整取值（可用 `|` 组合）：**

```
#define RT_DEVICE_FLAG_RDONLY    0x001   /* 只读 */
#define RT_DEVICE_FLAG_WRONLY    0x002   /* 只写 */
#define RT_DEVICE_FLAG_RDWR      0x003   /* 读写 */
#define RT_DEVICE_FLAG_REMOVABLE 0x004   /* 可移除 */
#define RT_DEVICE_FLAG_STANDALONE 0x008  /* 独立 */
#define RT_DEVICE_FLAG_SUSPENDED 0x020   /* 挂起 */
#define RT_DEVICE_FLAG_STREAM    0x040   /* 流模式 */
#define RT_DEVICE_FLAG_INT_RX    0x100   /* 中断接收 */
#define RT_DEVICE_FLAG_DMA_RX    0x200   /* DMA 接收 */
#define RT_DEVICE_FLAG_INT_TX    0x400   /* 中断发送 */
#define RT_DEVICE_FLAG_DMA_TX    0x800   /* DMA 发送 */
```

> `RT_DEVICE_FLAG_STREAM`（流模式）用于向串口终端输出字符串：当输出的字符是 `"\n"` 时，会自动在前面补一个 `"\r"` 做分行。

**查看已注册设备：** 注册成功后可用 FinSH 命令查看系统所有设备信息：

```
msh />list_device
device    type            ref count
--------  --------------  ----------
e0        Network Interface  0
sd0       Block Device       1
rtc       RTC                0
uart1     Character Device   0
uart0     Character Device   2
```

### 2.4 实验一：注册字符设备 `test`

在自己的开发板上注册一个 `test` 字符设备：
1. 调用 `rt_device_create()` 创建设备对象。

2. 填写设备操作方法 `ops`。

3. 调用 `rt_device_register()` 把设备注册到系统（驱动名称为 `test`）。

4. `list_device` 查看注册情况。

   ```
   #include <rtthread.h>
   #include <rtdevice.h>
   
   
   
   #if defined(BSP_USING_DRV_TEST)
   
   #define DRV_DEBUG
   #define LOG_TAG              "drv.test"
   #include <drv_log.h>
   
   static rt_err_t drv_test_init(rt_device_t dev)
   {
       LOG_I("test drv init");
       return RT_EOK;
   }
   
   static rt_err_t drv_test_open(rt_device_t dev, rt_uint16_t oflag)
   {
       LOG_I("test drv open flag = %d", oflag);
       return RT_EOK;
   }
   
   static rt_err_t drv_test_close(rt_device_t dev)
   {
       LOG_I("test drv close");
       return RT_EOK;
   }
   
   static rt_size_t drv_test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
   {
       LOG_I("test drv read pos = %d, size = %d", pos, size);
       return size;
   }
   
   static rt_size_t drv_test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
   {
       LOG_I("test drv write pos = %d, size = %d", pos, size);
       return size;
   }
   
   static rt_err_t drv_test_control(rt_device_t dev, int cmd, void *args)
   {
       LOG_I("test drv control cmd = %d", cmd);
       return RT_EOK;
   }
   
   int rt_drv_test_init(void)
   {
       rt_device_t test_dev = rt_device_create(RT_Device_Class_Char, 0);
       if(!test_dev)
       {
           LOG_E("test drv create failed!");
           return -RT_ERROR;
       }
   
       test_dev->init = drv_test_init;
       test_dev->open = drv_test_open;
       test_dev->close = drv_test_close;
       test_dev->read = drv_test_read;
       test_dev->write = drv_test_write;
       test_dev->control = drv_test_control;
   
       if(rt_device_register(test_dev, "test_drv", RT_DEVICE_FLAG_RDWR) != RT_EOK)
       {
           LOG_E("test drv register failed!");
           return -RT_ERROR;
       }
   
       return RT_EOK;
   }
   INIT_BOARD_EXPORT(rt_drv_test_init);
   
   static int drv_test_app(void)
   {
       rt_device_t test_dev = rt_device_find("test_drv");
       if(test_dev == RT_NULL)
       {
           LOG_E("can not find test drv!");
           return -RT_ERROR;
       }
       
       rt_device_open(test_dev, RT_DEVICE_OFLAG_RDWR);
       rt_device_control(test_dev, RT_DEVICE_CTRL_CONFIG, RT_NULL);
       rt_device_write(test_dev, 100, RT_NULL, 1024);
       rt_device_read(test_dev, 20, RT_NULL, 128);
   
       rt_device_close(test_dev);
   
       return RT_EOK;
   }
   MSH_CMD_EXPORT(drv_test_app, enable test drv app);
   
   #endif  /*defined(BSP_USING_DRV_TEST)*/
   
   ```

   注册情况：![](D:\Honor Share\Screenshot\屏幕截图 2026-08-23 103737.png)

   ![](D:\Honor Share\Screenshot\屏幕截图 2026-08-23 105641.png)

### 2.5 访问 I/O 设备（管理接口与 ops 映射）

应用程序通过 I/O 设备管理接口访问硬件设备，管理接口与设备 ops 方法存在映射关系：

| 功能 | 接口 |
|------|------|
| 查找设备 | `rt_device_t rt_device_find(const char* name)` |
| 初始化设备 | `rt_err_t rt_device_init(rt_device_t dev)` |
| 打开设备 | `rt_err_t rt_device_open(rt_device_t dev, rt_uint16_t oflags)` |
| 关闭设备 | `rt_err_t rt_device_close(rt_device_t dev)` |
| 控制设备 | `rt_err_t rt_device_control(rt_device_t dev, rt_uint8_t cmd, void* arg)` |
| 读设备 | `rt_size_t rt_device_read(rt_device_t dev, rt_off_t pos, void* buffer, rt_size_t size)` |
| 写设备 | `rt_size_t rt_device_write(rt_device_t dev, rt_off_t pos, const void* buffer, rt_size_t size)` |

### 2.6 查找设备

```c
rt_device_t rt_device_find(const char* name);
```

返回：设备句柄（找到）；`RT_NULL`（未找到对应设备对象）。

### 2.7 初始化设备

```c
rt_err_t rt_device_init(rt_device_t dev);
```

返回：`RT_EOK`（成功）；错误码（失败）。

> 当一个设备已经初始化成功后，调用此接口将不再重复初始化。

### 2.8 打开和关闭设备

```c
rt_err_t rt_device_open(rt_device_t dev, rt_uint16_t oflag);
rt_err_t rt_device_close(rt_device_t dev);
```

打开设备时，系统会检测设备是否已初始化，未初始化则默认调用初始化接口。**打开标志位 `oflag`：**

- `RT_DEVICE_OFlag_RDONLY`：只读
- `RT_DEVICE_OFlag_WRONLY`：只写
- `RT_DEVICE_OFlag_RDWR`：可读写
- （其他流式/非阻塞标志按需组合）

返回：`RT_EOK`（打开成功）；`-RT_EBUSY`（若设备注册时指定了 `RT_DEVICE_FLAG_STANDALONE`，则不允许重复打开）；其他错误码（失败）。

### 2.9 控制设备

```c
rt_err_t rt_device_control(rt_device_t dev, rt_uint8_t cmd, void* arg);
```

按 `cmd` 命令控制设备，命令由底层驱动自定义。

### 2.10 数据接收回调 `rx_indicate`

当硬件设备收到数据时，通过该函数设置**数据接收指示**，通知上层应用线程有数据到达：

```c
rt_err_t rt_device_set_rx_indicate(rt_device_t dev,
        rt_err_t (*rx_ind)(rt_device_t dev, rt_size_t size));
```

### 2.11 发送完成回调 `tx_complete`

在应用调用 `rt_device_write()` 写入数据时，若底层硬件支持自动发送，可设置回调，在底层硬件数据发送完成后（如 DMA/FIFO 传送完成或写入完毕产生完成中断时）被调用：

```c
rt_err_t rt_device_set_tx_complete(rt_device_t dev,
        rt_err_t (*tx_done)(rt_device_t dev, void* buffer));
```

### 2.12 I/O 框架调用关系图

```
应用程序
   │  rt_device_open/read/write/control/close
   ▼
I/O 设备管理层（I/O 设备管理接口）
   │  按 name 查找设备，并通过 ops 分发
   ▼
设备操作方法 ops（init/open/close/read/write/control）
   │
   ▼
硬件底层驱动（具体 BSP 实现）
```



---

## 培训3：GPIO（PIN）外设开发

### 3.1 引脚概念与原理

芯片上的引脚一般分为 4 类：**电源、时钟、控制与 I/O**。I/O 口在使用模式上分为：
- **GPIO**（General Purpose Input Output，通用输入/输出）
- **功能复用 I/O**（如 SPI/I2C/UART 功能脚）

大多数 MCU 引脚不止一个功能，可通过不同配置切换引脚实际功能。**通用 I/O 口主要特性：**

- **可编程控制中断**：触发模式可配置，一般有 **5 种中断触发模式**（上升沿、下降沿、双沿、高电平、低电平）。
- **输入输出模式可控制**：
  - 输出模式：推挽、开漏、上拉、下拉，可配置输出高/低电平。
  - 输入模式：浮空、上拉、下拉、模拟，可读取引脚电平状态。

### 3.2 PIN 设备管理接口总览

| 接口 | 功能 |
|------|------|
| `rt_pin_get()` | 获取引脚编号 |
| `rt_pin_mode()` | 设置引脚模式 |
| `rt_pin_write()` | 设置引脚电平 |
| `rt_pin_read()` | 读取引脚电平 |
| `rt_pin_attach_irq()` | 绑定引脚中断回调函数 |
| `rt_pin_irq_enable()` | 使能引脚中断 |
| `rt_pin_detach_irq()` | 脱离引脚中断回调函数 |

### 3.3 获取引脚编号（重要）

RT-Thread 提供的引脚编号**与芯片引脚号不是同一个概念**，引脚编号由 PIN 设备驱动程序定义，与具体芯片相关。有 3 种方式获取：

**① 使用 API：**
```c
pin_number = rt_pin_get("PF.9");
```

**② 使用宏定义（`rt-thread/bsp/stm32` BSP）：**
```c
#include <rtdevice.h>
#include <board.h>                       // 不包含可能报 "没有 'F' 定义"
#define LED0_PIN GET_PIN(F, 9)           // 获取 PF9
```

**③ 查看驱动文件 `drv_gpio.c`：** 该文件有数组存放每个 PIN 脚的编号信息，如：
```c
static const rt_uint16_t pins[] = {
    __STM32_PIN(2, A, 15),   /* 2 为 RT-Thread 引脚编号，A 为端口，15 为引脚号 */
    __STM32_PIN(3, B, 5),
    ... };
```
即 PA15 对应的 RT-Thread 引脚编号为 2。

### 3.4 设置引脚模式

```c
void rt_pin_mode(rt_base_t pin, rt_uint8_t mode);
```

`mode` 可取 5 种宏之一（实际支持模式看驱动实现）：

| 宏 | 值 | 含义 |
|----|----|------|
| `PIN_MODE_OUTPUT` | 0x00 | 输出 |
| `PIN_MODE_INPUT` | 0x01 | 输入 |
| `PIN_MODE_INPUT_PULLUP` | 0x02 | 上拉输入 |
| `PIN_MODE_INPUT_PULLDOWN` | 0x03 | 下拉输入 |
| `PIN_MODE_OUTPUT_OD` | 0x04 | 开漏输出 |

### 3.5 设置 / 读取引脚电平

```c
void rt_pin_write(rt_base_t pin, rt_ssize_t value);   /* value: PIN_LOW / PIN_HIGH */
rt_ssize_t rt_pin_read(rt_base_t pin);                 /* 返回 PIN_LOW / PIN_HIGH */
```

示例：
```c
#define BEEP_PIN_NUM 35   /* PB0 */
rt_pin_mode(BEEP_PIN_NUM, PIN_MODE_OUTPUT);
rt_pin_write(BEEP_PIN_NUM, PIN_LOW);     /* 输出低电平 */
int status = rt_pin_read(BEEP_PIN_NUM);  /* 读取电平 */
```

### 3.6 绑定引脚中断回调函数

```c
rt_err_t rt_pin_attach_irq(rt_base_t pin, rt_uint8_t mode,
                           void (*hdr)(void *args), void *args);
```

**中断触发模式 `mode` 可取 5 种宏之一：**

| 宏 | 值 | 含义 |
|----|----|------|
| `PIN_IRQ_MODE_RISING` | 0x00 | 上升沿触发 |
| `PIN_IRQ_MODE_FALLING` | 0x01 | 下降沿触发 |
| `PIN_IRQ_MODE_RISING_FALLING` | 0x02 | 边沿触发（上下沿都触发） |
| `PIN_IRQ_MODE_HIGH_LEVEL` | 0x03 | 高电平触发 |
| `PIN_IRQ_MODE_LOW_LEVEL` | 0x04 | 低电平触发 |

### 3.7 使能 / 脱离引脚中断

```c
rt_err_t rt_pin_irq_enable(rt_base_t pin, rt_uint8_t enabled);  /* enabled: PIN_IRQ_ENABLE / PIN_IRQ_DISABLE */
rt_err_t rt_pin_detach_irq(rt_base_t pin);
```

> 脱离回调函数后，中断并没有关闭，还可以调用 `rt_pin_attach_irq()` 再次绑定其他回调函数。

### 3.8 完整示例：按键控制蜂鸣器（官方 pin_beep_sample）

```c
#include <rtthread.h>
#include <rtdevice.h>

#define BEEP_PIN_NUM 35   /* PB0 */
#define KEY0_PIN_NUM 55   /* PD8 */
#define KEY1_PIN_NUM 56   /* PD9 */

void beep_on(void *args)
{
    rt_kprintf("turn on beep!\n");
    rt_pin_write(BEEP_PIN_NUM, PIN_HIGH);
}
void beep_off(void *args)
{
    rt_kprintf("turn off beep!\n");
    rt_pin_write(BEEP_PIN_NUM, PIN_LOW);
}
static void pin_beep_sample(void)
{
    /* 蜂鸣器引脚：输出模式，默认低电平 */
    rt_pin_mode(BEEP_PIN_NUM, PIN_MODE_OUTPUT);
    rt_pin_write(BEEP_PIN_NUM, PIN_LOW);

    /* 按键0：上拉输入，下降沿触发 */
    rt_pin_mode(KEY0_PIN_NUM, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(KEY0_PIN_NUM, PIN_IRQ_MODE_FALLING, beep_on, RT_NULL);
    rt_pin_irq_enable(KEY0_PIN_NUM, PIN_IRQ_ENABLE);

    /* 按键1：上拉输入，下降沿触发 */
    rt_pin_mode(KEY1_PIN_NUM, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(KEY1_PIN_NUM, PIN_IRQ_MODE_FALLING, beep_off, RT_NULL);
    rt_pin_irq_enable(KEY1_PIN_NUM, PIN_IRQ_ENABLE);
}
MSH_CMD_EXPORT(pin_beep_sample, pin beep sample);
```

### 3.9 按键库 FlexibleButton

- 在 menuconfig 中勾选 **FlexibleButton** 按键库。
- 用于按键消抖、单击/双击/长按等状态机处理，配合 GPIO 中断使用。

---

## 培训3：I2C 外设开发

### 4.1 I2C 总线简介

I2C（Inter-Integrated Circuit，读作 I-squared-C），由**飞利浦（PHILIPS）公司于 1980 年代**提出，是**半双工、双向二线制同步串行总线**，用于连接主板、嵌入式系统或手机的低速周边外设。

I2C 只需两根信号线：
- **SDA**（serial data）：双向数据线。
- **SCL**（serial clock）：双向时钟线。

> 与 SPI（两根线分别收/发数据）不同，I2C 只用一根线做数据收发。I2C 与 SPI 一样以主从方式工作，但**允许多个主设备**同时存在，每个器件有唯一地址，同一时刻只允许一个主设备。

**常见传输速率模式：**

| 模式 | 速率 |
|------|------|
| 低速模式 | 10 Kbit/s |
| 标准模式 | 100 Kbit/s |
| 快速模式 | 400 Kbit/s |
| 高速模式 | 3.4 Mbit/s |

### 4.2 数据传输格式与协议术语

总线空闲时 SDA 和 SCL 均为高电平。主机通讯流程：发送**开始条件** → 发送**从机地址和读写控制位** → **传输数据** → 发送**停止条件**。每个字节 8 位，高位在前、低位在后。

| 术语 | 说明 |
|------|------|
| **开始条件** | SCL 为高电平时，主机将 SDA 拉低，表示数据即将开始 |
| **从机地址** | 主机发送的第一个字节，高 7 位为地址，最低位为 R/W 读写控制位（1=读，0=写）。有 7 位/10 位两种模式；10 位模式第一位字节头 7 位是 `11110XX`，后 8 位在第二个字节 |
| **应答信号 ACK** | 每传完一个字节，接收方回复 ACK。写数据时由从机发送 ACK，读数据时由主机发送 ACK；主机读到最后一个字节时可发 NACK 再跟停止条件 |
| **数据** | 从机地址后可能发一些指令（依从机而定），然后开始传数据，每字节 8 位，字节数不限 |
| **重复开始条件** | 一次通信中，主机需与不同从机通信或切换读写时，可再次发送开始条件 |
| **停止条件** | SDA 为低电平时，主机将 SCL 拉高并保持，再拉高 SDA，表示传输结束 |

**起始位(S)/结束位(P) 与电平关系：**
- **起始位（S）**：SCL 高电平时，SDA 由高变低。
- **结束位（P）**：SCL 高电平时，SDA 由低变高。

**ACK/NACK：**
- **ACK**：拉低 SDA，并在 SCL 高电平期间保持 SDA 为低。
- **NACK**：不拉低 SDA（SDA 保持高电平），并在 SCL 高电平期间保持 SDA 为高。

### 4.3 主机向从机写 / 读数据

**写数据流程：**
1. 发送起始位。
2. 主设备发送从设备地址（7bit 地址 + 最低位写标志组成的 8bit 数据）。
3. 释放 SDA 线，等待从机应答。
4. 每一字节数据传输都跟一个应答位。
5. 数据传完发送结束位，释放 I2C 总线。

**读数据流程：**
1. 起始位后，主设备先发送「从设备地址 + 写标志」（指定寄存器/内部地址）。
2. 释放 SDA，从机应答后再次发送起始位（重复开始条件）。
3. 主设备再发送「从设备地址 + 读标志」，从机应答后把数据发给主设备，主设备应答。
4. 读完后发送 NACK + 结束位。

### 4.4 从机常用模式 / 访问 I2C 总线设备接口

RT-Thread 将 **I2C 主机虚拟为 I2C 总线设备**，I2C 从机通过 I2C 设备接口与总线通讯：

| 函数 | 描述 |
|------|------|
| `rt_device_find()` | 根据 I2C 总线设备名称查找设备获取设备句柄 |
| `rt_i2c_transfer()` | 传输数据（消息为单位传输） |

| 操作 | 说明 |
|------|------|
| 向从机某个寄存器写一个字节 | 一次写 |
| 向从机某个寄存器写多个字节 | 写地址 + 连续写 |
| 从从机某个寄存器读一个字节 | 写寄存器 + 读数据 |
| 从从机某个寄存器读多个字节 | 写寄存器 + 连续读 |

### 4.5 查看 / 探测 I2C 总线设备

- 开启 I2C 驱动后用 `list_device` 命令查看总线注册情况。
- 勾选 **i2c-tools** 软件包方便调试，探测总线设备：
```
msh> i2c scan i2c3
```

### 4.6 查找 I2C 总线设备

```c
rt_device_t rt_device_find(const char* name);
```

一般注册名称为 `i2c0`、`i2c1` 等：
```c
#define AHT10_I2C_BUS_NAME "i2c1"
struct rt_i2c_bus_device *i2c_bus;
i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(AHT10_I2C_BUS_NAME);
```

### 4.7 I2C 数据传输函数 `rt_i2c_transfer`

```c
rt_ssize_t rt_i2c_transfer(struct rt_i2c_bus_device *bus,
                           struct rt_i2c_msg msgs[], rt_uint32_t num);
```

- `bus`：I2C 总线设备句柄；`msgs[]`：待传输消息数组指针；`num`：消息元素个数。
- 返回消息数组元素个数（成功）或错误码（失败）。
- 与 SPI 自定义传输类似，以消息为单位；如需发送**重复开始条件**，则发送 2 个消息。

> ⚠️ `rt_i2c_transfer()` 会调用 `rt_mutex_take()`，**不能在中断服务程序里调用**，否则会导致 assertion 报错。

**消息结构 `struct rt_i2c_msg`：**

```c
struct rt_i2c_msg {
    rt_uint16_t addr;    /* 从机地址 */
    rt_uint16_t flags;   /* 读、写标志等 */
    rt_uint16_t len;     /* 读写数据字节数 */
    rt_uint8_t *buf;     /* 读写数据缓冲区指针 */
};
```

> 从机地址 `addr` 支持 7 位和 10 位二进制地址。**RT-Thread I2C 设备接口使用的从机地址均不包含读写位**，读写位控制需修改标志 `flags`。

**`flags` 可取值（可用 `|` 组合）：**

| 宏 | 值 | 含义 |
|----|----|------|
| `RT_I2C_WR` | 0x0000 | 写标志（不可与读标志 `|`） |
| `RT_I2C_RD` | 1<<0 | 读标志（不可与写标志 `|`） |
| `RT_I2C_ADDR_10BIT` | 1<<2 | 10 位地址模式 |
| `RT_I2C_NO_START` | 1<<4 | 无开始条件 |
| `RT_I2C_IGNORE_NACK` | 1<<5 | 忽视 NACK |
| `RT_I2C_NO_READ_ACK` | 1<<6 | 读的时候不发送 ACK |
| `RT_I2C_NO_STOP` | 1<<7 | 不发送结束位 |

### 4.8 推荐使用的从设备读写便捷 API

以下两个函数封装自 `rt_i2c_transfer()`，更简单易用，推荐使用：

**发送数据：**
```c
rt_ssize_t rt_i2c_master_send(struct rt_i2c_bus_device *bus, rt_uint16_t addr,
                              rt_uint16_t flags, const rt_uint8_t *buf, rt_uint32_t count);
```

**接收数据：**
```c
rt_ssize_t rt_i2c_master_recv(struct rt_i2c_bus_device *bus, rt_uint16_t addr,
                              rt_uint16_t flags, rt_uint8_t *buf, rt_uint32_t count);
```

> `flags` 只能传除 `RT_I2C_WR`/`RT_I2C_RD` 之外的其他标志位（可 `|` 组合）。

**小技巧——拼接发送一条数据（prefix + buffer）：**
```c
rt_i2c_master_send(_bus, _addr, RT_I2C_NO_STOP, prefix, prefix_len); /* 只发起始位，不发停止位 */
rt_i2c_master_send(_bus, _addr, RT_I2C_NO_START, buffer, len);       /* 不发起始位，只发停止位 */
```

### 4.9 I2C 使用思路与示例代码

**使用思路：**
1. 查找 I2C 总线设备（`rt_device_find`）。
2. 构造 `msgs` 消息（填充地址、标志、缓冲区）。
3. 启动传输（`rt_i2c_transfer` 或 `rt_i2c_master_send/recv`）。
4. 处理结果（校验返回值）。

**写一个字节数据：**
```c
struct rt_i2c_msg msgs = { .addr = slave_addr, .flags = RT_I2C_WR, .buff = &data, .len = 1 };
rt_i2c_transfer(bus, &msgs, 1);
```

**写多字节 / 读数据：**
```c
/* 读寄存器：第一条消息写寄存器地址(W) */
msg1.addr = dev_addr; msg1.flags = RT_I2C_WR; msg1.buf = reg; msg1.len = 1;
/* 第二条消息读数据(R) */
msg2.addr = dev_addr; msg2.flags = RT_I2C_RD; msg2.buf = data; msg2.len = n;
rt_i2c_transfer(bus, msgs, 2);
```

**完整 AHT10 温湿度传感器例程**（官方 `i2c_aht10_sample`，`MSH_CMD_EXPORT` 导出，命令格式 `i2c_aht10_sample i2c1`）：查找总线 → 写命令/校准 → 延时 → 读 6 字节 → 按位解析湿度与温度。要点：
- `write_reg()`/`read_regs()` 分别调用 `rt_i2c_transfer()`。
- 读温湿度前用 `rt_thread_mdelay(400)` 等待传感器就绪。

### 4.10 应用开发常见错误 / 总线死锁

**现象：** 单个设备挂掉导致总线死锁，`rt_i2c_transfer` 函数执行返回 `-5`。

**死锁原因：**
- 当 I2C 主机正与从机通信，主机准备发第 9 个时钟时，`SCL` 为高，从机开始拉低 `SDA` 做 ACK 准备，等待主机 `SCL` 变低后从机再释放 `SDA` 为高。
- 如果此时主机复位，`SCL` 还没来得及变低，而主机发现 `SDA` 一直为低，也在等待从机释放 `SDA` 为高，**主机和从机都进入相互等待的死锁状态**。

**解锁思路：**
- 在主机启动传输前，先控制 `SCL` 时钟线产生 **9 个时钟脉冲**（针对 8 位数据），这样 I2C 从设备可以完成被挂起的读操作，从死锁状态恢复。

**`rt_i2c_transfer` 返回 `-5` 的另一个原因：设备地址错误**（从机地址与设备手册不符）。

### 4.11 软件 I2C 驱动编写

1. 开启 I2C 框架（menuconfig 使能 `RT_USING_I2C`）。
2. 选中 **I2C 软件模拟设备**功能。
3. 配置内核（`menuconfig` 配置 I2C 引脚）。
4. 编写 I2C 软件模拟驱动模板（实现总线 start/stop/读/写 基元）。

---

## 培训4：SPI 外设开发

### 5.1 SPI 总线概念与原理

SPI（Serial Peripheral Interface，串行外设接口）是一种**高速、全双工、同步**通信总线，常用于短距离通讯，主要应用于 EEPROM、FLASH、实时时钟、AD 转换器、数字信号处理器和数字信号解码器之间。SPI 一般使用 **4 根线**通信：

| 线 | 含义 |
|----|------|
| **MOSI** | 主机输出 / 从机输入（Master Output/Slave Input） |
| **MISO** | 主机输入 / 从机输出（Master Input/Slave Output） |
| **SCLK** | 串行时钟线（Serial Clock），主设备输出时钟至从设备 |
| **CS** | 从设备选择线（Chip select），也叫 SS、CSB、CSN、EN 等 |

**工作方式：**
- SPI 以主从方式工作，通常一个主设备 + 一个或多个从设备。通信由主设备发起，通过 CS 选择从设备，SCLK 提供时钟，数据经 MOSI 输出、MISO 接收。
- 一个 SPI 主设备（控制器）可连接多个从设备；同一控制器下的从设备**共享 SCK、MISO、MOSI** 三根线，但每个从设备的 **CS 引脚独立**（一般为低电平有效）。
- 任何时刻一个主设备只有一个 CS 处于有效状态。

**时序模式（CPOL / CPHA）：**
- **CPOL**（时钟极性）：表示时钟信号的初始电平，0=初始低电平，1=初始高电平。
- **CPHA**（时钟相位）：表示在哪个时钟沿采样数据，0=首个时钟变化沿采样，1=第二个时钟变化沿采样。
- 组合出 4 种模式：①CPOL=0,CPHA=0 ②CPOL=0,CPHA=1 ③CPOL=1,CPHA=0 ④CPOL=1,CPHA=1。

**SPI Flash 扩展类型：**
- **QSPI**（Queued SPI）：Motorola 推出的 SPI 扩展，增加队列传输机制，一次可传多达 16 个 8/16 位数据的队列，启动后到结束都无需 CPU 干预；最大结构特点是**以 80 字节 RAM 代替发送/接收数据寄存器**。
- **Dual SPI Flash**：SPI Flash 全双工不常用，可发命令字节进入 Dual 半双工模式加倍速率，MOSI→SIO0、MISO→SIO1，一个时钟周期传 2 bit。
- **Quad SPI Flash**：增加 SIO2、SIO3 两根线，一个时钟传 4 bit。

> 标准 SPI / Dual SPI / Quad SPI Flash 三者：相同时钟下线数越多速率越高。

### 5.2 RT-Thread SPI 开发模式

在 RT-Thread 中 SPI 设备分为 **SPI 总线**和 **SPI 设备**两大类：
- **SPI 总线**：对应 SPI 控制器（主设备）。
- **SPI 设备**：对应不同 CS 连接的从设备。

使用前需先注册 SPI 总线，再把从设备挂载到总线上。**开发模式流程：**
1. 编写 SPI BUS 驱动 → 2. 注册/挂载 SPI Device → 3. 打开 SPI Device → 4. 使用 API 收发 → 5. 关闭/卸载。

### 5.3 挂载 SPI 设备

SPI 驱动会注册 SPI 总线，SPI 设备需挂载到已注册的总线。

**新接口（RT-Thread 5.0.0 起，推荐）：** 基于 PIN 框架绑定片选引脚，避免不同 BSP 片选操作不统一：

```c
rt_err_t rt_spi_bus_attach_device_cspin(struct rt_spi_device *device,
        const char *name, const char *bus_name, rt_base_t cs_pin, void *user_data);
```

- `cs_pin` 可通过 PIN 框架 `rt_pin_get("PB.14")` 或 BSP 级 `GET_PIN(B, 14)` 获取；`user_data` 用不到可设为 `RT_NULL`。

**兼容旧接口（5.0.0 前）：** 片选引脚通过 `user_data` 挂载：

```c
rt_err_t rt_spi_bus_attach_device(struct rt_spi_device *device,
        const char *name, const char *bus_name, void *user_data);
```

**命名原则：** SPI 总线命名 `spix`（如 `spi1`），SPI 设备命名 `spixy`（如 `spi10` 表示挂载在 spi1 总线上的 0 号设备）。旧接口 `user_data` 一般为 CS 引脚指针。

**从总线卸载设备（动态申请场景）：**
```c
rt_err_t rt_spi_bus_detach_device(struct rt_spi_device *device);        /* 应用层优先使用 */
rt_err_t rt_spi_bus_detach_device_cspin(struct rt_spi_device *device);
```
卸载时先从框架注销设备；若该设备是总线当前 owner 则清空；配置了有效 `cs_pin` 则将该引脚重新设为输入模式；最后把 `device->bus` 置为 `RT_NULL`。卸载后可 `rt_free()` 释放动态申请的设备。

**W25Q128 挂载示例：**
```c
static int rt_hw_spi_flash_init(void)
{
    struct rt_spi_device *spi_device = (struct rt_spi_device *)rt_malloc(sizeof(struct rt_spi_device));
    if (RT_EOK != rt_spi_bus_attach_device_cspin(spi_device, "spi10", "spi1", GET_PIN(B, 14), RT_NULL))
        return -RT_ERROR;
    if (RT_NULL == rt_sfud_flash_probe("W25Q128", "spi10"))
        return -RT_ERROR;
    return RT_EOK;
}
INIT_COMPONENT_EXPORT(rt_hw_spi_flash_init);
```

### 5.4 配置 SPI 设备

```c
rt_err_t rt_spi_configure(struct rt_spi_device *device, struct rt_spi_configuration *cfg);
```

**`struct rt_spi_configuration`：**
```c
struct rt_spi_configuration {
    rt_uint8_t  mode;        /* 模式 */
    rt_uint8_t  data_width;  /* 数据宽度：8/16/32 位 */
    rt_uint16_t reserved;    /* 保留 */
    rt_uint32_t max_hz;      /* 最大频率（波特率） */
};
```

**`mode` 常用宏组合：**
```
RT_SPI_LSB / RT_SPI_MSB       /* bit[2]: 传输顺序 LSB/MSB 在前 */
RT_SPI_MASTER / RT_SPI_SLAVE  /* bit[3]: 主从模式 */
RT_SPI_MODE_0                 /* CPOL=0, CPHA=0 */
RT_SPI_MODE_1                 /* CPOL=0, CPHA=1 */
RT_SPI_MODE_2                 /* CPOL=1, CPHA=0 */
RT_SPI_MODE_3                 /* CPOL=1, CPHA=1 */
RT_SPI_CS_HIGH                /* bit[4]: 片选高有效 */
RT_SPI_NO_CS                  /* bit[5]: 无片选 */
RT_SPI_3WIRE                  /* bit[6]: SI/SO 共用 */
RT_SPI_READY                  /* bit[7]: 从机拉低暂停 */
```

**配置示例：**
```c
struct rt_spi_configuration cfg;
cfg.data_width = 8;
cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
cfg.max_hz = 20 * 1000 * 1000;   /* 20MHz */
rt_spi_configure(spi_dev, &cfg);
```

**QSPI 配置：**
```c
rt_err_t rt_qspi_configure(struct rt_qspi_device *device, struct rt_qspi_configuration *cfg);
```
`struct rt_qspi_configuration` 继承 `struct rt_spi_configuration parent`，并增加 `medium_size`（介质大小）、`ddr_mode`（双倍速率）、`qspi_dl_width`（QSPI 总线位宽：单线 1、双线 2、四线 4）。

### 5.5 SPI 访问接口总览

| 函数 | 描述 |
|------|------|
| `rt_device_find()` | 根据 SPI 设备名称查找设备获取设备句柄 |
| `rt_spi_transfer_message()` | 自定义传输数据（消息链） |
| `rt_spi_transfer()` | 传输一次数据 |
| `rt_spi_send()` | 发送一次数据 |
| `rt_spi_recv()` | 接收一次数据 |
| `rt_spi_send_then_send()` | 连续两次发送 |
| `rt_spi_send_then_recv()` | 先发送后接收 |

> ⚠️ SPI 数据传输接口会调用 `rt_mutex_take()`，**不能在中断服务程序中调用**，否则 assertion 报错。

### 5.6 查找 SPI 设备

```c
rt_device_t rt_device_find(const char* name);
```
一般注册名称为 `spi10`、`qspi10` 等：
```c
#define W25Q_SPI_DEVICE_NAME "qspi10"
struct rt_spi_device *spi_dev_w25q = (struct rt_spi_device *)rt_device_find(W25Q_SPI_DEVICE_NAME);
```

### 5.7 自定义传输 `rt_spi_transfer_message`

```c
struct rt_spi_message *rt_spi_transfer_message(struct rt_spi_device *device,
        struct rt_spi_message *message);
```
返回 `RT_NULL`（成功）或指向未发送完消息的指针（失败）。可传输一串消息（单向链表），`struct rt_spi_message` 原型：

```c
struct rt_spi_message {
    const void *send_buf;         /* 发送缓冲区指针，RT_NULL 表示只接收 */
    void       *recv_buf;         /* 接收缓冲区指针，RT_NULL 表示只发送 */
    rt_size_t   length;           /* 收发数据字节数 */
    struct rt_spi_message *next;  /* 指向继续发送的下一条消息 */
    unsigned cs_take : 1;         /* 置 1：传输前将对应 CS 设为有效 */
    unsigned cs_release : 1;      /* 置 1：数据传输结束后释放 CS */
};
```

- `length` 单位为 word：数据 8 位时 1 个 length 占用 1 字节；16 位时占 2 字节。
- 多条消息通过 `next` 以单向链表连接；第一条消息 `cs_take` 需置 1，最后一条 `cs_release` 置 1。
- 当 `send_buf` 或 `recv_buf` 非空时，两者的可用空间都不得小于 `length`。

**读取 W25Q ID 示例：**
```c
rt_uint8_t w25x_read_id = 0x90;  /* 命令 */
rt_uint8_t id[5] = {0};
struct rt_spi_message msg1, msg2;

msg1.send_buf = &w25x_read_id; msg1.recv_buf = RT_NULL; msg1.length = 1;
msg1.cs_take = 1; msg1.cs_release = 0; msg1.next = &msg2;

msg2.send_buf = RT_NULL; msg2.recv_buf = id; msg2.length = 5;
msg2.cs_take = 0; msg2.cs_release = 1; msg2.next = RT_NULL;

rt_spi_transfer_message(spi_dev_w25q, &msg1);
```

### 5.8 传输一次数据 `rt_spi_transfer`

```c
rt_ssize_t rt_spi_transfer(struct rt_spi_device *device,
        const void *send_buf, void *recv_buf, rt_size_t length);
```
返回 0（失败）或成功传输的字节数。此函数**不需要手动控制片选**，等同于调用 `rt_spi_transfer_message()` 传输一条消息（`cs_take=1, cs_release=1`），发送时片选选中、返回时释放。

### 5.9 单独发送 / 单独接收

```c
rt_err_t rt_spi_send(struct rt_spi_device *device, const void *send_buf, rt_size_t length);  /* 只发送，忽略接收 */
rt_err_t rt_spi_recv(struct rt_spi_device *device, void *recv_buf, rt_size_t length);        /* 只接收 */
```

### 5.10 连续两次发送 `rt_spi_send_then_send`

```c
rt_err_t rt_spi_send_then_send(struct rt_spi_device *device,
        const void *send_buf1, rt_size_t send_length1,
        const void *send_buf2, rt_size_t send_length2);
```
先发第一条数据时片选选中，发送完第二条后释放片选。**适用场景**：先发送地址，再发送指定长度数据，中途不释放片选。

### 5.11 先发送后接收 `rt_spi_send_then_recv`

```c
rt_err_t rt_spi_send_then_recv(struct rt_spi_device *device,
        const void *send_buf, rt_size_t send_length,
        void *recv_buf, rt_size_t recv_length);
```
发送第一条数据时开始片选（忽略接收数据），然后发送第二条数据，主设备会发送 `0xFF`，接收数据保存在 `recv_buf` 中，返回时释放片选。**适用场景**：从 SPI 从设备读取一块数据，先发命令/地址再接收指定长度数据。

---

## 进阶：设备驱动框架（Driver Model）

> 本节整理自官方《设备驱动框架》章节（对应 `components/drivers`、公共头文件和 BSP 驱动实现），主要面向**复杂 SoC 平台驱动**。

### 6.1 驱动目录组织（`components/drivers`）

`components/drivers/Kconfig` 集中组织 DM 相关子系统，设备树、总线匹配、统一资源管理和通用子系统是复杂平台驱动的主要组织方式。

| 子系统 | 主要代码位置 | 说明 |
|--------|--------------|------|
| 驱动模型核心 | `drivers/core` | DM、platform bus、设备树资源解析、系统关机/复位回调 |
| Clock Time | `drivers/clock_time` | 统一承载 cputime/hwtimer，提供时钟源、事件源、clock timer、hrtimer、boottime |
| 电源时钟资源 | `clk`/`reset`/`regulator`/`pmdomain` | 面向 SoC 的电源和时钟资源框架 |
| DMA | `drivers/dma` | DMA 控制器、通道、循环/单次传输和 DMA 内存池 |
| VirtIO/RPMsg | `drivers/virtio`/`rpmsg` | 虚拟化设备、AMP/remote processor 通信 |
| 图形和输入 | `graphic`/`input` | DM 图形设备、backlight、framebuffer、input event |
| 块设备/存储 | `block`/`ata`/`nvme`/`scsi`/`ufs`/`sdio` | 统一块设备层和多类存储控制器 |
| 固件和 SCMI | `firmware/arm_scmi` | SCMI transport、clock、reset、regulator、power domain 等 |

这些模块大多依赖 `RT_USING_DM`，并通过 `rtdevice.h` 纳入公共设备头文件。

### 6.2 DM 核心能力

`components/drivers/core/dm.c` 和 `include/drivers/core/dm.h` 提供设备侧资源读取能力，驱动在 `probe` 中常用接口：

- `rt_dm_dev_iomap()` / `rt_dm_dev_iomap_by_name()`：根据设备树 `reg` 映射寄存器。
- `rt_dm_dev_get_irq()` / `rt_dm_dev_get_irq_by_name()`：读取并映射中断号。
- `rt_dm_dev_prop_read_u32()` / `rt_dm_dev_prop_read_string()` 等：读取设备树属性。
- `rt_dm_dev_set_name_auto()` / `rt_dm_dev_get_name_id()`：按 master id 自动分配稳定设备名。
- `rt_dm_dev_bind_fwdata()` / `rt_dm_dev_unbind_fwdata()`：绑定固件节点和 RT-Thread 设备对象。

`power.c`/`power.h` 用于系统级关机、重启和 reboot mode：`rt_dm_power_off_handler()`、`rt_dm_reboot_mode_register()`。其中 `mode` 区分 shutdown/reset，`priority` 控制平台、普通驱动、固件等回调顺序。

### 6.3 平台驱动编写推荐顺序

1. 由 OFW/platform 匹配设备节点。
2. 应用 pinctrl 默认状态。
3. 获取 regulator、power domain、clock、reset 等资源。
4. 映射寄存器并获取中断。
5. 初始化控制器私有结构体。
6. 注册到对应功能子系统（UART、SPI、I2C、block、input、graphic）。

> 按此顺序可减少"设备已注册，但电源/时钟/复位还没准备好"的问题。

### 6.4 驱动编写注意事项

- 复杂 SoC 驱动建议优先走 DM，不要在 BSP 中手写大量资源解析逻辑。
- 复杂子系统多依赖设备树，Kconfig 中通常有 `depends on RT_USING_DM` 或 `RT_USING_OFW`。
- 若 BSP 只使用传统设备接口，可以继续保留传统路径；复杂 driver 建议优先落在 DM 路径。
- 涉及 DMA、cache、framebuffer、VirtIO 的驱动，需同时检查 DMA 地址、cache 维护和内存属性。

---

## 总结

1. **I/O 设备框架本质**：通过「驱动代码与设备驱动分离 + 统一 API」解决跨芯片、多厂家的驱动碎片化问题，使代码可复用、可入库、可做成软件包。
2. **三层设备模型**：I/O 设备管理层 → 设备驱动框架层 → 设备驱动层；简单设备直接 `create→register`，复杂设备先注册到驱动框架再由框架注册。
3. **I/O API 五步式**：应用层通过 `open → read/write → control → close` 访问设备，底层由设备 `ops` 分发到具体硬件驱动；配套 `find/init`、`rx_indicate/tx_complete` 回调。
4. **GPIO（PIN）**：一套 `rt_pin_*` 接口统一配置模式、读写电平、绑定/使能/脱离中断；引脚编号需通过 `rt_pin_get / GET_PIN / drv_gpio.c` 三种方式之一获取。
5. **I2C**：理解总线协议（起止位、ACK/NACK、读写格式、重复开始），掌握 `rt_i2c_msg` 消息结构、`rt_i2c_transfer` 与推荐使用的 `rt_i2c_master_send/recv`，以及总线死锁成因与「9 个 SCL 时钟」解锁方法。
6. **SPI**：掌握 4 线通信、CPOL/CPHA 时序、总线/设备分离挂载、命名规则与各类传输 API 的适用场景；`rt_spi_transfer_message` 支持消息链表自定义传输，推荐 5.0.0 后使用 `rt_spi_bus_attach_device_cspin` 绑定片选。
7. **共同注意**：SPI/I2C 传输接口内部调用互斥锁，**不能在中断服务程序中调用**。
8. **进阶（DM）**：复杂 SoC 驱动优先走 Driver Model，结合设备树、电源/时钟/复位资源有序初始化，减少传统 BSP 手写资源解析。

---

## 参考资料

- RT-Thread 官方文档《I/O 设备模型》：<https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/device>
- RT-Thread 官方文档《设备驱动框架》：<https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/framework/device-driver-framework>
- RT-Thread 官方文档《PIN 设备》：<https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/pin/pin>
- RT-Thread 官方文档《I2C 总线设备》：<https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/i2c/i2c>
- RT-Thread 官方文档《SPI 设备》：<https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/device/spi/spi>
- RT-Thread 设备类型定义：`rt-thread/include/rtdef.h`（`rt_device_class_type`）