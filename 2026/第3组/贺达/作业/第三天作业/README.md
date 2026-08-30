# 第三天作业：线程间同步与通信

## 1. 实验目标

本次实验围绕 RT-Thread 的四类 IPC 机制展开，通过资源管理、共享数据保护、多条件汇合和结构化消息传递，验证不同机制的语义与适用边界。

| MSH 命令 | 实验场景 | 主要机制 | 工作线程 |
| --- | --- | --- | --- |
| `parking_demo` | 3 个车位供 5 辆车竞争 | 计数信号量、互斥量 | 5 个车辆线程 |
| `ticket_demo` | 两个窗口销售 20 张票 | 互斥量 | 每阶段 2 个售票线程 |
| `startup_demo` | 三个模块完成后启动业务 | 事件集 | 3 个初始化线程、1 个业务线程 |
| `package_demo` | 双生产者向分拣线程发送快递 | 消息队列、信号量 | 2 个收件线程、1 个分拣线程 |

每个命令另设一个管理线程，负责创建工作线程、等待实验结束并删除动态 IPC 对象。命令执行期间再次启动同一实验时，程序会报告 `demo is already running`；实验结束后运行标志复位，可以再次执行。

### 1.1 实验环境

| 项目 | 配置 |
| --- | --- |
| 目标处理器 | STM32F407ZG，Cortex-M4 |
| 操作系统 | RT-Thread 5.3.0 |
| 编译工具 | SCons、Arm GNU Toolchain；Keil MDK / Arm Compiler 6.24 交叉验证 |
| 调试接口 | 板载调试器与串口终端 |
| 串口参数 | 115200 bit/s，8 数据位，1 停止位，无校验 |
| 命令入口 | FinSH/MSH |

实测固件启动信息中的构建时间为 `Aug 21 2026 23:01:39`。实验现象以串口日志为主，不依赖 LED、屏幕或传感器等外设动作。

### 1.2 公共执行结构

四个实验采用相同的生命周期管理方式：

```text
MSH 命令
   │
   ├── 检查 running 标志，拒绝重复启动
   │
   └── 创建管理线程
          │
          ├── 创建 IPC 对象并检查返回值
          ├── 创建并启动工作线程
          ├── 等待工作线程的完成通知
          ├── 删除动态 IPC 对象
          └── 清除 running 标志
```

这种结构将业务线程与资源回收分离，保证对象不会在线程仍访问时被提前删除，也使同一命令可以在前一次实验结束后再次运行。

## 2. 提交文件

```text
第三天作业/
├── README.md
├── parking_demo.c
├── ticket_demo.c
├── startup_demo.c
└── package_demo.c
```

源码没有直接套用 `kernel_samples`。所有线程和 IPC 对象在创建后均检查返回值，线程启动失败时也会进入相应的清理路径。

## 3. 构建验证

将四个源文件加入 BSP 的 `applications` 目录后执行 SCons 构建：

```text
scons -j16
```

2026-08-21 的本地构建结果如下：

```text
CC build\applications\package_demo.o
CC build\applications\parking_demo.o
CC build\applications\startup_demo.o
CC build\applications\ticket_demo.o
LINK rt-thread.elf

Memory region         Used Size  Region Size  %age Used
CODE                    94052 B         1 MB      8.97%
RAM1                     5528 B       128 KB      4.22%

text     data      bss      dec
94052    1228      4296    99576
scons: done building targets.
```

构建过程未出现编译错误，成功生成 `rt-thread.elf`、`rtthread.hex` 和 `rtthread.bin`。

同一版本又通过 Keil 工程进行交叉构建，四个实验源文件均参与编译，结果为：

```text
compiling parking_demo.c...
compiling package_demo.c...
compiling ticket_demo.c...
compiling startup_demo.c...
linking...
0 Error(s), 0 Warning(s)
```

随后完成固件擦除、下载和校验，开发板能够正常进入 RT-Thread MSH 控制台。

## 4. 练习一：停车场车位管理

### 4.1 设计说明

`parking_slots` 是初值为 3 的计数信号量。车辆线程只有成功获取一次信号量后才能进入停车场，离开时释放一次，因此内核中的信号量计数直接表示剩余车位。

