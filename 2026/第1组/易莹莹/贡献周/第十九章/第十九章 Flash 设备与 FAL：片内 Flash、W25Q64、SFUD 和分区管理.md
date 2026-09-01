# 1. 创建工程
### 基于芯片创建工程

本例程需要使用 **RT-Thread Studio** 基于芯片创建工程，具体步骤如下：
**Step 1：打开 RT-Thread Studio，选择"基于芯片创建工程"**
点击菜单栏 `文件 → 新建 → RT-Thread 项目`，在弹出的对话框中选择 **"基于芯片"** 选项卡。
**Step 2：配置芯片参数**
![[贡献周/第十九章/figures/Pasted image 20260827134451.png]]
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
# 2. RT-Thread Setting 的配置
### 开启 MSH（FinSH）命令行外壳组件

![[贡献周/第十九章/figures/Pasted image 20260827125945.png]]
### 使能 FAL 及其相关驱动

![[贡献周/第十九章/figures/Pasted image 20260827090733.png]]

![[贡献周/第十九章/figures/Pasted image 20260827090932.png]]

![[贡献周/第十九章/figures/Pasted image 20260827091252.png]]
### 保存配置

点击保存按钮，RT-Thread Studio 会自动更新 `rtconfig.h` 和 `.config` 文件。

- 配置完成后，`rtconfig.h` 中应包含以下关键宏定义：
```c
#define RT_USING_FAL
#define FAL_DEBUG_CONFIG
#define FAL_DEBUG 1
#define FAL_PART_HAS_TABLE_CFG
#define FAL_USING_SFUD_PORT
#define RT_USING_SPI
#define RT_USING_SFUD
```
# 3. board.c/h 的配置（包括引脚的初始化，配合 CubeMX）
## board.h 配置

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

|宏定义|值|说明|
|---|---|---|
|`STM32_FLASH_START_ADRESS`|`0x08000000`|片内 Flash 起始地址|
|`STM32_FLASH_SIZE`|`1024 * 1024`（1MB）|片内 Flash 总容量|
|`STM32_SRAM_SIZE`|`128`（128KB）|片内 SRAM 大小|
|`HEAP_BEGIN`|bss 段末尾|堆内存起始地址|
|`HEAP_END`|`STM32_SRAM_END`|堆内存结束地址|
## board.c 配置（系统时钟初始化）

`board.c` 中的 `SystemClock_Config()` 函数负责配置系统时钟，使用 CubeMX 生成的 HAL 库代码：

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

