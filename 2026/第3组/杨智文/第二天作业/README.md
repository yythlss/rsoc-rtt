# 【DAY2】RT-Thread 启动流程、内核基础与多线程编程

- 姓名：杨智文
- 组别：第3组
- 目标平台：STM32F407 星火号（`stm32f407-rtspark`）
- 工程路径：`D:\RT-ThreadStudio\workspace\test`

## Part 1：理论知识梳理

### 1. STM32F407 启动流程与 RT-Thread 引导机制

本工程使用 GCC 工具链，完整启动路径如下：

```text
上电/复位
  -> CPU 从中断向量表取得初始 MSP 和 Reset_Handler 地址
  -> Reset_Handler
     -> 设置主栈指针
     -> 将 .data 从 Flash 复制到 SRAM
     -> 清零 .bss
     -> SystemInit()：配置时钟等底层硬件
     -> __libc_init_array()：执行 C/C++ 运行库初始化
     -> entry()
  -> rtthread_startup()
     -> 关闭中断
     -> rt_hw_board_init()：板级、时钟、堆和 INIT_BOARD_EXPORT 项初始化
     -> 初始化系统定时器与调度器
     -> rt_application_init()：创建 main 线程
     -> 创建 timer 线程和 idle 线程
     -> rt_system_scheduler_start()：启动调度
  -> main_thread_entry()
     -> rt_components_init()：按顺序执行自动初始化函数
     -> main()
```

`INIT_BOARD_EXPORT(fn)` 和 `INIT_APP_EXPORT(fn)` 最终都会展开为 `INIT_EXPORT(fn, level)`。在 GCC 下，宏把函数指针分别放入 `.rti_fn.1` 和 `.rti_fn.6` 段，并使用 `RT_USED` 防止编译器删除。链接脚本通过 `KEEP(*(SORT(.rti_fn*)))` 保留并按等级排序这些段。系统启动时遍历相应区间的函数指针：板级初始化阶段执行等级 1，main 线程中的组件初始化阶段执行等级 2～6。因此本作业只需写 `INIT_APP_EXPORT(thread_study_init)`，无需在 `main.c` 中手动调用。

### 2. 内核基础与对象模型

RT-Thread 用统一的内核对象管理机制组织线程、信号量、互斥量等资源。公共对象头保存对象类型、名称和对象链表节点，具体对象再扩展自己的控制信息。线程对应的控制块是 `struct rt_thread`，其中关键成员如下：

| 成员 | 作用 |
| --- | --- |
| `stat` | 保存线程当前状态，读取基本状态时应配合 `RT_THREAD_STAT_MASK` |
| `current_priority` | 线程当前有效优先级，数值越小优先级越高 |
| `init_tick` | 线程初始时间片，时间片耗尽后用于重新装载 |
| `remaining_tick` | 当前还剩多少时间片 |
| `sp` | 当前线程栈指针，线程切换时用于保存和恢复现场 |
| `stack_addr` / `stack_size` | 栈起始地址和总大小，可用于监控栈使用量 |

静态对象和动态对象的区别：

| 对比项 | 静态线程 `rt_thread_init` | 动态线程 `rt_thread_create` |
| --- | --- | --- |
| TCB/栈位置 | 编译期定义的全局或静态内存 | 运行时从系统堆申请 |
| 生命周期 | 由程序静态存储期决定 | 创建后存在，删除时归还堆 |
| 释放接口 | `rt_thread_detach` | `rt_thread_delete` |
| 优点 | 内存确定、无堆碎片、失败模式少 | 使用灵活、可按需创建和删除 |
| 适用场景 | 安全关键、长期运行、资源必须可预测 | 功能动态变化、资源按需使用 |

高可靠系统常强制使用静态线程，因为其内存占用可在编译阶段确定，不依赖运行时堆状态，能够避免堆耗尽、碎片化和动态分配不确定性。

### 3. 线程状态机与调度算法

RT-Thread 线程包含五种基本状态：

```text
                  rt_thread_startup()/rt_thread_resume()
        INIT ----------------------------------------------> READY
                                                               |
                                              调度器选中         | 时间片耗尽/被抢占
                                                               v
        CLOSE <------- 线程退出/删除 ------- RUNNING <------ READY
                                              |
                    delay/suspend/等待 IPC    |
                                              v
                                           SUSPEND
                                              |
                    超时/唤醒/resume ---------+
```