车辆最多等待 2 秒；若未获得车位，则输出超时信息，经过短暂退避后重新申请。前三辆车的停留时间大于 2 秒，因而后到车辆能够实际触发超时路径。`parking_used` 仅用于日志统计，由独立互斥量保护，不参与车位准入判定。

车辆线程结束前释放完成信号量，管理线程收到 5 次完成通知后再删除全部 IPC 对象，避免对象仍被使用时提前释放。

### 4.2 测试方法与判定条件

执行：

```text
msh >parking_demo
```

应观察到以下行为：

- 最先获得资源的 3 辆车进入停车场；
- 其余车辆等待 2 秒后报告超时，并继续重试；
- 有车辆离开并释放信号量后，等待车辆才能进入；
- 任一日志中的 `used spaces` 均不超过 `3/3`；
- 全部车辆离开后，命令可以重新执行。

典型日志格式：

```text
[parking] car1 entered, used spaces: 1/3
[parking] car2 entered, used spaces: 2/3
[parking] car3 entered, used spaces: 3/3
[parking] car4 wait timeout, retrying
[parking] car1 left, used spaces: 2/3
[parking] car4 entered, used spaces: 3/3
```

### 4.3 实测结果

开发板实测时，最初 3 辆车占满车位，另外两辆车在 2 秒后报告等待超时；有车辆离开后，两辆等待车辆依次进入。关键结果为：

```text
[parking] car2 wait timeout, retrying
[parking] car1 wait timeout, retrying
[parking] car2 left, used spaces: 0/3
[parking] finished, peak occupancy never exceeded 3
```

整个过程中占用量没有超过 `3/3`，全部车辆退出后恢复为 `0/3`，符合计数信号量的资源约束。

### 4.4 思考题

停车位是包含 3 个等价资源的资源池。计数信号量可以用初值表示可并行占用的资源数量，每次获取和释放分别对应车位占用与归还。互斥量只有“持有/未持有”两种状态，而且具有所有权语义，适合保护单一临界区，无法自然表达 3 个并发名额。因此，车位准入使用计数信号量；共享统计变量则使用互斥量保护。

## 5. 练习二：多窗口售票系统

### 5.1 对比实验设计

执行 `ticket_demo` 后会连续运行两个阶段：

1. 竞态阶段不使用互斥量，在读取余票数与写回结果之间主动延时 20 ms；
2. 安全阶段创建互斥量，保护“检查余票、取得票号、扣减余票、登记售出数量”这一完整事务。

两个阶段均由 A、B 两个窗口线程运行。第一阶段用于放大读—改—写竞争，第二阶段验证互斥保护后的结果。

### 5.2 测试方法与判定条件

执行：

```text
msh >ticket_demo
```

无锁阶段可能出现两个窗口售出相同票号：

```text
[ticket/race] window A sold ticket 20
[ticket/race] window B sold ticket 20
```

互斥阶段的最终统计应满足：

```text
[ticket/safe] all tickets sold: 20, remaining: 0
```

同时应确认互斥阶段没有重复票号，余票数没有小于 0。

### 5.3 实测结果

无锁阶段中，窗口 A、B 多次读取到同一个余票值，例如两个窗口均售出 20 号票，竞态现象能够稳定复现。进入互斥阶段后，票号从 20 到 1 各出现一次，最终输出：

```text
[ticket/race] window B sold ticket 20
[ticket/race] window A sold ticket 20
...
[ticket/safe] all tickets sold: 20, remaining: 0
```

对比结果说明，互斥量保护完整事务后消除了重复售票，剩余票数也保持为 0。

### 5.4 思考题

只给 `ticket_count--` 这一条语句加锁仍不安全，因为售票是由多个相关操作构成的事务：先检查余票，再读取票号，随后扣减并记录结果。如果检查和读取位于临界区之外，两个线程仍可能同时判断“有票”，并保存同一个票号；即使扣减操作分别串行执行，业务数据已经出现重复。锁的范围必须覆盖会共同决定业务结果的完整读—改—写过程。

## 6. 练习三：系统启动条件检查

### 6.1 事件位设计

```c
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
```

