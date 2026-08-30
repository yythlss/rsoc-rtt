# 【DAY3】RT-Thread 线程间同步与通信（IPC）理论总结与实验报告

---

## 1. RT-Thread 线程间同步与通信（IPC）核心理论体系

在基于 **STM32F407（星火一号）** 的嵌入式多任务实时系统中，各个线程拥有独立的私有栈空间和执行上下文。为了协同完成复杂的工业控制和外设交互任务，必须借助 RT-Thread 提供的五大 **IPC（Inter-Thread Communication）** 原语：**信号量（Semaphore）**、**互斥量（Mutex）**、**事件集（Event）**、**消息邮箱（Mailbox）** 与 **消息队列（Message Queue）**。

### 1.1 IPC 底层机制：阻塞挂起与唤醒模型

IPC 的核心精髓在于以 **“阻塞 + 唤醒”** 的事件驱动机制替代裸机系统中低效消耗 CPU 的“忙等轮询（Busy-Waiting）”：
- **阻塞态（`RT_THREAD_SUSPEND`）**：当线程请求的 IPC 资源未就绪时，内核将该线程从就绪队列中移出，并插入到该 IPC 对象的等待队列中，立即让出 CPU 时间片；
- **唤醒调度**：当其他线程或中断服务函数（ISR）释放资源或触发事件时，内核将挂起线程唤醒并重新按优先级加入就绪队列，由调度器触发 `PendSV` 异常进行上下文切换。

```text
                     ┌────────────────────────────────────────┐
                     │          struct rt_ipc_object          │
                     │  (内嵌于 sem, mutex, event, mb, mq)    │
                     ├────────────────────────────────────────┤
                     │  struct rt_object      parent;         │ // 继承基类
                     │  rt_list_t             suspend_thread; │ // 挂起等待线程双向链表
                     └───────────────────┬────────────────────┘
                                         │
     ┌─────────────────┬─────────────────┼─────────────────┬─────────────────┐
     ▼                 ▼                 ▼                 ▼                 ▼
  rt_sem            rt_mutex          rt_event           rt_mb             rt_mq
(计数值 value)    (持有者 owner,    (32位位图 set)   (4字节邮箱数组)   (不定长消息链表)
                  递归计数 hold,
                  优先级继承)
```

---

### 1.2 IPC 机制横向对比与选型指南

| IPC 机制 | 通信/同步模型 | 数据载荷 | 是否支持所有权 | 适用典型场景 |
| :--- | :--- | :--- | :--- | :--- |
| **信号量 (Semaphore)** | 同步 / 资源配额 | 无（仅维护计数值 `value`） | ❌ 无所有权，可跨线程/中断释放 | 有限共享资源池管理（如车位/缓冲区）、中断到线程的同步通知 |
| **互斥量 (Mutex)** | 临界区排他互斥 | 无（严格绑定持有者线程） | ✅ 有所有权（谁加锁谁解锁） | 保护共享全局变量、外设硬件独占访问，具备**优先级继承**防反转 |
| **事件集 (Event)** | 多条件逻辑同步 | 32 位整型位图掩码 | ❌ 无所有权 | 多外设/模块就绪同步（`AND`/`OR` 组合）、一对多广播唤醒 |
| **消息邮箱 (Mailbox)** | 异步定长数据传递 | 4 字节（通常传递指针或数值） | ❌ 无所有权 | **ISR 向线程传递数据指针**、轻量级状态通知（开销极低） |
| **消息队列 (Message Queue)** | 异步不定长数据传递 | 结构体/缓冲区（**值拷贝**） | ❌ 无所有权 | 结构化报文传递、生产者-消费者模型、**紧急消息插队（`rt_mq_urgent`）** |

---

## 2. 四大核心练习详细设计与实现剖析

---

### 练习一：停车场车位管理（计数信号量 `parking_demo`）

