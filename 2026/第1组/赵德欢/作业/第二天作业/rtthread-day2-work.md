# 1.STM32F407 启动流程与 RT-Thread 引导机制

## 1.1 为何需要自动化初始化
传统 RTOS/裸机工程里，驱动初始化通常靠人工在 `main()` 里逐个调用，需要用脑子记忆，新增驱动要改 main。而在rtthread内有两个函数`INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT` `声明及注册`，只要把初始化函数用宏标注，链接器在编译期就把它排进一个特定段，运行期由内核自动遍历执行，main 里什么都不用写。

---
### 1.2 宏展开机制
所有 `INIT_*_EXPORT` 宏都源于同一个底层宏 `INIT_EXPORT`：
```c
/* rtdef.h —— 六级初始化优先级，数字越小越靠前执行 */
#define INIT_BOARD_EXPORT(fn)      INIT_EXPORT(fn, "board")       /* 板级：最早 */
#define INIT_PREV_EXPORT(fn)       INIT_EXPORT(fn, "pre")
#define INIT_DEVICE_EXPORT(fn)     INIT_EXPORT(fn, "device")
#define INIT_COMPONENT_EXPORT(fn)  INIT_EXPORT(fn, "component")
#define INIT_ENV_EXPORT(fn)        INIT_EXPORT(fn, "env")
#define INIT_APP_EXPORT(fn)        INIT_EXPORT(fn, "app")         /* 应用：最晚 */

/* 真正的"魔法"定义 */
#define INIT_EXPORT(fn, level) \
    RT_USED const rt_init_fn_t __rt_init_##fn##_##level \
        SECTION("rt_init_fn." level) = fn
```
**其中**，INIT_BOARD_EXPORT(fn)` 做的事情，就是把 `fn` 这个函数指针，塞进 Flash 里一个叫 `rt_init_fn.board` 的小抽屉，和所有同级别的函数指针排在一起。

---

### 1.3 链接脚本布局（段的物理排序）

链接脚本（`link.lds` / `rtthread.lds`）负责把这些分散在各 `.o` 文件里的 `rt_init_fn.*` 段**聚到一起并按级别排序**

链接器还会生成两个边界符号：`__rt_init_fn_start`（段首）和 `__rt_init_fn_end`（段尾）。**这两个符号之间的内存，本质上就是一个"函数指针数组"**

---
### 1.4 运行期遍历（谁来"按抽屉执行"）
```c
/* components.c */

/* 入口 A：只跑 board 段，在 rt_hw_board_init() 内部、调度器启动之前 */
void rt_components_board_init(void)
{
    volatile const rt_init_fn_t *fn_ptr;

    for (fn_ptr = &__rt_init_fn_board_start;
         fn_ptr < &__rt_init_fn_board_end;
         fn_ptr++)
    {
        (*fn_ptr)();          /* 逐个取出函数指针并调用 */
    }
}

