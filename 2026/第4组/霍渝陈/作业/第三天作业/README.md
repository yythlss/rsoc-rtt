# 第三天作业：线程间同步与通信

> 目标平台：STM32F407 星火号（stm32f407-rtspark）
> 内核版本：RT-Thread 4.1.1（Cortex-M4）
> 开发环境：RT-Thread Studio 2.3.1 + 板载 ST-LINK 下载，串口 COM7/115200

---

## 一、理论总结

### 1. 为什么要线程间同步与通信

多线程系统中，多个线程会**共享资源**（全局变量、外设、缓冲区）或**协作完成任务**。如果没有同步机制，就会出现数据错乱（比如两个窗口同时卖出同一张票）；如果线程之间需要传递数据，就需要通信机制。RT-Thread 提供了四种经典的 IPC（Inter-Process Communication，进程间通信，这里指线程间）机制：**信号量、互斥量、事件集、消息队列**。

### 2. 四种机制对比

| 机制 | 本质 | 核心操作 | 适合场景 | 本次练习 |
|---|---|---|---|---|
| 计数信号量 | 计数器（资源数） | `take` 减 1 / `release` 加 1 | 多个同类资源的分配与回收 | 练习一：停车场 |
| 互斥量 | 0/1 锁（有所有权） | `take` / `release` | 保护临界区，共享数据互斥访问 | 练习二：售票 |
| 事件集 | 32 个事件位 | `send` 置位 / `recv` 等待 | 多个条件"全部/任一"满足后再行动 | 练习三：启动检查 |
| 消息队列 | 定长消息 FIFO | `send` 发消息 / `recv` 收消息 | 线程间传递数据（生产者-消费者） | 练习四：快递分拣 |

### 3. 信号量 vs 互斥量（重点）

- **信号量**：值可以大于 1，用来"数"有多少个可用资源（3 个车位 → 初始值 3）。它**没有所有权**概念，任何线程都可以 release。适合**资源计数与分配**。
- **互斥量**：值只有 0/1，是"锁"。它有**所有权**（谁 take 谁 release），并且带**优先级继承**机制——低优先级线程持有锁时，会临时提升优先级，避免高优先级线程被"饿死"（优先级反转）。适合**保护临界区**。
- 一句话记忆：**互斥量是锁，信号量是计数器**。

### 4. 事件集 vs 信号量（重点）

- 信号量等待的是"资源被释放"，`release` 一次只会唤醒**一个**等待的 `take`，而且资源计数会被消耗。
- 事件集等待的是"条件满足"，事件位**不会被消耗**（`send` 一次，所有等待该位的线程都能看到）；还支持 **AND 模式**（所有位到齐才唤醒）和 **OR 模式**（任一到位即唤醒）。
- 需要"三个模块**全部**就绪才能启动"这种组合条件时，事件集的 AND 模式天然匹配；用三个独立信号量则需要 take 三次，无法表达"同时满足"的语义。

### 5. 消息队列的特点

- 传递的是**数据本身**（内存拷贝），不是指针，生产者和消费者彻底解耦。
- 容量固定：队列满时 `send` 失败（或阻塞），空时 `recv` 阻塞等待。
- 适合"生产者-消费者"模型，天然带缓冲，能平滑双方的速率差。

---

## 二、实验报告

### 练习一：停车场车位管理（计数信号量）

**需求**：3 个车位、5 辆车；每辆车先申请车位才能进入；2 秒内申请不到打印等待信息稍后重试；任意时刻场内车辆 ≤ 3。

**实现思路**：
1. 信号量初始值 3，表示 3 个空闲车位；
2. 5 个车辆线程 `rt_sem_take(sem, 2秒)` 申请车位，成功 → 打印进入信息 → 停车 3 秒 → `rt_sem_release` 释放；
3. 超时 → 打印 `waiting for a space`，睡 1 秒后重新申请（循环）；
4. 用全局变量 `used_spaces` 记录当前占用数（在 take 成功后 +1，此时持有信号量，计数安全）；
5. 控制线程负责创建/回收信号量与线程，任务结束后删除信号量，命令可重复执行。

**关键代码**：

```c
/* 车辆线程：申请车位 → 停车 → 离开 */
if (rt_sem_take(parking_sem, WAIT_TIMEOUT) == RT_EOK)   /* 2 秒超时 */
{
    used_spaces++;
    rt_kprintf("car%d entered, used spaces: %d\n", id, used_spaces);
    rt_thread_mdelay(CAR_STAY_TIME);                    /* 停车 3 秒 */
    used_spaces--;
    rt_kprintf("car%d left\n", id);
    rt_sem_release(parking_sem);
    break;
}
else
{
    rt_kprintf("car%d waiting for a space\n", id);
    rt_thread_mdelay(RETRY_DELAY);                      /* 稍后再申请 */
}
```