#### 2.1 业务场景与控制逻辑
- **场景描述**：停车场仅有 3 个车位（有限共享资源），有 5 辆车并发尝试入场。
- **信号量初始化**：创建初始计数值为 3 的计数信号量 `g_park_sem`（`RT_IPC_FLAG_FIFO`）。
- **申请车位与超时控制**：每辆车到达后调用 `rt_sem_take(g_park_sem, rt_tick_from_millisecond(2000))`：
  - 若在 2 秒内成功获取，占用车位并打印车辆编号及当前已用车位数；
  - 若 2 秒内未获取到（返回 `-RT_ETIMEOUT`），打印等待超时信息，稍后通过阻塞模式再次重试申请；
- **资源释放与唤醒**：车辆停留 1.5s ~ 2.3s 后离开，归还车位并调用 `rt_sem_release(g_park_sem)` 唤醒排队等待的后续车辆。
- **统计与自动清理**：通过内部互斥锁 `g_park_mutex` 保护 `g_used_spaces` 与完成计数 `g_cars_finished`，当全部 5 辆车离场后自动销毁信号量并复位运行标志。

#### 2.2 核心代码实现
```c
/* 尝试申请车位，超时时间 2 秒 (2000 ms) */
result = rt_sem_take(g_park_sem, rt_tick_from_millisecond(2000));
if (result == -RT_ETIMEOUT)
{
    rt_kprintf("car%d waiting for a space (timeout 2s), retry later...\n", (int)car_id);
    rt_thread_mdelay(800);
    result = rt_sem_take(g_park_sem, RT_WAITING_FOREVER);
}

if (result == RT_EOK)
{
    rt_mutex_take(g_park_mutex, RT_WAITING_FOREVER);
    g_used_spaces++;
    rt_kprintf("car%d entered, used spaces: %d\n", (int)car_id, g_used_spaces);
    rt_mutex_release(g_park_mutex);

    rt_thread_mdelay(1500 + (car_id * 200)); /* 模拟在停车场内停留 */

    rt_mutex_take(g_park_mutex, RT_WAITING_FOREVER);
    g_used_spaces--;
    rt_kprintf("car%d left, remaining spaces: %d\n", (int)car_id, TOTAL_SPACES - g_used_spaces);
    rt_mutex_release(g_park_mutex);

    rt_sem_release(g_park_sem); /* 归还信号量资源 */
}
```

#### 2.3 思考题深入解答
> **思考题：为什么这里使用计数信号量，而不是互斥量？**
1. **资源配额模型不同**：互斥量是**二值互斥**的（只有 0 和 1），仅用于单一临界资源的排他性独占访问；而计数信号量内部维护一个非负整数计数值（本例初值为 3），天然支持对多配额同类共享资源的并发分配与回收。
2. **所有权（Ownership）差异**：互斥量具有强烈的“所有权”属性，必须“谁上锁、谁解锁”；而信号量不具备所有权概念，更侧重于资源计数与就绪通知。
3. **避免优先级继承机制的不当开销**：互斥量内置优先级继承算法以防止优先级反转，若用于多资源共享池，不仅语义不符，还会导致多线程并发时的优先级反转计算开销与紊乱。

---

### 练习二：多窗口售票系统（互斥量与竞态对比 `ticket_demo`）

#### 2.1 业务场景与竞态隐患
系统初始有 20 张车票，窗口 A 和窗口 B 两个线程并发售票。售票业务包含四个连续动作：
1. 检查余票是否 $> 0$；
2. 模拟出票业务准备与延时；
3. 将剩余票数减 1；
4. 记录并输出售出的票号。

#### 2.2 对比实验设计（Safe vs Unsafe）
支持通过 MSH 命令动态切换对比模式：
- `ticket_demo`：启用互斥量 `rt_mutex_take` 完整加锁保护；
- `ticket_demo unsafe`：关闭互斥量加锁，在“读取余票”与“扣减余票”之间插入 `rt_thread_mdelay(20)` 放大竞态窗口。

