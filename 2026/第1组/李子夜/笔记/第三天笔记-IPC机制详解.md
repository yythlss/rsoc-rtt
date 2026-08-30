# 【RSOC26】第三课：RT-Thread IPC 机制详解——信号量、互斥量与自定义结构体

> 提交人：李子夜（第1组）｜Gitee：@CyberSprout｜日期：2026-08-19

本文基于 RT-Thread **4.1.1** 源码编写，结合 Day3 作业实践中遇到的问题，系统讲解四种 IPC 机制的实现原理、API 使用方式、自定义结构体的正确传参方法，以及初学者最容易踩的坑。

---

## 一、IPC 全景：四种同步/通信机制

RT-Thread 的 IPC（进程间通信/同步）模块提供了四种核心机制：

```
┌─────────────┬──────────────┬─────────────────────┐
│   机制      │   核心用途   │    关键 API         │
├─────────────┼──────────────┼─────────────────────┤
│ 信号量      │ 资源计数 /   │ rt_sem_create/take/ │
│ (Semaphore) │ 单事件通知   │ release/delete      │
├─────────────┼──────────────┼─────────────────────┤
│ 互斥量      │ 独占访问 /   │ rt_mutex_create/    │
│ (Mutex)     │ 优先级继承   │ take/release/delete │
├─────────────┼──────────────┼─────────────────────┤
│ 事件集      │ 多条件组合   │ rt_event_create/    │
│ (Event)     │  AND/OR 仲裁 │ send/recv/delete    │
├─────────────┼──────────────┼─────────────────────┤
│ 消息队列    │ 线程间数据   │ rt_mq_create/send/  │
│ (Mailbox/MQ)│ 传递（拷贝） │ recv/delete         │
└─────────────┴──────────────┴─────────────────────┘
```

如何选？用一个判断流程：

```
需要「资源计数」或「事件通知」？
├── 资源可能 > 1？──→ 计数信号量
└── 资源永远 = 1？──→ 互斥量（需要优先级继承时）/ 信号量（不需要时）

需要「等 N 个条件同时满足」？
└── 事件集（AND 模式）

需要在线程间「传递结构化数据」？
└── 消息队列
```

---

## 二、信号量详解

### 2.1 结构体定义

```c
/* rtdef.h */
struct rt_semaphore
{
    struct rt_ipc_object    parent;   /* 继承 IPC 对象的通用头部 */
    rt_uint16_t             value;    /* 当前计数值 */
    rt_uint16_t             origin;   /* 初始值（用于 rt_sem_control） */
};

typedef struct rt_semaphore *rt_sem_t;
```

- `value`：当前可用资源数，take 时减 1，release 时加 1
- `origin`：创建时的初始值，可通过 `rt_sem_control(sem, RT_IPC_CMD_GET_COUNT, &v)` 读取

### 2.2 静态 vs 动态创建

```c
/* 静态：在 BSS 段中声明 */
struct rt_semaphore my_sem;
rt_sem_init(&my_sem, "my_sem", initial_value, RT_IPC_FLAG_FIFO);

/* 动态：堆分配 */
rt_sem_t sem = rt_sem_create("my_sem", initial_value, RT_IPC_FLAG_FRI);
```

- `RT_IPC_FLAG_FIFO`：等待线程按 FIFO 顺序获得信号量
- `RT_IPC_FLAG_PRIO`：等待线程按优先级从高到低获得

### 2.3 计数信号量的典型陷阱

| 错误做法 | 后果 | 正确做法 |
|---|---|---|
| 释放次数 > 获取次数 | value 溢出，投机线程全通过 | 保证 release 和 take 成对 |
| 用作互斥体但不用互斥量 | 无优先级继承，高优先级线程可能饿死 | 用 `rt_mutex_t` |
| 忘记检查 `rt_sem_create` 返回值 | 信号量为 NULL 时 crash | 每次创建后 `if (sem == RT_NULL) return` |

---

## 三、互斥量详解

### 3.1 互斥量 vs 信号量

