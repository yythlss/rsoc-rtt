# Day2：RT-Thread 内核基础与多线程编程

## Part 1：理论知识总结

### 1. STM32F407 启动流程与 RT-Thread 引导机制

#### 1.1 STM32F407 与 RT-Thread 启动流程

STM32F407 上电或复位后，并不会直接执行用户编写的 `main()`，而是先完成 MCU 启动、C 运行环境初始化以及 RT-Thread 内核初始化。

整体启动流程如下：

```text
STM32F407 上电 / 复位
        ↓
读取中断向量表                  // 获取初始 MSP 和 Reset_Handler 地址
        ↓
设置 MSP                       // 设置 Cortex-M4 主栈指针
        ↓
Reset_Handler                  // 进入复位处理函数
        ↓
SystemInit()                   // 完成 MCU 基础系统初始化
        ↓
初始化 .data / .bss           // 建立 C 语言运行环境
        ↓
rtthread_startup()             // 进入 RT-Thread 启动流程
        ↓
rt_hw_interrupt_disable()      // 暂时关闭中断
        ↓
rt_hw_board_init()             // 初始化板级硬件
        ↓
rt_system_timer_init()         // 初始化系统定时器
        ↓
rt_system_scheduler_init()     // 初始化线程调度器
        ↓
rt_application_init()          // 创建 main 线程
        ↓
rt_thread_idle_init()          // 创建 idle 空闲线程
        ↓
rt_system_scheduler_start()    // 启动线程调度器
        ↓
main_thread_entry()            // main 线程入口
        ↓
rt_components_init()           // 执行组件自动初始化
        ↓
INIT_APP_EXPORT() 注册函数      // 执行 APP 级自动初始化函数
        ↓
main()                         // 进入用户程序
```

其中，`Reset_Handler` 是 MCU 复位后的软件入口。启动阶段会完成 `.data` 数据复制和 `.bss` 清零，为后续 C 程序建立正确的运行环境。

RT-Thread 启动后通过 `rt_application_init()` 创建 Main Thread，再由调度器调度 `main_thread_entry()`，最终执行用户编写的 `main()`。因此，RT-Thread 中的 `main()` 实际运行在线程环境中。

------

#### 1.2 Main Thread 与 Idle Thread

RT-Thread 启动过程中会自动创建多个系统线程，其中比较重要的是 Main Thread 和 Idle Thread。

Main Thread 用于执行用户程序：

```text
rtthread_startup()
        ↓
rt_application_init()
        ↓
创建 Main Thread
        ↓
启动调度器
        ↓
main_thread_entry()
        ↓
main()
```

Idle Thread 是系统中优先级最低的线程，当没有其他就绪线程时运行。

```text
没有其他 READY 线程
        ↓
Idle Thread
```

Idle Thread 还可用于线程资源回收、空闲 Hook 和低功耗处理等后台任务。

------

#### 1.3 RT-Thread 自动初始化机制

RT-Thread 提供：

```c
INIT_BOARD_EXPORT()
INIT_APP_EXPORT()
```

等自动初始化宏，使模块可以在系统启动过程中自动执行，而不需要在 `main()` 中手动调用。

其基本原理可以概括为：

```text
初始化函数
        ↓
INIT_BOARD_EXPORT(fn)
或 INIT_APP_EXPORT(fn)
        ↓
INIT_EXPORT(fn, level)
        ↓
生成初始化函数指针
        ↓
放入指定 .rti_fn.x section
        ↓
链接器将同一级函数组织起来
        ↓
系统启动
        ↓
遍历初始化函数表
        ↓
依次执行初始化函数
```

`INIT_BOARD_EXPORT()` 与 `INIT_APP_EXPORT()` 的实现原理基本相同，主要区别在于初始化级别和执行时机不同。

| 宏                    | 初始化阶段               | 典型用途           |
| --------------------- | ------------------------ | ------------------ |
| `INIT_BOARD_EXPORT()` | Board 级初始化阶段       | 板级硬件、底层驱动 |
| `INIT_APP_EXPORT()`   | Application 级初始化阶段 | 用户应用、业务线程 |

例如：

```c
static int thread_study_init(void)
{
    /* 创建应用线程 */
    return 0;
}

INIT_APP_EXPORT(thread_study_init);
```

系统运行到组件初始化阶段时，会自动调用 `thread_study_init()`，因此无需在 `main()` 中再次手动调用。