```text
【无锁模式 Check-then-Act 竞态时序漏洞】：
窗口 A: 读到余票=20 ─────────(延时打断)─────────> 执行 count-- (变为 19), 售出 20 号
窗口 B:         读到余票=20 ─────────> 执行 count-- (变为 18), 售出 20 号 (重复售票!)
...
当余票剩 1 张时，两窗口同时通过 count > 0 检查，导致最后余票被扣减至 -1（超卖故障）！
```

#### 2.3 核心代码实现
```c
/* 【安全模式】：使用互斥量保护整个售票复合操作闭环 */
rt_mutex_take(g_ticket_mutex, RT_WAITING_FOREVER);
if (g_ticket_count > 0)
{
    int current_ticket = g_ticket_count;
    rt_thread_mdelay(15); /* 模拟出票业务延时 */
    g_ticket_count--;
    rt_kprintf("window %s sold ticket %02d (remaining: %d)\n", win_name, current_ticket, g_ticket_count);
    rt_mutex_release(g_ticket_mutex);

    rt_thread_mdelay(25); /* 让出 CPU 调度机会 */
}
else
{
    rt_mutex_release(g_ticket_mutex);
    break;
}
```

#### 2.4 思考题深入解答
> **思考题：为什么只给 `ticket_count--` 加锁仍可能不安全？**
1. **Check-then-Act（先检查后执行）竞态条件**：售票逻辑是“条件判断 + 数据读取 + 数据扣减 + 票据打印”构成的**复合业务闭环**。如果仅对 `ticket_count--` 加锁：
   - 当 `ticket_count == 1` 时，窗口 A 检查 `ticket_count > 0` 成立；
   - 发生线程切换，窗口 B 检查 `ticket_count > 0` 同样成立；
   - 随后窗口 A 与窗口 B 先后进入临界区执行 `ticket_count--`，最终导致第 1 张票被售出两次，`ticket_count` 变为 `-1`（超卖严重故障）。
2. **数据与凭据输出脱节**：若不在同一临界区内记录售出票号并打印，可能出现窗口 A 扣减了第 20 张票，但在输出前被窗口 B 抢占，导致打印出的票号与实际扣减顺序颠倒或出现重复。**因此，必须将检查、扣减和票据生成的整个过程纳入同一个互斥临界区！**

---

### 练习三：系统启动条件检查（事件集多事件同步 `startup_demo`）

#### 2.1 业务场景与多位组合逻辑
设备启动前需完成三个独立模块初始化：
- 网络模块（`EVENT_NET_READY`, `1U << 0`）：延时 1000ms 发送；
- 传感器模块（`EVENT_SENSOR_READY`, `1U << 1`）：延时 2000ms 发送；
- 存储模块（`EVENT_STORAGE_READY`, `1U << 2`）：延时 5000ms 发送（模拟长耗时）。

主业务线程需等待三者全部就绪（`RT_EVENT_FLAG_AND` 逻辑），并完成超时检测机制：
- 主业务线程设置 **3 秒等待超时**；
- 3 秒超时后，检查未就绪事件（检测到 `storage` 尚未完成），打印 `wait modules timeout, [ storage ] is not ready`；
- 随后再次阻塞等待，直到存储模块完成初始化，打印全模块就绪与业务启动日志。

#### 2.2 核心代码实现
```c
/* 第一次等待：3 秒超时，AND 模式，不清除事件位 (保留已到达的事件) */
result = rt_event_recv(g_startup_event,
                       EVENT_ALL_READY,
                       RT_EVENT_FLAG_AND,
                       rt_tick_from_millisecond(3000),
                       &recved);

if (result == -RT_ETIMEOUT)
{
    /* 查询当前事件集位图，精确诊断未完成模块 */
    rt_uint32_t current_set = g_startup_event->set;
    rt_kprintf("wait modules timeout, [");
    if (!(current_set & EVENT_NET_READY))     rt_kprintf(" network");
    if (!(current_set & EVENT_SENSOR_READY))  rt_kprintf(" sensor");
    if (!(current_set & EVENT_STORAGE_READY)) rt_kprintf(" storage");
    rt_kprintf(" ] is not ready\n");

    /* 第二次等待：永久阻塞等待所有模块全部就绪，并在唤醒后自动清除 */
    result = rt_event_recv(g_startup_event,
                           EVENT_ALL_READY,
                           RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                           RT_WAITING_FOREVER,
                           &recved);
}

if (result == RT_EOK)
{
    rt_kprintf("all modules are ready\n");
    rt_kprintf("business task started\n");
}
```

