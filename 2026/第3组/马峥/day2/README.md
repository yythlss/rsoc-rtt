# 线程的使用与内核基础实验报告（Day2）

---

## 一、实验目的

1. 掌握 RT-Thread 系统启动流程：理解从芯片复位到进入 `main()` 的完整启动链路，
   掌握 `$Sub$$main → rtthread_startup → rt_application_init → main_thread_entry`
   的调用关系，以及 `INIT_*_EXPORT` 自动初始化的执行顺序。

2. 掌握线程的创建与管理：深入理解动态线程（`rt_thread_create`）与静态线程
   （`rt_thread_init`）的区别与使用场景，熟练掌握线程的启动、挂起、恢复及销毁机制。

3. 理解线程调度机制：
   - **抢占式调度**：通过不同优先级线程，观察高优先级线程如何抢占低优先级线程的执行权。
   - **时间片轮转**：通过同优先级线程，观察线程在时间片耗尽后如何自动切换。

4. 掌握线程同步与状态转换：通过 `rt_thread_mdelay` 延时函数，观察线程从"运行"态
   转为"挂起"态，以及系统空闲时空闲线程的运行情况。

5. 学会使用调试工具：掌握调度器钩子函数（`rt_scheduler_sethook`）、`rt_kprintf`、
   日志宏（`LOG_D/LOG_I`）以及 msh 命令（`list_thread`、`free` 等）的使用方法。

6. 掌握简单外设的驱动方法：通过 `rt_pin` 引脚框架点亮/熄灭 LED、读取按键状态，
   理解 GPIO 输入输出配置与 RT-Thread 设备驱动框架的分层思想。

---

## 二、实验仪器

| 仪器/器件 | 规格/说明 |
|---|---|
| 开发板 | STM32F407 系列（星火一号） |
| 调试器 | ST-LINK |
| 计算机 | Windows 11 操作系统 |
| 开发环境 | RT-Thread Studio |
| 串口调试工具 | RT-Thread Studio 自带终端（FinSH/msh） |
| 外设模块 | 板载 LED（PF9）、按键 KEY0（PA0） |

---

## 三、实验原理

### 3.1 系统启动流程

RT-Thread 的启动过程分为"芯片级启动"和"内核级启动"两个阶段：

**芯片级（汇编）**：上电复位 → Reset_Handler（`startup_stm32f407xx.s`）→
初始化 MSP → `SystemInit()` 配置 168MHz 时钟 → `__main` → `main()`。

**内核级（C）**：RT-Thread 通过编译器符号 `$Sub$$main` 截获 `main()`，先执行
`rtthread_startup()`，完成内核初始化后再跳转回用户 `main()`：

```
main()
  └─ $Sub$$main()                           ← 编译器自动插入
       └─ rtthread_startup()                ← 内核启动总入口
            ├─ rt_hw_board_init()            板级初始化（时钟/串口/堆区）
            ├─ rt_system_tick_init()          SysTick 初始化（1ms/tick）
            ├─ rt_hw_interrupt_init()         中断向量初始化
            ├─ rt_system_scheduler_init()     调度器初始化
            ├─ rt_system_signal_init()         信号机制初始化
            ├─ rt_application_init()          创建 main 线程（优先级10）
            ├─ rt_components_init()           自动初始化链
            └─ rt_system_scheduler_start()    启动调度器
                 → main_thread_entry → 用户 main() 执行
```

> **关键点**：用户 `main()` 实际运行在一个优先级为 10 的 main 线程中，
> 调用 `rt_thread_mdelay` 会主动让出 CPU，而非死循环占死 CPU。

**自动初始化**：通过 `INIT_*_EXPORT` 宏将函数指针放入特定链接段，
`rt_components_init()` 按优先级（1→6）依次执行各注册函数，无需手动调用。

### 3.2 线程控制块（TCB）

RT-Thread 用 `struct rt_thread` 描述线程，关键字段：

| 字段 | 说明 |
|---|---|
| `name` | 线程名称 |
| `init_priority / current_priority` | 优先级（数值越小越高） |
| `stack_addr / stack_size / sp` | 线程栈（保存上下文） |
| `entry / parameter` | 入口函数与参数 |
| `stat` | 线程状态（INIT/READY/RUNNING/SUSPEND/CLOSE） |
| `init_tick / remaining_tick` | 时间片初始值/剩余值（Round-Robin 用） |

### 3.3 线程状态机

