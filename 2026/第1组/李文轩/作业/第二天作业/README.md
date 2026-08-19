# 【DAY2】RT-Thread 启动流程、内核基础与多线程编程

- **目标平台**：STM32F407 星火号（stm32f407-rtspark）
- **工程**：RT-Thread Studio 工程 `02-thread_sample`（STM32F407ZG）
- **调试方式**：ST-Link + 断点调试，观察实际调用链验证理论

---

# Part 1 理论知识梳理

## 一、STM32F407 启动流程与 RT-Thread 引导机制

### 1.1 启动路径：从上电复位 `Reset_Handler` 到 `main()` 的完整调用链

经 ST-Link 断点调试验证，实际调用链如下：

```
上电复位
  → Reset_Handler                    （startup_stm32f407xx.S）
      → SystemInit()                 （system_stm32f4xx.c，配置时钟树）
      → 初始化 .data 段、清零 .bss 段
      → 进入 C 环境
  → $Sub$$main()                     （components.c，MDK 下拦截 main 调用）
      → rtthread_startup()           （RT-Thread 系统初始化入口）
          → rt_hw_interrupt_disable()        关中断，初始化期间不允许被抢占
          → rt_hw_board_init()               板级硬件初始化（时钟、串口、内存）
          → rt_components_board_init()       板级组件自动初始化
          → rt_show_version()                打印版本信息
          → rt_system_timer_init()           系统定时器初始化
          → rt_system_scheduler_init()       调度器初始化
          → rt_system_signal_init()          信号模块初始化
          → rt_application_init()            创建 main 线程（main_thread_entry）
          → rt_system_timer_thread_init()    创建定时器线程（rt_thread_timer_entry）
          → rt_thread_idle_init()            创建空闲线程（rt_thread_idle_entry）
          → rt_system_scheduler_start()      启动调度器
  → main_thread_entry()              （调度器调度运行 main 线程）
      → main()                       （用户应用程序入口）
```

关键点说明：

1. **启动文件分编译器有不同入口**：MDK 下是 `$Sub$$main()`（先于 main 执行）、IAR 是 `_low_level_init()`、GCC 是 `entry()`，最终都汇聚到 `rtthread_startup()`。
2. **`rtthread_startup()` 是整个 RT-Thread 初始化的总导演**，负责完成硬件初始化、系统三大模块（定时器/调度器/信号）初始化，并创建三个系统线程：main 线程、定时器线程、空闲线程。
3. **调度器启动后**，main 线程按优先级被调度运行，其入口函数 `main_thread_entry()` 最终调用用户的 `main()`。
4. 调试时通过 Call Stack 窗口可以观察到：停在 `rtthread_startup()` 时，栈帧为 `rtthread_startup ← $Sub$$main ← Reset_Handler`；停在 `main()` 时，栈帧为 `main ← main_thread_entry`，两部分拼接即为完整调用链。

### 1.2 自动初始化原理：`INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT` 的底层实现

**设计思想**：用户不需要在 `main()` 里手动逐个调用初始化函数，而是通过宏把初始化函数"注册"到指定内存段，系统在启动时自动按顺序调用。

**宏定义**（rt-thread/include/rtdef.h）：

```c
#define INIT_EXPORT(fn, level)                                               \
        RT_USED const init_fn_t __rt_init_##fn RT_SECTION(".rti_fn." level) = fn

/* board init routines will be called in board_init() function */
#define INIT_BOARD_EXPORT(fn)      INIT_EXPORT(fn, "1")
#define INIT_PREV_EXPORT(fn)       INIT_EXPORT(fn, "2")
#define INIT_DEVICE_EXPORT(fn)     INIT_EXPORT(fn, "3")
#define INIT_COMPONENT_EXPORT(fn)  INIT_EXPORT(fn, "4")
#define INIT_ENV_EXPORT(fn)        INIT_EXPORT(fn, "5")
#define INIT_APP_EXPORT(fn)        INIT_EXPORT(fn, "6")
```

**底层实现原理**分三步：

1. **注册（编译期）**：`INIT_BOARD_EXPORT(fn)` 展开后，把函数指针 `fn` 放进名为 `.rti_fn.1` 的段；`INIT_APP_EXPORT(fn)` 放进 `.rti_fn.6`。数字（1~6）表示初始化优先级，数字越小越先执行。这里用的是**段（section）**机制，不是直接调用。

