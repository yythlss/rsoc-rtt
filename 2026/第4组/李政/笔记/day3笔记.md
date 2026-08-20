# RT-Thread 学习笔记：Day 3 线程间同步与通信（IPC 机制）

## 课程核心概述

在 RT-Thread 多线程编程中，为了协调多个线程对共享资源的访问以及实现线程之间的数据传递，内核提供了 **IPC（Inter-Process Communication，进程/线程间通信）** 机制。

- **线程间同步**：信号量（Semaphore）、互斥量（Mutex）、事件集（Event）。
- **线程间通信**：邮箱（Mailbox）、消息队列（Message Queue）。

## 一、核心基础概念

### 1. 临界区（Critical Section）

- **定义**：同一时刻**只允许一个线程访问**的共享资源或代码段。
- **生活类比**：一个房间内有饮水机、健身器材、卫生间，每个设施同一时刻只能容纳一个住户使用。

### 2. 阻塞与非阻塞

- **阻塞（Blocking）**：当线程请求的资源被占用时，线程主动让出 CPU 并进入挂起状态，直到资源可用才被唤醒。
- **非阻塞（Non-blocking）**：线程尝试获取资源，若资源不可用则立即返回错误码或超时状态，不阻塞当前线程执行。

### 3. 线程挂起与恢复

- 当线程进入阻塞状态时，内核将其从就绪列表中移出，放入对应 IPC 对象的挂起等待队列中；当同步事件到达或资源释放时，线程重新转入就绪态。

## 二、线程间同步机制

## 1. 信号量（Semaphore）

信号量用于解决线程间的同步或互斥问题，允许多个线程获取或释放。

#### (1) 分类

- **二值信号量**：初值通常为 `0`（表示事件未发生）。常用于线程与线程、线程与中断之间的同步。
- **计数信号量**：初值为非负整数 `N`（表示可用资源数量）。常用于控制系统中共享资源的多个实例（如停车场车位管理）。

#### (2) 核心数据结构与成员

- `parent`：继承自 `rt_ipc_object` 基类。
- `value`：当前信号量计数值。
- `max_value`：信号量最大计数值。
- `spinlock`：自旋锁，保护多核/并发下的成员安全。

#### (3) 核心 API

- **动态管理**：
  - `rt_sem_create(const char *name, rt_uint32_t value, rt_uint8_t flag)`
    - `flag` 可选：`RT_IPC_FLAG_FIFO`（先进先出排队）或 `RT_IPC_FLAG_PRIO`（优先级排队，推荐以保证实时性）。
  - `rt_sem_delete(rt_sem_t sem)`
- **静态管理**：
  - `rt_sem_init(rt_sem_t sem, const char *name, rt_uint32_t value, rt_uint8_t flag)`
  - `rt_sem_detach(rt_sem_t sem)`
- **获取与释放**：
  - `rt_sem_take(rt_sem_t sem, rt_int32_t time)`：获取信号量，值减 1；若值为 0 则根据 `time` 挂起等待。
  - `rt_sem_trytake(rt_sem_t sem)`：无等待获取，等价于 `rt_sem_take(sem, 0)`。
  - `rt_sem_release(rt_sem_t sem)`：释放信号量，值加 1，唤醒挂起队列中的线程。

### 2. 互斥量（Mutex）

互斥量是一种特殊的二值信号量，专用于临界资源的互斥访问。

#### (1) 互斥量与信号量的核心区别

- **所有权**：互斥量具有**所有权**概念，**只有持有互斥量的线程才能释放它**；信号量可由任何线程或中断释放。
- **递归访问**：互斥量支持同一线程的递归嵌套持有，内部记录持有次数（`hold` 计数）。
- **优先级继承（解决优先级翻转）**：
  - **优先级翻转问题**：高优先级线程 A 等待低优先级线程 C 持有的资源时，中优先级线程 B 抢占了 C 执行，导致 A 的执行反而滞后于 B。
  - **解决方法**：当 A 等待 C 持有的互斥量时，内核将 C 的优先级临时提升至 A 的级别（优先级继承），防止被 B 抢占；待 C 释放互斥量后恢复原优先级。

#### (2) 互斥量 vs 开关中断进入临界区

- `rt_enter_critical()` / `rt_hw_interrupt_disable()`：禁用中断或调度器，所有任务和中断响应被延迟，**严禁在其中加入延时**，适用于极高时序要求的微小代码段。
- **互斥量**：未获取到资源的线程被挂起，中断与高优先级的其他任务仍可正常响应。

#### (3) 核心 API

- **动态管理**：`rt_mutex_create(name, flag)` / `rt_mutex_delete(mutex)`（无论 flag 填什么，内核均默认按优先级排序）。
- **静态管理**：`rt_mutex_init(mutex, name, flag)` / `rt_mutex_detach(mutex)`。
- **获取与释放**：`rt_mutex_take(mutex, time)` / `rt_mutex_trytake(mutex)` / `rt_mutex_release(mutex)`。

