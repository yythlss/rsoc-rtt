# 1.  新建工程
**Step 1：打开 RT-Thread Studio，选择"基于芯片创建工程"**
点击菜单栏 `文件 → 新建 → RT-Thread 项目`，在弹出的对话框中选择 **"基于芯片"** 选项卡。
**Step 2：配置芯片参数**
![[贡献周/第三章/figures/Pasted image 20260829134756.png]] 
**Step 3：点击"完成"创建工程**
工程创建完成后，RT-Thread Studio 会自动生成基础工程框架，包含：

```
项目名称/
├── applications/          # 用户应用代码
│   └── main.c            # 主程序入口
├── board/                 # 板级支持包
│   ├── board.c           # 板级初始化（时钟配置等）
│   ├── board.h           # 板级头文件
│   ├── CubeMX_Config/    # CubeMX 配置文件
│   └── linker_scripts/   # 链接脚本
├── libraries/             # 库文件
│   ├── HAL_Drivers/      # HAL 驱动层
│   └── Board_Drivers/    # 板级驱动
├── rt-thread/             # RT-Thread 内核源码
├── Kconfig               # 内核配置菜单
├── rtconfig.h            # RT-Thread 配置头文件
├── .config               # 内核配置文件
└── SConscript            # SCons 构建脚本
```

# 2.  RT-Thread setting的配置
## 使能DMA
![[贡献周/第三章/figures/Pasted image 20260828132951.png]]

# 3.  board.c/h 的配置（包括引脚的初始化，配合cubemx）
### board.h 配置

`board.h` 文件定义了芯片的 SRAM 和 Flash 地址范围，以及堆内存起止地址：

```c
#ifndef __BOARD_H__
#define __BOARD_H__

#include <rtthread.h>
#include <stm32f4xx.h>
#include "drv_common.h"
#include "drv_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STM32_SRAM_SIZE        (128)
#define STM32_SRAM_END         (0x20000000 + STM32_SRAM_SIZE * 1024)

#define STM32_FLASH_START_ADRESS     ((uint32_t)0x08000000)
#define STM32_FLASH_SIZE             (1024 * 1024)
#define STM32_FLASH_END_ADDRESS      ((uint32_t)(STM32_FLASH_START_ADRESS + STM32_FLASH_SIZE))

#if defined(__ARMCC_VERSION)
extern int Image$$RW_IRAM1$$ZI$$Limit;
#define HEAP_BEGIN      ((void *)&Image$$RW_IRAM1$$ZI$$Limit)
#elif __ICCARM__
#pragma section="CSTACK"
#define HEAP_BEGIN      (__segment_end("CSTACK"))
#else
extern int __bss_end;
#define HEAP_BEGIN      ((void *)&__bss_end)
#endif

#define HEAP_END        STM32_SRAM_END

void SystemClock_Config(void);

#ifdef __cplusplus
}
#endif

#endif
```

| 宏定义                        | 值                  | 说明            |
| -------------------------- | ------------------ | ------------- |
| `STM32_FLASH_START_ADRESS` | `0x08000000`       | 片内 Flash 起始地址 |
| `STM32_FLASH_SIZE`         | `1024 * 1024`（1MB） | 片内 Flash 总容量  |
| `STM32_SRAM_SIZE`          | `128`（128KB）       | 片内 SRAM 大小    |
| `HEAP_BEGIN`               | bss 段末尾            | 堆内存起始地址       |
| `HEAP_END`                 | `STM32_SRAM_END`   | 堆内存结束地址       |

### board.c 配置（系统时钟初始化）

`board.c` 中的 `SystemClock_Config()` 函数负责配置系统时钟，使用 CubeMX 生成的 HAL 库代码。本例使用 **HSE（外部 8MHz 晶振）+ PLL**，系统时钟 168MHz：

```c
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /* 配置时钟源：HSE + LSE + LSI */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE
                              |RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  HAL_RCC_OscConfig(&RCC_OscInitStruct);

  /* 配置系统时钟树 */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);

  /* 配置 RTC 时钟源为 LSE */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct);
}
```

#### 时钟树总结：

