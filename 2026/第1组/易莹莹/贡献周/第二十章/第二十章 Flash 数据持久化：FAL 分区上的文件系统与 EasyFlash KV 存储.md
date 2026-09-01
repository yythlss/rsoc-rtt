# 1.  新建工程
### 基于芯片创建工程
本例程需要使用 **RT-Thread Studio** 基于芯片创建工程，具体步骤如下：
**Step 1：打开 RT-Thread Studio，选择"基于芯片创建工程"**
点击菜单栏 `文件 → 新建 → RT-Thread 项目`，在弹出的对话框中选择 **"基于芯片"** 选项卡。
**Step 2：配置芯片参数**
![[贡献周/第二十章/figures/Pasted image 20260827164410.png]]
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
### 开启 MSH（FinSH）命令行外壳组件

![[贡献周/第十九章/figures/Pasted image 20260827125945.png]]
### 使能 FAL 及其相关驱动

![[贡献周/第十九章/figures/Pasted image 20260827090733.png]]

![[贡献周/第十九章/figures/Pasted image 20260827090932.png]]

![[贡献周/第十九章/figures/Pasted image 20260827091252.png]]
### 添加EasyFlash 软件包

![[贡献周/第二十章/figures/Pasted image 20260828112637.png]]
### 保存配置

点击保存按钮，RT-Thread Studio 会自动更新 `rtconfig.h` 和 `.config` 文件。

- 配置完成后，`rtconfig.h` 中应包含以下关键宏定义：
```c
/* FAL */
#define RT_USING_FAL
#define FAL_DEBUG 0
#define FAL_PART_HAS_TABLE_CFG
#define FAL_USING_SFUD_PORT
#define FAL_USING_NOR_FLASH_DEV_NAME "norflash0"

/* SPI & SFUD */
#define RT_USING_SPI
#define RT_USING_SFUD
#define RT_SFUD_USING_SFDP
#define RT_SFUD_USING_FLASH_INFO_TABLE
#define RT_SFUD_SPI_MAX_HZ 50000000

/* EasyFlash */
#define PKG_USING_EASYFLASH
#define PKG_EASYFLASH_ENV
#define PKG_EASYFLASH_ERASE_GRAN 4096
#define PKG_EASYFLASH_WRITE_GRAN_1BIT
#define PKG_EASYFLASH_WRITE_GRAN 1
#define PKG_EASYFLASH_START_ADDR 0
#define PKG_EASYFLASH_DEBUG
#define PKG_USING_EASYFLASH_V410
#define PKG_EASYFLASH_VER_NUM 0x40100

/* 板级配置 */
#define BSP_USING_SPI_FLASH
#define BSP_USING_FAL
#define BSP_USING_EASYFLASH
#define BSP_USING_ON_CHIP_FLASH
#define BSP_USING_SPI2

```

# 3.  board.c/h 的配置（包括引脚的初始化，配合cubemx）
## 3.1 board.h 配置
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
## 3.2 board.c 配置（系统时钟初始化）

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
## CubeMX 引脚配置
SPI2 用于连接片外 W25Q64 Nor Flash，引脚配置如下（通过 CubeMX 设置）：

|引脚|功能|说明|
|---|---|---|
|PB12|SPI2_NSS|SPI2 片选信号|
|PB13|SPI2_SCK|SPI2 时钟信号|
|PB14|SPI2_MISO|SPI2 主入从出|
|PB15|SPI2_MOSI|SPI2 主出从入|

![[贡献周/第十九章/figures/Pasted image 20260827092431.png]]

>在 RT-Thread 工程中用 CubeMX 改完时钟/引脚后，**引脚与外设的初始化代码（MSP 回调、`HAL_MspInit`、各外设 MSP 函数）放在 CubeMX 生成目录下的 `Src/stm32f4xx_msp.c`文件里**；而**系统时钟配置函数（如 `SystemClock_Config`）应从 CubeMX 生成的 `Src/main.c`中拷出，放到 RT-Thread 工程的 `drivers/board.c`中**。

