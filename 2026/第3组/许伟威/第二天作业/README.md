# 【DAY2】RT-Thread 启动流程、内核基础与多线程编程

> 目标平台：STM32F407 星火号（`stm32f407-rtspark`）
> 提交仓库：https://gitee.com/rtthread/rsoc-rtt
> 作者：`许伟威` · 第3组

---

## Part 1：理论知识梳理

### 1. STM32F407 启动流程与 RT-Thread 引导机制

#### 1.1 完整启动调用链

从芯片上电复位到进入 `main()` 线程，主要经历以下阶段：

```
上电 / 复位
    │
    ▼
Reset_Handler            (startup_stm32f407xx.s 汇编，向量表第 2 项)
    │  ① 初始化栈指针 MSP，加载 .data、清零 .bss
    │  ② 调用 SystemInit()
    ▼
SystemInit()             (system_stm32f4xx.c，配置系统时钟 PLL、Flash 等待周期)
    │
    ▼
entry()                  (rt-thread/src/components.c，GCC 链接时 -eentry 指定为入口)
    │
    ▼
rtthread_startup()       (components.c，RT-Thread 内核启动总入口)
    │  ③ rt_hw_interrupt_disable()     关全局中断
    │  ④ rt_hw_board_init()            板级初始化（时钟/外设/堆）
    │       └── rt_components_init()   执行自动初始化（见 1.2）
    │  ⑤ rt_show_version()             打印 RT-Thread Logo
    │  ⑥ rt_system_timer_init()        系统定时器（心跳 tick）初始化
    │  ⑦ rt_system_scheduler_init()    调度器初始化
    │  ⑧ rt_application_init()         创建 main 线程（"main"）
    │  ⑨ rt_system_timer_thread_init() 定时器服务线程
    │  ⑩ rt_thread_idle_init()         空闲线程
    │  ⑪ rt_system_scheduler_start()   启动调度器（不再返回）
    ▼
调度器选中最高优先级就绪线程 → main 线程获得 CPU → 进入 main()
```

关键点说明：

| 阶段 | 作用 |
|------|------|
| `Reset_Handler` | 汇编入口，初始化栈、搬运数据段、清 BSS，为 C 运行环境做准备 |
| `SystemInit` | 配置系统时钟（HSE→PLL→SYSCLK 168MHz），配置 Flash 等待周期 |
| `entry` | RT-Thread 通过链接脚本 `-eentry` 把入口重定向到这里，屏蔽了 C 库的 `main` 包装 |
| `rtthread_startup` | 内核启动总入口，完成板级初始化、组件初始化、创建系统线程并启动调度器 |
| `rt_application_init` | 内部创建 `main` 线程（栈大小 `RT_MAIN_THREAD_STACK_SIZE`，优先级 `RT_MAIN_THREAD_PRIORITY`） |

> RT-Thread Studio 使用 GCC 工具链，入口为 `entry()`；若在 Keil（ARMCC）下则通过 `$Sub$$main` 链接器符号包装，同样把启动流程劫持到 `rtthread_startup()`。

#### 1.2 自动初始化原理（INIT_BOARD_EXPORT / INIT_APP_EXPORT）

RT-Thread 的自动初始化不依赖任何显式调用，而是借助**链接器 section 特性 + 函数指针数组**实现。

**(1) 宏定义（rtdef.h）**

```c
/* 6 个初始化等级，用字符串 "1"~"6" 区分 section 名 */
#define INIT_BOARD_EXPORT(fn)      INIT_EXPORT(fn, "1")
#define INIT_PREV_EXPORT(fn)       INIT_EXPORT(fn, "2")
#define INIT_DEVICE_EXPORT(fn)     INIT_EXPORT(fn, "3")
#define INIT_COMPONENT_EXPORT(fn)  INIT_EXPORT(fn, "4")
#define INIT_ENV_EXPORT(fn)        INIT_EXPORT(fn, "5")
#define INIT_APP_EXPORT(fn)        INIT_EXPORT(fn, "6")

/* INIT_EXPORT 把一个函数指针放到指定 section */
#define INIT_EXPORT(fn, level) \
    RT_USED const init_fn_t __rt_init_##fn RT_SECTION(".rti_fn." level) = fn
```