2. **链接（链接期）**：链接脚本（link.lds）把各目标文件的 `.rti_fn*` 段按名字排序后连续排列：

```lds
__rt_init_start = .;
KEEP(*(SORT(.rti_fn*)))     /* 按段名排序，保证 1→6 顺序 */
__rt_init_end = .;
```

   段内还有哨兵函数（`rti_start` 段 "0"、`rti_board_start` 段 "0.end"、`rti_end` 段 "6.end"），用于标记遍历的起止边界。

3. **调用（运行期）**：`rt_components_init()`（components.c）遍历这两个符号之间的函数指针，逐个执行：

```c
void rt_components_init(void)
{
    volatile const init_fn_t *fn_ptr;
    for (fn_ptr = &__rt_init_rti_board_start; fn_ptr < &__rt_init_rti_board_end; fn_ptr++)
    {
        (*fn_ptr)();        /* 依次调用所有注册的初始化函数 */
    }
}
```

**执行时机**：
- `INIT_BOARD_EXPORT` 注册的函数在 `rt_components_board_init()`（rtthread_startup 中较早调用）里执行，适合初始化板级外设。
- `INIT_APP_EXPORT` 注册的函数在 `rt_components_init()`（系统初始化完成后）里执行，适合应用层初始化。
- 整个机制让"组件/驱动/应用"各自注册、系统统一调度，模块之间解耦，新增模块无需改动 main()。

## 二、内核基础与对象模型

### 2.1 对象容器与控制块

**面向对象设计思想**：RT-Thread 用 C 语言模拟面向对象——所有内核对象（线程、信号量、互斥量、事件、消息队列、定时器、设备等）都继承自基类 `struct rt_object`（含 name、type、flag、list 四个成员），统一挂接在内核对象容器（链表）中管理，并通过"类型-名称"进行查找与统一操作。

**线程控制块 `struct rt_thread` 关键成员**：

| 成员 | 作用 |
|---|---|
| `parent` | 继承的 `rt_object` 基类，实现对象模型 |
| `stat` | 线程状态（初始/就绪/运行/挂起/关闭），调度器据此决策 |
| `current_priority` | 当前优先级（可能被临时改变，如优先级继承） |
| `init_priority` | 初始优先级（线程创建时设定的原始优先级） |
| `init_tick` / `remaining_tick` | 时间片初始值 / 剩余值，时间片轮转调度依据 |
| `sp` | 栈指针，保存线程上下文，线程切换的核心 |
| `entry` / `parameter` | 线程入口函数及其参数 |
| `stack_addr` / `stack_size` | 线程栈地址与大小 |
| `thread_timer` | 内置线程定时器（用于延时/超时） |
| `cleanup` | 线程退出时的清理函数 |

线程控制块定义形成线程内核对象后，链接到对象容器链表中统一管理，这是 RT-Thread 对象模型的核心体现。

### 2.2 内存分配方式：静态对象与动态对象对比

| 对比项 | 静态对象（`rt_thread_init`） | 动态对象（`rt_thread_create`） |
|---|---|---|
| 分配位置 | 编译期确定为全局变量（TCB + 栈） | 运行期从系统堆（heap）分配 |
| 分配方式 | 用户提供 TCB 指针和栈首地址 | 内核自动分配句柄 + 栈空间 |
| 生命周期 | 与程序共存亡 | 可动态创建、删除 |
| 释放机制 | 无需释放 | `rt_thread_delete()` 释放并回收 |
| 失败风险 | 无分配失败（编译期已确定） | 堆不足时可能创建失败 |
| 内存管理 | 无碎片问题 | 频繁创建/删除可能产生碎片 |

**为什么高可靠场景强制使用静态线程**：
1. **确定性**：静态线程的内存（TCB、栈）在编译期就确定并分配好，运行期不存在"malloc 失败"的可能，行为完全可预期。
2. **无碎片**：不会因频繁动态分配/释放产生内存碎片，长时间运行更稳定。
3. **可审计**：静态对象的资源占用一目了然，便于做资源预算与安全认证（航天、工业、医疗等场景要求）。
4. 动态线程适合资源灵活、追求开发效率的普通应用场景，但在资源紧张或对实时性、可靠性要求苛刻的系统里，静态线程是更稳妥的选择。

## 三、线程状态机与调度算法

### 3.1 五态模型

RT-Thread 线程有五种状态，状态转换关系如下：