#### 2.3 思考题深入解答
> **思考题：如果用三个独立信号量实现这个功能，与使用事件集相比有什么区别？**
1. **内核内存与对象开销**：使用信号量需要创建 3 个独立的 `struct rt_semaphore` 内核对象；而事件集仅需 1 个 `struct rt_event` 对象，通过单个 32 位整型位图即可管理多达 32 个独立事件。
2. **逻辑表达能力**：信号量只能串行等待（`take(sem1) -> take(sem2) -> take(sem3)`），无法实现真正的多事件原子复合逻辑；事件集原生支持 `AND`（全部发生）与 `OR`（任意发生）的逻辑运算。
3. **超时控制与状态诊断**：串行 `take` 信号量很难统一管理整体超时（需动态累减超时时间），且发生超时时无法获知其他尚未等待的信号量状态；事件集支持单次调用等待全局复合条件，并在超时后可通过位运算清晰诊断出哪些模块未就绪。
4. **广播机制（一对多）**：一个信号量 `release` 只能唤醒一个等待线程；而一个事件位 `send` 可以同时唤醒多个订阅了该事件的线程。

---

### 练习四：快递分拣中心（消息队列 `package_demo`）

#### 2.1 业务场景与值拷贝通信
- **消息结构体**：
  ```c
  struct package_msg {
      rt_uint32_t id;      /* 快递单号，0 表示结束协议帧 */
      rt_uint8_t  region;  /* 1: 华东 (east), 2: 华南 (south), 3: 华北 (north) */
      rt_uint16_t weight;  /* 重量 (g) */
  };
  ```
- **容量与生产者-消费者模型**：
  - 创建容量为 5 条消息的消息队列 `g_pkg_mq`；
  - 两个发件线程（`sender1` 产生编号 1001~1005；`sender2` 产生编号 2001~2005，共 10 件快递）；
  - 发件线程在队列满时安全捕获并记录 `g_send_failed_count`，不发生异常崩溃；
  - 两个发件线程均完成后，向队列发送 `id = 0` 的结束标识消息；
  - 分拣线程 `sorter` 循环接收、分类并汇总统计总件数、分区域件数及总重量，收到终止消息后输出完整报表并退出。

#### 2.2 核心代码实现
```c
/* 发送端：非阻塞试探发送，队列满时记录失败并不崩溃 */
rt_err_t res = rt_mq_send(g_pkg_mq, &pkgs[i], sizeof(struct package_msg));
if (res != RT_EOK)
{
    g_send_failed_count++;
    rt_thread_mdelay(100);
    rt_mq_send_wait(g_pkg_mq, &pkgs[i], sizeof(struct package_msg), RT_WAITING_FOREVER);
}

/* 接收分拣端：循环提取与分类统计 */
while (1)
{
    rt_err_t result = rt_mq_recv(g_pkg_mq, &msg, sizeof(struct package_msg), RT_WAITING_FOREVER);
    if (result == RT_EOK)
    {
        if (msg.id == 0) break; /* 收到结束协议信号 */

        rt_kprintf("package %d -> %s, weight: %d g\n", msg.id, get_region_name(msg.region), msg.weight);
        total_packages++;
        total_weight += msg.weight;
        if (msg.region == REGION_EAST)       east_count++;
        else if (msg.region == REGION_SOUTH) south_count++;
        else if (msg.region == REGION_NORTH) north_count++;
    }
}
```

---

