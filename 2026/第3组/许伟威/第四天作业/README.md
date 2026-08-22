# 【DAY4】RT-Thread 设备驱动框架与 IO 外设开发

> 目标平台：STM32F407 星火号（`stm32f407-rtspark`）
> 提交仓库：https://gitee.com/rtthread/rsoc-rtt
> 提交人：`许伟威` · 第3组

---

## 一、课堂笔记

### 1.1 设备驱动框架架构与分层设计

不同芯片厂商的 API 各不相同，直接调用会导致代码复用率低。RT-Thread 的思路是**分层抽象**：

```
┌─────────────────────────────────┐
│          上层应用（统一 API）      │
├─────────────────────────────────┤
│      IO 设备管理层（open/read/...）│
├─────────────────────────────────┤
│      设备驱动框架层（rt_device）   │
├─────────────────────────────────┤
│  底层驱动（对接具体硬件/HAL/寄存器） │
└─────────────────────────────────┘
```

- **硬件抽象层**：把 SPI、I2C、串口等抽象成统一设备，上层用同一套 API，底层驱动对接具体硬件，实现驱动与应用分离。
- **代码复用与迁移**：应用代码可以在不同 MCU 间迁移，只改底层驱动即可。
- **架构演进**：从最初针对 SPI 的统一 API，演进出统一的 IO 设备管理层，用 `open/write/read/close` 等标准接口操作硬件。

### 1.2 设备分类与核心结构

| 类型 | 说明 | 例子 |
|------|------|------|
| 字符设备 | 字节流顺序读写 | 串口、键盘 |
| 块设备 | 随机寻址读写 | 硬盘、Flash |

核心结构 `rt_device` 包含设备类型、参数、打开标志、引用计数和操作函数集（ops），通过"继承"实现面向对象。设备用 `rt_device_register` 挂到 IO 管理链表，应用层用 `rt_device_find` 查找再打开。

### 1.3 IO 设备管理与 API 调用流程

- **创建与注册**：`rt_device_create` 创建句柄，`rt_device_register` 挂到系统链表，可用 `list_device` 查看。
- **初始化与销毁**：`rt_device_init` 初始化，`rt_device_close` 关闭，系统用引用计数管理打开次数。
- **回调机制**：`rt_device_set_rx_indicate` / `rt_device_set_tx_complete` 设置收发回调，中断或 DMA 模式下通知上层。硬件中断 → 底层回调 → 框架层回调 → 上层应用。

### 1.4 GPIO 外设开发

- 引脚模式：输入（上拉/下拉）、输出、开漏等，用 `rt_pin_mode` 配置。
- 中断回调：`rt_pin_attach_irq` 绑定回调，支持上升沿/下降沿/双边沿。
- 电平读写：`rt_pin_write` / `rt_pin_read`。
- 按键消抖：机械抖动会触发多次中断，可用 button 库或延时消抖。

### 1.5 I2C 与 SPI 总线驱动

- **I2C**：用 `struct rt_i2c_msg`（设备地址、读写标志、缓冲区、长度）传输；总线锁死时可用 9 个 SCL 时钟脉冲恢复；7 位地址左移一位后补读写位（0 写 1 读）。
- **SPI**：总线-设备模型，先注册 SPI 控制器，再把设备挂到总线上；命名 `spixy`（x 总线号，y 设备序号）；配置项有时钟频率、数据宽度、CPOL/CPHA、MSB/LSB 等。

### 1.6 两种开发模式

| 模式 | 适用 | 特点 |
|------|------|------|
| 基于芯片（BSP） | 多种芯片适配 | 手动配 `board.h`/`board.c`，流程繁琐 |
| 基于 SDK（CubeMX） | 特定芯片型号 | 直接用 CubeMX 配置，自动化程度高 |

部分芯片支持包只支持软件模拟 I2C，硬件 I2C 需手动更新驱动或换主线版本。

---

## 二、PIN 设备对接分析

PIN（GPIO）设备是理解 RT-Thread 驱动框架最直观的例子。它不是走标准的 `read/write`，而是通过设备对象里的 `pin_ops` 函数指针直接转发到底层驱动。

### 2.1 完整调用链路

以"点亮 PF12 的红色 LED"为例：

```
应用层：
    rt_pin_write(GET_PIN(F, 12), PIN_HIGH);
        │
        ▼
PIN 框架层（pin.c）：
    _hw_pin.ops->pin_write(&_hw_pin.parent, pin, value);
        │  通过函数指针转发
        ▼
底层驱动层（drv_gpio.c）：
    stm32_pin_write(...) -> HAL_GPIO_WritePin(GPIOF, GPIO_PIN_12, GPIO_PIN_SET);
        │
        ▼
STM32 HAL 库 / 寄存器：
    操作 GPIOF->BSRR 寄存器，引脚输出高电平
```

### 2.2 各层职责

**① 上层 API（`components/drivers/include/drivers/pin.h`）**

对应用暴露的函数全部是标准接口：