在 RT-Thread 工程中用 CubeMX 改完时钟/引脚后，**引脚与外设的初始化代码（MSP 回调、`HAL_MspInit`、各外设 MSP 函数）放在 CubeMX 生成目录下的 `Src/stm32f4xx_msp.c`文件里**；而**系统时钟配置函数（如 `SystemClock_Config`）应从 CubeMX 生成的 `Src/main.c`中拷出，放到 RT-Thread 工程的 `drivers/board.c`中**。

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
# 4. FAL 框架说明
### FAL 概述
**FAL (Flash Abstraction Layer)** 即 Flash 抽象层，是 RT-Thread 的一个核心组件，用于对 Flash 及基于 Flash 的分区进行统一管理和操作。FAL 向上层应用（如文件系统、OTA、KV 存储等）提供统一的 API 接口，屏蔽底层不同 Flash 设备的差异。
**FAL 架构图：**
![FAL framework](https://www.rt-thread.org/document/site/rt-thread-version/rt-thread-standard/programming-manual/fal/figures/fal_framework.png)
### FAL 核心特性
- **支持静态可配置分区表**：可关联多个 Flash 设备
- **分区表自动装载**：避免多固件项目中分区表被多次定义
- **代码精简，无 OS 依赖**：可运行于裸机平台（如 bootloader）
- **统一的操作接口**：保证文件系统、OTA、NVM 等组件的底层驱动可重用性
- **自带 Finsh/MSH 测试命令**：可通过 Shell 按字节操作 Flash，方便调试
### FAL 核心数据结构
#### Flash 设备结构体 `fal_flash_dev`

```c
struct fal_flash_dev
{
    char name[FAL_DEV_NAME_MAX];    // Flash 设备名称（最大 23 字符）
    uint32_t addr;                  // Flash 设备起始地址
    size_t len;                     // Flash 设备容量（字节）
    size_t blk_size;                // 最小擦除块大小（字节）

    struct {
        int (*init)(void);          // 初始化函数
        int (*read)(long offset, uint8_t *buf, size_t size);  // 读函数
        int (*write)(long offset, const uint8_t *buf, size_t size); // 写函数
        int (*erase)(long offset, size_t size);               // 擦除函数
    } ops;

    size_t write_gran;              // 写入最小粒度（位）
};
```

#### 分区结构体 `fal_partition`

```c
struct fal_partition
{
    uint32_t magic_word;                     // 魔法数（系统使用）
    char name[FAL_DEV_NAME_MAX];             // 分区名称
    char flash_name[FAL_DEV_NAME_MAX];       // 所属 Flash 设备名称
    long offset;                             // 分区在 Flash 中的偏移地址
    size_t len;                              // 分区大小（字节）
    uint8_t reserved;                        // 保留项
};
```

### FAL 核心 API

|API 函数|功能说明|
|---|---|
|`fal_init()`|初始化 FAL 组件|
|`fal_flash_device_find(name)`|按名称查找 Flash 设备|
|`fal_partition_find(name)`|按名称查找分区|
|`fal_partition_read(part, offset, buf, size)`|从分区读取数据|
|`fal_partition_write(part, offset, buf, size)`|向分区写入数据|
|`fal_partition_erase(part, offset, size)`|擦除分区指定区域|
|`fal_partition_erase_all(part)`|擦除整个分区|
### 分区表配置（fal_cfg.h）

分区表定义在 `libraries/Board_Drivers/fal/fal_cfg.h` 中，本例程的分区表如下：

#### Flash 设备列表

```c
#define FAL_FLASH_DEV_TABLE                        \
{                                                  \
    &stm32_onchip_flash_128k,                      \
    &w25q64,                                       \
}
```

两个 Flash 设备：
- **stm32_onchip_flash_128k**：STM32F4 片内 Flash，擦除块 128KB
- **w25q64**：片外 SPI Nor Flash，容量 8MB，擦除块 4KB
#### 分区表

```c
#define FAL_PART_TABLE \ 
{ \  
{FAL_PART_MAGIC_WORD, "app", "onchip_flash_128k", 0, 384 * 1024, 0}, \  
{FAL_PART_MAGIC_WORD, "param", "onchip_flash_128k", 384 * 1024, 640 * 1024, 0}, \  
{FAL_PART_MAGIC_WORD, "easyflash", "W25Q64", 0, 512 * 1024, 0}, \  
{FAL_PART_MAGIC_WORD, "download", "W25Q64", 512 * 1024, 1024 * 1024, 0}, \  
{FAL_PART_MAGIC_WORD, "wifi_image", "W25Q64", (512 + 1024) * 1024, 512 * 1024, 0}, \  
{FAL_PART_MAGIC_WORD, "font", "W25Q64", (512 + 1024 + 512) * 1024, 3 * 1024 * 1024, 0}, \  
{FAL_PART_MAGIC_WORD, "filesystem", "W25Q64", (512 + 1024 + 512 + 3 * 1024) * 1024, 3 * 1024 * 1024, 0}, \  
}
```

### Flash 设备对接说明

#### 片内 Flash 对接

片内 Flash 设备实例定义在 `libraries/HAL_Drivers/drv_flash/drv_flash_f4.c` 中：

```c
const struct fal_flash_dev stm32_onchip_flash_128k =
{
    "onchip_flash_128k",
    STM32_FLASH_START_ADRESS_128K,   // 起始地址 0x08000000
    FLASH_SIZE_GRANULARITY_128K,     // 容量
    (128 * 1024),                    // 擦除块大小 128KB
    {
        NULL,                        // 无需初始化
        fal_flash_read_128k,         // 读接口
        fal_flash_write_128k,        // 写接口
        fal_flash_erase_128k,        // 擦除接口
    },
    8,                               // 写入粒度 8 位
};
```
#### 片外 Nor Flash 对接（SFUD）

片外 W25Q64 通过 SFUD 框架对接，定义在 `libraries/Board_Drivers/fal/fal_spi_flash_sfud_port.c` 中：

```c
struct fal_flash_dev w25q64 =
{
    .name       = "W25Q64",
    .addr       = 0,
    .len        = 8 * 1024 * 1024,   // 8MB
    .blk_size   = 4096,              // 擦除块大小 4KB
    .ops        = {init, read, write, erase},  // 基于 SFUD 的操作接口
    .write_gran = 1                  // 写入粒度 1 位
};
```

> **SFUD** (Serial Flash Universal Driver) 是一款开源的串行 SPI Flash 通用驱动库，覆盖了市面上绝大多数串行 Flash 型号，无需手动开发驱动即可操作 Flash。

# 5. 例程测试与讲解

## 例程代码（main.c）

本例程的核心功能是通过调用 FAL 接口，对指定分区进行完整的擦除、写入、读取测试，验证 Flash 驱动和 FAL 组件的正确性。

```c
#include <rtthread.h>
#include <fal.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define BUF_SIZE 1024

static int fal_test(const char *partiton_name);

int main(void)
{
    fal_init();  // 初始化 FAL 组件

    if (fal_test("param") == 0)
        LOG_I("Fal partition (%s) test success!", "param");
    else
        LOG_E("Fal partition (%s) test failed!", "param");

    if (fal_test("download") == 0)
        LOG_I("Fal partition (%s) test success!", "download");
    else
        LOG_E("Fal partition (%s) test failed!", "download");

    return 0;
}
```

### 分区测试函数详解

`fal_test()` 函数对指定分区执行完整的测试流程：
#### Step 1：查找分区和 Flash 设备

```c
partition = fal_partition_find(partiton_name);
flash_dev = fal_flash_device_find(partition->flash_name);
```

通过分区名称查找分区信息，再通过分区关联的 Flash 设备名称找到对应的 Flash 设备。
#### Step 2：擦除整个分区

```c
ret = fal_partition_erase_all(partition);
```

调用 `fal_partition_erase_all()` 擦除分区上的全部数据，擦除后所有数据变为 **0xFF**。
#### Step 3：校验擦除结果

```c
for (i = 0; i < partition->len;)
{
    rt_memset(buf, 0x00, BUF_SIZE);
    len = (partition->len - i) > BUF_SIZE ? BUF_SIZE : (partition->len - i);
    ret = fal_partition_read(partition, i, buf, len);
    // 校验每个字节是否都为 0xFF
    for(j = 0; j < len; j++)
    {
        if (buf[j] != 0xFF)
        {
            LOG_E("The erase operation did not really succeed!");
            return -1;
        }
    }
    i += len;
}
```

循环读取整个分区的数据，逐字节校验是否为 0xFF。若全部为 0xFF，说明擦除操作成功。
#### Step 4：写入整个分区

```c
for (i = 0; i < partition->len;)
{
    rt_memset(buf, 0x00, BUF_SIZE);  // 填充 0x00
    len = (partition->len - i) > BUF_SIZE ? BUF_SIZE : (partition->len - i);
    ret = fal_partition_write(partition, i, buf, len);
    i += len;
}
```

循环写入数据 0x00 到整个分区，每次写入 1024 字节。
#### Step 5：校验写入结果

```c
for (i = 0; i < partition->len;)
{
    rt_memset(buf, 0xFF, BUF_SIZE);
    len = (partition->len - i) > BUF_SIZE ? BUF_SIZE : (partition->len - i);
    ret = fal_partition_read(partition, i, buf, len);
    // 校验每个字节是否都为 0x00
    for(j = 0; j < len; j++)
    {
        if (buf[j] != 0x00)
        {
            LOG_E("The write operation did not really succeed!");
            return -1;
        }
    }
    i += len;
}
```

循环读取并校验数据是否为步骤 4 写入的 0x00，校验通过则说明写操作正常。
### 预期输出

编译下载后，通过串口终端（115200 波特率）可以看到如下输出：

![[贡献周/第十九章/figures/Pasted image 20260827145145.png]]
### Finsh/MSH 命令行测试
FAL 自带了 Finsh/MSH 测试命令，可以通过 Shell 进行操作：
![[贡献周/第十九章/figures/Pasted image 20260827145436.png]]
**常用 FAL Shell 命令：**

|命令|说明|
|---|---|
|`fal probe <partition>`|选择分区|
|`fal read <offset> <size>`|从分区读取指定长度数据|
|`fal write <offset> <data...>`|向分区写入数据|
|`fal erase <offset> <size>`|擦除分区指定区域|
|`fal bench <blk_size>`|分区性能测试|
# 6. 如何应用

### 应用场景一：配合 EasyFlash 实现 KV 存储

EasyFlash 是一款开源的轻量级嵌入式 Flash 存储器库，适合存储配置参数和日志。FAL 为其提供了底层 Flash 分区操作能力。
**步骤：**
1. 在 `RT-Thread Settings` 中使能 **EasyFlash** 软件包
2. 在分区表中预留 `easyflash` 分区（本例程已在 W25Q64 上预留 512KB）
3. EasyFlash 将通过 FAL 接口操作该分区，实现键值存储

```c
#include <easyflash.h>

// 初始化 EasyFlash（内部使用 FAL 操作 easyflash 分区）
easyflash_init();

// 存储配置
ef_set_env("device_id", "STM32F407-001");
ef_set_env("baud_rate", "115200");

// 读取配置
char *device_id = ef_get_env("device_id");
```

### 应用场景二：OTA 固件升级

使用 `download` 分区作为 OTA 固件下载区，配合 OTA 软件包实现固件升级：

```c
#include <fal.h>

// 下载固件到 download 分区
const struct fal_partition *dl_part = fal_partition_find("download");
fal_partition_erase_all(dl_part);
fal_partition_write(dl_part, 0, firmware_data, firmware_size);

// 校验后跳转到 app 分区执行
```

### 应用场景三：文件系统挂载

在 `filesystem` 分区上挂载文件系统（如 LittleFS 或 FatFS）：

```c
#include <fal.h>
#include <dfs_fs.h>

// 使用 FAL 分区作为块设备挂载文件系统
fal_init();

// 在 RT-Thread 中通过 fal 创建块设备，然后挂载
dfs_mount("filesystem", "/", "elm", 0, 0);
```

### 自定义 Flash 设备对接

如需添加新的 Flash 设备（如其他型号的 SPI Flash），只需实现 `fal_flash_dev` 结构体中的 ops 接口并注册到 Flash 设备列表中：

```c
struct fal_flash_dev my_flash =
{
    .name       = "my_flash",
    .addr       = 0,
    .len        = 16 * 1024 * 1024,  // 16MB
    .blk_size   = 4096,
    .ops        = {my_init, my_read, my_write, my_erase},
    .write_gran = 1
};

// 在 FAL_FLASH_DEV_TABLE 中添加
#define FAL_FLASH_DEV_TABLE    \
{                              \
    &stm32_onchip_flash_128k,  \
    &w25q64,                   \
    &my_flash,                 \
}
```

---

# 7. 结论

本章通过 FAL（Flash Abstraction Layer）组件，实现了对 STM32F407 的 **片内 Flash** 和 **片外 W25Q64 Nor Flash** 的统一管理。

|要点|说明|
|---|---|
|**FAL 架构**|Flash 设备层 + 分区管理层，向上提供统一 API|
|**双 Flash 管理**|同时管理片内 Flash（1MB）和片外 W25Q64（8MB）|
|**分区表设计**|7 个分区，合理分配用于 app、参数、文件系统、OTA 等|
|**SFUD 框架**|零代码驱动 W25Q64，自动识别 Flash 型号|
|**测试验证**|完整的擦除→校验→写入→校验测试流程|
|**Shell 调试**|FAL 自带 Finsh 命令，支持按字节读写 Flash|
FAL 组件是 RT-Thread 生态中 OTA 升级、EasyFlash KV 存储、文件系统等高级功能的基础。通过本章的学习，开发者可以：
1. **理解 FAL 的分层架构**，掌握 Flash 设备注册和分区表配置方法
2. **使用 SFUD 框架**快速驱动 SPI Nor Flash，无需手动编写驱动
3. **通过 FAL API 和 Shell 命令**对 Flash 进行读写擦操作
