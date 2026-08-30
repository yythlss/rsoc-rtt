# RT-Thread 线程间同步与通信实验报

| MSH 命令 | 练习 | IPC 机制 |
|---|---|---|
| `parking_demo` | 停车场车位管理 | 计数信号量 |
| `ticket_demo` | 多窗口售票系统 | 互斥量（含无锁对比实验） |
| `startup_demo` | 系统启动条件检查 | 事件集 |
| `package_demo` | 快递分拣中心 | 消息队列 |

---

## 一、理论总结

### 1. 基本概念

- **临界区**：每个线程中访问（读/写）共享资源的那段代码。多个线程同时执行各自的临界区会导致竞态（数据不一致），因此需要用互斥量等机制保证同一时刻只有一个线程进入临界区。
- **阻塞与非阻塞**：线程请求某个资源而资源暂不可用时，阻塞方式会让线程主动让出 CPU 并挂起等待，直到资源可用或超时才被唤醒（如 `rt_sem_take(sem, timeout)`）；非阻塞方式则不让线程等待，立即返回错误码，由调用者决定后续处理（如 `rt_sem_trytake()`，或消息队列满时 `rt_mq_send()` 立即返回 `-RT_EFULL`）。
- **挂起**：线程因等待信号量、互斥量、事件、消息等资源而暂停运行，从就绪队列移入对应 IPC 对象的挂起等待队列；当条件满足或等待超时后被唤醒并重新进入就绪状态。

### 2. 线程间同步

#### 2.1 信号量

- **概念**：信号量是一个内核计数器，用于协调多个线程对有限共享资源的访问。创建时指定初值，`take` 使值减 1（值已为 0 时线程挂起），`release` 使值加 1（并唤醒一个等待线程）。
- **二值信号量**：初值为 1，取值只有 0/1，功能类似一把锁，常用于线程互斥。
- **互斥信号量**：即用作互斥的二值信号量。与专门的互斥量相比，信号量没有所有权概念（释放者不要求是获取者），也不具备优先级继承与递归加锁能力，因此用于互斥时不如互斥量严谨。
- **控制块**：

```c
struct rt_semaphore
{
    struct rt_ipc_object parent;   /* 继承自 ipc_object 类 */
    rt_uint16_t          value;    /* 信号量的值 */
    rt_uint16_t          reserved; /* 保留字段 */
};
typedef struct rt_semaphore *rt_sem_t;
```

- **常用 API**：

```c
/* 创建与删除（动态） */
rt_sem_t rt_sem_create(const char *name, rt_uint32_t value, rt_uint8_t flag);
rt_err_t rt_sem_delete(rt_sem_t sem);

/* 初始化与脱离（静态） */
rt_err_t rt_sem_init(rt_sem_t sem, const char *name, rt_uint32_t value, rt_uint8_t flag);
rt_err_t rt_sem_detach(rt_sem_t sem);

/* 获取：普通获取（带超时） 与 无等待获取 */
rt_err_t rt_sem_take(rt_sem_t sem, rt_int32_t timeout);
rt_err_t rt_sem_trytake(rt_sem_t sem);

/* 释放 */
rt_err_t rt_sem_release(rt_sem_t sem);
```

#### 2.2 互斥量

- **概念**：互斥量只有"锁定/解锁"两种状态，用于保护临界区，保证同一时刻只有一个线程访问共享数据。
- **特性**：
  - **所有权**：互斥量只能由加锁的线程解锁（内核会校验），信号量可由任意线程释放；
  - **优先级继承**：互斥量支持优先级继承协议，可缓解优先级翻转问题；
  - **递归加锁**：同一线程可重复加锁（`hold` 计数），信号量不支持。
- **控制块**：

```c
struct rt_mutex
{
    struct rt_ipc_object parent;             /* 继承自 ipc_object 类 */
    rt_uint16_t          value;              /* 互斥量的值 */
    rt_uint8_t           original_priority;  /* 持有线程的原始优先级 */
    rt_uint8_t           hold;               /* 持有线程的持有次数 */
    struct rt_thread    *owner;              /* 当前拥有互斥量的线程 */
};
typedef struct rt_mutex *rt_mutex_t;
```

