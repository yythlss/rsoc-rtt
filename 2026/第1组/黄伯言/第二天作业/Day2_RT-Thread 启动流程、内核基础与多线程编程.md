# 【DAY2】RT-Thread 启动流程、内核基础与多线程编程

- **目标平台**：STM32F407 星火号（`stm32f407-rtspark`）
- **提交仓库**：`https://gitee.com/rtthread/rsoc-rtt`
- **作业交付物**：理论总结与实验报告（`README.md`）+ 可编译源码（`*.c`）通过 Gitee PR 提交。

## Part 1：理论知识梳理文档（写入作业目录 `README.md`）

### 1. STM32F407 启动流程与 RT-Thread 引导机制

- **启动路径**：
  
  RT-Thread 支持多种平台和多种编译器，而 rtthread_startup() 函数是 RT-Thread 规定的统一启动入口。

系统先从启动文件开始运行，然后进入 RT-Thread 的启动函数 rtthread_startup() ，最后进入用户入口函数 main().

大致可以分为四个部分：

（1）初始化与系统相关的硬件；

（2）初始化系统内核对象，例如定时器、调度器、信号；

（3）创建 main 线程，在 main 线程中对各类模块依次进行初始化；

（4）初始化定时器线程、空闲线程，并启动调度器。

为了在进入 main() 之前完成 RT-Thread 系统功能初始化，我们使用了 MDK 的扩展功能 `$Sub$$` 和 `$Super$$`。可以给 main 添加 `$Sub$$` 的前缀符号作为一个新功能函数 `$Sub$$main`，这个 `$Sub$$main` 可以先调用一些要补充在 main 之前的功能函数（这里添加 RT-Thread 系统启动，进行系统一系列初始化），再调用 `$Super$$main` 转到 main() 函数执行(MDK特有)

**自动初始化原理**：

##### 初始化导出宏 `INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT` 的实现原理

这两个宏是基于编译器的**段属性**与**链接脚本内存布局**实现的，核心机制是“将函数指针标记并放入特定段，系统启动后统一遍历调用”。

###### 1. 宏展开：将函数指针放入段

两者均来自基础宏 `INIT_EXPORT(fn, level)`，区别在于 `level` 不同：

| 宏                       | 展开后的 `level` | 放入的段        |
| ----------------------- | ------------ | ----------- |
| `INIT_BOARD_EXPORT(fn)` | `"1"`        | `.rti_fn.1` |
| `INIT_APP_EXPORT(fn)`   | `"6"`        | `.rti_fn.6` |

宏展开的核心代码示意（以 `INIT_APP_EXPORT` 为例）：

```c
__attribute__((used)) const init_fn_t __rt_init_fn_name \
__attribute__((section(".rti_fn.6"))) = fn_name;
```

### 2. 内核基础与对象模型

