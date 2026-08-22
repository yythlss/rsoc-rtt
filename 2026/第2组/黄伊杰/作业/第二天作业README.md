# RT-Thread 理论知识梳理

---

## 1. STM32F407 启动流程与 RT-Thread 引导机制

### 1.1 启动路径：从上电复位到 `main()` 的完整调用链

STM32F407 上电复位后，CPU 从向量表读取初始 MSP 和复位向量，跳转到 `Reset_Handler`。启动文件 `startup_xx.S` 根据编译器不同，通过不同入口进入 C 环境：

| 编译器 | 入口函数 |
|--------|----------|
| MDK (ARM Compiler) | `$Sub$$main()` |
| IAR | `__low_level_init()` |
| GCC | `entry()` |

三条路径最终统一汇入 **`rtthread_startup()`**，这是 RT-Thread 的核心启动函数。

#### `rtthread_startup()` 完整调用链

```
rtthread_startup()
│
├─ 1. rt_hw_interrupt_disable()
│     └─ 关闭全局中断，防止初始化过程中被中断打断
│
├─ 2. rt_hw_board_init()
│     ─ rt_components_board_init()
│           └─ 执行所有 board init functions（INIT_BOARD_EXPORT 注册的函数）
│
├─ 3. rt_show_version()
│     └─ 打印 RT-Thread 版本信息
│
├─ 4. rt_system_timer_init()
│     └─ 初始化系统定时器链表
│
├─ 5. rt_system_scheduler_init()
│     └─ 初始化调度器（优先级位图、就绪队列等）
│
├─ 6. rt_system_signal_init()
│     └─ 初始化信号机制
│
├─ 7. rt_application_init()
│     └─ rt_thread_init(main_thread_entry)
│           └─ 创建 main 线程（用户应用入口）
│
├─ 8. rt_system_timer_thread_init()
│     └─ rt_thread_init(rt_thread_timer_entry)
│           └─ 创建定时器线程
│
├─ 9. rt_thread_idle_init()
│     └─ rt_thread_init(rt_thread_idle_entry)
│           └─ 创建空闲线程
│
└─ 10. rt_system_scheduler_start()
       └─ 启动调度器，根据调度规则执行
             ├─ main_thread_entry()
             │     └─ rt_components_init()
             │           ├─ pre-initialization functions  (INIT_PREV_EXPORT)
             │           ├─ device init functions         (INIT_DEVICE_EXPORT)
             │           ├─ components init functions     (INIT_COMPONENT_EXPORT)
             │           ├─ environment init functions    (INIT_ENV_EXPORT)
             │           └─ applications init functions   (INIT_APP_EXPORT)
             │     └─ main()                              ← 用户程序入口
             ├─ rt_thread_timer_entry()                   ← 定时器线程
             ├─ rt_thread_idle_entry()                    ← 空闲线程
             └─ 其他线程
```

> **关键文件路径参考**：
> - 启动汇编：`startup_stm32f407xx.s`
> - RT-Thread 启动入口：`components.c` → `rtthread_startup()`
> - 板级初始化：`board.c` → `rt_hw_board_init()`

### 1.2 自动初始化原理：`INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT`

RT-Thread 的自动初始化机制利用 **编译器链接脚本的段（Section）排列特性**，实现了无需手动注册即可按阶段自动调用初始化函数的功能。

#### 底层实现原理

**核心思想**：利用 `__attribute__((section("name")))` 将函数指针放入链接器指定的自定义段中，链接后这些函数指针在内存中连续排列，运行时通过遍历该段即可依次调用。

**宏定义展开（以 `INIT_BOARD_EXPORT` 为例）**：

```c
#define INIT_BOARD_EXPORT(fn)                                      \
    INIT_EXPORT(fn, "0.end")

#define INIT_EXPORT(fn, level)                                     \
    const rt_init_fn_t fn##_export RT_USED                          \
    __attribute__((section(".rti_fn." level))) = fn;
```

**链接脚本中的段布局**：

```
SECTIONS
{
    .rti_fn : {
        __rti_fn_start = .;
        *(.rti_fn.0)        /* INIT_BOARD_EXPORT */
        *(.rti_fn.0.end)
        *(.rti_fn.1)        /* INIT_PREV_EXPORT */
        *(.rti_fn.1.end)
        *(.rti_fn.2)        /* INIT_DEVICE_EXPORT */
        *(.rti_fn.2.end)
        *(.rti_fn.3)        /* INIT_ENV_EXPORT */
        *(.rti_fn.3.end)
        *(.rti_fn.4)        /* INIT_APP_EXPORT */
        *(.rti_fn.4.end)
        *(.rti_fn.5)        /* INIT_COMPONENT_EXPORT */
        *(.rti_fn.5.end)
        *(.rti_fn.6)        /* INIT_ENV_EXPORT (app) */
        *(.rti_fn.6.end)
        __rti_fn_end = .;
    }
}
```

