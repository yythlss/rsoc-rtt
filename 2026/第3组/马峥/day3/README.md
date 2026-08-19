# RT-Thread 线程间同步与通信 — DAY3 作业

- **目标平台**：STM32F407 星火号（`stm32f407-rtspark`）
- **提交仓库**：`https://gitee.com/rtthread/rsoc-rtt`
- **学生**：马峥 | 第3组

---

## 一、理论知识梳理

### 1.1 IPC 概述

RT-Thread 提供丰富的线程间通信与同步机制，简称 IPC（Inter-Process Communication / Inter-Thread Communication）。本章重点掌握四种核心 IPC：

| IPC 机制 | 本质 | 适用场景 |
|----------|------|----------|
| **信号量（Semaphore）** | 计数器，控制对共享资源的访问数量 | 资源计数（停车场、多生产者/消费者） |
| **互斥量（Mutex）** | 二值锁，解决优先级反转问题 | 保护临界区，防止数据竞争 |
| **事件集（Event）** | 多位标志组合，等待指定事件组合 | 多条件同步（AND/OR 模式） |
| **消息队列（Message Queue）** | FIFO 消息缓存区 | 异步数据传递，线程解耦 |

### 1.2 信号量（Semaphore）

**原理**：内部维护一个计数器 `value`，`take` 使计数器减 1（阻塞或返回错误），
`release` 使计数器加 1（唤醒等待线程）。

- **二值信号量**：计数器只能是 0 或 1，作用类似互斥量（但不支持优先级继承）。
- **计数信号量**：计数器允许多值，适合"资源池"场景（如 N 个车位、N 个缓冲区）。

```c
/* 创建计数信号量（初始值=3） */
rt_sem_t sem = rt_sem_create("sem", 3, RT_IPC_FLAG_FIFO);

/* 获取信号量（阻塞等待） */
rt_sem_take(sem, RT_WAITING_FOREVER);

/* 获取信号量（超时等待） */
rt_err_t err = rt_sem_take(sem, rt_tick_from_millisecond(2000));

/* 释放信号量 */
rt_sem_release(sem);

/* 删除信号量 */
rt_sem_delete(sem);
```

**与互斥量的区别**：互斥量是二值的，且支持优先级继承防止优先级反转；
计数信号量可以是任意非负整数，适合资源计数场景。

### 1.3 互斥量（Mutex）

**原理**：同一时刻只允许一个线程持有锁，持有期间其他线程 `take` 会阻塞。

**关键特性**：
- **优先级继承**：当高优先级线程等待低优先级线程持有的互斥量时，
  持有互斥量的低优先级线程临时提升优先级，避免中等优先级线程"插队"导致
  高优先级线程长时间等待（优先级反转问题的解决方案）。
- **不可递归持有**：同一线程不能对已持有的互斥量再次 `take`（会死锁）。

```c
rt_mutex_t mutex = rt_mutex_create("mutex", RT_IPC_FLAG_FIFO);

rt_mutex_take(mutex, RT_WAITING_FOREVER);   /* 加锁 */
// ... 临界区代码 ...
rt_mutex_release(mutex);                      /* 解锁 */

rt_mutex_delete(mutex);
```

### 1.4 事件集（Event）

**原理**：每个事件用一个 bit 表示，线程可以等待多个事件位同时到达（AND 模式）
或任意一个到达（OR 模式）。

```c
/* 定义事件位 */
#define EVENT_NET_READY     (1U << 0)
#define EVENT_SENSOR_READY  (1U << 1)
#define EVENT_STORAGE_READY (1U << 2)

/* 创建事件集 */
rt_event_t evt = rt_event_create("evt", RT_IPC_FLAG_FIFO);

/* 发送事件（设置对应 bit） */
rt_event_send(evt, EVENT_NET_READY);

/* 接收事件（AND 模式：等待所有指定位都就绪） */
rt_event_recv(evt,
              EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY,
              RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
              timeout,
              &recved);
```

