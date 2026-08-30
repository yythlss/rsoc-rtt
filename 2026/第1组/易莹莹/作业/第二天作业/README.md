# Part 1 理论整理 
## 1. STM32F407 启动流程与 RT-Thread 引导机制
### 启动路径
- 详细画出/写出从上电复位 `Reset_Handler` 到进入 `main()` 的完整调用链。
![STM32F407 启动流程图](figures/Pasted%20image%2020260818223818.png)
```text
上电 / 复位
  ↓
Reset_Handler（设置栈、向量表，执行 SystemInit）
  ↓
工具链 C 运行时初始化
  ├─ GCC：entry()
  ├─ ARMCC：$Sub$$main()
  └─ IAR：__low_level_init()
  ↓
rtthread_startup()
  ├─ 关闭全局中断
  ├─ rt_hw_board_init()：时钟、GPIO、UART、堆等板级初始化
  ├─ rt_show_version()：打印 RT-Thread banner
  ├─ rt_system_timer_init()
  ├─ rt_system_scheduler_init()
  ├─ rt_system_signal_init()（启用信号时）
  ├─ rt_application_init()：创建 main 线程
  ├─ rt_system_timer_thread_init()（启用软件定时器线程时）
  ├─ rt_thread_idle_init()：创建 idle 线程
  └─ rt_system_scheduler_start()：启动调度器
       ↓
main 线程 main_thread_entry()
  ├─ rt_components_init()：按等级执行自动初始化函数
  └─ main()：用户应用入口
```

- RT-Thread 的 `main()` 不是复位后直接调用的裸机入口。而是会先创建 `main` 线程，再由 `main_thread_entry()` 执行组件初始化，最后进入用户的 `main()`。
- 调度器启动后，系统按照数值更小优先级更高的规则选择就绪线程；`idle` 线程优先级最低，在没有其他就绪线程时运行。
### 自动初始化原理
- 解释 `INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT` 的底层实现原理。
`INIT_BOARD_EXPORT(fn)` 和 `INIT_APP_EXPORT(fn)` 都是 `INIT_EXPORT(fn, level)` 的封装，区别只是登记的等级不同：

|            宏            | 等级  | 执行阶段  |      典型内容      |
| :---------------------: | :-: | :---: | :------------: |
|   `INIT_BOARD_EXPORT`   | `1` | 板级初始化 |  GPIO、时钟、底层设备  |
|  `INIT_DEVICE_EXPORT`   | `3` | 设备初始化 | UART、SPI、I2C 等 |
| `INIT_COMPONENT_EXPORT` | `4` | 组件初始化 |   DFS、网络协议栈    |
|    `INIT_ENV_EXPORT`    | `5` | 环境初始化 |  文件系统挂载、环境变量   |
|    `INIT_APP_EXPORT`    | `6` | 应用初始化 |   应用线程、应用服务    |

宏把函数指针放入名称类似 `.rti_fn.1` / `.rti_fn.6` 的特定段；链接脚本按等级把这些段连续排列，并生成起止符号。`rt_components_init()` 从 `__rt_init_rti_board_end` 遍历到 `__rt_init_rti_end`，依次取出函数指针并调用。因此，应用只需要写：


```c
static int thread_study_init(void)
{
    /* 创建线程、配置引脚等 */
    return 0;
}
INIT_APP_EXPORT(thread_study_init);
```

就可以在 `main()` 前由系统自动执行，不需要在 `main.c` 中硬编码调用。
- 自动初始化的关键是“宏登记 + 链接器排序 + 统一遍历调用”。
## 2. 内核基础与对象模型
### 对象容器与控制块
- 简述 RT-Thread 面向对象的设计思想，线程控制块（`struct rt_thread`）中关键成员（如 `stat`、`current_priority`、`init_tick`、`sp`）的作用。
RT-Thread 把线程、信号量、互斥量、邮箱等都抽象成内核对象。对象有统一的名称、类型和链表节点，具体对象再扩展自己的控制字段。
- 这样做可以用统一的对象管理、查找、挂起、恢复和调度接口，体现了 RT-Thread 的面向对象思想。

|             成员              |                      作用                       |
| :-------------------------: | :-------------------------------------------: |
|           `stat`            | 线程状态和附加标志，低位可与 `RT_THREAD_STAT_MASK` 相与得到五态之一 |
|     `current_priority`      |               当前有效优先级，数值越小优先级越高               |
|         `init_tick`         |            同优先级线程的初始时间片，时间片耗尽后触发轮转            |
|            `sp`             |               保存的线程栈指针，上下文切换时恢复               |
| `stack_addr` / `stack_size` |       栈起始地址和总大小，初始化时会填充 `#`，可据此统计历史剩余空间       |
|  `tlist` / `thread_timer`   |              就绪 / 挂起链表节点和线程延时定时器              |

### 内存分配方式
- 对比静态对象与动态对象的区别（分配位置、生命周期、释放机制），分析为什么某些高可靠场景强制使用静态线程。
#### 静态创建