```
                rt_thread_create()/rt_thread_init()
                            │
                            ▼
  初始状态（RT_THREAD_INIT）──────────────┐  不参与调度
                            │ rt_thread_startup()
                            ▼
  就绪状态（RT_THREAD_READY）───调度器选择──▶ 运行状态（RT_THREAD_RUNNING）
       ▲                                    │
       │ rt_thread_resume() 等              │ rt_thread_delay()/rt_sem_take() 等
       └────────────────────────────────────▼
                                     挂起状态（RT_THREAD_SUSPEND）  不参与调度
                            │ rt_thread_exit()/delete()/detach()
                            ▼
  关闭状态（RT_THREAD_CLOSE）  不参与调度
```

各状态说明：

| 状态 | 宏定义 | 说明 |
|---|---|---|
| 初始状态 | `RT_THREAD_INIT` | 线程刚创建还未开始运行，不参与调度 |
| 就绪状态 | `RT_THREAD_READY` | 按优先级排队等待执行，调度器选择后进入运行 |
| 运行状态 | `RT_THREAD_RUNNING` | 当前正在运行；单核系统中仅 `rt_thread_self()` 返回的线程处于此态 |
| 挂起状态 | `RT_THREAD_SUSPEND` | 阻塞态，因资源不可用或主动延时而挂起，不参与调度 |
| 关闭状态 | `RT_THREAD_CLOSE` | 线程运行结束，不参与调度 |

**典型状态转换 API**：
- 创建/初始化：`rt_thread_create()` / `rt_thread_init()`
- 启动：`rt_thread_startup()`（初始 → 就绪）
- 挂起：`rt_thread_delay()` / `rt_sem_take()` / `rt_mutex_take()` / `rt_event_recv()` / `rt_mb_recv()` / `rt_mq_recv()`（运行 → 挂起）
- 恢复：`rt_thread_resume()` / `rt_sem_release()` / `rt_mutex_release()` / `rt_event_send()` / `rt_mb_send()` / `rt_mq_send()`（挂起 → 就绪）
- 结束：`rt_thread_exit()` / `rt_thread_delete()` / `rt_thread_detach()`（→ 关闭）

### 3.2 调度机制剖析

**抢占式调度（Preemptive Scheduling）**：
- 每个线程有固定优先级（0~255，数值越小优先级越高）。
- 当有比当前运行线程优先级更高的线程进入就绪态时，调度器**立即打断**当前线程，切换到高优先级线程运行（通过 PendSV 中断 + 上下文切换实现）。
- 体现"高优先级线程优先"的实时性要求，是 RT-Thread 默认的调度方式。

**时间片轮转（Round-Robin）**：
- 针对**优先级相同**的就绪态线程：每个线程配置时间片（单位 OS Tick），调度器让它们轮流运行。
- 线程运行完自己的时间片后，调度器自动切换到下一个同优先级就绪线程；时间片用尽会被挂到队列末尾。
- 例：线程 A 时间片 10 tick、线程 B 时间片 5 tick，则 A 每次运行 10 个节拍、B 运行 5 个节拍，交替执行。
- 若就绪队列中只有一个同优先级线程（或该优先级无竞争者），时间片不影响其连续运行。

**两种机制协同**：抢占式解决"不同优先级之间谁先跑"，时间片轮转解决"同优先级之间怎么轮流"，两者共同构成 RT-Thread 的调度核心。

---

# Part 2 多线程实战编程

## 一、程序结构设计

按作业要求，在工程 `applications` 目录下新建 `thread_study.c`，包含三个线程 + 自动初始化 + MSH 命令：

| 线程 | 创建方式 | 优先级 | 时间片 | 栈大小 | 功能 |
|---|---|---|---|---|---|
| `th_led1` | 动态（`rt_thread_create`） | 16 | 5 ticks | 1024 | 翻转 LED_R(PF12)，打印计数 + tick |
| `th_led2` | 静态（`rt_thread_init`） | 16 | 5 ticks | 1024 | 翻转 LED_B(PF11)，打印计数 |
| `th_monitor` | 动态 | 10 | 5 ticks | 1024 | 打印线程状态/剩余栈；KEY0 挂起/恢复 th_led1 |

- **自动加载**：`INIT_APP_EXPORT(thread_sample_init)`，无需在 main.c 中调用。
- **MSH 命令**：`MSH_CMD_EXPORT_ALIAS(cmd_thread_dump, thread_dump, ...)` 导出线程查看命令。

