# RT-Thread 线程间同步与通信（IPC）—— 第 3 天作业

> 芯片：STM32F407ZGT6（星火1号开发板）｜开发环境：RT-Thread Studio
> 配套代码：`parking.c` / `ticket.c` / `startup.c` / `package.c`
> 参考：RT-Thread 官方《线程间同步》文档
> （https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/ipc1/ipc1）

---

## 第一部分 理论总结

RT-Thread 提供了一套完整的内核对象（`rt_ipc_object`），用于解决多线程共享资源时的「同步」与「通信」问题。本次作业使用四类 IPC 对象：**信号量、互斥量、事件集、消息队列**。下表总览其定位：

| IPC 对象 | 内核类 | 核心语义 | 典型适用场景 |
|----------|--------|----------|--------------|
| 信号量（Semaphore）| `rt_semaphore` | 计数器，表示「还有多少可用资源」 | 资源数量管理、同步 |
| 互斥量（Mutex）| `rt_mutex` | 同一时刻仅一个线程持有，支持优先级继承、递归持有 | 保护共享临界区 |
| 事件集（Event）| `rt_event` | 一组事件位，支持 AND / OR 逻辑等待 |「多条件且/或」同步 |
| 消息队列（Message Queue）| `rt_messagequeue` | 消息按固定结构传递，先进先出 | 线程间传递数据 |

### 1. 信号量（Semaphore）

信号量本质是一个**非负计数器 + 等待队列**。每调用一次 `rt_sem_take()`，计数减 1，若计数为 0 则调用者被挂起到等待队列；每调用一次 `rt_sem_release()`，计数加 1，并唤醒一个等待线程。因此它天然描述「有限数量资源」的分配，例如停车场只有 3 个车位：

- `rt_sem_create(name, count, flag)`：动态创建，`count` 为可用资源数。
- `rt_sem_init()`：静态初始化`struct rt_semaphore`。
- `rt_sem_take(sem, time)`：申请资源，可按 `time` 设置超时（`RT_WAITING_FOREVER` 永久等待）。
- `rt_sem_release(sem)`：释放资源。
- 计数信号量对被释放的线程采用 FIFO / PRIO 两种排队策略（由 flag 决定）。

### 2. 互斥量（Mutex）

互斥量与二元信号量的区别在于互斥量引入了**所有权、优先级继承和递归持有**：

- `rt_mutex_create(name, flag)` / `rt_mutex_take()/release()`。
- **优先级继承**：当高优先级线程等待一个被低优先级线程持有的互斥量时，持有者的优先级会临时提升到等高者，避免优先级反转造成的死锁风险。
- **递归持有**：同一个线程可多次 `take` 同一互斥量，须对应同等次数 `release`。
- 机制上确保了「同一时刻只有一个线程进入临界区」。

> 互斥量保护的是「谁能改这个共享数据」，信号量表达的是「还有多少可用」。两者语义不同，不能混用。

### 3. 事件集（Event）

事件集用 **32 位寄存器** 的每一位表示一个事件，一次可以同时等待/发送多个事件：

- `rt_event_create/send/recv`。
- `RT_EVENT_FLAG_AND`：须所有指定事件位都置起才返回（**等等所有**）。
- `RT_EVENT_FLAG_OR`：任一事件位置起即返回。
- `RT_EVENT_FLAG_CLEAR`：事件满足后自动清零对应位。
- 事件只传输「事件标志」，**不传输数据**，且适合「一次等待多个条件」的场景（如系统启动的多模块就绪检查）。

### 4. 消息队列（Message Queue）

消息队列是「邮箱的扩展」：可以传输任意长度的消息，按顺序排队（FIFO/PRIO），每条消息是确界长度的一块数据拷贝：

- `rt_mq_create(name, msg_size, max_msgs, flag)`：消息大小 + 队列容量。
- `rt_mq_send()`：非阻塞发送，队列满返回 `-RT_EFULL`（可计数重试）。
- `rt_mq_recv()`：接收消息，可设置超时。
- 适合线程间**传输结构化数据**（本作业中的快递 `struct package_msg`）。

