# 【DAY2】RT-Thread 启动流程、内核基础与多线程编程作业报告

* **学员姓名**：杜马辰
* **所在组别**：第 2 组
* **指导导师**：张丙儒
* **目标硬件**：STM32F407 星火号（Spark-1）
* **开发环境**：macOS (Apple Silicon) + GCC + SCons + OpenOCD
* **提交日期**：2026-08-23

---

## Part 1：理论知识梳理

### 一、 裸机开发的局限性与 RTOS 引入价值

1. **阻塞性问题**：
   * 在传统裸机前后台系统（`while(1)` 轮询 + 中断）中，后台任务若存在长时间延时或等待特定标志位（如等待传感器就绪、慢速通信），会导致 CPU 处于阻塞死等状态，使得 LCD 刷新、按键扫描等前台实时任务无法得到及时响应。
2. **裸机系统四大缺陷**：
   * **并发效率低**：无法实现多任务分时并发，CPU 资源利用率低。
   * **实时性差**：任务响应时间受其他任务阻塞时长的直接影响，无法保证确定性延时。
   * **代码可维护性差**：任务间耦合严重，“牵一发而动全身”，增加新功能极易破坏原有系统的时序。
   * **模块化与重用性低**：缺乏统一的驱动框架与抽象层，代码跨芯片平台迁移成本高。

---

### 二、 RT-Thread 启动流程与引导机制

#### 1. 工具链入口差异
在进入系统初始化之前，不同工具链在底层汇编和 C 库入口做了差异化处理：
* **Keil MDK (ARMCC/ARMCLANG)**：利用 `$Sub$$main()` 与 `$Super$$main()` 机制，在进入真正的 `main()` 之前插桩跳转到 `rtthread_startup()`。
* **IAR (ICCARM)**：通过重写 `Low Level Init`（底层初始化函数）截获启动入口。
* **GCC 工具链**：在链接脚本和汇编启动文件（`startup_stm32f407xx.s`）中，`Reset_Handler` 复位中断服务函数执行完 `SystemInit` 后，直接跳转到 `entry()` 函数进入 `rtthread_startup()`。

#### 2. 完整启动序列图
```text
上电复位 (Power On)
   │
   ▼
[汇编阶段] Reset_Handler (startup_stm32f407xx.s)
   ├──> SystemInit()：初始化系统时钟 (168MHz)、配置 Flash 预取与向量表
   └──> entry() / $Sub$$main()
         │
         ▼
[内核引导阶段] rtthread_startup() (src/components.c)
   ├──> rt_hw_interrupt_disable()        // 1. 关闭全局中断
   ├──> rt_hw_board_init()               // 2. 硬件板级底层初始化
   │      ├──> SystemCoreClockUpdate()
   │      ├──> _SysTick_Config()         // 初始化 OS Tick 定时器 (1ms/tick)
   │      ├──> rt_system_heap_init()     // 初始化系统动态堆内存
   │      ├──> rt_hw_pin_init() / usart  // 板载 GPIO、串口等基础驱动初始化
   │      └──> rt_components_board_init()// ★ 执行 INIT_BOARD_EXPORT 导出的板级初始化函数
   │
   ├──> rt_show_version()                // 3. 打印 RT-Thread 字符 Logo 与版本信息
   ├──> rt_system_timer_init()           // 4. 初始化系统定时器链表
   ├──> rt_system_scheduler_init()       // 5. 初始化调度器就绪队列
   ├──> rt_system_signal_init()          // 6. 初始化信号机制 (可选)
   │
   ├──> rt_application_init()            // 7. ★ 创建 "main" 线程 (入口为 main_thread_entry)
   │
   ├──> rt_system_timer_thread_init()    // 8. 创建软件定时器守护线程
   ├──> rt_thread_idle_init()            // 9. 创建空闲线程 (Idle Thread，优先级最低)
   │
   └──> rt_system_scheduler_start()      // 10. ★ 启动调度器 (接管 CPU，开启多任务调度)
         │
         ▼
[应用主线程] main_thread_entry()
   ├──> rt_components_init()             // ★ 按优先级执行 INIT_PREV/ENV/APP/PLAT/DEVICE_EXPORT
   └──> main()                           // 进入用户 main 函数
```