**预期运行结果**：

```text
car1 entered, used spaces: 1
car2 entered, used spaces: 2
car3 entered, used spaces: 3
car4 waiting for a space
car5 waiting for a space
car1 left
car4 entered, used spaces: 3
car2 left
car5 entered, used spaces: 3
car3 left
parking: all cars done, resources released
```

（实际运行顺序可能因调度略有不同，但"任意时刻 used spaces ≤ 3"和"超时重试"两个特性必然体现。）

**思考题：为什么使用计数信号量，而不是互斥量？**

车位是 **3 个同类的有限资源**，需要"数出还剩几个"。计数信号量初始值 = 3 就表达了这个资源池，允许多个线程同时占用（最多 3 个）。而互斥量只有 0/1 两种状态，只能锁"一个"资源；它的所有权和优先级继承机制是为"保护临界区"设计的，不适合"分配多个资源"的场景。若用互斥量模拟，必须创建 3 个互斥量并管理它们的分配，逻辑复杂且无法表达"还剩几个车位"。

---

### 练习二：多窗口售票系统（互斥量）

**需求**：两个窗口卖同一批 20 张票；每次只能一个窗口修改余票；最终 20 张全卖出、票号不重复、余票不为负。

**实现思路**：
1. 全局变量 `ticket_count` 保存余票（初始 20）；
2. 互斥量保护**完整售票过程**：检查余票 → 读票号 → 减票 → 打印，全程在锁内；
3. 两个窗口线程卖完（余票 0）自动退出；
4. 提供两个命令做对比实验：`ticket_demo`（加锁，正确版）和 `ticket_demo_nolock`（不加锁 + 读写之间插入 2ms 延时，演示竞态）。

**关键代码**：

```c
rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);   /* 进入临界区 */
if (ticket_count <= 0)                              /* 没票了，退出 */
{
    rt_mutex_release(ticket_mutex);
    break;
}
int n = ticket_count;
rt_thread_mdelay(1);                                /* 模拟售票耗时（锁内，安全） */
ticket_count = n - 1;
rt_kprintf("window %c sold ticket %d\n", name, n);
rt_mutex_release(ticket_mutex);                     /* 退出临界区 */
```

**预期运行结果（加锁版）**：

```text
window A sold ticket 20
window B sold ticket 19
window B sold ticket 18
...
all tickets sold
remaining tickets: 0
```

**对比实验结果（无锁版）**：不加锁时，两个窗口会**同时读到相同的余票**，卖出重复票号（例如 `window A sold ticket 20` 和 `window B sold ticket 20` 同时出现），实际卖出的票数会少于 20，演示出明显的竞态错误；加锁后所有票号唯一、总数恰好 20。

**思考题：为什么只给 `ticket_count--` 加锁仍可能不安全？**

"售票"是一连串操作：**检查余票 > 0 → 读出票号 → 修改余票**。如果只锁减票那一行，两个线程可能同时通过"余票 > 0"的检查（都读到 20），然后都卖出 20 号票 → **重复售票**；极端情况下还会**超卖成负数**。必须把"检查 + 读 + 改 + 记录"作为一个**完整的临界区**整体加锁，才能保证判断和修改之间不被打断。

---

### 练习三：系统启动条件检查（事件集）

**需求**：网络/传感器/存储三个模块独立初始化，全部就绪后主业务线程才能启动；先做 3 秒超时测试（存储模块故意设 5 秒），超时打印未就绪模块，再继续等待。

**实现思路**：
1. 事件集 + 三个事件位：`NET=1<<0`、`SENSOR=1<<1`、`STORAGE=1<<2`；
2. 三个初始化线程延时 1s/2s/5s 后 `rt_event_send` 各自就绪事件；
3. 主业务线程 `rt_event_recv` 用 **AND + CLEAR** 模式等待三个位全部到齐；
4. 第一轮只等 3 秒 → 必然超时（存储要 5 秒）→ 通过 `recved` 参数（已到位的位）判断并打印"哪个模块没就绪"；
5. 第二轮不限时等待，三个位到齐后打印 `all modules are ready` 并启动业务。

**关键代码**：

