# DAY2 RT-Thread 启动流程、内核基础与多线程编程

- 姓名：王童乐
- 组别：第2组
- 目标平台：STM32F407 星火一号（STM32F407ZG）
- RT-Thread：4.1.1
- 开发工具：RT-Thread Studio 2.3.1、GNU Arm Embedded Toolchain、ST-Link

## Part 1：理论知识梳理

### 1. STM32F407 启动流程与 RT-Thread 引导机制

STM32F407 上电或复位后，处理器先从向量表首项装载主栈指针 MSP，再从第二项取得 `Reset_Handler` 地址。`Reset_Handler` 调用 `SystemInit()` 完成芯片早期初始化，随后由 C 运行库复制 `.data`、清零 `.bss` 并执行运行库初始化。

本工程使用 GCC，RT-Thread 入口和用户程序的主要调用链为：

```text
上电/复位
  -> 装载 MSP 和 Reset_Handler
  -> Reset_Handler
  -> SystemInit + C 运行时初始化
  -> entry
  -> rtthread_startup
     -> rt_hw_board_init
     -> rt_system_timer_init
     -> rt_system_scheduler_init
     -> rt_application_init（创建 main 线程）
     -> rt_system_timer_thread_init
     -> rt_thread_idle_init
     -> rt_system_scheduler_start
  -> main_thread_entry
     -> rt_components_init
        -> thread_study_init（INIT_APP_EXPORT）
     -> 用户 main()
```

`rtthread_startup()` 先关闭中断，完成板级初始化、内核对象系统、定时器系统和调度器初始化，然后创建主线程、定时器线程和空闲线程，最后启动调度器。调度器运行主线程后，`main_thread_entry()` 先执行组件自动初始化，再进入用户 `main()`。

#### 自动初始化原理

`INIT_BOARD_EXPORT(fn)` 和 `INIT_APP_EXPORT(fn)` 最终展开为 `INIT_EXPORT(fn, level)`。该宏通过 `RT_SECTION` 和 `RT_USED` 将初始化函数指针放入 `.rti_fn.<level>` 链接段，链接器保留并按等级排列这些入口，运行时再遍历相应区间并依次调用。

- `INIT_BOARD_EXPORT(fn)` 使用 level 1，在板级初始化期间执行。
- `INIT_APP_EXPORT(fn)` 使用 level 6，在主线程的应用初始化阶段执行。

本实验使用：

```c
INIT_APP_EXPORT(thread_study_init);
```

因此测试模块由 RT-Thread 自动加载，不需要在 `main.c` 中硬编码调用，降低了模块与主程序之间的耦合。

### 2. 内核基础与对象模型

RT-Thread 将线程、信号量、互斥量、邮箱等统一抽象成内核对象。公共对象部分保存名称、类型、标志和对象链表节点；对象容器按类型管理已经创建的对象。线程控制块 `struct rt_thread` 在公共对象的基础上保存线程调度和上下文信息。

| 关键成员 | 作用 |
| --- | --- |
| `stat` | 线程当前状态及相关标志 |
| `current_priority` | 当前优先级，数值越小优先级越高 |
| `init_tick` | 创建线程时设置的初始时间片 |
| `remaining_tick` | 当前剩余时间片 |
| `sp` | 上下文切换时保存的线程栈指针 |
| `stack_addr` | 线程栈起始地址 |
| `stack_size` | 线程栈总大小 |
| `tlist` | 将线程挂入就绪队列或等待队列的节点 |
| `thread_timer` | 线程延时和超时等待使用的定时器 |

#### 静态线程与动态线程

| 对比项 | 静态线程 | 动态线程 |
| --- | --- | --- |
| 创建 API | `rt_thread_init()` | `rt_thread_create()` |
| TCB 和栈 | 应用在编译期定义 | 运行时从 Heap 分配 |
| 生命周期 | 由静态存储期和应用控制 | 可删除并回收内存 |
| 创建风险 | 内存需求在链接时确定 | 可能因堆不足或碎片化失败 |
| 适用场景 | 关键任务、固定资源、高可靠系统 | 数量或生命周期需要动态变化的任务 |

本实验中 `th_led1` 和 `th_monitor` 动态创建；`th_led2` 的 TCB 和 1024 字节栈使用全局静态对象。高可靠场景常使用静态线程，因为它不依赖运行时堆，内存占用可在链接阶段确定，也避免了堆碎片和不确定的分配时间。

### 3. 线程状态机与调度算法

RT-Thread 线程五态及典型转换如下：

```text
初始态 --rt_thread_startup()--> 就绪态
就绪态 --被调度器选中----------> 运行态
运行态 --时间片耗尽/被抢占-----> 就绪态
运行态 --delay/等待IPC/suspend--> 挂起态
挂起态 --超时/IPC到达/resume----> 就绪态
运行态 --入口返回/退出---------> 关闭态
```

- 初始态：线程已经创建，但尚未启动。
- 就绪态：具备运行条件，正在就绪队列等待 CPU。
- 运行态：当前正在占用 CPU。
- 挂起态：线程正在延时、等待 IPC 或被显式挂起。
- 关闭态：线程已经退出或被删除，等待清理或已脱离调度。