- **对象容器与控制块**：
  
  RT-Thread 内核在设计上借鉴了面向对象（Object-Oriented）思想，通过结构体封装、继承和虚函数表（或函数指针）等方式，将硬件资源抽象为内核对象，形成统一的对象管理模型。
  
  - **内核对象（`rt_object`）** ：所有内核对象（如线程、信号量、消息队列、设备等）都继承自 `rt_object` 基类，该基类包含对象名称、类型、链表节点等公共属性，并挂载到全局对象容器中，便于统一管理。
  - **继承与派生**：具体内核对象（如 `rt_thread`、`rt_mutex`、`rt_device`）在结构体头部嵌入 `rt_object` 基类，实现“继承”。这种设计使得内核可以基于对象类型进行统一操作（如查找、删除、遍历）。
  - **多态与虚函数**：设备驱动框架中广泛使用函数指针（类似虚函数表）实现多态。例如 `rt_device` 中的 `init`、`open`、`read`、`write`、`control` 等函数指针，由具体驱动实现，上层调用时无需关心具体硬件细节。
  
  这种设计让内核具有良好的可扩展性和可维护性，便于实现模块化开发。
  
  ---
  
  ### 2. 1线程控制块 `struct rt_thread` 关键成员解析
  
  线程控制块是 RT-Thread 中最重要的内核对象之一，用于描述和管理一个线程的所有信息。以下是几个关键成员的作用：
  
  #### 2.1.1 `stat` —— 线程状态
  
  - **类型**：`rt_uint8_t`
  - **作用**：记录线程当前的运行状态。取值包括：
    - `RT_THREAD_RUNNING`：正在运行
    - `RT_THREAD_READY`：就绪，等待调度
    - `RT_THREAD_SUSPEND`：挂起，等待事件或超时
    - `RT_THREAD_INIT`：已创建但未启动
    - `RT_THREAD_CLOSE`：已关闭
    - `RT_THREAD_STAT_MASK` 等状态掩码用于位标志扩展
  
  该字段是调度器判断线程是否可被调度的核心依据，只有状态为 `READY` 或 `RUNNING` 的线程才能参与调度。
  
  ##### 2.1.2`current_priority` —— 当前优先级
  
  - **类型**：`rt_uint8_t`
  - **作用**：存储线程当前的实际优先级。RT-Thread 支持线程优先级的动态变更（通过 `rt_thread_control` 或 `rt_thread_priority` 接口），该字段为调度器提供了选择最高优先级线程的数值依据。数值越小，优先级越高（与 FreeRTOS 等相反）。
  
  ##### 2.1.3 `init_tick` —— 初始时间片
  
  - **类型**：`rt_tick_t`
  - **作用**：记录线程被创建时指定的时间片大小（以系统时钟节拍 `tick` 为单位）。同优先级线程采用时间片轮转调度，当线程的剩余时间片（`remaining_tick`）耗尽且未主动让出 CPU 时，调度器会将 CPU 切换给同优先级的其他线程。
  
  ##### 2.1.4 `sp` —— 栈指针
  
  - **类型**：`rt_ubase_t`
  - **作用**：存储线程当前栈指针（Stack Pointer）的值。这是线程上下文切换的核心字段，保存了线程被切换出去时 CPU 寄存器的现场，当线程恢复运行时，调度器会将 `sp` 恢复到 CPU 的 SP 寄存器，从而还原线程的执行现场（包括局部变量、返回地址等）。每个线程拥有独立的栈空间，通过 `sp` 实现隔离。
  
  ---
  
  ### 2.2 总结对比
  
  | 成员                 | 类型           | 作用                        |
  | ------------------ | ------------ | ------------------------- |
  | `stat`             | `rt_uint8_t` | 标识线程当前运行状态，调度器依据此判断是否参与调度 |
  | `current_priority` | `rt_uint8_t` | 存储当前优先级，用于调度器选择下一个运行线程    |
  | `init_tick`        | `rt_tick_t`  | 记录初始时间片，用于同优先级线程的轮转调度     |
  | `sp`               | `rt_ubase_t` | 当前栈指针，上下文切换时用于保存和恢复线程现场   |
  
  这些成员共同支撑了 RT-Thread 线程的创建、调度、切换与状态管理，是理解内核运行机制的关键入口。

### RT-Thread 面向对象设计思想与线程控制块关键成员

- **内存分配方式**：

###### 1. 分配位置

- **静态对象**：内存由编译器在编译时分配，位于静态存储区（`.data` 段或 `.bss` 段）。这些对象通常定义为全局变量或使用 `static` 关键字修饰的局部变量，其地址在链接时已固定。

- **动态对象**：内存通过内核的内存管理接口（如 `rt_malloc`）在**系统堆（Heap）** 中动态分配。分配时机为运行时，具体地址取决于堆的当前使用情况，无法预先确定。
  
  ###### 2. 生命周期

- **静态对象**：生命周期与系统运行周期一致，从**系统启动**时即已存在，直到**系统断电或复位**才会消亡。在整个运行期间，对象始终有效，不会被自动销毁。

- **动态对象**：生命周期由开发者显式控制。通过 `rt_*_create` 接口创建后生效，通过 `rt_*_delete` 或 `rt_free` 接口销毁后即失效，可在系统运行过程中按需反复创建和删除。
  
  ###### 3. 释放机制