具体分工
- `qmax/Src/`里只保留两个文件：
    - `stm32f4xx_msp.c`—— 引脚复用、时钟使能等 MSP 初始化
    - `stm32f4xx_it.c`—— 中断向量
    - 其他（含 `main.c`）都可删，RT 工程不用 CubeMX 的 main
- 时钟初始化：从 CubeMX 生成的 `main.c`里把 `SystemClock_Config`等拷到 `drivers/board.c` 对应位置，外设时钟使能也归到这里
- CubeMX 生成目录里 `Drivers/`、`EWARM/`、`*.frc`、`*.nc`等冗余文件全部删掉，避免和 RT 工程自带的 HAL 库冲突
**引脚/MSP → `stm32f4xx_msp.c`；
系统时钟 → `drivers/board.c`**。
![[贡献周/第十九章/figures/Pasted image 20260827093200.png]]

# 4.  EasyFlash 框架说明
## EasyFlash 概述

**EasyFlash** 是一款开源的轻量级嵌入式 Flash 存储器库，主要为 MCU 提供便捷、通用的上层应用接口。该库目前提供三大实用功能：

|功能|说明|
|---|---|
|**Env（环境变量）**|快速保存产品参数，支持写平衡（磨损平衡）及掉电保护模式|
|**IAP（在线升级）**|封装 IAP 常用接口，支持 CRC32 校验，支持 Bootloader 及 Application 升级|
|**Log（日志存储）**|无需文件系统，日志可直接存储在 Flash 上|
### 架构图


```mermaid
graph TD
    A["应用层<br/>ef_get_env / ef_set_env / ..."] --> B["EasyFlash 核心"]
    subgraph EasyFlash核心模块
        B1["ENV环境变量"]
        B2["IAP升级"]
        B3["Log日志"]
    end
    B --> B1 & B2 & B3

    B1 & B2 & B3 --> C["移植层 ef_port.c"]
    C --> D["FAL Flash抽象层"]
    D --> E["片内Flash"]
    D --> F["片外W25Q64"]
````


## 4.2 EasyFlash 核心 API

### 环境变量操作

| API 函数                   | 功能说明            |
| ------------------------ | --------------- |
| `easyflash_init()`       | 初始化 EasyFlash   |
| `ef_get_env(key)`        | 读取环境变量（返回字符串指针） |
| `ef_set_env(key, value)` | 设置环境变量（内存操作）    |
| `ef_save_env()`          | 保存环境变量到 Flash   |
| `ef_del_env(key)`        | 删除环境变量          |
| `ef_reset_env()`         | 复位环境变量为默认值      |
| `ef_print_env()`         | 打印所有环境变量        |
### ENV 工作原理

```
setenv → 写入 RAM 缓存（不写 Flash，掉电丢失）
saveenv → 将 RAM 缓存写入 Flash 分区（掉电保护）
上电初始化 → 从 Flash 分区读取 ENV 到 RAM 缓存
```

## EasyFlash 移植说明
EasyFlash 基于 FAL 的移植主要分为 3 步：
### Step 1：复制移植文件
从 `packages/EasyFlash-v4.1.0/ports/ef_fal_port.c` 复制到 `libraries/Board_Drivers/ef_fal_port.c`。
### Step 2：修改分区名
在 `ef_fal_port.c` 中，将 `FAL_EF_PART_NAME` 宏定义为存储环境变量的分区名：

```c
/* EasyFlash partition name on FAL partition table */
#define FAL_EF_PART_NAME               "easyflash"
```
### Step 3：修改默认环境变量
在 `ef_fal_port.c` 中，修改 `default_env_set` 数组，定义默认环境变量：
```c
static const ef_env default_env_set[] = {
        {"iap_need_copy_app", "0"},
        {"iap_need_crc32_check", "0"},
        {"iap_copy_app_size", "0"},
        {"stop_in_bootloader", "0"},
};
```

> 本例程只记录开机次数（`boot_times`），实际在 `main.c` 中通过 `ef_set_env` 动态添加，默认数组保留 IAP 相关变量。

## FAL 分区配置
分区表定义在 `libraries/Board_Drivers/fal/fal_cfg.h` 中：
### Flash 设备列表

```c
#define FAL_FLASH_DEV_TABLE                                          
{                                                                    
    &stm32_onchip_flash_128k,                                        
    &w25q64,                                                        
}
```

两个 Flash 设备：
- **stm32_onchip_flash_128k**：STM32F4 片内 Flash，擦除块 128KB
- **w25q64**：片外 SPI Nor Flash，容量 8MB，擦除块 4KB

### 分区表（无 Bootloader）

```c
#define FAL_PART_TABLE                                                                                              
{                                                                                                                   
    {FAL_PART_MAGIC_WROD,        "app", "onchip_flash_128k",                            0,       384 * 1024, 0}, 
    {FAL_PART_MAGIC_WROD,      "param", "onchip_flash_128k",                   384 * 1024,       640 * 1024, 0}, 
    {FAL_PART_MAGIC_WROD,  "easyflash",    "W25Q64",                                    0,       512 * 1024, 0}, 
    {FAL_PART_MAGIC_WROD,   "download",    "W25Q64",                           512 * 1024,      1024 * 1024, 0}, 
    {FAL_PART_MAGIC_WROD, "wifi_image",    "W25Q64",                  (512 + 1024) * 1024,       512 * 1024, 0}, 
    {FAL_PART_MAGIC_WROD,       "font",    "W25Q64",            (512 + 1024 + 512) * 1024,  3 * 1024 * 1024, 0}, 
    {FAL_PART_MAGIC_WROD, "filesystem",    "W25Q64", (512 + 1024 + 512 + 3 * 1024) * 1024,  3 * 1024 * 1024, 0}, 
}
```

> `easyflash` 分区分配在片外 W25Q64 上（512KB），这是因为 W25Q64 的擦除粒度（4KB）比片内 Flash（128KB）更小，更适合 KV 存储的频繁写入。同时也能避免频繁擦除片内 Flash 影响固件寿命。

## EasyFlash 配置参数（ef_cfg.h）

```c
/* 环境变量配置 */
#define EF_USING_ENV                    // 启用 ENV 功能
#define EF_ENV_AUTO_UPDATE              // 版本号变化时自动追加新环境变量