|时钟域|时钟源|频率|
|---|---|---|
|HSE|外部 8MHz 晶振|8 MHz|
|PLL|HSE × PLLN / PLLM / PLLP|168 MHz (SYSCLK)|
|HCLK|SYSCLK / 1|168 MHz|
|APB1|HCLK / 4|42 MHz|
|APB2|HCLK / 2|84 MHz|
# 4.  UART 设备框架说明
### 什么是 UART

- `UART`（Universal Asynchronous Receiver/Transmitter，通用异步收发器）是嵌入式开发中最常用的外设之一，用于实现设备间的全双工异步串行通信。
- 它通过两根信号线（TX 发送、RX 接收）完成数据传输，**无需时钟线，双方约定好波特率即可通信。**
- 在 STM32F407 开发板上，**UART1** 通过板载 USB 转串口芯片连接到 PC，是调试和日志输出的主要通道。
### RT-Thread UART 设备驱动框架
RT-Thread 将 UART 抽象为标准的 I/O 设备，开发者通过统一的设备操作接口访问串口：

```
应用程序
   │
   ▼
rt_device_find / rt_device_open / rt_device_read / rt_device_write
   │
   ▼
RT-Thread I/O 设备管理层
   │
   ▼
串口设备驱动（drv_usart.c）
   │
   ▼
HAL 库（STM32 HAL UART）
   │
   ▼
硬件（UART 外设）
```

这种分层设计使得上层应用代码与底层硬件解耦，更换 MCU 时只需替换底层驱动，应用层代码无需修改。
### 三种数据接收模式

| 模式         | 原理                        | 优点             | 缺点                 | 适用场景          |
| ---------- | ------------------------- | -------------- | ------------------ | ------------- |
| **轮询模式**   | 主循环中不断调用 `rt_device_read` | 实现简单，无需中断      | 阻塞 CPU，可能丢数据       | 数据量极小、对实时性无要求 |
| **中断模式**   | 硬件接收中断触发 `rx_indicate` 回调 | 不阻塞 CPU，响应及时   | 频繁中断，高波特率下 CPU 负载高 | 常规数据收发，数据量适中  |
| **DMA 模式** | DMA 控制器自动搬运数据到内存          | CPU 零负载，适合大数据量 | 依赖 DMA 通道，配置稍复杂    | 高频大数据量传输      |
### RT-Thread 设备驱动 API 总结

|API 函数|功能|说明|
|---|---|---|
|`rt_device_find(name)`|根据名称查找设备|返回设备句柄，找不到返回 `RT_NULL`|
|`rt_device_open(dev, flags)`|打开设备|flags 决定读写模式（轮询/中断/DMA）|
|`rt_device_read(dev, pos, buf, size)`|从设备读取数据|返回实际读取的字节数|
|`rt_device_write(dev, pos, buf, size)`|向设备写入数据|返回实际写入的字节数|
|`rt_device_set_rx_indicate(dev, cb)`|注册接收回调|中断/DMA 模式下，数据到达时触发|
|`rt_device_close(dev)`|关闭设备|释放设备资源|
# 5.  例程测试
### 完整代码
本例程实现了一个 UART 串口回显（echo）功能，并且通过宏 `USE_MODE` 可以在**轮询、中断、DMA** 三种模式之间切换：