- **常用 API**：

```c
/* 创建与删除（动态） */
rt_mutex_t rt_mutex_create(const char *name, rt_uint8_t flag);
rt_err_t   rt_mutex_delete(rt_mutex_t mutex);

/* 初始化与脱离（静态） */
rt_err_t rt_mutex_init(rt_mutex_t mutex, const char *name, rt_uint8_t flag);
rt_err_t rt_mutex_detach(rt_mutex_t mutex);

/* 获取：普通获取（带超时） 与 无等待获取 */
rt_err_t rt_mutex_take(rt_mutex_t mutex, rt_int32_t timeout);
rt_err_t rt_mutex_trytake(rt_mutex_t mutex);

/* 释放 */
rt_err_t rt_mutex_release(rt_mutex_t mutex);
```

#### 2.3 事件集

- **概念**：事件集是一个 32 位的位图，每一位表示一个事件，用于一个线程等待多个事件的发生。支持 `AND`（全部满足）、`OR`（任一满足）、`CLEAR`（满足后自动清除）组合，且一次发送可唤醒多个等待线程（一对多）。
- **实现方式**：内核用 32 位无符号整数 `set` 保存事件集合；`rt_event_send()` 置位并唤醒满足条件的等待线程，`rt_event_recv()` 按 AND/OR/CLEAR 选项判断条件是否满足。
- **控制块**：

```c
struct rt_event
{
    struct rt_ipc_object parent;   /* 继承自 ipc_object 类 */
    rt_uint32_t          set;      /* 事件集合，每一 bit 表示一个事件 */
};
typedef struct rt_event *rt_event_t;
```

- **常用 API**：

```c
/* 创建与删除（动态） */
rt_event_t rt_event_create(const char *name, rt_uint8_t flag);
rt_err_t   rt_event_delete(rt_event_t event);

/* 初始化与脱离（静态） */
rt_err_t rt_event_init(rt_event_t event, const char *name, rt_uint8_t flag);
rt_err_t rt_event_detach(rt_event_t event);

/* 获取（接收事件；timeout = 0 即为无等待获取）
   opt: RT_EVENT_FLAG_AND / RT_EVENT_FLAG_OR / RT_EVENT_FLAG_CLEAR */
rt_err_t rt_event_recv(rt_event_t event, rt_uint32_t set, rt_uint8_t opt,
                       rt_int32_t timeout, rt_uint32_t *recved);

/* 释放（发送事件） */
rt_err_t rt_event_send(rt_event_t event, rt_uint32_t set);
```

### 3. 线程间通信

#### 3.1 消息邮箱

- **特点**：每条消息固定为 4 字节（`rt_ubase_t`），只能传递一个值（通常是指针或 32 位整数），按 FIFO 收发；邮箱满时发送线程可挂起等待或立即返回。
- **应用**：轻量级的线程间"传值/传地址"通知，例如把一块缓冲区的指针通过邮箱传给另一个线程。
- **控制块**：

```c
struct rt_mailbox
{
    struct rt_ipc_object parent;                /* 继承自 ipc_object 类 */
    rt_ubase_t          *msg_pool;              /* 消息缓冲区首地址 */
    rt_uint16_t          size;                  /* 消息缓冲区大小 */
    rt_uint16_t          entry;                 /* 消息缓冲区中的消息数目 */
    rt_uint16_t          in_offset;             /* 进入消息缓冲区的偏移 */
    rt_uint16_t          out_offset;            /* 离开消息缓冲区的偏移 */
    rt_list_t            suspend_sender_thread; /* 发送线程的挂起等待队列 */
};
typedef struct rt_mailbox *rt_mailbox_t;
```

- **常用 API**：