/* Flash 物理参数 */
#define EF_ERASE_MIN_SIZE   4096        // 最小擦除单元 4KB（对齐 W25Q64 扇区）
#define EF_WRITE_GRAN       1           // 写入粒度 1 bit（Nor Flash 特性）

/* 存储区域 */
#define EF_START_ADDR       0           // 分区内起始地址
#define ENV_AREA_SIZE       (EF_ERASE_MIN_SIZE * 2)  // ENV 区域 8KB（至少 2 个扇区供 GC）
```

# 5.  例程测试
## 例程代码（main.c）

本例程的核心功能是通过 EasyFlash 记录开机次数，每次上电自动 +1 并保存到 Flash：

```c
#include <rtthread.h>
#include <easyflash.h>
#include <fal.h>
#include <stdlib.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

static void test_env(void);

int main(void)
{
    fal_init();                           // ① 初始化 FAL

    if (easyflash_init() == EF_NO_ERR)    // ② 初始化 EasyFlash
    {
        test_env();                       // ③ 演示环境变量功能
    }

    return 0;
}

static void test_env(void)
{
    uint32_t i_boot_times = 0;
    char *c_old_boot_times, c_new_boot_times[11] = {0};

    /* 从环境变量中获取启动次数 */
    c_old_boot_times = ef_get_env("boot_times");
    if (c_old_boot_times == RT_NULL)
        c_old_boot_times[0] = '0';

    i_boot_times = atol(c_old_boot_times);
    i_boot_times++;                      // ④ 启动次数 +1

    LOG_D("===============================================");
    LOG_D("The system now boot %ld times", i_boot_times);
    LOG_D("===============================================");

    sprintf(c_new_boot_times, "%ld", i_boot_times);
    ef_set_env("boot_times", c_new_boot_times);  // ⑤ 写入内存
    ef_save_env();                               // ⑥ 保存到 Flash（掉电保护）
}
```

### 执行流程详解

|步骤|函数|说明|
|---|---|---|
|①|`fal_init()`|初始化 FAL，加载分区表|
|②|`easyflash_init()`|初始化 EasyFlash，从 Flash 读取已有 ENV 到 RAM|
|③|`test_env()`|演示 KV 读写|
|④|`ef_get_env("boot_times")`|从 RAM 缓存读取 boot_times|
|⑤|`ef_set_env(...)`|更新 RAM 缓存中的值|
|⑥|`ef_save_env()`|将 RAM 缓存持久化到 Flash|
## 5.2 预期输出

编译下载后，通过串口终端（115200 波特率）可以看到如下输出：

![[贡献周/第二十章/figures/Pasted image 20260828121853.png]]

按下复位按键，可以看到开机次数加一，**掉电后重新上电，次数不会丢失**：

![[贡献周/第二十章/figures/Pasted image 20260828121924.png]]
## FinSH/MSH 命令行测试

EasyFlash 自带了 5 个 FinSH 测试命令：

| 命令                     | 功能            | 示例                    |
| ---------------------- | ------------- | --------------------- |
| `printenv`             | 打印所有环境变量      | `printenv`            |
| `getvalue <key>`       | 获取某个 key 的值   | `getvalue boot_times` |
| `setenv <key> <value>` | 设置环境变量（内存）    | `setenv boot_times 5` |
| `saveenv`              | 保存环境变量到 Flash | `saveenv`             |
| `resetenv`             | 复位环境变量为默认值    | `resetenv`            |
### MSH 命令演示x

![[贡献周/第二十章/figures/Pasted image 20260828122726.png]]

|操作|行为|
|---|---|
|`setenv` 后不 `saveenv`|只修改内存，掉电丢失|
|`setenv` 后 `saveenv`|写入 Flash，掉电不丢失|
|`resetenv`|恢复默认值并自动保存|
|`setenv <key>` 不带 value|删除该环境变量|
# 6.  应用——配合传感器软件包：存储校准参数

下面以 **aht10 温湿度传感器软件包** 为例，演示如何将 EasyFlash 和第三方软件包配合使用，实现传感器校准参数的持久化存储。
## 场景描述
aht10 传感器出厂时是校准好的，但实际使用中可能因为安装位置、供电电压差异等原因需要微调。我们希望把校准偏移量存到 Flash 里，掉电后还能保留，下次开机自动加载。
## 操作步骤

### 第一步：添加 aht10 软件包

在 RT-Thread Studio 中打开 **RT-Thread Settings**，点击「添加软件包」，搜索 **AHT21**，选择 **Enable AHT21(i2c3)** 并添加。

![[贡献周/第二十章/figures/Pasted image 20260828135224.png]]

> 添加后 RT-Thread Studio 会自动下载软件包到 `packages/` 目录并更新 `rtconfig.h`。

保存后，`rtconfig.h` 中会新增：

```c
#define PKG_USING_AHT10
```
### 第二步：修改 `ef_fal_port.c`，添加默认环境变量

打开 `libraries/Board_Drivers/ef_fal_port.c`，找到 `default_env_set` 数组，在末尾的 `{NULL, NULL}` 之前**追加两行**校准参数的默认值：

```c
static const ef_env default_env_set[] = {
    // ... 原有的 iap_need_copy_app 等 ...

    /* 新增：传感器校准偏移量 */
    {"temp_offset",  "0.0"},    // 温度偏移，默认 0
    {"humi_offset",  "0.0"},    // 湿度偏移，默认 0

    // ... 其余保持不变 ...
};
```

> 新增环境变量后，必须把 `ef_cfg.h` 中的 `PKG_EASYFLASH_ENV_VER_NUM` 版本号 **+1**，这样 EasyFlash 的自动更新机制才会把新变量写进去。
### 第三步：修改 `main.c`，编写传感器 + 校准逻辑

在 `main.c` 中，需要做三件事：

**① 添加头文件：**

在文件顶部的 `#include` 区域，新增：

