

## 一、裸机开发模式及其缺陷

- **前后台模式**
  - **后台**：主循环（如 `while(1)`）按顺序执行任务（读取 ADC、刷新 LCD 等）。
  - **前台**：中断处理（如 SPI、串口接收中断），打断后台执行。
- **主要缺陷**
  - **并发性**：缺乏真正的并发处理能力，阻塞操作会卡住整个系统。
  - **实时性**：后台任务阻塞（如 `delay`）会严重影响系统响应。
  - **可维护性**：代码与硬件强绑定，移植困难。
  - **可重用性**：模块化程度低，重复造轮子。

---

## 二、RTOS 引入及线程概念

- **引入目的**：通过线程（任务）机制解决裸机缺陷。
- **核心思想**：**分而治之**——将复杂任务拆分为多个独立线程，实现并发执行。
- **示例**：ADC 读取、SPI 通信等可拆分为独立线程，避免相互阻塞。

---

## 三、RTOS 启动流程

1. **芯片上电**：执行启动文件（如 `startup.s`）中的 `Reset_Handler`。
2. **底层初始化**：清 BSS 段、初始化 SRAM 等芯片级操作。
3. **工具链层**：
   - 关闭硬件总中断，保证启动稳定。
   - 调用入口函数（如 `entry`），完成系统初始化。
4. **关键初始化步骤**：
   - 初始化 UART（用于终端打印）。
   - 初始化定时器（提供延时计时）。
   - 初始化调度器（管理线程运行）。
   - 可选：信号初始化。
   - 应用线程初始化。
5. **自动初始化**：RT-Thread 通过自动初始化机制（`INIT_*_EXPORT` 宏 + 初始化函数表，由 `rt_components` 组件负责扫描执行）在 `main` 函数之前自动调用各组件的初始化函数，无需手动调用。
6. **调度器启动**：系统开始按优先级调度线程。

> **自动初始化宏分级**（按执行先后）：`INIT_BOARD_EXPORT` → `INIT_DEVICE_EXPORT` → `INIT_COMPONENT_EXPORT` → `INIT_ENV_EXPORT` → `INIT_APP_EXPORT`。板级（board）最早执行，应用层（app）最后执行。

---

## 四、线程控制块（TCB）

- **作用**：线程的"身份证"和"档案袋"，记录线程所有关键信息。
- **关键成员**：
  - `sp`（栈指针）：指向线程独立栈空间，记录栈大小和起始地址。
  - `entry`（入口函数）：线程执行的起点。
  - `parameter`：传递给入口函数的参数。
  - 错误代码、运行状态、优先级、退出钩子函数、定时器等。
- **面向对象思想**：用 C 语言模拟继承，`rt_thread` 结构体嵌套 `rt_object` 基类，提供统一属性（name、type、flag 等）。

---

## 五、线程栈与上下文切换

- **独立栈空间**：每个线程拥有自己的栈，用于保存局部变量和函数调用。
- **栈溢出**：RTOS 中特指线程栈溢出，需注意栈大小分配（栈太小会越界破坏其他内存）。
- **上下文切换**：
  - 切换时保存当前线程现场（寄存器、程序计数器等）到自身栈中。
  - 恢复下一个线程的上下文，实现无缝接续执行。

---

## 六、线程状态及生命周期

- **五种状态**：
  1. **初始态**：线程刚被创建，未启动。
  2. **就绪态**：等待 CPU 资源。
  3. **运行态**：正在占用 CPU 执行。
  4. **挂起态**：因延时、信号量等待等暂时让出 CPU。
  5. **关闭态**：线程结束，等待资源回收。
- **状态转换**：
  - 创建 → 初始态 → 调用 `rt_thread_startup()` → 就绪态 → 调度运行 → 运行态。
  - 运行态可通过 `delay`、信号量获取等进入挂起态，挂起结束后恢复就绪态。
  - 线程退出后进入关闭态，由 **idle 线程** 回收资源。
- **idle 线程（空闲线程）**：
  - 最低优先级线程，系统"兜底"线程。默认 256 级优先级时为 **255**，裁剪为 32 级时对应 **31**；该优先级固定保留，**用户线程不可使用**。
  - 永远处于就绪态，仅当无其他就绪线程时运行。
  - 负责资源回收（清理关闭态线程）、低功耗管理、喂狗等。

---

## 七、线程调度策略

- **抢占式优先级调度**
  - 优先级数字越小等级越高：**0 为最高，255 为最低**。
  - 高优先级线程一旦就绪，立即抢占低优先级线程。
  - 典型默认优先级：main 线程（如 10）、timer 线程（如 4）、idle 线程（最低，固定 255）。
- **时间片轮转**
  - 仅适用于**同优先级**线程。
  - 每个线程分配时间片（如 10ms），超时后让出 CPU 给同优先级下一个线程，实现公平分时。