/* 入口 B：跑剩下五段（pre→device→component→env→app），在 main 线程入口里、用户 main 之前 */
void rt_components_init(void)
{
    volatile const rt_init_fn_t *fn_ptr;

    for (fn_ptr = &__rt_init_fn_pre_start;
         fn_ptr < &__rt_init_fn_app_end;
         fn_ptr++)
    {
        (*fn_ptr)();
    }
}
```
![[Pasted image 20260818214306.png|394]]


---
### 1.5 INIT_BOARD_EXPORT 与 INIT_APP_EXPORT 的本质区别

| 维度 | `INIT_BOARD_EXPORT` | `INIT_APP_EXPORT` |
|---|---|---|
| 所属段 | `rt_init_fn.board` | `rt_init_fn.app` |
| 调用入口 | `rt_components_board_init()` | `rt_components_init()` |
| 调用时机 | **调度器启动之前**（中断仍关闭） | **调度器启动之后**、用户 `main()` **之前** |
| 线程环境 | 无线程概念，不能阻塞/睡眠 | 已是线程上下文，可用 `rt_thread_mdelay`、信号量等 |
| 典型用途 | 时钟、GPIO pinmux、早期 UART | 网络协议栈、文件系统、应用级组件 |
| 失败后果 | 系统根本起不来 | 仅该应用组件异常，系统仍在跑 |
**执行顺序保证**：因为 `board` 段在 Flash 中排在 `component` 段之前，且 `rt_components_board_init` 先于 `rt_components_init` 被调用，所以 `rt_hw_usart_init` 一定先于 `rt_hw_console_init` 执行。

所以，他们的本质区别是中断的时机不同，而这也决定了他们能做什么，能不能使用线程。

---
## 2. 内核基础与对象模型
RT-Thread 用 **纯 C 语言**模拟面向对象，核心为”对象头 + 类型标签“：
- 所有内核对象（线程、信号量、互斥量、定时器、设备、邮箱……）的 `struct` **第一个成员都是 `struct rt_object`**，提供统一的名字、类型、链表节点。
- 通过 `type` 字段区分对象种类；通过 `list` 双向链表节点，把对象挂进"**对象容器**"（内核用 `rt_object_container` 数组按类型管理，共 `RT_Object_Class_Unknown` 到各类别），实现类似"基类容器"的效果。
- 访问对象时先检查 `type`，再按具体类型强转回派生结构体
```c
/* rtdef.h：所有内核对象的公共头部 */
struct rt_object
{
    char       name[RT_NAME_MAX];  /* 对象名字 */
    rt_uint8_t type;               /* 对象类型(枚举) */
    rt_uint8_t flag;               /* 对象标志 */
    rt_list_t  list;               /* 挂到对象容器的链表节点 */
};

/* 线程控制块：第一成员就是 rt_object，等价于"继承" */
struct rt_thread
{
    struct rt_object parent;       /* 继承的公共头部 */
    rt_list_t  tlist;              /* 调度器用的就绪/挂起链表节点 */
    /* ... 线程私有字段见 2.2 ... */
};
```

---

### 2.2 线程控制块 `struct rt_thread` 关键成员

| 成员 | 类型 | 作用 |
|---|---|---|
| `stat` | `rt_uint8_t` | **线程状态机核心**：`RT_THREAD_INIT`(初始) / `RT_THREAD_READY`(就绪) / `RT_THREAD_RUNNING`(运行) / `RT_THREAD_SUSPEND`(挂起) / `RT_THREAD_CLOSE`(关闭)。调度器据此决定是否参与调度 |
| `current_priority` | `rt_uint8_t` | **当前优先级**（0 最高）。调度时比较此值选最高优先级线程运行；可被 `rt_thread_control` 动态修改 |
| `init_priority` | `rt_uint8_t` | 初始优先级，作为"基准"。当优先级被临时提升（如优先级翻转防护）后，可恢复回此值 |
| `init_tick` | `rt_uint32_t` | **时间片初始值**（单位：tick）。同优先级线程轮转时，每次获得的 CPU 时间长度 |
| `remaining_tick` | `rt_uint32_t` | **剩余时间片**。SysTick 每 tick 递减；归零后线程被移到同优先级就绪队尾，并重置为 `init_tick` |
| `sp` | `void *` | **当前栈指针**。上下文切换时保存/恢复现场的关键——指向该线程在被切出时各寄存器压栈的位置 |
| `entry` | `void *` | 线程入口函数指针 |
| `parameter` | `void *` | 传给 `entry` 的参数 |
| `stack_addr` / `stack_size` | `void *` / `rt_uint32_t` | 线程栈的起址与大小（静态或动态分配，见 2.3） |
| `tlist` | `rt_list_t` | 调度器链表节点：线程在**就绪表/挂起表/定时器超时表**中的挂接点 |
| `thread_timer` | `struct rt_timer` | 线程内置定时器：用于 `rt_thread_delay()` / `rt_thread_suspend` 的超时唤醒 |
| `error` | `rt_err_t` | 最近一次操作的错误码 |
| `cleanup` | 函数指针 | 线程退出时（状态变 `CLOSE`）的清理回调 |
> **调度器视角**：`stat` 决定"是否就绪"、`current_priority` 决定"被谁抢占"、`init_tick`/`remaining_tick` 决定"同优先级能跑多久"、`sp` 决定"切回来时从哪恢复现场"——这四个字段是调度算法直接读写的对象。

---

### 2.3 静态对象 vs 动态对象

#### 1. 静态线程创建（编译期确定内存）

```c
/* 1. 定义线程控制块（TCB），由编译器在静态区分配内存 */
static struct rt_thread led_thread;

