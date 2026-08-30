# 【DAY3】RT-Thread 线程间同步与通信（IPC）实验报告

* **学员姓名**：杜马辰（第 2 组）
* **指导导师**：张丙儒
* **目标硬件**：STM32F407 星火号（Spark-1 开发板）
* **源码文件**：`study.c`
* **MSH 命令**：`parking_demo`、`ticket_demo`、`startup_demo`、`package_demo`
* **提交日期**：2026-08-20

---

## 一、 练习一：停车场车位管理（计数信号量 Semaphore）

### 1. 实验场景
停车场共有 **3 个可用车位**，有 **5 辆车（5 个并发线程）** 竞争车位。
* 使用计数信号量管理车位，初始计数值为 3。
* 车辆申请车位时设置 2000ms 超时。若超时未申请到车位，输出等待日志；申请成功后进入停车场停留，离开时释放车位，确保场内同时停放车辆不超过 3 辆。

### 2. 核心 API
* `rt_sem_create("park_sem", 3, RT_IPC_FLAG_PRIO)`：动态创建初始值为 3 的计数信号量。
* `rt_sem_take(sem, timeout)`：申请车位资源（计数值减 1，为 0 则进入挂起队列阻塞）。
* `rt_sem_release(sem)`：释放车位资源（计数值加 1，唤醒挂起队列中的最高优先级就绪车辆）。

### 3. 运行日志（FinSH 控制台输出）
```text
msh >parking_demo
[Parking Demo Started] Total spaces: 3, Cars: 5
Car 1: Entered successfully! [Current Used: 1 / 3]
Car 2: Entered successfully! [Current Used: 2 / 3]
Car 3: Entered successfully! [Current Used: 3 / 3]
Car 4: Waiting for an available space (timeout 2s)...
Car 5: Waiting for an available space (timeout 2s)...
Car 1: Left the parking lot. [Current Used: 2 / 3]
Car 4: Entered successfully! [Current Used: 3 / 3]
```

### 4. 思考题：为什么这里使用计数信号量，而不是互斥量？
* **互斥量（Mutex）**：计数值只能为 1，且**具有严格的线程所有权（谁上锁谁解锁）**，专门用于同一时刻仅允许单个线程访问的排他性临界资源。
* **计数信号量（Semaphore）**：计数值支持初始为 $N$（本例为 3），用于管理**一组多个相同的有限共享资源**，且不绑定持有者。因此管理多个车位必须采用计数信号量。

---

## 二、 练习二：多窗口售票系统（互斥量 Mutex）

### 1. 实验场景
两个售票窗口（Window A 和 Window B）并发销售 **20 张火车票**。
* 使用互斥量保护票数 `g_remaining_tickets` 的“读取-判断-出票-自减”完整临界区。
* 避免多线程竞态条件（Race Condition），杜绝同一张票被重复售出或超卖导致票数变负。

### 2. 核心 API
* `rt_mutex_create("tick_mtx", RT_IPC_FLAG_PRIO)`：动态创建互斥量（支持优先级继承）。
* `rt_mutex_take(mutex, RT_WAITING_FOREVER)`：获取互斥锁。
* `rt_mutex_release(mutex)`：释放互斥锁。

### 3. 对比实验与现象分析
1. **不加锁版本**：两个窗口并发读取 `g_remaining_tickets` 时，由于时间片切换，会出现两个窗口同时售出相同票号（如同时卖出第 18 号票），甚至票数减到负数。
2. **加互斥锁版本**：完整临界区被互斥锁包围，20 张票被严格有序、无重复地全部售出，票数等于 0 时窗口安全退出。

### 4. 运行日志
```text
msh >ticket_demo
[Ticket Demo Started] Total Tickets: 20
[Window A] Sold ticket NO. 20 (Remaining: 19)
[Window B] Sold ticket NO. 19 (Remaining: 18)
[Window A] Sold ticket NO. 18 (Remaining: 17)
...
[Window B] Sold ticket NO. 01 (Remaining: 00)
[Window A] All tickets sold out! (Remaining: 0)
[Window B] All tickets sold out! (Remaining: 0)
```