**各阶段含义与调用时机**：

| 阶段宏 | 段名 | 调用时机 | 典型用途 |
|--------|------|----------|----------|
| `INIT_BOARD_EXPORT` | `.rti_fn.0` | `rt_hw_board_init()` 内，调度器启动前 | 硬件外设初始化（GPIO、UART、SPI） |
| `INIT_PREV_EXPORT` | `.rti_fn.1` | `main_thread_entry()` → `rt_components_init()` | 内核预初始化 |
| `INIT_DEVICE_EXPORT` | `.rti_fn.2` | 同上 | 设备驱动初始化 |
| `INIT_COMPONENT_EXPORT` | `.rti_fn.3` | 同上 | 软件包、中间件初始化 |
| `INIT_ENV_EXPORT` | `.rti_fn.4` | 同上 | 文件系统挂载、网络配置 |
| `INIT_APP_EXPORT` | `.rti_fn.5` | 同上 | 用户应用线程创建、服务启动 |

**`__attribute__((used))`** 确保即使函数指针未被直接引用，链接器也不会将其优化丢弃。

---

## 2. 内核基础与对象模型

### 2.1 线程控制块定义

RT-Thread 内核采用 **C 语言模拟面向对象** 的设计模式。所有内核对象继承自公共基类 `struct rt_object`，每个具体对象的控制块在结构体首部嵌入 `rt_object`，实现"继承"关系。

`struct rt_thread` 定义如下：

```c
struct rt_thread
{
    struct rt_object    parent;         /* 继承的基类成员（必须在首位） */

    /* 栈指针与入口指针 */
    void               *sp;             /**< 栈指针 */
    void               *entry;          /**< 入口函数指针 */
    void               *parameter;      /**< 参数 */
    void               *stack_addr;     /**< 栈地址指针 */
    rt_uint32_t         stack_size;     /**< 栈大小 */
};
```

#### `struct rt_thread` 关键成员详解

| 成员 | 作用 |
|------|------|
| `parent` | 继承自 `struct rt_object`，包含对象名称、类型、标志、链表节点等公共属性 |
| `sp` | 栈指针，线程上下文切换时指向线程栈中保存的寄存器现场 |
| `entry` | 线程入口函数地址 |
| `parameter` | 传递给入口函数的参数 |
| `stack_addr` | 线程栈的起始地址 |
| `stack_size` | 线程栈大小（字节） |

### 2.2 线程属性 — 线程栈

RT-Thread 的线程具有 **独立的栈空间**，当进行线程切换时，会将当前线程的上下文存在栈中；当线程要恢复运行时，再从栈中读取上下文信息进行恢复。

#### 线程栈的用途

1. **上下文保存**：线程切换时，CPU 寄存器（PSR、PC、LR、R0~R3、R12 等）压入栈中保存，恢复时从栈中弹出。
2. **局部变量存储**：函数中的局部变量从线程栈空间中申请。初始时局部变量会从寄存器中分配（ARM 架构），当这个函数再调用另一个函数时，这些局部变量将放入栈中。

#### 线程栈的增长方向

线程栈的增长方向与芯片架构密切相关。RT-Thread 3.1.0 以前的版本，均只支持栈由 **高地址向低地址** 的增长方式。对于 ARM Cortex-M 架构，线程栈构造如下：

```
栈顶（高地址）
┌─────────┐
│   PSR   │
│   PC    │ ← 线程入口点
│   LR    │ ← 线程退出点
│   R12   │
│   R3    │
│   R2    │
│   R1    │
│   R0    │ ← 线程入口参数
│   R11   │
│   ...   │
│   R4    │
└─────────┘
栈底（低地址）← 栈：自顶往下增长
```

### 2.3 内存分配方式：静态对象 vs 动态对象

| 维度 | 静态对象 | 动态对象 |
|------|----------|----------|
| **分配位置** | 编译时分配在 BSS/Data 段（全局/静态变量区域） | 运行时从堆内存（heap）中动态分配 |
| **创建方式** | `rt_thread_init()` 或 `INIT_BOARD_EXPORT` 自动注册 | `rt_thread_create()` → 内部调用 `rt_malloc()` |
| **生命周期** | 与系统同生命周期，上电即存在，不可释放 | 从创建到显式调用 `rt_thread_delete()` 释放 |
| **释放机制** | 无需释放，编译期确定 | `rt_free()` 归还堆内存；若忘记释放则造成内存泄漏 |
| **内存碎片** | 无碎片问题 | 频繁创建/删除可能导致堆碎片化 |
| **启动速度** | 无运行时分配开销，启动更快 | 需要堆分配，有一定开销 |
| **灵活性** | 低，数量和大小编译时固定 | 高，运行时按需创建 |