网络、传感器和存储线程分别延时 700 ms、1300 ms 和 5000 ms 后发送对应事件。业务线程第一次使用 `RT_EVENT_FLAG_AND` 等待全部事件，超时时间为 3000 ms。由于存储初始化尚未完成，业务线程会根据就绪位输出缺失模块，然后再次以 AND 模式持续等待。

第一次等待不使用 `RT_EVENT_FLAG_CLEAR`，因此已经到达的网络和传感器事件位会被保留；第二次等待成功后再清除目标事件位。

### 6.2 测试方法与判定条件

执行：

```text
msh >startup_demo
```

预期顺序如下：

```text
[startup] network ready at 700 ms
[startup] sensor ready at 1300 ms
[startup] wait modules timeout, storage not ready
[startup] storage ready at 5000 ms
[startup] all modules are ready
[startup] business task started
```

业务线程不得在存储事件到达前打印启动信息。

### 6.3 实测结果

开发板串口实测输出为：

```text
[startup] network ready at 700 ms
[startup] sensor ready at 1300 ms
[startup] wait modules timeout, storage not ready
[startup] storage ready at 5000 ms
[startup] all modules are ready
[startup] business task started
```

网络和传感器事件先到达，3 秒等待超时后准确指出存储模块尚未完成；存储事件在 5 秒时到达，随后业务线程才启动，满足事件集 AND 等待要求。

### 6.4 思考题

三个信号量也能分别表示三个模块完成，但业务线程通常需要依次获取，等待顺序会影响超时预算，也不便一次得到多个条件的状态。事件集把多个布尔条件编码在同一对象中，能够以 AND 模式等待全部条件、以 OR 模式等待任意条件，还可保留已到达事件供下一次判断。对于“多个条件汇合后继续”的场景，事件集的表达更集中；信号量更适合资源计数或单一完成通知。

## 7. 练习四：快递分拣中心

### 7.1 消息与结束协议

消息结构与题目要求一致：

```c
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};
```

当前 RT-Thread 5.3 中，`rt_mq_recv()` 成功时返回实际接收的字节数，而不是固定返回 `RT_EOK`。因此分拣线程以返回值是否等于 `sizeof(struct package_msg)` 判断一条完整消息是否接收成功。

两个收件线程分别使用 `1101~1105` 和 `2201~2205` 的编号范围，保证 10 个编号互不重复。消息队列容量固定为 5，生产速度高于分拣速度；当 `rt_mq_send` 因队列已满而失败时，发送线程累计失败次数，延时后重试同一条消息，避免崩溃和静默丢件。

两个发送线程各释放一次完成信号量。管理线程确认两者全部结束后，才向同一消息队列发送唯一的结束消息 `PACKAGE_END_ID`。FIFO 顺序保证分拣线程先处理此前入队的普通快递，再收到结束标记并汇总。

### 7.2 测试方法与判定条件

执行：

```text
msh >package_demo
```

检查内容包括：

- 10 个快递编号均出现且不重复；
- 区域名称只出现 `east`、`south`、`north`；
- 队列满时出现 `queue full ... retry`，程序仍继续运行；
- 最终 `total packages` 为 10；
- 三个区域数量之和为 10；
- 输出总重量和发送失败/重试次数；
- 统计完成后任务退出，可再次执行命令。

典型统计格式：

```text
[package] total packages: 10
[package] east: 4, south: 3, north: 3
[package] total weight: 10800 g, send failed/retried: 10
```

### 7.3 实测结果

消息队列容量为 5，两个生产者连续发送时多次触发队列满，并按设计延时重试。分拣线程最终仍收到全部 10 条普通消息，统计结果为：

```text
[package] total packages: 10
[package] east: 4, south: 3, north: 3
[package] total weight: 10800 g, send failed/retried: 10
```

`send failed/retried: 10` 表示普通快递首次入队失败后累计重试 10 次，并不表示丢失了 10 件快递。最终总数为 10，三个区域数量之和也为 10，说明重试机制避免了队列满造成的数据丢失。

## 8. 综合验证步骤