```c
#include <easyflash.h>
#include "aht10.h"    // 传感器驱动头文件
```

**② 在 `main()` 中初始化设备并加载校准值：**

在 `easyflash_init()` 成功之后，添加：

```c
/* 初始化 aht10 传感器（I2C1 总线） */
aht10_device_t aht10_dev = aht10_init("i2c1");

/* 从 EasyFlash 环境变量加载校准偏移量 */
float temp_offset = 0.0f, humi_offset = 0.0f;
char *str = ef_get_env("temp_offset");
if (str != RT_NULL) {
    temp_offset = atof(str);   // 字符串 → 浮点数
}
str = ef_get_env("humi_offset");
if (str != RT_NULL) {
    humi_offset = atof(str);
}
```

**③ 读取传感器数据时应用校准：**

```c
/* 读取原始数据 */
float temperature, humidity;
aht10_read_temperature(aht10_dev, &temperature);
aht10_read_humidity(aht10_dev, &humidity);

/* 应用校准偏移 */
temperature += temp_offset;
humidity   += humi_offset;

LOG_D("Temp: %.1f°C, Humi: %.1f%%", temperature, humidity);
```

### 第六步：通过 MSH 命令校准

添加一个自定义 MSH 命令，方便你在运行时调整校准值。在 `main.c` 中添加：

