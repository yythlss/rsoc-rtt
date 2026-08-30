# RT-Thread IPC（线程间同步与通信）学习笔记

> 本文基于工程 `E:\rtthread\workplace_study\02_rtthread_IPC\applications` 下的 4 个示例整理：
> - `parking_demo.c` —— 计数信号量（semaphore）
> - `tick_demo.c` —— 互斥量（mutex，含“故意不加锁”对比实验）
> - `startup_demo.c` —— 事件集（event）
> - `package_demo.c` —— 消息队列（message queue）
>
> 官方文档（建议配合阅读）：
> - IPC 上篇：<https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/ipc1/ipc1>
> - IPC 下篇：<https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/ipc2/ipc2>
>
> 线程使用的注意点另见：`RT-Thread_线程注意笔记.md`

---

## 〇、通用套路（四个示例都一样）

1. 所有 IPC 对象和线程都要**检查创建结果**（判 `RT_NULL`）。
2. 程序通过 **MSH 命令**启动：`MSH_CMD_EXPORT(cmd, desc);`。
3. 每个练习至少 **两个线程** + 一个 IPC 对象。
4. 线程入口函数结束时 **直接 `return`** 即可，系统自动回收（见第五节）。
5. 动态线程的栈、TCB、队列缓冲区都从**堆**分配，堆边界配错会崩（见第六节）。

---

## 一、计数信号量 semaphore —— `parking_demo.c`

### 场景
停车场 3 个车位，5 辆车抢，最多同时进 3 辆。

### 关键 API
```c
rt_sem_t rt_sem_create(const char *name, rt_uint32_t value, rt_uint8_t flag);
rt_err_t rt_sem_take(rt_sem_t sem, rt_int32_t timeout);   // value--，为0则阻塞
rt_err_t rt_sem_release(rt_sem_t sem);                    // value++
```
- `value = 3` 表示初始剩余车位；`RT_IPC_FLAG_PRIO` 表示按优先级唤醒等待线程。
- `rt_sem_take(sem, RT_TICK_PER_SECOND*2)`：最多等 2 秒，超时返回 `-RT_ETIMEOUT`。

### 代码要点（`parking_demo.c`）
- 创建初始值 3 的信号量：`parking_demo.c:53`。
- 5 个车辆线程循环 `take` → 进入 → 延时停留 → `release` 离开（`parking_demo.c:82-112`）。
- 共享变量 `use_pack_space` 用 `rt_enter_critical()/rt_exit_critical()` 临界区保护（`parking_demo.c:94-97,100-103`）。

### 思考题：为什么用计数信号量而不是互斥量？
- 互斥量 = **1 个资源、谁拿着谁独占**（value 只能是 0/1，强调“持有者释放”）。
- 计数信号量 = **N 个同类资源**，不绑定持有者，任何线程 `release` 都 +1。
- 车位有 3 个、且“进/出”是不同动作（不是同一段临界区反复进出），用计数信号量最自然。

> ⚠️ 小瑕疵（可改）：`parking_demo()` 里信号量创建失败时只打印没 `return -RT_ERROR`（`parking_demo.c:54-57`）；且 5 个线程只保存了最后一个 `pack_space_thread` 句柄，重复运行不易清理。作业里建议补上失败返回 + 句柄数组/重复运行保护。

---

## 二、互斥量 mutex —— `tick_demo.c`

### 场景
两个窗口卖 20 张票，同一变量 `window_ticket_count` 被两个线程改，必须互斥。

### 关键 API
```c
rt_mutex_t rt_mutex_create(const char *name, rt_uint8_t flag);
rt_err_t rt_mutex_take(rt_mutex_t mutex, rt_int32_t timeout);
rt_err_t rt_mutex_release(rt_mutex_t mutex);
```

### 对比实验（作业要求）
- **先不加锁**：`tick_demo.c:98-128` 整段 mutex 调用被注释掉。两窗口同时 `if(count>0){count--;}` 会竞态：可能卖出相同票号、或 `count` 变负。
- **再加锁**：把 `rt_mutex_take`/`rt_mutex_release` 解开注释，把“读-改-写”整段包起来，20 张票不重不漏。

### 思考题：为什么只给 `ticket_count--` 加锁仍可能不安全？
危险区间是 **“读取余票 → 判断 >0 → 减 1” 这一段**，不是一个 `count--` 指令。若只锁 `count--`：
```
窗口A读count=1 → 窗口B读count=1 → A减成0 → B减成-1（重复/超卖）
```
所以必须锁**整段“检查+修改”逻辑**。