1. 将构建生成的固件烧录到开发板并复位；
2. 打开串口终端，参数设置为 `115200 8N1`；
3. 执行 `help`，确认四个 MSH 命令均已注册；
4. 依次执行 `parking_demo`、`ticket_demo`、`startup_demo` 和 `package_demo`；
5. 每个实验结束后再次执行同一命令，确认资源已经回收；
6. 保存四个命令从启动到结束统计的串口日志。

由于管理线程在后台执行，MSH 命令函数启动任务后即可返回，终端提示符 `msh >` 可能出现在实验日志之间。这是异步命令执行产生的正常现象，不表示实验提前结束。多个业务线程的单行输出已经使用短调度临界区串行化，避免不同日志在同一行内互相截断。

## 9. 调试问题与修正记录

### 9.1 旧实验线程持续输出

最初测试工程仍包含第二天的 `thread_study.c`，其中的 `INIT_APP_EXPORT` 会在系统启动后自动创建监控线程，造成周期日志持续输出。测试时将该源文件从当前固件构建中排除，使第三天实验的串口输出保持独立。

### 9.2 消息队列返回值误判

初版代码把 `rt_mq_recv()` 的返回值与 `RT_EOK` 比较，串口反复出现 `receive failed: 8`。检查 RT-Thread 5.3 接口后确认：接收成功时返回实际复制的字节数，本实验消息结构大小正好为 8 字节。

修正后使用以下条件判断完整消息：

```c
received_size == (rt_ssize_t)sizeof(struct package_msg)
```

重新构建和烧录后，10 条消息全部完成分拣，区域数量与总重量统计正确。

### 9.3 并发日志交错

多个同优先级线程同时调用 `rt_kprintf()` 时，原始日志曾出现两条文本互相穿插。修正方案是在单条打印期间进入短调度临界区，打印结束后立即退出。该处理只保证单行日志完整，不改变实验所验证的 IPC 调度关系。

### 9.4 事件发送后的打印顺序

存储线程发送事件后会立即唤醒优先级更高的业务线程，初版日志可能先显示业务启动，再显示 `storage ready`。事件逻辑本身正确，但文本顺序不利于阅读。修正版在模块完成初始化后先记录就绪日志，再发送事件，因此实测顺序能够直接反映“存储就绪后业务启动”。

## 10. 验收结果

| 验收项目 | 实测结果 | 状态 |
| --- | --- | --- |
| 4 个独立 MSH 命令 | `parking_demo`、`ticket_demo`、`startup_demo`、`package_demo` 均已注册并运行 | 通过 |
| 每项至少两个线程 | 四项实验均包含多个工作线程 | 通过 |
| 创建结果检查 | 线程及动态 IPC 对象均检查创建结果 | 通过 |
| 重复启动保护 | 运行期间拒绝同名实验再次启动，结束后释放资源 | 通过 |
| 停车场容量约束 | 峰值为 3，最终占用数恢复为 0 | 通过 |
| 售票竞态对比 | 无锁阶段出现重复票号，加锁后售出 20 张且余票为 0 | 通过 |
| 启动条件汇合 | 3 秒报告存储未就绪，5 秒后业务启动 | 通过 |
| 快递消息统计 | 共 10 件，区域为 4/3/3，总重量 10800 g | 通过 |
| 队列满处理 | 记录 10 次失败重试，最终无消息丢失 | 通过 |
| 工程构建 | SCons 成功；Keil 0 Error、0 Warning | 通过 |

## 11. 当前状态

已完成：

- 四个 IPC 实验源码；
- SCons 与 Keil 双重构建验证；
- 目标板烧录及串口运行验证；
- 理论分析、思考题和实测结果整理。

当前没有影响作业验收的已知问题。并发线程的实际执行顺序可能随调度时机变化，但资源上限、票数一致性、事件先后约束和消息统计等关键不变量应保持不变。

## 12. 实验结论

- 计数信号量用于控制有限资源的可用数量；
- 互斥量用于保护具有一致性要求的完整共享数据事务；
- 事件集适合表达多个独立条件的 AND/OR 汇合；
- 消息队列通过复制结构化消息实现生产者与消费者解耦；
- 动态对象不仅要检查创建结果，还必须规划清晰的退出、通知与回收顺序。