这种机制可以降低不同模块与 `main()` 之间的耦合，提高代码的模块化程度。

------

### 2. RT-Thread 内核基础与对象模型

#### 2.1 RT-Thread 对象模型

RT-Thread 使用 C 语言实现，但内核中采用了类似面向对象的设计思想。

线程、信号量、互斥量、事件、定时器和设备等都可以作为 RT-Thread 内核对象进行统一管理。

```text
                RT-Thread Object
                       │
        ┌──────────────┼──────────────┐
        │              │              │
      Thread        Semaphore       Timer
```

线程对象基于：

```c
struct rt_thread
```

进行描述，而 `struct rt_thread` 又包含 RT-Thread 基础对象相关信息。

因此可以将线程控制块理解为 RT-Thread 为每一个线程建立的一份“线程档案”。

------

#### 2.2 线程控制块 TCB

线程控制块 TCB（Thread Control Block）用于保存线程的各种运行信息。

RT-Thread 中通过：

```c
struct rt_thread
```

定义线程控制块。

其中包含：

```text
线程名称
线程状态
线程优先级
线程入口函数
线程栈
时间片
线程错误码
调度相关信息
定时器
用户数据
```

一个线程可以简单理解为：

```text
Thread
 │
 ├── 线程入口函数
 ├── 线程栈
 └── TCB
      ├── stat
      ├── current_priority
      ├── init_tick
      ├── sp
      └── 其他线程信息
```

作业中需要重点理解以下成员：

| 成员               | 作用                                         |
| ------------------ | -------------------------------------------- |
| `stat`             | 保存线程当前运行状态                         |
| `current_priority` | 保存线程当前优先级                           |
| `init_tick`        | 保存线程初始化时设置的时间片                 |
| `sp`               | 保存线程当前栈指针，用于线程上下文保存与恢复 |

------

#### 2.3 线程栈

每个线程都有自己独立的线程栈。

线程栈主要用于保存：

- CPU 上下文
- 函数局部变量
- 函数调用信息
- 寄存器现场
- 函数返回地址

线程切换时，可以简单理解为：

```text
线程 A RUNNING
      ↓
保存 A 的上下文到线程 A 栈
      ↓
加载线程 B 栈中的上下文
      ↓
线程 B RUNNING
```

因此线程栈是实现线程上下文切换的重要基础。

如果栈空间设置过小，可能导致 Stack Overflow，因此创建线程时需要根据函数调用深度和局部变量大小合理设置线程栈。

------

#### 2.4 静态线程与动态线程

RT-Thread 主要支持两种线程创建方式：

```text
线程
 │
 ├── 静态线程
 └── 动态线程
```

两者最主要的区别是线程控制块和线程栈的内存来源不同。

##### 静态线程

静态线程的 TCB 和线程栈由程序提前定义：

```c
static struct rt_thread thread;

static rt_uint8_t thread_stack[1024];
```

然后使用：

```c
rt_thread_init();
```

进行初始化。

##### 动态线程

动态线程使用：

```c
rt_thread_create();
```

创建，由系统从动态内存中申请线程控制块和线程栈。

两种方式对比如下：

| 对比项         | 动态线程             | 静态线程           |
| -------------- | -------------------- | ------------------ |
| API            | `rt_thread_create()` | `rt_thread_init()` |
| TCB            | 系统动态分配         | 用户提前定义       |
| 线程栈         | 系统动态分配         | 用户提前定义       |
| 内存来源       | Heap                 | 静态存储区         |
| 灵活性         | 较高                 | 相对较低           |
| 资源确定性     | 较低                 | 较高               |
| 运行时动态申请 | 需要                 | 不需要             |

动态线程更加灵活，但依赖运行时动态内存，可能受到 Heap 空间不足和内存碎片的影响。

静态线程的 TCB 和栈空间在程序运行前基本已经确定，因此：

```text
内存大小确定
内存位置确定
资源占用确定
运行行为更加可预测
```

所以在工业控制、汽车电子以及对可靠性和实时确定性要求较高的系统中，通常更加倾向使用静态线程。

------

### 3. 线程状态机与调度算法

#### 3.1 RT-Thread 线程五态模型

RT-Thread 中线程主要存在以下五种状态：

