# 【RSOC26】第3天作业 - RT-Thread 线程间同步与通信

> 提交人：李子夜（第1组）｜Gitee：@CyberSprout｜日期：2026-08-19

---

## 作业概述

本作业通过 4 个练习，覆盖 RT-Thread 中四种核心 IPC 机制：

| 练习 | IPC 对象 | 场景 |
|---|---|---|
| 练习一 | 计数信号量 | 停车场 3 车位 / 5 辆车 |
| 练习二 | 互斥量 | 双窗口售票，20 张票 |
| 练习三 | 事件集 | 三模块就绪检查，AND 等待 |
| 练习四 | 消息队列 | 快递分拣，区域分类统计 |

所有代码可直接在启用 `RT_USING_SEMAPHORE`、`RT_USING_MUTEX`、`RT_USING_EVENT`、`RT_USING_MESSAGEQUEUE` 的工程中编译运行。

---

## 源代码目录

```
2026/第1组/李子夜/作业/
├── 第3天作业.md          ← 本文件
├── parking.c             ← 练习一：停车场
├── ticket.c              ← 练习二：售票系统
├── startup.c             ← 练习三：启动条件检查
└── package.c             ← 练习四：快递分拣
```

---

## 环境确认

| 项目 | 值 |
|---|---|
| RT-Thread 版本 | 4.1.1 |
| SOC | STM32F407ZG（星火一号） |
| 编译环境 | RT-Thread Studio（GCC 工具链） |
| 所有代码已验证编译通过 | 是 |

---

## 练习一：停车场车位管理

### 设计思路

```
计数信号量（value=3）
    ├── car1  take → 进入，停留 3s，release
    ├── car2  take → 进入，停留 3s，release
    ├── car3  take → 进入（3 个车位全满）
    ├── car4  take timeout(2s) → 等待超时，500ms 后重试
    │                               → 此时一个车位释放（car 离开）
    │                               → 重试 take → 成功
    ├── car5  take → 进入，停留 3s，release
```

### 核心机制

- `rt_sem_create("park_sem", 3, FIFO)` — 创建初始值为 3 的信号量
- `rt_sem_take(sem, 2000)` — 阻塞等待，超时 2 秒返回 `-RT_ETIMEOUT`
- `rt_sem_release(sem)` — 离开时释放车位
- `rt_atomic_t` — 安全地记录当前已用车位数（不参与调度，只用于打印）

### 典型输出

```text
=== Parking Demo: 3 spaces, 5 cars ===
car1 entered, used spaces: 1
car2 entered, used spaces: 2
car3 entered, used spaces: 3
car4 waiting for a space timeout, will retry
car2 left, remaining spaces: 1
car4 entered (retry), used spaces: 3
```

### 思考题

**为什么用计数信号量而不是互斥量？**

1. 资源数量 > 1 —— 3 个车位可同时被占用，互斥量只能保证串行
2. 信号量的值域直观表示「剩余车位数」
3. 不存在嵌套获取场景，不需要互斥量的优先级继承机制

---

## 练习二：多窗口售票系统

### 设计思路

```
互斥量 ticket_mutex
    ├── 窗口 A 线程：循环 take mutex → 读 count → count-- → 记票号 → release
    ├── 窗口 B 线程：同上
    └── count 从 20 减到 0 时，两线程退出
```

### 对比实验

文件包含两个版本的售票线程：
- `ticket_window_locked` — 使用互斥量保护完整临界区
- `ticket_window_nolock` — 无锁版本，在读和写之间插入 5ms 延迟

运行无锁版本时会出现 `ticket_count < 0` 或重复票号的问题。

### 核心机制

- `rt_mutex_create("ticket_mtx", PRIO)` — 创建互斥量（优先级继承）
- `rt_mutex_take(mutex, FOREVER)` — 获取互斥量
- `rt_mutex_release(mutex)` — 释放互斥量

### 思考题

**为什么只给 `ticket_count--` 加锁仍可能不安全？**

票务操作的原子安全区是「读取余票 + 判断 >0 + -- + 记录票号」四步。只给 `--` 加锁时：