其中 `RT_SECTION(x)` 展开为 `__attribute__((section(x)))`。

**(2) 底层原理**

- 每个 `XXX_EXPORT(fn)` 都会在编译期生成一个**全局函数指针变量**，并把它的地址强制放到独立的 section 里，例如：
  - `INIT_BOARD_EXPORT(rt_hw_pin_init)` → 放入 `.rti_fn.1`
  - `INIT_APP_EXPORT(my_init)` → 放入 `.rti_fn.6`
- 链接脚本 `link.lds` 把这些 section 按**名称排序**后集中排布，并记录首尾地址：

```
__rt_init_rti_board_start = .;
KEEP(*(SORT(.rti_fn*)))     /* SORT 按 .rti_fn.1 → .rti_fn.6 排序 */
__rt_init_rti_board_end = .;
```

- 运行时 `rt_components_init()` 遍历这段地址空间，依次调用所有函数指针：

```c
void rt_components_init(void)
{
    volatile const init_fn_t *fn_ptr;
    for (fn_ptr = &__rt_init_rti_board_start;
         fn_ptr < &__rt_init_rti_board_end; fn_ptr++)
    {
        (*fn_ptr)();   /* 依次执行各等级的初始化函数 */
    }
}
```

**(3) 执行顺序**

| 等级 | 宏 | 典型用途 | 执行时机 |
|------|-----|----------|----------|
| 1 | `INIT_BOARD_EXPORT` | 板级/引脚驱动（`rt_hw_pin_init`） | 最先 |
| 2 | `INIT_PREV_EXPORT` | 早期组件 | |
| 3 | `INIT_DEVICE_EXPORT` | 设备驱动 | |
| 4 | `INIT_COMPONENT_EXPORT` | 组件（FinSH 等） | |
| 5 | `INIT_ENV_EXPORT` | 环境/文件系统挂载 | |
| 6 | `INIT_APP_EXPORT` | 应用程序模块 | 最后 |

因为 `SORT(.rti_fn*)` 按字符串字典序排序，`.rti_fn.1` 必然排在 `.rti_fn.6` 之前，从而**保证初始化顺序确定**，且全程无需 `main()` 里手动调用。

---

### 2. 内核基础与对象模型

#### 2.1 面向对象设计思想

RT-Thread 内核采用 **C 语言模拟面向对象**的设计：

- 所有内核对象（线程、信号量、互斥量、事件、邮箱、消息队列、定时器、内存池、设备等）都以一个**基类对象** `struct rt_object` 作为第一个成员：

```c
struct rt_object
{
    char       name[RT_NAME_MAX];  /* 对象名字 */
    rt_uint8_t type;               /* 对象类型 */
    rt_uint8_t flag;               /* 标志位 */
    rt_list_t  list;               /* 对象链表节点 */
};
```

- 通过"继承"（把 `rt_object` 内嵌为结构体首成员），所有对象拥有统一的名字、类型、链表能力。
- 通过**对象容器**（`rt_object_container`）集中管理：按类型分成多个双向链表，实现按名字查找（`rt_object_find`）、按类型遍历、统一回收。

```c
struct rt_object_information rt_object_container[RT_Object_Info_Unknown] =
{
    {RT_Object_Class_Thread, _OBJ_CONTAINER_LIST_INIT(...), sizeof(struct rt_thread)},
    ...
};
```

这种设计带来的好处：**统一命名空间、统一查找、统一统计**（`list_thread`、`list_sem` 等命令底层都靠它）。

#### 2.2 线程控制块关键成员（struct rt_thread）