```c
rt_thread_t rt_thread_create(const char* name,
                            void (*entry)(void* parameter),
                            void* parameter,
                            rt_uint32_t stack_size,
                            rt_uint8_t priority,
                            rt_uint32_t tick);
```

- 线程句柄（线程控制块指针）、线程栈由用户提供，一般被设置为全局变量，在编译时就确定，内核不负责动态分配内存空间。
- 用户提供的栈首地址需要做系统对齐（如ARM上需要做4字节对齐）

| 参数        | 描述                                                         |
| ----------- | ------------------------------------------------------------ |
| thread      | 线程句柄。线程句柄由用户提供出来，并指向对应的线程控制块内存地址 |
| name        | 线程的名称；线程名称的最大长度由 rtconfig.h 中定义的 RT_NAME_MAX 宏指定，多余部分会被自动截掉 |
| entry       | 线程入口函数                                                 |
| parameter   | 线程入口函数参数                                             |
| stack_start | 线程栈起始地址                                               |
| stack_size  | 线程栈大小，单位是字节。在大多数系统中需要做栈空间地址对齐（例如 ARM 体系结构中需要向 4 字节地址对齐） |
| priority    | 线程的优先级。优先级范围根据系统配置情况（rtconfig.h 中的 RT_THREAD_PRIORITY_MAX 宏定义），如果支持的是 256 级优先级，那么范围是从 0～255，数值越小优先级越高，0 代表最高优先级 |
| tick        | 线程的时间片大小。时间片（tick）的单位是操作系统的时钟节拍。当系统中存在相同优先级线程时，这个参数指定线程一次调度能够运行的最大时间长度。这个时间片运行结束时，调度器自动选择下一个就绪态的同优先级线程进行运行 |
| **返回**    | **——**                                                       |
| RT_EOK      | 线程创建成功                                                 |
| -RT_ERROR   | 线程创建失败                                                 |

#### 动态创建

```c
rt_thread_t rt_thread_create(const char* name,
                             void (*entry)(void* parameter),
                             void* parameter,
                             rt_uint32_t stack_size,
                             rt_uint8_t priority,
                             rt_uint32_t tick);
```

- 调用这个函数时，系统会从**动态堆内存**中分配一个线程句柄以及按照参数中指定的栈大小相应的空间。

- 分配出来的栈空间是按照 `rtconfig.h` 中配置的 `RT_ALIGN_SIZE` 方式对齐。
#### 对比

| 对比项  |              静态线程               |                       动态线程                       |
| :--: | :-----------------------------: | :----------------------------------------------: |
| 创建方式 | 全局 TCB + 全局栈，`rt_thread_init()` |         `rt_thread_create()` 从堆申请 TCB 和栈         |
| 内存位置 |   编译期确定，通常位于 `.bss` / 静态 RAM    |              运行时从 RT-Thread heap 分配              |
| 生命周期 |       由程序生命周期管理，通常不会自动释放        | `rt_thread_delete()` 后由内核回收，线程退出后进入 defunct 回收流程 |
|  优点  |      无堆碎片、创建结果可预测，适合安全关键路径      |                 编程方便，栈大小和线程数量更灵活                 |
|  风险  |       占用固定 RAM，数量过多会浪费空间        |             可能申请失败，也可能出现碎片和生命周期管理错误              |
**分析：** 
- 在电机控制、故障保护、汽车电子等高可靠场景，线程数量和栈空间通常需要在设计阶段确定，所以优先使用静态线程：系统启动后不依赖堆分配，能避免运行时 `malloc` 失败和碎片化导致的不确定行为。
## 3. 线程状态机与调度算法
### 五态模型
- 绘制或描述 RT-Thread 线程状态（初始态、就绪态、运行态、挂起态、关闭态）及触发状态转换的典型 API。

|  状态  |                                                        描述                                                         |
| :--: | :---------------------------------------------------------------------------------------------------------------: |
| 初始状态 |                    当线程刚开始创建还没开始运行时就处于初始状态；在初始状态下，线程不参与调度。此状态在 RT-Thread 中的宏定义为 RT_THREAD_INIT                     |
| 就绪状态 |        在就绪状态下，线程按照优先级排队，等待被执行；一旦当前线程运行完毕让出处理器，操作系统会马上寻找最高优先级的就绪态线程运行。此状态在 RT-Thread 中的宏定义为 RT_THREAD_READY        |
| 运行状态 | 线程当前正在运行。在单核系统中，只有 rt_thread_self() 函数返回的线程处于运行状态；在多核系统中，可能就不止这一个线程处于运行状态。此状态在 RT-Thread 中的宏定义为 RT_THREAD_RUNNING |
| 挂起状态 |            也称阻塞态。它可能因为资源不可用而挂起等待，或线程主动延时一段时间而挂起。在挂起状态下，线程不参与调度。此状态在 RT-Thread 中的宏定义为 RT_THREAD_SUSPEND            |
| 关闭状态 |                       当线程运行结束时将处于关闭状态。关闭状态的线程不参与线程的调度。此状态在 RT-Thread 中的宏定义为 RT_THREAD_CLOSE                       |