## 二、编译与烧录

```
scons -j4          // 编译，零错误
```

通过 ST-Link 烧录 `rt-thread.elf`（STM32_Programmer_CLI）：

```
File download complete
Time elapsed during download operation: 00:00:01.681
Application is running, Please Hold on...
Start operation achieved successfully
```

## 三、运行结果分析（串口日志）

### 3.1 线程运行日志（COM21, 115200）

```
[D/main] Hello RT-Thread! count=776
[monitor] th_led1 stat:2 remain_stack:852
[monitor] th_led2 stat:2 remain_stack:852
th_led2 count: 1548
th_led1 count: 1545, tick: 777206
th_led2 count: 1549
th_led1 count: 1546, tick: 777709
[D/main] Hello RT-Thread! count=777
th_led2 count: 1550
th_led1 count: 1547, tick: 778212
th_led2 count: 1551
th_led1 count: 1548, tick: 778715
[D/main] Hello RT-Thread! count=778
[monitor] th_led1 stat:2 remain_stack:852
[monitor] th_led2 stat:2 remain_stack:852
th_led2 count: 1552
th_led1 count: 1549, tick: 779218
th_led2 count: 1553
th_led1 count: 1550, tick: 779721
```

**分析**：
- `th_led1` 与 `th_led2` 同为优先级 16，日志中两者交替打印计数（1545→1548→1549...），且各自 `rt_thread_mdelay(500)` 主动让出 CPU，体现了**同优先级线程时间片/主动让出的交替调度**。
- `th_led1` 打印 `tick`（如 777206），间隔约 500 tick（500ms），符合 `mdelay(500)` 设置。
- `[monitor]` 每 2 秒打印一次（周期 2000ms），显示两个工作线程 `stat:2`（就绪态）与剩余栈 852 字节。

### 3.2 自定义 MSH 命令 `thread_dump`

```
msh >thread_dump
name         status   prio   remain_stack
----------------------------------------
th_led1     2       16     852
th_led2     2       16     852
th_monitor  2       10     836
msh >
```

**分析**：命令通过 `rt_thread_find()` 按名称查找三个线程，打印其状态（2=就绪）、优先级、剩余栈空间。与 `INIT_APP_EXPORT` 自动加载的机制配合，验证了作业第 4 点要求。

### 3.3 系统线程列表 `list_thread`

```
msh >list_thread
thread   pri  status      sp     stack size max used left tick  error
-------- ---  ------- ---------- ----------  ------  ---------- ---
th_monit  10  suspend 0x000000bc 0x00000400    25%   0x00000003 OK
th_led2   16  suspend 0x000000ac 0x00000400    22%   0x00000003 OK
th_led1   16  suspend 0x000000ac 0x00000400    23%   0x00000003 OK
tshell    20  running 0x000000cc 0x00001000    15%   0x00000002 OK
tidle0    31  ready   0x00000060 0x00000100    56%   0x0000001f OK
timer      4  suspend 0x00000080 0x00000200    25%   0x00000009 OK
main      10  suspend 0x000000b8 0x00000800    14%   0x0000000d OK
msh >
```

**分析**：
- 三个作业线程（th_led1/th_led2/th_monitor）全部创建成功并被调度器管理。
- 优先级关系正确：th_monitor(10) > th_led1/th_led2(16) > tshell(20) > tidle0(31 最低，空闲线程)。
- 线程栈使用率约 22%~25%，栈分配 1024 字节（0x400）充足。
- main 线程（优先级 10）与定时器线程（timer，优先级 4）为系统自带。

## 四、实验结果总结

1. **多线程并发**：三个用户线程 + main 线程 + 系统线程同时运行，通过串口日志清晰可见各自独立的执行流。
2. **动态 vs 静态线程**：th_led1 用 `rt_thread_create`（堆分配），th_led2 用 `rt_thread_init`（用户提供全局 TCB 和栈），两者运行效果一致，验证了作业 Part 1 2.2 节的理论。
3. **抢占式 + 时间片**：th_monitor 优先级 10 能抢占优先级 16 的工作线程（监控打印插入到工作线程日志之间）；th_led1/th_led2 同优先级交替运行。
4. **自动初始化**：三个线程通过 `INIT_APP_EXPORT` 自动创建，main.c 无任何硬编码调用。
5. **MSH 扩展**：`thread_dump` 命令可随时查看线程运行状态，方便调试。
