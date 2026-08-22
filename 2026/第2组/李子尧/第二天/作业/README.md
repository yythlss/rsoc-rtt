# RSOC 2026 Day2：RT-Thread 启动流程、内核基础与多线程实验

## 1. 实验环境与任务

| 项目 | 配置 |
| --- | --- |
| MCU | STM32F407ZG |
| RTOS | RT-Thread 4.1.1 |
| 开发环境 | RT-Thread Studio |
| 编译器 | ARM GCC |
| 调试接口 | ST-Link / SWD |
| Console | USART1 / COM7，115200 8-N-1 |
| 板卡 | RT-Thread 星火 1 号 |
| LED_R | PF12，低电平有效 |
| LED_B | PF11，低电平有效 |
| KEY0 | PC0，低电平有效 |

实验完成 RT-Thread 启动流程与自动初始化机制分析，并实现动态线程、静态线程、高优先级监控线程、线程状态与栈空间监测、按键挂起恢复以及 MSH 自定义命令。应用模块通过 `INIT_APP_EXPORT(thread_study_init)` 自动初始化，`applications/main.c` 无需增加线程初始化调用。

## 2. RT-Thread 系统启动流程

以下流程基于 RT-Thread 4.1.1 源码、GNU GCC 构建配置和 STM32F407 链接脚本。

```text
Reset_Handler
      ↓
C Runtime
      ↓
entry()
      ↓
rtthread_startup()
      ↓
rt_hw_board_init()
      ↓
rt_components_board_init()
      ↓
rt_system_timer_init()
      ↓
rt_system_scheduler_init()
      ↓
rt_application_init()
      ↓
rt_thread_idle_init()
      ↓
rt_system_scheduler_start()
      ↓
main_thread_entry()
      ↓
rt_components_init()
      ↓
main()
```

链接脚本以 `ENTRY(Reset_Handler)` 指定复位入口。GNU GCC 配置下，`entry()` 调用 `rtthread_startup()`；该函数依次完成板级初始化、定时器系统初始化、调度器初始化、应用主线程创建和 idle 线程初始化，随后启动调度器。

`rt_application_init()` 创建 Main Thread。调度器启动后，`main_thread_entry()` 先执行 `rt_components_init()`，再进入应用 `main()`。因此，应用 `main()` 运行于线程调度环境中，而非 MCU 复位后的首个 C 函数。

## 3. 自动初始化机制

### 3.1 `INIT_EXPORT`

初始化导出宏将函数指针放入指定的初始化段。GNU GCC 配置下，函数指针位于 `.rti_fn.<level>` 段，链接脚本通过 `KEEP(*(SORT(.rti_fn*)))` 保留并排序这些段。

### 3.2 `INIT_BOARD_EXPORT`

`INIT_BOARD_EXPORT()` 对应板级初始化层级。 `rt_components_board_init()` 遍历 `rti_board_start` 与 `rti_board_end` 对应的函数指针区间，用于在内核调度器启动前完成板级组件初始化。

### 3.3 `INIT_APP_EXPORT`

`INIT_APP_EXPORT()` 对应应用初始化层级。 `rt_components_init()` 在 Main Thread 中遍历组件初始化区间并调用已导出的函数。初始化顺序由段名及链接脚本排序共同确定。

### 3.4 应用初始化

```c
INIT_APP_EXPORT(thread_study_init);
```

`thread_study_init()` 注册至应用初始化段。链接阶段对应 section 被集中保留，`rt_components_init()` 在 Main Thread 中遍历并调用该函数，因此无需在 `main()` 中显式调用线程初始化函数。

## 4. 内核对象与线程控制块

### 4.1 RT-Thread 对象模型

RT-Thread 采用统一内核对象模型。 `struct rt_thread` 以 `struct rt_object` 为基础，后者提供对象名称、类型、标志和对象容器链表节点等公共信息。

| `struct rt_object` 成员 | 作用 |
| --- | --- |
| `name[RT_NAME_MAX]` | 对象名称，用于 MSH 和调试识别 |
| `type` | 对象类别及静态/动态对象标志 |
| `flag` | 对象控制标志 |
| `list` | 对象容器链表节点 |

对象容器用于枚举和管理内核对象；`list_thread` 通过线程对象容器显示系统线程状态。

### 4.2 `struct rt_thread` 关键成员

| 字段 | 作用 |
| --- | --- |
| `sp` | 保存线程栈指针，用于上下文切换 |
| `entry` | 线程入口函数 |
| `parameter` | 线程入口参数 |
| `stack_addr` | 线程栈起始地址 |
| `stack_size` | 线程栈大小 |
| `stat` | 当前线程状态 |
| `current_priority` | 当前优先级 |
| `init_priority` | 初始优先级 |
| `init_tick` | 初始时间片 |
| `remaining_tick` | 当前剩余时间片 |