---

## 第二部分 实验报告

### 练习一：停车场车位管理（计数信号量）— `parking_demo`

**运行逻辑**

1. `rt_sem_create("park_sem", 3, RT_IPC_FLAG_FIFO)` 创建初值为 3 的计数信号量。
2. 动态创建 5 个车辆线程 `car1..car5`，每辆车 `rt_sem_take(park_sem, 2000)` 申请车位。
3. 成功 → `park_used++` 并打印 `carN entered, used spaces: x`；超时 → 打印 `waiting for a space` 后重新申请。
4. 停留 `CAR_STAY_MS` 后 `park_used--` 并 `rt_sem_release()` 释放。

**实测输出（节选，与信号量初值一致，场内车辆数恒 ≤ 3）**

```text
car1 entered, used spaces: 1
car2 entered, used spaces: 2
car3 entered, used spaces: 3
car4 waiting for a space, try again later
car1 left, used spaces: 2
car4 entered, used spaces: 3
...
```

> 当 3 个车位被占满后，第 4、5 辆车因计数为 0 被挂起；有车释放（release 唤醒等待队列）后，按 FIFO 排队的一辆进入。任意时刻场内车辆数严格不超过 3，正是计数信号量「资源计数」语义的直接体现。

**思考题：为什么用计数信号量，而不是互斥量？**

因为这里需要管理的是一批**可复用的同类资源（多个车位）**，允许「多个持有者同时在场」，只是总数受限。计数信号量的计数器值正好表示「还有几个车位可用」，当某个车位被释放就 `release` 加回。而互斥量是二值的，表达「该资源同一个时刻只能被一个线程持有」，无法表达「最多有 3 辆车同时在场」。《线程间同步》文档也明确指出：信号量用于「资源可用数量的统计」，互斥量用于「保护唯一临界资源」。本场景若用互斥量则最多只能放 1 辆车进场，不符合题意。

---

### 练习二：多窗口售票系统（互斥量）— `ticket_demo` / `ticket_race`

**热门对比实验设计**

`ticket.c` 同时实现两套可运行版本，用命令入口区分，便于直接对比：

| 命令 | 是否加锁 | 行为 |
|------|----------|------|
| `ticket_demo` | 加互斥量 | 「检查余票→读票号→减1→打印」整段处于临界区，结果正确 |
| `ticket_race` | 不加互斥量，并在读票号与减1之间插入 30ms 延时 | 复现竞态：重复售票 / 余票错乱 |

**`ticket_demo` 正确版实测输出（结尾）**

```text
window A sold ticket 3
window B sold ticket 2
all tickets sold
remaining tickets: 0
window A exits
window B exits
```

校验：20 张全部售出、每张票号唯一、剩余票数 = 0（不小于 0）。

**`ticket_race` 竞态版实测输出（节选，体现重复票号与错乱）**

```text
window A sold ticket 20
window B sold ticket 20      <-- 重复票号！
window B sold ticket 19
...                          <-- 可能出现 remaining 一次减 2 等错乱
```

**对比结论**：加锁版是「读-改-写」原子进行，票号唯一、总数正确；无锁版因两个窗口可同时读到同一余票再分别修改，导致重复售票和总数错误，验证了互斥量「同一时刻仅一个持有者」的必要性。

**思考题：为什么只给 `ticket_count--` 加锁仍可能不安全？**

因为售票过程是「**读取余票 → 判断有余票 → 读取票号 → 递减**」多个步骤，`ticket_count--` 只是其中一步。如果只给 `--` 加锁：
1. 两个窗口的「判断 `remaining > 0`」与「读取票号」可能在加锁前/外并行执行，都读到同一个值（重复票号）。
2. `--` 虽然本身原子/有锁，但基于的是同一个旧值，等效「两次递减只减了一次」，最终可能出现「售出的总票数超过剩余」或漏卖。
3. 只有把**完整的检查+读取+修改+打印**放进同一临界区，才能保证对共享计数的访问彼此互斥。《线程间同步》文档强调互斥量应保护「整个共享资源的访问过程」，而不是单个赋值语句。