| 特性 | 互斥量 | 信号量 |
|---|---|---|
| 所有权 | 有（谁 take 谁 release） | 无（任何线程都可以 release） |
| 优先级继承 | 有 | 无 |
| 递归获取 | 不支持（会死锁） | 支持（每次递减） |
| 适用于 | 独占资源保护 | 资源计数 / 事件通知 |

互斥量带有**所有权**语义：只有取信号的线程才能释放。如果线程 A 的 rt_mutex_take 成功，线程 B 调用 rt_mutex_release 会返回错误 `-RT_ERROR`。

### 3.2 优先级继承

```c
rt_mutex_t m = rt_mutex_create("my_mutex", RT_IPC_FLAG_PRIO);
```

优先级继承解决的核心问题是**优先级反转**：

```
场景：
  线程 H 优先级高（5）——需要等 m
  线程 M 优先级中（20）——持有 m，正在执行
  线程 L 优先级低（30）——正在运行，抢占 M

问题：H 要等 M 释放 m，但 M 被 L 抢占了……H 永远等不到

优先级继承：L 获取 m 时，L 的优先级临时提升到 5，
            直到 L 释放 m 为止，确保 M 能继续运行并释放 m
```

### 3.3 互斥量的典型陷阱

```c
/* 死锁示例：线程 A */
rt_mutex_take(m1, FOREVER);
rt_mutex_take(m2, FOREVER);   /* 阻塞，等 m2 */

/* 同时：线程 B */
rt_mutex_take(m2, FOREVER);
rt_mutex_take(m1, FOREVER);   /* 阻塞，等 m1 */
/* → ABBA 死锁 */
```

**解决**：所有线程按**相同顺序**获取互斥量，或使用 `RT_IPC_CMD_GET_COUNT` 检测超时。

---

## 四、事件集详解

### 4.1 结构体

```c
/* rtdef.h */
struct rt_event
{
    struct rt_ipc_object parent;
    rt_uint32_t         set;      /* 当前有效的事件位（位图） */
    rt_uint8_t          pending;  /* 等待标志数量（AND/OR 不同） */
};

typedef struct rt_event *rt_event_t;
```

### 4.2 等待模式

`rt_event_recv()` 的第三个参数可以组合以下标志：

| 标志 | 含义 |
|---|---|
| `RT_EVENT_FLAG_AND` (0x01) | 所有指定位都必须置位才唤醒 |
| `RT_EVENT_FLAG_OR` (0x02) | 任一指定位置位就唤醒 |
| `RT_EVENT_FLAG_CLEAR` (0x04) | 唤醒时清除对应位，避免重复触发 |

组合示例：
- `AND | CLEAR`：全部事件到达且只触发一次
- `OR`：任一事件到达即触发，且不清除

### 4.3 典型用法：启动条件门控

```c
/* 模块 A：完成初始化后 */
rt_event_send(event, EVENT_A_READY);

/* 模块 B：等待 A + B + C 全部就绪 */
rt_event_recv(event, EVENT_A|EVENT_B|EVENT_C,
              AND|CLEAR, FOREVER, &recved);
```

---

## 五、消息队列详解

### 5.1 消息队列 vs 邮箱

RT-Thread 有两种队列类 IPC：

| | 消息队列 | 邮箱 |
|---|---|---|
| 数据大小 | 可变长（每条消息固定大小） | 4 字节（rt_ubase_t） |
| 队列满 | 发送阻塞 / 返回错误 | 同上 |
| 典型场景 | 结构化数据传递（如快递信息） | 事件通知 / 指针传递 |

**4.1.1 中 `rt_mailbox_create` 存在，但消息队列（`rt_mq_create`）是更通用的数据传递方式。**

### 5.2 消息大小不能动态改变

消息队列在创建时指定 `msg_size`，之后每条消息的大小**必须严格等于**此值。如果发送 `sizeof(struct package_msg)` 而接收方用 `sizeof(int)`，数据会错位。

---

## 六、自定义结构体传入线程的正确方式