/* 2. 定义线程栈，大小为 512 字节，同样在静态区 */
static rt_uint8_t led_stack[512];

/* 3. 初始化线程（此时线程处于“初始态”，不会立刻运行） */
rt_thread_init(
    &led_thread,                   /* 线程控制块指针 */
    "led",                         /* 线程名称，字符串，用于调试识别 */
    led_entry,                     /* 线程入口函数，即线程要做的事情 */
    RT_NULL,                       /* 传递给入口函数的参数（无参数则填 RT_NULL） */
    &led_stack[0],                 /* 栈起始地址（数组首元素地址） */
    sizeof(led_stack),             /* 栈大小（单位：字节） */
    RT_THREAD_PRIORITY_MAX - 2,    /* 线程优先级：数字越小越高，这里设为次低优先级 */
    20                             /* 时间片：同优先级轮转时，每次运行 20 个 tick */
);

/* 4. 启动线程：将线程从“初始态”转为“就绪态”，等待调度器调度 */
rt_thread_startup(&led_thread);
```

---

### 2. 动态线程创建（运行期从堆分配内存）

c

```c
/* 1. 创建线程（系统自动从堆中分配控制块和栈空间） */
rt_thread_t tid = rt_thread_create(
    "led",                         /* 线程名称 */
    led_entry,                     /* 线程入口函数 */
    RT_NULL,                       /* 传递给入口函数的参数 */
    512,                           /* 栈大小（单位：字节） */
    RT_THREAD_PRIORITY_MAX - 2,    /* 线程优先级：次低 */
    20                             /* 时间片：20 个 tick */
);