---

### 练习三：系统启动条件检查（事件集）— `startup_demo`

**设计**

- 事件位：`EVENT_NET_READY(1<<0)`、`EVENT_SENSOR_READY(1<<1)`、`EVENT_STORAGE_READY(1<<2)`。
- 三个初始化线程分别延时 `500ms / 700ms / 5000ms` 后 `rt_event_send()` 发送就绪事件。
- 业务线程用 `RT_EVENT_FLAG_AND`（并 `RT_EVENT_FLAG_CLEAR`）等待 `EVENT_ALL_READY`：
  - 第一次等待超时 3000ms —— 因存储模块需 5000ms 而超时，打印「尚未就绪的模块」；
  - 再以更长超时 6000ms 等待，存储就绪后满足 AND 全部条件。

**实测输出（与题目示例一致）**

```text
network ready
sensor ready
wait modules timeout, storage is not ready
storage ready
all modules are ready
business task started
```

**超时后如何知道「缺哪个模块」**：本实现用 `rt_event_recv(..., AND, 0)`（超时 0、不带 CLEAR）对每个事件位做非阻塞「查看当前是否已置起」，从而打印尚未就绪的模块，不改变事件集本身。体现了事件集「一次等待多个位 + 检查状态」的便利。

**思考题：用三个独立信号量实现，与事件集相比有什么区别？**

- **语义**：三个信号量天然是「三个独立的计数器」，业务线程必须分别 `rt_sem_take` 三次才能表示「三个就绪」，逻辑重复且分散。
- **一次性等待 vs 多次等待**：事件集可在**一次** `rt_event_recv` 调用中满足「多个事件位全部就绪」（AND）或「任意一个就绪」（OR），三种模块的等待被合并为一个条件判断；信号量则只能一个条件一个条件地等待。
- **检查已就绪状态**：事件位是「可随时读的状态位」，未就绪的位立即可查（如上超时报告）；信号量若要「查询有没有计数」则缺乏便捷只读接口，且信号量被消费（take）后即消耗，不适合「多个模块连续置位、且状态可随时复查」的场景。
- **适用边界**：信号量更擅长「资源数量/完成一次同步」，事件集更擅长「多条件组合判断」。本题「三个模块都完成了才启动」正适合事件集。

---

### 练习四：快递分拣中心（消息队列）— `package_demo`

**设计**

- 消息类型 `struct package_msg { id, region, weight }`，队列容量 5 条（`rt_mq_create(..., sizeof(pkg_msg), 5, ...)`）。
- 两个发件线程各产生 5 条（id 分别为 1001~1005、2001~2005，不重复）；区域分布经 `region_map` 控制为华东 4 / 华南 3 / 华北 3。
- 分拣线程 `rt_mq_recv()` 接收并打印，同时累计总件数、各区件数、总重量。
- 队列满时 `rt_mq_send` 返回非 `RT_EOK`，本实现不崩溃、`send_failed++` 并延时重发。
- 每个发件线程完成后发送一条结束消息（`id==0`，共 2 条），分拣线程收齐后打印统计并退出、释放队列资源。

**实测输出（节选）**

```text
package 1001 -> east, weight: 1020 g
package 2001 -> south, weight: 1020 g
package 1002 -> north, weight: 1040 g
package 2002 -> east, weight: 1040 g
...
total packages: 10
east: 4, south: 3, north: 3
total weight: 10300 g
send failed: 0
```

> 由于分拣线程优先级（15）高于发件线程（25），消息被及时取走，正常运行时队列很少被塞满，故 `send failed` 为 0。若想观察「队列满」路径，可将分拣线程优先级调到低于发件线程（或加大 `MSG_PER_SENDER`），此时 `send_failed` 会变为非零但系统不崩溃，正好验证健壮性设计。

---

## 三、工程集成说明