> ⚠️ 本示例当前是“不加锁”版本，用来演示竞态。提交作业前记得补上 mutex 版本做对比。

---

## 三、事件集 event —— `startup_demo.c`

### 场景
网络/传感器/存储三个模块初始化完成后，主业务线程才能启动。

### 关键宏与 API
```c
#define EVENT_NET_READY     (1U << 0)
#define EVENT_SENSOR_READY  (1U << 1)
#define EVENT_STORAGE_READY (1U << 2)

rt_event_t rt_event_create(const char *name, rt_uint8_t flag);
rt_err_t rt_event_send(rt_event_t event, rt_uint32_t set);
rt_err_t rt_event_recv(rt_event_t event, rt_uint32_t set,
                       rt_uint8_t option, rt_int32_t timeout, rt_uint32_t *recved);
```
- `option = RT_EVENT_FLAG_AND`：set 里的位**全部到齐**才唤醒。
- `option |= RT_EVENT_FLAG_CLEAR`：唤醒后自动清除这些位（避免重复触发）。

### 代码要点（`startup_demo.c`）
- 三个初始化线程延时不同时间后 `rt_event_send` 对应位：`startup_demo.c:112-149`。
- 主业务线程用 `AND` 等三事件齐：`startup_demo.c:156-163`。
- 超时（storage 5s，业务只等 3s）会先打 `wait modules timeout`，再继续等：`startup_demo.c:169-177`。

### 思考题：用 3 个信号量 vs 事件集？
- 3 个信号量也能表达“三个都就绪”，但要**分别 take 三个**，且难以表达“任选/任意组合”语义。
- 事件集一个 32 位变量可表示 **32 个独立事件 + AND/OR 逻辑**，更省资源、语义更清晰。

### ⚠️ 该示例曾踩的坑（已修）
- 原来线程里调用 `rt_thread_delete(rt_thread_self())` 删除正在运行的自己 → 释放了正在用的栈 → `rt_smem_free` 断言 + 硬fault。**改正：线程直接 `return`**，系统自动 `rt_thread_exit()` 回收。

## 四、消息队列 message queue —— `package_demo.c`

### 场景
两个发件线程产生快递消息，一个分拣线程从队列取出并按区域分类。

### 关键 API
```c
rt_mq_t rt_mq_create(const char *name, rt_size_t msg_size,
                     rt_size_t max_msgs, rt_uint8_t flag);
rt_err_t rt_mq_send(rt_mq_t mq, void *buffer, rt_size_t size);       // 非阻塞，满返回 -RT_EFULL
rt_err_t rt_mq_recv(rt_mq_t mq, void *buffer, rt_size_t size, rt_int32_t timeout);
```

### 消息结构
```c
struct package_msg { rt_uint32_t id; rt_uint8_t region; rt_uint16_t weight; };
```
> 注意：`region` 用 `enum`（占 4 字节），结构体有对齐，但创建/发送都用 `sizeof` 一致即可。

### 代码要点 / 正确做法（`package_demo.c`）
1. 队列容量 5：`package_demo.c:58-61`。
2. 两个发件线程各发 6 条：**前 5 条真实快递 + 第 6 条 `region = PKG_REGION_END(0)` 结束消息**（`package_demo.c:128-176`）。
3. **队列满不丢消息、要重试**：`rt_mq_send` 返回非 `RT_EOK` 时 `fail++`、`rt_thread_mdelay(10)` 后用**同一个 i 重发**（`package_demo.c:147-158`）。否则结束消息可能丢失 → 分拣线程永远等不到第二条结束消息 → 卡死。
4. 分拣线程用 `finish_cnt` 计数结束消息，**收到 2 条才打印统计并 `break`**（`package_demo.c:178-202`）。不能收到第 1 条就退，否则丢掉另一发件线程的快递。
5. id 用 `2000+i` / `1000+i` 分段，**保证不重复**。
6. 统计变量在 `package_demo()` 开头**清零**，否则重复运行会累加（`package_demo.c:69`）。
7. 分拣线程退出前 `rt_mq_delete`/`rt_mutex_delete` 并把手柄清 `RT_NULL`，支持重复运行（文件末尾）。

### 统计要求
- `total`：真实快递总数（不含结束消息）。
- `east/south/north`：各区域数量；`weight`：总重量；`send_fail`：队列满重试次数。