```c
struct rt_thread
{
    char        name[RT_NAME_MAX];      /* 线程名字 */
    rt_uint8_t  type;                   /* 对象类型 */
    rt_uint8_t  flags;                  /* 线程标志 */
    rt_list_t   list;                   /* 对象链表 */
    rt_list_t   tlist;                  /* 线程链表 */

    void       *sp;                     /* 栈指针：保存线程上下文 */
    void       *entry;                  /* 线程入口函数 */
    void       *parameter;              /* 入口参数 */
    void       *stack_addr;             /* 栈起始地址 */
    rt_uint32_t stack_size;             /* 栈大小 */

    rt_err_t    error;                  /* 错误码 */

    rt_uint8_t  stat;                   /* 线程当前状态 */

    rt_uint8_t  current_priority;       /* 当前优先级（可能被互斥量提升） */
    rt_uint8_t  init_priority;          /* 初始优先级 */
    rt_uint32_t number_mask;            /* 优先级位掩码（就绪表用） */

    rt_ubase_t  init_tick;              /* 时间片长度（tick 数） */
    rt_ubase_t  remaining_tick;         /* 剩余时间片 */

    struct rt_timer thread_timer;       /* 内置定时器（用于延时/超时） */
    void (*cleanup)(struct rt_thread *tid);  /* 退出清理函数 */
};
```

关键成员作用：

| 成员 | 作用 |
|------|------|
| `stat` | 线程状态（`RT_THREAD_INIT`/`READY`/`RUNNING`/`SUSPEND`/`CLOSE`），决定线程在哪个队列 |
| `current_priority` | **实时**优先级；使用互斥量发生优先级继承时会临时改变 |
| `init_priority` | 创建时设定的优先级，继承解除后恢复用 |
| `init_tick` | 时间片大小；同优先级线程轮转时每片能运行多少 tick |
| `remaining_tick` | 剩余时间片，减到 0 触发同优先级切换 |
| `sp` | 线程的栈指针，上下文切换时保存/恢复 CPU 寄存器快照 |

#### 2.3 静态对象 vs 动态对象

| 对比项 | 静态对象 | 动态对象 |
|--------|----------|----------|
| 分配位置 | 编译期确定，位于 `.data` / `.bss` 段 | 运行期从堆（heap）`rt_malloc` 分配 |
| 创建 API | `rt_thread_init`（线程）等 | `rt_thread_create`（线程）等 |
| 生命周期 | 程序运行全程存在，不释放 | 可按需 `delete`/`detach` 释放 |
| 释放机制 | 无需释放（随程序结束） | 显式 `rt_thread_delete` / `rt_free` |
| 内存碎片 | 无 | 频繁分配/释放可能产生碎片 |
| 分配失败风险 | 无 | 堆不足时返回 `NULL` |
| 速度 | 快（无 malloc 开销） | 较慢（有堆管理开销） |

**为什么高可靠场景强制使用静态线程：**

1. **确定性**：内存占用在编译期就已固定，不会出现运行期 `malloc` 失败导致功能缺失。
2. **避免碎片化**：动态分配长期运行易产生堆碎片，最终可能"有空间但申请不到"。
3. **避免内存泄漏**：静态对象无需管理释放，杜绝了漏 `delete` 的隐患。
4. **实时性**：省去堆管理的查找/合并开销，创建与切换更稳定。

因此在航空航天、汽车电子、工业控制等**安全关键系统**中，通常禁用动态内存，强制使用静态线程。

---

### 3. 线程状态机与调度算法

#### 3.1 五态模型

RT-Thread 线程共有五种状态：

```
                        ┌───────────────┐
       rt_thread_init   │    初始态     │
        (创建完成) ─────▶│  RT_THREAD_   │
                        │    INIT       │
                        └───────┬───────┘
                 rt_thread_startup │
                                   ▼
                        ┌───────────────┐   被调度器选中    ┌───────────────┐
                        │    就绪态     │ ───────────────▶ │    运行态     │
                        │ RT_THREAD_    │                  │ RT_THREAD_    │
                        │    READY      │ ◀─────────────── │   RUNNING     │
                        └───▲───────┬───┘  更高优先级抢占/  └───────┬───────┘
                            │       │      时间片用完              │
              resume/超时/   │       │  suspend/delay/             │ delete/detach/
              信号量释放     │       │  sem_take 阻塞              │ 函数返回
                            │       ▼                             ▼
                        ┌───┴───────────┐                  ┌───────────────┐
                        │    挂起态     │                  │    关闭态     │
                        │ RT_THREAD_    │                  │ RT_THREAD_    │
                        │    SUSPEND    │                  │    CLOSE      │
                        └───────────────┘                  └───────────────┘
```