**与三个独立信号量的区别**：
- 独立信号量：需要维护 3 个信号量对象，每个 `take` 单独判断，逻辑复杂；
- 事件集：一个对象管理多位标志，`AND` 模式一次等待所有条件，原生支持组合判断。

### 1.5 消息队列（Message Queue）

**原理**：线程通过 FIFO 消息队列传递数据，发送线程 `send` 将数据放入队列尾部，
接收线程 `recv` 从队列头部取出数据。队列满时 `send` 可选阻塞/非阻塞，队列空时
`recv` 阻塞等待。

```c
struct package_msg { int id; int region; int weight; };

/* 创建消息队列（每条消息大小 sizeof(struct)，最多 5 条） */
rt_mq_t mq = rt_mq_create("mq", sizeof(struct package_msg), 5, RT_IPC_FLAG_FIFO);

/* 发送消息（非阻塞，队列满则立即返回错误） */
rt_mq_send(mq, &msg, sizeof(msg));

/* 接收消息（阻塞） */
rt_mq_recv(mq, &recv_msg, sizeof(recv_msg), RT_WAITING_FOREVER);

/* 删除 */
rt_mq_delete(mq);
```

---

## 二、练习一：停车场车位管理

### 2.1 设计说明

- **IPC 机制**：计数信号量 `sem_parking`，初始值 = 3
- **线程**：5 个车辆线程（`car1`~`car5`），优先级 15
- **逻辑**：`rt_sem_take(sem, 2000ms)` 申请车位 → 停留 1~3 秒 → `rt_sem_release()` 释放

### 2.2 源码文件

`ipc_parking.c`

### 2.3 串口输出示例

```
[parking_demo] sem_parking created, spaces=3
[parking_demo] all 5 car threads started
car1 arrived
car1 entered, used spaces: 1
car2 arrived
car2 entered, used spaces: 2
car3 arrived
car3 entered, used spaces: 3
car4 arrived
car4 waiting for a space (timeout), will retry
car5 arrived
car5 waiting for a space (timeout), will retry
car2 left
car4 entered, used spaces: 3
car1 left
...
```

### 2.4 思考题解答

**Q：为什么使用计数信号量，而不是互斥量？**

A：互斥量只能表示"有/无"两种状态（二值），无法表达"3个可用车位"的资源计数场景。
计数信号量的计数器可以取任意非负整数，正好对应"剩余车位数"，每次 `take` 使计数减 1，
每次 `release` 使计数加 1，这是互斥量无法实现的功能。

---

## 三、练习二：多窗口售票系统

### 3.1 设计说明

- **IPC 机制**：互斥量 `mutex_ticket`
- **线程**：`winA` 和 `winB`，优先级 12
- **对比实验**：
  - 测试 1（无锁）：读取 → `delay(1ms)` → 写入，演示并发冲突
  - 测试 2（有锁）：`mutex_take` → 检查 → 延时 → 减票 → `mutex_release`

### 3.2 源码文件

`ipc_ticket.c`

### 3.3 对比实验结果

**测试 1（无锁）**：
```
[NO LOCK] window A sold ticket 20
[NO LOCK] window B sold ticket 20    ← ⚠️ 票号重复！
[NO LOCK] window A sold ticket 19
...
[RESULT] final ticket_count=0 (expected 0)
[WARNING] ticket_count != 0, race condition detected!
```

**测试 2（有锁）**：
```
[WITH LOCK] window A sold ticket 20
[WITH LOCK] window B sold ticket 19
[WITH LOCK] window A sold ticket 18
...
[RESULT] final ticket_count=0 (expected 0)
```

### 3.4 思考题解答

**Q：为什么只给 `ticket_count--` 加锁仍可能不安全？**

A：因为 `ticket_count--` 本身不是原子操作，它包含三步：
1. 读取 `ticket_count` 到寄存器
2. 寄存器值减 1
3. 写回 `ticket_count`