RT-Thread 使用基于优先级的抢占式调度，优先级数值越小越高。优先级 10 的 `th_monitor` 唤醒后可抢占优先级 16 的工作线程，先处理按键并采集状态。

多个同优先级线程同时就绪时使用时间片轮转。`th_led1` 与 `th_led2` 均为优先级 16、时间片 5 ticks。一个线程用完时间片后，同优先级的另一个就绪线程可以获得 CPU；本实验线程还会主动延时，因此实际运行中也会因延时阻塞而切换。

## Part 2：多线程实验

### 1. 实验设计

| 线程 | 创建方式 | 优先级 | 时间片 | 栈 | 功能 |
| --- | --- | ---: | ---: | ---: | --- |
| `th_led1` | 动态 | 16 | 5 ticks | 1024 B | 翻转 PF12 红灯，打印计数和 tick |
| `th_led2` | 静态 | 16 | 5 ticks | 1024 B | 翻转 PF11 绿灯，打印计数 |
| `th_monitor` | 动态 | 10 | 5 ticks | 1024 B | 每 2 秒打印状态与剩余栈，处理按键事件 |

星火一号官方 BSP 将 PF11 命名为 `LED_B`，但板载资源说明标注该灯实际显示为绿色，因此源码中使用语义更直观的 `LED_G_PIN`。板载 `KEY_DOWN` 使用 PC1，上拉输入和下降沿中断，并增加 200 ms 软件消抖。中断回调只记录事件，实际线程控制在监控线程中完成。

### 2. 挂起与恢复的兼容处理

RT-Thread 4.1.1 要求 `rt_thread_suspend()` 的目标是当前线程。如果监控线程直接挂起一个正在延时的 `th_led1`，可能触发内核断言。因此本实验使用安全的请求机制：

```text
第一次按键
  -> 中断置位 key_event
  -> th_monitor 设置 led1_pause_request
  -> th_led1 醒来后调用 rt_thread_suspend(rt_thread_self())
  -> th_led1 计数和红灯停止

第二次按键
  -> 中断置位 key_event
  -> th_monitor 调用 rt_thread_resume(th_led1_handle)
  -> th_led1 从原位置继续运行
```

该实现实际调用了 `rt_thread_suspend()` 和 `rt_thread_resume()`，同时遵守 4.1.1 的内核约束。

### 3. 状态与剩余栈统计

RT-Thread 在线程栈初始化时用字符 `'#'` 填充栈空间。Cortex-M 栈向低地址增长，从 `stack_addr` 开始统计连续的 `'#'` 字节数，可估算尚未使用的栈空间。监控线程读取线程的 `stat`、`stack_addr` 和 `stack_size` 并周期输出。

源码还导出了自定义 FinSH 命令：

```c
MSH_CMD_EXPORT(thread_dump, show study thread information);
```

在 `msh >` 中输入 `thread_dump`，可查看三个实验线程的状态、剩余栈以及 `th_led1` 的控制状态。

### 4. 编译与下载结果

RT-Thread Studio Debug 配置编译成功：

```text
Build Finished. 0 errors, 0 warnings.
Flash: 62736 B (61.27 KB)
RAM:    6424 B (6.27 KB)
```

使用 ST-Link 和 SWD 接口下载 `rtthread.elf` 到 `0x08000000`，下载进度 100%，程序复位后正常运行。调试串口为 UART1（PA9/PA10）、COM9、115200-8-N-1。

### 5. 运行结果

监控线程正常读取两个工作线程状态和剩余栈：

```text
[monitor] tick=8060, control=running
[monitor] th_led1: state=suspend, unused_stack=788/1024 bytes
[monitor] th_led2: state=suspend, unused_stack=788/1024 bytes
```

采样时两个线程正在 `rt_thread_mdelay()`，所以状态显示为 `suspend`。剩余栈约 788 字节，没有发现栈空间不足。

第一次按下 KEY_DOWN 后：

```text
[th_led1] count=10, tick=9079
[monitor] suspend th_led1 requested
[th_led1] suspended by key

[monitor] tick=12095, control=paused
[monitor] th_led1: state=suspend, unused_stack=788/1024 bytes
[th_led2] count=9
```

`th_led1` 计数停止，红灯停止翻转；`th_led2` 继续计数，绿灯继续运行。再次按下 KEY_DOWN：

```text
[monitor] resume th_led1 requested
[th_led1] resumed by key
[th_led1] count=11, tick=28245

[monitor] tick=30242, control=running
```

恢复后 `th_led1` 从 11 继续计数，说明线程上下文被保留，恢复过程没有重建线程。

## 实验总结

本实验完成了一个动态工作线程、一个静态工作线程和一个高优先级动态监控线程，验证了 RT-Thread 自动初始化、对象模型、优先级抢占、同优先级时间片参数、线程状态读取、栈余量估算、GPIO 中断和线程挂起/恢复。

最终结果为：工程编译无错误和警告；红绿 LED 独立翻转；监控线程周期输出正常；按键可稳定控制 `th_led1` 挂起与恢复；测试模块通过 `INIT_APP_EXPORT()` 自动加载，并通过 `MSH_CMD_EXPORT()` 导出 `thread_dump`。