- 初始态 `INIT`：线程已创建或初始化，但还没有启动。
- 就绪态 `READY`：具备运行条件，位于就绪队列中等待调度。
- 运行态 `RUNNING`：当前占用 CPU。
- 挂起态 `SUSPEND`：主动挂起、延时或等待 IPC；可由超时、唤醒或 `rt_thread_resume()` 回到就绪态。
- 关闭态 `CLOSE`：线程运行结束或被删除，等待资源回收。

抢占式调度：当更高优先级线程就绪时，调度器立即让它抢占低优先级线程。本实验中 `th_monitor` 优先级为 10，高于两个 LED 线程的 16，因此监控线程被唤醒后优先运行。

时间片轮转：同一优先级存在多个就绪线程时，每个线程最多运行自己的时间片；时间片用完后移到同优先级队列末尾。本实验的 `th_led1` 和 `th_led2` 同为优先级 16、时间片 5 ticks，可以公平获得 CPU。

## Part 2：多线程实验

### 1. 实现方案

| 线程 | 创建方式 | 优先级 | 时间片 | 栈 | 功能 |
| --- | --- | ---: | ---: | ---: | --- |
| `th_led1` | `rt_thread_create` 动态创建 | 16 | 5 ticks | 1024 B | 翻转 PF12 红灯，打印计数和系统 tick |
| `th_led2` | `rt_thread_init` 静态初始化 | 16 | 5 ticks | 1024 B | 翻转 PF11 蓝灯，打印计数和系统 tick |
| `th_monitor` | `rt_thread_create` 动态创建 | 10 | 5 ticks | 1024 B | 每 2 秒打印 LED 线程状态和剩余栈，处理按键控制 |

板载 RGB LED 为低电平点亮。KEY_UP 使用 PC5、上拉输入和下降沿中断。中断函数只记录按键事件，实际的 `rt_thread_suspend()` / `rt_thread_resume()` 在线程上下文中执行。因为调用 `rt_thread_mdelay()` 的线程也表现为挂起态，程序在主动暂停前会等待 `th_led1` 的延时结束，避免把延时状态与人工暂停混淆。

### 2. 自动初始化与调试命令

- 使用 `INIT_APP_EXPORT(thread_study_init)` 自动创建并启动三个线程。
- `main.c` 不直接调用作业初始化函数。
- 使用 `MSH_CMD_EXPORT(thread_dump, ...)` 导出 `thread_dump` 命令。
- 剩余栈统计利用 RT-Thread 创建线程时用 `'#'` 填充栈的特性，计算尚未被覆盖的连续字节数。

### 3. 编译验证

已使用工程配置的 ARM GNU 编译器和 STM32F407 编译参数对 `thread_study.c` 执行编译检查，结果通过。编译器仅报告 RT-Thread 自带 `MSH_CMD_EXPORT` 宏的函数指针转换提示，作业源码无语法错误。

### 4. 烧录后的预期现象与日志格式

1. 红灯和蓝灯约每 500 ms 翻转一次。
2. 串口持续输出两个工作线程的计数和系统 tick。
3. 每 2 秒打印两个 LED 线程的状态与剩余栈。
4. 按一次 KEY_UP 暂停红灯线程，再按一次恢复。
5. 在 FinSH 中输入 `thread_dump` 可查看三个线程。

```text
[thread_study] Day2 threads started; press KEY_UP to pause/resume th_led1.
[th_led1] count=1 tick=... LED_R=ON
[th_led2] count=1 tick=... LED_B=ON

[monitor] thread state and remaining stack
th_led1    SUSPEND  pri=16 tick=05 stack_free=.../1024
th_led2    SUSPEND  pri=16 tick=05 stack_free=.../1024

msh >thread_dump
name       state    priority/tick/remaining stack
th_led1    ...
th_led2    ...
th_monitor ...
```

> 说明：以上为程序定义的日志格式。真实 tick、线程瞬时状态和剩余栈数值以开发板烧录后的串口输出为准；硬件运行照片应在烧录测试后补充到 PR。

## Part 3：源码

源码见同目录下的 [`thread_study.c`](./thread_study.c)。
