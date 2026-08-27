# 【DAY4】RT-Thread I/O 设备管理层、PIN 设备对接与虚拟设备（test_dev）设计

> **作者**：冷梦奇（第1组）  
> **日期**：2026-08-20  
> **平台**：STM32F407 星火1号（`stm32f407-rtspark`）  
> **工程目录**：`D:\RT-ThreadStudio\workspace\04_dev`  

---

## 目录
- [一、 小白也能懂：什么是 RT-Thread 的设备驱动框架？](#一-小白也能懂什么是-rt-thread-的设备驱动框架)
- [二、 PIN 设备是如何一层层对接工作的？](#二-pin-设备是如何一层层对接工作的)
  - [2.1 形象比喻：管家与电灯开关](#21-形象比喻管家与电灯开关)
  - [2.2 调用链全过程跟踪](#22-调用链全过程跟踪)
  - [2.3 按键中断实操 (key_sample.c)](#23-按键中断实操-key_samplec)
- [三、 自己动手写一个虚拟设备（test_dev）](#三-自己动手写一个虚拟设备test_dev)
  - [3.1 设计思路](#31-设计思路)
  - [3.2 核心代码实现解析](#32-核心代码实现解析)
- [四、 编译与运行效果验证](#四-编译与运行效果验证)
  - [4.1 编译结果](#41-编译结果)
  - [4.2 串口运行与测试输出](#42-串口运行与测试输出)

---

## 一、 小白也能懂：什么是 RT-Thread 的设备驱动框架？

### 1. 为什么不用裸机方式直接操作硬件？
在刚学单片机时，我们控制引脚输出都是直接调用厂商库函数（比如 STM32 的 `HAL_GPIO_WritePin`）。  
但这样做有一个大痛点：**一旦更换芯片（例如换成 GD32 或 ESP32），所有控制代码必须推倒重写！**

### 2. 生动比喻：手机的 Type-C 充电口
RT-Thread 的设备框架就像把手机充电口统一成了 **Type-C**：
- 不管你买的是华为、小米还是苹果，充电线插头（**应用层 API**）都是一样的。
- 手机内部的具体电池管理电路（**底层硬件驱动**）由各家芯片自己去实现。
- 我们写应用代码时，只需要调用通用的 `open / read / write / close`，再也不用关心芯片底层复杂的寄存器细节。

```text
┌─────────────────────────────────────────────────────────────┐
│                 应用层 (Application Layer)                  │
│       使用统一的 API: rt_device_open / read / write ...      │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────▼──────────────────────────────┐
│             I/O 设备管理层 (I/O Device Manager)              │
│       负责设备注册、根据名字查找设备、管理设备打开状态       │
└──────────────────────────────┬──────────────────────────────┘
                               │ 调用底层实现的具体动作
┌──────────────────────────────▼──────────────────────────────┐
│                底层驱动层 (Hardware Driver Layer)            │
│       具体芯片的驱动实现 (如 STM32 的 HAL 库或寄存器操作)    │
└─────────────────────────────────────────────────────────────┘
```

---

## 二、 PIN 设备是如何一层层对接工作的？

### 2.1 形象比喻：管家与电灯开关
- **你（应用层）**：跟管家说一句“帮我把 1 号灯关掉”（调用 `rt_pin_write(pin, PIN_LOW)`）。
- **管家（PIN 设备框架层）**：查阅房间电器手册，找到 1 号灯对应的硬件开关操作方式（调用底层 `ops->pin_write`）。
- **电工师傅（驱动层 `drv_gpio.c`）**：走到电闸箱，把连接电灯的具体开关扳下来（调用 STM32 `HAL_GPIO_WritePin` 写入 `BSRR` 寄存器）。

---

### 2.2 调用链全过程跟踪

以我们在代码中写 `rt_pin_write(pin, PIN_LOW)` 为例，全过程如下：

```text
1. 应用层调用: rt_pin_write(pin, PIN_LOW)
   │
   ▼
2. 框架层转接: components/drivers/misc/pin.c 中的 rt_pin_write()
   调用: _hw_pin.ops->pin_write(&_hw_pin.parent, pin, PIN_LOW)
   │
   ▼
3. 驱动层实现: drivers/drv_gpio.c 中的 stm32_pin_write()
   计算出引脚属于哪个端口（如 GPIOF）和第几号引脚（如 Pin 9）
   调用: HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET)
   │
   ▼
4. 硬件寄存器生效:
   向 STM32 的 GPIOF->BSRR 寄存器写入复位指令，物理引脚瞬间变为低电平！
```

---

### 2.3 按键中断实操 (`key_sample.c`)

在星火一号开发板上，我们为按键 `KEY_UP`（引脚 PC5）配置了中断响应：

```c
/* 按键中断回调函数：按键按下瞬间会自动跳到这里执行 */
static void key_up_callback(void *args)
{
    rt_kprintf(">> [中断提醒] KEY_UP 按键被按下了! (PC5) <<\n");
}

/* 初始化按键 */
int key_init(void)
{
    /* 1. 设置为上拉输入模式 (平时高电平，按下变低电平) */
    rt_pin_mode(KEY_UP_PIN, PIN_MODE_INPUT_PULLUP);

    /* 2. 绑定下降沿中断 (电平从高变低的瞬间触发中断) */
    rt_pin_attach_irq(KEY_UP_PIN, PIN_IRQ_MODE_FALLING, key_up_callback, RT_NULL);

    /* 3. 使能开启中断通道 */
    rt_pin_irq_enable(KEY_UP_PIN, PIN_IRQ_ENABLE);

    rt_kprintf("[key] 按键中断初始化完成 (引脚 PC5)\n");
    return RT_EOK;
}
INIT_APP_EXPORT(key_init);
```

---

## 三、 自己动手写一个虚拟设备（test_dev）

### 3.1 设计思路
我们要实现一个名叫 **`test_dev`** 的虚拟字符设备：
1. **内部缓冲区**：在内存中开辟一块 64 字节的字符数组 `s_buffer`，用来模拟设备存储。
2. **实现 6 个基础接口**：
   - `init`：初始化，清空缓冲区；
   - `open` / `close`：打开和关闭设备；
   - `write`：把用户传入的字符串写进内部数组；
   - `read`：把内部数组里的数据读给用户；
   - `control`：支持清空缓冲区指令。
3. **注册设备**：调用 `rt_device_register` 把 `test_dev` 注册到操作系统中。

---

### 3.2 核心代码实现解析 (`test_dev.c`)

```c
#include "test_dev.h"
#include <string.h>

#define TEST_BUF_SIZE   64

static struct rt_device s_test_dev;
static char s_buffer[TEST_BUF_SIZE];
static rt_size_t s_data_len = 0;

/* 写入数据到虚拟设备 */
static rt_size_t test_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    if (buffer == RT_NULL || size == 0) return 0;

    rt_size_t write_bytes = (size < (TEST_BUF_SIZE - s_data_len)) ? size : (TEST_BUF_SIZE - s_data_len);
    rt_memcpy(s_buffer + s_data_len, buffer, write_bytes);
    s_data_len += write_bytes;

    rt_kprintf("[test_dev] 写入数据 %d 字节\n", (int)write_bytes);
    return write_bytes;
}

/* 从虚拟设备中读取数据 */
static rt_size_t test_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    if (buffer == RT_NULL || size == 0 || s_data_len == 0) return 0;

    rt_size_t read_bytes = (size < s_data_len) ? size : s_data_len;
    rt_memcpy(buffer, s_buffer, read_bytes);

    s_data_len -= read_bytes;
    for (rt_size_t i = 0; i < s_data_len; i++)
    {
        s_buffer[i] = s_buffer[i + read_bytes];
    }

    rt_kprintf("[test_dev] 读出数据 %d 字节\n", (int)read_bytes);
    return read_bytes;
}

/* 注册为 "test_dev" 设备 */
int test_dev_register(void)
{
    s_test_dev.type    = RT_Device_Class_Char;
    s_test_dev.init    = test_dev_init;
    s_test_dev.open    = test_dev_open;
    s_test_dev.close   = test_dev_close;
    s_test_dev.read    = test_dev_read;
    s_test_dev.write   = test_dev_write;
    s_test_dev.control = test_dev_control;

    return rt_device_register(&s_test_dev, "test_dev", RT_DEVICE_FLAG_RDWR);
}
INIT_DEVICE_EXPORT(test_dev_register);
```

---

## 四、 编译与运行效果验证

### 4.1 编译结果
在 RT-Thread Studio 中编译，**0 错误、0 警告**：

```text
[CC] applications/main.c
[CC] applications/test_dev.c
[CC] applications/key_sample.c
Build completed successfully (0 errors, 0 warnings).
```

---

### 4.2 串口运行与测试输出

#### 1. 系统开机输出：
```text
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 27 2026 16:18:00
 2006 - 2022 Copyright by RT-Thread team
[test_dev] 虚拟设备 test_dev 注册成功!
[key] 按键中断初始化完成 (引脚 PC5)

=============================================
  欢迎来到 RT-Thread Day4 设备驱动实验!
  在串口终端输入 test_dev_sample 即可运行测试
=============================================
msh >
```

#### 2. 在控制台查看设备列表：
```text
msh >list_device
device           type         ref count
-------- -------------------- ----------
test_dev Character Device     0         
pin      Pin Device           0         
uart1    Character Device     2         
```
可以看到我们的 `test_dev` 已经成功挂载到系统中！

#### 3. 执行测试命令 (`test_dev_sample`)：
```text
msh >test_dev_sample

--- 开始测试 test_dev 设备 ---
[test_dev] 2. 设备已打开 (open)
[test_dev] 3. 写入数据 16 字节
[test_dev] 4. 读出数据 16 字节
>> 从设备中读出的字符串: [hello rt-thread] <<
[test_dev] 5. 设备已关闭 (close)
--- test_dev 测试成功! ---

msh >
```

#### 4. 按下板子上的按键：
```text
>> [中断提醒] KEY_UP 按键被按下了! (PC5) <<
```

---

## 五、 总结

通过今天的学习与动手实践：
1. **彻底搞懂了分层抽象**：知道了为什么要通过 `rt_device` 统一接口；
2. **打通了 PIN 驱动调用链**：从应用层函数一路追到了底层寄存器；
3. **独立写出了第一个虚拟设备**：理解了设备的 6 个基础接口（`init/open/close/read/write/control`）和注册机制！