五态模型：初始 → 就绪 → 运行 → 挂起 → 关闭

| 转换 | 触发条件 |
|---|---|
| 初始 → 就绪 | `rt_thread_startup()` |
| 就绪 → 运行 | 调度器选中（最高优先级） |
| 运行 → 挂起 | `rt_thread_mdelay()` / 等待 IPC |
| 挂起 → 就绪 | 延时结束 / 资源可用 |
| 运行 → 关闭 | `return` / `rt_thread_exit()` |

### 3.4 调度机制

- **抢占式调度**：就绪队列中优先级最高（数值最小）的线程获 CPU，高优先级就绪时立即抢占。
- **时间片轮转（Round-Robin）**：同优先级线程按 tick 时间片轮转执行。
- 优先级范围 0~31，31 为 idle（最低优先级）。

### 3.5 动态线程 vs 静态线程

| 对比项 | 动态 `rt_thread_create()` | 静态 `rt_thread_init()` |
|---|---|---|
| TCB/栈分配 | 系统堆分配 | 用户全局变量/数组 |
| 生命周期 | 程序运行期可动态管理 | 编译时确定，与程序同周期 |
| 释放 | `rt_thread_delete()` | `rt_thread_detach()` |
| 适用场景 | 数量不确定，需动态管理 | 高可靠实时系统（无碎片、确定性） |

### 3.6 简单外设（GPIO：LED 与按键）

RT-Thread 提供统一的 `rt_pin_*` 引脚框架：

```c
rt_pin_mode(GET_PIN(F, 9), PIN_MODE_OUTPUT);        // 配置输出
rt_pin_write(GET_PIN(F, 9), PIN_HIGH);              // 输出高
rt_uint8_t v = rt_pin_read(GET_PIN(A, 0));          // 读取电平
```

---

## 四、实验步骤

### 4.1 实验准备

1. **创建工程**：RT-Thread Studio → File → New → RT-Thread Project，
   基于 STM32F407 系列芯片的 RT-Thread 标准版工程。
2. **连接调试器**：ST-LINK USB 连接电脑与开发板 SWD 接口，确保电源正常。
3. **检查设备管理器**：确认 ST-LINK Virtual COM Port（COM13）和 ST-LINK Debug 正常识别。
4. **配置串口**：波特率 115200，8N1，无流控。

### 4.2 实验 A：动态与静态线程 + 抢占式调度

**目的**：验证动态/静态线程创建方式，以及高优先级线程对低优先级线程的抢占。

**代码要点**：

```c
// ====== 动态线程（thread1）======
static rt_thread_t tid1 = RT_NULL;
static void thread1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    for (count = 0; count < 10; count++)
    {
        rt_kprintf("thread1 count: %d\n", count);
        rt_thread_mdelay(500);
    }
    rt_kprintf("thread1 exit\n");
}

// ====== 静态线程（thread2）======
static char thread2_stack[1024];
static struct rt_thread thread2;
static void thread2_entry(void *param)
{
    rt_uint32_t count = 0;
    for (count = 0; count < 10; count++)
    {
        rt_kprintf("thread2 count: %d\n", count);
    }
    rt_kprintf("thread2 exit\n");
}

int main(void)
{
    // 动态创建 thread1（优先级 25）
    tid1 = rt_thread_create("thread1", thread1_entry, RT_NULL,
                            512, 25, 5);
    if (tid1 != RT_NULL) rt_thread_startup(tid1);

    // 静态初始化 thread2（优先级 24，抢占 thread1）
    rt_thread_init(&thread2, "thread2", thread2_entry, RT_NULL,
                   thread2_stack, sizeof(thread2_stack), 24, 5);
    rt_thread_startup(&thread2);

    return 0;
}
MSH_CMD_EXPORT(thread_sample, thread sample);
```

**实验现象**：
- `thread2`（优先级24）先于 `thread1`（优先级25）执行，说明高优先级线程成功抢占低优先级线程。
- `thread1` 每次 `rt_thread_mdelay(500)` 后从 RUNNING 转为 SUSPEND，500ms 后恢复 READY。
- 两个线程执行完毕后系统进入 idle 空闲线程。

**编译结果**：0 errors, 0 warnings（截图：图 4-1~图 4-6）
**串口输出**：图 4-7~图 4-8

### 4.3 实验 B：时间片轮转调度

**目的**：验证同优先级线程之间的时间片轮转（Round-Robin）机制。

