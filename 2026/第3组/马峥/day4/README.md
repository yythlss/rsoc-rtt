# RT-Thread 设备驱动框架 — DAY4 作业

- **目标平台**：STM32F407 星火号（`stm32f407-rtspark`）
- **学生**：马峥 | 第3组

---

## 一、RT-Thread 设备驱动框架概述

### 1.1 三层架构

RT-Thread 的设备驱动框架分为三层：

```
┌─────────────────────────────────────────┐
│     应用层（Application Layer）          │  rt_device_open/read/write/control
├─────────────────────────────────────────┤
│     设备驱动框架层（Device Framework）   │  rt_device_create/register + 具体设备类
│  PIN框架 / I2C框架 / SPI框架 / UART框架  │
├─────────────────────────────────────────┤
│     IO设备管理层（I/O Manager）          │  rt_device_t 对象链表、统一接口
├─────────────────────────────────────────┤
│     板级驱动层（BSP / HAL）             │  GPIO/I2C/SPI 寄存器操作
└─────────────────────────────────────────┘
```

**核心对象**：`struct rt_device`，包含：
```c
struct rt_device
{
    struct rt_object parent;        // 继承自内核对象（链表节点 + 名称）
    rt_uint16_t   flag;            // 设备标志（读写/只读等）
    rt_uint16_t   open_flag;       // 当前打开标志
    rt_uint8_t    ref_count;       // 引用计数
    rt_uint8_t    device_id;       // 设备编号

    /* 设备类型 */
    enum rt_device_class_type type;

    /* 操作接口（函数指针） */
    rt_err_t  (*init)   (rt_device_t dev);
    rt_err_t  (*open)   (rt_device_t dev, rt_uint16_t oflag);
    rt_err_t  (*close)  (rt_device_t dev);
    rt_ssize_t (*read)  (rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);
    rt_ssize_t (*write) (rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
    rt_err_t  (*control)(rt_device_t dev, int cmd, void *args);

    void *user_data;  // 私有数据（如 I2C 地址、SPI 配置等）
};
```

### 1.2 设备注册与使用流程

```
应用层                            驱动层
  │                                │
  │  rt_device_create()             │
  │ ──────────────────────────────► │ 创建 struct rt_device 对象
  │                                │
  │  dev->init/open/read/write = xxx│ 绑定操作函数指针
  │ ──────────────────────────────► │
  │                                │
  │  rt_device_register()           │
  │ ──────────────────────────────► │ 加入内核设备链表
  │                                │
  │  rt_device_find("name")        │
  │ ◄────────────────────────────── │ 返回 rt_device_t 句柄
  │                                │
  │  rt_device_open(handle, flag)   │
  │ ◄────────────────────────────── │ → open(dev, oflag)
  │                                │
  │  rt_device_read/write/control   │
  │ ◄────────────────────────────── │ → 对应函数指针
```

---

## 二、PIN 设备框架深度解析

### 2.1 PIN 设备在驱动框架中的位置

PIN 设备（`rt_pin_*`）是 RT-Thread 设备驱动框架中**最简化的字符设备**特例，
其底层通过 `rt_device` 统一管理层接入内核：

```
应用层（rt_pin_mode/write/read）
         │
         ▼
  PIN 设备框架层（pin.c）
    ├─ pin_ops 结构体（操作函数指针）
    └─ 绑定到 rt_device
         │
         ▼
  板级 BSP（drv_gpio.c / HAL）
    └─ 实际操作 GPIO 寄存器（BSRR/IDR/LCKR）
```

### 2.2 PIN 设备 API 与 rt_device 的对应关系

| `rt_pin_*` API | 底层对应 `rt_device_*` | 说明 |
|---|---|---|
| `rt_pin_mode(pin, mode)` | 驱动层设置 | 配置输入/输出/上拉 |
| `rt_pin_write(pin, value)` | `write()` | 输出高/低电平 |
| `rt_pin_read(pin)` | `read()` | 读取 GPIO IDR 寄存器 |
| `rt_pin_attach_irq(pin, mode, cb, args)` | 中断绑定 | 中断回调函数挂接 |
| `rt_pin_irq_enable(pin, enable)` | 中断使能 | 使能/失能 GPIO 中断 |