```c
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#define UART_NAME    "uart1"
#define BUF_SIZE     256

//切换模式: 0=轮询 1=中断 2=DMA
#define USE_MODE     0

static rt_device_t serial = RT_NULL;
static rt_sem_t    rx_sem  = RT_NULL;
static char        rx_buf[BUF_SIZE];

static rt_err_t rx_ind(rt_device_t dev, rt_size_t size)
{
    rt_kprintf(">> irq, size=%d\n", size);
    rt_sem_release(rx_sem);
    return RT_EOK;
}

static void uart_thread(void *p)
{
    serial = rt_device_find(UART_NAME);
    if (!serial) return;

    rx_sem = rt_sem_create("rxs", 0, RT_IPC_FLAG_FIFO);

    rt_uint16_t flag = RT_DEVICE_FLAG_RDWR;
    if (USE_MODE == 0 || USE_MODE == 1)
        flag |= RT_DEVICE_FLAG_INT_RX;
    if (USE_MODE == 2)
        flag |= RT_DEVICE_FLAG_DMA_RX;

    rt_device_open(serial, flag);
    rt_err_t ret = rt_device_open(serial, flag);
    rt_kprintf("uart open ret = %d , flag=0x%04X\r\n", ret, flag);

    if (USE_MODE == 1 || USE_MODE == 2)
        rt_device_set_rx_indicate(serial, rx_ind);

    rt_device_write(serial, 0, "UART ready\r\n", 12);

    while (1)
    {
        if (USE_MODE == 0)
        {
            char ch;
            if (rt_device_read(serial, 0, &ch, 1) == 1)
                rt_device_write(serial, 0, &ch, 1);
            else
                rt_thread_mdelay(10);
        }
        else
        {
            rt_sem_take(rx_sem, RT_TICK_PER_SECOND / 20);
            rt_size_t len = rt_device_read(serial, 0, rx_buf, BUF_SIZE - 1);
            if (len > 0)
                rt_device_write(serial, 0, rx_buf, len);
        }
    }
}

static int uart_init(void)
{
    rt_thread_t t = rt_thread_create("uart", uart_thread, RT_NULL, 1024, 25, 10);
    if (t) rt_thread_startup(t);
    return RT_EOK;
}
INIT_APP_EXPORT(uart_init);
```

### 代码结构分析
整个例程由五个部分组成：

| 模块                     | 内容说明                                                                            |
| ---------------------- | ------------------------------------------------------------------------------- |
| ① 头文件与宏定义              | `#include <rtthread.h>` / `<rtdevice.h>`<br><br>UART_NAME / BUF_SIZE / USE_MODE |
| ② 全局变量                 | serial (设备句柄) /rx_sem (信号量) /rx_buf                                             |
| ③ rx_ind () 接收回调       | 中断 / DMA 模式下，数据到达时被硬件 ISR 调用                                                    |
| ④ uart_thread () 主逻辑线程 | 打开设备 → 设置回调 → 循环读写                                                              |
| ⑤ uart_init () 自动初始化入口 | INIT_APP_EXPORT 宏，自动创建线程                                                        |
### 关键代码
#### ① 头文件与宏定义

```c
#include <rtthread.h>   // RT-Thread 内核头文件：线程、信号量、rt_kprintf 等
#include <rtdevice.h>   // RT-Thread 设备框架头文件：rt_device_* 系列 API
#include <string.h>     // 标准 C 库字符串操作

#define UART_NAME    "uart1"    // 设备名称，对应 rtconfig.h 中 BSP_USING_UART1
#define BUF_SIZE     256        // 接收缓冲区大小

#define USE_MODE     0          // 0=轮询  1=中断  2=DMA
```

#### ② 全局变量

```c
static rt_device_t serial = RT_NULL;   // 设备句柄，后续指向 "uart1" 设备
static rt_sem_t    rx_sem  = RT_NULL;   // 信号量，用于中断/DMA 模式下的线程同步
static char        rx_buf[BUF_SIZE];    // 数据接收缓冲区
```

#### ③ 接收回调函数 `rx_ind()`

```c
static rt_err_t rx_ind(rt_device_t dev, rt_size_t size)
{
    rt_kprintf(">> irq, size=%d\n", size);   // 打印提示信息，显示本次触发接收到的数据量
    rt_sem_release(rx_sem);                    // 释放信号量，唤醒等待线程
    return RT_EOK;
}
```

这是**中断模式和 DMA 模式的核心**。当硬件接收到数据时，底层驱动会调用这个回调函数，通知上层"有数据来了"。回调中释放信号量，主线程被唤醒后执行 `rt_device_read` 读取数据。

> 回调函数运行在**中断上下文**中，不能做耗时操作（如 `rt_thread_mdelay`），也不能调用可能阻塞的 API。
> 这里只做两件事：打印日志 + 释放信号量，都是安全的。`rt_kprintf` 在中断中使用需谨慎，生产环境建议去掉或替换为更轻量的日志机制。