**代码要点**：

```c
#define THREAD_PRIORITY   20
#define THREAD_TIMESLICE  10

static void thread_entry(void* parameter)
{
    rt_uint32_t value = (rt_uint32_t)parameter;
    rt_uint32_t count = 0;
    while (1)
    {
        if (0 == (count % 5))
        {
            rt_kprintf("thread %d is running, count = %d\n", value, count);
            if (count > 200) return;
        }
        count++;
    }
}

int main(void)
{
    rt_thread_t tid = RT_NULL;
    tid = rt_thread_create("thread1", thread_entry, (void*)1, 1024, 20, 10);
    if (tid != RT_NULL) rt_thread_startup(tid);

    tid = rt_thread_create("thread2", thread_entry, (void*)2, 1024, 20, 5);
    if (tid != RT_NULL) rt_thread_startup(tid);

    return 0;
}
```

**实验现象**：
- 两个线程优先级相同（20），`thread1` 时间片 10，`thread2` 时间片 5。
- 两者交替打印 `count`，验证 Round-Robin 调度正常工作。
- `thread2` 时间片更短，切换频率更高，符合预期。
- 观察到串口输出交错（`rt_kprintf` 并发访问导致），证明两个线程确实并发运行。

**编译结果**：0 errors, 0 warnings（截图：图 4-7~图 4-9）
**串口输出**：图 4-10~图 4-12

### 4.4 实验 C：调度器钩子函数

**目的**：通过 `rt_scheduler_sethook` 捕获每次线程切换事件，直观观察上下文切换过程。

**代码要点**：

```c
static void hook_of_scheduler(struct rt_thread* from, struct rt_thread* to)
{
    rt_kprintf("from: %s --> to: %s\n", from->name, to->name);
}

int main(void)
{
    rt_scheduler_sethook(hook_of_scheduler);  // ← 先注册钩子

    rt_thread_t tid1 = rt_thread_create("thread1", thread_entry, (void*)1, 1024, 20, 10);
    rt_thread_startup(tid1);

    rt_thread_t tid2 = rt_thread_create("thread2", thread_entry, (void*)2, 1024, 20, 5);
    rt_thread_startup(tid2);

    return 0;
}
```

**实验现象**：
- 每次线程切换均打印 `from: xxx --> to: yyy`。
- 典型切换序列：`main → thread2 → thread1 → tshell → tidle → thread2 → thread1 → ...`
- `tidle`（idle 线程）频繁出现，说明用户线程因 `mdelay` 挂起时，CPU 切换至空闲线程。
- 钩子函数在调试阶段非常有用，发布时应关闭以减少开销。

**编译结果**：0 errors, 0 warnings（截图：图 4-13~图 4-14）
**串口输出**：图 4-15~图 4-17

### 4.5 实验 D：LED 闪烁 + 按键检测 + msh 调试

**目的**：掌握 `rt_pin` 框架驱动 GPIO 的方法，以及 msh 调试命令的使用。

**代码要点**：

```c
#define DBG_TAG "peripheral"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define LED_PIN  GET_PIN(F, 9)   // LED_R
#define KEY_PIN  GET_PIN(A, 0)   // KEY0

static void led_thread_entry(void *parameter)
{
    rt_uint8_t level = PIN_HIGH;
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    LOG_I("led thread start, pin=%d", LED_PIN);

    while (1)
    {
        rt_pin_write(LED_PIN, level);
        level = (level == PIN_HIGH) ? PIN_LOW : PIN_HIGH;
        rt_thread_mdelay(500);
    }
}

static void key_thread_entry(void *parameter)
{
    rt_uint8_t last_state = PIN_HIGH;
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);
    LOG_I("key thread start, pin=%d", KEY_PIN);

    while (1)
    {
        rt_uint8_t state = rt_pin_read(KEY_PIN);
        if (state != last_state)
        {
            rt_thread_mdelay(20);                      // 软件消抖
            if (rt_pin_read(KEY_PIN) == state)
            {
                if (state == PIN_LOW)
                    LOG_I("KEY0 pressed!");
                else
                    LOG_I("KEY0 released!");
                last_state = state;
            }
        }
        rt_thread_mdelay(10);
    }
}

int peripheral_sample(void)
{
    rt_thread_t led_tid  = rt_thread_create("led", led_thread_entry, RT_NULL, 512, 15, 10);
    rt_thread_t key_tid = rt_thread_create("key", key_thread_entry, RT_NULL, 512, 16, 10);
    if (led_tid)  rt_thread_startup(led_tid);
    if (key_tid)  rt_thread_startup(key_tid);
    return 0;
}
MSH_CMD_EXPORT(peripheral_sample, led blink and key sample);
```