如果在第 1 步和第 3 步之间发生线程切换，另一个线程也执行同样的三步，
两个线程会读到相同的旧值，导致"丢失更新"（Lost Update）问题。
必须把"检查余票 → 延时 → 减票 → 打印"整个过程都锁住。

---

## 四、练习三：系统启动条件检查

### 4.1 设计说明

- **IPC 机制**：事件集 `event_startup`，AND 模式等待
- **线程**：
  - `net_init`（延时 800ms）→ 发 `EVENT_NET_READY`
  - `sensor_init`（延时 1500ms）→ 发 `EVENT_SENSOR_READY`
  - `storage_init`（延时 5000ms）→ 发 `EVENT_STORAGE_READY`
  - `business`（优先级最高 8）→ 等待 ALL_EVENTS，超时 3000ms

### 4.2 源码文件

`ipc_event.c`

### 4.3 串口输出示例

```
[startup_demo] all init threads started
business thread: waiting for all modules...
network ready
sensor ready
wait modules timeout, storage is not ready     ← 第一次等待超时（等3s，storage需5s）
business thread: waiting for all modules...
storage ready
all modules are ready
business task started
```

### 4.4 思考题解答

**Q：用三个独立信号量实现，与事件集有什么区别？**

A：独立信号量需要 3 个 `rt_sem_take()`，每次调用后需自行判断其他信号量状态，
逻辑复杂且容易出错；事件集的 `AND` 模式一次等待多个标志位，代码简洁，
且 `rt_event_recv` 返回值明确区分"成功"和"超时"，语义清晰。

---

## 五、练习四：快递分拣中心

### 5.1 设计说明

- **IPC 机制**：消息队列 `mq_package`，容量 5 条
- **线程**：
  - `sender1`、`sender2`（优先级 12）：各产生 5 条快递消息
  - `sorter`（优先级 9）：接收消息并分类统计
- **消息结构**：`{id, region(1=华东/2=华南/3=华北), weight}`
- **结束标记**：两个发送线程结束后，各发一条 `{0,0,0}` 结束标记

### 5.2 源码文件

`ipc_mailbox.c`

### 5.3 串口输出示例

```
[sender1] sent pkg (region=east, weight=1200 g)
[sender2] sent pkg (region=south, weight=850 g)
[sorter] received end marker (1/2)
[sorter] received end marker (2/2)
[sorter] received end marker (2/2)   ← 等待结束时收到了队列中的残留消息

--- Sorting Statistics ---
total packages: 10
east: 4, south: 3, north: 3
total weight: 10250 g
send failed: 0
-------------------------
```

### 5.4 队列满处理

当消息队列已满（5条）时，`rt_mq_send()` 返回 `RT_EFULL`，记录 `send_failed++` 并重试，
不会崩溃。

---

## 六、MSH 命令汇总

| 命令 | 文件 | 说明 |
|------|------|------|
| `parking_demo` | `ipc_parking.c` | 停车场（计数信号量） |
| `ticket_demo` | `ipc_ticket.c` | 售票系统（互斥量+对比实验） |
| `startup_demo` | `ipc_event.c` | 启动条件检查（事件集） |
| `package_demo` | `ipc_mailbox.c` | 快递分拣（消息队列） |

**防重复执行**：各命令执行前通过 `rt_thread_find()` 检测同名线程是否已存在，
若存在则打印警告并拒绝重复启动。

---

## 七、提交说明

```
2026/第3组/马峥/day3/
├── README.md           # 理论梳理 + 实验结果（本文档）
├── ipc_parking.c      # 练习一源码
├── ipc_ticket.c       # 练习二源码
├── ipc_event.c        # 练习三源码
└── ipc_mailbox.c      # 练习四源码
```

**PR 提交**：
- 分支名：`day3-mazheng`
- 标题：`[作业提交] 线程同步与通信培训 - 马峥`
