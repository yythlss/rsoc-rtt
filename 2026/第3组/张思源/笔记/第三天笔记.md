# 【DAY3】RT-Thread 线程间同步与通信 -- 张思源（第3组）

- **目标平台**：STM32F407 星火一号（`stm32f407-rtspark`，RT-Thread Studio 工程 `03_ipc_sample`）
- **提交内容**：本 `README.md`（思考题 + 实验报告）+ 4 份源码（`parking.c` / `ticket.c` / `startup.c` / `package.c`）
- **MSH 命令**：`parking_demo` / `ticket_demo` / `startup_demo` / `package_demo`

---

## 练习一：停车场车位管理（计数信号量）

### 实验设计

| 对象 | 类型 | 说明 |
| --- | --- | --- |
| `parking_sem` | 计数信号量 | 初始值 3，表示 3 个车位 |
| `car1`~`car5` | 5 个线程 | 优先级 20，时间片 10，栈 1024B |

每辆车通过 `rt_sem_take(parking_sem, 2000ms)` 申请车位，2 秒超时后打印等待信息并重新申请；拿到车位后停留 3.5 秒离开并 `rt_sem_release` 释放车位。已用车位数通过 `PARKING_NUM - parking_sem->value` 计算得出。

### 串口日志

```
msh >parking_demo
car5 entered, used spaces: 1
car4 entered, used spaces: 2
car3 entered, used spaces: 3
car2 waiting for a space
car1 waiting for a space
car5 left
car2 entered, used spaces: 3
car4 left
car1 entered, used spaces: 3
car3 left
car2 left
car1 left
```

### 结果分析

- 前 3 辆车（car5/car4/car3）成功获取车位，`used spaces` 从 1 递增到 3，说明信号量初始值为 3 且最多 3 辆同时进入。
- car2/car1 在 2 秒内未获取车位，打印 `waiting for a space`，验证了超时机制。
- car5 离开后释放车位，car2 立即补上；car4 离开后 car1 补上，说明等待线程被正确唤醒。
- 重复执行 `parking_demo` 时，检测到 `parking_sem != RT_NULL` 会拒绝启动；所有车辆离开后自动清理信号量，可再次启动。

### 思考题：为什么使用计数信号量，而不是互斥量？

1. **资源数量不同**：互斥量的值只有 0 和 1，只能保护单一资源；计数信号量的值可以是任意非负整数，能表示多个同类资源。停车场有 3 个车位，需要初始值为 3 的计数信号量，互斥量无法表达"3 个可用资源"
2. **所有权语义不同**：互斥量有所有权概念，谁获取谁释放，不能由其他线程代为释放；而计数信号量没有所有权，一辆车获取车位后，可以由同一线程在离开时释放，也可以用在不同线程之间。停车场场景中，车辆线程获取和释放是同一个线程，但计数信号量的灵活性更适合资源池模式
3. **适用场景**：互斥量适用于保护临界区（同一时刻只允许一个线程访问共享资源），计数信号量适用于管理有限资源池（允许 N 个线程同时访问 N 个资源）。停车场属于资源池问题，因此用计数信号量

---

## 练习二：多窗口售票系统（互斥量）

### 实验设计

| 对象 | 类型 | 说明 |
| --- | --- | --- |
| `ticket_mutex` | 互斥量 | 保护售票临界区 |
| `ticket_count` | 全局变量 | 剩余票数，初始值 20 |
| `winA` / `winB` | 2 个线程 | 优先级 20，时间片 10，栈 1024B |

每个窗口线程循环执行：加锁 -> 检查余票 -> 打印票号 -> 余票减 1 -> 解锁 -> 延时 500ms。票号即减 1 之前的 `ticket_count` 值（20~1）。

### 串口日志

```
msh >ticket_demo
window B sold ticket 20
window A sold ticket 19
window B sold ticket 18
window A sold ticket 17
...
window B sold ticket 2
window A sold ticket 1
```

### 结果分析

- 20 张票全部售出，票号 20~1 无重复，余票最终为 0，未出现超卖
- 窗口 A 和 B 交替售票，说明互斥量有效保证了同一时刻只有一个窗口修改 `ticket_count`
- 售完后自动清理互斥量，可重复执行

### 对比实验（不加互斥量）

暂时注释掉 `rt_mutex_take` 和 `rt_mutex_release`，在打印 `ticket_count` 之后、`ticket_count--` 之前加入 `rt_thread_mdelay(10)`，观察是否出现重复售票

不加锁时，窗口 A 打印票号后被 `mdelay(10)` 切走，窗口 B 读到相同的 `ticket_count`，导致两张票打印相同票号，出现重复售票。加回互斥量后问题消失

### 思考题：为什么只给 `ticket_count--` 加锁仍可能不安全？

临界区不仅仅是 `ticket_count--` 这一条语句，而是**整个售票过程**：检查余票 -> 减票 -> 记录票号 -> 打印。如果只给 `ticket_count--` 加锁：

1. 线程 A 在锁外检查 `ticket_count > 0`（假设为 1），认为还有票
2. 线程 A 被切走，线程 B 也在锁外检查 `ticket_count > 0`（仍为 1），也认为还有票
3. 线程 A 进入锁内执行 `ticket_count--`，票数变为 0
4. 线程 B 进入锁内执行 `ticket_count--`，票数变为 -1，**超卖**

因此，必须把"检查余票"和"修改余票"作为一个**不可分割的原子操作**一起加锁，否则检查和修改之间的时间窗口会导致竞态条件

---

## 练习三：系统启动条件检查（事件集）

### 实验设计