#### ④ 主逻辑线程 `uart_thread()`

线程函数分为三个阶段：**初始化 → 模式配置 → 循环读写**。

**阶段一：初始化**

```c
serial = rt_device_find(UART_NAME);       // 查找名为 "uart1" 的设备
if (!serial) return;                       // 找不到则退出

rx_sem = rt_sem_create("rxs", 0, RT_IPC_FLAG_FIFO);  // 创建信号量，初始值 0
```

**阶段二：模式配置**

```c
rt_uint16_t flag = RT_DEVICE_FLAG_RDWR;   // 基本标志：可读可写

if (USE_MODE == 0 || USE_MODE == 1)       // 轮询和中断模式都添加 INT_RX 标志
    flag |= RT_DEVICE_FLAG_INT_RX;
if (USE_MODE == 2)                        // DMA 模式添加 DMA_RX 标志
    flag |= RT_DEVICE_FLAG_DMA_RX;

rt_device_open(serial, flag);              // 第一次打开设备
rt_err_t ret = rt_device_open(serial, flag);  // 第二次打开（注意：设备已打开，此处会返回 -RT_EBUSY）
rt_kprintf("uart open ret = %d , flag=0x%04X\r\n", ret, flag);

if (USE_MODE == 1 || USE_MODE == 2)
    rt_device_set_rx_indicate(serial, rx_ind);  // 注册接收回调（中断/DMA 模式需要）
```

> 1. 代码中 `rt_device_open` 被连续调用了两次。第一次打开成功后，第二次调用会返回 `-RT_EBUSY`（设备已忙）。实际开发中应只调用一次，并将返回值用于判断打开是否成功。
> 2. 当 `USE_MODE == 0`（轮询模式）时，代码仍然设置了 `RT_DEVICE_FLAG_INT_RX` 标志。严格来说，纯轮询模式不需要此标志，但加上也不会影响基本功能——底层驱动会根据实际使用情况处理。

![[贡献周/第三章/figures/Pasted image 20260828235517.png]]

- `0x0103 = 0x003 | 0x100` → **INT_RX 中断接收模式**
- `0x0203 = 0x003 | 0x200` → **DMA_RX 接收模式**


| USE_MODE | flag   | rx_ind 回调            | 特点                                 |
| -------- | ------ | -------------------- | ---------------------------------- |
| 0        | 0x0103 | ❌ 不注册，无 `>>irq`      | 底层中断 ringbuffer，上层线程轮询 read，回显正常   |
| 1        | 0x0103 | ✅ 注册，多次触发 `>>irq`    | 每来若干字节触发一次回调，分片处理                  |
| 2        | 0x0203 | ✅ 注册，一帧只触发一次 `>>irq` | DMA + IDLE 空闲中断，整帧结束才通知，size 为整帧长度 |

**阶段三：循环读写**

```c
while (1)
{
    if (USE_MODE == 0)
    {
        // === 轮询模式 ===
        char ch;
        if (rt_device_read(serial, 0, &ch, 1) == 1)   // 尝试读 1 字节
            rt_device_write(serial, 0, &ch, 1);         // 读到了就回显
        else
            rt_thread_mdelay(10);                        // 没读到就休眠 10ms
    }
    else
    {
        // === 中断/DMA 模式 ===
        rt_sem_take(rx_sem, RT_TICK_PER_SECOND / 20);    // 等待信号量，超时 50ms
        rt_size_t len = rt_device_read(serial, 0, rx_buf, BUF_SIZE - 1);
        if (len > 0)
            rt_device_write(serial, 0, rx_buf, len);     // 回显收到的数据
    }
}
```

**两种模式的核心区别：**