### 6.1 问题：void * 传参的隐藏陷阱

`rt_thread_create` 的第三个参数是 `void *parameter`，传入任何指针都可以，但有一个**经典错误**：

```c
/* ❌ 错误：把整数强制转型为指针 */
rt_thread_create("car", car_entry, (void *)1, ...);

/* 在线程中恢复时：
   (rt_uint32_t)param  可能得到 1
   但如果 param 不是合法指针，某些 ARM 编译器会做 sign-extend
   产生意外的大数
*/

/* ✅ 正确：用 intptr_t 安全转换 */
#include <stdint.h>
rt_thread_create("car", car_entry, (void *)(intptr_t)1, ...);
/* 或者 */
rt_thread_create("car", car_entry, (void *)(rt_uintptr_t)1, ...);
```

### 6.2 多参数用结构体

如果线程需要多个参数，将其打包为结构体：

```c
/* ❌ 形式不佳：用全局变量传递额外参数 */
static int g_module_delay;
rt_thread_create("mod", init_thread, (void *)1, ...);
g_module_delay = 5000;

/* ✅ 正确：结构体封装全部参数 */
struct module_param {
    const char *name;
    rt_uint32_t delay_ms;
    rt_uint32_t event_flag;
};
static struct module_param param = {"storage", 5000, EVENT_STORAGE_READY};
rt_thread_create("storage", init_thread, &param, ...);

/* 在线程中解包 */
void init_thread(void *p)
{
    struct module_param *mp = p;
    rt_kprintf("%s delay=%d\n", mp->name, mp->delay_ms);
}
```

### 6.3 结构体成员的字节对齐

注意 `struct package_msg`：

```c
struct package_msg
{
    rt_uint32_t id;       /* 4B 偏移 0 */
    rt_uint8_t  region;   /* 1B  偏移 4 */
                          /* 1B  padding   偏移 5 ~ 7 */
    rt_uint16_t weight;   /* 2B  偏移 8 */
};
```

`sizeof(struct package_msg)` = **12 字节**（不是 4+1+2=7）。这是因为 ARM 的 `__attribute__((aligned(4)))` 要求每个成员按最大成员对齐。

**关键**：`rt_mq_create` 的 `msg_size` 必须传 `sizeof(struct package_msg)`，不能传 `7`。

可以通过编译后确认：
```c
rt_kprintf("sizeof package_msg = %d\n", sizeof(struct package_msg));
```

---

## 七、常见问题与解决方案

### Q1：信号量 count 溢出

**现象**：`rt_sem_release` 调用过多，`value` 从最大值（65535）跳回 0，所有等待线程被意外唤醒。

**原因**：release 没有对应的 take。常见于错误处理路径中先 release 再 goto cleanup 又在 cleanup 中 release 一次。

**解决**：
```c
/* 方案 A：条件释放 */
if (resource_acquired)
    rt_sem_release(sem);

/* 方案 B：常量跟踪 */
static int held = 0;
take:   held++;  take;
release: held--; sem_release;
assert(held >= 0);
```

### Q2：互斥量持有期间调用了阻塞 API

**现象**：高优先级线程获取互斥量后在临界区中调用了 `rt_thread_delay` 或 `rt_sem_take`，导致整个系统卡死。

**原因**：互斥量是**不可递归**的。持有互斥量时进入阻塞会让互斥量一直不释放，其他线程永远在等。

**解决**：
```c
/* ❌ 错误：互斥量持有期间休眠 */
rt_mutex_take(m, FOREVER);
rt_thread_delay(1000);    /* 其他线程卡住 */
rt_mutex_release(m);

/* ✅ 正确：分割临界区和耗时操作 */
rt_mutex_take(m, FOREVER);
copy_shared_data();       /* 只做最小必要操作 */
rt_mutex_release(m);
process_data();           /* 耗时操作在临界区外 */
```

### Q3：MQ 发送阻塞导致发件线程挂死

**现象**：发送线程写入消息队列后阻塞，分拣线程已退出，队列再也没人读。