#### 状态转换

![线程状态转换图](https://www.rt-thread.org/document/site/rt-thread-version/rt-thread-standard/programming-manual/thread/figures/04thread_sta.png)

- **初始 -> 就绪**
	- `rt_thread_create()`：动态创建线程并初始化线程控制块。
	- `rt_thread_init()`：静态初始化线程对象及相关参数。
	- `rt_thread_startup()`：启动线程，使其进入就绪状态。
- **就绪 -> 运行**
    - 调度器根据线程优先级和时间片选择就绪线程运行。
- **就绪 -> 挂起**
    - `rt_thread_delay()`：延时，让出 CPU。
    - `rt_sem_take()`：获取信号量。
    - `rt_mutex_take()`：获取互斥量。
    - `rt_mp_alloc()`：从内存池中申请一个内存块。
    - `rt_event_recv()`：接收事件集中的指定事件，并按照设定条件等待或返回。
    - `rt_mb_recv()`：从邮箱中接收一条消息。
    - `rt_mq_recv()`：从消息队列中接收一条消息。
- **挂起 -> 就绪**
    - `rt_thread_resume()`：恢复挂起线程，使其重新进入就绪状态。
    - `rt_sem_release()`：释放信号量，使信号量计数加一，并唤醒等待线程。
    - `rt_mutex_release()`：释放当前线程持有的互斥量。
    - `rt_mp_release()`：释放内存块，并将其归还到内存池。
    - `rt_event_send()`：发送指定事件，并唤醒满足条件的等待线程。
    - `rt_mb_send()`：向邮箱发送一条消息，邮箱已满时发送失败。
    - `rt_mq_send()`：向消息队列发送一条消息，队列已满时发送失败。
- **运行 -> 关闭**
    - `rt_thread_exit()`：退出当前线程，使线程进入关闭状态。
- **挂起 -> 关闭**
    - `rt_thread_delete()`：删除线程并释放动态分配的线程资源。
    - `rt_thread_detach()`：分离线程并释放线程相关资源。

### 调度机制剖析
#### 抢占式调度
线程从挂起态转为就绪态后，如果它的**优先级高于当前运行线程**，调度器会立即触发切换；高优先级线程不必等待低优先级线程主动让出 CPU。
#### 时间片轮转
当多个就绪线程拥有**相同优先级**时，每个线程最多运行 `init_tick` 个系统 tick，时间片用完后把当前线程放到同优先级队列尾部，轮到下一个线程。

# Part 2 实验结果分析与串口截图

## 实验结果分析
- 程序通过 `INIT_APP_EXPORT()` 自动初始化，并创建了三个线程：动态线程 `th_led1`、静态线程 `th_led2` 和监控线程 `th_monitor`。
    
- `th_led1` 优先级为 16、时间片为 5 ticks、栈大小为 1024 Bytes，负责翻转红色 LED；`th_led2` 使用静态 TCB 和栈，参数相同，负责翻转蓝色 LED。实验中两个 LED 均能正常闪烁，说明动态线程和静态线程创建成功。
    
- `th_monitor` 优先级为 10，通过轮询 PC0 检测 KEY0，并进行 50 ms 消抖。第一次按键后，`th_led1` 被挂起，红色 LED 停止运行；再次按键后，`th_led1` 恢复运行，计数值继续增加。
    
- 从串口结果可以看到，按键松开时 `key=1、suspended=1`，按下时 `key=0、suspended=0`，说明按键检测以及线程挂起、恢复功能正常。通过 `thread_dump` 和 `list_thread` 还可以查看线程优先级、运行状态和栈使用情况，实验结果符合要求。
## 串口截图
![串口运行截图](figures/Pasted%20image%2020260819001453.png)

# 作业要求

### 1. 提交目录规范

```
2026/<组别>/<姓名>/第二天作业
├── README.md             # 包含：Part 1 理论整理 + Part 2 实验结果分析与串口截图
└── thread_study.c        # 作业源码（含动/静态线程、自动初始化、MSH导出）
```

### 2. PR 提交流程与规范

1. Fork 官方仓库 [https://gitee.com/rtthread/rsoc-rtt](https://gitee.com/rtthread/rsoc-rtt) 到个人账号。
2. 基于 `master` 新建分支：`day2-姓名拼音`。
3. 编写代码、文档并进行本地 `scons` 编译验证与星火1号烧录测试。
4. 提交 Commit。
5. 向主仓库提交 Pull Request：
   - **PR 标题**：`[作业提交] 内核与多线程培训 - <姓名>`
   - **PR 内容**：附带理论文档简述、硬件运行现象照片及 FinSH 串口日志输出。
