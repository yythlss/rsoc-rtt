# 【DAY3】RT-Thread 线程间同步与通信课后作业

> 目标平台：STM32F407 星火号（`stm32f407-rtspark`）
> 提交仓库：https://gitee.com/rtthread/rsoc-rtt
> 提交人：`许伟威` · 第3组

---

## 一、IPC 机制理论总结

多线程共享同一块内存和处理器，多个线程同时访问共享资源会产生竞争，需要一套机制在线程之间做同步和通信。RT-Thread 提供了信号量、互斥量、事件集、邮箱、消息队列五种 IPC 机制。

### 1.1 基本概念

- **临界区**：同一时间只允许一个线程访问的共享资源（或代码段）。多个线程同时进入临界区会破坏数据。
- **阻塞与非阻塞**：线程申请资源时，资源不可用可以选择阻塞（挂起等待）或非阻塞（立即返回失败）。
- **线程挂起**：线程阻塞时会从就绪队列移出、不再参与调度，等条件满足后再回到就绪态。

### 1.2 信号量（Semaphore）

信号量是一个计数值，用来管理资源的数量。

| 类型 | 初始值 | 用途 |
|------|--------|------|
| 二值信号量 | 通常为 0 | 线程间同步（一个事件的发生） |
| 计数信号量 | 资源数量 | 管理多个同类资源实例 |

主要 API：

```c
rt_sem_t rt_sem_create(const char *name, rt_uint32_t value, rt_uint8_t flag);
rt_err_t rt_sem_take(rt_sem_t sem, rt_int32_t time);      /* 获取，value-- */
rt_err_t rt_sem_release(rt_sem_t sem);                     /* 释放，value++ */
```

`rt_sem_init` 是静态版本，线程控制块和信号量对象由用户提供。`take` 的 `flag` 参数决定等待队列的排队方式，实时性要求高的场景优先选 `RT_IPC_FLAG_PRIO`（按优先级排队）而不是 `RT_IPC_FLAG_FIFO`。

### 1.3 互斥量（Mutex）

互斥量是特殊的二值信号量，但有两个关键区别：

1. **所有权**：互斥量只能由持有它的线程释放，信号量可以被任意线程释放。
2. **解决优先级反转**：通过**优先级继承协议**，当高优先级线程等待互斥量时，临时提升持有互斥量线程的优先级，阻止中优先级线程抢占，避免高优先级线程被无限期拖延。

互斥量还支持递归访问（同一线程可多次 take）。

```c
rt_mutex_t rt_mutex_create(const char *name, rt_uint8_t flag);
rt_err_t rt_mutex_take(rt_mutex_t mutex, rt_int32_t time);
rt_err_t rt_mutex_release(rt_mutex_t mutex);
```

### 1.4 事件集（Event）

事件集用 32 位无符号整数表示 32 个事件标志位，支持一对多、多对多同步。

```c
rt_event_t rt_event_create(const char *name, rt_uint8_t flag);
rt_err_t rt_event_send(rt_event_t event, rt_uint32_t set);           /* 置位 */
rt_err_t rt_event_recv(rt_event_t event, rt_uint32_t set,
                       rt_uint8_t option, rt_int32_t timeout,
                       rt_uint32_t *recved);                          /* 等待 */
```

`option` 关键标志：

| 标志 | 含义 |
|------|------|
| `RT_EVENT_FLAG_AND` | 逻辑与，等待所有指定位都置位 |
| `RT_EVENT_FLAG_OR` | 逻辑或，任一位置位即满足 |
| `RT_EVENT_FLAG_CLEAR` | 满足后清除这些标志位 |

### 1.5 消息邮箱（Mailbox）

邮箱传递固定 4 字节（32 位系统指针大小）的消息，适合传指针或整型。

- 发送通常**非阻塞**，适合在中断里给线程发消息。
- 接收通常**阻塞**，可设超时。

### 1.6 消息队列（Message Queue）

消息队列是邮箱的扩展，支持**变长消息**，内部用链表管理消息块。

```c
rt_mq_t rt_mq_create(const char *name, rt_size_t msg_size,
                     rt_size_t max_msgs, rt_uint8_t flag);
rt_err_t rt_mq_send(rt_mq_t mq, const void *buffer, rt_size_t size);
rt_err_t rt_mq_recv(rt_mq_t mq, void *buffer, rt_size_t size, rt_int32_t timeout);
```

`rt_mq_urgent` 可把消息插到队列头部，实现紧急消息优先处理。

---

## 二、练习一：停车场车位管理

### 2.1 实现思路

用初始值为 3 的计数信号量表示剩余车位。5 辆车各自尝试 `rt_sem_take`，拿到的进入、拿不到的等待；车辆停留一段时间后 `rt_sem_release` 归还车位。`take` 设置 2 秒超时，超时打印等待信息后重新申请。

