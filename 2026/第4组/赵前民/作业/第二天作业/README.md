## Part1：STM32F407 启动流程与 RT-Thread 引导机制

### 1.1 从上电复位 Reset_Handler 到 main() 完整调用链

STM32F407 在裸机与 RT-Thread 系统下启动流程不同，RT-Thread 会接管启动流程，在调度器启动后再进入用户 main 函数，完整调用链如下：

```Plain
芯片上电复位
↓
硬件自动读取 0x00000004 复位向量，跳转执行 Reset_Handler（汇编启动文件）
↓
Reset_Handler
    1. 初始化栈指针 SP
    2. 调用 SystemInit() 配置系统时钟、中断向量表、FPU
    3. 跳转至 rt_thread_startup()（RT-Thread 内核入口）
↓
rt_thread_startup()
    1. rt_hw_board_init()        // 板级硬件初始化
    2. rt_system_heap_init()     // 系统堆内存初始化
    3. rt_system_scheduler_init()// 调度器初始化
    4. 内核对象、定时器、空闲线程初始化
    5. 执行 INIT_BOARD_EXPORT 注册的板级初始化函数
    6. rt_system_scheduler_start() // 开启调度器
↓
调度器切换至主线程
↓
进入 main() 函数
↓
自动执行 INIT_APP_EXPORT 注册的应用初始化函数
```

### 1.2 自动初始化原理：INIT_BOARD_EXPORT 与 INIT_APP_EXPORT

RT-Thread 自动初始化基于 **GCC 自定义段属性**实现，无需用户手动调用初始化函数，内核开机自动遍历执行。

#### 底层实现原理

两个宏本质是对 `__attribute__((section()))` 的封装，将自定义初始化函数的函数指针存放至链接脚本定义的专属内存段中：

- **INIT_BOARD_EXPORT(fn)**：将函数指针放入 `.rti_board` 段
- **INIT_APP_EXPORT(fn)**：将函数指针放入 `.rti_app` 段

链接阶段，编译器将所有同等级初始化函数指针连续排列，形成函数指针数组；内核启动时遍历该段内存，依次执行所有注册函数，实现自动初始化。

#### 执行时机与区别

- **INIT_BOARD_EXPORT**：调度器启动前执行，无线程环境，仅可做硬件底层初始化，禁止阻塞 API。
- **INIT_APP_EXPORT**：进入 main 函数、调度器正常运行后执行，具备完整线程环境，可使用系统各类 API。

## 2 内核基础与对象模型

### 2.1 对象容器与线程控制块

RT-Thread 采用 **面向对象设计思想**，将线程、信号量、消息队列、互斥锁等所有内核资源统一抽象为内核对象，通过统一的对象容器管理，支持对象遍历、统计、调试与资源管理，提升内核通用性与可维护性。

线程控制块 `struct rt_thread` 是描述线程的核心数据结构，保存线程全部属性与运行上下文，关键成员作用如下：

- **stat（线程状态）**：标记线程当前状态（初始、就绪、运行、挂起、关闭），是调度器判断线程是否参与调度的核心依据。
- **current_priority（当前优先级）**：决定线程抢占优先级，数值越小优先级越高，支持运行时动态修改，是抢占式调度的核心参数。
- **init_tick（时间片）**：用于同优先级时间片轮转调度，系统滴答中断中递减，计数归零时线程主动让出 CPU。
- **sp（栈指针）**：保存线程私有栈的栈顶地址，线程切换时用于保存/恢复寄存器上下文，保证线程切换后继续断点执行。

### 2.2 静态对象与动态对象区别

| 对比维度     | 静态对象                       | 动态对象                             |
| :----------- | :----------------------------- | :----------------------------------- |
| 内存分配位置 | 编译期分配，位于 BSS/DATA 段   | 运行期分配，取自系统 Heap 堆内存     |
| 生命周期     | 贯穿整个系统运行周期           | 创建后生效，删除后失效               |
| 释放机制     | 无需手动释放，仅可脱离内核管理 | 调用 rt_thread_delete 主动释放堆内存 |
| 运行开销     | 无运行时分配开销，无内存碎片   | 存在分配开销，长期运行易产生碎片     |

#### 高可靠场景强制使用静态线程的原因

1. **杜绝内存分配失败风险**：动态线程依赖堆内存，长期运行会产生内存碎片，极端情况下 malloc 分配失败导致任务创建失败；静态线程编译期固定分配内存，运行零风险。
2. **运行时序可确定**：静态内存地址、大小固定，系统资源可量化、可预估，满足工业实时系统的确定性要求。
3. **无内存泄漏问题**：无需手动释放内存，避免动态对象误删、漏删导致的内存泄漏故障。

## 3 线程状态机与调度算法

### 3.1 RT-Thread 线程五态模型

RT-Thread 标准五态：初始态、就绪态、运行态、挂起态、关闭态，各状态转换规则与对应 API 如下：