```c
#include <stdlib.h>  // for atof

static int cmd_calib(int argc, char **argv)
{
    if (argc != 3) {
        rt_kprintf("Usage: calib <temp_offset> <humi_offset>\n");
        rt_kprintf("  e.g.  calib -0.5 2.0\n");
        return -1;
    }

    ef_set_env("temp_offset", argv[1]);
    ef_set_env("humi_offset", argv[2]);
    ef_save_env();

    rt_kprintf("Calib saved: temp_offset=%s, humi_offset=%s\n",
               argv[1], argv[2]);
    return 0;
}
MSH_CMD_EXPORT(calib, set sensor calibration offsets);
```

**编译 → 下载 → 串口测试：**

```
msh >calib -0.5 2.0
Calib saved: temp_offset=-0.5, humi_offset=2.0
msh >printenv
===== Env on easyflash =====
boot_times=12
temp_offset=-0.5
humi_offset=2.0
msh >

/* 断电重启后，校准值仍然有效 */
```

## 关键要点

| 要点 | 说明 |
|------|------|
| **软件包配合** | EasyFlash 负责存储，aht10 负责传感，各司其职 |
| **新增环境变量** | 在 `ef_fal_port.c` 的 `default_env_set` 数组里添加，版本号 +1 |
| **数据类型** | EasyFlash 只存字符串，`atof()` 把字符串转成浮点数用 |
| **MSH 命令** | 用 `MSH_CMD_EXPORT` 注册自定义命令，运行时调校准 |
| **掉电保护** | `ef_save_env()` 写入 Flash，校准值断电不丢 |

# 7.  结论

本章通过 EasyFlash 软件包，基于 FAL（Flash Abstraction Layer）实现了 **环境变量（KV）读写与掉电保护** 功能。

|要点|说明|
|---|---|
|**EasyFlash 功能**|环境变量（ENV）、IAP 升级、日志存储三大功能|
|**ENV 存储机制**|内存操作 + Flash 持久化两步走，`setenv` 只改内存，`saveenv` 才写 Flash|
|**掉电保护**|写入时采用"先写新扇区，再擦旧扇区"策略，断电也能恢复旧数据|
|**磨损平衡**|不在固定位置反复擦写，通过 GC 机制在多个扇区间轮转|
|**底层依赖**|FAL 提供统一 Flash 分区接口，EasyFlash 不关心底层是片内还是片外 Flash|
|**Shell 调试**|自带 5 个 MSH 命令，支持运行时查看/修改/保存/复位环境变量|
|**存储位置**|`easyflash` 分区（W25Q64 上 512KB），擦除粒度 4KB|

通过本章的学习，开发者可以：
1. **掌握 EasyFlash 的移植方法**：复制 `ef_fal_port.c` → 改分区名 → 改默认环境变量
2. **理解 `setenv`/`saveenv` 的两步机制**，避免只 `setenv` 不 `saveenv` 导致掉电丢失
3. **使用 MSH 命令** 在运行时调试环境变量
4. **将 EasyFlash 应用到实际项目**：存储设备参数、记录运行日志、配合 IAP 升级