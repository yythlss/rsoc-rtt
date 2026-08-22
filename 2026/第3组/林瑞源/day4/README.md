# 第四天作业：RT-Thread 设备驱动框架与 IO 外设开发

**提交人**：林瑞源（第 3 组）  **平台**：星火 1 号（STM32F407）
**培训主题**：RT-Thread 设备驱动框架与 IO 外设开发
**讲师**：吴艺彬（CYFS）

---

## 一、课堂笔记：设备驱动框架架构与分层设计

### 1.1 为什么要分层抽象

嵌入式开发中，不同 MCU 厂商提供的外设 API 差异巨大（ST 的 HAL、GD 的固件库、NXP 的 SDK……），导致上层应用代码与底层驱动深度耦合，换一颗芯片就要重写大量业务逻辑。RT-Thread 设备框架通过**分层抽象**解决这一痛点：

| 层次 | 职责 | 典型 API |
|------|------|----------|
| 应用层 | 业务逻辑，不关心硬件细节 | `rt_device_find/open/read/write/close` |
| IO 管理层 | 统一设备管理，维护设备链表，分发 ops 调用 | `rt_device_register/find/control` |
| 驱动层 | 对接具体硬件，实现 ops 函数指针集 | `drv_gpio_init/read/write/...` |
| 硬件层 | MCU 寄存器 / HAL 库 | STM32 HAL / GD32 fwlib |

上层应用使用统一的 `open/read/write/close` 接口操作硬件，底层驱动只负责对接具体硬件。这种设计使得**上层应用代码可在不同 MCU 间无缝迁移**，仅需适配底层驱动，大幅降低学习成本和碎片化开发。

### 1.2 设备分类与核心结构

**设备类型划分**：
- **字符设备**：串口、键盘等，支持字节流顺序读写
- **块设备**：硬盘、Flash 等，支持随机寻址读写

**核心数据结构 `rt_device`**：
```c
struct rt_device
{
    enum rt_device_class_type type;    /* 设备类型 */
    rt_uint16_t flag;                  /* 打开标志 */
    rt_uint16_t open_count;            /* 引用计数 */
    /* ... 其他字段 ... */
    const struct rt_device_ops *ops;   /* 操作函数集 */
};
```
通过继承机制实现面向对象编程：自定义设备结构体以 `struct rt_device` 开头，后面追加私有字段。

**设备管理机制**：设备通过 `rt_device_register` 注册到 IO 管理系统的链表中，应用层通过 `rt_device_find` 按名称查找并打开设备。可通过 MSH 命令 `list_device` 查看系统中所有已注册设备。

### 1.3 IO 设备管理 API 调用流程

设备从创建到调用的完整生命周期：

```
rt_device_create()  →  分配设备对象内存（含私有结构）
       ↓
绑定 ops 函数指针    →  dev->init/open/read/write/control = my_xxx
       ↓
rt_device_register() →  挂入内核设备链表
       ↓
rt_device_find()     →  应用层按名称查找
       ↓
rt_device_open()     →  触发 dev->init + dev->open，引用计数 +1
       ↓
rt_device_read/write/control()  →  分发到对应 ops
       ↓
rt_device_close()    →  引用计数 -1，归零触发 dev->close
```

系统通过**引用计数**管理设备的打开次数，多次 open 需要 close 同样次数才真正关闭。

### 1.4 回调机制与中断处理

- `rt_device_set_rx_indicate`：设置接收回调，在中断/DMA 接收到数据时通知上层
- `rt_device_set_tx_complete`：设置发送完成回调

**回调链传递**：硬件中断 → 底层驱动回调 → RT-Thread 框架层回调 → 上层应用通知。

---

## 二、GPIO 外设开发与实操演示

### 2.1 PIN 设备 API

RT-Thread 将 GPIO 抽象为 PIN 设备，提供精简的 C 风格 API（不走 rt_device 流程）：

| API | 功能 |
|-----|------|
| `rt_pin_mode(pin, mode)` | 配置引脚模式（输入上拉/下拉、输出、开漏输出等） |
| `rt_pin_write(pin, value)` | 设置输出电平（PIN_HIGH / PIN_LOW） |
| `rt_pin_read(pin)` | 读取引脚电平 |
| `rt_pin_attach_irq(pin, mode, cb, args)` | 绑定引脚中断回调（上升沿/下降沿/双边沿） |
| `rt_pin_irq_enable(pin, enable)` | 使能/失能引脚中断 |

### 2.2 按键消抖问题

实操中发现按键触发两次中断——这是机械抖动导致。解决方案：
- 使用软件包（如 button 库）做状态机消抖
- 或在中断回调中加延时后再次读取电平确认

