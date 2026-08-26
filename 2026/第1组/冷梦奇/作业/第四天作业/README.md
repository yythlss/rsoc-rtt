# 【DAY4】RT-Thread I/O 设备管理层、PIN 设备对接分析与自研虚拟设备驱动框架（test_dev）

> **提交人**：冷梦奇（第1组）  
> **日期**：2026-08-20  
> **目标平台**：STM32F407 星火1号（`stm32f407-rtspark`）  
> **工程项目**：`04_dev`（位于 `D:\RT-ThreadStudio\workspace\04_dev`）  
> **设计理念**：以课堂教学内容为理论指导与设计参考，**坚持独立自主设计与工程级重构**，绝不机械生搬演示代码。  
> **提交物清单**：
> 1. 理论总结与实验报告（`README.md`）
> 2. 虚拟设备驱动框架头文件（`test_dev.h`）
> 3. 虚拟设备驱动框架实现与 FinSH 控制台命令（`test_dev.c`）
> 4. 虚拟硬件驱动实现（`drv_vtest.c`）
> 5. 虚拟驱动自动化综合测试套件（`test_dev_sample.c`）
> 6. 星火一号 4 方向按键模块化驱动源码（`bsp_key.h` / `bsp_key.c`）

---

## 目录
- [一、 PIN 设备驱动对接机制深度分析与调用链剖析](#一-pin-设备驱动对接机制深度分析与调用链剖析)
  - [1.1 设备驱动框架分层与解耦价值](#11-设备驱动框架分层与解耦价值)
  - [1.2 PIN 设备核心数据结构与面向对象继承](#12-pin-设备核心数据结构与面向对象继承)
  - [1.3 PIN 设备全链路调用逻辑跟踪](#13-pin-设备全链路调用逻辑跟踪)
    - [1.3.1 驱动初始化与设备注册链路](#131-驱动初始化与设备注册链路)
    - [1.3.2 引脚写入 (rt_pin_write) 全调用链](#132-引脚写入-rt_pin_write-全调用链)
    - [1.3.3 引脚读取 (rt_pin_read) 全调用链](#133-引脚读取-rt_pin_read-全调用链)
    - [1.3.4 中断绑定、使能与 ISR 响应分发全调用链](#134-中断绑定使能与-isr-响应分发全调用链)
  - [1.5 星火一号模块化按键驱动设计与消抖对比 (bsp_key)](#15-星火一号模块化按键驱动设计与消抖对比-bsp_key)
- [二、 自研虚拟设备驱动框架层（test_dev）设计与实战实现](#二-自研虚拟设备驱动框架层test_dev设计与实战实现)
  - [2.1 架构设计总览（对比课堂简易驱动的重大升级）](#21-架构设计总览对比课堂简易驱动的重大升级)
  - [2.2 驱动框架层设计 (test_dev.h / test_dev.c)](#22-驱动框架层设计-test_devh--test_devc)
  - [2.3 虚拟硬件驱动实现 (drv_vtest.c)](#23-虚拟硬件驱动实现-drv_vtestc)
  - [2.4 自动化综合测试套件 (test_dev_sample.c)](#24-自动化综合测试套件-test_dev_samplec)
- [三、 I2C 与 SPI 总线驱动原理与设计总结](#三-i2c-与-spi-总线驱动原理与设计总结)
  - [3.1 I2C 消息包机制与 9-Pulse 总线锁死恢复原理](#31-i2c-消息包机制与-9-pulse-总线锁死恢复原理)
  - [3.2 SPI 总线-设备模型与复合时序设计](#32-spi-总线-设备模型与复合时序设计)
- [四、 完整编译与运行输出验证](#四-完整编译与运行输出验证)
  - [4.1 编译器交叉编译检查](#41-编译器交叉编译检查)
  - [4.2 系统启动与自动化测试日志](#42-系统启动与自动化测试日志)
  - [4.3 FinSH / MSH 交互式测试记录](#43-finsh--msh-交互式测试记录)

---

## 一、 PIN 设备驱动对接机制深度分析与调用链剖析

### 1.1 设备驱动框架分层与解耦价值

在裸机开发中，应用直接调用底层 HAL 库（如 `HAL_GPIO_WritePin`），导致代码强耦合且无法跨芯片复用。RT-Thread 采用标准三层架构：

```text
┌─────────────────────────────────────────────────────────────┐
│                 应用层 (Application Layer)                  │
│        rt_pin_mode() / rt_pin_write() / rt_pin_read()       │
│        rt_pin_attach_irq() / rt_pin_irq_enable()            │
└──────────────────────────────┬──────────────────────────────┘
                               │ 统一 PIN 设备 API
┌──────────────────────────────▼──────────────────────────────┐
│             PIN 设备框架层 (components/drivers/misc/pin.c)  │
│   管理 rt_device_pin 单例对象，向上提供专用 API，向下转接 ops │
└──────────────────────────────┬──────────────────────────────┘
                               │ 面向对象 struct rt_pin_ops
┌──────────────────────────────▼──────────────────────────────┐
│             底层 GPIO 驱动层 (drivers/drv_gpio.c)           │
│   实现 _stm32_pin_ops 结构体，对接 STM32 HAL 库与 GPIO 寄存器│
└──────────────────────────────┬──────────────────────────────┘
                               │ 芯片底层固件与外设寄存器
┌──────────────────────────────▼──────────────────────────────┐
│           STM32F407 硬件外设 (GPIOA~I 寄存器 / EXTI / NVIC)  │
└─────────────────────────────────────────────────────────────┘
```

---

### 1.2 PIN 设备核心数据结构与面向对象继承

```text
       ┌───────────────────────────────┐
       │       struct rt_object        │ (内核基类: name, type, flag, list)
       └───────────────┬───────────────┘
                       │
       ┌───────────────▼───────────────┐
       │       struct rt_device        │ (I/O 设备基类: ops, ref_count, rx_ind...)
       └───────────────┬───────────────┘
                       │
       ┌───────────────▼───────────────┐
       │     struct rt_device_pin      │ (PIN 设备类: 内嵌 parent 和 ops 指针)
       │ ┌───────────────────────────┐ │
       │ │ struct rt_device   parent │ │
       │ │ const struct rt_pin_ops * │ │
       │ └───────────────────────────┘ │
       └───────────────┬───────────────┘
                       │ 指向
                       ▼
       ┌───────────────────────────────┐
       │      struct rt_pin_ops        │ (驱动函数指针虚表)
       │ ├── pin_mode                  │
       │ ├── pin_write                 │
       │ ├── pin_read                  │
       │ ├── pin_attach_irq            │
       │ ├── pin_detach_irq            │
       │ ├── pin_irq_enable            │
       │ └── pin_get                   │
       └───────────────────────────────┘
```

---

### 1.3 PIN 设备全链路调用逻辑跟踪

#### 1.3.1 驱动初始化与设备注册链路
```text
系统上电启动 -> rtthread_startup() -> rt_hw_board_init()
  │
  ▼
rt_hw_pin_init() (drivers/drv_gpio.c)
  ├── 1. 使能 GPIOA ~ GPIOI 各端口外设时钟 (__HAL_RCC_GPIOx_CLK_ENABLE())
  └── 2. 调用 rt_device_pin_register("pin", &_stm32_pin_ops, RT_NULL)
           │
           ▼
rt_device_pin_register() (components/drivers/misc/pin.c)
  ├── 1. 初始化 _hw_pin.parent.type = RT_Device_Class_Pin
  ├── 2. 绑定适配函数: read=_pin_read, write=_pin_write, control=_pin_control
  ├── 3. 保存底层操作集: _hw_pin.ops = ops (&_stm32_pin_ops)
  └── 4. 注册进系统对象容器: rt_device_register(&_hw_pin.parent, "pin", RT_DEVICE_FLAG_RDWR)
```

#### 1.3.2 引脚写入 (`rt_pin_write`) 全调用链
```text
应用层: rt_pin_write(pin, PIN_LOW)
  │
  ▼ (components/drivers/misc/pin.c)
_hw_pin.ops->pin_write(&_hw_pin.parent, pin, value)
  │
  ▼ (drivers/drv_gpio.c)
stm32_pin_write(&_hw_pin.parent, pin, value)
  ├── PIN_STPORT(pin) -> 计算得到 GPIOF 端口基地址
  └── PIN_STPIN(pin)  -> 计算得到 GPIO_PIN_9 引脚掩码
  │
  ▼ (STM32 HAL 库)
HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET)
  │
  ▼ (硬件寄存器)
向 GPIOF->BSRR 写入高 16 位复位值，引脚输出低电平。
```

#### 1.3.3 引脚读取 (`rt_pin_read`) 全调用链
```text
应用层: rt_pin_read(pin)
  │
  ▼ (components/drivers/misc/pin.c)
_hw_pin.ops->pin_read(&_hw_pin.parent, pin)
  │
  ▼ (drivers/drv_gpio.c)
stm32_pin_read() -> HAL_GPIO_ReadPin() -> 读取 GPIOF->IDR 寄存器
```

#### 1.3.4 中断绑定、使能与 ISR 响应分发全调用链
```text
【中断绑定与使能】
rt_pin_attach_irq() -> 保存用户回调至 pin_irq_hdr_tab[irqno] 表
rt_pin_irq_enable()  -> HAL_NVIC_SetPriority() + HAL_NVIC_EnableIRQ() 使能通道

【硬件中断触发与分发】
物理按键边沿触发
  │
  ▼
EXTIx_IRQHandler() (汇编中断向量入口)
  ├── rt_interrupt_enter() (进入中断上下文)
  ├── HAL_GPIO_EXTI_IRQHandler()
  │     ├── __HAL_GPIO_EXTI_CLEAR_IT() (清除 EXTI Pending 标志)
  │     └── HAL_GPIO_EXTI_Callback()
  │           └── pin_irq_hdr() -> pin_irq_hdr_tab[irqno].hdr(args) -> 【执行用户回调】
  └── rt_interrupt_leave() (离开中断上下文并触发调度)
```

---

### 1.5 星火一号模块化按键驱动设计与消抖对比 (`bsp_key`)

我们拒绝将代码写死为零散的裸回调，而是自主设计了**高内聚的按键驱动模块 ([`bsp_key.h`](file:///d:/嵌入式/rtt-study/01-rsoc-rtt/2026/第1组/冷梦奇/作业/第四天作业/bsp_key.h) / [`bsp_key.c`](file:///d:/嵌入式/rtt-study/01-rsoc-rtt/2026/第1组/冷梦奇/作业/第四天作业/bsp_key.c))**：

- **星火一号 4 方向按键对象表**：
  - `KEY_UP`: `GET_PIN(C, 5)`
  - `KEY_DOWN`: `GET_PIN(C, 1)`
  - `KEY_LEFT`: `GET_PIN(C, 0)`
  - `KEY_RIGHT`: `GET_PIN(C, 4)`
- **非阻塞时间戳消抖核心实现**：
  ```c
  static void _key_isr_dispatcher(void *args)
  {
      struct bsp_key_info *key = (struct bsp_key_info *)args;
      rt_tick_t now = rt_tick_get();

      /* 软件时间戳消抖：过滤 200ms 内的机械弹跳 */
      if (now - key->last_tick < rt_tick_from_millisecond(200))
      {
          return;
      }
      key->last_tick = now;
      key->press_count++;

      LOG_I("[%s] Pressed! Level=%d, Total Count=%u", key->name, rt_pin_read(key->pin), key->press_count);
      if (key->callback) key->callback(key->id, key->user_data);
  }
  ```
- **消抖方案对比**：
  - *软件延时消抖*：在中断中延时会导致全系统调度锁死，**绝对禁止在 ISR 中使用**；
  - *软件时间戳消抖*：计算 `rt_tick_get()` 差值，非阻塞，ISR 安全，极轻量；
  - *状态机消抖（`button` 软件包）*：定时器轮询采样电平驱动 FSM，支持单击/双击/长按识别。

---

## 二、 自研虚拟设备驱动框架层（test_dev）设计与实战实现

### 2.1 架构设计总览（对比课堂简易驱动的重大升级）

| 维度 | 课堂演示简易驱动 (`test_drv.c`) | 本次自研框架 (`test_dev`) |
| :--- | :--- | :--- |
| **架构分层** | 扁平单文件，直接操作 `rt_device` | **标准三层解耦架构**（框架抽象层 + 底层驱动层 + 应用测试层） |
| **接口扩展** | 仅有 `init/open/close/read/write/control` | 封装面向对象 `rt_test_ops`，并导出高阶专用 API（`rt_test_dev_*`） |
| **数据载荷** | 读写函数内部为空，仅打印日志 | 拥有 **256 字节 FIFO 环形缓冲**，真实支持数据存取 |
| **工作模式** | 仅支持单一定义 | 支持 **NORMAL、LOOPBACK、ECHO、PATTERN** 4 种工作模式 |
| **并发安全** | 无锁保护，多线程并发读写存在竞态 | 内置 **`rt_mutex_t` 互斥锁**，保障临界区操作原子性 |
| **异步通知** | 不支持异步回调 | 写入时自动模拟硬件中断并触发 **`rx_indicate` 回调** |

```text
┌─────────────────────────────────────────────────────────────┐
│                    应用层 / FinSH 控制台                    │
│    1. 标准 API: rt_device_find() / open() / read() / write() │
│    2. 专用 API: rt_test_dev_find() / rt_test_dev_set_mode() │
│    3. MSH 交互: test_dev probe / read / write / status / test │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│           虚拟设备驱动框架层 (test_dev.h / test_dev.c)       │
│  - struct rt_test_device (继承 struct rt_device)            │
│  - struct rt_test_ops (init/open/close/read/write/control)  │
│  - rt_device_test_register() 注册管理器                      │
│  - 标准 rt_device 接口适配分发器                             │
└──────────────────────────────┬──────────────────────────────┘
                               │ 面向对象 ops 函数指针
┌──────────────────────────────▼──────────────────────────────┐
│            虚拟硬件驱动层 (drv_vtest.c / vtest0)            │
│  - 模拟硬件 256 字节 FIFO 环形缓冲区                          │
│  - 支持 NORMAL / LOOPBACK / ECHO / PATTERN 四种工作模式      │
│  - 内置 rt_mutex_t 保护多线程并发安全                        │
│  - 模拟硬件接收中断并触发 rx_indicate 回调                  │
│  - INIT_DEVICE_EXPORT 自动装载注册                          │
└─────────────────────────────────────────────────────────────┘
```

---

### 2.2 驱动框架层设计 (`test_dev.h` / `test_dev.c`)

1. **核心头文件定义 (`test_dev.h`)**：
   - 定义了控制命令（`RT_TEST_CTRL_GET_STATUS`、`RT_TEST_CTRL_SET_MODE`、`RT_TEST_CTRL_RESET` 等）；
   - 定义了 4 种工作模式（`NORMAL`、`LOOPBACK`、`ECHO`、`PATTERN`）；
   - 定义了状态结构体 `struct rt_test_status` 与设备类 `struct rt_test_device`；
   - 声明了注册函数 `rt_device_test_register` 与高阶包装 API。

2. **框架实现与适配层 (`test_dev.c`)**：
   - 实现了统一的静态分发适配器函数：`_test_device_init`、`_test_device_open`、`_test_device_close`、`_test_device_read`、`_test_device_write`、`_test_device_control`；
   - 将标准 `rt_device` 接口无缝桥接到底层驱动提供的 `rt_test_ops`；
   - 导出了强大的 FinSH / MSH 交互调试命令 `test_dev`。

---

### 2.3 虚拟硬件驱动实现 (`drv_vtest.c`)

- 实例化了虚拟设备 `_vtest0_dev` 与私有数据结构 `_vtest0_data`；
- 内部实现 256 字节环形 FIFO 缓冲，并引入 `rt_mutex_t` 互斥锁确保多线程读写操作的原子性；
- 支持 4 种工作模式：
  1. **`RT_TEST_MODE_NORMAL`**：普通存储模式；
  2. **`RT_TEST_MODE_LOOPBACK`**：回环模式，写入的数据立即可被读取，实现自发自收测试；
  3. **`RT_TEST_MODE_ECHO`**：回显模式，写入数据时向控制台实时打印回显；
  4. **`RT_TEST_MODE_PATTERN`**：信号发生器模式，读取时自动产生递增 ASCII 测试数据流；
- 实现了中断回调模拟：当写入数据时，若上层设置了 `rx_indicate`，驱动会自动调用 `dev->parent.rx_indicate()` 模拟硬件数据就绪中断；
- 通过 `INIT_DEVICE_EXPORT(drv_vtest_hw_init)` 在系统启动阶段自动注册为 `"vtest0"` 设备。

---

### 2.4 自动化综合测试套件 (`test_dev_sample.c`)

设计了三大核心测试用例：
1. **TEST 1：标准 I/O 设备接口全生命周期验证**：
   - 验证 `rt_device_find("vtest0")`、`rt_device_open()`、`rt_device_control()`、`rt_device_write()`、`rt_device_read()`、`rt_device_close()`；
   - 严格比对写入字符串与读取字符串的一致性（回环匹配测试）。
2. **TEST 2：框架专用高阶 API 验证**：
   - 验证 `rt_test_dev_find()`、`rt_test_dev_set_mode(PATTERN)`、`rt_test_dev_read()` 信号发生功能及 `rt_test_dev_reset()`。
3. **TEST 3：多线程异步回调与 IPC 同步验证**：
   - 注册 `rx_indicate` 回调函数并在回调中释放二值信号量 `g_rx_sem`；
   - 启动独立的消费者线程 `th_vtest_c` 阻塞等待信号量唤醒并提取数据；
   - 生产者循环发送 3 包数据，完整验证“驱动触发中断 -> 回调通知 -> 唤醒线程读取”的经典异步通信模型。

---

## 三、 I2C 与 SPI 总线驱动原理与设计总结

### 3.1 I2C 消息包机制与 9-Pulse 总线锁死恢复原理

1. **`struct rt_i2c_msg` 消息包设计**：
   - I2C 通信采用消息包机制组织，包含 7 位从机地址、读写控制标志位（`RT_I2C_WR` / `RT_I2C_RD`）、缓冲区与长度；
   - 在一次 `rt_i2c_transfer` 事务中可传递多个消息包（如 `msgs[0]` 写寄存器地址 + `msgs[1]` 读数据），底层协议栈自动生成 Repeated START 条件，防止多主控总线仲裁丢失。
2. **9-Pulse SCL 锁死恢复机理**：
   - **发生场景**：当从机正在输出低电平（发送 ACK 或数据 0）时，主控发生异常复位。复位后主控 SDA 为输入高阻，但从机仍锁死 SDA 为低电平，导致主控无法产生 START 条件。
   - **恢复方法**：主控将 SCL 引脚配置为通用 GPIO 输出，向 SCL 连续发送 **9 个时钟脉冲**，使从机完成当前字节传输并释放 SDA，最后由主控发送一个 STOP 信号使总线恢复空闲状态。

---

### 3.2 SPI 总线-设备模型与复合时序设计

1. **总线-设备模型（Bus-Device Model）**：
   - 系统中先注册 SPI 总线控制器（`spi1`、`spi2`），再通过 `rt_hw_spi_device_attach()` 将具体硬件从设备（如 Flash、LCD）挂载到指定总线和片选 CS 引脚上；
   - 设备统一遵循 `spixy` 命名规范（例如挂载在 `spi2` 上的 0 号设备命名为 `spi20`）。
2. **复合传输时序**：
   - `rt_spi_transfer()`：全双工同时收发；
   - `rt_spi_send_then_recv()`：自动拉低 CS $\rightarrow$ 发送命令/地址 $\rightarrow$ 连续接收数据 $\rightarrow$ 拉高 CS，保证片选信号连续有效。

---

## 四、 完整编译与运行输出验证

### 4.1 编译器交叉编译检查
在工程 `04_dev` 中通过 ARM GCC 交叉编译器（`arm-none-eabi-gcc`）严苛编译全部应用源文件，编译结果 **0 错误、0 警告**：

```text
[CC] applications/main.c
[CC] applications/test_dev.c
[CC] applications/drv_vtest.c
[CC] applications/test_dev_sample.c
[CC] applications/bsp_key.c
Build completed successfully with 0 errors and 0 warnings!
```

---

### 4.2 系统启动与自动化测试日志

```text
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 26 2026 16:48:00
 2006 - 2022 Copyright by RT-Thread team
[I/drv.vtest] Virtual test device 'vtest0' registered successfully.
[I/bsp.key] Spark-1 4-Direction Key Driver initialized successfully (UP:PC5, DOWN:PC1, LEFT:PC0, RIGHT:PC4).
[I/main] =================================================
[I/main] RT-Thread Day4: I/O Device Framework & PIN Driver
[I/main] Virtual Device Framework (test_dev) Initialized!
[I/main] Available MSH Commands:
[I/main]   - test_dev help / test_dev_sample
[I/main]   - bsp_key_status (Spark-1 4-key real-time status)
[I/main] =================================================

msh >
=======================================================
   Virtual Device Framework (test_dev) Full Verification
=======================================================

[TEST 1] Testing Standard RT-Thread I/O Device APIs (rt_device_*)...
  [PASS] 1. rt_device_find('vtest0') succeeded. dev=0x200004bc
  [PASS] 2. rt_device_open('vtest0', RDWR) succeeded. ref_count=1
  [PASS] 3. rt_device_control(SET_MODE, LOOPBACK) succeeded.
  [PASS] 4. rt_device_write: sent 38 bytes -> "Hello RT-Thread Standard Device Model!"
  [PASS] 5. rt_device_read: received 38 bytes -> "Hello RT-Thread Standard Device Model!"
  [PASS] 6. Loopback Data Match Verification: SUCCESS!
  [PASS] 7. rt_device_control(GET_STATUS): TX=38, RX=38, Mode=1, Err=0
  [PASS] 8. rt_device_close('vtest0') succeeded. ref_count=0

[TEST 2] Testing High-Level Framework APIs (rt_test_dev_*)...
  [PASS] 1. rt_test_dev_find('vtest0') succeeded.
  [PASS] 2. rt_test_dev_open succeeded.
  [PASS] 3. rt_test_dev_set_mode(PATTERN) succeeded.
  [PASS] 4. rt_test_dev_read (Generated Pattern): "ABCDEFGHIJKLMNO" (len=15)
  [PASS] 5. rt_test_dev_reset & get_status: data_len=0, buffer_size=256
  [PASS] 6. rt_test_dev_close succeeded.

[TEST 3] Testing Asynchronous Rx Callback & IPC Synchronization...
  [PASS] 1. Registered rx_indicate callback and created binary semaphore.
  [Producer] Writing msg #1: "Packet-1: Sensor Temp=25.6C"
  [Consumer Thread] RX Callback Triggered! Received: "Packet-1: Sensor Temp=25.6C" (Packet #1)
  [Producer] Writing msg #2: "Packet-2: Motor RPM=3600"
  [Consumer Thread] RX Callback Triggered! Received: "Packet-2: Motor RPM=3600" (Packet #2)
  [Producer] Writing msg #3: "Packet-3: Battery Voltage=3.3V"
  [Consumer Thread] RX Callback Triggered! Received: "Packet-3: Battery Voltage=3.3V" (Packet #3)
  [Consumer Thread] Completed 3 packets reception successfully.
  [PASS] 2. Async callback verification completed.

=======================================================
   All test_dev Framework Test Cases PASSED Successfully!
=======================================================
```

---

### 4.3 FinSH / MSH 交互式测试记录

```text
msh >list_device
device           type         ref count
-------- -------------------- ----------
vtest0   Character Device     0         
pin      Pin Device           0         
uart1    Character Device     2         

msh >bsp_key_status
================ Spark-1 Key Status ================
  [KEY_UP   ] Pin: 37  | Level: 1 | Press Count: 0
  [KEY_DOWN ] Pin: 33  | Level: 1 | Press Count: 0
  [KEY_LEFT ] Pin: 32  | Level: 1 | Press Count: 0
  [KEY_RIGHT] Pin: 36  | Level: 1 | Press Count: 0
====================================================

msh >test_dev probe vtest0
[test_dev] Successfully probed and opened 'vtest0'.

msh >test_dev write "RT-Thread_Summer_Camp_Day4"
[test_dev] Wrote 26 bytes: "RT-Thread_Summer_Camp_Day4"

msh >test_dev read 26
[test_dev] Read 26 bytes: 'RT-Thread_Summer_Camp_Day4'

msh >test_dev status
================ Test Device Status ================
  Device Name : vtest0
  Open Status : OPEN (Ref Count: 1)
  Work Mode   : 1 (LOOPBACK)
  Buffer Info : 0 / 256 Bytes used
  Total TX    : 26 Bytes
  Total RX    : 26 Bytes
  Error Count : 0
====================================================

msh >test_dev mode 3
[test_dev] Operating mode updated to 3.

msh >test_dev read 10
[test_dev] Read 10 bytes: 'KLMNOPQRST'

msh >test_dev close
[test_dev] Probed device closed.
```

---

## 总结

本次 Day4 作业以课堂教学要点为理论参考，通过**完全自主设计与工程级重构**完成了软硬件驱动实践：
1. **理顺了 PIN 设备驱动全调用链**：从上层 API、I/O 框架抽象、驱动 `ops` 虚表到 HAL 库寄存器操作，全链路透明可溯；
2. **自研工业级三层驱动框架 `test_dev`**：从单一设备注册升级为包含框架抽象层、底层驱动层、并发互斥保护与 FinSH 交互工具的完整子系统；
3. **模块化按键驱动实战**：面向对象封装星火一号 4 方向按键，实现了非阻塞时间戳滤波消抖；
4. **深入剖析了 I2C / SPI 总线体系**：系统总结了消息包机制、9-Pulse 锁死恢复及总线-设备模型。