- **初始态（RT_THREAD_INIT）**：线程初始化完成，未加入就绪队列。创建 API：`rt_thread_init` / `rt_thread_create`；调用 `rt_thread_startup` 转为就绪态。
- **就绪态（RT_THREAD_READY）**：线程资源就绪，等待 CPU 时间片。可被高优先级任务抢占，或时间片轮转获取 CPU；调用`rt_thread_suspend` 进入挂起态。
- **运行态（RT_THREAD_RUNNING）**：当前占用 CPU 正在执行的线程。高优先级任务就绪则被抢占回就绪态；时间片耗尽回归就绪态；执行阻塞 API 进入挂起态。
- **挂起态（RT_THREAD_SUSPEND）**：线程阻塞休眠，不参与调度。触发方式：延时、等待信号量、主动挂起；资源到位/延时结束/主动唤醒（`rt_thread_resume`）转为就绪态。
- **关闭态（RT_THREAD_CLOSE）**：线程执行结束，退出调度队列。触发方式：线程函数 return、`rt_thread_exit`。

### 3.2 调度机制剖析

#### 3.2.1 抢占式调度

RT-Thread 核心实时机制，规则：**只要更高优先级线程变为就绪态，立即抢占当前低优先级线程的 CPU 使用权**。

抢占行为多发生在中断退出阶段，中断内唤醒高优先级任务，退出中断时触发上下文切换，保证紧急任务优先执行，保障系统实时性。不同优先级线程完全遵循高优先执行原则。

#### 3.2.2 时间片轮转调度

时间片轮转仅针对 **同一优先级** 的多个就绪线程。

系统每个线程拥有固定时间片`init_tick`，SysTick 滴答中断驱动计数递减；当前线程时间片耗尽后，主动让出 CPU，同优先级下一个就绪线程获得执行权并重置时间片。若线程提前阻塞，剩余时间片直接作废。

### 总结

RT-Thread 调度核心规则：**不同优先级抢占优先，同优先级时间片轮转**，兼顾系统实时性与多任务公平调度。



## Part2： 多线程实战编程

基于 STM32F407 平台，完成动态线程、静态线程、线程状态监控、线程挂起/恢复、自动初始化、控制台命令导出全套实战功能。

### 1 工程配置与全局定义

提前完成板载 LED、按键 GPIO 初始化。

```c
#include <stm32f4xx_hal.h>

/*************************引脚定义**************************/
#define LED_Port        GPIOF
#define LED1_Pin        GPIO_PIN_11
#define LED2_Pin        GPIO_PIN_12
//使用按键btn_down
#define Key_Port        GPIOC
#define Key_Pin         GPIO_PIN_1

/********************* 全局静态线程定义 *********************/
// 静态线程2栈大小、TCB控制块定义
#define TH_LED2_STACK_SIZE    2048
rt_uint8_t th_led2_stack[TH_LED2_STACK_SIZE];
struct rt_thread th_led2_tcb;

/********************* 线程句柄定义 *********************/
rt_thread_t th_led1_handle = RT_NULL;
rt_thread_t th_monitor_handle = RT_NULL;

/********************* 计数变量 *********************/
static rt_uint32_t led1_cnt = 0;
static rt_uint32_t led2_cnt = 0;
static rt_uint8_t key_flag = 0;
static rt_uint8_t task_flag = 0;
```

### 2 板载硬件底层驱动

适配 STM32F407 板载 LED、按键基础驱动

```c
void gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 使能GPIO时钟
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // 初始化PF11、PF12为推挽输出（LED）
    GPIO_InitStruct.Pin = LED1_Pin | LED2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(LED_Port, LED1_Pin | LED2_Pin, GPIO_PIN_RESET);

    // 初始化PC1为上拉输入（按键）
    GPIO_InitStruct.Pin = Key_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(Key_Port, &GPIO_InitStruct);
}
INIT_BOARD_EXPORT(gpio_init);

// LED 翻转
void led_toggle(uint16_t GPIO_Pin)
{
    HAL_GPIO_TogglePin(LED_Port, GPIO_Pin);
}

rt_bool_t key_scan(void)
{
    if ((HAL_GPIO_ReadPin(Key_Port, Key_Pin) == GPIO_PIN_RESET))
    {
        rt_thread_mdelay(20);
        if(HAL_GPIO_ReadPin(Key_Port, Key_Pin) == GPIO_PIN_RESET)
            return RT_TRUE;
    }

    return RT_FALSE;
}
```

### 3 线程入口函数实现

#### 3.1 动态线程1：LED_R 闪烁线程

优先级16、时间片5ticks、栈1024Byte，循环翻转LED并打印计数与系统时钟节拍

```c
void th_led1_entry(void *parameter)
{
    while(1)
    {
        if(task_flag) {
            rt_kprintf("Suspend th_led1 success!\n");
            rt_thread_suspend(th_led1_handle);
            rt_schedule();
        }

        led_toggle(LED1_Pin);
        led1_cnt++;
        rt_kprintf("[LED1] Count: %d, SysTick: %d\n", led1_cnt, rt_tick_get());
        rt_thread_mdelay(500);
    }
}
```

#### 3.2 静态线程2：LED_B 闪烁线程