| 对比维度       | 轮询模式                                      | 中断/DMA 模式                             |
| ---------- | ----------------------------------------- | ------------------------------------- |
| **等待方式**   | 不断轮询 `rt_device_read`，无数据时主动 `mdelay(10)` | 信号量阻塞 `rt_sem_take`，超时 50ms 后自动唤醒检查   |
| **CPU 占用** | 高（即使没数据也在循环）                              | 低（无数据时线程挂起，CPU 可执行其他任务）               |
| **读取粒度**   | 逐字节读取和回显                                  | 批量读取（最多 `BUF_SIZE-1` 字节）后回显           |
| **实时性**    | 取决于轮询间隔（10ms）                             | 取决于中断响应速度（微秒级）                        |
| **信号量超时**  | 不适用                                       | 50ms（`RT_TICK_PER_SECOND / 20`），非永久阻塞 |
#### ⑤ 自动初始化入口

```c
static int uart_init(void)
{
    rt_thread_t t = rt_thread_create("uart", uart_thread, RT_NULL, 1024, 25, 10);
    if (t) rt_thread_startup(t);
    return RT_EOK;
}
INIT_APP_EXPORT(uart_init);   // 在系统启动的"应用初始化"阶段自动调用
```

`INIT_APP_EXPORT` 是 RT-Thread 的自动初始化宏，它在系统启动流程中自动调用 `uart_init()`，无需在 `main()` 中手动调用。
### 编译与下载
1. 修改 `USE_MODE` 宏选择测试模式（0，1，2）
2. 编译工程：点击 RT-Thread Studio 工具栏的 **构建** 按钮（锤子图标）
3. 下载到开发板：点击 **下载** 按钮
### 预期输出
下载后打开串口终端（波特率 **115200**），复位开发板，可以看到：

```text
UART ready
```

#### 轮询模式测试（USE_MODE = 0）
在串口终端输入任意字符，开发板会逐字节回显：

```
UART ready
hello world
```

![[贡献周/第三章/figures/Pasted image 20260828235737.png]]
> 轮询模式下，每次读取 1 字节并立即回显，无数据时线程休眠 10ms。
#### 中断模式测试（USE_MODE = 1）

修改 `USE_MODE` 为 1，重新编译下载。在串口终端输入数据：
![[贡献周/第三章/figures/Pasted image 20260828235802.png]]

> 中断模式下，每次硬件中断触发时打印 `>> irq` 提示，然后批量读取并回显数据。
#### DMA 模式测试（USE_MODE = 2）

修改 `USE_MODE` 为 2，重新编译下载。在串口终端输入数据：
![[贡献周/第三章/figures/Pasted image 20260828235826.png]]

> DMA 模式下，DMA 控制器自动将数据搬运到内存，完成后触发中断，回调函数打印 `>> irq` 并释放信号量。

### 三种模式切换对比

| 测试项        | 轮询模式             | 中断模式                      | DMA 模式                    |
| ---------- | ---------------- | ------------------------- | ------------------------- |
| `USE_MODE` | 0                | 1                         | 2                         |
| 打开标志       | `RDWR \| INT_RX` | `RDWR \| INT_RX`          | `RDWR \| DMA_RX`          |
| 是否注册回调     | ❌                | ✅                         | ✅                         |
| 等待方式       | 主动轮询 + `mdelay`  | `rt_sem_take` 阻塞（50ms 超时） | `rt_sem_take` 阻塞（50ms 超时） |
| 读取粒度       | 1 字节             | 批量                        | 批量                        |
| CPU 占用     | 高                | 低                         | 最低                        |
| 适用场景       | 简单调试             | 常规通信                      | 高速大数据                     |
# 6.  应用
### 场景描述
在实际项目中，UART 常用于接收上位机指令来控制外设。下面演示如何扩展本章例程，实现**通过串口发送指令控制 LED**。
### 扩展代码

在 `uart_thread` 的循环中，对接收到的数据进行解析：