- **调度触发**
  - 调度器启动后，从就绪列表选取最高优先级线程运行。
  - 实际线程切换由 **PendSV 异常**完成（在中断退出时触发，保证不会打断其他中断）。

### 7.1 优先级与时间片数值详解

- **优先级范围**：系统支持 0~255 共 256 个优先级级别，**0 最高、255 最低**；可通过 `rtconfig.h` 裁剪为 **8 级或 32 级**，以节省资源，配置灵活。
- **idle 独占最低优先级**：最低优先级固定保留给 idle 线程，用户线程不可占用。
- **抢占规则**：高优先级就绪即抢占。例：任务 A 优先级 0、任务 B 优先级 10，若 A 就绪，B 会被立刻换出；只有 A 执行完且无更高优先级任务时，B 才能继续。
- **时间片单位 tick**：1 tick 默认 **10ms**（可在 `rtconfig.h` 改为 1ms 等）。
- **同优先级轮转**：优先级相同的线程按各自时间片轮转。例：线程 A、B 优先级均为 10，A 时间片 20 tick、B 时间片 100 tick，则 CPU 按 `A→B→A→B…` 轮流分配。时间片设置过小会导致**频繁切换**反而降低效率，需权衡。

---

## 八、线程创建方式

RT-Thread 提供两种创建方式，本质都是分配"线程控制块 + 栈空间"，区别仅在内存来源。

- **静态创建**（`rt_thread_init`）：
  - 预先分配线程控制块和栈空间（如全局数组）。
  - 优点：内存分配确定、无碎片，适合资源受限或高安全场景（如军工）。
- **动态创建**（`rt_thread_create`）：
  - 从系统堆中动态申请控制块和栈。
  - 优点：灵活，但存在内存碎片风险与不确定性。
- **创建后**：线程处于初始态，必须调用 `rt_thread_startup()` 进入就绪态。

### 8.1 代码示例

动态创建（从堆分配）：

```c
#include <rtthread.h>

static void thread_entry(void *parameter)
{
    rt_uint32_t count = 0;
    while (1)
    {
        rt_kprintf("dynamic thread count: %d\n", count++);
        rt_thread_mdelay(500);   /* 延时 500ms，单位毫秒 */
    }
}

int main(void)
{
    rt_thread_t tid;

    tid = rt_thread_create("dynamic",     /* 线程名称（用于调试显示） */
                           thread_entry,  /* 线程入口函数 */
                           RT_NULL,       /* 入口参数 */
                           512,           /* 线程栈大小（字节） */
                           10,            /* 优先级：数字越小越高 */
                           10);           /* 时间片（单位 tick） */

    if (tid != RT_NULL)
        rt_thread_startup(tid);             /* 启动线程，进入就绪态 */
    else
        rt_kprintf("create thread failed!\n");

    return 0;
}
```

静态创建（使用预先分配的数组）：

```c
#include <rtthread.h>

#define THREAD_STACK_SIZE  512
#define THREAD_PRIORITY    10
#define THREAD_TIMESLICE   10

static struct rt_thread  static_thread;                 /* 预先分配的控制块 */
static rt_uint8_t        static_thread_stack[THREAD_STACK_SIZE]; /* 预先分配的栈 */

static void thread_entry(void *parameter)
{
    rt_uint32_t count = 0;
    while (1)
    {
        rt_kprintf("static thread count: %d\n", count++);
        rt_thread_mdelay(500);
    }
}

int main(void)
{
    rt_err_t result;

    result = rt_thread_init(&static_thread,        /* 线程控制块 */
                            "static",              /* 线程名称 */
                            thread_entry,          /* 入口函数 */
                            RT_NULL,               /* 入口参数 */
                            static_thread_stack,   /* 栈起始地址 */
                            THREAD_STACK_SIZE,     /* 栈大小 */
                            THREAD_PRIORITY,       /* 优先级 */
                            THREAD_TIMESLICE);     /* 时间片 */

    if (result == RT_EOK)
        rt_thread_startup(&static_thread);
    else
        rt_kprintf("init thread failed!\n");

    return 0;
}
```

> 注意：静态方式要求栈数组为**全局/静态**变量（生命周期与线程一致）；动态方式需保证系统堆（`RT_HEAP_SIZE`）足够。

### 8.2 常用线程管理 API

| API | 作用 |
| :-- | :-- |
| `rt_thread_create()` | 动态创建线程（从堆分配） |
| `rt_thread_init()` | 静态初始化线程（使用指定内存） |
| `rt_thread_startup()` | 启动线程，使其进入就绪态 |
| `rt_thread_delete()` | 删除动态线程（释放堆内存） |
| `rt_thread_detach()` | 脱离静态线程（不释放内存） |
| `rt_thread_yield()` | 主动让出 CPU，切换到同优先级其他线程 |
| `rt_thread_mdelay(ms)` | 毫秒级延时（进入挂起态） |
| `rt_thread_sleep(tick)` | tick 级延时 |
| `rt_thread_self()` | 获取当前线程句柄 |
| `rt_thread_control()` | 控制线程（如修改优先级、挂起/恢复） |
| `rt_thread_suspend()` / `rt_thread_resume()` | 挂起 / 恢复线程 |