静态TCB+静态栈，同优先级、同时间片配置，循环翻转LED并打印计数

```c
void th_led2_entry(void *parameter)
{
    while(1)
    {
        led_toggle(LED2_Pin);
        led2_cnt++;
        rt_kprintf("[LED2] Count: %d\n", led2_cnt);
        rt_thread_mdelay(500);
    }
}
```

#### 3.3 监控控制线程（高优先级）

优先级10，2秒周期执行，打印线程状态、剩余栈空间，响应按键挂起/恢复LED1线程

```c
void th_monitor_entry(void *parameter)
{
    while(1)
    {
        // 按键单次触发：切换LED1线程挂起/恢复状态
        if(key_scan())
        {
            if (key_flag)
            {
                task_flag = 0;
                rt_thread_resume(th_led1_handle);
                rt_kprintf("Resume th_led1 success!\n");
            }
            else
                task_flag = 1;

            key_flag = !key_flag;
        }

        // 打印线程运行状态与剩余栈空间
        rt_kprintf("\n========== Thread Status Info ==========\n");
        rt_kprintf("[LED1 Thread] State: %d\n", th_led1_handle->stat);

        // 静态线程直接读取TCB
        rt_kprintf("[LED2 Thread] State: %d\n", th_led2_tcb.stat);
        rt_kprintf("========================================\n\n");

        rt_thread_mdelay(2000);
    }
}
```

### 4 线程初始化与自动加载

通过 `INIT_APP_EXPORT` 自动初始化，无需main函数硬编码调用，区分动态/静态线程创建方式

```c
void thread_test_init(void)
{
    /***** 动态创建 LED1 线程 *****/
    th_led1_handle = rt_thread_create("th_led1",th_led1_entry,RT_NULL,2048,16,5);
    if(th_led1_handle != RT_NULL)
    {
        rt_thread_startup(th_led1_handle);
        rt_kprintf("[Init] th_led1 create success!\n");
    }

    /***** 静态初始化 LED2 线程 *****/
    rt_thread_init(&th_led2_tcb,"th_led2",th_led2_entry,RT_NULL,th_led2_stack,TH_LED2_STACK_SIZE,16,5);
    rt_thread_startup(&th_led2_tcb);
    rt_kprintf("[Init] th_led2 static init success!\n");

    /***** 动态创建监控线程 *****/
    th_monitor_handle = rt_thread_create("th_monitor",th_monitor_entry,RT_NULL,2048,10,5);
    if(th_monitor_handle != RT_NULL)
    {
        rt_thread_startup(th_monitor_handle);
        rt_kprintf("[Init] th_monitor create success!\n");
    }
}
INIT_APP_EXPORT(thread_test_init);
```

### 5 自定义控制台调试命令

通过 `MSH_CMD_EXPORT` 导出 thread_dump 命令，手动查看线程详细信息

```c
void thread_dump_cmd(void)
{
    rt_kprintf("\n===== Custom Thread Dump Info =====\n");
    rt_kprintf("\nLED1 Count: %d\n", led1_cnt);
    rt_kprintf("LED2 Count: %d\n", led2_cnt);
    rt_kprintf("LED1 Suspend State: %s\n", th_led1_handle->stat);
}
MSH_CMD_EXPORT(thread_dump_cmd, dump custom thread info);
```

### 6 功能说明与核心要点

#### 6.1 线程参数校验

- **th_led1（动态）**：优先级16、时间片5ticks、栈大小1024Byte，同优先级与LED2线程时间片轮转调度
- **th_led2（静态）**：全局静态栈+静态TCB，无堆内存分配，高可靠运行
- **th_monitor（监控）**：优先级10，高于工作线程，可抢占工作线程，保证监控任务实时执行

#### 6.2 核心功能逻辑

- 双LED线程同优先级轮转运行，分别打印独立计数与系统节拍
- 按键单次触发切换LED1线程挂起/恢复状态，线程状态实时生效
- 监控线程2秒周期打印双线程状态、剩余栈空间，实现线程运行监控
- 所有业务自动初始化，不占用main函数资源，符合RT-Thread开发规范
- 自定义控制台命令，支持手动查询线程状态与运行计数

### 7 实验现象总结

#### 7.1串口数据分析

![image-20260818223455183](C:\Users\loong dragen\桌面\RTT学习\source\day02\image-20260818223455183.png)

**可以看到当按键按下之前，led1任务和led2任务都在执行；按下后led1任务被挂起，之后就只有led2任务的输出**



![image-20260818223700152](C:\Users\loong dragen\桌面\RTT学习\source\day02\image-20260818223700152.png)

**可以看到当按键再次按下，led1任务被恢复，led1任务和led2任务都有输出**



#### 7.2 实验结果总结

- 系统上电后，红蓝LED交替闪烁，串口持续打印两个线程的运行计数
- 每2秒打印一次线程状态
- 按下板载按键，LED1线程挂起，红灯停止闪烁；再次按下恢复运行
- 控制台输入 `thread_dump`，可打印系统所有线程信息与自定义运行参数