### 2.3 PIN 中断流程

```
外部中断触发（EXTI）
    │
    ▼
drv_gpio.c 中断服务函数（GPIOx_IRQHandler）
    │
    ▼
rt_pin_irq_hdr[pin].hdr(rt_pin_irq_hdr[pin].args)  // 查表调用用户回调
```

`GET_PIN(port, pin)` 宏通过 `((port) * 16 + (pin))` 计算引脚编号，
在 `rt_pin_irq_hdr[]` 数组中索引对应的中断处理项。

### 2.4 PIN 框架关键代码路径

```
rtdevice.h
  └─ rt_pin_mode / rt_pin_write / rt_pin_read
        │
        ▼
  pin.c（PIN 框架层）
    └─ rt_device_find("pin") → 获取 pin_device
        └─ pin_device->ops->xxx() 调用 BSP 实现
              │
              ▼
        drv_gpio.c（STM32 HAL 实现）
          └─ HAL_GPIO_Init() / HAL_GPIO_WritePin() / HAL_GPIO_ReadPin()
```

---

## 三、I2C 总线设备框架

### 3.1 I2C 框架三层模型

```
应用层：rt_i2c_transfer(bus, msgs, num)
           │
           ▼
      I2C 核心层（i2c_core.c）
           │
           ▼
      I2C 总线驱动层（drv_i2c.c → HAL_I2C_*）
```

### 3.2 I2C 消息结构

```c
struct rt_i2c_msg {
    rt_uint16_t addr;    // 从设备地址（7bit，底层自动左移1位）
    rt_uint16_t flags;   // RT_I2C_WR（写）或 RT_I2C_RD（读）
    rt_uint16_t len;     // 数据长度
    rt_uint8_t *buf;     // 数据缓冲区
};
```

### 3.3 典型读写操作

```c
/* 单字节写 */
msgs.addr = 0x68;
msgs.flags = RT_I2C_WR;
msgs.buf = buf;
msgs.len = 1;
rt_i2c_transfer(i2c_bus, &msgs, 1);   // 发送 1 条消息

/* 单字节读（先写寄存器地址，再读数据）*/
msgs[0] = { .addr=0x68, .flags=RT_I2C_WR, .buf=reg, .len=1 };
msgs[1] = { .addr=0x68, .flags=RT_I2C_RD, .buf=data, .len=1 };
rt_i2c_transfer(i2c_bus, msgs, 2);    // 发送 2 条消息（写+读）
```

---

## 四、SPI 总线设备框架

### 4.1 SPI 框架三层模型

```
应用层：rt_spi_transfer/send/recv
           │
           ▼
      SPI 核心层（spi_core.c）
           │
           ▼
      SPI 总线驱动层（drv_spi.c → HAL_SPI_*）
```

### 4.2 SPI 设备挂载流程

```c
/* BSP 驱动中挂载 SPI 设备到指定 GPIO 片选 */
rt_hw_spi_device_attach("spi2", "spi20", GET_PIN(B, 12));
// 参数：总线名 "spi2"，设备名 "spi20"，CS 引脚 PB12
```

### 4.3 SPI 配置与传输

```c
struct rt_spi_configuration cfg;
cfg.data_width = 8;                          // 数据位宽
cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0;     // 主机模式，CPOL=0/CPHA=0
cfg.max_hz = 1 * 1000 * 1000;              // 1MHz
rt_spi_configure(spi_dev, &cfg);

rt_spi_transfer(spi_dev, send_buf, recv_buf, len);  // 全双工
rt_spi_send(spi_dev, buf, len);                     // 仅发送
rt_spi_recv(spi_dev, buf, len);                     // 仅接收
```

---

## 五、虚拟设备驱动框架设计（test_dev）

### 5.1 设计目标

设计一个虚拟字符设备 `test_dev`，模拟传感器驱动的典型读写/控制流程，
完整走通 RT-Thread 设备驱动框架的三层架构。