### 常见的几个错误写法（都来自调试过程）
| 错误 | 现象 | 原因 |
|------|------|------|
| 队列满时只记 `fail` 不重试 | 快递丢失 / 结束消息丢失 → 分拣卡死 | `rt_mq_send` 非阻塞，满了直接返回，必须重试才能发满 |
| 收到第 1 条结束消息就 `break` | `total` 只有 ~5，丢另一线程的快递 | 要 `finish_cnt==2` 才退出 |
| id 用 `rand()%1000` 两线程同范围 | 编号重复 | 用固定段 `1000+i` / `2000+i` 区分 |
| 统计变量不清零 | 第二次运行数字累加 | `package_demo()` 开头全置 0 |

---

## 五、线程生命周期与回收（来自 `RT-Thread_线程注意笔记.md`）

1. **禁止 `rt_thread_delete(rt_thread_self())`**：线程正在运行、还在用自己这块栈，删自己会立刻释放正在使用的栈 → `rt_smem_free` 断言 + 硬fault。正确做法是线程入口直接 `return`，系统自动 `rt_thread_exit()` 回收。
2. **线程退出后资源在空闲线程（idle）释放**：动态线程 `return` 后不会立刻 `rt_free` 自己的栈/TCB，而是挂入僵尸队列，由空闲线程（tidle0）真正释放。所以**空闲线程栈要留够**，否则线程正常退出时释放资源会把 tidle 栈爆掉（现象：`thread:tidle0 stack overflow`）。
3. 若确实需要动态删除线程，应由**其他线程**调用 `rt_thread_delete(handle)`，而不是删自己。

---

## 六、调试血泪：硬fault + `0xdeadbeef` 与堆边界（来自 package_demo 排错）

`package_demo` 曾反复硬fault，fault 输出里 `r04~r11` 全是 `0xdeadbeef`，而且 fault 线程在 `main` / `package1_thread` 之间变（布局一变就换受害者）。结论：

- `0xdeadbeef` 是 RT-Thread 给**新线程栈填充的哨兵值**。`r4~r11` 全是它，说明**某线程的栈被另一块“新线程栈”覆盖重写**了。
- 根因：**`board.c` 里堆（heap）的结束地址划得太靠 RAM 顶部，和 `main` 线程栈（靠 RAM 顶部向下生长）重叠**。于是 `rt_malloc` 分配出来的线程栈/队列缓冲区互相踩、也踩 `main` 栈，把对方栈里的 deadbeef 写进活线程的寄存器现场 → 一恢复上下文就硬fault。
- **改法（board.c）**：把 `RT_HW_HEAP_END` 往下收，给 `main` 线程栈留出余量（例如 RAM 顶 `0x20010000`、main 栈 0x1000，则 heap end 设为 `0x2000F000`）。线程栈大小调大（如 4096）反而更挤、更易撞，正常练习用 1024 即可。

### 硬fault 排查清单
1. 看 `hard fault on thread: xxx` 和 `r04~r11`：全是 `0xdeadbeef` → 栈/堆被踩（内存重叠或越界）。
2. 看各线程 `stack size` / `max used`：接近 100% 是栈溢出；都很低却崩 → 多半是堆重叠/越界，不是本线程栈小。
3. 先确认 `tidle0` 栈够不够（第五节）；再检查 `board.c` 的 `RT_HW_HEAP_BEGIN/END` 是否与 `main` 栈重叠。
4. 必要时打开 `RT_DEBUG` / 栈溢出检查（`RT_USING_OVERFLOW_CHECK`）让系统在溢出时主动打印。

---

## 七、四件套速查

| IPC | 创建 | 获取/发送 | 释放/接收 | 典型用途 |
|-----|------|-----------|-----------|----------|
| 计数信号量 `rt_sem_t` | `rt_sem_create` | `rt_sem_take` | `rt_sem_release` | N 个同类资源（车位/连接数） |
| 互斥量 `rt_mutex_t` | `rt_mutex_create` | `rt_mutex_take` | `rt_mutex_release` | 保护“读-改-写”临界区 |
| 事件集 `rt_event_t` | `rt_event_create` | `rt_event_send` | `rt_event_recv(AND/OR+CLEAR)` | 多条件齐/任意组合就绪 |
| 消息队列 `rt_mq_t` | `rt_mq_create` | `rt_mq_send`（非阻塞） | `rt_mq_recv` | 线程间传递结构体数据 |

> 所有对象都要判 `RT_NULL`；所有命令用 `MSH_CMD_EXPORT` 导出；所有线程结束时 `return` 而非删自己。