### 2.3 调试与源码分析

通过单步调试分析了 GPIO 驱动从上层 API 调用到底层 STM32 HAL 库的完整调用栈，验证了 ops 函数指针的正确赋值与分发。

---

## 三、PIN 设备对接分析（从底层驱动到 IO 管理层）

### 3.1 对接链路全景

以星火 1 号（STM32F407）为例，PIN 设备的完整对接链路如下：

```
应用层：rt_pin_write(GET_PIN(B, 5), PIN_HIGH)
          ↓
PIN 抽象层：rt_pin_ops->pin_write(stm32_pin_ops.pin_write)
          ↓
BSP 驱动层：stm32_pin_write() → HAL_GPIO_WritePin()
          ↓
硬件层：GPIOB->BSRR = GPIO_PIN_5  (寄存器写)
```

### 3.2 关键文件与函数

| 层次 | 文件 | 关键函数/结构 |
|------|------|--------------|
| PIN 抽象层 | `components/drivers/misc/pin.c` | `rt_pin_write()` → 通过 `static struct rt_pin_ops *_pin_ops` 分发 |
| BSP 驱动层 | `drivers/drv_gpio.c` | `stm32_pin_ops` 结构体，绑定 `pin_mode/pin_write/pin_read/pin_attach_irq` 等 |
| 硬件适配 | `drivers/drv_gpio.c` | 内部调用 `HAL_GPIO_WritePin()` / `HAL_GPIO_ReadPin()` |
| 引脚映射 | `drivers/drv_gpio.c` | `pins[]` 数组：将 RT-Thread 逻辑引脚号映射到 STM32 GPIOx + GPIO_PIN_x |

### 3.3 引脚编号机制

星火 1 号使用 `GET_PIN(port, pin)` 宏将物理引脚转换为逻辑编号：

```c
/* GET_PIN(B, 5) = 16*1 + 5 = 21 */
#define GET_PIN(PORTx, PIN)     (rt_base_t)((16 * (PORTx - 'A')) + PIN)
```

BSP 驱动层维护 `pins[]` 数组，将逻辑编号映射到 `{GPIOB, GPIO_PIN_5}`，`stm32_pin_write` 查表后调用 HAL 库操作对应寄存器。

### 3.4 中断对接

`rt_pin_attach_irq` 的实现链路：
1. 应用层调用 `rt_pin_attach_irq(pin, mode, callback, args)`
2. PIN 抽象层转发到 `stm32_pin_attach_irq`
3. BSP 驱动层将回调存入 `pin_irq_hdr_tab[pin]`，并配置 STM32 EXTI 中断线
4. 硬件中断触发 → `stm32_pin_exti_irq` 查表调用 `pin_irq_hdr_tab[pin].hdr`

### 3.5 设计要点总结

- **ops 函数指针集**是驱动对接的核心机制：BSP 驱动实现具体函数，通过结构体赋值挂到 PIN 抽象层的函数指针上
- **逻辑编号解耦**：上层用 `GET_PIN` 生成逻辑号，不关心 GPIO 端口/引脚的具体寄存器地址
- **中断回调表**：BSP 驱动维护 `pin_irq_hdr_tab`，硬件 EXTI 中断到应用层回调通过查表完成分发

---

## 四、I2C 与 SPI 总线驱动开发

### 4.1 I2C 总线通信

| 要点 | 说明 |
|------|------|
| 消息结构体 | `struct rt_i2c_msg`：含设备地址、读写标志、缓冲区指针及长度 |
| 地址格式 | 7 位地址，传输时左移一位，末尾补读写位（0 写 / 1 读） |
| 总线锁死恢复 | 产生 9 个 SCL 时钟脉冲恢复总线 |
| 软硬件支持 | 部分芯片支持包仅软件模拟 I2C（Soft I2C），硬件 I2C 需手动更新驱动 |

### 4.2 SPI 总线设备挂载

SPI 采用**总线-设备模型**：
1. 先注册 SPI 总线控制器（`rt_spi_bus_register`）
2. 再将具体设备（如 Flash）挂载到总线上（`rt_spi_device_attach`）

**设备命名规则**：`spixy`，x 为总线号，y 为设备序号（如 `spi10` = 总线 1 上的 0 号设备）。

**配置参数**：时钟频率、数据宽度（8/16 位）、传输模式（CPOL/CPHA）、数据位序（MSB/LSB）。

---

## 五、虚拟设备驱动框架（test_dev）设计

### 5.1 设计目标

设计一个**虚拟字符设备** `test_dev`，不对应任何真实硬件，数据只在内存缓冲区中流转，演示 RT-Thread 设备驱动框架的标准对接流程。