### 5. 思考题：为什么只给 `ticket--` 单行指令加锁仍可能不安全？
因为售票逻辑包含两步：① `if (ticket > 0)` 检查余票；② `ticket--` 扣除余票。
如果只锁 `ticket--`，窗口 A 和窗口 B 都可以同时通过第 ① 步检查，从而在余票只有 1 张时先后执行扣减，造成超卖（`ticket = -1`）。因此**必须锁住“判断+修改”的整个业务决策区间**。

---

## 三、 练习三：系统启动条件检查（事件集 Event）

### 1. 实验场景
系统上电后，主业务线程（Business Thread）必须等待 **网络模块（1s 就绪）、传感器模块（2s 就绪）、存储模块（5s 就绪）** 三者全部初始化完毕后才允许启动。
* 业务线程采用 `RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR` 进行逻辑“与”等待。
* 单次等待设置 3000ms 超时，超时打印未就绪模块，继续等待直到全部就绪。

### 2. 核心 API 与事件位
```c
#define EVENT_NET_READY         (1U << 0)
#define EVENT_SENSOR_READY      (1U << 1)
#define EVENT_STORAGE_READY     (1U << 2)
```
* `rt_event_send(&event, EVENT_XXX)`：向事件集发送指定就绪标志。
* `rt_event_recv(&event, mask, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, timeout, &recved)`：阻塞等待全条件满足。

### 3. 运行日志
```text
msh >startup_demo
[Startup Demo Started] Waiting for Net(1s), Sensor(2s), Storage(5s)...
[Init] Network module initialized ready.
[Init] Sensor module initialized ready.
[Business] Waiting modules timeout! Missing:
  - Storage NOT ready
[Init] Storage module initialized ready.

[Business] ALL modules are READY!
[Business] Main business task is NOW RUNNING...
```

### 4. 思考题：如果用 3 个独立信号量实现，与使用事件集相比有什么区别？
1. **事件集**：单个 32 位对象可同时表达 32 个独立事件，原生支持 `AND`（全满足）或 `OR`（任意满足）复合逻辑，代码紧凑，内存开销极小。
2. **3 个独立信号量**：需要创建 3 个独立的 IPC 对象，且 RT-Thread 没有原生 API 一步等待“所有信号量”，需要手动写复杂的循环嵌套判断，增加了调度开销和代码复杂度。

---

## 四、 练习四：快递分拣中心（消息队列 Message Queue）

### 1. 实验场景
* 发件线程 1 和发件线程 2 各发送 5 件快递（共 10 件）。
* 消息队列容量限制为 5。当队列满时，`rt_mq_send` 非阻塞返回 `-RT_EFULL`，发送线程记录重试次数并稍后重发。
* 分拣线程接收 10 件快递，分类统计华东、华南、华北区域件数及总重量。

### 2. 数据结构
```c
typedef struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;   // 1: 华东, 2: 华南, 3: 华北
    rt_uint16_t weight;   // 包裹重量 (g)
} package_msg_t;
```

### 3. 运行日志
```text
msh >package_demo
[Package Sorting Demo Started] Queue Capacity: 5, Total Packages: 10
[Sender 1] Sent Package #101 -> Region 1, Weight 500g
[Sender 2] Sent Package #201 -> Region 3, Weight 550g
  >> [Sorter] Received #101 (Region: 1, Weight: 500g) | Processed: 1/10
  >> [Sorter] Received #201 (Region: 3, Weight: 550g) | Processed: 2/10
...
[Sender 1] Queue FULL! Fail count: 1, retry after 100ms...
...
================ [Sorting Center Summary] ================
Total Packages Processed : 10
  - East Region (华东)   : 4
  - South Region (华南)  : 3
  - North Region (华北)  : 3
Total Weight             : 7250 g
==========================================================
```

---

## 五、 实验避坑总结

1. **超时返回值判断**：RT-Thread 中超时错误码为负数 `-RT_ETIMEOUT`，不能误写成正数 `RT_ETIMEOUT`。
2. **退出循环前释放互斥锁**：在使用 `break` 跳出循环时，必须先调用 `rt_mutex_release`，防止线程退出后锁未释放导致其他线程永久死锁。
3. **消息队列满重试机制**：`rt_mq_send` 在队列满时返回 `-RT_EFULL`，发送端必须使用循环重试逻辑，防止丢失包裹消息导致分拣线程计数不足永久挂起。
4. **局部变量手动清零**：栈上定义的局部结构体与统计计数器必须显式初始化（`= 0`），避免残留脏数据影响统计结果。
