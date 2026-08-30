# 【DAY4】RT-Thread 自定义 LED 设备驱动框架设计实验报告

* **学员姓名**：杜马辰
* **所在组别**：第 2 组
* **指导导师**：张丙儒
* **目标硬件**：STM32F407 星火号（Spark-1）
* **源码文件**：`virtu_dev.h`、`virtu_dev.c`
* **MSH 测试命令**：`virtu_dev_test`
* **提交日期**：2026-08-21

---

## 一、 设计目标与需求说明

在 RT-Thread 操作系统中，标准 I/O 设备模型通过统一的 `struct rt_device` 提供通用接口（`open`/`close`/`read`/`write`/`control`）。但在许多具体的硬件外设（如 PIN 引脚、CAN、I2C、Sensor）中，通用读写接口往往无法直观表达特定设备的语义（例如点灯、熄灯、翻转操作）。

本实验的目标是**深度仿照 RT-Thread 原生 PIN 设备驱动框架的三层架构**，设计并实现一套独立的 **自定义 LED 设备驱动框架**：
1. **接口隔离与语义化**：不直接使用泛化的 `write/control`，而是定义专有的操作方法表 `struct rt_led_ops`，并对外暴露语义化的用户 API（`rt_led_init`、`rt_led_on`、`rt_led_off`、`rt_led_toggle`）。
2. **软硬件解耦**：框架层仅定义接口契约与分发逻辑，不依赖具体 MCU 外设寄存器；BSP 硬件驱动层负责对接具体的 STM32 硬件引脚，实现平台的可移植性。
3. **内核自动注册**：利用 `INIT_BOARD_EXPORT` 实现系统上电引导阶段的设备自动注册与绑定。

---

## 二、 系统架构与分层设计

```text
┌─────────────────────────────────────────────────────────────┐
│                    应用层 (Application)                      │
│        用户业务逻辑直接调用语义化 API: rt_led_on() / rt_led_off()      │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                 驱动框架层 (Framework Layer)                 │
│      定义 struct rt_led_ops / struct rt_device_led          │
│         提供 rt_device_led_register() 注册管理               │
└──────────────────────────────┬──────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                 BSP 驱动层 (BSP Driver Layer)                │
│    stm32_led_init / stm32_led_on / stm32_led_off / toggle   │
│           对接 STM32F407 实际 GPIO (PF11/PF12)               │
└─────────────────────────────────────────────────────────────┘
```

### 与原生 PIN 驱动框架对照关系表

| 组件类型 | RT-Thread 原生 PIN 框架 | 本实验自定义 LED 框架 |
| :--- | :--- | :--- |
| **头文件定义** | `pin.h` | `virtu_dev.h` |
| **框架层源码** | `pin.c` | `virtu_dev.c`（框架层） |
| **底层硬件驱动** | `drv_gpio.c` | `virtu_dev.c`（BSP 层） |
| **操作方法表** | `struct rt_pin_ops` | `struct rt_led_ops` |
| **专用设备控制块** | `struct rt_device_pin` | `struct rt_device_led` |
| **设备注册函数** | `rt_device_pin_register()` | `rt_device_led_register()` |
| **用户控制 API** | `rt_pin_write()` | `rt_led_on()` / `rt_led_off()` |
| **板级自动初始化** | `INIT_BOARD_EXPORT(rt_hw_pin_init)` | `INIT_BOARD_EXPORT(rt_hw_led_init)` |

---

## 三、 核心数据结构与 API 设计

### 1. 操作方法表 `struct rt_led_ops`
```c
struct rt_led_ops
{
    void (*led_init)(struct rt_device *device, rt_base_t pin, rt_base_t mode);
    void (*led_on)(struct rt_device *device, rt_base_t pin);
    void (*led_off)(struct rt_device *device, rt_base_t pin);
    void (*led_toggle)(struct rt_device *device, rt_base_t pin);
};
```

### 2. 自定义 LED 设备结构体 `struct rt_device_led`
```c
struct rt_device_led
{
    struct rt_device parent;        /* 继承标准内核设备基类 */
    const struct rt_led_ops *ops;   /* 底层硬件操作函数指针表 */
};
```

---

## 四、 实验运行现象与串口输出日志

在 FinSH 控制台中执行 `virtu_dev_test` 命令：

```text
msh >virtu_dev_test

================ [Virtual LED Device Driver Test] ================
[BSP LED] Pin 91 initialized to mode 0.
[BSP LED] Pin 91 turned ON (Low level).
[BSP LED] Pin 91 turned OFF (High level).
[BSP LED] Toggling Pin 91 for 10 cycles...
  - Toggle Count: 1, Current Level: 0
  - Toggle Count: 2, Current Level: 1
  - Toggle Count: 3, Current Level: 0
  - Toggle Count: 4, Current Level: 1
  - Toggle Count: 5, Current Level: 0
  - Toggle Count: 6, Current Level: 1
  - Toggle Count: 7, Current Level: 0
  - Toggle Count: 8, Current Level: 1
  - Toggle Count: 9, Current Level: 0
  - Toggle Count: 10, Current Level: 1
==================================================================
```

---

## 五、 实验技术总结与分析

1. **通用驱动模型与专用驱动框架的差异**：
   * 泛化模型（`rt_device_read`/`write`）适合按字节流读写的块设备与字符设备。
   * 专用框架（如本 LED 框架、PIN 框架）通过自定义 `ops` 接口表，为上层应用提供直观、类型安全的语义化接口，极大地提升了应用代码的可读性与工程维护性。
2. **高内聚低耦合与跨平台移植优势**：
   * 当底层硬件从 STM32F407 迁移到其他微控制器（如 RISC-V 或 GD32）时，框架层及所有上层业务代码无需任何改动，仅需重新实现一套适配新芯片 GPIO 寄存器的 `struct rt_led_ops` 即可完成移植。
