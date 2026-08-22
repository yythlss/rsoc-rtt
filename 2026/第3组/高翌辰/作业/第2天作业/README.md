# 第二天作业：RT-Thread 多线程 LED 与按键控制

## Part 1：理论整理

### DAY2：RT-Thread 启动流程、内核基础与多线程编程

#### 1. STM32F407 启动流程与 RT-Thread 引导机制

##### 1.1 从上电复位到进入 `main()` 的调用链

STM32F407 上电或复位后，Cortex-M4 会从中断向量表取得初始主栈指针（MSP）和复位入口地址，然后执行 `Reset_Handler`。启动文件完成 C 运行环境准备后，将控制权交给 RT-Thread。

```text
上电/复位
  ↓
Reset_Handler（启动文件）
  ├─ 设置栈指针，初始化 .data 和 .bss
  ├─ SystemInit()：完成时钟等芯片基础配置
  └─ entry()
       ↓
rtthread_startup()
  ├─ 关闭中断
  ├─ rt_hw_board_init()：板级初始化
  │    └─ rt_components_board_init()：执行 Board 自动初始化
  ├─ 初始化系统 Tick、调度器等内核设施
  ├─ rt_application_init()：创建 Main 线程
  ├─ 创建 Idle 线程（及可选的软件定时器线程）
  └─ rt_system_scheduler_start()：启动调度器
       ↓
Main 线程的 main_thread_entry()
  ├─ rt_components_init()：执行其余自动初始化
  └─ 用户 main()
```

因此，用户的 `main()` 不是复位后最早执行的 C 函数。它运行在 RT-Thread 创建的 Main 线程中，只有调度器启动并选中该线程后才会执行。

##### 1.2 `INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT` 的自动初始化原理

自动初始化的核心是：**编译期注册、链接期汇总、运行期按顺序调用**。

```c
static int led_init(void)
{
    /* 初始化 LED */
    return RT_EOK;
}
INIT_BOARD_EXPORT(led_init);
```

上述宏会把 `led_init` 的函数指针放入指定的初始化段中；例如 `INIT_BOARD_EXPORT` 对应 Board 阶段，`INIT_APP_EXPORT` 对应应用阶段。链接脚本将这些段保留并排成连续的函数指针表。

启动时，RT-Thread 分阶段遍历该表：

```text
INIT_BOARD_EXPORT(fn)
  → 注册到 Board 初始化段
  → rt_components_board_init() 调用 fn

INIT_APP_EXPORT(fn)
  → 注册到应用初始化段
  → Main 线程中的 rt_components_init() 调用 fn
  → 用户 main()
```

这样，驱动和组件只需在自身文件中注册初始化函数，无需在 `main()` 中逐个手动调用；内核仍能保证板级初始化先于组件和应用初始化执行。

#### 2. 内核基础与对象模型

##### 2.1 对象容器与控制块

RT-Thread 使用 C 语言实现轻量的面向对象思想。线程、信号量、互斥量、事件、邮箱、消息队列和定时器等都属于内核对象；每个对象都有记录自身属性和状态的控制块。

不同类型的对象具有名称、类型、标志和链表节点等共同信息。内核为每类对象维护一个对象容器，将同类型对象组织成链表，以便统一查找、统计和管理。

```text
对象容器
  ├─ 线程对象：线程 1、线程 2、……
  ├─ 信号量对象：信号量 1、信号量 2、……
  ├─ 互斥量对象：互斥量 1、互斥量 2、……
  └─ 其他对象：事件、邮箱、消息队列、定时器等
```

线程控制块 `struct rt_thread` 保存线程的栈、入口函数、状态与调度信息，是调度器管理线程的依据。常用关键成员如下：

| 成员 | 作用 |
| --- | --- |
| `stat` | 线程当前状态，如初始、就绪、运行、挂起、关闭。 |
| `current_priority` | 当前调度优先级；数值越小，优先级越高。 |
| `init_tick` | 线程初始时间片；同优先级线程轮转时决定一次可运行的 Tick 数。 |
| `sp` | 当前栈指针；线程切换时据此保存或恢复寄存器现场。 |
| `entry`、`parameter` | 线程入口函数及其参数。 |
| `stack_addr`、`stack_size` | 线程栈的地址和大小。 |

##### 2.2 静态对象与动态对象

以线程为例，RT-Thread 支持静态和动态两种创建方式：

| 对比项 | 静态线程 | 动态线程 |
| --- | --- | --- |
| 创建 API | `rt_thread_init()` | `rt_thread_create()` |
| 控制块与栈 | 用户预先提供，通常位于 `.bss` 或 `.data` | 内核从堆中运行时申请 |
| 生命周期 | 一般伴随系统运行；分离后内核不释放用户内存 | 删除后由系统回收控制块和栈 |
| 风险 | 栈预留不足或栈过小 | 堆不足、内存碎片、遗漏释放 |
| 适用场景 | 核心控制、长期运行、资源固定的任务 | 按需创建、短生命周期或可动态启停的任务 |

静态线程的地址和大小可在系统设计阶段确定，因此高可靠场景常强制使用：

1. 可预先评估 RAM 占用，资源更可预测；
2. 不依赖运行时堆分配，避免关键任务因堆不足而创建失败；
3. 避免频繁申请和释放造成的内存碎片；
4. 启动时间和最坏执行时间更容易分析。