```c
void rt_pin_mode(rt_base_t pin, rt_base_t mode);
void rt_pin_write(rt_base_t pin, rt_base_t value);
int  rt_pin_read(rt_base_t pin);
rt_err_t rt_pin_attach_irq(rt_int32_t pin, rt_uint32_t mode,
                           void (*hdr)(void *args), void *args);
rt_err_t rt_pin_detach_irq(rt_int32_t pin);
rt_err_t rt_pin_irq_enable(rt_base_t pin, rt_uint32_t enabled);
```

**② 引脚编号映射（`GET_PIN` 宏）**

`GET_PIN(端口, 引脚)` 把端口和引脚号编码成一个整数索引：

```c
#define GET_PIN(PORTx, PIN) \
    (rt_base_t)((16 * ((rt_base_t)__STM32_PORT(PORTx) - (rt_base_t)GPIOA_BASE) / 0x0400UL) + PIN)
```

例如 `GET_PIN(F, 12)` 得到的是一个数字索引，驱动层再把它反解成 GPIOF + PIN12。

**③ PIN 框架层（`components/drivers/misc/pin.c`）**

内部维护一个全局 pin 设备 `_hw_pin`，`rt_pin_write` 这类函数本身不做硬件操作，只做一件事——通过函数指针转发：

```c
void rt_pin_write(rt_base_t pin, rt_base_t value)
{
    RT_ASSERT(_hw_pin != RT_NULL);
    _hw_pin.ops->pin_write(&_hw_pin.parent, pin, value);
}
```

`_hw_pin.ops` 是 `struct rt_pin_ops` 函数指针集合，由底层驱动注册时填入。

**④ 底层驱动（`drv_gpio.c`）**

驱动实现 `struct rt_pin_ops` 的每个成员，并调用 `rt_device_pin_register` 注册：

```c
const static struct rt_pin_ops _stm32_pin_ops =
{
    stm32_pin_mode,
    stm32_pin_write,
    stm32_pin_read,
    stm32_pin_attach_irq,
    stm32_pin_detach_irq,
    stm32_pin_irq_enable,
    stm32_pin_get,
};

int rt_hw_pin_init(void)
{
    return rt_device_pin_register("pin", &_stm32_pin_ops, RT_NULL);
}
```

各函数最终调用 STM32 HAL 库（`HAL_GPIO_Init`、`HAL_GPIO_WritePin`、`HAL_GPIO_ReadPin` 等）。

### 2.3 对接关键点

1. **解耦**：应用只认 `rt_pin_*` 和引脚索引，不知道底层是 STM32 还是其他芯片。换芯片时只改 `drv_gpio.c` 里的 `pin_ops` 实现。
2. **函数指针桥接**：`struct rt_pin_ops` 把框架层和驱动层连起来，注册时一次性把驱动实现填入。
3. **注册时机**：`rt_hw_pin_init` 通过 `INIT_BOARD_EXPORT` 在板级初始化阶段执行，保证应用用引脚前驱动已就绪。
4. **与普通设备的区别**：PIN 设备不走 `rt_device_open/read/write` 那套，而是直接用 `pin_ops` 转发，调用更直接、开销更小。

---

## 三、虚拟设备驱动框架层 test_dev

### 3.1 设计目标

自己实现一个虚拟字符设备 `test_dev`，走完设备框架的完整生命周期，验证"上层统一 API + 底层 ops"的分层思想。

### 3.2 结构设计

```c
struct test_dev
{
    struct rt_device parent;    /* 继承基类 rt_device */
    rt_uint32_t value;          /* 设备内部数据 */
};
```

设备内部维护一个 32 位计数值：

| 接口 | 行为 |
|------|------|
| `init` | 计数值清零 |
| `open` / `close` | 打印打开/关闭信息 |
| `write` | 写入一个 32 位值作为初值 |
| `read` | 读出当前值并自增 |
| `control` | 支持重置、查询两个命令 |

### 3.3 对接流程

```
INIT_DEVICE_EXPORT 阶段：
    test_dev_register() 填好 ops -> rt_device_register("test_dev")

MSH 输入 test_dev_demo 时：
    rt_device_find("test_dev")
    -> rt_device_open(dev, RT_DEVICE_OFLAG_RDWR)   触发 init + open
    -> rt_device_write / read / control
    -> rt_device_close
```

### 3.4 运行现象

```text
test_dev registered
msh />test_dev_demo
test_dev init done
test_dev open, oflag=0x0003
write 100
read 100
read 101
test_dev value: 102
test_dev reset
test_dev value: 0
test_dev close
```

`write 100` 写入初值，两次 `read` 依次读出 100、101（每次读出后自增），`control GET` 显示当前值 102，`reset` 清零后再查为 0。

---

## 四、MSH 命令说明

| 命令 | 说明 |
|------|------|
| `test_dev_demo` | 演示虚拟设备完整调用流程 |
| `list_device` | 查看系统已注册设备（可见 test_dev） |

`test_dev.c` 编译进工程后，`test_dev` 会在启动阶段自动注册，可直接用 `list_device` 查看。