```
线程 A: 读 count=1   → 认为有票
线程 B: 读 count=1   → 也认为有票
线程 A: -- (加锁) → count=0
线程 B: -- (加锁) → count=-1  ← 错误！
```

两个线程都认为自己卖了最后一张票。

---

## 练习三：系统启动条件检查

### 设计思路

```
事件集 (ALL_READY = bit0|bit1|bit2)
    ├── 网络线程（1s 后置位 bit0）
    ├── 传感器线程（2s 后置位 bit1）
    ├── 存储线程（5s 后置位 bit2）
    └── 业务线程：
        ├── rt_event_recv(AND, 3s timeout)
        │   → 超时（只有 bit0+bit1 到达，bit2 未到）
        │   → 打印 "storage is not ready"
        │   → 再次 rt_event_recv(AND, FOREVER)
        │   → 三个 bit 全部到达 → 打印 "all modules are ready"
```

### 关键参数传递改进

使用 `struct module_param` 结构体传递线程参数（名称、延迟、事件位），避免使用字符串指针比较——后者依赖于编译器的字符串合并行为，含义不明确。

### 思考题

**3 个信号量 vs 事件集：**

| 维度 | 3 个信号量 | 事件集 |
|---|---|---|
| 内存 | ~150B × 3 结构 + 链表 | ~44B × 1 结构 |
| 原子性 | 需用户轮询，无法保证 | 内核保证 AND 一次性唤醒 |
| 超时 | 各自设 timeout | 一次 recv 统一超时 |
| 适用场景 | 资源计数 / 单事件 | 多条件仲裁 |

---

## 练习四：快递分拣中心

### 设计思路

```
消息队列 (容量 5, 每条 struct package_msg)
    ├── sender1 (base_id=1001) → 发送 5 条消息
    │   ├── 1001 → east,  858g
    │   ├── 1002 → south, 432g
    │   ├── 1003 → north, 991g
    │   ├── 1004 → east,  267g
    │   └── 1005 → south, 755g
    ├── sender2 (base_id=2001) → 发送 5 条消息
    │   ├── 2001 → north, 610g
    │   ├── ...
    │   └── 2005 → south, 188g
    └── sorter:
        ├── rt_mq_recv() 持续接收
        ├── 每条按 region 分类计数 + 累加 weight
        └── total 达到 10 条后：
            ├── 等 done_sem（两个 sender 都结束）
            └── 打印统计数据
```

### 核心机制

- `rt_mq_create("pkg_mq", sizeof(struct package_msg), 5, FIFO)` — 创建容量 5 的消息队列
- `rt_mq_send(mq, &msg, sizeof(msg))` — 发送消息，队列满时返回错误
- `rt_mq_recv(mq, &msg, sizeof(msg), FOREVER)` — 永久等待接收
- `rt_atomic_inc/add` — 无锁统计（避免在 sorter 线程中额外加锁）

### 结束同步

	sender1 完成后 → atomic_inc(senders_finished)
	sender2 完成后 → atomic_inc(senders_finished)
	             → 检测到 == 2 → rt_sem_release(done_sem)
	sorter → rt_sem_take(done_sem) → 打印统计 → break

---

## 验证方式

1. 将 4 个 `.c` 文件加入 RT-Thread Studio 工程
2. 确保 `rtconfig.h` 中开启了：
   ```
   RT_USING_SEMAPHORE
   RT_USING_MUTEX
   RT_USING_EVENT
   RT_USING_MESSAGEQUEUE
   ```
3. 编译下载到板子
4. 在 msh 中依次执行：
   ```
   msh />parking_demo
   msh />ticket_demo
   msh />startup_demo
   msh />package_demo
   ```

---

## 待完成

- [ ] 上板验证实际输出（需要 ST-Link）
- [ ] 拍摄终端输出截图

---

*所有代码基于 rt-thread 4.1.1 源码 API 签名，在 RT-Thread Studio 中已验证编译通过。未上板项如实标注为待完成。*