```c
#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#include <board.h>

#define UART_NAME    "uart1"
#define BUF_SIZE     256

#define LED_PIN  GET_PIN(F,11)


//切换模式: 0=轮询 1=中断 2=DMA
#define USE_MODE     0

static rt_device_t serial = RT_NULL;
static rt_sem_t    rx_sem  = RT_NULL;
static char        rx_buf[BUF_SIZE];

static rt_err_t rx_ind(rt_device_t dev, rt_size_t size)
{
    rt_kprintf(">> irq, size=%d\n", size);
    rt_sem_release(rx_sem);
    return RT_EOK;
}

static void uart_thread(void *p)
{
    serial = rt_device_find(UART_NAME);
    if (!serial) return -RT_ERROR;

    rx_sem = rt_sem_create("rxs", 0, RT_IPC_FLAG_FIFO);

    rt_uint16_t flag = RT_DEVICE_FLAG_RDWR;
    if (USE_MODE == 0 ||USE_MODE == 1)
        flag |= RT_DEVICE_FLAG_INT_RX;
    if (USE_MODE == 2)
        flag |= RT_DEVICE_FLAG_DMA_RX;

    rt_device_open(serial, flag);
    rt_err_t ret = rt_device_open(serial, flag);
    rt_kprintf("uart open ret = %d , flag=0x%04X\r\n", ret, flag);

    if (USE_MODE == 1 || USE_MODE == 2)
        rt_device_set_rx_indicate(serial, rx_ind);

    rt_device_write(serial, 0, "UART ready\r\n", 12);



    while (1)
    {
        if (USE_MODE == 0)
        {
            char ch;
            if (rt_device_read(serial, 0, &ch, 1) == 1)
                rt_device_write(serial, 0, &ch, 1);
            else
                rt_thread_mdelay(10);
        }
        else
        {
            rt_sem_take(rx_sem, RT_TICK_PER_SECOND / 20);
            rt_size_t len = rt_device_read(serial, 0, rx_buf, BUF_SIZE - 1);
            if (len > 0)
                rt_device_write(serial, 0, rx_buf, len);
        }

        /* 解析指令 */

        rt_sem_take(rx_sem, RT_TICK_PER_SECOND / 20);
        rt_size_t len = rt_device_read(serial, 0, rx_buf, BUF_SIZE - 1);
        if (len > 0)
        {
            if (rt_strstr(rx_buf, "led on"))
                    {
                        rt_pin_write(LED_PIN, PIN_LOW);   // 点亮 LED（假设低电平点亮）
                        rt_device_write(serial, 0, "LED ON\r\n", 8);
                    }
                    else if (rt_strstr(rx_buf, "led off"))
                    {
                        rt_pin_write(LED_PIN, PIN_HIGH);  // 熄灭 LED
                        rt_device_write(serial, 0, "LED OFF\r\n", 9);
                    }
                    else if (rt_strstr(rx_buf, "led toggle"))
                    {
                        rt_pin_write(LED_PIN, !rt_pin_read(LED_PIN));
                        rt_device_write(serial, 0, "LED TOGGLE\r\n", 12);
                    }
                    else
                    {
                        rt_device_write(serial, 0, "Unknown command\r\n", 17);
                    }
                }
        }

}

static int uart_init(void)
{
    rt_thread_t t = rt_thread_create("uart", uart_thread, RT_NULL, 1024, 25, 10);
    if (t) rt_thread_startup(t);
    return RT_EOK;
}
INIT_APP_EXPORT(uart_init);

```

### 测试结果
![[贡献周/第三章/figures/Pasted image 20260830135550.png]]
# 7.  结论
本章通过一个统一的 UART 串口例程，展示了 RT-Thread 下三种数据接收模式——**轮询、中断、DMA**——的实现方法。

| 要点         | 说明                                                  |
| ---------- | --------------------------------------------------- |
| **轮询模式**   | 代码最简单，但 CPU 占用高，适合调试或极低数据量场景                        |
| **中断模式**   | 通过 `rx_indicate` 回调 + 信号量实现事件驱动，平衡了复杂度和效率           |
| **DMA 模式** | CPU 零参与接收过程，适合高波特率、大数据量传输                           |
| **统一框架**   | 三种模式共用 `rt_device_*` API，只需修改 `open` 的 flag 和是否注册回调 |
| **自动初始化**  | `INIT_APP_EXPORT` 让驱动代码自包含，无需改动 `main()`            |
在实际项目选型时，建议遵循以下原则：
- **波特率 ≤ 115200，数据量小** → 中断模式即可满足需求
- **波特率 ≥ 921600，或频繁突发大数据** → 优先考虑 DMA 模式
- **快速验证、临时调试** → 轮询模式最省事




