# RT-Thread 线程创建与调度笔记

对应工程：`E:\rtthread\workplace_study\01_rtthread_task`，作业文档写在工程根目录 `README.md`。

---

## 一、线程创建函数及参数

### 1. `rt_thread_init()` —— 静态创建

```c
rt_err_t rt_thread_init(struct rt_thread *thread,
                        const char       *name,
                        void            (*entry)(void *parameter),
                        void             *parameter,
                        void             *stack_start,
                        rt_uint32_t       stack_size,
                        rt_uint8_t        priority,
                        rt_uint32_t       tick);
```

| 顺序 | 参数 | 类型 | 说明 |
|---|---|---|---|
| 1 | `thread` | `struct rt_thread *` | 线程控制块 TCB 的地址，**用户提供**（全局/static 变量）。静态创建与动态创建最大的区别：控制块由用户定义，内核不分配 |
| 2 | `name` | `const char *` | 线程名。用于 shell（`list_thread`）、调度钩子、调试识别，可重名 |
| 3 | `entry` | `void (*)(void *)` | 线程入口函数。内核启动线程时调用它，**函数返回即线程结束**（进关闭态） |
| 4 | `parameter` | `void *` | 传给入口函数的参数，即 `entry(parameter)`。不需要传 `NULL`，常用于传结构体指针 |
| 5 | `stack_start` | `void *` | 栈起始地址，**用户提供**（如 `char thread1_stack[1024]`）。需 4 字节对齐（`__attribute__((aligned(4)))`），Cortex-M 的 SP 必须 4 字节对齐 |
| 6 | `stack_size` | `rt_uint32_t` | 栈大小，单位**字节**。要容纳调用链局部变量 + 中断嵌套上下文，不够会栈溢出 |
| 7 | `priority` | `rt_uint8_t` | 优先级，**数字越小越高**，0 最高。范围 `0 ~ RT_THREAD_PRIORITY_MAX-1`（常用 32） |
| 8 | `tick` | `rt_uint32_t` | 时间片，单位 tick。**仅同优先级**线程间轮转生效，不同优先级靠抢占不靠时间片 |

**返回值**：`rt_err_t`，`RT_EOK` 成功，其他为错误码。

**配套销毁**：`rt_thread_detach(&thread1)`——只从内核摘除，**内存还是用户的**。

### 2. `rt_thread_create()` —— 动态创建

```c
rt_thread_t rt_thread_create(const char *name,
                             void      (*entry)(void *parameter),
                             void       *parameter,
                             rt_uint32_t stack_size,
                             rt_uint8_t  priority,
                             rt_uint32_t tick);
```

| 顺序 | 参数 | 类型 | 说明 |
|---|---|---|---|
| 1 | `name` | `const char *` | 线程名，同上 |
| 2 | `entry` | `void (*)(void *)` | 入口函数，同上 |
| 3 | `parameter` | `void *` | 入口参数，同上 |
| 4 | `stack_size` | `rt_uint32_t` | 栈大小，单位字节。**只给大小不给地址**——栈由内核 `rt_malloc` 分配 |
| 5 | `priority` | `rt_uint8_t` | 优先级，规则同 init |
| 6 | `tick` | `rt_uint32_t` | 时间片，规则同 init |

**返回值**：`rt_thread_t`（`struct rt_thread *`），成功返回线程句柄，失败返回 **`RT_NULL`**（堆不够）。

**配套销毁**：`rt_thread_delete(thread2_handle)`——内核在 **idle 线程**里回收（先跑 cleanup 释放栈和 TCB）。

**关键差异**：`create` 比 `init` **少两个参数**——没有 `thread`（控制块）和 `stack_start`（栈地址），这两个由内核 `rt_malloc` 分配。代价：必须开 `RT_USING_HEAP`，且存在堆碎片、运行时分配失败（返回 RT_NULL）的风险。

### 3. 两函数对比

| | `rt_thread_init`（静态） | `rt_thread_create`（动态） |
|---|---|---|
| 参数个数 | 8 个 | 6 个 |
| 控制块 TCB | 用户传入地址（参数 1） | 内核 malloc |
| 栈空间 | 用户传地址 + 大小（参数 5、6） | 内核 malloc，只传大小 |
| 返回值 | `rt_err_t` 状态码 | `rt_thread_t` 句柄（失败 `RT_NULL`） |
| 销毁 | `rt_thread_detach()` | `rt_thread_delete()` |
| 启动 | `rt_thread_startup(&thread1)` | `rt_thread_startup(handle)` |
| 依赖 | 不需要堆 | 需要 `RT_USING_HEAP` |
| 适用 | 资源紧张、栈大小固定、高可靠场景 | 灵活、用完即释放 |

