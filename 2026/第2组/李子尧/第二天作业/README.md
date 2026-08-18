# RSOC 2026 Day2：RT-Thread 启动、线程与调度

## 1. 工程、板卡与交付范围

本次在既有的 RT-Thread Studio 工程 `G:\RT-ThreadStudio\workspace\RSOC26_Day2_F407` 中完成，不重建工程、不改动 `applications/main.c`。当前工程使用 RT-Thread **4.1.1**、STM32F407ZG 配置；连接到星火 1 号板时，按已安装 BSP 示例采用如下引脚：LED_R 为 PF12、LED_B 为 PF11、KEY0 为 PC0，均为低电平有效。

本目录仅交付本次作业的 `thread_study.c` 和本文档。程序通过 `INIT_APP_EXPORT` 自动加入应用初始化，不依赖在 `main()` 中手工调用。

## 2. RT-Thread 的实际启动链路

本节以当前工程的 GNU 工具链和 RT-Thread 4.1.1 源码为准，而不是只写抽象流程。

```text
复位 / Reset_Handler
        ↓ （链接脚本入口）
entry() → rtthread_startup()
        ↓
rt_hw_board_init()
        ↓
rt_components_board_init()     [板级自动初始化区间]
        ↓
rt_system_timer_init() / rt_system_scheduler_init()
        ↓
rt_application_init()          [创建 main 线程]
        ↓
rt_thread_idle_init() → rt_system_scheduler_start()
        ↓
main_thread_entry()
        ↓
rt_components_init()           [组件自动初始化，包括 INIT_APP_EXPORT]
        ↓
main()
```

`link.lds` 以 `ENTRY(Reset_Handler)` 指明 MCU 复位入口；本工程使用 GNU GCC 时，`components.c` 中的 `entry()` 再调用 `rtthread_startup()`。启动函数先关闭中断、执行 `rt_hw_board_init()`，再初始化定时器与调度器，创建应用主线程和 idle 线程，最后启动调度器。调度器启动后，`main_thread_entry()` 先执行 `rt_components_init()`，再进入用户的 `main()`。

因此，自动初始化并不是“编译器神奇地调用函数”，而是链接器和内核共同完成的：`link.lds` 用 `KEEP(*(SORT(.rti_fn*)))` 收集初始化函数指针；`rt_components_board_init()` 与 `rt_components_init()` 顺序遍历两个边界符号之间的指针并逐一调用。

### 2.1 `INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT`

在本工程未开启 `RT_DEBUG_INIT` 时，宏会把函数指针放入 `.rti_fn.<level>` 段：

```c
INIT_BOARD_EXPORT(board_driver_init); /* level "1"：板级初始化 */
INIT_APP_EXPORT(thread_study_init);   /* level "6"：应用初始化 */
```

链接脚本按段名排序；内核用 `rti_board_start/rti_board_end` 和 `rti_end` 作为哨兵。前者遍历板级区间，后者从 board 结束位置继续遍历至总结束位置。`thread_study_init()` 属于应用级，所以在 main 线程执行 `rt_components_init()` 时被调用；这也解释了本作业无需修改 `main.c`。

## 3. 内核对象、线程生命周期与调度

### 3.1 对象模型

所有内核对象以 `struct rt_object` 开头，核心字段是：

| 字段 | 作用 |
| --- | --- |
| `name[RT_NAME_MAX]` | 对象名，供 MSH 和调试识别 |
| `type` | 对象类别，并带有静态/动态标志 |
| `flag` | 对象控制标志 |
| `list` | 挂入相应对象容器的链表节点 |

线程对象在此基础上再保存 `sp`、入口函数、参数、栈首地址与大小、`stat`、当前/初始优先级、时间片、线程定时器等运行信息。对象容器使 `list_thread` 能枚举线程；本作业的 `thread_dump` 则针对三个作业线程给出状态与栈余量。

### 3.2 静态、动态创建与销毁