```c
/* 创建与删除（动态） */
rt_mailbox_t rt_mb_create(const char *name, rt_size_t size, rt_uint8_t flag);
rt_err_t     rt_mb_delete(rt_mailbox_t mb);

/* 初始化与脱离（静态） */
rt_err_t rt_mb_init(rt_mailbox_t mb, const char *name, void *msgpool,
                    rt_size_t size, rt_uint8_t flag);
rt_err_t rt_mb_detach(rt_mailbox_t mb);

/* 获取（接收；timeout = 0 即为无等待获取） */
rt_err_t rt_mb_recv(rt_mailbox_t mb, rt_ubase_t *value, rt_int32_t timeout);

/* 释放（发送） */
rt_err_t rt_mb_send(rt_mailbox_t mb, rt_ubase_t value);
```

#### 3.2 消息队列

- **特点**：可存放多条消息，每条消息长度可变（不超过 `msg_size`），按 FIFO 接收；发送时内核拷贝消息内容（值传递），发送方与接收方不共享内存。
- **应用**：线程间传递数据块（结构体等），例如本实验中的快递消息 `struct package_msg`。
- **控制块**：

```c
struct rt_messagequeue
{
    struct rt_ipc_object parent;                /* 继承自 ipc_object 类 */
    void                *msg_pool;              /* 指向存放消息的缓冲区地址 */
    rt_uint16_t          msg_size;              /* 每个消息的长度 */
    rt_uint16_t          max_msgs;              /* 最大能够容纳的消息数 */
    rt_uint16_t          entry;                 /* 队列中已有的消息数 */
    void                *msg_queue_head;        /* 消息队列链表头 */
    void                *msg_queue_tail;        /* 消息队列链表尾 */
    void                *msg_queue_free;        /* 指向队列中的空闲节点 */
    rt_list_t            suspend_sender_thread; /* 发送线程的挂起等待队列 */
};
typedef struct rt_messagequeue *rt_mq_t;
```

- **常用 API**：

```c
/* 创建与删除（动态） */
rt_mq_t rt_mq_create(const char *name, rt_size_t msg_size,
                     rt_size_t max_msgs, rt_uint8_t flag);
rt_err_t rt_mq_delete(rt_mq_t mq);

/* 初始化与脱离（静态） */
rt_err_t rt_mq_init(rt_mq_t mq, const char *name, void *msgpool,
                    rt_size_t msg_size, rt_size_t pool_size, rt_uint8_t flag);
rt_err_t rt_mq_detach(rt_mq_t mq);

/* 获取（接收；timeout = 0 即为无等待获取） */
rt_err_t rt_mq_recv(rt_mq_t mq, void *buffer, rt_size_t size, rt_int32_t timeout);

/* 释放（发送） */
rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, rt_size_t size);
```

> 以上控制块与常用 API 参见 RT-Thread 文档：
> - 线程间同步：https://www.rt-thread.org/document/site/master/#/rt-thread-version/rt-thread-standard/programming-manual/ipc1/ipc1
> - 线程间通信：https://www.rt-thread.org/document/site/master/#/rt-thread-version/rt-thread-standard/programming-manual/ipc2/ipc2

---

## 二、总体设计说明（公共机制）

1. **MSH 命令启动**：四个练习各自 `MSH_CMD_EXPORT` 导出命令，烧录后复位即可在终端输入命令运行；`main.c` 仅保留最小占位，不包含任何实验代码。
2. **创建结果检查**：所有线程（`rt_thread_create`）与 IPC 对象（`rt_sem_create` 等）均检查返回值，失败时打印错误信息并回滚（删除已创建线程、复位标志），不留半初始化状态。
3. **重复执行安全**：
   - 每个练习维护一个运行标志（`g_xxx_running`），命令执行时若上一轮任务尚未结束则**拒绝重复启动**；
   - 任务结束后线程入口函数直接 `return`，由内核 `_thread_exit()` 自动回收线程资源（栈与线程控制块）；
   - IPC 对象采用"惰性创建、跨轮复用"，每轮开始前复位到初始状态（如事件集 `RT_IPC_CMD_RESET`、信号量排空后重新补满）。