### 2.2 运行现象

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
```

车 1、2、3 先进入占满 3 个车位，车 4、5 因 2 秒内没拿到车位打印等待信息；车离开释放车位后，等待的车重新申请成功。整个过程中在场车辆始终不超过 3 辆。

### 2.3 思考题

**为什么用计数信号量而不是互斥量？**

互斥量本质是取值只有 0/1 的信号量，只能表示"一个资源"，而且带所有权属性，适合保护临界区。停车场有 3 个车位，是"多个同类资源实例"，需要用计数信号量（初始值 3）来统计剩余数量：每进入一辆 `value--`，每离开一辆 `value++`，天然保证了同时在场车辆不超过 3。互斥量无法表示数量 3，也谈不上"剩余车位"。

---

## 三、练习二：多窗口售票系统

### 3.1 实现思路

两个窗口线程循环"检查余票 → 减 1 → 记录票号 → 打印"，用互斥量把整个读-改-写过程包起来。票号从 20 递减，售完（余票为 0）后两个线程自动退出。

### 3.2 运行现象

```text
window A sold ticket 20
window B sold ticket 19
window B sold ticket 18
window A sold ticket 17
...
window A sold ticket 1
all tickets sold, remaining tickets: 0
```

### 3.3 对比实验

把 `USE_MUTEX` 改成 0，并在"读余票"和"改余票"之间加入 `rt_thread_mdelay(5)`，两个窗口会同时读到同一个余票值，出现重复票号，最终余票可能为负：

```text
window A sold ticket 20
window B sold ticket 20   ← 重复票号
window A sold ticket 18
window B sold ticket 18   ← 重复票号
...
```

加回互斥量后运行恢复正常，20 张票全部卖出、无重复票号、余票为 0。

### 3.4 思考题

**为什么只给 `ticket_count--` 加锁仍可能不安全？**

售票的完整操作是"读余票 → 判断是否 >0 → 减 1"，这是一个读-改-写序列。如果只给减操作加锁，读和判断仍在线程间交错：两个窗口可能同时读到余票 20，都判断有票，然后先后执行 `20 - 1 = 19`，结果只卖出一张却记了两张、还产生重复票号。锁必须覆盖"读-判断-改"的整个过程，让这个序列原子化，临界区才是安全的。

---

## 四、练习三：系统启动条件检查

### 4.1 实现思路

定义三个事件位表示三个模块就绪，三个初始化线程各自延时后 `rt_event_send` 置位；主业务线程用 `RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR` 等待三个位全部置位。存储模块初始化 5 秒、主业务最多等 3 秒，超时后通过读取事件集当前值判断哪些模块没就绪，再继续等待。

### 4.2 运行现象

```text
network ready
sensor ready
wait modules timeout, storage is not ready
storage ready
all modules are ready
business task started
```

### 4.3 思考题

**用三个独立信号量实现，和事件集相比有什么区别？**

事件集用一个 32 位值统一管理多个事件，能一次性等待"全部就绪"（AND）或"任一就绪"（OR），还能在满足后一次性清除多个标志。三个独立信号量只能一个个 `take`，无法原子地判断"三个都好了"，而且信号量计数会被消费、需要额外逻辑恢复初始值，逻辑更绕、可读性差。多事件组合同步场景下事件集更合适。

---

## 五、练习四：快递分拣中心

### 5.1 实现思路

创建容量 5 的消息队列，两个发件线程各发 5 条快递（编号用 1001~1005、2001~2005 两个区间避免重复）。发件前检查队列是否已满，满了就记一次失败并稍后重试，不崩溃。每个发件线程发完后再发一条 `id=0` 的结束消息（`weight` 字段带上自己的失败次数），分拣线程收到 2 条结束消息后统计并退出。

### 5.2 运行现象

```text
package 1001 -> east, weight: 500 g
package 2001 -> east, weight: 500 g
package 1002 -> south, weight: 637 g
...
total packages: 10
east: 4, south: 4, north: 2
total weight: 7552 g
send failed: 6
```

分拣线程每条耗时 30ms、发件线程每条间隔 10ms，发件快于分拣，队列会短时占满，`send failed` 记录了队列满时发件线程重试的次数，最终 10 条快递全部被正确分拣统计。

### 5.3 结果说明

- 快递总数 = 10，与两个线程各发 5 条一致。
- 各区域计数之和 = 总数，无丢件。
- 总重量为 10 条快递重量之和。
- `send failed > 0` 说明队列满时发件线程没有崩溃，而是记录失败后重试。

---

## 六、MSH 命令说明

| 命令 | 对应练习 | 说明 |
|------|----------|------|
| `parking_demo` | 练习一 | 计数信号量，停车场 |
| `ticket_demo` | 练习二 | 互斥量，售票 |
| `startup_demo` | 练习三 | 事件集，启动检查 |
| `package_demo` | 练习四 | 消息队列，分拣 |

四个 demo 都用全局对象指针是否为空判断任务是否已启动，重复执行命令时直接提示"already running"并返回，不会因同名线程或 IPC 对象残留而异常。