| 对比 | 动态线程 `rt_thread_create` | 静态线程 `rt_thread_init` |
| --- | --- | --- |
| 本作业对象 | `th_led1`、`th_monitor` | `th_led2` |
| TCB / 栈来源 | 内核堆分配 | 应用提供全局 TCB 与栈数组 |
| 失败特征 | 返回 `RT_NULL` | 返回错误码 |
| 删除后的清理 | idle 线程回收栈和动态对象 | detach 后解除对象，不释放应用静态内存 |

`rt_thread_create()` 内部先分配线程对象，再分配栈并调用初始化逻辑；`rt_thread_init()` 只初始化调用者提供的内存。二者完成初始化后都必须 `rt_thread_startup()` 才会进入就绪队列。动态线程被删除后先进入 defunct 队列，由 idle 线程延后回收，避免在线程自身栈仍被使用时立即释放。

### 3.3 五种基本状态

| 状态 | 含义 | 本程序中的可见情形 |
| --- | --- | --- |
| `INIT` | 已初始化、尚未启动 | `rt_thread_init/create` 后、`startup` 前 |
| `READY` | 可运行，等待被调度 | 同优先级线程轮转前后 |
| `RUNNING` | 当前 CPU 正在运行 | 被调度器选中时 |
| `SUSPEND` | 等待延时、事件或人工挂起 | 两个 LED 线程 `mdelay` 时；`th_led1` 人工暂停时 |
| `CLOSE` | 已关闭，等待/完成回收 | 本程序的无限循环线程正常运行时不会到达 |

`SUSPEND` 不能单独解释成“KEY0 人工暂停”：调用 `rt_thread_mdelay()` 的线程也会暂时处于该状态。因此程序额外输出 `manual_suspend=yes/no`，专门标识 KEY0 触发的暂停。

### 3.4 抢占与同优先级时间片

RT-Thread 的优先级数值越小越高。`th_monitor` 为 10，优先于 `th_led1` / `th_led2` 的 16；当它由 tick、按键状态改变等条件变为可运行时，会抢占较低优先级的 LED 线程。两个 LED 线程优先级相同、时间片均为 5 tick，因此在它们同时 ready 时调度器可按时间片轮转；但本程序的周期延时通常会先把运行线程放入延时等待，再让其他就绪线程运行。

## 4. 本次线程设计与代码对应

| 名称 | 创建方式 | 优先级 / 时间片 | 栈 | 行为 |
| --- | --- | --- | --- | --- |
| `th_led1` | `rt_thread_create` | 16 / 5 tick | 1024 B | 每 500 ms 翻转 PF12（红灯），打印 tick 与计数 |
| `th_led2` | `rt_thread_init` | 16 / 5 tick | 静态 1024 B，`RT_ALIGN_SIZE` 对齐 | 每 700 ms 翻转 PF11（蓝灯），打印 tick 与计数 |
| `th_monitor` | `rt_thread_create` | 10 / 5 tick | 1024 B | KEY0 防抖、暂停/恢复与约 2 s 状态报告 |

```text
KEY0（PC0，低有效）
         │
         ▼
th_monitor（优先级 10） ─请求/恢复─► th_led1（优先级 16） ─► LED_R / PF12
         │
         └─ 每约 2 s 输出状态与栈余量

th_led2（优先级 16） ─────────────────────────────────────► LED_B / PF11
```

KEY0 使用内部上拉输入。监控线程每 20 ms 采样，连续 3 次相同才承认电平变化；这样既有约 60 ms 的简易去抖，也能持续检测按键。累计采样时间达到 2000 ms 时才打印状态。这里**没有**直接 `rt_thread_mdelay(2000)`，因为那会令按键响应最坏延迟接近 2 s；20 ms 轮询累计同样得到约 2 s 报告周期，同时保持可交互性。