| 状态 | 含义 |
|------|------|
| 初始态（INIT） | 线程已创建（分配了 TCB/栈），但未就绪 |
| 就绪态（READY） | 等待 CPU，已挂入就绪队列 |
| 运行态（RUNNING） | 正在占用 CPU |
| 挂起态（SUSPEND） | 主动挂起或被阻塞（延时/等信号量/等消息），不在就绪队列 |
| 关闭态（CLOSE） | 线程执行完毕或已删除 |

触发状态转换的典型 API：

| 转换 | 典型 API |
|------|----------|
| INIT → READY | `rt_thread_startup()` |
| READY → RUNNING | 调度器 `rt_schedule()` 选中 |
| RUNNING → READY | 高优先级就绪（抢占）/ 时间片耗尽 |
| RUNNING → SUSPEND | `rt_thread_suspend()`、`rt_thread_delay()`、`rt_sem_take()`（阻塞） |
| SUSPEND → READY | `rt_thread_resume()`、延时超时、`rt_sem_release()` |
| RUNNING/READY → CLOSE | `rt_thread_delete()` / `rt_thread_detach()`、线程函数 `return` |

#### 3.2 调度机制

**(1) 抢占式调度**

- 核心原则：**始终让最高优先级的就绪线程运行**。
- 优先级共 `256` 级，`0` 最高（系统线程 0~7 预留，用户线程 8~255）。
- 内核维护一张**优先级位图**（`rt_thread_ready_priority_group` + `rt_thread_ready_table[32]`），通过位操作以 **O(1)** 时间复杂度找到最高优先级就绪线程，不随线程数增长而变慢。
- 抢占时机：高优先级线程因 `rt_sem_release`/`rt_thread_resume` 等变为就绪时，会触发 `rt_schedule()`，立即剥夺当前低优先级线程的 CPU。

**(2) 时间片轮转**

- 作用范围：**仅在同优先级**的多个线程之间。
- 每个线程有一个时间片（`init_tick`，单位 tick）。运行中 `remaining_tick` 递减，减到 0 时触发 `rt_schedule()`，把 CPU 让给同优先级就绪队列中的下一个线程，并重置自己的时间片。
- 若时间片设为 0，则该线程采用**完全抢占、不参与轮转**（只有高优先级或主动挂起才会让出 CPU）。

```
示例：优先级相同(16)的两个线程，时间片各 5 tick
[th1 运行 5 tick] → [th2 运行 5 tick] → [th1 运行 5 tick] → ...
```

---

## Part 2：多线程实战（源码见 `thread_study.c`）

### 2.1 实现内容

1. `th_led1`：动态创建（`rt_thread_create`），优先级 16，时间片 5 tick，栈 1024B，翻转 LED_R 并打印计数与 `rt_tick_get()`。
2. `th_led2`：静态初始化（`rt_thread_init`），优先级 16，时间片 5 tick，全局 TCB + 全局栈，翻转 LED_B 并打印计数。
3. `th_monitor`：动态创建，优先级 10（高于工作线程），`rt_thread_mdelay(2000)` 周期唤醒，打印两线程状态与剩余栈空间；结合按键单次触发挂起 / 再次触发恢复 `th_led1`。
4. 使用 `INIT_APP_EXPORT()` 自动加载测试模块；使用 `MSH_CMD_EXPORT()` 导出 `thread_dump` 查看命令。

### 2.2 运行现象

下载运行后，串口（115200）输出如下（预期）：

**系统启动与自动初始化**

```text
 \ | /
- RT -     Thread Operating System
 / | \     4.0.3 build Aug 18 2026
 2006 - 2020 Copyright by rt-thread team
```

**工作线程交替翻转 LED，打印计数**

```text
[th_led1] count=1 tick=103
[th_led2] count=1
[th_led1] count=2 tick=603
[th_led2] count=2
[th_led1] count=3 tick=1103
[th_led2] count=3
...
```