---

## 九、SCons 构建系统

- **简介**：基于 Python 的构建工具，用于编译 RT-Thread 项目，替代繁琐的 Makefile。
- **核心文件**
  - `SConstruct`：顶层构建脚本。
  - `SConscript`：分散在各源码目录，控制当前目录的构建行为。
- **常用命令**
  - `scons`：编译项目。
  - `scons -jN`：多线程加速（如 `-j20`）。
  - `scons -c`：清理编译产物。
  - `scons --target=mdk` / `--target=cmake`：生成 IDE 工程文件。
- **关键语法**
  - `glob`：匹配源文件。
  - `DefineGroup`：添加文件分组，精细控制参与编译的文件。
  - `depend`：依赖宏控制脚本是否生效（宏需在 `rtconfig.h` 中启用）。
  - `rtconfig.h`：全局配置，由 Kconfig 生成，集中管理所有宏。

### 9.1 实战避坑指南

- **工具链环境变量 `RTT_EXEC_PATH`**：SCons 通过该变量定位编译器。使用非 ARM 平台或自定义编译器时需手动指定：
  - Windows（CMD）：`set RTT_EXEC_PATH=编译器路径` 后执行 `scons`。
  - Linux/macOS（bash）：`export RTT_EXEC_PATH=编译器路径`。
  - 未正确设置会报"找不到编译器"等错误。
- **`rtconfig.py` 切换编译器**：编译器和参数统一在此配置，需按工具链（GCC / Keil AC5/AC6 / IAR / LLVM）设置 `ARCH`（架构）、`CPU`（指令集）、`FPU`（浮点类型）等，否则编译结果不正确。
- **修改配置后先清理再编译**：改了 `rtconfig.h` 或 `SConscript` 后若出现"奇怪的错误"或配置未生效，先 `scons -c` 清理旧产物再 `scons` 重编，通常能解决。
- **生成 IDE 工程**：`--target=mdk` 生成 Keil 工程；`--target=cmake` 生成 CMake 脚本可导入 VS Code / VS / CLion；生成 IAR 工程需本机已装 IAR。
- **跨平台路径**：Windows 用 `\`、Linux/macOS 用 `/`；在 `SConscript` 中建议用 Python `os.path` 处理路径。Linux 下注意脚本可执行权限。

---

## 十、开发环境与实践要点

- **RT-Thread Studio**：集成开发环境，支持图形化配置、软件包管理。
- **工程配置**：通过 `rtconfig.h` 和 `menuconfig` 调整功能。
- **软件包**：云端拉取，如 `Kernel Simple`，丰富外设驱动和组件。
- **调试技巧**：
  - 使用 `list_thread` 或 `ps` 命令查看线程状态。
  - Tab 键补全命令。
  - 注意下载器（ST-Link / J-Link）和串口的匹配。

### 10.1 GPIO 设备模型与 MSH 命令导出（LED 实战示例）

翻转 LED 本质是操作引脚设备。RT-Thread 提供统一的 PIN 设备接口，无需直接操作寄存器：

```c
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* 以 STM32 为例，PC13 接 LED；请按开发板原理图替换为实际引脚编号 */
#define RED_LED_PIN    GET_PIN(C, 13)

/* 动态线程：翻转红色 LED（对应作业"优先级 16"） */
static void red_led_entry(void *parameter)
{
    rt_pin_mode(RED_LED_PIN, PIN_MODE_OUTPUT);   /* 配置为推挽输出 */
    while (1)
    {
        rt_pin_write(RED_LED_PIN, PIN_HIGH);      /* 点亮 */
        rt_thread_mdelay(500);
        rt_pin_write(RED_LED_PIN, PIN_LOW);       /* 熄灭 */
        rt_thread_mdelay(500);
    }
}

int led_sample(void)
{
    rt_thread_t tid = rt_thread_create("red_led", red_led_entry,
                                       RT_NULL, 512, 16, 10);
    if (tid != RT_NULL)
        rt_thread_startup(tid);
    return 0;
}

/* 导出为 MSH 命令：在终端输入 led_sample 即可运行 */
MSH_CMD_EXPORT(led_sample, toggle red led by thread);
```

> - 静态线程（作业中的"蓝色 LED"）只需把上例的 `rt_thread_create` 换成 `rt_thread_init` + 预分配栈即可。
> - `GET_PIN(port, pin)` 是 RT-Thread 的引脚编号宏；`PIN_HIGH`/`PIN_LOW`/`PIN_MODE_OUTPUT` 为设备框架定义的常量。
> - 除 MSH 导出外，也可通过 `INIT_APP_EXPORT(led_sample)` 让函数在开机时自动执行。