驱动层 (`test_drv.c`) 使用 `rt_device_create()` 动态创建设备（参考课程 `dev_test.c` 模式），
应用层 (`test_dev.c`) 通过 `rt_device_find()` 查找并使用设备。

### 5.2 私有设备结构

```c
struct test_device {
    struct rt_device parent;    // 继承 rt_device，必须放第一个
    rt_uint8_t buffer[64];      // 模拟数据缓冲区
    rt_size_t data_len;         // 有效数据长度
    rt_uint32_t magic;          // 魔数 0xDEADBEEF
};
```

> **要点**：私有结构必须以 `struct rt_device` 开头，`rt_device_create()` 传入的 size 会
> 包含这部分空间，通过 `(struct test_device *)dev` 可安全访问私有字段。

### 5.3 设备控制命令

| 命令 | 值 | 功能 |
|------|----|------|
| `RT_DEVICE_CTRL_CONFIG` | 内核定义 | 标准配置命令 |
| `TEST_DEV_CTRL_GET_VERSION` | `RT_DEVICE_CTRL_USER_ALLOC + 1` | 获取设备版本字符串 |
| `TEST_DEV_CTRL_CLEAR_BUFFER` | `RT_DEVICE_CTRL_USER_ALLOC + 2` | 清空内部缓冲区 |
| `TEST_DEV_CTRL_ECHO` | `RT_DEVICE_CTRL_USER_ALLOC + 3` | Echo 回显测试 |

### 5.4 源码文件

| 文件 | 说明 |
|------|------|
| `test_drv.c` | 驱动层：`INIT_DEVICE_EXPORT()` 自动初始化，`rt_device_create/register`，绑定 6 个 ops 函数 |
| `test_dev.c` | 应用层：`test_dev_demo` MSH 命令，演示完整的 find→open→write→read→control→close 流程 |

### 5.5 关键设计细节

**防重复注册**（驱动层）：
```c
if (rt_device_find(TEST_DEV_NAME) != RT_NULL) {
    return RT_EOK;  // 设备已存在，直接跳过
}
```

**回调绑定**（驱动层）：
```c
dev->init    = drv_test_init;
dev->open    = drv_test_open;
dev->close   = drv_test_close;
dev->read    = drv_test_read;
dev->write   = drv_test_write;
dev->control = drv_test_control;
```

**应用层使用流程**（test_dev.c）：
```
rt_device_find("test_dev")     // 1. 查找设备
rt_device_open(dev, RDWR)      // 2. 打开设备（触发 drv_test_open）
rt_device_write(dev, ...)       // 3. 写入数据（触发 drv_test_write）
rt_device_read(dev, ...)       // 4. 读取数据（触发 drv_test_read）
rt_device_control(dev, cmd, ..) // 5. 控制命令（触发 drv_test_control）
rt_device_close(dev)            // 6. 关闭设备（触发 drv_test_close）
```

---

## 六、MSH 命令

| 命令 | 说明 |
|------|------|
| `test_dev_demo` | 虚拟设备完整测试（open→write→read→control→close），共 9 步演示 |
| `irq_key_enable` | 按键中断使能（四方向键 PC0/1/4/5） |

---

## 七、编译与运行

```bash
# 编译（在 RT-Thread Studio 中 Ctrl+B 或使用 scons）
# 编译成功后，在 MSH 中运行：

test_dev_demo

# 预期输出：
# [1] Find device: OK -> test_dev
# [2] Open device: OK
# [3] Write: sent=22 bytes, ret=22
# [4] Read: got=22 bytes, data="Hello RT-Thread Device!"
# [5] Control GET_VERSION: ret=0, version=v1.0.0
# [6] Control ECHO: ret=0
# [7] Control CLEAR_BUFFER: ret=0
# [8] Read after clear: got=0 bytes (should be 0)
# [9] Close device: OK
```

---

## 八、提交说明

```
2026/第3组/马峥/day4/
├── README.md       # 课堂笔记 + 驱动框架分析（本文档）
├── test_drv.c      # 虚拟驱动层源码（INIT_DEVICE_EXPORT 自动初始化）
└── test_dev.c      # 应用层测试源码（test_dev_demo MSH 命令）
```

