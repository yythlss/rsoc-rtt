## 新建工程
### 安装 RT-Thread Studio
#### IDE 简介
RT-Thread Studio 是 RT-Thread 官方推出的集成开发环境，基于 Eclipse 平台深度定制。与传统的 MDK（Keil）/IAR 开发方式不同，RT-Thread Studio 针对 RT-Thread 开发流程做了大量优化：
- **工程管理**：可视化创建、配置、编译、下载 RT-Thread 项目
- **图形化配置**：RT-Thread Settings 替代手动编辑 rtconfig.h，支持组件和软件包的可视化管理
- **CubeMX 集成**：内置 STM32CubeMX 支持，可直接在 IDE 中配置引脚和外设
- **软件包生态**：一键搜索、安装、管理 RT-Thread 软件包
- **终端调试**：内置串口终端，直接与 FinSH 命令行交互
- **一键下载**：集成 ST-Link、J-Link、DAP-Link 等多种调试器   
#### 下载与安装步骤
**第一步：下载安装包**
访问[RT-Thread 下载页面](https://www.rt-thread.org/download.html) ：
在下载页面中找到 **RT-Thread Studio** 部分，根据操作系统选择对应的安装包。
- RT-Thread Studio 支持 Windows、Linux 和 macOS 三大平台，其中 Windows 平台使用最为广泛。
**第二步：安装**
![install-studio](https://www.rt-thread.org/document/site/development-tools/rtthread-studio/um/figures/install-studio.png)
1. 双击安装包启动安装向导
2. **关键提示**：安装路径必须选择**全英文且不含空格**的路径。推荐路径：
   - ✅ `D:\RT-Thread\RT-Thread-Studio`
   - ✅ `C:\RTThreadStudio`
   - ❌ `D:\开发工具\RT-Thread Studio`（含中文和空格）
   - ❌ `C:\Program Files\RT-Thread Studio`（含空格）
3. 保持其他选项为默认值，点击"安装"
4. 等待安装完成后，勾选"启动 RT-Thread Studio"
**第三步：首次启动**
首次启动 RT-Thread Studio 时，需要联网登录 RT-Thread 账号。如果没有账号，可以点击"注册"按钮创建新账号。登录后，RT-Thread Studio 会自动初始化工作空间，并检测和下载必要的工具链组件。
![sign-in](https://www.rt-thread.org/document/site/development-tools/rtthread-studio/um/figures/sign-in.png)
#### 界面布局概览
启动完成后，RT-Thread Studio 的主界面分为以下几个区域：

| 区域                 | 位置       | 功能                  |
| ------------------ | -------- | ------------------- |
| 项目资源管理器            | 左侧       | 显示工程文件树，管理项目文件      |
| 代码编辑器              | 中间       | 编写和编辑代码，支持语法高亮和自动补全 |
| RT-Thread Settings | 双击工程下的节点 | 图形化配置内核、组件、驱动和软件包   |
| 控制台                | 下方       | 显示编译输出、下载日志和串口终端    |
| 工具栏                | 顶部       | 提供编译、下载、调试等快捷操作按钮   |
### 基于开发板创建工程
#### 安装对应的SDK支持包
1. 打开 RT-Thread Studio，点击菜单栏 **文件 → 包管理器**（或点击工具栏上的 SDK 管理器图标）
2. 在弹出的包管理器中，找到 **Board_Support_Packages** 分类
3. 在搜索框中输入对应的开发板支持包
4. 点击**安装**按钮，等待下载和安装完成
SDK 安装完成后，在包管理器中可以看到已安装的 SDK 版本信息
##### SDK 资源包包含的内容

| 内容    | 说明                    |
| ----- | --------------------- |
| 芯片支持包 | 基础工程框架                |
| 板级支持包 | 开发板的 board.c/h 和驱动文件  |
| 示例代码  | 按学习阶段编号的示例项目          |
| 库文件   | STM32 HAL 库和 CMSIS 组件 |
#### 创建工程
1. 在项目资源管理器窗口内点击右键，选择新建子菜单项目。
2. 在弹出的新建项目向导对话框中选择 RT‑Thread 项目类型，然后点击下一步。
3. 填写工程名，选择基于开发板创建工程，选择开发板型号，选择 BSP 版本号，选择 RT‑Thread 源码版本，选择调试器和调试接口，然后点击完成按钮。
4. 点击完成后，等待工程创建过程。
5. 工程创建成功后项目资源管理器窗口会出现刚创建的工程 test。
### 基于芯片支持包创建工程
#### 创建工程
1. 点击菜单栏 **文件 → 新建 → RT-Thread 项目**
2. 在弹出的新建项目向导中，选择 **"基于芯片"** 选项卡
3. 在芯片列表中展开对应的系列，找到并选择对应的芯片
4. 在右侧填写项目信息：
   - **项目名称**
   - **项目位置**：选择工作空间路径（建议全英文路径）
   - **RT-Thread 版本**：
1. 点击 **完成**，RT-Thread Studio 会自动生成工程文件
创建完成后，在项目资源管理器中可以看到工程目录结构。
#### 工程目录结构详解
创建完成后，工程目录结构如下：

```
rsoc_chapter01/                        # 工程根目录
├── .settings/                         # IDE 配置文件（自动生成）
├── applications/                      # 应用层代码
│   └── main.c                         # 主程序入口
├── board/                             # 板级支持包（BSP）
│   ├── board.c                        # 板级初始化（时钟、外设、内存）
│   ├── board.h                        # 板级头文件（引脚定义）
│   ├── board_linker/                  # 链接脚本
│   │   └── link.lds                   # 内存布局定义
│   ├── CubeMX_Config/                 # CubeMX 配置
│   │   ├── CubeMX_Config.ioc          # CubeMX 工程文件
│   │   └── Src/                       # CubeMX 生成的 HAL 初始化代码
│   │       ├── main.c                 # HAL 库初始化
│   │       └── stm32f4xx_hal_msp.c    # 外设 MSP 初始化
│   └── ports/                         # 板级驱动（针对具体外设）
├── rt-thread/                         # RT-Thread 内核源码
│   ├── src/                           # 内核源码
│   ├── include/                       # 内核头文件
│   ├── components/                    # 组件源码
│   │   ├── finsh/                     # FinSH 命令行组件
│   │   ├── dfs/                       # 虚拟文件系统
│   │   └── drivers/                   # 驱动框架
│   └── libcpu/                        # 处理器架构相关代码
│       └── arm/cortex-m4/             # ARM Cortex-M4 移植代码
├── libraries/                         # 芯片厂商库
│   └── HAL_Drivers/                   # STM32 HAL 库
│       ├── Inc/                       # HAL 库头文件
│       └── Src/                       # HAL 库源码
├── rtconfig.h                         # RT-Thread 内核配置（自动生成）
├── rtconfig_preinc.h                  # 预编译配置
├── SConscript                         # SCons 构建脚本
├── SConstruct                         # SCons 顶层构建脚本
└── template.uvprojx                   # 可选的 MDK 工程模板
```

**关键目录说明：**

| 目录/文件                                   | 作用                  | 是否需要修改                        |
| --------------------------------------- | ------------------- | ----------------------------- |
| `applications/main.c`                   | 用户应用入口，编写业务逻辑       | ✅ 经常修改                        |
| `board/board.h`                         | 引脚定义，外设宏定义          | ✅ 按需修改                        |
| `board/board.c`                         | 系统时钟配置，板级初始化        | ✅ 按需修改                        |
| `board/CubeMX_Config/CubeMX_Config.ioc` | CubeMX 工程，用于引脚和时钟配置 | ✅ 按需修改                        |
| `rtconfig.h`                            | RT-Thread 内核配置头文件   | ⚠️ 通过 RT-Thread Settings 间接修改 |
| `rt-thread/`                            | RT-Thread 内核源码      | ❌ 一般不修改                       |
| `libraries/`                            | HAL 库源码             | ❌ 一般不修改                       |
#### 编译与下载验证
**第一步：编译工程**
右键工程名称 → **重新构建项目**（或点击工具栏的锤子图标）。
编译成功后，控制台输出类似以下内容：

```
...
arm-none-eabi-objcopy -O ihex rtthread.elf rtthread.hex
arm-none-eabi-size rtthread.elf
   text    data     bss     dec     hex filename
  42345    1032    4304   47681    ba41 rtthread.elf
```

- `text`：代码段大小（Flash 占用）
- `data`：已初始化数据段大小
- `bss`：未初始化数据段大小（RAM 占用）
- `dec`：总大小（十进制）= text + data + bss
**第二步：连接硬件**
1. 使用 USB 线连接开发板的 ST-Link 接口到电脑
2. 使用另一个 USB 转 TTL 串口模块连接开发板的 UART1 引脚（PA9 TX / PA10 RX）
3. 确保 ST-Link 驱动已安装
**第三步：下载程序**
右键工程 → **调试方式 → ST-Link 下载**（或点击工具栏的下载图标）。
**第四步：串口验证**
打开串口终端（波特率 115200，数据位 8，停止位 1，无校验），按下开发板复位键，应看到 RT-Thread 的启动 Logo：

```
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 24 2026
 2006 - 2023 Copyright by RT-Thread team
msh >
```

看到 `msh >` 提示符，说明工程创建成功、编译下载正常、RT-Thread 系统正常运行。
### 启动流程分析
理解 RT-Thread 的启动流程，有助于后续的 BSP 配置和驱动开发。
- 从上电到 `msh >` 提示符的完整启动链路：

```
上电复位
  │
  ▼
Reset_Handler (startup_stm32f407xx.s)
  │  └── SystemInit()                  -- 系统初始化（FPU、时钟源选择）
  │  └── __main()                      -- C 运行时初始化（BSS 清零、数据段拷贝）
  │
  ▼
main()  [board/CubeMX_Config/Src/main.c]
  │  └── HAL_Init()                    -- HAL 库初始化
  │  └── SystemClock_Config()          -- 时钟树配置（168MHz）
  │
  ▼
rtthread_startup()  [rt-thread/src/components.c]
  │  └── rt_hw_board_init()            -- 板级硬件初始化 [board/board.c]
  │       └── 时钟初始化
  │       └── 内存初始化
  │       └── 中断初始化
  │       └── 控制台初始化
  │  └── rt_show_version()             -- 打印 RT-Thread Logo
  │  └── rt_system_timer_init()        -- 系统定时器初始化
  │  └── rt_system_scheduler_init()    -- 调度器初始化
  │  └── rt_application_init()         -- 应用初始化（创建 main 线程）
  │  └── rt_system_timer_thread_init() -- 定时器线程初始化
  │  └── rt_thread_idle_init()         -- 空闲线程初始化
  │  └── rt_system_scheduler_start()   -- 启动调度器
  │
  ▼
main 线程入口  [applications/main.c]
  │  └── main()                        -- 用户 main 函数
  │       └── 用户初始化代码
  │
  ▼
调度器运行
  │  └── FinSH 线程启动
  │  └── msh > 提示符出现
```

>`applications/main.c` 中的 `main()` 函数并不是系统启动的入口，而是 RT-Thread 调度器启动后，在 main 线程中执行的用户入口函数。
>真正的系统启动入口在 `components.c` 的 `rtthread_startup()` 中。
## RT-Thread Setting 的配置
### 配置界面概览
RT-Thread Settings 是 RT-Thread Studio 提供的图形化配置工具，它本质上是 `rtconfig.h` 的可视化编辑器。双击工程资源管理器中的 **RT-Thread Settings** 节点，即可打开配置界面。
![image-20210127181544774](https://www.rt-thread.org/document/site/development-tools/rtthread-studio/figures/image-20210127181325681.png)

| 标签页 | 功能 | 对应关系 |
|--------|------|---------|
| **内核** | 内核功能裁剪：线程、信号量、内存管理、IPC 等 | 对应 `rtconfig.h` 中的 `RT_USING_*` 宏 |
| **组件** | 组件启用/禁用：FinSH、DFS、网络、驱动框架等 | 对应 `rtconfig.h` 中的组件配置宏 |
| **软件包** | 在线搜索、安装、管理 RT-Thread 软件包 | 对应 `packages/` 目录和 `rtconfig.h` 中的软件包宏 |
| **硬件** | 芯片和外设的硬件配置 | 对应 `board.h` 和 HAL 库配置 |
### 内核配置
**内核配置决定了 RT-Thread 内核的基本行为。**
#### 内核基础配置

| 配置项                            | 说明            | 建议值  | 备注                        |
| ------------------------------ | ------------- | ---- | ------------------------- |
| 系统 Tick 频率（RT_TICK_PER_SECOND） | 每秒的系统节拍数      | 1000 | 值为 1000 时，每个 Tick = 1ms   |
| 线程优先级数量                        | 可用的优先级级别数     | 32   | 数值越小优先级越高，0 为最高优先级        |
| 最大线程名称长度                       | 线程名称字符串的最大长度  | 8    | 包括 `\0` 结束符               |
| 空闲线程栈大小                        | 空闲线程的栈空间大小    | 256  | 空闲线程只做简单操作，256 字节足够       |
| 主线程栈大小                         | main 线程的栈空间大小 | 2048 | 如果 main 函数中创建了大量局部变量，需要增大 |
#### Tick 
系统 Tick 是 RT-Thread 内核的心跳。以 `RT_TICK_PER_SECOND = 1000` 为例：

```
时间单位换算：
  1 Tick = 1ms = 1/1000 秒
  1 秒 = 1000 Tick

API 中的延时单位：
  rt_thread_mdelay(500)  →  延时 500ms（500 Tick）
  rt_thread_sleep(10)    →  休眠 10 Tick（10ms）
  rt_tick_get()          →  获取从启动到现在的 Tick 计数
```

> Tick 频率越高，系统实时性越好，但上下文切换的开销也越大。
#### 内存管理配置

| 配置项      | 说明                                 | 建议值          |
| -------- | ---------------------------------- | ------------ |
| 使用动态内存管理 | 启用 `rt_malloc`/`rt_free` 等动态内存分配函数 | ✅ 启用         |
| 堆大小      | 动态内存堆的总大小                          | 16384（16KB）  |
| 内存管理算法   | 小内存管理算法（small memory）或 SLAB 算法     | small memory |
> **堆大小设置建议**：基于芯片创建工程时，默认堆大小通常为 16KB。如果后续需要启用网络功能（如 LWIP），需要增加到 64KB 以上。可以在 `board.c` 的 `rt_hw_board_init()` 函数中找到 `HEAP_BEGIN` 和 `HEAP_END` 的定义。
#### 线程间通信（IPC）配置

| 配置项 | 说明 | 建议值 |
|--------|------|--------|
| 使用信号量 | 线程间同步机制 | ✅ 启用 |
| 使用互斥量 | 保护共享资源 | ✅ 启用 |
| 使用事件集 | 一对多的事件通知机制 | ✅ 启用 |
| 使用邮箱 | 线程间消息传递 | ✅ 启用 |
| 使用消息队列 | 线程间流式数据传递 | 按需启用 |
### 组件配置
#### FinSH 命令行组件
FinSH 是 RT-Thread 的命令行组件，提供了类似 Linux Shell 的交互式调试接口。这是开发过程中**最重要的调试工具**之一。

| 配置项 | 说明 | 建议值 |
|--------|------|--------|
| 启用 FinSH | 开启 FinSH 命令行组件 | ✅ 启用 |
| FinSH 线程优先级 | FinSH 线程的优先级 | 21 |
| FinSH 线程栈大小 | FinSH 线程的栈空间 | 4096 |
| FinSH 历史命令数量 | 可通过 ↑↓ 键回显的历史命令数 | 5 |
| 使用 msh 模式 | 使用增强的 msh 模式（支持自动补全） | ✅ 启用 |
| 使用串口方式 | 通过串口与 FinSH 交互 | ✅ 启用 |
| 串口设备名称 | FinSH 使用的串口设备名 | uart1 |
> 启用 FinSH 后，可以在串口终端中输入 `list_device`（查看设备列表）、`ps`（查看线程状态）、`free`（查看内存使用）、`help`（查看所有命令）等命令，实时监控系统运行状态。

#### 设备驱动框架
切换到"组件 → 驱动"标签页，这里是驱动框架的配置核心。驱动框架采用"按需启用"原则——**需要什么外设，就启用什么驱动框架**。

| 驱动框架 | 配置项 | 说明 | 本章建议 |
|---------|--------|------|---------|
| GPIO | 使用 GPIO 驱动 | PIN 设备驱动框架 | ✅ 启用 |
| UART | 使用 UART 驱动 | 串口设备驱动框架 | ✅ 启用 |
| SPI | 使用 SPI 驱动 | SPI 总线设备驱动框架 | 后续章节按需启用 |
| I2C | 使用 I2C 驱动 | I2C 总线设备驱动框架 | 后续章节按需启用 |
| ADC | 使用 ADC 驱动 | 模数转换器驱动框架 | 后续章节按需启用 |
| DAC | 使用 DAC 驱动 | 数模转换器驱动框架 | 后续章节按需启用 |
| RTC | 使用 RTC 驱动 | 实时时钟驱动框架 | 后续章节按需启用 |
| WDT | 使用 WDT 驱动 | 看门狗驱动框架 | 后续章节按需启用 |
| HWTIMER | 使用 HWTIMER 驱动 | 硬件定时器驱动框架 | 后续章节按需启用 |
| PWM | 使用 PWM 驱动 | 脉冲宽度调制驱动框架 | 后续章节按需启用 |
| CAN | 使用 CAN 驱动 | CAN 总线驱动框架 | 后续章节按需启用 |
| SDIO | 使用 SDIO 驱动 | SDIO 接口驱动框架 | 后续章节按需启用 |
#### 其他常用组件

| 组件 | 说明 | 本章建议 |
|------|------|---------|
| DFS | 虚拟文件系统 | 不启用（第十八章启用） |
| 网络 | 网络协议栈（LWIP） | 不启用（第二十二章启用） |
| ulog | 日志组件 | 可选启用 |
| utest | 单元测试框架 | 不启用 |
### 软件包管理
#### 软件包搜索与安装
**操作步骤：**
1. 在软件包标签页的搜索框中输入关键词
2. 浏览搜索结果，点击软件包名称查看详细信息
3. 点击 **安装** 按钮，RT-Thread Studio 自动下载并集成到工程中
安装完成后，软件包源码会出现在 `packages/` 目录下，同时 `rtconfig.h` 中会自动添加对应的配置宏。
### 配置保存与生效
当在 RT-Thread Settings 中修改配置后，点击 **保存** 按钮，RT-Thread Studio 会执行以下操作：

```
点击"保存"
  │
  ├── 1. 更新 rtconfig.h 中的宏定义
  │     └── 例如：启用 GPIO 驱动 → #define RT_USING_PIN
  │
  ├── 2. 更新 packages/Kconfig 和 packages/ 目录
  │     └── 例如：安装 multibutton → 下载源码到 packages/multibutton-xxx/
  │
  └── 3. 触发 SCons 重新生成构建配置
        └── 更新编译依赖关系
```

## board.c/h 的配置（包括引脚的初始化，配合 CubeMX）
### board.h — 引脚定义与宏配置
`board.h` 是板级头文件，主要负责三件事：**引脚宏定义**、**外设宏定义**和**板级参数配置**。
#### 引脚宏定义（使用 GET_PIN 宏）
RT-Thread 使用 `GET_PIN(port, pin)` 宏将物理引脚编号转换为内部编号，实现硬件无关的引脚访问：

```c
/* 语法：GET_PIN(端口字母, 引脚编号) */
#define LED_R_PIN    GET_PIN(F, 9)   /* PF9  → 红色 LED */
#define LED_B_PIN    GET_PIN(F, 10)  /* PF10 → 蓝色 LED */
#define KEY0_PIN     GET_PIN(C, 0)   /* PC0  → 用户按键 KEY0 */
#define KEY1_PIN     GET_PIN(C, 1)   /* PC1  → 用户按键 KEY1 */
#define WK_UP_PIN    GET_PIN(A, 0)   /* PA0  → 唤醒按键 */
#define BEEP_PIN     GET_PIN(B, 2)   /* PB2  → 蜂鸣器 */
```

#### GET_PIN 宏的原理
`GET_PIN` 宏将 GPIO 端口和引脚编号组合成一个 16 位的整数：

```c
#define GET_PIN(PORT, PIN)  ((uint16_t)(((uint16_t)(PORT - 'A') & 0x0F) << 8) | ((uint16_t)PIN & 0xFF))

/* 编码公式：
 *   高 8 位：端口编号（PORT - 'A'），取值 0~15（对应 GPIOA~GPIOP）
 *   低 8 位：引脚编号，取值 0~15
 *
 * 示例：
 *   GET_PIN(F, 9)  →  ((5) << 8) | 9  →  (0x0500) | 0x0009  →  0x0509
 *   GET_PIN(A, 0)  →  ((0) << 8) | 0  →  (0x0000) | 0x0000  →  0x0000
 *   GET_PIN(B, 2)  →  ((1) << 8) | 2  →  (0x0100) | 0x0002  →  0x0102
 */
```

#### 星火 1 号完整引脚定义
基于星火 1 号原理图，以下是 `board.h` 中推荐添加的完整引脚定义（按功能分组，便于维护）：

```c
/* ==================== 星火 1 号 board.h 引脚定义 ==================== */

/* --- LED 指示灯 --- */
#define LED_R_PIN    GET_PIN(F, 9)   /* 红色 LED（低电平亮） */
#define LED_B_PIN    GET_PIN(F, 10)  /* 蓝色 LED（低电平亮） */

/* --- 按键 --- */
#define KEY0_PIN     GET_PIN(C, 0)   /* 用户按键 KEY0（按下低电平） */
#define KEY1_PIN     GET_PIN(C, 1)   /* 用户按键 KEY1（按下低电平） */
#define WK_UP_PIN    GET_PIN(A, 0)   /* 唤醒按键 WK_UP（按下高电平） */

/* --- 蜂鸣器 --- */
#define BEEP_PIN     GET_PIN(B, 2)   /* 蜂鸣器 */

/* --- 串口 --- */
#define CONSOLE_TX   GET_PIN(A, 9)   /* UART1 TX（调试控制台） */
#define CONSOLE_RX   GET_PIN(A, 10)  /* UART1 RX（调试控制台） */
#define RS485_TX     GET_PIN(D, 5)   /* UART2 TX（RS485） */
#define RS485_RX     GET_PIN(D, 6)   /* UART2 RX（RS485） */
#define RS485_RTS    GET_PIN(D, 4)   /* RS485 方向控制 */

/* --- I2C 总线 --- */
#define I2C1_SCL     GET_PIN(B, 6)   /* I2C1 SCL（AHT10/AP3216C） */
#define I2C1_SDA     GET_PIN(B, 7)   /* I2C1 SDA */
#define I2C2_SCL     GET_PIN(B, 10)  /* I2C2 SCL（ES8388 音频） */
#define I2C2_SDA     GET_PIN(B, 11)  /* I2C2 SDA */

/* --- SPI 总线 --- */
#define SPI1_SCK     GET_PIN(A, 5)   /* SPI1 SCK（ICM20608） */
#define SPI1_MISO    GET_PIN(A, 6)   /* SPI1 MISO */
#define SPI1_MOSI    GET_PIN(A, 7)   /* SPI1 MOSI */
#define SPI1_CS      GET_PIN(A, 4)   /* SPI1 NSS（ICM20608 片选） */
#define SPI2_SCK     GET_PIN(B, 13)  /* SPI2 SCK（W25Q64 / RW007） */
#define SPI2_MISO    GET_PIN(B, 14)  /* SPI2 MISO */
#define SPI2_MOSI    GET_PIN(B, 15)  /* SPI2 MOSI */
#define SPI2_FLASH_CS GET_PIN(B, 12) /* W25Q64 片选 */
#define SPI2_WIFI_CS GET_PIN(C, 4)   /* RW007 片选 */
#define SPI3_SCK     GET_PIN(B, 3)   /* SPI3 SCK（ST7789 LCD） */
#define SPI3_MOSI    GET_PIN(B, 5)   /* SPI3 MOSI */
#define SPI3_LCD_CS  GET_PIN(A, 15)  /* ST7789 片选 */
#define SPI3_LCD_DC  GET_PIN(B, 4)   /* ST7789 数据/命令 */
#define SPI3_LCD_RST GET_PIN(A, 8)   /* ST7789 复位 */
#define SPI3_LCD_BLK GET_PIN(C, 8)   /* ST7789 背光 */

/* --- CAN 总线 --- */
#define CAN1_TX      GET_PIN(D, 1)   /* CAN1 TX */
#define CAN1_RX      GET_PIN(D, 0)   /* CAN1 RX */

/* --- USB --- */
#define USB_DP       GET_PIN(A, 12)  /* USB D+ */
#define USB_DM       GET_PIN(A, 11)  /* USB D- */

/* --- SDIO --- */
#define SDIO_D0      GET_PIN(C, 8)   /* SDIO D0（TF 卡） */
#define SDIO_D1      GET_PIN(C, 9)   /* SDIO D1 */
#define SDIO_D2      GET_PIN(C, 10)  /* SDIO D2 */
#define SDIO_D3      GET_PIN(C, 11)  /* SDIO D3 */
#define SDIO_CLK     GET_PIN(C, 12)  /* SDIO CLK */
#define SDIO_CMD     GET_PIN(D, 2)   /* SDIO CMD */

/* --- I2S 音频 --- */
#define I2S2_WS      GET_PIN(B, 12)  /* I2S2 WS（ES8388） */
#define I2S2_CK      GET_PIN(B, 13)  /* I2S2 CK */
#define I2S2_SD      GET_PIN(C, 3)   /* I2S2 SD */
#define I2S2_MCK     GET_PIN(C, 6)   /* I2S2 MCK */

/* --- 红外 --- */
#define IR_IN_PIN    GET_PIN(A, 1)   /* 红外接收 */

/* --- 可编程彩灯 --- */
#define SK6805_PIN   GET_PIN(F, 11)  /* SK6805 数据引脚 */
```

### board.c — 时钟配置与板级初始化
`board.c` 是板级初始化文件，负责系统时钟配置、内存初始化、外设初始化和控制台配置。
#### 时钟配置函数
`board.c` 中的时钟配置核心是 `SystemClock_Config()` 函数，它通过 HAL 库配置 STM32 的时钟树：

```c
/**
 * 系统时钟配置
 * 目标：HCLK = 168MHz, PCLK1 = 42MHz, PCLK2 = 84MHz
 */
void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    /* 1. 配置 HSE（外部高速晶振） */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;         /* 启用 HSE */
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;     /* 启用 PLL */
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;  /* PLL 时钟源 = HSE */
    RCC_OscInitStruct.PLL.PLLM = 8;                  /* 分频因子 M = 8 */
    RCC_OscInitStruct.PLL.PLLN = 336;                /* 倍频因子 N = 336 */
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;      /* 分频因子 P = 2 */
    RCC_OscInitStruct.PLL.PLLQ = 7;                  /* 分频因子 Q = 7 */
    HAL_RCC_OscConfig(&RCC_OscInitStruct);

    /* 2. 配置系统时钟源、AHB/APB 总线和 Flash 等待周期 */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK |
                                   RCC_CLOCKTYPE_SYSCLK |
                                   RCC_CLOCKTYPE_PCLK1 |
                                   RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;  /* 系统时钟 = PLL */
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;         /* AHB = SYSCLK / 1 */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;          /* APB1 = AHB / 4 */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;          /* APB2 = AHB / 2 */
    HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5);
}
```

**STM32F407 时钟树计算：**

```
HSE = 8MHz（外部晶振）
  │
  ├── PLLM = 8   →  PLL 输入 = 8MHz / 8 = 1MHz
  │
  ├── PLLN = 336  →  VCO 输出 = 1MHz × 336 = 336MHz
  │
  ├── PLLP = 2   →  SYSCLK = 336MHz / 2 = 168MHz
  │
  ├── AHB (HCLK) = SYSCLK / 1  = 168MHz
  │   ├── APB1 (PCLK1) = 168MHz / 4 = 42MHz（低速外设：UART2-5, I2C, SPI2-3）
  │   └── APB2 (PCLK2) = 168MHz / 2 = 84MHz（高速外设：UART1, SPI1, ADC）
  │
  └── PLLQ = 7  →  48MHz（USB OTG / SDIO 时钟）
```

#### 板级初始化函数
`rt_hw_board_init()` 是 RT-Thread 启动时调用的板级初始化函数，位于 `board.c` 中：

```c
void rt_hw_board_init(void)
{
    /* 1. HAL 库初始化 */
    HAL_Init();

    /* 2. 系统时钟配置 */
    SystemClock_Config();

    /* 3. 系统 Tick 定时器初始化（SysTick 或专用定时器） */
    rt_hw_systick_init();

    /* 4. 板级硬件初始化 */
    /*    - 引脚时钟使能 */
    /*    - 外设初始化 */
    /*    - 控制台串口初始化 */

    /* 5. 内存堆初始化 */
#if defined(RT_USING_HEAP)
    rt_system_heap_init((void *)HEAP_BEGIN, (void *)HEAP_END);
#endif

    /* 6. 中断向量表配置 */
    /* 7. 控制台设备注册 */
    rt_console_set_device(RT_CONSOLE_DEVICE_NAME);
}
```

#### 内存堆配置
在 `board.c` 中，`HEAP_BEGIN` 和 `HEAP_END` 定义了动态内存堆的起止地址：

```c
#define HEAP_BEGIN  ((void *)&__bss_end)        /* 堆起始地址 = BSS 段末尾 */
#define HEAP_END    ((void *)(0x20000000 + 0x20000))  /* 堆结束地址 = SRAM 末尾 */
```

### 配合 STM32CubeMX 进行引脚配置
#### CubeMX 在 RT-Thread 开发中的角色
在 RT-Thread 的开发流程中，CubeMX 负责**引脚分配和硬件配置**，RT-Thread Settings 负责**软件配置**。两者分工明确：

| 配置工具 | 负责内容 | 输出文件 |
|---------|---------|---------|
| CubeMX | 引脚复用、时钟树、外设参数（波特率、SPI 模式等） | `CubeMX_Config.ioc` → `main.c` / `stm32f4xx_hal_msp.c` |
| RT-Thread Settings | 内核裁剪、组件启用、驱动框架、软件包 | `rtconfig.h` |

#### 打开 CubeMX 工程
在 RT-Thread Studio 中直接打开 CubeMX：
1. 在项目资源管理器中，展开 `board/CubeMX_Config/` 目录
2. 双击 `CubeMX_Config.ioc` 文件
3. RT-Thread Studio 会自动启动 STM32CubeMX 并加载工程
#### 引脚配置步骤
以配置 LED 引脚（PF9、PF10）为例：
**第一步：进入引脚配置界面**
在 CubeMX 主界面中，点击 **Pinout & Configuration** 标签页，在右侧的芯片引脚图中找到目标引脚。
**第二步：配置引脚功能**
点击 PF9 引脚，在弹出的功能列表中选择 **GPIO_Output**。同样的方法配置 PF10。
**第三步：配置引脚属性**
在左侧的 **System Core → GPIO** 列表中，找到 PF9 和 PF10，配置以下属性：

| 属性 | 值 | 说明 |
|------|-----|------|
| GPIO output level | High | 初始输出高电平（LED 熄灭） |
| GPIO mode | Output Push Pull | 推挽输出模式 |
| GPIO Pull-up/Pull-down | No pull-up and no pull-down | 无上下拉 |
| Maximum output speed | Low | 低速输出（LED 控制不需要高速） |
| User Label | LED_R / LED_B | 自定义标签（便于代码可读性） |

**第四步：配置时钟树**
切换到 **Clock Configuration** 标签页，确认时钟树配置正确：

```
HSE: 8 MHz → PLLM:/8 → PLLN:×336 → PLLP:/2 → SYSCLK: 168 MHz
                                              → AHB: 168 MHz
                                              → APB1: 42 MHz
                                              → APB2: 84 MHz
```

**第五步：生成代码**
点击工具栏的 **GENERATE CODE** 按钮（齿轮图标），CubeMX 会重新生成 `board/CubeMX_Config/Src/main.c` 和 `stm32f4xx_hal_msp.c`。
**第六步：同步到 RT-Thread 工程**
CubeMX 生成代码后，RT-Thread Studio 会自动检测到变更并提示是否同步。点击 **是** 完成同步。

> **注意**：CubeMX 生成的 `main.c` 中的 `main()` 函数和 `SystemClock_Config()` 函数会被 RT-Thread 的 `board.c` 引用和调用。不要直接修改 CubeMX 生成的文件中的 `main()` 函数，所有用户代码都应该写在 `applications/main.c` 中。
## 相关框架的说明（RT-Thread I/O 设备模型）
### 为什么需要设备模型？
在传统的裸机开发中，操作外设的方式是直接调用 HAL 库函数：

```c
/* 裸机方式：操作 GPIO */
HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_RESET);  /* 点亮 LED */
HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_SET);    /* 熄灭 LED */

/* 裸机方式：发送串口数据 */
HAL_UART_Transmit(&huart1, data, len, 1000);            /* 阻塞发送 */
```

这种方式的**痛点**：
1. **代码不通用**：换一个引脚、换一个芯片型号，代码就要大量修改
2. **接口不统一**：GPIO 用 `HAL_GPIO_WritePin`，UART 用 `HAL_UART_Transmit`，SPI 用 `HAL_SPI_Transmit`——每个外设的 API 都不同
3. **缺乏抽象**：上层应用直接依赖底层硬件，耦合度高，移植困难
4. **并发访问困难**：多个线程同时操作同一个外设时，需要自己实现互斥机制
RT-Thread 的 I/O 设备模型正是为了解决这些问题而设计的。它提供了一套**统一的设备访问接口**，让开发者使用相同的 API 操作不同类型的外设，同时由框架层负责并发控制和资源管理。
### 设备模型的四层架构
RT-Thread 的 I/O 设备模型采用分层架构设计，从应用层到硬件层共分为四层：
![I/O 设备模型框架](https://www.rt-thread.org/document/site/rt-thread-version/rt-thread-standard/programming-manual/device/figures/io-dev.png)

**各层职责：**

| 层次 | 职责 | 示例 |
|------|------|------|
| 应用层 | 使用统一的设备接口操作设备 | `rt_device_write(dev, data, len)` |
| 驱动框架层 | 定义设备类型、提供类型专属的扩展接口 | `rt_pin_write(pin, value)` |
| 设备驱动层 | 实现框架层定义的接口，调用 HAL 库操作硬件 | `stm32_pin_write()` → `HAL_GPIO_WritePin()` |
| 硬件层 | 芯片厂商提供的底层库 | STM32 HAL 库 / CMSIS |
### 核心数据结构：rt_device
`rt_device` 是 RT-Thread 设备模型的核心数据结构，所有设备对象都必须包含它（通常通过继承的方式）：

```c
struct rt_device
{
    struct rt_object          parent;       /* 内核对象基类（包含名称、类型等） */
    enum rt_device_class_type type;         /* 设备类型 */
    rt_uint16_t               flag;         /* 设备标志（读写/打开/关闭方式） */
    rt_uint16_t               open_flag;    /* 设备打开标志（由 open 函数设置） */
    rt_uint8_t                ref_count;    /* 引用计数（多线程安全） */
    rt_uint8_t                device_id;    /* 设备 ID（0-255） */

    /* 设备操作接口（函数指针） */
    rt_err_t (*rx_indicate)(rt_device_t dev, rt_size_t size);  /* 接收回调 */
    rt_err_t (*tx_complete)(rt_device_t dev, void *buffer);    /* 发送完成回调 */

    /* 标准操作接口 */
    rt_err_t  (*init)   (rt_device_t dev);                       /* 初始化 */
    rt_err_t  (*open)   (rt_device_t dev, rt_uint16_t oflag);    /* 打开 */
    rt_err_t  (*close)  (rt_device_t dev);                       /* 关闭 */
    rt_size_t (*read)   (rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);  /* 读取 */
    rt_size_t (*write)  (rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size); /* 写入 */
    rt_err_t  (*control)(rt_device_t dev, int cmd, void *args);  /* 控制 */

    void *user_data;    /* 用户自定义数据 */
};
```

- **关键字段说明：**

| 字段 | 类型 | 说明 |
|------|------|------|
| `parent` | `rt_object` | 继承自内核对象基类，包含设备名称（`name`）和内核对象类型 |
| `type` | `enum` | 设备类型（字符设备、块设备、网络设备等，共 17 种） |
| `flag` | `rt_uint16_t` | 设备读写标志：`RT_DEVICE_FLAG_RDONLY`（只读）、`RT_DEVICE_FLAG_WRONLY`（只写）、`RT_DEVICE_FLAG_RDWR`（读写） |
| `ref_count` | `rt_uint8_t` | 引用计数：每调用一次 `rt_device_open()`，计数 +1；每调用一次 `rt_device_close()`，计数 -1。计数为 0 时才能安全关闭设备 |
| `init` / `open` / `close` | 函数指针 | 设备生命周期管理接口 |
| `read` / `write` | 函数指针 | 数据收发接口 |
| `control` | 函数指针 | 设备控制接口（配置参数、设置模式等） |
### 面向对象设计：设备继承链
RT-Thread 在 C 语言中实现了面向对象的继承机制。以 PIN 设备为例：

```c
/* 1. 设备模型基类：rt_device */
struct rt_device
{
    struct rt_object parent;
    enum rt_device_class_type type;
    /* ... 其他基类成员 */
};

/* 2. PIN 设备子类：继承 rt_device */
struct rt_device_pin
{
    struct rt_device parent;          /* 继承 rt_device */
    const struct rt_pin_ops *ops;     /* 引脚操作接口 */
    /* ... PIN 设备专属成员 */
};

/* 3. PIN 设备操作接口 */
struct rt_pin_ops
{
    void (*pin_mode)(struct rt_device *device, rt_base_t pin, rt_base_t mode);
    void (*pin_write)(struct rt_device *device, rt_base_t pin, rt_base_t value);
    int  (*pin_read)(struct rt_device *device, rt_base_t pin);
    rt_err_t (*pin_attach_irq)(struct rt_device *device, rt_int32_t pin,
                                rt_uint32_t mode, void (*hdr)(void *args), void *args);
    rt_err_t (*pin_detach_irq)(struct rt_device *device, rt_int32_t pin);
    rt_err_t (*pin_irq_enable)(struct rt_device *device, rt_base_t pin, rt_uint32_t enabled);
};
```

**继承链的访问方式：**

```c
/* 父类指针 → 子类对象 */
struct rt_device_pin *pin_dev = (struct rt_device_pin *)rt_device_find("pin");

/* 子类 → 父类 */
rt_device_t dev = &pin_dev->parent;

/* 通过父类指针调用子类实现的接口 */
rt_device_write(dev, 0, &pin_value, sizeof(pin_value));  /* 多态调用 */
```

这种设计使得所有的设备操作都可以通过 `rt_device` 基类指针进行统一调用，而具体的实现由子类完成。这就是 C 语言中的**多态**。
### 设备类型分类
RT-Thread 定义了 17 种设备类型，覆盖了嵌入式开发中几乎所有常见的外设：

| 类型常量 | 数值 | 设备类型 | 说明 |
|---------|------|---------|------|
| `RT_Device_Class_Char` | 0 | 字符设备 | 串口、控制台等流式设备 |
| `RT_Device_Class_Block` | 1 | 块设备 | SD 卡、Flash 等块存储设备 |
| `RT_Device_Class_NetIf` | 2 | 网络接口 | 以太网、Wi-Fi 网络接口 |
| `RT_Device_Class_MTD` | 3 | 内存技术设备 | NOR/NAND Flash 底层设备 |
| `RT_Device_Class_CAN` | 4 | CAN 设备 | CAN 总线控制器 |
| `RT_Device_Class_RTC` | 5 | RTC 设备 | 实时时钟 |
| `RT_Device_Class_Sound` | 6 | 声音设备 | 音频播放/录制 |
| `RT_Device_Class_Graphic` | 7 | 图形设备 | LCD 显示、GPU |
| `RT_Device_Class_I2CBUS` | 8 | I2C 总线 | I2C 总线控制器 |
| `RT_Device_Class_SPIBUS` | 9 | SPI 总线 | SPI 总线控制器 |
| `RT_Device_Class_SPIDevice` | 10 | SPI 设备 | 挂载在 SPI 总线上的从设备 |
| `RT_Device_Class_SDIO` | 11 | SDIO 设备 | SDIO 接口控制器 |
| `RT_Device_Class_PM` | 12 | 电源管理 | 电源管理框架 |
| `RT_Device_Class_Pipe` | 13 | 管道设备 | 线程间管道通信 |
| `RT_Device_Class_PHY` | 14 | PHY 设备 | 以太网 PHY 芯片 |
| `RT_Device_Class_Miscellaneous` | 15 | 杂项设备 | PIN 设备、ADC、DAC、PWM 等 |
| `RT_Device_Class_Timer` | 16 | 定时器设备 | 硬件定时器 |
> **注意**：PIN 设备（GPIO）、ADC 设备、DAC 设备、PWM 设备、WDT 设备等都属于 `RT_Device_Class_Miscellaneous`（杂项设备）类型。这是因为它们虽然功能不同，但在设备模型中共享相同的注册和访问模式。
### 统一设备访问接口
RT-Thread 设备模型的核心价值在于**统一接口**。无论是串口、SPI Flash 还是传感器，上层应用都使用相同的 API 进行访问：
#### 设备查找：rt_device_find()
![简单 I/O 设备使用序列图](https://www.rt-thread.org/document/site/rt-thread-version/rt-thread-standard/programming-manual/device/figures/io-call.png)

```c
/**
 * 通过设备名称查找设备对象
 * @param name  设备名称（如 "pin", "uart1", "spi10"）
 * @return      设备句柄（rt_device_t），查找失败返回 RT_NULL
 */
rt_device_t rt_device_find(const char *name);
```

**使用示例：**

```c
rt_device_t dev;

/* 查找 PIN 设备 */
dev = rt_device_find("pin");
if (dev == RT_NULL)
{
    rt_kprintf("PIN device not found!\n");
    return -RT_ERROR;
}

/* 查找 UART 设备 */
dev = rt_device_find("uart1");
```

#### 设备打开/关闭：rt_device_open() / rt_device_close()

```c
/**
 * 打开设备（增加引用计数）
 * @param dev   设备句柄
 * @param oflag 打开标志（RT_DEVICE_FLAG_RDONLY / WRONLY / RDWR）
 * @return      RT_EOK 成功，其他值失败
 */
rt_err_t rt_device_open(rt_device_t dev, rt_uint16_t oflag);

/**
 * 关闭设备（减少引用计数）
 * @param dev   设备句柄
 * @return      RT_EOK 成功，其他值失败
 */
rt_err_t rt_device_close(rt_device_t dev);
```

**使用示例：**

```c
rt_device_t uart_dev = rt_device_find("uart1");

/* 以读写模式打开串口 */
rt_device_open(uart_dev, RT_DEVICE_FLAG_RDWR);

/* ... 使用设备 ... */

/* 使用完毕后关闭 */
rt_device_close(uart_dev);
```

#### 数据读写：rt_device_read() / rt_device_write()

```c
/**
 * 从设备读取数据
 * @param dev    设备句柄
 * @param pos    读取位置（块设备使用，字符设备通常为 0）
 * @param buffer 数据缓冲区
 * @param size   读取大小
 * @return       实际读取的字节数
 */
rt_size_t rt_device_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);

/**
 * 向设备写入数据
 * @param dev    设备句柄
 * @param pos    写入位置
 * @param buffer 数据缓冲区
 * @param size   写入大小
 * @return       实际写入的字节数
 */
rt_size_t rt_device_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
```

#### 设备控制：rt_device_control()

```c
/**
 * 向设备发送控制命令
 * @param dev  设备句柄
 * @param cmd  控制命令
 * @param args 命令参数
 * @return     RT_EOK 成功，其他值失败
 */
rt_err_t rt_device_control(rt_device_t dev, int cmd, void *args);
```

这是设备模型中**最灵活**的接口。不同设备类型有不同的控制命令，例如：

| 设备类型 | 控制命令示例 | 说明 |
|---------|------------|------|
| UART | `RT_DEVICE_CTRL_CONFIG` | 配置波特率、数据位等 |
| PIN | `RT_DEVICE_CTRL_CONFIG` | 配置引脚模式 |
| RTC | `RT_DEVICE_CTRL_RTC_GET_TIME` | 获取当前时间 |
| WDT | `RT_DEVICE_CTRL_WDT_KEEPALIVE` | 喂狗 |
| PWM | `RT_DEVICE_CTRL_PWM_SET_PERIOD` | 设置 PWM 周期 |
### PIN 设备框架详解
PIN 设备是 RT-Thread 中最基础的设备类型，负责 GPIO 的管理和操作。它使用的是 **PIN 设备框架专属的 API**，而非通用的 `rt_device_read/write`：
#### PIN 设备 API

```c
/* 设置引脚模式 */
void rt_pin_mode(rt_base_t pin, rt_base_t mode);

/* 设置引脚输出电平 */
void rt_pin_write(rt_base_t pin, rt_base_t value);

/* 读取引脚输入电平 */
int rt_pin_read(rt_base_t pin);

/* 绑定引脚中断回调 */
rt_err_t rt_pin_attach_irq(rt_int32_t pin, rt_uint32_t mode,
                            void (*hdr)(void *args), void *args);

/* 解绑引脚中断回调 */
rt_err_t rt_pin_detach_irq(rt_int32_t pin);

/* 使能/禁止引脚中断 */
rt_err_t rt_pin_irq_enable(rt_base_t pin, rt_uint32_t enabled);
```

#### 引脚模式定义

| 模式常量 | 说明 | 应用场景 |
|---------|------|---------|
| `PIN_MODE_OUTPUT` | 推挽输出 | 控制 LED、蜂鸣器等输出设备 |
| `PIN_MODE_OUTPUT_OD` | 开漏输出 | I2C、单总线等需要外部上拉的场景 |
| `PIN_MODE_INPUT` | 浮空输入 | 读取外部数字信号 |
| `PIN_MODE_INPUT_PULLUP` | 上拉输入 | 按键检测（默认高电平，按下低电平） |
| `PIN_MODE_INPUT_PULLDOWN` | 下拉输入 | 按键检测（默认低电平，按下高电平） |
| `PIN_MODE_OUTPUT_PP` | 推挽输出（别名） | 同 `PIN_MODE_OUTPUT` |
#### 引脚电平定义

| 电平常量 | 说明 |
|---------|------|
| `PIN_LOW` | 低电平（0V） |
| `PIN_HIGH` | 高电平（3.3V） |
#### PIN 设备使用示例

```c
#include <rtdevice.h>
#include <board.h>

/* 使用 board.h 中定义的引脚宏 */
#define LED_PIN    GET_PIN(F, 9)    /* 红色 LED */
#define KEY_PIN    GET_PIN(C, 0)    /* 用户按键 KEY0 */

/* 按键回调函数 */
static void key_callback(void *args)
{
    rt_kprintf("KEY0 pressed!\n");
}

int pin_demo(void)
{
    /* 1. 配置 LED 引脚为输出模式 */
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);

    /* 2. 点亮 LED */
    rt_pin_write(LED_PIN, PIN_LOW);   /* 低电平点亮 */

    /* 3. 配置按键引脚为上拉输入模式 */
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);

    /* 4. 绑定按键中断（下降沿触发） */
    rt_pin_attach_irq(KEY_PIN, PIN_IRQ_MODE_FALLING,
                      key_callback, RT_NULL);

    /* 5. 使能按键中断 */
    rt_pin_irq_enable(KEY_PIN, PIN_IRQ_ENABLE);

    return RT_EOK;
}
```

### UART 设备框架详解
UART（串口）设备是 RT-Thread 中使用最频繁的通信设备之一，FinSH 控制台就是通过 UART 设备实现的。
#### UART 设备配置
在 RT-Thread Settings 中启用 UART 驱动后，还需要在 `board.h` 中配置串口参数：

```c
/* board.h 中的 UART 配置 */
#define BSP_USING_UART1             /* 启用 UART1 */
#define BSP_UART1_TX_PIN    "PA9"   /* UART1 TX 引脚 */
#define BSP_UART1_RX_PIN    "PA10"  /* UART1 RX 引脚 */

#define BSP_USING_UART2             /* 启用 UART2 */
#define BSP_UART2_TX_PIN    "PD5"   /* UART2 TX 引脚 */
#define BSP_UART2_RX_PIN    "PD6"   /* UART2 RX 引脚 */
```

#### UART 设备访问模式
UART 设备支持三种访问模式：

| 模式 | 说明 | 适用场景 |
|------|------|---------|
| 轮询模式 | CPU 主动查询数据状态 | 简单测试、非实时场景 |
| 中断模式 | 数据到达时触发中断通知 CPU | 一般数据收发 |
| DMA 模式 | DMA 控制器自动搬运数据，零 CPU 开销 | 大数据量收发 |
#### UART 设备使用示例

```c
#include <rtdevice.h>

/* 串口接收缓冲区 */
static rt_uint8_t uart_rx_buffer[256];

/* 串口接收回调 */
static rt_err_t uart_rx_callback(rt_device_t dev, rt_size_t size)
{
    /* 读取接收到的数据 */
    rt_device_read(dev, 0, uart_rx_buffer, size);
    rt_kprintf("Received %d bytes: %s\n", size, uart_rx_buffer);
    return RT_EOK;
}

int uart_demo(void)
{
    rt_device_t uart_dev;
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;

    /* 1. 查找串口设备 */
    uart_dev = rt_device_find("uart2");
    if (uart_dev == RT_NULL)
    {
        rt_kprintf("UART2 not found!\n");
        return -RT_ERROR;
    }

    /* 2. 配置串口参数 */
    config.baud_rate = BAUD_RATE_115200;
    config.data_bits = DATA_BITS_8;
    config.stop_bits = STOP_BITS_1;
    config.parity = PARITY_NONE;
    rt_device_control(uart_dev, RT_DEVICE_CTRL_CONFIG, &config);

    /* 3. 打开串口 */
    rt_device_open(uart_dev, RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_INT_RX);

    /* 4. 设置接收回调 */
    rt_device_set_rx_indicate(uart_dev, uart_rx_callback);

    /* 5. 发送数据 */
    rt_device_write(uart_dev, 0, "Hello UART!\r\n", 13);

    return RT_EOK;
}
```

### 设备模型的设计思想

| 设计思想      | 实现方式                                            | 实际价值                 |
| --------- | ----------------------------------------------- | -------------------- |
| **统一接口**  | 所有设备通过 `find/open/read/write/control` 五个 API 访问 | 降低学习成本，一套 API 操作所有外设 |
| **分层架构**  | 应用层 → 框架层 → 驱动层 → 硬件层                           | 各层职责清晰，修改一层不影响其他层    |
| **面向对象**  | `rt_device` 基类 + 子类继承扩展                         | 代码复用，扩展方便            |
| **引用计数**  | `ref_count` 字段                                  | 多线程安全，防止设备被意外关闭      |
| **自动初始化** | `INIT_DEVICE_EXPORT` 等宏                         | 驱动自动注册，无需手动调用        |