| 对象 | 类型 | 说明 |
| --- | --- | --- |
| `startup_event` | 事件集 | 3 个事件位，AND 模式等待 |
| `EVENT_NET_READY` | 事件位 | `(1 << 0)` 网络就绪 |
| `EVENT_SENSOR_READY` | 事件位 | `(1 << 1)` 传感器就绪 |
| `EVENT_STORAGE_READY` | 事件位 | `(1 << 2)` 存储就绪 |
| `net` / `sensor` / `storage` / `main` | 4 个线程 | 初始化线程延时 1s/2s/5s，主业务线程等待 |

主业务线程第一次用 `RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR` 等待 3 个事件，超时 3 秒。超时后通过 `startup_event->set` 检查哪些位缺失，打印未就绪模块。然后第二次用 `RT_WAITING_FOREVER` 继续等待，直到存储模块（5 秒）就绪

### 串口日志

```
msh >startup_demo
net ready
sensor ready
wait modules timeout, storage is not ready
storage ready
all modules are ready
business task started
```

### 结果分析

- 网络模块 1 秒就绪，传感器模块 2 秒就绪，均在 3 秒超时前完成
- 存储模块 5 秒就绪，超过 3 秒超时，主业务线程正确打印 `storage is not ready`
- 超时后第二次等待（FOREVER），存储模块在 5 秒就绪后主业务线程立即被唤醒，打印启动信息
- 注意：超时判断使用 `startup_event->set` 而非 `received` 变量，因为 `rt_event_recv` 超时返回时不会更新 `received`（仍为 0），直接读事件集的 `set` 成员才能获取当前已到达的事件位

### 思考题：如果用三个独立信号量实现，与使用事件集相比有什么区别？

| 对比项 | 三个独立信号量 | 事件集 |
| --- | --- | --- |
| 等待方式 | 需要 3 次 `rt_sem_take` 串行等待，每次只能等一个信号量 | 一次 `rt_event_recv` 即可同时等待多个事件位 |
| AND/OR 逻辑 | 只能手动实现 AND（依次 take），OR 难以实现 | 原生支持 `RT_EVENT_FLAG_AND` 和 `RT_EVENT_FLAG_OR` |
| 超时控制 | 每次 take 有独立超时，总等待时间难以精确控制 | 一次 recv 统一超时，超时后可知哪些事件已到达、哪些缺失 |
| 资源开销 | 3 个信号量控制块 | 1 个事件集控制块 |
| 代码复杂度 | 高，需要循环等待、判断超时、处理中途唤醒 | 低，一行 `rt_event_recv` 搞定 |
| 场景适配 | 适合各模块独立、无关联的等待 | 适合多条件组合等待（全部就绪 / 任一就绪） |

事件集天生为"多事件组合等待"设计，用三个信号量虽然能实现相同功能，但代码更复杂、超时控制更困难、且无法高效实现 OR 逻辑

---

## 练习四：快递分拣中心（消息队列）

### 实验设计

| 对象 | 类型 | 说明 |
| --- | --- | --- |
| `package_mq` | 消息队列 | 容量 5 条，每条 `sizeof(struct package_msg)` |
| `sender1` / `sender2` | 2 个发送线程 | 分别产生 5 条消息，id 从 1001 和 2001 开始 |
| `sorter` | 1 个分拣线程 | 接收消息并按区域统计 |

消息结构体包含 `id`（快递编号）、`region`（1=华东/2=华南/3=华北）、`weight`（重量）。发送线程用 `rt_mq_send` 发送，队列满时返回 `-RT_EFULL`，记录失败次数。发完 5 条后发送 `region = 0xFF` 的结束消息。分拣线程收到 2 条结束消息后打印统计并退出

### 串口日志

```
msh >package_demo
package 2001 -> south, weight: 300 g
package 1001 -> south, weight: 300 g
package 2002 -> north, weight: 600 g
package 1002 -> north, weight: 600 g
package 2003 -> east, weight: 900 g
package 1003 -> east, weight: 900 g
package 2004 -> south, weight: 1200 g
package 1004 -> south, weight: 1200 g
package 2005 -> north, weight: 1500 g
package 1005 -> north, weight: 1500 g
sender 2 finished, send failed: 0
sender 1 finished, send failed: 0
total packages: 10
east: 2, south: 4, north: 4
total weight: 9000 g
```

### 结果分析

- 10 条快递全部被分拣，编号 1001~1005 和 2001~2005 无重复
- 各区域统计正确：华东 2、华南 4、华北 4，总计 10 条
- 总重量 9000g = (300+600+900+1200+1500) x 2，计算正确
- `send failed: 0`，说明发送间隔 100ms 足够让分拣线程消费消息，队列未满
- 两个发送线程各发一条结束消息（`region = 0xFF`），分拣线程收到 2 条后正确退出并清理队列

---

## 总结

本次作业通过四个场景实践了 RT-Thread 的四种 IPC 机制：

| IPC 机制 | 核心特点 | 适用场景 |
| --- | --- | --- |
| 计数信号量 | 值为非负整数，无所有权 | 资源池管理（停车场车位） |
| 互斥量 | 值为 0/1，有所有权，可优先级继承 | 临界区保护（售票系统） |
| 事件集 | 32 位事件位，支持 AND/OR | 多条件组合等待（启动检查） |
| 消息队列 | 缓冲多条结构化消息 | 线程间数据传递（快递分拣） |

共同设计要点：
1. 所有 IPC 对象和线程创建后均检查返回值，失败时打印错误并退出
2. 通过 MSH 命令启动，使用全局指针状态检测防止重复执行
3. 任务完成后主动删除 IPC 对象并重置指针，保证可重复运行