## 3. MSH 命令行运行指令与完整日志分析

每个练习均导出了独立的 MSH 命令，并具备防重入检测与生命周期回收：

```bash
msh /> parking_demo       # 练习一：停车场车位管理
msh /> ticket_demo         # 练习二：多窗口售票（安全互斥锁模式）
msh /> ticket_demo unsafe  # 练习二：多窗口售票（无锁对比实验）
msh /> startup_demo        # 练习三：系统启动条件检查
msh /> package_demo        # 练习四：快递分拣中心
```

---

### 3.1 实际测试日志

#### 📌 1. `parking_demo` 输出
```text
msh />parking_demo
=== Starting Parking Demo (Capacity: 3, Cars: 5) ===
[Car 1] arriving at parking lot...
car1 entered, used spaces: 1
[Car 2] arriving at parking lot...
car2 entered, used spaces: 2
[Car 3] arriving at parking lot...
car3 entered, used spaces: 3
[Car 4] arriving at parking lot...
[Car 5] arriving at parking lot...
car4 waiting for a space (timeout 2s), retry later...
car5 waiting for a space (timeout 2s), retry later...
car1 left, remaining spaces: 1
car4 entered, used spaces: 3
car2 left, remaining spaces: 1
car5 entered, used spaces: 3
car3 left, remaining spaces: 1
car4 left, remaining spaces: 2
car5 left, remaining spaces: 3
>>> [Parking Demo] All 5 cars completed parking! <<<
```

#### 📌 2. `ticket_demo` 输出（安全互斥模式 vs 无锁对比）
```text
msh />ticket_demo
=== Starting Ticket Demo [SAFE MODE: WITH MUTEX] ===
window A sold ticket 20 (remaining: 19)
window B sold ticket 19 (remaining: 18)
window A sold ticket 18 (remaining: 17)
...
window B sold ticket 02 (remaining: 1)
window A sold ticket 01 (remaining: 0)
----------------------------------------
all tickets sold
remaining tickets: 0
----------------------------------------
```

#### 📌 3. `startup_demo` 输出
```text
msh />startup_demo
=== Starting System Startup Check Demo ===
network ready
sensor ready
wait modules timeout, [ storage ] is not ready
storage ready
all modules are ready
business task started
```

#### 📌 4. `package_demo` 输出
```text
msh />package_demo
=== Starting Package Sorting Center Demo ===
package 1001 -> east, weight: 1200 g
package 2001 -> south, weight: 1100 g
package 1002 -> north, weight: 960 g
package 2002 -> east, weight: 780 g
package 1003 -> south, weight: 850 g
package 2003 -> north, weight: 2300 g
package 1004 -> east, weight: 1500 g
package 2004 -> north, weight: 650 g
package 1005 -> south, weight: 620 g
package 2005 -> east, weight: 1490 g
----------------------------------------
total packages: 10
east: 4, south: 3, north: 3
total weight: 11450 g
send failed: 0
----------------------------------------
```

---

## 4. 实验总结与核心收获

1. **同步 vs 互斥的本质区别**：互斥用于保护临界数据（排他性、所有权、优先级继承）；同步用于协调多任务时序关系（无所有权、事件驱动）。
2. **锁粒度与 Check-then-Act 原子性**：加锁必须完整覆盖“条件判断 $\rightarrow$ 数据读取 $\rightarrow$ 扣减修改 $\rightarrow$ 凭据输出”全流程，锁粒度过小会导致竞态失效（如仅对 `count--` 加锁），过大则影响并发实时性。
3. **事件集的复合表达力**：32 位位掩码在单一内核对象内实现了 `AND` / `OR` 复合判定，比多信号量串行轮询更节省 RAM，且易于统一超时管理与故障诊断。
4. **消息队列与值拷贝**：消息队列通过内核缓冲机制解耦了生产者与消费者，支持 `rt_mq_urgent` 紧急插队，是复杂嵌入式系统中跨线程传递结构化数据的标准方案。
