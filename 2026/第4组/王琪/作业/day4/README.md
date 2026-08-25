# 基于RT-Thread Studio 芯片创建的RT-Thread 4.1.1项目，采用星火一号开发板，设计一个虚拟的设备驱动框架层。

> 在 RT-Thread 4.1.1 + 星火一号（STM32F407ZGT6）项目中设计虚拟设备驱动框架层，核心思路是**模仿 RT-Thread 原生的 `rt_device` 模型**，用结构体 + 函数指针实现"面向对象"的设备抽象，将设备管理与具体驱动解耦。

## 1.**整体架构设计**

```
应用层 (main.c)
    ↓ 调用统一接口 test_dev_xxx()
test_dev 框架层 (test_dev.c)
    ↓ 通过函数指针回调
设备驱动层 (drv_virtual_uart.c / drv_led.c ...)
    ↓ 操作硬件或模拟
硬件 / 虚拟设备
```

## 2.框架层的核心职责：

- 维护一个**设备链表**，管理所有注册的设备
- 提供统一的 `open/close/read/write/control` 接口
- 通过**函数指针表（ops）**将调用转发给具体驱动

## 3.**文件结构**

在 RT-Thread Studio 项目的 `applications` 目录下创建以下文件：

```
applications/
├── test_dev.h              ← 框架头文件
├── test_dev.c              ← 框架实现
├── drv_virtual_uart.h      ← 虚拟串口驱动头文件
├── drv_virtual_uart.c      ← 虚拟串口驱动实现
└── main.c                  ← 测试入口
```

## 4.**设计要点总结**

| 设计要素     | 实现方式                            | 对应 RT-Thread 原生机制           |
| ------------ | ----------------------------------- | --------------------------------- |
| 设备描述     | `struct test_dev`                   | `struct rt_device`                |
| 操作接口     | `struct test_dev_ops`（函数指针表） | `struct rt_device_ops`            |
| 设备注册     | `test_dev_register()` + 链表        | `rt_device_register()` + 对象容器 |
| 设备查找     | `test_dev_find()` 按名称遍历        | `rt_device_find()`                |
| 驱动私有数据 | `void *user_data`                   | 同样使用 `user_data`              |
| 多态机制     | 不同驱动填充不同的 `ops`            | C 语言模拟面向对象                |

这套框架完全模仿了 RT-Thread 的设备驱动模型思想，后续可以继续扩展，比如新增 `drv_led.c`（控制星火一号板载 LED）、`drv_key.c`（读取板载按键），只需实现各自的 `ops` 并调用 `test_dev_register()` 注册即可，上层应用代码无需任何修改。

## 5.结果展示

```
msh >
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 25 2026 19:05:22
 2006 - 2022 Copyright by RT-Thread team

=== test_dev Framework Test ===

[test_dev] registered: vuart0
[vuart] init: baud=115200, bits=8
=== Registered Devices ===
  -> vuart0 (ref=0)
==========================
[vuart] open
[vuart] write 27 bytes: Hello, test_dev on Spark-1!
[vuart] set baudrate = 9600
current baudrate = 9600
[vuart] close

=== Test Done ===
msh >list_device
device           type         ref count
-------- -------------------- ----------
uart1    Character Device     2
pin      Pin Device           0
```