每个线程具有独立栈空间。上下文切换时，CPU 寄存器现场保存到当前线程栈中；再次调度该线程时，内核从其栈中恢复上下文。

## 5. 静态线程与动态线程

| 项目 | 动态线程 | 静态线程 |
| --- | --- | --- |
| API | `rt_thread_create()` | `rt_thread_init()` |
| TCB | Heap 动态分配 | 应用静态提供 |
| Stack | Heap 动态分配 | 应用静态提供 |
| 生命周期 | 运行时管理 | 应用确定 |
| 动态内存依赖 | 有 | 无 |
| 内存确定性 | 相对较低 | 较高 |
| 本实验对象 | `th_led1`、`th_monitor` | `th_led2` |

`rt_thread_create()` 为线程控制块和线程栈分配堆内存，失败时返回 `RT_NULL`。 `rt_thread_init()` 初始化由应用提供的线程控制块和栈，返回错误码表示初始化结果。两种方式均需调用 `rt_thread_startup()` 后才进入就绪队列。

动态线程删除后先进入 defunct 队列，由 idle 线程延后回收线程栈和动态对象。静态线程 detach 后解除对象管理关系，不释放应用提供的静态内存。静态线程的内存布局在编译和链接阶段即可确定，不依赖运行时 Heap，可降低动态分配失败和内存碎片对长期运行系统的影响，适用于资源确定性要求较高的场景。

## 6. 线程状态与调度

### 6.1 五态模型

| 状态 | 含义 | 典型触发条件 |
| --- | --- | --- |
| `RT_THREAD_INIT` | 已初始化，尚未启动 | `rt_thread_create()` / `rt_thread_init()` 后 |
| `RT_THREAD_READY` | 可运行，等待调度 | `rt_thread_startup()`、超时或恢复后 |
| `RT_THREAD_RUNNING` | 当前 CPU 正在执行 | 调度器选中线程 |
| `RT_THREAD_SUSPEND` | 等待延时、事件或挂起 | `rt_thread_mdelay()`、等待对象或 `rt_thread_suspend()` |
| `RT_THREAD_CLOSE` | 线程已关闭 | `rt_thread_delete()` 或线程入口结束 |

```text
INIT -- rt_thread_startup() --> READY -- scheduler --> RUNNING
                                      ▲                 │
                                      │                 ├─ delay / wait / suspend ─► SUSPEND
                                      │                 │                              │
                                      └── timeout / resume ────────────────────────────┘
RUNNING -- delete / return --> CLOSE
```

`SUSPEND` 同时表示延时等待和人工挂起。因此，程序通过 `manual_suspend=yes/no` 区分 KEY0 控制的人工挂起与普通延时状态。

### 6.2 抢占式调度

RT-Thread 采用基于优先级的抢占式调度，优先级数值越小，优先级越高。 `th_monitor` 的优先级为 10，高于 `th_led1` 和 `th_led2` 的 16。当高优先级线程进入就绪态时，调度器可以抢占低优先级运行线程。

### 6.3 时间片轮转

`th_led1` 与 `th_led2` 的优先级均为 16、初始时间片均为 5 tick。当多个同优先级线程同时处于就绪态时，调度器按照剩余时间片进行轮转。周期延时会使线程暂时进入 `SUSPEND`，从而让其他就绪线程获得 CPU。

## 7. 多线程实验设计

| 名称 | 创建方式 | 优先级 | 时间片 | 栈 | 功能 |
| --- | --- | ---: | ---: | ---: | --- |
| `th_led1` | 动态 | 16 | 5 tick | 1024 B | PF12 翻转、计数和 tick 输出 |
| `th_led2` | 静态 | 16 | 5 tick | 1024 B | PF11 翻转、计数和 tick 输出 |
| `th_monitor` | 动态 | 10 | 5 tick | 1024 B | KEY0 检测、线程状态和剩余栈监测 |

```text
KEY0 (PC0, active-low)
        │
        ▼
th_monitor (priority 10) ── request / resume ──► th_led1 (priority 16) ──► LED_R (PF12)
        │
        └── periodic status and stack report

th_led2 (priority 16) ──────────────────────────────────────────────────────► LED_B (PF11)
```

### 7.1 `th_led1`

`th_led1` 使用 `rt_thread_create()` 动态创建。线程每隔约 500 ms 翻转 PF12，并输出 `rt_tick_get()` 与计数值。

### 7.2 `th_led2`

`th_led2` 使用全局 `struct rt_thread` 和 1024 B 对齐静态栈，通过 `rt_thread_init()` 初始化。线程每隔约 700 ms 翻转 PF11，并输出计数值。