#### 3. 自动初始化机制原理
RT-Thread 采用编译器特定的**段属性（Section）**技术，避免在代码中显式调用初始化函数：
1. **段分布**：通过 `INIT_EXPORT(fn, level)` 宏，将函数指针存放在以 `.rti_fn.` 为前缀的独立数据段中（如 `.rti_fn.1`、`.rti_fn.6`）。
2. **链接器排序**：链接脚本根据 `level` 的标号将这些函数指针在 Flash 中按升序连续排列。
3. **两级分步执行**：
   * **`INIT_BOARD_EXPORT`（level 1）**：在调度器启动前执行，用于内存堆、串口等最底层板级硬件的就绪。
   * **`INIT_APP_EXPORT`（level 6）**：在 `main` 线程中被 `rt_components_init()` 统一执行，用于加载用户业务线程与组件。

---

### 三、 内核对象模型与线程生命周期

#### 1. 对象容器与控制块关键成员
RT-Thread 采用面向对象设计思想，所有内核资源（线程、互斥量、信号量、定时器等）均继承自 `struct rt_object`，并由内核全局的 `rt_object_container` 容器进行链表化统一管理。

`struct rt_thread`（线程控制块 TCB）关键成员作用：
* **`stat`**：记录当前线程状态（`RT_THREAD_INIT`、`RT_THREAD_READY`、`RT_THREAD_SUSPEND`、`RT_THREAD_RUNNING`、`RT_THREAD_CLOSE`）。
* **`current_priority`**：线程当前实际执行优先级（支持优先级继承机制，防止优先级反转）。
* **`init_tick`**：线程创建时分配的初始时间片计数值。
* **`remaining_tick`**：线程在当前轮次中剩余的时间片。
* **`sp`**：线程独立栈顶指针，用于上下文切换时准确恢复 CPU 核心寄存器（R0-R15、xPSR 等）的现场。

#### 2. 上下文切换机制
当触发调度时（如高优先级抢占或时间片耗尽）：
* **压栈（保存现场）**：触发 PendSV 异常，将当前线程正在使用的 CPU 寄存器压入该线程私有的栈空间中，并更新 `thread->sp`。
* **弹栈（恢复现场）**：调度器选出最高优先级的下一个就绪线程，将其栈中的寄存器值出栈恢复到 CPU 中，实现无缝的任务切换。

#### 3. 空闲线程（Idle）的核心职责
* **优先级最低**（通常为 31）：保证只要有任何工作线程就绪，Idle 线程就会立即被抢占。
* **资源回收（Garbage Collection）**：负责销毁处于 `CLOSE`（关闭态）的线程，释放其动态申请的 TCB 和栈内存。
* **低功耗与看门狗管理**：系统无任务执行时在此处进入 Sleep 睡眠模式或周期性喂狗。

#### 4. 静态创建 vs 动态创建对比
| 对比项 | 静态创建（`rt_thread_init`） | 动态创建（`rt_thread_create`） |
| :--- | :--- | :--- |
| **内存位置** | 编译期确定，分配在 `.bss` 或 `.data` 全局数据段 | 运行期由系统动态内存堆（Heap）中分配 |
| **空间占用确定性** | **100% 确定**，编译阶段即可精准知晓 RAM/Flash 消耗 | 依赖运行期堆状态，存在内存耗尽风险 |
| **内存碎片** | **0 内存碎片** | 频繁申请释放存在内存碎片风险 |
| **适用场景** | **高安全、高可靠关键任务**（工控、汽车电子、航天） | 通用应用开发、临时后台任务 |

---

### 四、 线程状态机与调度算法

#### 1. 线程五态转换图
```text
           [ 初始态 INIT ]
                 │
                 │ rt_thread_startup()
                 ▼
      ┌───> [ 就绪态 READY ] <───────────────────┐
      │          │                               │
      │          │ 调度器调度 (Schedule)          │
      │          ▼                               │
      │     [ 运行态 RUNNING ]                   │
      │          │                               │
      │          │ rt_thread_delay() /           │ 超时 / IPC 条件满足 /
      │          │ rt_thread_suspend() / 等待信号│ rt_thread_resume()
      │          ▼                               │
      │     [ 挂起态 SUSPEND ] ──────────────────┘
      │          │
      │ 自然结束 │ rt_thread_delete() / rt_thread_detach()
      └─────────>▼
            [ 关闭态 CLOSE ] (等待空闲线程回收资源)
```

#### 2. 调度机制剖析
* **抢占式调度（Preemptive）**：就绪列表中永远优先执行最高优先级（数值最小）的线程。当高优先级任务就绪时，立即剥夺当前低优先级任务的 CPU 执行权。
* **时间片轮转（Round-Robin）**：当多个就绪线程处于**同一优先级**时，每个线程轮流消耗自身分配的时间片（`init_tick`）。当时间片递减为 0 时，调度器将其排入同优先级队列尾部，让出 CPU 给下一个同优先级线程。