#### 高可靠场景强制使用静态线程的原因

1. **确定性保证**：静态线程的内存在上电前就已确定，不存在运行时分配失败的可能。在内存紧张的嵌入式系统中，`rt_malloc()` 可能因堆不足返回 `RT_NULL`，导致线程创建失败——这在安全关键系统中不可接受。
2. **无内存碎片风险**：动态分配/释放长期运行后会产生堆碎片，极端情况下即使总空闲内存足够，也无法分配连续大块内存。
3. **可预测的启动时序**：静态线程无需等待堆初始化完成即可使用，适合在系统最早期就启动的关键守护线程。
4. **故障隔离**：堆管理器本身如果因 bug 损坏，动态线程可能受影响；静态线程不受堆状态影响。
5. **认证合规**：DO-178C、IEC 61508、ISO 26262 等安全标准通常要求证明动态内存分配的安全性，使用静态分配可大幅简化认证过程。

---

## 3. 线程状态机与调度算法

### 3.1 五态模型

RT-Thread 线程共有 **5 种状态**：

| 状态 | 宏定义 | 描述 |
|------|--------|------|
| **初始状态** | `RT_THREAD_INIT` | 线程刚开始创建，还没开始运行时就处于初始状态；在初始状态下，线程不参与调度。 |
| **就绪状态** | `RT_THREAD_READY` | 在就绪状态下，线程按照优先级排队，等待被执行；一旦当前线程运行完毕让出处理器，操作系统会马上寻找最高优先级的就绪态线程运行。 |
| **运行状态** | `RT_THREAD_RUNNING` | 线程当前正在运行。在单核系统中，只有 `rt_thread_self()` 函数返回的线程处于运行状态；在多核系统中，可能就不止这一个线程处于运行状态。 |
| **挂起状态** | `RT_THREAD_SUSPEND` | 也称阻塞态。它可能因为资源不可用而挂起等待，或线程主动延时一段时间而挂起。在挂起状态下，线程不参与调度。 |
| **关闭状态** | `RT_THREAD_CLOSE` | 当线程运行结束时将处于关闭状态。关闭状态的线程不参与线程的调度。 |

### 3.2 线程状态转化

```
                    rt_thread_create()
                    rt_thread_init()
                          │
                          ▼
                    ┌──────────┐
                    │ 初始状态  │
                    ────┬─────┘
                         │ rt_thread_startup()
                         ▼
    ┌─────────────────────────────────────────────┐
    │ ① 导致挂起的 API：                           │
    │   rt_thread_delay()                         │
    │   rt_sem_take()                             │
    │   rt_mutex_take()                           │
    │   rt_mp_alloc()                             │
    │   rt_event_recv()                           │
    │   rt_mb_recv()                              │
    │   rt_mq_recv()                              │
    ─────────────────────────────────────────────┘
                         │
    ┌────────────┐  ①   ▼    ┌────────────┐
    │  就绪状态   │◄────────►│  运行状态   │
    └─────┬──────┘           └─────┬──────┘
          │ ▲                      │
          │ │  rt_thread_suspend() │
          │ │                      ▼
          │ │               ┌────────────┐
          │ │               │  挂起状态   │
          │ │               └─────┬──────┘
          │ │                     │
          │ │  ②                  │
          │ └─────────────────────┘
          │
    ┌─────────────────────────────────────────────┐
    │  导致恢复的 API：                           │
    │   rt_thread_resume()                        │
    │   rt_sem_release()                          │
    │   rt_mutex_release()                        │
    │   rt_mp_release()                           │
    │   rt_event_send()                           │
    │   rt_mb_send()                              │
    │   rt_mq_send()                              │
    └─────────────────────────────────────────────┘

    运行状态 ──rt_thread_exit()──▶ 关闭状态
    挂起状态 ──rt_thread_delete() / rt_thread_detach()──▶ 关闭状态
```

#### 状态转换与典型 API 对照

| 状态转换 | 触发 API |
|----------|----------|
| 初始态 → 就绪态 | `rt_thread_startup()` |
| 就绪态 → 运行态 | 调度器选中（最高优先级就绪线程） |
| 运行态 → 就绪态 | 被更高优先级线程抢占；`rt_thread_yield()` 主动让出；时间片耗尽 |
| 运行态 → 挂起态 | `rt_thread_delay()`、`rt_sem_take()`、`rt_mutex_take()`、`rt_mp_alloc()`、`rt_event_recv()`、`rt_mb_recv()`、`rt_mq_recv()` |
| 挂起态 → 就绪态 | `rt_thread_resume()`、`rt_sem_release()`、`rt_mutex_release()`、`rt_mp_release()`、`rt_event_send()`、`rt_mb_send()`、`rt_mq_send()` |
| 运行态 → 关闭态 | `rt_thread_exit()` |
| 挂起态 → 关闭态 | `rt_thread_delete()`、`rt_thread_detach()` |