| 状态   | 宏定义              | 含义                         |
| ------ | ------------------- | ---------------------------- |
| 初始态 | `RT_THREAD_INIT`    | 线程已经创建，但还未参与调度 |
| 就绪态 | `RT_THREAD_READY`   | 已具备运行条件，等待 CPU     |
| 运行态 | `RT_THREAD_RUNNING` | 当前正在 CPU 上执行          |
| 挂起态 | `RT_THREAD_SUSPEND` | 暂时不能运行，不参与正常调度 |
| 关闭态 | `RT_THREAD_CLOSE`   | 线程生命周期已经结束         |

线程状态变化可以表示为：

```text
                    rt_thread_startup()
        INIT  ----------------------------> READY
                                               │
                                               │ Scheduler
                                               ↓
                                           RUNNING
                                           /      \
                                          /        \
                 delay / suspend / IPC   /          \ 线程结束
                                        ↓            ↓
                                    SUSPEND        CLOSE
                                        │
                                        │ resume
                                        │ timeout
                                        │ 条件满足
                                        ↓
                                      READY
```

需要注意：

**READY 并不代表线程正在运行。**

READY 只表示线程已经满足运行条件，仍需等待调度器分配 CPU。

------

#### 3.2 常用线程状态转换 API

| API / 事件            | 典型状态变化      | 作用             |
| --------------------- | ----------------- | ---------------- |
| `rt_thread_create()`  | → INIT            | 动态创建线程     |
| `rt_thread_init()`    | → INIT            | 静态初始化线程   |
| `rt_thread_startup()` | INIT → READY      | 启动线程         |
| 调度器 Scheduler      | READY → RUNNING   | 分配 CPU         |
| `rt_thread_suspend()` | RUNNING → SUSPEND | 挂起线程         |
| `rt_thread_resume()`  | SUSPEND → READY   | 恢复线程         |
| `rt_thread_mdelay()`  | RUNNING → SUSPEND | 当前线程主动延时 |
| Timeout               | SUSPEND → READY   | 延时结束         |
| 线程结束              | → CLOSE           | 结束线程生命周期 |

其中：

```c
rt_thread_mdelay(ms);
```

不仅表示延时，同时还会使当前线程暂时退出调度。

```text
RUNNING
   ↓
rt_thread_mdelay()
   ↓
SUSPEND
   ↓
延时到期
   ↓
READY
```

这使 CPU 可以在当前线程等待期间运行其他线程。

------

#### 3.3 线程优先级

RT-Thread 根据线程优先级决定线程被调度的先后顺序。

规则为：

```text
优先级数字越小
        ↓
实际线程优先级越高
```

例如：

```text
Thread A：priority = 10
Thread B：priority = 20
```

当 A 和 B 同时处于 READY 状态时：

```text
Thread A
```

优先获得 CPU。

------

#### 3.4 抢占式调度

RT-Thread 支持基于优先级的抢占式调度。

如果当前正在运行一个低优先级线程，而此时一个更高优先级线程进入 READY：

```text
低优先级线程 RUNNING
        ↓
高优先级线程 READY
        ↓
调度器比较优先级
        ↓
高优先级线程抢占
        ↓
高优先级线程 RUNNING
```

例如本实验线程配置：

```text
th_monitor = priority 10

th_led1    = priority 16
th_led2    = priority 16
```

因此 `th_monitor` 在延时结束重新进入 READY 后，其优先级高于两个 LED 工作线程，可以优先获得 CPU。

这种机制能够保证更重要、更紧急的线程及时得到响应。

------

#### 3.5 时间片轮转

当多个线程具有相同优先级时，RT-Thread 可以使用时间片轮转机制让这些线程轮流使用 CPU。

例如：

```text
Thread A
priority = 16
time slice = 5 ticks

Thread B
priority = 16
time slice = 5 ticks
```

在两个线程持续处于可运行状态时，可以按照时间片进行轮转：

```text
Thread A
运行一个时间片
      ↓
Thread B
运行一个时间片
      ↓
Thread A
      ↓
...
```

时间片单位为：

```text
Tick
```

其实际时间长度由系统 Tick 频率决定。

例如系统配置为：

```text
RT_TICK_PER_SECOND = 1000
```

则：

```text
1 tick = 1 ms
```

但如果 Tick 频率不同，实际时间也会发生变化，因此不能简单认为：

```text
1 tick 永远等于 1 ms
```

需要根据实际系统配置确定。

------

#### 3.6 抢占、时间片和主动阻塞的区别

线程切换可能由不同原因产生。