```c
/* 第一轮：AND 模式等全部事件，最多 3 秒 */
ret = rt_event_recv(startup_evt, EVENT_ALL_READY,
                    RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                    BUSINESS_WAIT_TIME, &recved);
if (ret == RT_ETIMEOUT)
{
    if (!(recved & EVENT_STORAGE_READY))
        rt_kprintf("wait modules timeout, storage is not ready\n");
}

/* 第二轮：不限时，继续等直到三个事件全部到达 */
rt_event_recv(startup_evt, EVENT_ALL_READY,
              RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
              RT_WAITING_FOREVER, &recved);
rt_kprintf("all modules are ready\n");
rt_kprintf("business task started\n");
```

**预期运行结果**：

```text
network ready
sensor ready
wait modules timeout, storage is not ready
storage ready
all modules are ready
business task started
```

**思考题：如果用三个独立信号量实现这个功能，与使用事件集相比有什么区别？**

1. **等待方式**：三个信号量只能依次 take 三次，无法表达"三个条件**同时**满足"的 AND 语义；事件集一次调用即可等待组合条件。
2. **资源消耗**：信号量被 take 后计数会消耗，一个 `release` 只能唤醒一个 `take`；事件位**不消耗**，`send` 一次所有等待线程都能看到——如果以后有多个业务线程都要等"全部就绪"，事件集依然有效，信号量则不行。
3. **诊断能力**：事件集超时后能通过 `recved` 参数知道"已经到齐了哪些位"，方便打印缺哪个模块（本练习的超时诊断正是靠这一点）；信号量做不到。

---

### 练习四：快递分拣中心（消息队列）

**需求**：两个收件线程各产生 5 条快递消息（编号不重复），分拣线程从容量为 5 的消息队列中取出并按区域分类统计；队列满时发送线程不得崩溃，需记录失败次数；结束后打印统计结果。

**实现思路**：
1. 消息队列容量 5，消息结构 `struct package_msg`（id/region/weight）；
2. 两个发送线程编号前缀不同（1001~1005 / 2001~2005），天然不重复；区域轮流 华东/华南/华北；
3. 队列满时 `rt_mq_send` 返回 `-RT_EFULL` → 记录 `send_failed` 并延时重试，**不崩溃**；
4. 每个发送线程发完 5 条后，再发一条"结束消息"（`region=0` 哨兵）；
5. 分拣线程（高优先级）收到 2 条结束消息后，打印统计结果并退出。

**关键代码**：

```c
/* 发送一条消息：队列满就记录失败并延时重试（不崩溃） */
static void send_package(const struct package_msg *msg)
{
    while (rt_mq_send(pkg_mq, msg, sizeof(*msg)) != RT_EOK)
    {
        send_failed++;              /* 队列满，记一次失败 */
        rt_thread_mdelay(50);       /* 等分拣线程腾出空间 */
    }
}

/* 分拣线程：循环收消息，收到结束消息（region=0）则计数 */
while (end_count < SENDER_COUNT)
{
    rt_mq_recv(pkg_mq, &msg, sizeof(msg), RT_WAITING_FOREVER);
    if (msg.region == MSG_END) { end_count++; continue; }
    /* 打印 + 统计区域和重量 */
}
```

**预期运行结果**：

```text
package 1001 -> east, weight: 500 g
package 2001 -> south, weight: 500 g
package 1002 -> north, weight: 637 g
...
total packages: 10
east: 4, south: 3, north: 3
total weight: 11410 g
send failed: N
```

（`send failed` 统计的是"队列满被拒后重试"的次数，并发运行时 N > 0 是正常的，恰好证明"满队列不崩溃、消息不丢失"这一功能在工作；10 条消息最终全部送达，`total packages` 恒为 10。）

---

## 三、实验总结与踩坑

1. **IPC 对象创建必须检查返回值**：信号量/互斥量/事件集/消息队列/线程创建失败时返回 `RT_NULL`，不检查会导致空指针崩溃。
2. **MSH 命令描述字符串不能带逗号**：`MSH_CMD_EXPORT` 宏按逗号分隔参数，描述里出现逗号会被当成第 3 个参数导致编译错误（改用空格分隔描述）。
3. **MSH 命令可重复执行**：通过"任务运行标志"拒绝重复启动 + 任务结束后主动删除 IPC 对象并复位状态，避免同名对象残留导致 `rt_sem_create` 等返回失败。
4. **临界区要锁"完整过程"**：判断 + 修改必须作为一个整体加锁（练习二对比实验直观展示了不锁的后果）。
5. **事件集先打印再发事件**：初始化线程先 `rt_kprintf` 再 `rt_event_send`，避免高优先级业务线程抢占导致输出顺序错乱。

### 选型心得

- 要"数资源" → 信号量；要"锁数据" → 互斥量；要"等条件组合" → 事件集；要"传数据" → 消息队列。四种机制配合使用可以解决绝大多数线程协作问题。