---

### 五、 SCons 构建系统核心机制

1. **核心文件结构**：
   * **`SConstruct`**：顶层构建入口脚本。
   * **`SConscript`**：源码子目录构建规则脚本，负责管理当前目录的文件包含与编译选项。
   * **`rtconfig.py`**：定义工具链类型（`gcc`/`armcc`）、编译选项（`CFLAGS`）、链接参数（`LFLAGS`）与芯片架构。
2. **常用构建命令**：
   * `scons -jN`：多线程并行编译。
   * `scons -c`：清除构建产生的中间文件和目标固件。
   * `scons --target=vsc` / `mdk5`：导出生成指定 IDE 的工程文件。
   * `scons --dist`：将当前 BSP 及其依赖的最小化内核/组件打包成独立的发布包。
3. **条件编译控制**：
   * 通过 `Kconfig` 配置宏定义生成 `rtconfig.h`。
   * 在 `SConscript` 中通过 `CPPPATH`、`SRC` 及 `DEPENDS` 实现依据宏开关进行代码的动态裁剪与编译。

---

## Part 2：多线程实战编程与实验结果

### 一、 实验设计与代码实现说明
本实验在 `thread_study.c` 中实现了以下功能：
1. **工作线程 1 (`th_led1`)**：动态创建（`rt_thread_create`），优先级 16，时间片 5 ticks，栈 1024 字节。周期性翻转板载红灯（LED_R），并通过串口打印当前执行计数与系统 `rt_tick_get()`。
2. **工作线程 2 (`th_led2`)**：静态初始化（`rt_thread_init`），优先级 16，时间片 5 ticks，栈 1024 字节。周期性翻转板载蓝灯（LED_B），并打印计数。
3. **监控线程 (`th_mon`)**：动态创建，优先级 10（高优先级），周期延时 2000ms。周期性唤醒并打印 `th_led1` 和 `th_led2` 的运行状态与剩余栈空间大小（通过扫描栈底 `#` 水印字节计算得出）。
4. **按键外部中断控制**：配置板载 KEY0 下降沿触发中断。按下按键时交替调用 `rt_thread_suspend(th_led1)` 与 `rt_thread_resume(th_led1)`，实现对红灯工作线程的暂停与恢复。
5. **系统集成与 MSH 调试命令**：使用 `INIT_APP_EXPORT()` 实现模块开机自启；使用 `MSH_CMD_EXPORT(thread_dump, ...)` 导出调试命令。

---

### 二、 实板运行日志（FinSH 控制台输出截图记录）

```text
 \ | /
- RT -     Thread Operating System
 / | \     5.0.2 build Aug 23 2026 22:00:00
 2006 - 2022 Copyright by RT-Thread team
[I/thread_study] DAY2 Thread Study module initialized successfully via INIT_APP_EXPORT.
msh >[I/thread_study] [th_led1] count: 1, tick: 512
[I/thread_study] [th_led2] count: 1
[I/thread_study] [th_led1] count: 2, tick: 1012
[I/thread_study] [th_led2] count: 2

============== [Thread Monitor Dump] ==============
Name: th_led1  | Stat: SUSPEND | Priority: 16 | Stack Free:  824 / 1024 Bytes
Name: th_led2  | Stat: SUSPEND | Priority: 16 | Stack Free:  848 / 1024 Bytes
===================================================

[I/thread_study] [th_led1] count: 3, tick: 1512
[I/thread_study] [th_led2] count: 3

/* 按下 KEY0 按键测试挂起 */
[W/thread_study] >> KEY0 Pressed: [th_led1] has been SUSPENDED! <<
[I/thread_study] [th_led2] count: 4
[I/thread_study] [th_led2] count: 5

============== [Thread Monitor Dump] ==============
Name: th_led1  | Stat: SUSPEND | Priority: 16 | Stack Free:  824 / 1024 Bytes
Name: th_led2  | Stat: SUSPEND | Priority: 16 | Stack Free:  848 / 1024 Bytes
===================================================

/* 再次按下 KEY0 按键测试恢复 */
[W/thread_study] >> KEY0 Pressed: [th_led1] has been RESUMED! <<
[I/thread_study] [th_led1] count: 4, tick: 3612
[I/thread_study] [th_led2] count: 6

/* MSH 调试命令测试 */
msh >thread_dump

----- Manual Thread Dump -----
[th_led1] Stat: SUSPEND, Remaining Tick: 5, Stack Free: 824 B
[th_led2] Stat: SUSPEND, Remaining Tick: 5, Stack Free: 848 B
------------------------------
```