4. 演示线程统一栈大小 1024 B、优先级 16（数值上低于 main 线程的 10、高于 tshell 的 20）；演示输出统一走 `demo_printf()`（内部加打印互斥量），避免多线程 `rt_kprintf` 按字符交错。

---

## 三、练习一：停车场车位管理（计数信号量）

### 设计思路

- 创建计数信号量 `park`，**初值 3**，表示剩余车位。
- 创建 5 个车辆线程（`car1`~`car5`），每辆车错峰 300 ms 到达。
- 车辆用 `rt_sem_take(sem, 2000ms)` 申请车位：
  - **成功**：打印 `carX entered, used spaces: N`（N = 3 − 信号量当前值），停 3 秒后 `rt_sem_release()` 释放车位并打印 `carX left`；
  - **超时**：打印 `carX waiting for a space`，延时 100 ms 后重新申请。
- 最后一辆车离场后打印 `parking demo finished` 并复位运行标志。

### 结果分析

车位只有 3 个，car1~car3 错峰到达后立即获得车位，信号量值从 3 递减到 0，`used spaces` 依次为 1、2、3。car4 在 0.9 s 到达时车位已满，等满 2 s 仍未获得，打印 `car4 waiting for a space` 后延时 100 ms 重新申请。需要说明的是：car4 重新申请期间处于 100 ms 延时中（并未挂起在信号量上），而 car5 仍在第一次 2 s 等待中，因此 car1 在 3.0 s 离场释放车位时，先被唤醒的是挂起在信号量上的 car5（`car5 entered`）；随后 car2 离场，car4 才获得车位。这一顺序符合信号量"按挂起队列 FIFO 顺序唤醒"的规则，且整个过程任意时刻在场车辆数始终 ≤ 3，"等待超时 → 稍后重试 → 最终进入"的容错逻辑也得到体现。

### 思考题：为什么这里使用计数信号量，而不是互斥量？

车位是**可以同时被多辆车占用的同类资源**，数量为 3，需要用"剩余数量"来描述。计数信号量的初值正好表达资源数量，`take/release` 分别对应"占用/归还一份资源"，3 辆车可同时拿到。而互斥量只有 0/1 两种状态，只能表达"整个停车场同时只允许一辆车进入"，无法表达"3 个车位"，因此本场景必须使用计数信号量。

---

## 四、练习二：多窗口售票系统（互斥量）

### 设计思路

- 全局变量 `g_ticket_left` 保存剩余票数，初值 20；`g_ticket_sold[20]` 记录已售票号用于检测重复。
- 窗口 A/B 两个线程循环执行：检查余票 → 记录票号 → 余票减 1 → 打印结果；两次售票之间延时 1 ms 让两个窗口交替执行。
- **加锁版本**：整个"检查-减票-记录"过程放在 `rt_mutex_take/release` 之间，成为原子操作。
- 两个窗口都退出后，由最后退出的线程打印售出总数、重复票号数、剩余票数。

### 结果分析

加锁后，20 张票恰好售完、票号从 20 到 1 不重复、余票为 0，满足全部硬性指标。无锁时，A、B 都通过 `g_ticket_left > 0` 检查后，在 10 ms 延时里同时持有同一个旧值，从而卖出相同票号；当只剩最后 1 张票时两个窗口都能通过检查，连减两次后余票可能变为 −1。无锁版本的竞态暴露形式每次运行可能不同（重复票号、负余票、售出总数 ≠ 20 等），可反复执行多次观察对比。

### 思考题：为什么只给 `ticket_count--` 加锁仍可能不安全？

售票是"检查—修改"两步操作，只有把两步整体变成原子操作才安全。若只锁住 `ticket_count--`，线程 A、B 仍可以在锁外同时通过 `if (ticket_count > 0)` 的检查（都认为还有余票），随后依次进入锁内各自执行一次减一；当只剩最后 1 张票时，A、B 都能通过检查，连减两次后余票变成 −1，并卖出两个相同票号。因此必须把"检查余票 + 减票 + 记录票号"作为一个完整临界区，用互斥量整体保护，这正是"保护完整的售票过程"的原因。