```text
1. 高优先级线程进入 READY
        ↓
   抢占式调度

2. 同优先级线程时间片耗尽
        ↓
   时间片轮转

3. 当前线程调用 delay / 等待 IPC
        ↓
   主动阻塞，让出 CPU
```

例如：

```c
rt_thread_mdelay(500);
```

会使当前线程主动进入挂起状态。

这种线程切换与“时间片耗尽”不是同一个概念。

因此在多线程实验中，如果线程主动调用 `rt_thread_mdelay()`，看到线程交替运行时不能简单认为一定是时间片轮转造成的。

------

### 4. Part 1 小结

RT-Thread 启动后会完成板级初始化、内核初始化、系统线程创建以及调度器启动，最终由 Main Thread 执行用户 `main()`。

RT-Thread 使用统一的对象模型管理线程等内核对象，线程的运行信息保存在 `struct rt_thread` 线程控制块中，其中状态、优先级、时间片和栈指针是线程调度的重要依据。

线程可以通过动态方式和静态方式创建。动态线程使用方便、灵活，但依赖运行时动态内存；静态线程资源提前分配，确定性和可预测性更高。

RT-Thread 线程在 INIT、READY、RUNNING、SUSPEND 和 CLOSE 五种状态之间转换。调度器首先根据线程优先级进行抢占式调度，对于相同优先级的线程则可以使用时间片轮转机制共享 CPU。

这些机制共同构成了 RT-Thread 多线程运行和实时调度的基础。

---

## Part 2：多线程实战实验报告

### 1. 实验目的

本实验基于 STM32F407 星火 1 号开发板和 RT-Thread，实现动态线程、静态线程和监控线程，学习线程的创建、运行、挂起、恢复以及线程状态监控方法。

实验主要完成以下功能：

- 动态创建 `th_led1`，控制红色 LED 周期翻转，并打印运行计数和系统 Tick。
- 静态初始化 `th_led2`，控制蓝色 LED 周期翻转，并打印运行计数。
- 动态创建高优先级监控线程 `th_monitor`，周期查看两个工作线程的运行状态和剩余栈空间。
- 使用 KEY0 控制 `th_led1` 的挂起与恢复。
- 使用 `INIT_APP_EXPORT()` 自动加载实验模块。
- 使用 `MSH_CMD_EXPORT()` 导出 `thread_dump` 调试命令。

---

### 2. 实验环境

- 开发板：STM32F407 星火 1 号
- 操作系统：RT-Thread
- 红色 LED：PF12
- 蓝色 LED：PF11
- KEY0：PC0
- GPIO 引脚定义头文件：

```c
#include "drv_common.h"
```

板载 LED 为低电平点亮：

```text
PIN_LOW  -> LED 点亮
PIN_HIGH -> LED 熄灭
```

---

### 3. 线程设计

本实验共创建 3 个线程：

| 线程名称     | 创建方式   | 优先级 |     栈大小 |  时间片 | 功能                           |
| ------------ | ---------- | -----: | ---------: | ------: | ------------------------------ |
| `th_led1`    | 动态创建   |     16 | 1024 Bytes | 5 ticks | 翻转 LED_R，打印计数和 Tick    |
| `th_led2`    | 静态初始化 |     16 | 1024 Bytes | 5 ticks | 翻转 LED_B，打印计数           |
| `th_monitor` | 动态创建   |     10 | 1024 Bytes | 5 ticks | 按键检测、线程状态及栈空间监控 |

其中：

```text
th_monitor priority = 10
th_led1    priority = 16
th_led2    priority = 16
```

RT-Thread 中优先级数字越小，实际优先级越高，因此 `th_monitor` 的优先级高于两个 LED 工作线程。

---

### 4. 自动初始化实验

实验模块通过：

```c
INIT_APP_EXPORT(thread_study_init);
```

注册为 APP 级自动初始化函数，没有在 `main()` 中手动调用。

系统启动后的实际输出：

```text
thread_study_init success!
th_led1 create success!
th_led2 init success!
th_monitor create success!
```

结果表明：

- `INIT_APP_EXPORT()` 自动初始化成功。
- `th_led1` 动态创建成功。
- `th_led2` 静态初始化成功。
- `th_monitor` 动态创建成功。

---

### 5. 动态线程 th_led1 实验

`th_led1` 使用：

```c
rt_thread_create()
```

动态创建，参数为：

```text
栈大小：1024 Bytes
优先级：16
时间片：5 ticks
```

线程循环翻转红色 LED，并通过：

```c
rt_tick_get()
```