**参数顺序记忆**：
- `init`：`线程对象, 名字, 入口, 参数, 栈地址, 栈大小, 优先级, 时间片`
- `create`：`名字, 入口, 参数, 栈大小, 优先级, 时间片`（= init 去掉开头"线程对象"和"栈地址"）

### 4. 配套 API 速查

| 函数 | 原型（要点） | 作用 |
|---|---|---|
| 启动 | `rt_err_t rt_thread_startup(rt_thread_t thread)` | 初始态 → 就绪态，挂入就绪队列 |
| 挂起 | `rt_err_t rt_thread_suspend(rt_thread_t thread)` | 运行/就绪 → 挂起态（注意：挂起自己后要手动 `rt_schedule()`） |
| 恢复 | `rt_err_t rt_thread_resume(rt_thread_t thread)` | 挂起 → 就绪态 |
| 延时 | `rt_err_t rt_thread_delay(rt_tick_t tick)` / `rt_thread_mdelay(rt_int32_t ms)` / `rt_thread_sleep(rt_tick_t tick)` | 本线程挂起 N 个 tick/ms（内嵌 thread_timer 实现） |
| 让出 | `rt_err_t rt_thread_yield(void)` | 运行 → 就绪，同优先级轮转给下一个 |
| 删除 | `rt_err_t rt_thread_delete(rt_thread_t thread)` | 动态线程：挂 defunct 队列，idle 回收 |
| 脱离 | `rt_err_t rt_thread_detach(rt_thread_t thread)` | 静态线程：脱离内核，内存用户自己管 |
| 当前线程 | `rt_thread_t rt_thread_self(void)` | 拿自己的控制块指针 |
| 查找 | `rt_thread_t rt_thread_find(char *name)` | 按名字查线程 |

### 5. 最小使用模板（对照工程 rt_task_sample.c）

```c
/* 静态线程：控制块和栈都在 BSS，编译期定死 */
struct rt_thread  thread1;
char thread1_stack[1024] __attribute__((aligned(4)));

static void thread_entry1(void *parameter)
{
    for (int i = 0; i < 10; i++)
    {
        rt_kprintf("thread1 count=[%d]\r\n", i);
        rt_thread_mdelay(500);      /* 延时：运行态 → 挂起态 */
    }
    /* return → 关闭态，内核自动回收 */
}

int create_thread_sample(void)
{
    rt_thread_init(&thread1, "thread1", thread_entry1, NULL,
                   thread1_stack, sizeof(thread1_stack), 20, 10);
    rt_thread_startup(&thread1);

    rt_thread_t t2 = rt_thread_create("thread2", thread_entry2, NULL, 1024, 20, 10);
    if (t2 != RT_NULL)
        rt_thread_startup(t2);

    return 0;
}
```

---

## 二、五态与典型 API

![线程状态](E:\笔记\rtthread\RT-Thread线程创建与调度笔记.assets\线程状态-1787047977518-2-1787047978657-4.png)

| 状态 | 宏 | 值 | 进入该态 / 离开该态的典型 API |
|---|---|---|---|
| 初始态 | `RT_THREAD_INIT` | 0x00 | `rt_thread_init/create` 之后；`rt_thread_startup` 离开 |
| 就绪态 | `RT_THREAD_READY` | 0x01 | `rt_thread_startup` / 延时到期 / `rt_thread_resume` / IPC 释放；被调度器选中离开 |
| 运行态 | `RT_THREAD_RUNNING` | 0x03 | 调度器选中；`rt_thread_delay`/`suspend`/时间片到/被抢占离开 |
| 挂起态 | `RT_THREAD_SUSPEND` | 0x02 | `rt_thread_delay/mdelay/sleep` / `rt_thread_suspend` / 等 IPC 无资源；到期/IPC 释放/`rt_thread_resume` 离开 |
| 关闭态 | `RT_THREAD_CLOSE` | 0x04 | 入口函数 return、`rt_thread_delete/detach`；idle 线程回收动态线程 |

`stat` 是位掩码：低 3 位（0x07）为状态，`RT_THREAD_STAT_YIELD`(0x08) 表示时间片用完，与状态位共存。

---

## 三、作业各条与笔记对照

| 作业条目 | 写在哪 |
|---|---|
| 1.1 启动路径 | `README.md` §1.1（调用链图 + 关键点）；启动流程细节见 [[rtthread启动流程]] 笔记 |
| 1.2 自动初始化原理 | `README.md` §1.2（宏展开/段表/扫描函数/BOARD vs APP 对比） |
| 2.1 对象容器与控制块 | `README.md` §2.1（对象容器 + rt_thread 成员表） |
| 2.2 静态 vs 动态 | `README.md` §2.2 + 本笔记"二"（参数逐条对比） |
| 3.1 五态模型 | `README.md` §3.1（状态图 `线程状态.png` + 转换表） |
| 3.2 调度机制 | `README.md` §3.2（抢占 + 时间片轮转，源码 clock.c / scheduler.c） |