- 四个 `.c` 文件均放在应用的 `applications/` 目录由 RT-Thread Studio 编译即可。
- 每个练习通过 `MSH_CMD_EXPORT()` 导出独立命令：`parking_demo` / `ticket_demo` / `startup_demo` / `package_demo`（另附 `ticket_race` 供对比）。
- **资源安全与可重复执行**：
  - 每个练习在创建线程 / IPC 对象后均逐项检查返回结果，失败则回滚清理（删除已建线程、删除 IPC 对象）并返回错误码。
  - `parking_demo` / `startup_demo` / `package_demo` 通过静态 `running` 标志检测任务是否仍在运行，重复执行时拒绝，避免同名线程/IPC 对象残留导致异常。
  - `ticket_demo` 在售完后自动退出售票线程并清理互斥量，`startup_demo`、`package_demo` 在任务结束后主动 `detach/delete` 相应对象，均可再次启动。
- 未使用 `kernel_samples` 示例代码，四个练习均为按需求自行实现。

---

## 四、烧录与验证步骤（逐步操作）

### 4.1 前置准备

| 项目 | 要求 |
|------|------|
| 开发板 | 星火1号（STM32F407ZGT6），USB 线连接板载调试器（ST-Link 或 DAP） |
| 开发环境 | RT-Thread Studio（含 STM32F407 工程模板） |
| 串口终端 | 板载虚拟串口（如 COM3），波特率 **115200**，8N1，无流控 |
| 参考工程 | 第 2 天作业的 `thread_test.c` 已能在该工程编译烧录，本作业沿用同一工程 |

> 若尚未建立工程：RT-Thread Studio → 新建 RT-Thread 工程 → 选择芯片 `STM32F407ZGT6` → 完成创建。

### 4.2 第一步：把源码加入工程

1. 将 `parking.c`、`ticket.c`、`startup.c`、`package.c` 四个文件复制到工程目录：
   `工程根目录/applications/`
2. 在 RT-Thread Studio 左侧资源管理器中右键 `applications` 文件夹 → **刷新**（F5），确认四个 `.c` 文件出现在列表中。
3. 双击打开任意一个文件，确认无红色报错（如 `rt_mq_create` 等 API 未声明，见 4.6 排查）。

### 4.3 第二步：确认内核组件配置

在 `rtconfig.h` 或 RT-Thread Settings（`RT-Thread Studio → 工程右键 → RT-Thread Settings`）中确认以下宏已开启（默认开启）：

```c
#define RT_USING_SEMAPHORE     1   /* 信号量   */
#define RT_USING_MUTEX         1   /* 互斥量   */
#define RT_USING_EVENT         1   /* 事件集   */
#define RT_USING_MESSAGEQUEUE  1   /* 消息队列 */
#define RT_USING_MSH           1   /* MSH 命令行 */
#define RT_USING_HEAP          1   /* 动态内存（rt_*_create 需要） */
```

> 若 `RT_USING_HEAP` 未开启，`rt_sem_create / rt_mutex_create / rt_mq_create / rt_thread_create` 等动态创建接口将不可用，需在 Settings 中开启。

### 4.4 第三步：编译与烧录

1. 点击工具栏 **构建**（锤子图标）或 `Ctrl+B`，等待编译完成，底部控制台应显示 `Build Finished`（无 error）。
2. 点击 **下载**（烧录图标）将固件烧录到开发板，控制台显示烧录成功（如 `Download OK`）。
3. 若烧录失败，检查：调试器驱动是否安装、开发板是否上电、`Debugger` 配置中的接口（SWD）与速率是否正确。

### 4.5 第四步：连接串口终端进入 MSH

1. 打开串口工具（RT-Thread Studio 内置终端 / MobaXterm / Xshell / 串口助手），选择板载虚拟串口号，波特率 **115200**。
2. 复位开发板（按复位键或重新上电），终端应出现 RT-Thread 启动日志，最后出现 MSH 提示符：

```text
 \ | /
- RT -     Thread Operating System
 / | \     5.0.x ... 
 msh />
```

3. 输入 `help` 回车，应能看到本作业导出的命令：

```text
msh />help
...
parking_demo
package_demo
startup_demo
ticket_demo
ticket_race
...
```