/* 2. 检查线程是否创建成功 */
if (tid != RT_NULL)
{
    /* 3. 启动线程（转为就绪态） */
    rt_thread_startup(tid);
}
else
{
    /* 创建失败，通常是堆内存不足 */
    rt_kprintf("create led thread failed!\n");
}
```

| 维度 | 静态对象 | 动态对象 |
|---|---|---|
| 分配位置 | 编译期确定的 `.bss`/全局区（或你指定的静态数组） | 运行时从**内存堆（heap）** 动态申请 |
| 典型 API | `rt_thread_init()`、`rt_sem_init()`（对象由调用者提供） | `rt_thread_create()`、`rt_sem_create()`（内核帮你 malloc） |
| 生命周期 | 与程序同寿，或手动 `rt_thread_detach()` 脱离 | `rt_thread_delete()` / `rt_sem_delete()` 释放回堆 |
| 释放机制 | 无（或 detach 仅从容器摘除，内存不回收） | 释放回堆，可被后续分配复用 |
| 失败风险 | 编译期就占好内存，**无运行时分配失败** | 堆不足时返回 `RT_NULL`，需判空 |
| 内存碎片 | 无 | 长期运行可能产生碎片 |

---

### 2.4 实践建议
 实践建议：关键任务线程（控制环、看门狗、通信）、中断相关对象用静态；非关键、数量随业务波动的临时线程可用动态。
 
在工控、轨交、医疗、航天等**高可靠（safety-critical）场景**，通常禁止动态分配，原因：

1. **确定性（Determinism）**：静态对象在编译/链接期就占好固定内存，**运行期分配不会失败**。动态 `malloc` 在堆紧张时可能返回 `NULL`，而关键控制线程若在启动时才失败，系统就起不来了。
2. **无内存碎片**：长期 7×24 运行的设备，动态分配/释放会逐步产生碎片，最终无连续块可用。静态分配一次性划定，永不产生碎片。
3. **无释放类 Bug**：动态对象有 `use-after-free`、`double-free`、`泄漏` 等风险；静态对象生命周期与系统一致，规避整类错误。
4. **启动顺序与可验证性**：静态对象地址固定，可静态分析内存布局、做 MISRA/功能安全审计；动态堆的行为难以在出厂前完全验证。
5. **最坏执行时间（WCET）可控**：动态分配的时间不确定，静态分配的执行路径可预测——这对实时性认证（如 IEC 61508、ISO 26262）是硬要求。

## 3. 线程状态机与调度算法

### 3.1 rtt线程状态描述及转换api

|**<br><br>状态名称<br><br>**|**<br><br>宏定义（在 `rtdef.h` 中）<br><br>**|**<br><br>描述<br><br>**|
|---|---|---|
|**初始态**​|`RT_THREAD_INIT`|线程刚被创建，系统为其分配了控制块和栈，但还未就绪运行。|
|**就绪态**​|`RT_THREAD_READY`|线程已经具备运行条件，正在等待 CPU 资源（排在就绪列表中）。|
|**运行态**​|`RT_THREAD_RUNNING`|线程当前正占用 CPU 正在执行。|
|**挂起态**​|`RT_THREAD_SUSPEND`|线程因为等待某个事件（如延时到期、信号量、互斥量等）而主动让出 CPU。|
|**关闭态**​|`RT_THREAD_CLOSE`|线程执行完毕或主动退出，等待系统回收资源（由 idle 线程完成最后清理）。|

![[五态关系图.png]]

| 起点 → 终点   | 触发 API / 事件                                                                             |
| --------- | --------------------------------------------------------------------------------------- |
| 初始态 → 就绪态 | `rt_thread_startup()`                                                                   |
| 就绪态 → 运行态 | 调度器在节拍/中断返回时选中（无显式 API，由 `rt_schedule()` 驱动）                                            |
| 运行态 → 就绪态 | `rt_thread_yield()`（主动让出）、被高优先级线程抢占                                                     |
| 运行态 → 挂起态 | `rt_thread_suspend()`、`rt_thread_delay()`、`rt_sem_take()`（资源不可用）、`rt_mb_recv()` 等阻塞 API |
| 挂起态 → 就绪态 | `rt_thread_resume()`、等待的信号量/邮箱到位、`rt_timer` 超时唤醒                                        |
| * → 关闭态   | `rt_thread_delete()`（动态）/ `rt_thread_detach()`（静态）或线程入口函数 `return`                      |
>在其中程序未完成而发生的“抢占”行为不是线程自己调的，而是调度器在中断返回或资源释放点强制发生的**。

---

### 3.3 抢占式调度（Preemptive Scheduling）

RT-Thread 默认是**基于优先级的全抢占式**调度：

- **核心规则**：就绪队列中，**优先级数值最小（0 最高）的线程永远先跑**。任何时刻只要有更高优先级线程进入就绪态，当前运行线程立即被抢占。
- **抢占发生的时机（调度点）**：
  1. **中断返回**：ISR 里释放了资源（如 `rt_sem_release`），退出中断前调用 `rt_schedule()`，发现更高优先级就绪 → 切换。
  2. **主动释放资源**：线程调用 `rt_sem_release()` / `rt_mb_send()` 等，内部触发 `rt_schedule()`。
  3. **主动让出**：线程调 `rt_thread_yield()`（让同优先级或更高优先级先跑）。
  4. **线程挂起/延时**：`rt_thread_delay()` 当前线程进入 SUSPEND，调度器选下一个就绪线程。
- **实现支撑**：调度器用"优先级位图（bitmap）+ 每优先级一个就绪链表"的结构，找最高优先级线程是 O(1)；`current_priority` 字段即比较依据。

---

### 3.4 时间片轮转（Round-Robin）

抢占式解决"谁先跑（优先级）"，时间片轮转解决"**同优先级多个线程怎么公平分 CPU**"：

- **作用范围**：**仅在相同 `current_priority` 的线程之间**生效。不同优先级仍走抢占式。
- **原理**：
  - 每个线程有 `init_tick`（时间片长度）和 `remaining_tick`（剩余）。
  - 系统节拍（SysTick）每触发一次，`remaining_tick--`。
  - 递减到 0 时：该线程被移到**同优先级就绪队列的队尾**，`remaining_tick` 重置为 `init_tick`，调度器在同优先级里选下一个。
- 线程也可主动 `rt_thread_yield()` 立即让出（不等时间片耗尽）。