读取当前系统 Tick。

部分实际串口数据：

```text
[th_led1] count = 1, tick = 7
[th_led1] count = 2, tick = 510
[th_led1] count = 3, tick = 1013
[th_led1] count = 4, tick = 1516
[th_led1] count = 5, tick = 2019
[th_led1] count = 6, tick = 2522
```

相邻 Tick 差值：

```text
510  - 7    = 503 ticks
1013 - 510  = 503 ticks
1516 - 1013 = 503 ticks
```

线程中设置：

```c
rt_thread_mdelay(500);
```

实际运行中相邻输出约相差 503 ticks，与设置的 500 ms 周期基本一致。多出的少量 Tick 主要来自 LED 操作、串口打印和线程调度等程序执行时间。

实验过程中红色 LED 能够持续周期翻转，`count` 和 Tick 均持续增加，说明动态线程运行正常。

---

### 6. 静态线程 th_led2 实验

`th_led2` 使用静态方式初始化：

```c
static struct rt_thread th_led2;
static rt_uint8_t th_led2_stack[1024];
```

通过：

```c
rt_thread_init()
```

完成线程初始化。

运行时实际输出：

```text
[th_led2] count = 1
[th_led1] count = 1, tick = 11

[th_led2] count = 2
[th_led1] count = 2, tick = 514

[th_led2] count = 3
[th_led1] count = 3, tick = 1017

[th_led2] count = 4
[th_led1] count = 4, tick = 1520
```

两个线程均能够持续运行，红色 LED 和蓝色 LED 均正常周期翻转。

`th_led1` 和 `th_led2` 的优先级均为 16，时间片均设置为 5 ticks。

需要注意的是，两个线程内部都调用了：

```c
rt_thread_mdelay(500);
```

因此当前实验中两个线程的交替运行主要包含主动延时让出 CPU 的影响，不能仅通过当前串口输出直接证明 5 ticks 时间片轮转。

---

### 7. th_monitor 线程实验

`th_monitor` 使用动态方式创建：

```text
优先级：10
栈大小：1024 Bytes
时间片：5 ticks
```

监控线程周期获取 `th_led1` 和 `th_led2` 的：

- 当前线程状态
- 剩余栈空间

实际输出：

```text
----- thread monitor -----
th_led1: state=SUSPEND, stack_free=788 Bytes
th_led2: state=SUSPEND, stack_free=792 Bytes
--------------------------
```

运行一段时间后：

```text
----- thread monitor -----
th_led1: state=SUSPEND, stack_free=780 Bytes
th_led2: state=SUSPEND, stack_free=776 Bytes
--------------------------
```

后续剩余栈空间基本稳定在：

```text
th_led1：约 780 Bytes
th_led2：约 776 Bytes
```

说明两个线程当前 1024 Bytes 的栈空间均具有较充足余量。

两个 LED 线程在监控时经常显示：

```text
SUSPEND
```

这是因为两个工作线程均周期执行：

```c
rt_thread_mdelay(500);
```

线程在延时期间会暂时处于挂起状态，因此该现象属于正常运行结果。

---

### 8. KEY0 挂起与恢复实验

KEY0 用于控制 `th_led1` 的运行状态。

第一次按键后实际输出：

![image-20260819024604667](figures/image-20260819024604667.png)

```text
[th_led1] count = 12, tick = 5551
[KEY] request th_led1 suspend
[th_led1] suspend self
[th_led2] count = 13
```

随后只有 `th_led2` 继续运行：

```text
[th_led2] count = 14
[th_led2] count = 15
[th_led2] count = 16
[th_led2] count = 17
```

监控线程显示：

```text
----- thread monitor -----
th_led1: state=SUSPEND, stack_free=780 Bytes
th_led2: state=SUSPEND, stack_free=776 Bytes
--------------------------
```

此时 `th_led1` 的计数停止在：

```text
count = 12
```

说明 `th_led1` 已成功挂起，而 `th_led2` 不受影响，继续正常运行。

再次按下 KEY0 后：

![image-20260819024732173](figures/image-20260819024732173.png)

```text
[KEY] th_led1 resume request
[th_led1] resumed
[th_led1] count = 13, tick = 10298
```

随后：

```text
[th_led1] count = 14, tick = 10802
[th_led1] count = 15, tick = 11305
[th_led1] count = 16, tick = 11808
```

可以看到挂起前：

```text
count = 12
```

恢复后：