当前 RT-Thread 4.1.1 的 `rt_thread_suspend()` 要求目标线程是 `rt_thread_self()`。所以 KEY0 第一次按下时，监控线程仅置位请求；`th_led1` 在自己的循环安全点调用 `rt_thread_suspend(rt_thread_self())`，第二次按下后监控线程调用 `rt_thread_resume(th_led1)`。这样既符合内核断言，也不会把其他线程在未知临界区强行挂起。

## 5. 状态与栈检查命令

`thread_dump` 由 `MSH_CMD_EXPORT` 注册；串口 MSH 中可执行：

```text
msh />thread_dump
msh />list_thread
```

前者输出三个作业线程的状态、优先级、栈余量及 `th_led1` 的人工暂停标志；后者是内核命令，展示系统全部线程。栈余量以 RT-Thread 初始化时填充的 `'#'` 字节扫描得到，属于运行时估计值。

## 6. 构建、下载与串口回归

### 6.1 工具与设置

- RT-Thread Studio：当前工程的 Debug 配置。
- 下载器：ST-Link，SWD；实测连接时使用硬件复位可识别为 STM32F405/407，Device ID 为 `0x413`。
- 串口：ST-Link Virtual COM Port，`COM7`，`115200 8-N-1`。
- RT-Thread 版本：4.1.1。

### 6.2 实机结果

2026-08-18 此前的人工实机验证中，两灯按 500 ms / 700 ms 节奏运行；第一次 KEY0 后 `th_led1` 停止且显示 `manual_suspend=yes`，第二次 KEY0 后其约 500 ms 输出恢复并显示 `manual_suspend=no`。当时 `thread_dump` 读取到 `th_led1` 约 776/1024 B、`th_led2` 约 776/1024 B、`th_monitor` 约 724/1024 B 的空闲栈（测量值会随编译和运行路径略变）。

本次文档整理后的 Studio Clean Build 已通过，资源占用为 Flash **61.09 KB**、RAM **6.20 KB**。随后已通过 ST-Link 重新下载并校验 `rtthread.bin`，以硬件复位方式连接到 Device ID `0x413` 的 STM32F407；COM7 的真实输出保存在同目录 [day2_serial.log](day2_serial.log)。该日志包含两个 LED 线程的连续计数、监控线程约 2 s 状态报告、`thread_dump`、`list_thread`，以及 KEY0 请求暂停、`th_led1` 自挂起、`rt_thread_resume()` 返回 0、恢复后继续计数的全过程。

<!-- TODO: 用户补一张星火 1 号板 LED 实际运行照片。未使用他人图片，也不伪造图片。 -->

### 6.3 SCons 命令说明

在具备完整 SCons BSP 支持、包含 `SConstruct` 的根目录中：

```powershell
scons       # 构建
scons -j4   # 四任务并行构建
scons -c    # 清理 SCons 构建产物
```

本工程已执行真实 `scons -j4`：补齐 `applications/SConscript` 的板级头文件路径和 `STM32F407xx` 宏后，`main.c` 与 `thread_study.c` 均编译通过；但这个 Studio 管理工程原有 SCons 配置没有纳入启动文件、板级驱动和完整链接选项，最终链接缺少 `_exit`、`main` 及板级上下文切换符号而失败。故 **SCons 整工程链接为未通过**，不能以 Studio 的成功构建替代它。Studio 的 Debug 配置使用其自动生成的完整源文件和链接规则，Clean Build 已 0 errors、0 warnings。

## 7. 结果汇总与复现步骤

1. 用 Studio 构建 Debug 配置，确认 0 errors、0 warnings。
2. 通过 ST-Link 下载 `Debug/rtthread.bin` 到 `0x08000000`，执行校验并复位。
3. 打开 COM7 115200，观察启动、`th_led1`、`th_led2`、`monitor` 周期日志。
4. 在 MSH 发送 `thread_dump` 和 `list_thread`；短按 KEY0 两次，分别观察暂停与恢复。

源码保留了所有创建失败检查、启动顺序、GPIO 初始化、自动初始化宏及 MSH 导出。未修改 `main.c`，也未把动态/静态线程写成同一种方式。