### 7.3 `th_monitor`

`th_monitor` 使用 `rt_thread_create()` 动态创建，优先级为 10。监控线程采用 20 ms 基础采样周期检测 KEY0，并累计至约 2000 ms 输出一次线程状态和栈余量。该方式在保持约 2 s 状态报告周期的同时，避免单次 2000 ms 阻塞造成较大的按键响应延迟。

### 7.4 KEY0 挂起与恢复

KEY0 使用内部上拉输入，按下为低电平。连续 3 次 20 ms 采样结果一致后确认电平变化，用于抑制按键抖动。

RT-Thread 4.1.1 中，`rt_thread_suspend()` 对调用上下文存在约束。 `th_monitor` 仅设置挂起请求；`th_led1` 在自身线程上下文的安全点执行 `rt_thread_suspend(rt_thread_self())`。再次检测到有效按键后，`th_monitor` 调用 `rt_thread_resume(th_led1)` 恢复线程运行。

### 7.5 栈空间监控

RT-Thread 在线程栈初始化时填充 `'#'` 字节。Cortex-M 栈向低地址方向增长，程序从栈起始地址扫描连续 `'#'` 字节以估计剩余栈空间。状态读取使用 `thread->stat & RT_THREAD_STAT_MASK`，并转换为可读状态名。

### 7.6 `thread_dump`

```text
msh />thread_dump
msh />list_thread
```

`thread_dump` 由 `MSH_CMD_EXPORT` 导出，输出三个实验线程的状态、优先级和剩余栈空间；`list_thread` 显示系统全部线程。

## 8. 构建与资源占用

### 8.1 RT-Thread Studio

RT-Thread Studio 的 Debug Clean Build 结果如下。

| 检查项 | 结果 |
| --- | --- |
| Build | PASS，0 errors，0 warnings |
| Flash | 61.09 KB |
| RAM | 6.20 KB |

### 8.2 SCons

执行命令：

```text
scons -Q -j1
```

结果：

```text
return code = 0
text = 60676 B
data = 1880 B
bss  = 4464 B
```

SCons 构建成功生成 `rt-thread.elf`。

## 9. 实机验证结果

### 9.1 ST-Link 下载

ST-Link 通过 SWD 连接 STM32F407，下载 `rtthread.bin` 至 `0x08000000` 后完成校验并复位。目标芯片 Device ID 为 `0x413`。

### 9.2 串口运行

COM7 采用 115200 8-N-1 配置。串口日志显示 `th_led1` 按约 500 ms 周期输出，`th_led2` 按约 700 ms 周期输出，监控线程按约 2 s 周期报告状态与栈空间。

### 9.3 `thread_dump` 与 `list_thread`

实机日志中的 `thread_dump` 输出：

```text
Day2 thread study status:
[th_led1] state=suspend, priority=16, stack_free=776/1024 bytes
[th_led1] manual_suspend=no
[th_led2] state=suspend, priority=16, stack_free=776/1024 bytes
[th_monitor] state=suspend, priority=10, stack_free=724/1024 bytes
```

`list_thread` 显示 `th_monitor` 优先级为 10，`th_led1` 和 `th_led2` 优先级均为 16，与线程配置一致。

### 9.4 KEY0 Suspend / Resume

实机日志中的按键控制过程：

```text
[monitor] KEY0 pressed: request th_led1 suspend
[th_led1] manual suspend entered
[monitor] KEY0 pressed: resume th_led1, result=0
[th_led1] resumed by monitor
[th_led1] tick=160425, count=317, LED_R toggled
```

日志表明，第一次 KEY0 有效按下后，`th_led1` 进入人工挂起状态；第二次有效按下后，`rt_thread_resume()` 返回 `RT_EOK`，`th_led1` 恢复周期输出。 `th_led2` 与 `th_monitor` 在此过程中保持运行。

完整串口输出见同目录 `day2_serial.log`。

| 检查项 | 结果 |
| --- | --- |
| Studio Clean Build | PASS，0 errors，0 warnings |
| SCons Build | PASS |
| ST-Link Program / Verify | PASS |
| COM7 Console | PASS |
| `th_led1` / `th_led2` / `th_monitor` | PASS |
| `thread_dump` / `list_thread` | PASS |
| KEY0 Suspend / Resume | PASS |

## 10. 结论

实验完成了 RT-Thread 动态线程、静态线程和高优先级监控线程的设计与运行验证。三个线程能够按照设定优先级和周期稳定运行，KEY0 可控制 `th_led1` 挂起与恢复，`thread_dump` 能够输出线程状态及剩余栈空间。RT-Thread Studio 与 SCons 构建均通过，程序经 ST-Link 下载后在 STM32F407 星火 1 号平台运行正常。