### 3.3 调度机制剖析

#### 3.3.1 抢占式调度

**核心原则**：任何时刻，CPU 始终分配给 **优先级最高的就绪线程**。

**实现机制**：

1. **优先级位图**：RT-Thread 使用多级优先级（0 为最高，数值越小优先级越高），通过 **优先级位图表（priority bitmap）** 快速查找最高优先级就绪线程。
2. **抢占触发点**：
   - **中断返回时**：中断处理中若唤醒了更高优先级线程，中断返回的调度检查会立即切换到新的高优先级线程。
   - **内核 API 调用时**：如 `rt_sem_release()` 唤醒了等待该信号量的高优先级线程，立即触发调度。
   - **`rt_thread_yield()`**：主动放弃 CPU，触发调度。
3. **上下文切换**：通过 PendSV 中断（ARM Cortex-M）实现。调度器触发 PendSV，在 PendSV_Handler 中完成寄存器压栈/出栈，实现线程切换。

#### 3.3.2 时间片轮转

**适用条件**：仅对 **相同优先级** 的多个就绪线程生效。

**实现机制**：

1. **时间片分配**：每个线程创建时通过 `tick` 参数设定时间片大小（单位为 OS tick）。当系统中存在相同优先级线程时，这个参数指定线程一次调度能够运行的最大时间长度。
2. **Tick 中断处理**：
   ```
   SysTick_Handler()（每 1ms 触发一次）
   ├── rt_tick++  （全局 tick 计数器递增）
   ├── 遍历定时器链表，处理到期的软件定时器
   └── 检查当前运行线程的 remaining_tick：
         ├── remaining_tick--
         ── 若 remaining_tick == 0：
               ├── remaining_tick = init_tick（重置时间片）
               └── rt_schedule()  → 触发调度，选择下一个同优先级就绪线程
   ```
3. **优先级 vs 时间片的关系**：
   - **不同优先级**：严格抢占，高优先级线程随时打断低优先级线程，时间片不生效。
   - **相同优先级**：按时间片轮转，每个线程运行 `tick` 个 tick 后让出 CPU，调度器自动选择下一个就绪态的同优先级线程进行运行。

#### 调度算法总结

```
┌─────────────────────────────────────────────────────
│                 RT-Thread 调度策略                    │
├─────────────────────────────────────────────────────
│                                                     │
│  1. 抢占式优先级调度（主策略）                        │
│     └─ 最高优先级就绪线程始终获得 CPU                 │
│                                                     │
│  2. 同优先级时间片轮转（辅助策略）                    │
│     └─ 相同优先级的线程按时间片轮流执行               │
│                                                     │
│  3. 调度触发时机：                                   │
│     ├─ 中断返回（PendSV）                           │
│     ├─ 内核 API 调用后（如 sem_release）             │
│     ├─ 时间片耗尽（SysTick）                        │
│     └─ 主动让出（rt_thread_yield）                   │
│                                                     │
│  优先级范围：0（最高）~ 255（最低，由 RT_THREAD_PRIORITY_MAX 定义） │
│  默认 tick 频率：1000 Hz（1ms/tick）                 │
│                                                     │
└─────────────────────────────────────────────────────┘
```

### 3.4 线程初始化接口

线程的初始化接口参数如下：

| 参数 | 描述 |
|------|------|
| `thread` | 线程句柄。线程句柄由用户提供出来，并指向对应的线程控制块内存地址 |
| `name` | 线程的名称；线程名称的最大长度由 `rtconfig.h` 中定义的 `RT_NAME_MAX` 宏指定，多余部分会被自动截掉 |
| `entry` | 线程入口函数 |
| `parameter` | 线程入口函数参数 |
| `stack_start` | 线程栈起始地址 |
| `stack_size` | 线程栈大小，单位是字节。在大多数系统中需要做栈空间地址对齐（例如 ARM 体系结构中需要向 4 字节地址对齐） |
| `priority` | 线程的优先级。优先级范围根据系统配置情况（`rtconfig.h` 中的 `RT_THREAD_PRIORITY_MAX` 宏定义），如果支持的是 256 级优先级，那么范围是从 0 ~ 255，数值越小优先级越高，0 代表最高优先级 |
| `tick` | 线程的时间片大小。时间片（tick）的单位是操作系统的时钟节拍。当系统中存在相同优先级线程时，这个参数指定线程一次调度能够运行的最大时间长度。这个时间片运行结束时，调度器自动选择下一个就绪态的同优先级线程进行运行 |

**返回值**：

| 返回值 | 含义 |
|--------|------|
| `RT_EOK` | 线程创建成功 |
| `-RT_ERROR` | 线程创建失败 |

---

> **参考**：RT-Thread 官方文档 / STM32F407 工程  
> **文档编写日期**：2026-08-18