- **静态对象**：**无需释放，且不允许释放**。内存由编译器自动管理，开发者不应调用任何释放接口，否则可能导致系统崩溃。

- **动态对象**：**必须显式释放**。当对象不再使用时，必须调用对应的销毁接口释放其占用的堆内存（包括对象控制块及内部可能额外申请的资源，如线程栈）。若只删除对象控制块而未释放其内部资源，则会产生**内存泄漏**；若重复释放，则可能导致内存管理混乱甚至系统死机。

高可靠场景强制使用静态对象，本质上是**将风险从运行时提前到编译时**，防止运行时出现内存碎片问题，提升系统即时性，以牺牲灵活性为代价，换取系统的确定性、可预测性和安全性。

### 4. 线程状态机与调度算法

- **五态模型**：

![线程状态转换图](https://www.rt-thread.org/document/site/rt-thread-version/rt-thread-standard/programming-manual/thread/figures/04thread_sta.png)

- **调度机制剖析**：
  - **抢占式调度**：要有一个更高优先级的线程进入就绪态，系统必须立即暂停当前线程，将 CPU 控制权交给该高优先级线程。这种调度方式确保了系统能够及时响应外部事件，是硬实时性的基础保障。
  - **时间片轮转**：
  - 一个线程被分配一个固定的运行时间配额，以系统时钟节拍（Tick）为单位。当线程获得 CPU 后，可以连续运行的时间长度即为该线程的时间片。
  - 当当前线程的时间片耗尽时，调度器将 CPU 切换给同优先级队列中的下一个就绪线程，形成一个循环队列，依次轮转执行。

## Part 2：多线程实战编程（C 语言实现）

### 任务说明

1. **工作线程 1（动态创建 `th_led1`）**：
- 优先级 `16`，时间片 `5 ticks`，栈 `1024 Bytes`。
- 循环翻转板载 LED_R，每次执行打印计数与 `rt_tick_get()`。

```
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED

static void led1_thread_entry(void *parameter);
rt_thread_t th_led1 = RT_NULL;
/* 模块初始化函数，使用 INIT_APP_EXPORT 自动加载 */
static int led1_thread_init(void)
{
    

    /* 动态创建线程 th_led1 */
    th_led1 = rt_thread_create("th_led1",           /* 线程名称 */
                               led1_thread_entry,   /* 线程入口函数 */
                               RT_NULL,             /* 线程入口参数 */
                               1024,                /* 线程栈大小，单位字节 */
                               16,                  /* 线程优先级 */
                               5);                  /* 线程时间片，单位 tick */

    /* 如果创建成功，则启动线程 */
    if (th_led1 != RT_NULL)
        rt_thread_startup(th_led1);
    else
        rt_kprintf("th_led1 create failed!\n");

    return 0;
}
/* 使用 INIT_APP_EXPORT 宏导出，系统启动后自动执行 */
INIT_APP_EXPORT(led1_thread_init);

/* 线程入口函数实现 */
static void led1_thread_entry(void *parameter)
{
    rt_uint32_t count = 0;


    while (1)
    {
        /* 翻转 LED_R */
        rt_pin_write(PIN_LED_R, PIN_LOW);
        rt_thread_mdelay(500);
        rt_pin_write(PIN_LED_R, PIN_HIGH);
        rt_thread_mdelay(500);

        count++;
        rt_kprintf("th_led1 count: %d, tick: %d\n", count, rt_tick_get());
    }
}
```



```
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 18 2026 22:42:59
 2006 - 2022 Copyright by RT-Thread team
msh >th_led1 count: 1, tick: 1002
th_led1 count: 2, tick: 2004
th_led1 count: 3, tick: 3006
th_led1 count: 4, tick: 4008
th_led1 count: 5, tick: 5010
th_led1 count: 6, tick: 6012
th_led1 count: 7, tick: 7014
th_led1 count: 8, tick: 8016
th_led1 count: 9, tick: 9018
th_led1 count: 10, tick: 10020
```

![](C:/Users/HUANG/AppData/Roaming/marktext/images/2026-08-18-23-00-41-image.png)

###### 分析：

板载led红灯闪烁，并且串口在启动之后输出线程名称和计数以及时间片tick计数，表明这个线程在正常工作。

2.**工作线程 2（静态初始化 `th_led2`）**：

- 优先级 `16`，时间片 `5 ticks`，静态定义全局栈与 TCB。
- 循环翻转板载 LED_B，每次执行打印计数。

```
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
/* ===== 手动定义 LED_B 引脚 ===== */
#define PIN_LED_B              GET_PIN(F, 11)      // PF11 :  LED_B        --> LED

/* ===== 静态线程资源定义（全局变量） ===== */
struct rt_thread th_led2_tcb;          /* 线程控制块（TCB） */
static rt_uint8_t th_led2_stack[1024];        /* 线程栈，大小 1024 字节 */

/* 线程入口函数声明 */
static void led2_thread_entry(void *parameter);

/* 模块初始化函数，使用 INIT_APP_EXPORT 自动加载 */
static int led2_thread_init(void)
{
    rt_err_t result;

    /* 静态初始化线程 th_led2 */
    result = rt_thread_init(&th_led2_tcb,              /* 线程控制块地址 */
                            "th_led2",                 /* 线程名称 */
                            led2_thread_entry,         /* 线程入口函数 */
                            RT_NULL,                   /* 线程入口参数 */
                            &th_led2_stack[0],         /* 线程栈起始地址 */
                            sizeof(th_led2_stack),     /* 线程栈大小 */
                            16,                        /* 线程优先级 */
                            5);                        /* 线程时间片，单位 tick */

    if (result == RT_EOK) {
        /* 启动线程 */
        rt_thread_startup(&th_led2_tcb);
        rt_kprintf("th_led2 static init success!\n");
    } else {
        rt_kprintf("th_led2 static init failed!\n");
    }

    return 0;
}
/* 使用 INIT_APP_EXPORT 宏导出，系统启动后自动执行 */
INIT_APP_EXPORT(led2_thread_init);

/* ===== 线程入口函数实现 ===== */
static void led2_thread_entry(void *parameter)
{
    rt_uint32_t count = 0;

    /* 初始化 LED_B 引脚为推挽输出模式 */
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);

    while (1)
    {
        /* 翻转 LED_B：拉低点亮（假设低电平亮） */
        rt_pin_write(PIN_LED_B , PIN_LOW);
        rt_thread_mdelay(1000);
        rt_pin_write(PIN_LED_B , PIN_HIGH);
        rt_thread_mdelay(1000);

        count++;
        rt_kprintf("th_led2 count: %d\n", count);
    }
}
```

```
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 18 2026 22:42:59
 2006 - 2022 Copyright by RT-Thread team
th_led2 static init success!
msh >th_led1 count: 1, tick: 1005
th_led2 count: 1
th_led1 count: 2, tick: 2007
th_led1 count: 3, tick: 3009
th_led2 count: 2
th_led1 count: 4, tick: 4011
th_led1 count: 5, tick: 5013
th_led2 count: 3
th_led1 count: 6, tick: 6015
th_led1 count: 7, tick: 7017
th_led2 count: 4
th_led1 count: 8, tick: 8019
th_led1 count: 9, tick: 9021
th_led2 count: 5
th_led1 count: 10, tick: 10023
```

![](C:/Users/HUANG/AppData/Roaming/marktext/images/2026-08-18-23-12-58-image.png)

###### 分析：

红绿两个led按设定的节奏闪烁，表明系统正常运行。同时串口打印出两个线程要求打印的信息，表明两个线程是通过调度轮流执行的，同时并没有明显抢占出现，表明两个线程的运行是平级的。（led1出现两次led2才出现一次是因为延时的不同，满足代码的编写预期现象）

3.**监控与控制线程（动态创建 `th_monitor`）**：

- 优先级 `10`（高于工作线程），周期延时 `rt_thread_mdelay(2000)`。
- 唤醒后获取并打印 `th_led1` 与 `th_led2` 的运行状态与剩余栈空间大小。
- 结合板载按键检测（或按键
- 中断）：单次触发时调用 `rt_thread_suspend()` 挂起 `th_led1`；再次触发时调用 `rt_thread_resume()` 恢复。

```
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
/* ===== 外部引用：获取工作线程的句柄和控制块 ===== */
extern rt_thread_t th_led1;              /* 动态线程句柄（在 th_led1.c 中定义） */
extern struct rt_thread th_led2_tcb;     /* 静态线程控制块（在 th_led2.c 中定义） */

/* ===== 定义按键引脚 ===== */
/* 配置 KEY 输入引脚  */
#define PIN_KEY0        GET_PIN(C, 0)     // PC0:  KEY0         --> KEY
#define PIN_KEY1        GET_PIN(C, 1)      // PC1 :  KEY1         --> KEY
#define PIN_KEY2        GET_PIN(C, 4)      // PC4 :  KEY2         --> KEY
#define PIN_WK_UP       GET_PIN(C, 5)     // PC5:  WK_UP        --> KEY

/* ===== 定义事件集 ===== */
static rt_event_t key_event = RT_NULL;
#define KEY_EVENT_FLAG   (1 << 0)

/* ===== 线程入口函数声明 ===== */
static void monitor_thread_entry(void *parameter);
static void key_irq_callback(void *args);

/* ===== 模块初始化函数 ===== */
static int monitor_thread_init(void)
{
    /* 1. 创建事件集 */
    key_event = rt_event_create("key_evt", RT_IPC_FLAG_PRIO);
    if (key_event == RT_NULL) {
        rt_kprintf("key_event create failed!\n");
        return -1;
    }

    /* 2. 注册按键中断 */
    rt_pin_attach_irq(PIN_KEY0, PIN_IRQ_MODE_FALLING, key_irq_callback, RT_NULL);
    rt_pin_irq_enable(PIN_KEY0, PIN_IRQ_ENABLE);
    rt_kprintf("key interrupt registered on pin %d\n", PIN_KEY0);

    /* 3. 动态创建监控线程 */
    rt_thread_t th_monitor = rt_thread_create("th_monitor",
                                              monitor_thread_entry,
                                              RT_NULL,
                                              2048,      /* 栈大小 2048 字节 */
                                              10,        /* 优先级 10，高于工作线程（16） */
                                              10);       /* 时间片 10 ticks */
    if (th_monitor != RT_NULL) {
        rt_thread_startup(th_monitor);
        rt_kprintf("th_monitor create success!\n");
    } else {
        rt_kprintf("th_monitor create failed!\n");
        return -1;
    }

    return 0;
}
/* 使用 INIT_APP_EXPORT 导出，系统启动后自动执行 */
INIT_APP_EXPORT(monitor_thread_init);

/* ===== 按键中断回调函数（在中断上下文执行） ===== */
static void key_irq_callback(void *args)
{
    /* 发送事件，通知监控线程 */
    rt_event_send(key_event, KEY_EVENT_FLAG);
}

/* ===== 获取线程剩余栈空间的辅助函数 ===== */
static size_t get_thread_stack_free(rt_thread_t thread)
{
    if (thread == RT_NULL)
        return 0;

    /* 计算栈剩余空间：当前栈指针 - 栈起始地址 */
    rt_ubase_t sp = thread->sp;                          /* 当前栈指针（ARM Cortex-M 向下增长） */
    rt_ubase_t stack_addr = (rt_ubase_t)thread->stack_addr; /* 栈起始地址（低地址） */

    /* Cortex-M 栈是向下增长的，已使用空间 = stack_addr - sp，剩余 = stack_size - 已使用 */
    if (sp >= stack_addr) {
        return thread->stack_size - (sp - stack_addr);
    }
    return 0;
}

/* ===== 监控线程入口函数 ===== */
static void monitor_thread_entry(void *parameter)
{
    rt_uint32_t recv_events = 0;
    rt_err_t result;

    /* 线程状态描述字符串 */
    const char *state_str[] = {
        "INIT",      /* 0x00 */
        "READY",     /* 0x01 */
        "RUNNING",   /* 0x02 */
        "SUSPEND",   /* 0x03 */
        "CLOSE"      /* 0x04 */
    };

    while (1)
    {
        /* ===== 1. 等待 2 秒 ===== */
        rt_thread_mdelay(2000);

        /* ===== 2. 打印两个工作线程的状态和剩余栈空间 ===== */
        rt_kprintf("\n========== Monitor Dump ==========\n");

        /* ---- th_led1（动态线程） ---- */
        if (th_led1 != RT_NULL) {
            rt_uint8_t state1 = th_led1->stat & RT_THREAD_STAT_MASK;
            rt_kprintf("[th_led1] state: %s", state_str[state1]);
            if (state1 == RT_THREAD_SUSPEND) {
                rt_kprintf(" (SUSPENDED)");
            }
            rt_kprintf(", free stack: %d bytes\n", get_thread_stack_free(th_led1));
        } else {
            rt_kprintf("[th_led1] thread not exist\n");
        }

        /* ---- th_led2（静态线程） ---- */
        {
            rt_uint8_t state2 = th_led2_tcb.stat & RT_THREAD_STAT_MASK;
            rt_kprintf("[th_led2] state: %s", state_str[state2]);
            if (state2 == RT_THREAD_SUSPEND) {
                rt_kprintf(" (SUSPENDED)");
            }
            rt_kprintf(", free stack: %d bytes\n", get_thread_stack_free(&th_led2_tcb));
        }

        rt_kprintf("==================================\n");

        /* ===== 3. 检查是否有按键事件（非阻塞） ===== */
        result = rt_event_recv(key_event,
                               KEY_EVENT_FLAG,
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               RT_WAITING_NO,      /* 非阻塞，立即返回 */
                               &recv_events);

        if (result == RT_EOK) {
            /* 收到按键事件，切换 th_led1 的挂起/恢复状态 */
            if (th_led1 != RT_NULL) {
                rt_uint8_t state = th_led1->stat & RT_THREAD_STAT_MASK;

                if (state == RT_THREAD_SUSPEND) {
                    /* 如果已挂起，则恢复 */
                    if (rt_thread_resume(th_led1) == RT_EOK) {
                        rt_kprintf(">>> th_led1 resumed by key press <<<\n");
                        /* 恢复后需主动触发调度 */
                        rt_schedule();
                    }
                } else if (state == RT_THREAD_READY || state == RT_THREAD_RUNNING) {
                    /* 如果正在运行或就绪，则挂起 */
                    if (rt_thread_suspend(th_led1) == RT_EOK) {
                        rt_kprintf(">>> th_led1 suspended by key press <<<\n");
                        /* 挂起后需主动触发调度 */
                        rt_schedule();
                    }
                } else {
                    rt_kprintf("th_led1 current state (%d) cannot suspend/resume\n", state);
                }
            }
        }
    }
}

```

```
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 18 2026 22:42:59
 2006 - 2022 Copyright by RT-Thread team
th_led2 static init success!
key interrupt registered on pin 32
th_monitor create success!
msh >th_led1 count: 1, tick: 1011

========== Monitor Dump ==========
[th_led1] thread not exist
[th_led2] state: READY, free stack: 172 bytes
==================================
th_led2 count: 1, tick: 2025

th_led1 count: 3, tick: 3029
```

分析：可以看出，两个线程中通过监视器，可以得到资源的展示；同时通过按键中断，可以达到暂时挂起一个线程的目的

**系统集成与调试命令**：

- 使用 `INIT_APP_EXPORT()` 自动加载测试模块，禁止在 `main.c` 中硬编码调用。
- 使用 `MSH_CMD_EXPORT()` 导出自定义查看命令（如 `thread_dump`）。

```
#include <rtthread.h>

/* ===== 外部引用：获取工作线程的句柄和控制块 ===== */
extern rt_thread_t th_led1;
extern struct rt_thread th_led2_tcb;

/* ===== 获取线程状态字符串 ===== */
static const char* get_thread_state_str(rt_uint8_t state)
{
    state &= RT_THREAD_STAT_MASK;
    switch (state) {
        case RT_THREAD_INIT:      return "INIT";
        case RT_THREAD_READY:     return "READY";
        case RT_THREAD_RUNNING:   return "RUNNING";
        case RT_THREAD_SUSPEND:   return "SUSPEND";
        case RT_THREAD_CLOSE:     return "CLOSE";
        default:                  return "UNKNOWN";
    }
}

/* ===== 获取线程剩余栈空间 ===== */
static size_t get_thread_stack_free(rt_thread_t thread)
{
    if (thread == RT_NULL)
        return 0;

    rt_ubase_t sp = (rt_ubase_t)thread->sp;
    rt_ubase_t stack_addr = (rt_ubase_t)thread->stack_addr;

    if (sp >= stack_addr) {
        return thread->stack_size - (sp - stack_addr);
    }
    return 0;
}

/* ===== 打印单个线程信息 ===== */
static void print_thread_info(const char* name,
                              rt_thread_t thread,
                              const char* type,
                              rt_uint8_t priority)
{
    if (thread == RT_NULL) {
        rt_kprintf("%-12s | %-10s | %-8s | %-6s | %-10s | %-8s\n",
                   name, type, "N/A", "N/A", "N/A", "NON-EXIST");
        return;
    }

    rt_uint8_t state = thread->stat & RT_THREAD_STAT_MASK;
    size_t free_stack = get_thread_stack_free(thread);

    rt_kprintf("%-12s | %-10s | %-8d | %-6d | %-10d | %-8s\n",
               name,
               type,
               priority,
               thread->stack_size,
               free_stack,
               get_thread_state_str(state));
}

/* ===== 自定义命令：thread_dump ===== */
static void cmd_thread_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("\n");
    rt_kprintf("========== RT-Thread Thread Dump ==========\n");
    rt_kprintf("%-12s | %-10s | %-8s | %-6s | %-10s | %-8s\n",
               "Thread Name", "Type", "Priority", "Stack", "Free Stack", "State");
    rt_kprintf("------------|------------|----------|--------|------------|----------\n");

    /* 打印 th_led1（动态线程） */
    print_thread_info("th_led1", th_led1, "Dynamic", 16);

    /* 打印 th_led2（静态线程） */
    print_thread_info("th_led2", (rt_thread_t)&th_led2_tcb, "Static", 16);

    /* 打印 th_monitor（动态线程） */
    rt_thread_t monitor_thread = rt_thread_find("th_monitor");
    if (monitor_thread != RT_NULL) {
        print_thread_info("th_monitor", monitor_thread, "Dynamic", 10);
    } else {
        print_thread_info("th_monitor", RT_NULL, "Dynamic", 10);
    }

    /* 打印当前运行线程 */
    rt_thread_t current = rt_thread_self();
    if (current != RT_NULL) {
        rt_kprintf("\n[Current Running Thread: %s]\n", current->name);
    }

    rt_kprintf("==============================================\n");
}

/* ===== 导出命令到 MSH ===== */
MSH_CMD_EXPORT(cmd_thread_dump, dump thread info);   /* 修复：去掉描述中的逗号 */
```

![](C:/Users/HUANG/AppData/Roaming/marktext/images/2026-08-18-23-55-18-image.png)

## Part 3：Gitee PR 提交与规范

### 1. 提交目录规范

```
2026/<组别>/<姓名>/第二天作业
├── README.md             # 包含：Part 1 理论整理 + Part 2 实验结果分析与串口截图
└── thread_study.c        # 作业源码（含动/静态线程、自动初始化、MSH导出）
```

### 2. PR 提交流程与规范

1. Fork 官方仓库 [https://gitee.com/rtthread/rsoc-rtt](https://gitee.com/rtthread/rsoc-rtt) 到个人账号。
2. 基于 `master` 新建分支：`day2-姓名拼音`。
3. 编写代码、文档并进行本地 `scons` 编译验证与星火1号烧录测试。
4. 提交 Commit。
5. 向主仓库提交 Pull Request：
   - **PR 标题**：`[作业提交] 内核与多线程培训 - <姓名>`
   - **PR 内容**：附带理论文档简述、硬件运行现象照片及 FinSH 串口日志输出。