```text
count = 13
```

线程从原有执行位置继续运行，而不是重新开始计数。

该实验验证了：

```text
RUNNING
   ↓
rt_thread_suspend()
   ↓
SUSPEND

SUSPEND
   ↓
rt_thread_resume()
   ↓
READY
   ↓
Scheduler
   ↓
RUNNING
```

---

### 9. MSH 自定义命令实验

使用：

```c
MSH_CMD_EXPORT(thread_dump, show thread status and stack usage);
```

将 `thread_dump` 导出为 MSH 命令。

在 FinSH/MSH 中执行：

```text
msh > thread_dump
```

实际运行结果：

![image-20260819024513682](figures/image-20260819024513682.png)

```text
========== thread dump ==========
th_led1:
  state      : SUSPEND
  priority   : 16
  stack free : 780 Bytes

th_led2:
  state      : SUSPEND
  priority   : 16
  stack free : 776 Bytes
=================================
```

结果表明 `thread_dump` 命令能够正常执行，并正确读取：

- 线程状态
- 当前优先级
- 剩余栈空间

说明 `MSH_CMD_EXPORT()` 导出功能正常。

---

### 10. 实验中出现的问题及解决方法

#### 10.1 `GET_PIN` 未定义

首次编译时出现：

```text
warning: implicit declaration of function 'GET_PIN'
error: 'F' undeclared
```

原因是当前 BSP 中 `GET_PIN` 宏对应的头文件没有包含。

解决方法：

```c
#include "drv_common.h"
```

加入后重新编译，GPIO 引脚宏可以正常使用。

---

#### 10.2 `rt_thread_suspend()` 触发断言

最初尝试在 `th_monitor` 中直接执行：

```c
rt_thread_suspend(th_led1);
```

运行时出现：

```text
(thread == rt_thread_self()) assertion failed
at function: rt_thread_suspend
```

当前工程所使用的 RT-Thread 实现要求线程通过 `rt_thread_suspend()` 挂起自身。

解决方法：

按键线程只设置暂停标志：

```text
KEY0
 ↓
设置暂停请求
```

随后由 `th_led1` 自身执行：

```c
rt_thread_suspend(rt_thread_self());
rt_schedule();
```

恢复时由监控线程执行：

```c
rt_thread_resume(th_led1);
```

修改后挂起和恢复功能运行正常。

---

#### 10.3 多线程串口打印出现交叉

运行过程中出现过：

```text
[th_led1] count = 5, tick[th_led2] count = 5
 = 2026
```

以及：

```text
0th_led1] count = 15, tick = 706[th_led2] count = 15
```

这是因为多个线程同时使用：

```c
rt_kprintf()
```

输出数据，线程切换过程中不同线程的串口字符串发生交叉。

该问题不会影响线程本身的正常运行。

实验中通过观察完整周期输出以及 `thread_dump` 独立结果判断线程状态。

---

### 11. 实验结果分析

本实验成功实现了一个动态工作线程、一个静态工作线程以及一个高优先级监控线程。

`th_led1` 使用 `rt_thread_create()` 动态创建，其 TCB 和线程栈由系统动态分配；`th_led2` 使用 `rt_thread_init()` 静态初始化，其 TCB 和 1024 Bytes 栈空间由程序提前定义，两种线程均能够正常运行。

两个 LED 工作线程优先级均为 16，监控线程优先级为 10，因此监控线程具有更高调度优先级。监控线程可以周期读取两个工作线程的状态和剩余栈空间。

KEY0 实验中，`th_led1` 挂起后计数停止，而 `th_led2` 继续运行；恢复后 `th_led1` 从原有计数继续执行，验证了线程挂起、恢复以及上下文保持过程。

通过 `thread_dump` 命令可以在 MSH 中手动读取线程状态、优先级和栈空间，验证了 RT-Thread Shell 命令导出机制。

---

### 12. 实验结论

本实验完成了 RT-Thread 多线程编程的主要功能：

- 动态线程创建与启动
- 静态线程初始化与启动
- 多线程并发运行
- 高优先级监控线程
- 线程状态监控
- 线程剩余栈空间检测
- 按键控制线程挂起和恢复
- `INIT_APP_EXPORT()` 自动初始化
- `MSH_CMD_EXPORT()` 自定义 Shell 命令

通过实际运行结果，对 RT-Thread 的线程创建方式、线程状态变化、优先级调度和线程控制机制有了进一步理解。