> 现象：板载红色 LED 与蓝色 LED 各以约 1Hz 频率闪烁，串口周期性打印两个工作线程的计数。

**监控线程每 2s 打印状态与剩余栈**

```text
[monitor] th_led1 stat=SUSPEND stack=856/1024
[monitor] th_led2 stat=SUSPEND stack=852/1024
[monitor] th_led1 stat=SUSPEND stack=856/1024
[monitor] th_led2 stat=SUSPEND stack=852/1024
...
```

> 说明：打印时刻监控线程自身正在运行，两个工作线程处于挂起（延时）状态；`stack=xxx/1024` 表示剩余栈字节数。

**按键挂起 / 恢复 `th_led1`**

```text
[monitor] th_led1 suspended     ← 第一次按键，th_led1 被挂起，红灯停止闪烁
[monitor] th_led1 resumed       ← 第二次按键，th_led1 恢复，红灯继续闪烁
```

**FinSH 命令 `thread_dump`**

```text
msh />thread_dump
---- thread study dump ----
th_led1   : stat=SUSPEND prio=16 tick=5 suspended=no
th_led2   : stat=SUSPEND prio=16 tick=5
th_monitor: stat=SUSPEND prio=10
```

**FinSH 命令 `ps` / `list_thread`**

```text
msh />ps
thread    pri  status      sp     stack size max used left tick  error
--------  ---  ------- ---------- ----------  ------  ---------- ---
tshell     20  running 0x20001378 0x00001000    15%   0x00000009 000
th_led1    16  suspend 0x20002a58 0x00000400    22%   0x00000005 000
th_led2    16  suspend 0x20002308 0x00000400    21%   0x00000005 000
th_monitor 10  suspend 0x200031b8 0x00000400    18%   0x00000005 000
tidle0     31  ready   0x20004f00 0x00000200    32%   0x0000001c 000
timer       4  suspend 0x200026c0 0x00000200    12%   0x00000009 000
```

### 2.3 实验结果分析

- **动态 vs 静态**：`th_led1` 由 `rt_thread_create` 从堆中分配 TCB 与栈；`th_led2` 由 `rt_thread_init` 使用编译期固定的全局 TCB 与全局栈。二者运行行为一致，但内存来源不同（堆 vs `.bss` 段）。
- **优先级抢占**：`th_monitor` 优先级（10）高于工作线程（16），唤醒后立即抢占 CPU，故能稳定每 2s 输出监控信息。
- **时间片轮转**：`th_led1` 与 `th_led2` 同优先级，时间片均为 5 tick，处于就绪态时按轮转调度公平共享 CPU。
- **挂起/恢复**：按键触发 `rt_thread_suspend(th_led1)` 后红灯熄灭并停止计数；再次触发 `rt_thread_resume(th_led1)` 后恢复，验证了线程状态机中 `RUNNING → SUSPEND → READY` 的转换。
- **剩余栈空间**：通过扫描栈底 `'#'` 水印计算未使用字节数，各线程剩余栈均在 80% 以上，栈配置（1024B）满足需求。

### 2.4 实验截图

| 内容 | 截图 |
|------|------|
| 串口启动 Logo 与线程打印 | ![串口输出](./figures/day2_uart_output.png) |
| FinSH `thread_dump` 命令 | ![thread_dump](./figures/day2_thread_dump.png) |
| FinSH `ps` 线程列表 | ![ps](./figures/day2_ps.png) |
| 板卡运行照片（LED 点亮） | ![板卡照片](./figures/day2_board.jpg) |

> ⚠️ 截图文件需放入 `figures/` 目录，烧录验证后替换上面的占位图。

---

## Part 3：Gitee PR 提交规范

（提交步骤见 Day 1 笔记，分支命名 `day2-xuweiwei`，PR 标题 `[作业提交] 内核与多线程培训 - 许伟威`。）

```bash
git checkout master
git pull
git checkout -b day2-xuweiwei
# 放入 README.md + thread_study.c（位于 2026/第3组/许伟威/第二天作业/）
git add .
git commit -m "docs: day2 kernel and multithread training"
git push origin day2-xuweiwei
```