**原因**：`rt_mq_send` 在队列满时会阻塞等待（如果用 `rt_mq_send_wait`），但接收方不再运行。

**解决**：发送方使用**非阻塞**发送，队列满时记录失败计数：
```c
if (rt_mq_send(mq, &msg, sizeof(msg)) != RT_EOK)
{
    send_failed_count++;
    /* 丢弃或重试，不阻塞 */
}
```

### Q4：事件位被过早清除

**现象**：多个线程使用同一个事件集，一个线程用 `AND|CLEAR` 读取后，另一个线程的事件位已经丢失。

**原因**：`RT_EVENT_FLAG_CLEAR` 是全局清除，影响所有等待者。

**解决**：
- 方案 A：使用 `OR` 模式，各模块独立消费自己的事件位
- 方案 B：每个模块用自己的事件集，避免共享

### Q5：创建重复执行同一个 MSH 命令

**现象**：第二次执行 `msh />parking_demo` 时输出乱码或 crash。

**原因**：第一次运行创建的线程退出后，全局句柄未清零，第二次 `rt_sem_create` 对同名信号量返回错误或覆盖旧对象，残留线程还在运行。

**解决**：守卫检查 + 清理：
```c
static rt_sem_t sem = RT_NULL;

void my_demo(...)
{
    if (sem != RT_NULL) {
        rt_kprintf("already running\n");
        return;
    }
    sem = rt_sem_create(...);
    /* ... */
    /* 结束后清零 */
    rt_sem_delete(sem);
    sem = RT_NULL;
}
```

---

## 八、自定义结构体的完整示例模板

以下结构体可以在任何 IPC 对象间传递（信号量 / 队列的参数、线程参数等）：

```c
/* 自定义任务参数 */
struct task_info
{
    rt_uint32_t task_id;       /* 任务编号 */
    const char *task_name;     /* 任务名称（只读，生命周期 > 线程） */
    void       *user_data;     /* 用户数据指针 */
    rt_uint16_t interval_ms;   /* 执行间隔 */
    rt_uint8_t  priority;      /* 优先级 */
};

/* 线程入口中解包 */
static void task_thread(void *param)
{
    struct task_info *ti = (struct task_info *)param;

    while (1)
    {
        rt_kprintf("[%s] task %u running\n", ti->task_name, ti->task_id);
        rt_thread_mdelay(ti->interval_ms);
    }
}

/* 创建线程 */
struct task_info ti = {1, "worker", NULL, 1000, 20};
rt_thread_create("worker", task_thread, &ti, 1024, ti.priority, 10);
```

**结构体放在什么存储期？**

| 方式 | 生命周期 | 适用 |
|---|---|---|
| 局部变量（函数内 `static`） | 程序运行期间 | 大多数场景 |
| 全局变量 | 程序运行期间 | 单一实例 |
| 堆分配（`rt_malloc`） | 手动 free | 多实例，动态增减 |

**注意**：传递给线程的结构体指针所指的存储，必须**在线程运行期间保持有效**。绝不要传栈上局部变量的地址：

```c
void create_my_thread(void)
{
    struct task_info ti;        /* 栈上！函数返回后释放 */
    rt_thread_create("t", thread_entry, &ti, ...);  /* ❌ 悬垂指针 */
}

/* ✅ 改为 static */
static struct task_info ti;
```

---

## 九、完整 API 速查表

### 9.1 信号量（Semaphore）

| API | 作用 | 返回值 |
|---|---|---|
| `rt_sem_create(name, value, flag)` | 动态创建 | `RT_NULL` = 失败 |
| `rt_sem_init(sem, name, value, flag)` | 静态初始化 | `RT_EOK` / `-RT_ERROR` |
| `rt_sem_take(sem, timeout)` | 获取信号量 | `RT_EOK` / `-RT_ETIMEOUT` / `-RT_ERROR` |
| `rt_sem_release(sem)` | 释放信号量 | `RT_EOK` |
| `rt_sem_delete(sem)` | 删除信号量 | `RT_EOK` |
| `rt_sem_control(sem, cmd, arg)` | 查询/控制 | `RT_EOK` |