### 5.2 驱动层（test_drv.c）

```c
struct test_device
{
    struct rt_device parent;      /* RT-Thread 设备基类，必须放第一 */
    rt_uint8_t  buf[64];          /* 模拟设备内部数据缓冲区 */
    rt_size_t   data_len;         /* 当前有效数据长度 */
    rt_uint32_t magic;            /* 魔数，init 时校验 */
};
```

- `rt_device_create(RT_Device_Class_Char, sizeof(struct test_device))` 动态创建设备对象
- 绑定 6 个 ops：`init / open / close / read / write / control`
- `rt_device_register(dev, "test_dev", RT_DEVICE_FLAG_RDWR)` 注册进内核设备链表
- `INIT_DEVICE_EXPORT` 在设备初始化阶段自动注册

### 5.3 应用层（test_dev.c）

MSH 命令 `test_dev_demo` 演示完整生命周期：

```
find("test_dev") → open(RDWR) → write("Hello...") → read → control(GET_VERSION)
→ control(ECHO) → control(CLEAR_BUFFER) → read(验证清空) → close
```

### 5.4 自定义控制命令

避开内核已用值（0x01~0x04 / 0x10~0x12），从 0x20 起分配：

| 命令 | 值 | 功能 |
|------|----|------|
| `TEST_DEV_CTRL_GET_VERSION` | 0x20 | 获取设备版本字符串 |
| `TEST_DEV_CTRL_CLEAR_BUFFER` | 0x21 | 清空设备缓冲区 |
| `TEST_DEV_CTRL_ECHO` | 0x22 | 回显当前数据长度 |

> 注：本版 RT-Thread 无 `RT_DEVICE_CTRL_USER_ALLOC` 宏，自定义命令直接使用不与内核冲突的十六进制值。

### 5.5 预期串口输出

```
msh >test_dev_demo

====== test_dev Demo ======
[1] find: OK -> test_dev
[test_dev] open, oflag=0x0202
[2] open: OK
[test_dev] write: pos=0 size=30 stored=30
[3] write: sent=30 ret=30
[test_dev] read: pos=0 size=64 copied=30
[4] read: got=30 bytes, data="Hello RT-Thread virtual device!"
[test_dev] control: GET_VERSION=v1.0.0-lry
[5] control GET_VERSION: ret=0 version=v1.0.0-lry
[test_dev] control: echo received, len=30
[6] control ECHO: ret=0
[test_dev] control: buffer cleared
[7] control CLEAR_BUFFER: ret=0
[test_dev] read: buffer empty
[8] read after clear: got=0 bytes (expect 0)
[test_dev] close
[9] close: OK
====== test_dev Demo Done ======
```

### 5.6 编译信息

- 本地 scons 编译通过（arm-none-eabi-gcc 10.3）
- text=80652 / data=1524 / bss=3488，0 error

---

## 六、两种开发模式对比

| 对比项 | 基于芯片（BSP）模式 | 基于 SDK（CubeMX）模式 |
|--------|---------------------|------------------------|
| 适用场景 | 多种芯片适配 | 特定芯片型号 |
| 配置方式 | 手动 `board.h` / `board.c` 配置引脚和时钟 | CubeMX 生成配置文件，自动化程度高 |
| 流程复杂度 | 相对繁琐 | 相对简洁 |

---

## 七、提交文件清单

| 文件 | 说明 |
|------|------|
| `README.md` | 本文件：课堂笔记 + PIN 对接分析 + test_dev 设计说明 |
| `test_drv.c` | 虚拟设备驱动层：rt_device_create + ops 绑定 + register + INIT_DEVICE_EXPORT |
| `test_dev.c` | 应用层：test_dev_demo MSH 命令，完整走通 find→open→write→read→control→close |

---

## 八、小结

RT-Thread 设备驱动框架通过分层抽象（应用层 / IO 管理层 / 驱动层 / 硬件层），实现了上层应用与底层硬件的解耦。核心机制是 `rt_device` 结构体 + ops 函数指针集：驱动层实现具体函数并挂到设备对象上，IO 管理层维护设备链表并分发调用，应用层只需 `find/open/read/write/close` 即可操作任意设备。

本次通过设计虚拟字符设备 `test_dev`，完整走通了「驱动注册 → 应用查找 → 读写控制 → 关闭」的全流程，直观验证了框架的解耦能力。PIN 设备的对接分析则展示了从 `rt_pin_write` 到 STM32 HAL 库寄存器操作的完整链路，加深了对 ops 机制和逻辑编号解耦的理解。