但静态线程仍需要合理估算栈大小，不能因此忽略栈溢出风险。

#### 3. 线程状态机与调度算法

##### 3.1 五态模型

| 状态 | 宏 | 含义 |
| --- | --- | --- |
| 初始态 | `RT_THREAD_INIT` | 线程已创建，但尚未进入就绪队列。 |
| 就绪态 | `RT_THREAD_READY` | 线程具备运行条件，等待调度器分配 CPU。 |
| 运行态 | `RT_THREAD_RUNNING` | 当前正在占用 CPU 执行。单核 MCU 同一时刻只有一个运行线程。 |
| 挂起态 | `RT_THREAD_SUSPEND` | 正在延时或等待信号量、消息等资源，暂时不能运行。 |
| 关闭态 | `RT_THREAD_CLOSE` | 线程已结束或被删除，不再参与调度。 |

```text
创建：rt_thread_init() / rt_thread_create()
  ↓
初始态（INIT）
  │ rt_thread_startup()
  ↓
就绪态（READY） ←──── 延时到期、资源可用、rt_thread_resume() ────┐
  │ 调度器选中                                                       │
  ↓                                                                  │
运行态（RUNNING） ── rt_thread_delay() / 等待 IPC / 自身挂起 ──→ 挂起态（SUSPEND）
  │
  └─ 线程入口返回、rt_thread_delete() 或 rt_thread_detach()
       ↓
    关闭态（CLOSE）
```

运行线程被更高优先级线程抢占，或同优先级时间片耗尽时，会回到就绪态；调度器随后选择下一线程运行。

##### 3.2 抢占式调度

RT-Thread 采用基于优先级的抢占式调度：调度器总是选择**最高优先级（数值最小）**的就绪线程运行。

```text
低优先级线程 A 正在运行
  ↓
高优先级线程 B 因信号量释放或消息到达而变为就绪态
  ↓
调度器保存 A 的现场并切换到 B
```

因此，低优先级线程即使时间片尚未用完，也不能阻止更高优先级的就绪线程运行。B 延时、等待资源或结束后，A 才有机会继续执行。

##### 3.3 时间片轮转

时间片轮转仅用于**多个同优先级线程同时处于就绪态**的情况，不会影响高优先级线程优先运行的规则。

创建线程时传入的 `tick` 参数会写入 `init_tick`。每次系统 Tick 到来，当前线程的剩余时间片递减；时间片用尽后，调度器将它放到同优先级就绪队列尾部，切换到下一个同优先级线程。

```text
线程 A、B：优先级均为 10，时间片均为 5 Tick

A 运行 5 Tick → B 运行 5 Tick → A 再运行 5 Tick → ……
```

若没有同优先级的竞争线程，时间片用尽后通常仍会继续运行当前线程；若有更高优先级线程就绪，仍优先运行高优先级线程。线程也可调用 `rt_thread_yield()` 主动让出 CPU，给同优先级线程运行机会。

## Part 2：实验结果分析

### 1. 硬件运行现象

`th_led1` 与 `th_led2` 分别每隔 500 ms 读取并反转对应引脚电平，因此两路 LED 都以约 500 ms 的间隔改变一次亮灭状态，完成一次完整的亮灭循环约为 1 s。由于两个工作线程以相同的优先级、时间片和延时周期启动，肉眼观察时两路 LED 的翻转节奏基本一致。

按下左侧按键后，监控线程在下一个 2 s 监控周期处理挂起请求，`th_led1` 停止运行，红色 LED 保持按键触发瞬间的当前状态，不再继续翻转；`LED_B` 所在的 `th_led2` 不受影响，仍然每 500 ms 翻转。再次按下按键后，`th_led1` 被恢复运行，红色 LED 继续以 500 ms 的间隔翻转。


### 2. 现象与结论

串口启动后，`th_led1` 和 `th_led2` 分别输出 LED 翻转计数；`th_led1` 同时打印系统 tick。监控线程约每 2 秒输出一次状态和剩余栈空间。

第一次按键后，日志出现 `BTN_LEFT: th_led1 suspend requested`，随后 `th_led1` 停止计数而 `th_led2` 保持运行；第二次按键后出现 `BTN_LEFT: th_led1 resumed`，`th_led1` 从原计数继续输出，说明挂起和恢复功能正常。

`thread_dump` 中的 `suspend` 还可能表示线程正处于 `rt_thread_mdelay()` 延时状态，并不一定是按键造成的手动挂起。串口偶尔出现字符交错，是多个线程同时调用 `rt_kprintf()` 导致的 UART 输出交织，不影响线程调度结果。

### 3. FinSH 串口日志（节选）

```text
 \ | /
- RT -     Thread Operating System
  / | \     4.1.1 build Aug 19 2026 16:25:07
hello!
th_led2 count: 0
th_led1 count: 0, tick: 4
...
th_led1: suspend stack: 780/1024 bytes
th_led2: suspend stack: 788/1024 bytes
...
BTN_LEFT: th_led1 suspend requested
th_led1: suspend stack: 780/1024 bytes
th_led2 count: 16
th_led2 count: 17
th_led2 count: 18
...
BTN_LEFT: th_led1 resumed
th_led1: ready   stack: 780/1024 bytes
th_led2: ready   stack: 788/1024 bytes
th_led1 count: 16, tick: 12053
th_led2 count: 25
```