`timeout` 选项：
- `RT_WAITING_FOREVER`（-1）：永久阻塞
- `0`：非阻塞，立即返回
- `> 0`：等待指定 tick 数

### 9.2 互斥量（Mutex）

| API | 作用 | 返回值 |
|---|---|---|
| `rt_mutex_create(name, flag)` | 动态创建 | `RT_NULL` = 失败 |
| `rt_mutex_init(mutex, name, flag)` | 静态初始化 | `RT_EOK` |
| `rt_mutex_take(mutex, timeout)` | 获取 | `RT_EOK` / `-RT_ETIMEOUT` / `-RT_ERROR` |
| `rt_mutex_trytake(mutex)` | 尝试获取（不阻塞）| `RT_EOK` / `-RT_EBUSY` |
| `rt_mutex_release(mutex)` | 释放 | `RT_EOK` |
| `rt_mutex_delete(mutex)` | 删除 | `RT_EOK` |

### 9.3 事件集（Event）

| API | 作用 | 返回值 |
|---|---|---|
| `rt_event_create(name, flag)` | 创建 | `RT_NULL` = 失败 |
| `rt_event_send(event, set)` | 置位 | `RT_EOK` |
| `rt_event_recv(event, set, flag, timeout, &recved)` | 等待 | `RT_EOK` / `-RT_ETIMEOUT` |
| `rt_event_delete(event)` | 删除 | `RT_EOK` |

`flag` 组合：
- `EVENT_FLAG_AND`：所有位必须都置位
- `EVENT_FLAG_OR`：任一位置位
- `EVENT_FLAG_CLEAR`：消费后清除（防止重触发）

### 9.4 消息队列（Message Queue）

| API | 作用 | 返回值 |
|---|---|---|
| `rt_mq_create(name, msg_size, max_msgs, flag)` | 动态创建 | `RT_NULL` |
| `rt_mq_init(mq, name, msgpool, msg_size, pool_size, flag)` | 静态初始化 | `RT_EOK` |
| `rt_mq_send(mq, buffer, size)` | 发送（满时阻塞） | `RT_EOK` |
| `rt_mq_send_wait(mq, buffer, size, timeout)` | 发送（可超时） | `RT_EOK` / `-RT_ETIMEOUT` |
| `rt_mq_urgent(mq, buffer, size)` | 紧急发送（插队到队头） | `RT_EOK` |
| `rt_mq_recv(mq, buffer, size, timeout)` | 接收 | `RT_EOK` / `-RT_ETIMEOUT` |

`rt_mq_create` 参数：
- `msg_size`：每条消息的大小（通常 `sizeof(struct)`）
- `max_msgs`：队列容量（最多存几条消息）

---

## 十、推荐的 IPC 对象生命周期管理

```
创建阶段的通用模板：

1. 全局声明句柄（初始值为 RT_NULL）
2. MSH 入口函数开头检查句柄 != RT_NULL → 拒绝重复执行
3. 按顺序创建 IPC 对象，每个创建后检查返回值
4. 任一创建失败 → 清理已创建的对象 → 清零句柄 → return
5. 创建线程并启动
6. 线程自然退出后（通过 done 信号量或调度退出）
7. 删除 IPC 对象 → 清零句柄
```

---

## 十一、扩展阅读（源码路径）

| 文件 | 内容 |
|---|---|
| `rt-thread/src/ipc/semaphore.c` | 信号量完整实现 |
| `rt-thread/src/ipc/mutex.c` | 互斥量 + 优先级继承 |
| `rt-thread/src/ipc/event.c` | 事件集 set/recv 实现 |
| `rt-thread/src/ipc/messagequeue.c` | 消息队列 send/recv 实现 |
| `rt-thread/include/rtdef.h` | 全部 IPC 结构体定义 |
| `rt-thread/include/rtthread.h` | 全部 IPC API 声明（351-450 行） |