**实验现象**：
- LED 以 500ms 为周期闪烁。
- 按下/松开 KEY0 时，串口分别打印 `KEY0 pressed!` / `KEY0 released!`。
- `msh >list_thread` 可查看 led、key 线程的优先级/状态/时间片信息。
- `msh >free` 可查看系统内存堆使用情况。

---

## 五、实验数据分析

### 5.1 动态/静态线程创建对比

| 对比项 | thread1（动态） | thread2（静态） |
|---|---|---|
| 创建函数 | `rt_thread_create()` | `rt_thread_init()` |
| 栈空间 | 系统自动分配 512 字节 | 用户手动提供 1024 字节数组 |
| 线程控制块 | 系统自动分配（返回指针） | 用户提供（全局变量） |
| 适用场景 | 数量不确定，需动态管理 | 线程数量固定，节省堆开销 |

### 5.2 抢占式调度验证

thread2（优先级 24）先于 thread1（优先级 25）执行，因为数值越小优先级越高，
高优先级线程就绪后立即抢占 CPU，完全符合 RT-Thread 抢占式调度规则。

### 5.3 时间片轮转验证

thread1（时间片 10）和 thread2（时间片 5）交替执行，打印 `count` 值交替出现。
thread2 时间片更短，切换频率更高，证明时间片大小直接影响同优先级线程的调度频率。

### 5.4 串口输出交错现象分析

两个线程并发调用 `rt_kprintf` 时，输出字符相互穿插（如 "thread 2 thread 1 is running"），
说明 `rt_kprintf` 内部未加锁，是并发访问共享资源的典型案例，也侧面证明了时间片轮转正常工作。

---

## 六、实验结论

1. **系统启动流程**：掌握了 RT-Thread 从复位到 `main()` 的完整启动链路，理解了
   `$Sub$$main` 机制、各 `INIT_*_EXPORT` 初始化函数的执行顺序，以及 main 运行在
   优先级 10 的 main 线程中的事实。

2. **动态/静态线程与抢占式调度**：验证了 `rt_thread_create` 和 `rt_thread_init`
   的正确用法，证明了高优先级线程（数值小）可成功抢占低优先级线程的 CPU。

3. **时间片轮转调度**：验证了同优先级线程按时间片 Round-Robin 轮转执行，时间片
   越小切换频率越高。

4. **调度器钩子函数**：通过 `rt_scheduler_sethook` 成功捕获每次上下文切换，直观
   展示了 main、thread1、thread2、tshell、tidle 等线程的切换序列。

5. **简单外设与 msh 调试**：通过 `rt_pin_*` 框架驱动 LED 与按键，掌握了
   `list_thread`、`free` 等 msh 调试命令的使用方法。

---

## 七、源码附录

### 实验 A 源码（动态/静态线程 + 抢占式调度）

```c
#include <rtthread.h>
#include <board.h>

#define THREAD_PRIORITY     25
#define THREAD_STACK_SIZE   512
#define THREAD_TIMESLICE    5

static rt_thread_t tid1 = RT_NULL;

/* 线程 1 入口（动态线程，有延时） */
static void thread1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    for (count = 0; count < 10; count++)
    {
        rt_kprintf("thread1 count: %d\n", count);
        rt_thread_mdelay(500);
    }
    rt_kprintf("thread1 exit\n");
}

/* 线程 2 入口（静态线程，无延时，抢占 thread1） */
static char thread2_stack[1024];
static struct rt_thread thread2;
static void thread2_entry(void *param)
{
    rt_uint32_t count = 0;
    for (count = 0; count < 10; count++)
    {
        rt_kprintf("thread2 count: %d\n", count);
    }
    rt_kprintf("thread2 exit\n");
}

int thread_sample(void)
{
    tid1 = rt_thread_create("thread1", thread1_entry, RT_NULL,
                             THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    if (tid1 != RT_NULL) rt_thread_startup(tid1);

    rt_thread_init(&thread2, "thread2", thread2_entry, RT_NULL,
                   thread2_stack, sizeof(thread2_stack),
                   THREAD_PRIORITY - 1, THREAD_TIMESLICE);
    rt_thread_startup(&thread2);

    return 0;
}
MSH_CMD_EXPORT(thread_sample, thread sample);

int main(void)
{
    thread_sample();
    return 0;
}
```

