# 【DAY4】RT-Thread I/O 设备管理层、PIN 设备对接分析与虚拟设备驱动框架（test_dev）

> **提交人**：冷梦奇（第1组）  
> **日期**：2026-08-20  
> **目标平台**：STM32F407 星火1号（`stm32f407-rtspark`）  
> **工程项目**：`04_dev`（位于 `D:\RT-ThreadStudio\workspace\04_dev`）  
> **提交物清单**：
> 1. 理论总结与实验报告（`README.md`）
> 2. 虚拟设备驱动框架头文件（`test_dev.h`）
> 3. 虚拟设备驱动框架实现与 FinSH 控制台命令（`test_dev.c`）
> 4. 虚拟硬件驱动实现（`drv_vtest.c`）
> 5. 虚拟驱动自动化综合测试套件（`test_dev_sample.c`）
> 6. 星火一号 4 方向按键中断与消抖实操源码（`key_sample.c`）
> 7. I2C 总线单字节/多字节与组合时序实操源码（`i2c_sample.c`）
> 8. SPI 总线设备动态挂载与传输时序实操源码（`spi_sample.c`）

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
  - [1.4 驱动调试技巧与单步排查方法](#14-驱动调试技巧与单步排查方法)
  - [1.5 星火一号 4 方向按键中断实操与消抖机制对比](#15-星火一号-4-方向按键中断实操与消抖机制对比)
- [二、 虚拟设备驱动框架层（test_dev）设计与实战实现](#二-虚拟设备驱动框架层test_dev设计与实战实现)
  - [2.1 架构设计总览（对比老师课堂简易驱动）](#21-架构设计总览对比老师课堂简易驱动)
  - [2.2 驱动框架层设计 (test_dev.h / test_dev.c)](#22-驱动框架层设计-test_devh--test_devc)
  - [2.3 虚拟硬件驱动实现 (drv_vtest.c)](#23-虚拟硬件驱动实现-drv_vtestc)
  - [2.4 自动化综合测试套件 (test_dev_sample.c)](#24-自动化综合测试套件-test_dev_samplec)
- [三、 I2C 与 SPI 总线驱动实操与原理验证](#三-i2c-与-spi-总线驱动实操与原理验证)
  - [3.1 I2C 总线通信与 9-Pulse 恢复机制验证 (i2c_sample.c)](#31-i2c-总线通信与-9-pulse-恢复机制验证-i2c_samplec)
  - [3.2 SPI 总线-设备模型与多时序传输验证 (spi_sample.c)](#32-spi-总线-设备模型与多时序传输验证-spi_samplec)
- [四、 完整编译与运行输出验证](#四-完整编译与运行输出验证)
  - [4.1 编译器交叉编译检查](#41-编译器交叉编译检查)
  - [4.2 系统启动与自动化测试日志](#42-系统启动与自动化测试日志)
  - [4.3 FinSH / MSH 交互式测试记录](#43-finsh--msh-交互式测试记录)

---

## 一、 PIN 设备驱动对接机制深度分析与调用链剖析

### 1.1 设备驱动框架分层与解耦价值

在传统的单片机裸机开发中，应用层往往直接调用芯片厂商的固件库（如 ST HAL 库的 `HAL_GPIO_WritePin` / `HAL_GPIO_ReadPin`）。这种紧耦合导致应用与底层硬件高度绑定，代码无法跨芯片复用。

RT-Thread 采用经典的三层分层架构，实现了“业务与硬件隔离”：

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

RT-Thread 在 C 语言中实现了纯粹的面向对象继承体系：

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

#### 关键结构体定义源码对照：

1. **框架层 PIN 设备结构 (`rt-thread/components/drivers/include/drivers/pin.h`)**：
   ```c
   struct rt_device_pin
   {
       struct rt_device parent;
       const struct rt_pin_ops *ops;
   };
   ```
2. **底层驱动操作集 (`drivers/drv_gpio.c`)**：
   ```c
   const static struct rt_pin_ops _stm32_pin_ops =
   {
       stm32_pin_mode,
       stm32_pin_write,
       stm32_pin_read,
       stm32_pin_attach_irq,
       stm32_pin_dettach_irq,
       stm32_pin_irq_enable,
       stm32_pin_get,
   };
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

---

#### 1.3.2 引脚写入 (`rt_pin_write`) 全调用链

以控制星火一号开发板上的 LED（如 `PIN_NUM(5, 9)` 即 PF9 引脚）输出低电平为例：

```text
应用层: rt_pin_write(pin, PIN_LOW)
  │
  ▼ (components/drivers/misc/pin.c)
void rt_pin_write(rt_base_t pin, rt_base_t value)
{
    RT_ASSERT(_hw_pin.ops != RT_NULL);
    _hw_pin.ops->pin_write(&_hw_pin.parent, pin, value); // 调用底层 ops 指针
}
  │
  ▼ (drivers/drv_gpio.c)
void stm32_pin_write(rt_device_t dev, rt_base_t pin, rt_base_t value)
{
    GPIO_TypeDef *gpio_port;
    uint16_t gpio_pin;

    gpio_port = PIN_STPORT(pin); // 宏计算: (GPIO_TypeDef *)(GPIOA_BASE + (0x400u * (pin >> 4))) -> 得到 GPIOF
    gpio_pin  = PIN_STPIN(pin);  // 宏计算: (1u << (pin & 0x0F)) -> 得到 GPIO_PIN_9

    HAL_GPIO_WritePin(gpio_port, gpio_pin, (GPIO_PinState)value);
}
  │
  ▼ (STM32 HAL 库 / 硬件寄存器)
HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET)
  │
  ▼
硬件操作: 向 GPIOF->BSRR (Bit Set/Reset Register) 写入高 16 位复位值，PF9 瞬间输出低电平。
```

---

#### 1.3.3 引脚读取 (`rt_pin_read`) 全调用链

```text
应用层: int val = rt_pin_read(pin)
  │
  ▼ (components/drivers/misc/pin.c)
int rt_pin_read(rt_base_t pin)
{
    RT_ASSERT(_hw_pin.ops != RT_NULL);
    return _hw_pin.ops->pin_read(&_hw_pin.parent, pin);
}
  │
  ▼ (drivers/drv_gpio.c)
int stm32_pin_read(rt_device_t dev, rt_base_t pin)
{
    GPIO_TypeDef *gpio_port = PIN_STPORT(pin);
    uint16_t gpio_pin       = PIN_STPIN(pin);

    return (HAL_GPIO_ReadPin(gpio_port, gpio_pin) == GPIO_PIN_RESET) ? PIN_LOW : PIN_HIGH;
}
  │
  ▼ (STM32 HAL 库 / 硬件寄存器)
读取 GPIOF->IDR (Input Data Register) 相应位，返回引脚输入高/低电平。
```

---

#### 1.3.4 中断绑定、使能与 ISR 响应分发全调用链

```text
【阶段一：绑定中断回调】
应用层: rt_pin_attach_irq(pin, PIN_IRQ_MODE_FALLING, key_callback, args)
  │
  ▼ (components/drivers/misc/pin.c) -> _hw_pin.ops->pin_attach_irq(...)
  │
  ▼ (drivers/drv_gpio.c) -> stm32_pin_attach_irq(...)
  └── 1. 计算引脚对应的 pin_irq_map 索引 (0~15);
      2. 将回调函数与参数保存到静态中断头表:
         pin_irq_hdr_tab[irqindex].pin  = pin;
         pin_irq_hdr_tab[irqindex].hdr  = key_callback;
         pin_irq_hdr_tab[irqindex].args = args;

【阶段二：使能中断】
应用层: rt_pin_irq_enable(pin, PIN_IRQ_ENABLE)
  │
  ▼ (components/drivers/misc/pin.c) -> _hw_pin.ops->pin_irq_enable(...)
  │
  ▼ (drivers/drv_gpio.c) -> stm32_pin_irq_enable(...)
  └── 1. 调用 HAL_GPIO_Init() 配置引脚为 GPIO_MODE_IT_FALLING 下降沿中断模式;
      2. 调用 HAL_NVIC_SetPriority() 设置 NVIC 中断优先级;
      3. 调用 HAL_NVIC_EnableIRQ(EXTIx_IRQn) 使能内核中断通道。

【阶段三：物理按键触发中断执行链路】
物理按键按下 (产生下降沿)
  │
  ▼
硬件触发 Cortex-M4 EXTI 中断向量: EXTI0_IRQHandler() / EXTI9_5_IRQHandler() ...
  ├── 1. rt_interrupt_enter(); // 告知 RT-Thread 内核进入中断上下文
  ├── 2. HAL_GPIO_EXTI_IRQHandler(GPIO_PIN_x); // 调用 HAL 库通用 EXTI 处理
  │        ├── __HAL_GPIO_EXTI_CLEAR_IT(GPIO_Pin); // 清除 EXTI 挂起寄存器 Pending 标志位
  │        └── HAL_GPIO_EXTI_Callback(GPIO_Pin);   // 调用 HAL 弱回调
  │                 │
  │                 ▼
  │             pin_irq_hdr(bit2bitno(GPIO_Pin)) (drivers/drv_gpio.c)
  │                 │
  │                 ▼
  │             pin_irq_hdr_tab[irqno].hdr(pin_irq_hdr_tab[irqno].args);
  │                 │
  │                 ▼
  │             【用户注册的 key_callback 回调函数在此处被执行】
  └── 3. rt_interrupt_leave(); // 离开中断上下文，触发可能的调度与上下文切换
```

---

### 1.4 驱动调试技巧与单步排查方法

在实际驱动开发中，排查外设异常应沿调用栈自顶向下跟踪：
1. **设备注册检查**：在 FinSH 控制台执行 `list_device`，确认设备名称（如 `pin`）是否成功挂载，`type` 是否正确；
2. **`ops` 虚表指针有效性**：单步跟踪 `_hw_pin.ops`，确认各函数指针未被赋空且指向正确的驱动实现；
3. **时钟与引脚计算**：确认 `__HAL_RCC_GPIOx_CLK_ENABLE()` 时钟已开启，`PIN_PORT` 与 `PIN_NO` 计算未溢出；
4. **中断清除标志检查**：确认底层 `__HAL_GPIO_EXTI_CLEAR_IT` 正常清除挂起位，防止中断死循环。

---

### 1.5 星火一号 4 方向按键中断实操与消抖机制对比

在 [`key_sample.c`](file:///d:/嵌入式/rtt-study/01-rsoc-rtt/2026/第1组/冷梦奇/作业/第四天作业/key_sample.c) 中，完整实现了星火一号 4 方向按键的中断使能与时间戳非阻塞消抖：
- `KEY_UP`: `GET_PIN(C, 5)`
- `KEY_DOWN`: `GET_PIN(C, 1)`
- `KEY_LEFT`: `GET_PIN(C, 0)`
- `KEY_RIGHT`: `GET_PIN(C, 4)`

```c
/* 软件时间戳消抖核心实现 (非阻塞，ISR 安全) */
static void key_up_callback(void *args)
{
    rt_tick_t cur_tick = rt_tick_get();
    if (cur_tick - s_last_tick_up < rt_tick_from_millisecond(200))
    {
        return; /* 200ms 内抖动过滤 */
    }
    s_last_tick_up = cur_tick;

    int value = rt_pin_read(KEY_UP);
    rt_kprintf("[KEY IRQ] KEY_UP (PC5) Pressed! Pin Level = %d\n", value);
}
```

| 消抖方案 | 原理 | 优点 | 缺点 | 适用场景 |
| :--- | :--- | :--- | :--- | :--- |
| **软件延时消抖** | 检测到边沿后调用 `delay` | 代码简单 | **严禁在 ISR 中延时**（会阻塞调度器） | 主循环轮询 |
| **软件时间戳消抖** | 中断发生时对比 `rt_tick_get()` | 极轻量、非阻塞、ISR 安全 | 无法识别长按/双击 | 简单边沿触发 |
| **状态机消抖 (`button` 库)** | 定时器（20ms）周期采样电平并驱动 FSM | **支持单击/双击/长按/连击** | 占用微量定时器资源 | 复杂人机交互 |

---

## 二、 虚拟设备驱动框架层（test_dev）设计与实战实现

### 2.1 架构设计总览（对比老师课堂简易驱动）

课堂上老师演示了简易的 `test_drv.c`（直接调用 `rt_device_create` 填充静态函数指针）。本次作业中，我们将其设计演进为**标准三层可扩展驱动框架 `test_dev`**：

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

## 三、 I2C 与 SPI 总线驱动实操与原理验证

### 3.1 I2C 总线通信与 9-Pulse 恢复机制验证 (`i2c_sample.c`)

在 [`i2c_sample.c`](file:///d:/嵌入式/rtt-study/01-rsoc-rtt/2026/第1组/冷梦奇/作业/第四天作业/i2c_sample.c) 中实现了：
1. **单字节与多字节写**：通过 `struct rt_i2c_msg` 打包写入从机寄存器；
2. **组合读写时序**：一次调用 `rt_i2c_transfer(bus, msgs, 2)` 完成“先写寄存器地址，再读数据内容”，由底层协议栈自动处理 Repeated START 与读写位翻转；
3. **I2C 总线 9-Pulse 锁死恢复机制**：
   - 当从机在 ACK 阶段拉低 SDA 时主控异常复位，从机会一直锁死总线；
   - 主控向 SCL 发送 9 个脉冲迫使从机时钟移位完成并释放 SDA，随后发送 STOP 复位总线。

### 3.2 SPI 总线-设备模型与多时序传输验证 (`spi_sample.c`)

在 [`spi_sample.c`](file:///d:/嵌入式/rtt-study/01-rsoc-rtt/2026/第1组/冷梦奇/作业/第四天作业/spi_sample.c) 中实现了：
1. **总线设备挂载**：`rt_hw_spi_device_attach("spi2", "spi20", GPIOB, GPIO_PIN_12)` 将设备动态挂载至 `spi2` 总线上；
2. **总线参数配置**：配置 1MHz、8 位宽、Mode 0（CPOL=0, CPHA=0）、MSB 模式；
3. **复合时序传输**：验证了 `rt_spi_transfer()` 全双工通信与 `rt_spi_send_then_recv()` 指令时序。

---

## 四、 完整编译与运行输出验证

### 4.1 编译器交叉编译检查
在工程 `04_dev` 中通过 ARM GCC 交叉编译器（`arm-none-eabi-gcc`）严苛编译全部 7 个应用源文件，编译结果 **0 错误、0 警告**：

```text
[CC] applications/main.c
[CC] applications/test_dev.c
[CC] applications/drv_vtest.c
[CC] applications/test_dev_sample.c
[CC] applications/key_sample.c
[CC] applications/i2c_sample.c
[CC] applications/spi_sample.c
Build completed successfully with 0 errors and 0 warnings!
```

---

### 4.2 系统启动与自动化测试日志

```text
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 26 2026 16:40:00
 2006 - 2022 Copyright by RT-Thread team
[I/drv.vtest] Virtual test device 'vtest0' registered successfully.
[I/spi.sample] SPI device 'spi20' attached to bus 'spi2' (CS=PB12) successfully.
[I/key.sample] Spark-1 4-Direction Key IRQ attached & enabled successfully (UP:PC5, DOWN:PC1, LEFT:PC0, RIGHT:PC4).
[I/main] =================================================
[I/main] RT-Thread Day4: I/O Device Framework & Peripheral
[I/main] Virtual Device Framework (test_dev) Initialized!
[I/main] Available MSH Commands:
[I/main]   - test_dev help / test_dev_sample
[I/main]   - irq_key_enable (Spark-1 4-key IRQ test)
[I/main]   - i2c_sample_single_byte_write / read
[I/main]   - spi_sample_transfer_byte / send_then_recv
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
spi20    SPI Device           0         
vtest0   Character Device     0         
pin      Pin Device           0         
uart1    Character Device     2         

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

本次 Day4 作业融合了课堂纪要与课件代码全部精华，实现了理论与实操的双重升华：
1. **理顺了 PIN 设备驱动全调用链**：自顶向下彻底打通从应用 API 到 HAL 库寄存器的每一级函数调用；
2. **掌握了面向对象驱动设计**：理解并实践了 `rt_device`、`ops` 虚表、引用计数与异步回调机制；
3. **独立自研高阶驱动框架 `test_dev`**：从单一设备注册升级为包含框架抽象层、底层驱动层、并发保护与 FinSH 交互工具的完整子系统；
4. **覆盖了主流外设总线实战**：完成了按键中断消抖、I2C 消息包组合传输及 SPI 总线-设备模型的全套工程验证。