---

## 九、PIN 设备对接分析（课堂补充）

### 9.1 PIN 设备在 RT-Thread 中的定位

PIN 设备在 RT-Thread 中**不是**标准的 `rt_device` 子类，而是通过以下方式接入设备框架：

```
rt_pin_xxx() API
       │
       ▼
rtdevice.h 中 pin 相关的内联函数
       │
       ▼
rt_device_find("pin")  ──►  pin_device（已由 BSP 注册）
       │
       ▼
pin_device->ops->xxx()  ──► drv_gpio.c 中的 BSP 实现
       │
       ▼
HAL_GPIO_Init() / HAL_GPIO_WritePin() / HAL_GPIO_ReadPin()
```

`pin_device` 由 BSP 的 `drv_gpio.c` 在 `INIT_DEVICE_EXPORT(rt_pin_init)` 时注册，
应用层通过 `rt_pin_*` 系列函数间接使用标准的 GPIO 外设。

### 9.2 PIN 中断的查表机制

RT-Thread PIN 中断使用**静态数组**实现，结构如下：

```c
// rtconfig.h 中定义最大引脚数
#define RT_NUMBER_OF_PIN  ...

// pin.c 中的中断表
struct rt_pin_irq_hdr {
    rt_uint16_t        pin;
    rt_uint16_t        mode;       // PIN_IRQ_MODE_FALLING / RISING / etc.
    void              (*hdr)(void *args);
    void               *args;
};
static struct rt_pin_irq_hdr rt_pin_irq_hdr_tab[RT_NUMBER_OF_PIN];
```

**中断触发流程**：
```
外部中断 EXTI（PA0~PG15）
    │
    ▼
drv_gpio.c 中的 stm32_gpio_exti_irqhandler()
    │
    ▼
rt_pin_irq_handler(pin)  // 找到 rt_pin_irq_hdr_tab[pin]
    │
    ▼
rt_pin_irq_hdr_tab[pin].hdr(rt_pin_irq_hdr_tab[pin].args)  // 执行用户回调
```

### 9.3 GET_PIN 宏与引脚编号

```c
#define GET_PIN(port, pin)  ((port) * 16 + (pin))
// 例如：GET_PIN(C, 5) = (2 * 16 + 5) = 37  →  PC5
```

> **注意**：port 按字母顺序编号（A=0, B=1, C=2...），pin 为引脚号（0~15）。
> 这个编号同时用作 `rt_pin_irq_hdr_tab[]` 数组的索引。

### 9.4 PIN 模式对照表

| `rt_pin_mode()` 参数 | 说明 |
|---|---|
| `PIN_MODE_INPUT` | 浮空输入 |
| `PIN_MODE_INPUT_PULLUP` | 输入 + 上拉 |
| `PIN_MODE_INPUT_PULLDOWN` | 输入 + 下拉 |
| `PIN_MODE_OUTPUT` | 推挽输出（初始低） |
| `PIN_MODE_OUTPUT_OD` | 开漏输出 |

### 9.5 PIN 设备与 rt_device 的关系总结

| 方面 | PIN 设备 | 普通字符设备 |
|------|------|------|
| 注册方式 | `INIT_DEVICE_EXPORT(rt_pin_init)` | `rt_device_create() + register()` |
| 应用 API | `rt_pin_read/write/mode` | `rt_device_read/write/open` |
| 中断支持 | 查表 `rt_pin_irq_hdr_tab[]` | 需自行实现 |
| 设备名 | `"pin"`（固定） | 开发者自定义 |
| 复用场景 | 仅 GPIO | 可扩展为传感器、总线等任意外设 |

**核心理解**：PIN 设备是 RT-Thread 设备驱动框架的一个**特例**——它直接暴露 `rt_pin_*` API，
而没有通过 `rt_device_*` 的通用接口。这是一种简化设计，在对实时性要求高的 GPIO 场景下
减少了一层函数调用开销。