---

## 五、练习三：系统启动条件检查（事件集）

### 设计思路

- 定义三个事件位 `EVENT_NET_READY / EVENT_SENSOR_READY / EVENT_STORAGE_READY`（`1<<0/1/2`）。
- 三个初始化线程（`netinit` 1 s、`snrinit` 2 s、`stoinit` 5 s）各自延时后 `rt_event_send()` 发送对应就绪事件并打印 `xxx ready`。
- 主业务线程（`biz`）用 **AND 模式 + 3 s 超时**循环等待：
  - `rt_event_recv(EVENT_ALL_READY, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, 3000ms, &recved)`；
  - 全部到达 → 打印 `all modules are ready` / `business task started`，退出；
  - 超时 → 读取事件集当前值，打印尚未就绪的模块（`wait modules timeout, storage not ready`），再次等待。
- 事件位采用 **AND+CLEAR**：成功时自动清零；超时时已到达的事件位保留，供下一次等待继续使用，不会丢失。

### 结果分析

主业务线程第一次等待 3 s 超时：此时网络（1 s）、传感器（2 s）已就绪，存储模块（5 s）未就绪，正确打印缺失模块名。超时返回时已就绪的事件位不清除，第二次等待在存储就绪后立即满足 AND 条件返回，打印启动信息——超时测试与再次等待按要求完成。

### 思考题：如果用三个独立信号量实现，与使用事件集有什么区别？

三个独立信号量无法直接表达"全部到达"这一组合条件：主线程只能依次等待三个信号量，或再引入额外的计数/状态变量，逻辑更复杂，而事件集一次 AND 等待即可原子地判断多个条件。在超时与状态查询上，事件集超时后可以直接读出位图，判断缺哪个模块，信号量则只能知道取到了几个，还需自己维护对应关系。在资源占用上，事件集只是一个对象加 32 位位图，位本身几乎不占资源，三个信号量则是三个独立内核对象，占用更多内存与对象槽位。此外事件集一次发送可同时唤醒多个等待同一条件的线程（一对多），而信号量释放一次只能唤醒一个等待线程。

---

## 六、练习四：快递分拣中心（消息队列）

### 设计思路

- 消息结构 `struct package_msg { rt_uint32_t id; rt_uint8_t region; rt_uint16_t weight; }`，region 1/2/3 对应华东/华南/华北，region = 0 表示**结束消息**。
- 创建消息队列 `pkgmq`：消息大小 `sizeof(struct package_msg)`，容量 **5**。
- 两个发件线程（`sndr1`、`sndr2`）各发 5 条快递：编号分别为 1001~1005、2001~2005（**全局唯一**），区域轮转（1→3），重量按公式生成。
- 发送使用**非阻塞** `rt_mq_send()`：队列满返回 `-RT_EFULL` 时**计数发送失败次数**并延时 50 ms 重试，发送线程不会崩溃。
- 每个发件线程发完 5 条后发送一条结束消息（region = 0）。
- 分拣线程（`sorter`）`rt_mq_recv()` 阻塞接收：普通消息打印编号/区域/重量并统计（总数、各区域数、总重量），每分拣一条延时 80 ms（让队列满的情况真实出现）；收到 **2 条结束消息**后打印统计结果并退出。

### 结果分析

两个发件线程并发发送，分拣线程按 FIFO 顺序取出 10 条快递，编号无重复（1001~1005、2001~2005）。区域统计 east:4、south:3、north:3 与发送公式 `((sender_no + i) % 3) + 1` 的分布一致，总重量 11750 g 为两条发送序列之和。队列容量只有 5，发件线程发送速度快于分拣速度时，`rt_mq_send()` 返回 `-RT_EFULL`，此时线程计数失败并延时重试，没有崩溃、没有丢件；`send failed` 的具体次数随运行时序略有浮动，结束消息（region=0）到达后分拣线程输出统计并退出。