### 3. 事件集（Event）

事件集用于多事件的同步，能够实现**一对多**或**多对多**的复杂同步。

#### (1) 特性与原理

- **32 位标志位**：每个事件集包含一个 32 位的整型变量，每一位（bit）代表一个独立的事件。
- **无排队性**：若向同一位连续发送多次事件且未被读取，效果等同于只发送一次。
- **触发逻辑**：
  - **逻辑或（`RT_EVENT_FLAG_OR`）**：任一指定事件发生即唤醒线程。
  - **逻辑与（`RT_EVENT_FLAG_AND`）**：所有指定事件全部发生才唤醒线程。
  - **清除标志（`RT_EVENT_FLAG_CLEAR`）**：线程成功捕获事件后，自动将对应 bit 置 0。

#### (2) 核心 API

- **动态/静态创建**：`rt_event_create` / `rt_event_delete`；`rt_event_init` / `rt_event_detach`。
- **发送事件**：`rt_event_send(rt_event_t event, rt_uint32_t set)`。
- **接收事件**：`rt_event_recv(rt_event_t event, rt_uint32_t set, rt_uint8_t option, rt_int32_t timeout, rt_uint32_t *recved)`。

## 三、线程间通信机制

## 1. 消息邮箱（Mailbox）

- **特性**：开销低、速度快。在 32 位系统中，每封邮件固定占用 **4 个字节**（刚好容纳一个 32 位整型或指针地址）。
- **非阻塞发送**：发送通常是非阻塞的，可安全用于中断服务程序（ISR）向线程投递消息。

#### 核心 API

- **创建与删除**：
  - 动态：`rt_mb_create(const char *name, rt_size_t size, rt_uint8_t flag)`（`size` 为邮件最大容量）。
  - 静态：`rt_mb_init(rt_mailbox_t mb, const char *name, void *msgpool, rt_size_t size, rt_uint8_t flag)`。
- **发送邮件**：
  - `rt_mb_send(rt_mailbox_t mb, rt_ubase_t value)`：非阻塞发送（邮箱满时返回 `-RT_EFULL`）。
  - `rt_mb_send_wait(rt_mailbox_t mb, rt_ubase_t value, rt_int32_t timeout)`：支持超时等待发送。
  - `rt_mb_urgent(rt_mailbox_t mb, rt_ubase_t value)`：**紧急邮件**，插入队首优先处理。
- **接收邮件**：
  - `rt_mb_recv(rt_mailbox_t mb, rt_ubase_t *value, rt_int32_t timeout)`。

### 2. 消息队列（Message Queue）

- **特性**：邮箱的扩展升级版，能接收**不固定长度**（以字节为单位，但不超过创建时设定的最大长度）的消息块。
- **内部机制**：基于链表管理，内部维护消息链表与空闲消息链表。发送时将内容从源内存拷贝至空闲消息块并挂入队尾。

#### 核心 API

- **创建与删除**：
  - 动态：`rt_mq_create(name, msg_size, max_msgs, flag)`。
  - 静态：`rt_mq_init(mq, name, msgpool, msg_size, pool_size, flag)`。
- **发送与接收**：
  - `rt_mq_send(rt_mq_t mq, const void *buffer, rt_size_t size)`：向队尾发送消息。
  - `rt_mq_send_wait(rt_mq_t mq, const void *buffer, rt_size_t size, rt_int32_t timeout)`：带等待机制发送。
  - `rt_mq_urgent(rt_mq_t mq, const void *buffer, rt_size_t size)`：将消息直接插到**队首**，供消费者优先接收。
  - `rt_mq_recv(rt_mq_t mq, void *buffer, rt_size_t size, rt_int32_t timeout)`：将队首消息拷贝至目标 buffer。

## 四、工程实验与调试技巧总结

1. **示例包使用（Kernel Samples）**：
   - 在 RT-Thread Studio 中通过 `RT-Thread Settings` 搜索添加 `kernel_samples` 软件包，勾选对应示例（信号量、互斥量、邮箱、事件、队列）即可自动加入编译。
2. **调试日志等级控制**：
   - 若默认调试日志输出过多，可在 `main.c` 中修改 `DBG_LEVEL` 或将打印等级切换为 `DBG_INFO`，过滤无关的 `LOG_D` 输出。
3. **MSH 控制台验证**：
   - 通过 `MSH_CMD_EXPORT` 导出的示例命令（如 `semaphore_sample`、`mutex_sample`），在终端直接执行即可观察交互现象。
4. **单步调试排查**：
   - 在获取锁与释放锁的关键点打断点，配合多线程栈与调用链窗口，观察线程挂起、唤醒以及优先级动态变化的完整流程。