### 4.6 第五步：逐个命令验证

**① 停车场 `parking_demo`**

```text
msh />parking_demo
parking: 3 lots, 5 cars, demo started
car1 entered, used spaces: 1
car2 entered, used spaces: 2
car3 entered, used spaces: 3
car4 waiting for a space, try again later
car1 left, used spaces: 2
car4 entered, used spaces: 3
...
```

验证点：
- 任意时刻 `used spaces` 最大为 **3**，绝不出现 4；
- 有车离开后，等待中的车（car4/car5）按 FIFO 依次进入；
- 再次输入 `parking_demo` 应提示 `already running`（防重入生效）。

**② 售票正确版 `ticket_demo`**

```text
msh />ticket_demo
=== with mutex ===
sale 20 tickets by 2 windows
window A sold ticket 20
window B sold ticket 19
...
all tickets sold
remaining tickets: 0
window A exits
window B exits
```

验证点：
- 恰好售出 20 张，票号 1~20 各出现一次（无重复）；
- 结尾 `remaining tickets: 0`；
- 两窗口线程自动退出，可再次执行 `ticket_demo`。

**③ 售票竞态版 `ticket_race`（对比实验）**

```text
msh />ticket_race
=== WITHOUT mutex (race condition) ===
window A sold ticket 20
window B sold ticket 20      <-- 重复票号（竞态）
...
```

验证点：
- 大概率出现**重复票号**或 `remaining` 异常跳变；
- 与 `ticket_demo` 对比，直观证明互斥量的必要性。

**④ 启动条件检查 `startup_demo`**

```text
msh />startup_demo
network ready
sensor ready
wait modules timeout, storage is not ready
storage ready
all modules are ready
business task started
```

验证点：
- 网络/传感器约 0.5~0.7s 就绪，存储 5s 才就绪；
- 业务线程第一次 3s 超时后打印 `storage is not ready`，第二次等待全部就绪后打印两行启动信息；
- 结束后可再次执行 `startup_demo`。

**⑤ 快递分拣 `package_demo`**

```text
msh />package_demo
package 1001 -> east, weight: 1020 g
package 2001 -> south, weight: 1020 g
...
total packages: 10
east: 4, south: 3, north: 3
total weight: 10300 g
send failed: 0
```

验证点：
- 共 10 条快递，id 无重复；
- 区域统计 east=4 / south=3 / north=3，总重量正确；
- `send failed` 为 0（正常优先级配置下队列不溢出）；
- 结束后可再次执行 `package_demo`。

### 4.7 第六步：可重复执行与资源释放验证

对每个命令连续执行两次，验证防重入与资源释放：

```text
msh />parking_demo
parking_demo is already running, ignore this command   <-- 第二次执行被拒绝
```

```text
msh />package_demo
...（第一次正常跑完）
msh />package_demo
...（第二次再次正常跑完，无 "already exist" / 断言错误）
```

> 若第二次执行出现 `assert failed` 或 `already exist`，说明上一次任务未正确清理同名对象，需检查 `running` 标志复位与 `detach/delete` 是否执行。

### 4.8 常见问题排查

| 现象 | 可能原因 | 处理 |
|------|----------|------|
| 编译报 `rt_sem_create` 未定义 | 未开启 `RT_USING_SEMAPHORE` / `RT_USING_HEAP` | 在 RT-Thread Settings 中开启对应组件后重新构建 |
| 烧录失败 | 调试器驱动 / 接口配置错误 | 检查驱动与 Debugger 配置（SWD、速率） |
| 串口无输出 | 波特率不对 / 串口号选错 | 确认 115200、8N1，重新插拔 USB |
| `help` 看不到命令 | 文件未加入编译 / 未刷新工程 | 确认 `.c` 在 `applications/` 且已刷新、重新构建 |
| 第二次执行报 `assert failed` | 上次任务未清理干净 | 检查 `running` 标志与资源释放逻辑 |
| `ticket_race` 未出现重复票号 | 竞态偶发，延时不够 | 增大 `RACE_DELAY_MS`（如 30→100）后重新编译再试 |