### 实验 B 源码（时间片轮转）

```c
#include <rtthread.h>
#include <board.h>

#define THREAD_STACK_SIZE   1024
#define THREAD_PRIORITY     20

static void thread_entry(void* parameter)
{
    rt_uint32_t value = (rt_uint32_t)parameter;
    rt_uint32_t count = 0;
    while (1)
    {
        if (0 == (count % 5))
        {
            rt_kprintf("thread %d is running, count = %d\n", value, count);
            if (count > 200) return;
        }
        count++;
    }
}

int main(void)
{
    rt_thread_t tid = RT_NULL;

    tid = rt_thread_create("thread1", thread_entry, (void*)1,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid != RT_NULL) rt_thread_startup(tid);

    tid = rt_thread_create("thread2", thread_entry, (void*)2,
                           THREAD_STACK_SIZE, THREAD_PRIORITY, 5);
    if (tid != RT_NULL) rt_thread_startup(tid);

    return 0;
}
```

### 实验 C 源码（调度器钩子）

```c
#include <rtthread.h>
#include <board.h>

#define THREAD_STACK_SIZE 1024
#define THREAD_PRIORITY   20

static void thread_entry(void* parameter)
{
    rt_uint32_t value = (rt_uint32_t)parameter;
    for (int count = 0; count < 10; count++)
    {
        rt_kprintf("thread %d is running\n", value);
        rt_thread_mdelay(1000);
    }
}

/* 调度器钩子：每次线程切换时自动调用 */
static void hook_of_scheduler(struct rt_thread* from, struct rt_thread* to)
{
    rt_kprintf("from: %s --> to: %s\n", from->name, to->name);
}

int main(void)
{
    rt_scheduler_sethook(hook_of_scheduler);   // ← 先注册钩子

    rt_thread_t tid1 = rt_thread_create("thread1", thread_entry, (void*)1,
                                        THREAD_STACK_SIZE, THREAD_PRIORITY, 10);
    if (tid1 != RT_NULL) rt_thread_startup(tid1);

    rt_thread_t tid2 = rt_thread_create("thread2", thread_entry, (void*)2,
                                        THREAD_STACK_SIZE, THREAD_PRIORITY, 5);
    if (tid2 != RT_NULL) rt_thread_startup(tid2);

    return 0;
}
```

### 实验 D 源码（LED 闪烁 + 按键检测 + msh 调试）

```c
#include <rtthread.h>
#include <rtdevice.h>

#define DBG_TAG  "peripheral"
#define DBG_LVL  DBG_LOG
#include <rtdbg.h>

#define LED_PIN  GET_PIN(F, 9)   // LED_R
#define KEY_PIN  GET_PIN(A, 0)   // KEY0

static void led_thread_entry(void *parameter)
{
    rt_uint8_t level = PIN_HIGH;
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    LOG_I("led thread start, pin=%d", LED_PIN);

    while (1)
    {
        rt_pin_write(LED_PIN, level);
        level = (level == PIN_HIGH) ? PIN_LOW : PIN_HIGH;
        rt_thread_mdelay(500);
    }
}

static void key_thread_entry(void *parameter)
{
    rt_uint8_t last_state = PIN_HIGH;
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);
    LOG_I("key thread start, pin=%d", KEY_PIN);

    while (1)
    {
        rt_uint8_t state = rt_pin_read(KEY_PIN);
        if (state != last_state)
        {
            rt_thread_mdelay(20);
            if (rt_pin_read(KEY_PIN) == state)
            {
                if (state == PIN_LOW)
                    LOG_I("KEY0 pressed!");
                else
                    LOG_I("KEY0 released!");
                last_state = state;
            }
        }
        rt_thread_mdelay(10);
    }
}

int peripheral_sample(void)
{
    rt_thread_t led_tid  = rt_thread_create("led", led_thread_entry, RT_NULL, 512, 15, 10);
    rt_thread_t key_tid = rt_thread_create("key", key_thread_entry, RT_NULL, 512, 16, 10);
    if (led_tid)  rt_thread_startup(led_tid);
    if (key_tid)  rt_thread_startup(key_tid);
    return 0;
}
MSH_CMD_EXPORT(peripheral_sample, led blink and key sample);
```
