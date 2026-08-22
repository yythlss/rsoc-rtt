# RT-Thread 源码目录结构分析

> 分析对象：`D:\stm32\RT-ThreadStudio\workspace\day\rt-thread\`
> 版本依据：工程 `rtconfig.h` 中 `RT_VER_NUM 0x40101` → **RT-Thread 4.1.1**
> 用途：理清 RT-Thread 内核源码树每个目录/文件是干什么的，以及它们如何配合构成一个可运行的操作系统。

---

## 1. 源码树总览

`rt-thread/` 目录是一份完整的 RT-Thread **内核源码树**（不是工程、不是 BSP），它和工程根目录的 `applications/`、`drivers/`、`libraries/` 分开存放。RT-Thread 官方将源码树分成 6 大目录：

```
rt-thread/
├── components/   组件与中间件        （1434 个文件）
├── examples/     示例代码            （199 个文件）
├── include/      内核头文件          （7 个文件）
├── libcpu/       芯片/CPU 移植代码   （501 个文件）
├── src/          内核核心源码        （19 个文件）
├── tools/        构建工具脚本        （127 个文件）
│
├── Kconfig       内核配置菜单入口（menuconfig 用它生成配置树）
├── SConscript    内核构建脚本入口
├── README.md / README_zh.md   官方说明文档
├── LICENSE / AUTHORS / .gitignore / .gitattributes
```

**一个要点**：RT-Thread 官方仓库里还有 `BSP/`（板级支持包）和 `documentation/`，但这份源码树里**没有**。因为在 RT-Thread Studio 工程里，板级部分（`board.c`、`drv_*.c`、HAL 库、链接脚本）被抽到了工程根目录的 `drivers/`、`libraries/`、`linkscripts/`，不再放在 `rt-thread/` 里。

分层关系一句话：**src + include 是内核，libcpu 是内核与硬件的"翻译层"，components 是内核之上的可选模块，tools 负责把这些代码编译/配置起来。**

---

## 2. 内核核心：`src/` + `include/`

### 2.1 `src/` —— 内核源码（19 个文件）

这是 RT-Thread 操作系统的"心脏"，全部与芯片无关、纯 C 实现：

| 文件 | 作用 |
| --- | --- |
| `scheduler.c` | **调度器**。就绪队列、优先级调度、时间片轮转；`rt_schedule()` 是线程切换的核心入口 |
| `thread.c` | **线程管理**。线程的创建/删除/启动/挂起/恢复/延时，`rt_thread_*` 系列 API |
| `timer.c` | **定时器**。软件定时器与硬件定时器接口，`rt_timer_*`；配合 `RT_USING_TIMER_SOFT` 可走定时器线程 |
| `ipc.c` | **线程间通信（IPC）**。信号量 `rt_sem_*`、互斥锁 `rt_mutex_*`、事件 `rt_event_*`、邮箱 `rt_mb_*`、消息队列 `rt_mq_*` 全部在这一个文件里 |
| `mem.c` | 小内存管理算法（first-fit/小内存堆），用于小 RAM 的 MCU |
| `memheap.c` | 多内存堆管理（可管理多块不连续内存） |
| `mempool.c` | 内存池（固定大小块的快速分配） |
| `slab.c` | slab 内存分配算法 |
| `object.c` | **内核对象系统**。所有内核对象（线程/信号量/定时器…）的初始化、注册、查找；对象在内部以链表管理 |
| `device.c` | **设备框架**。`rt_device_*` 设备注册、打开、读、写等，是驱动模型的基础 |
| `kservice.c` | 内核基础服务：`rt_kprintf` 打印、字符串/内存操作、小工具函数 |
| `clock.c` | 系统时钟节拍（tick）维护，`rt_tick_get()` |
| `cpu.c` | CPU 相关封装：开关中断、CPU 信息 |
| `irq.c` | **中断管理**。中断安装 `rt_hw_interrupt_install`、中断进入/退出计数 |
| `idle.c` | **空闲线程**（优先级最低的线程），创建空闲线程并执行空闲钩子 |
| `signal.c` | 信号机制（面向 POSIX 的信号处理） |
| `components.c` | **组件自动初始化**。按顺序调用 `INIT_BOARD_EXPORT` / `INIT_APP_EXPORT` 等宏注册的初始化函数（`rt_components_init`） |
| `Kconfig` | 内核部分的配置菜单定义 |
| `SConscript` | 内核源码的 SCons 构建脚本 |

> 记忆方法：**调度 + 线程 + 定时器 + IPC + 内存 + 对象/设备 + 中断 + 空闲线程**，这就是一个 RTOS 内核的全部骨架。

### 2.2 `include/` —— 内核头文件（7 个）

| 文件 | 作用 |
| --- | --- |
| `rtthread.h` | **总头文件**。应用和驱动只需 `#include <rtthread.h>`，它会串起其余头文件 |
| `rtdef.h` | **核心定义**。基础类型、内核对象结构体、通用宏（如 `RT_THREAD_PRIORITY_MAX`、`RT_NULL`、`rt_list_t` 等） |
| `rthw.h` | **硬件抽象接口**。声明所有必须由移植层实现的函数：`rt_hw_interrupt_disable/enable`、`rt_hw_context_switch`、`rt_hw_tick_init` 等 |
| `rtservice.h` | 内核容器/链表工具：双向链表 `rt_list_*`、`rt_slist_*`、`container_of` 等 |
| `rtdbg.h` | 调试宏：`LOG_D/I/W/E`，配合 `DBG_TAG`、`DBG_LVL` 使用（drv_*.c 顶部常见） |
| `rtdebug.h` | 断言与调试辅助（`RT_ASSERT` 等） |
| `rtm.h` | 内核模块（module）相关声明（动态加载模块场景用） |

> 关键理解：**`src/` 实现内核，`include/` 定义接口**。`rthw.h` 里的函数声明就是给 `libcpu/` 移植层下的"订单"——内核只管调用，具体怎么操作硬件由移植层完成。

---

## 3. 移植层：`libcpu/`

`libcpu/`（CPU 移植层）按**芯片架构**分目录，为每种架构实现上下文切换和中断入口：

```
libcpu/
├── arm/      ARM 体系（含 cortex-m0/m3/m4/m7、cortex-a、arm926 等）
├── risc-v/   RISC-V 体系
├── mips/     MIPS
├── ia32/     x86
├── aarch64/  64 位 ARM
├── arc/ c-sky/ rx/ ppc/ avr32/ ...  其它小众/历史架构
├── Kconfig   架构选择配置
└── SConscript
```

本项目用的是 STM32F407ZG（Cortex-M4），对应 **`libcpu/arm/cortex-m4/`**：

| 文件 | 作用 |
| --- | --- |
| `cpuport.c` | Cortex-M4 的 CPU 移植实现：SysTick 定时器、PendSV 异常、`rt_hw_interrupt_*`、启动第一个线程 |
| `context_gcc.S` | GCC 汇编写的**线程上下文切换**（`rt_hw_context_switch` 等），保存/恢复寄存器 |
| `context_iar.S` | IAR 编译器版本（同逻辑、IAR 汇编语法） |
| `context_rvds.S` | MDK/Keil（ARMCC）版本 |
| `SConscript` | 构建脚本 |

同级还有 `libcpu/arm/common/`，放 ARM 通用的辅助代码：

| 文件 | 作用 |
| --- | --- |
| `backtrace.c` | 栈回溯（调试死机/异常定位用） |
| `div0.c` / `divsi3.S` | 软除法支持 |
| `showmem.c` | 内存查看工具 |

> 理解：**上下文切换是 RTOS 的"命门"**，必须贴近芯片写汇编，所以它放在移植层而不是内核层。换一块不同的 CPU，只需要换 `libcpu/` 里对应的目录。

---

## 4. 组件层：`components/`

组件运行在内核之上，是可选、可裁剪的（由 `rtconfig.h` 里的 `RT_USING_*` 宏决定编不编译）。

```
components/
├── finsh/      命令行组件（FinSH / MSH）
├── dfs/        虚拟文件系统框架（VFS）
├── drivers/    设备驱动框架（串口/GPIO/SPI/I2C…的设备抽象层）
├── libc/       C 库适配层（newlib 等）
├── net/        网络框架（lwIP 协议栈、netdev、socket 抽象层 SAL、AT）
├── fal/        Flash 抽象层（分区表管理）
├── utilities/  工具组件（日志 ulog、单元测试 utest、rt-link、ymodem 等）
├── lwp/        轻量进程（用户态进程支持）
├── vmm/        与 Linux 共存的虚拟内存/双系统
├── vbus/       内核间虚拟总线
├── legacy/     兼容旧接口的遗留代码
├── Kconfig / SConscript
```

### 4.1 `finsh/` —— 命令行（本项目已启用）

`RT_USING_FINSH` + `FINSH_USING_MSH` 在 `rtconfig.h` 里已打开，对应串口上那个 `msh>` 提示符。

| 文件 | 作用 |
| --- | --- |
| `shell.c` | FinSH 传统 shell（逐行命令解释） |
| `msh.c` | **MSH（模块 shell）**，本项目用的就是它，支持 `ps`/`list_thread`/`free` 等命令 |
| `cmd.c` | 内建命令注册表 |
| `msh_parse.c` | MSH 命令参数解析 |

### 4.2 `dfs/` —— 文件系统（本项目未启用）

```
dfs/
├── src/              VFS 核心（文件/目录/挂载管理）
├── filesystems/      具体文件系统实现：
│   ├── devfs/         设备文件系统（/dev）
│   ├── elmfat/        FAT 文件系统（SD 卡/U 盘）
│   ├── ramfs/         内存文件系统
│   ├── romfs/         只读内存文件系统
│   ├── nfs/           NFS 网络文件系统
│   └── skeleton/      写新文件系统的模板
└── include/           头文件
```

### 4.3 `drivers/` —— 设备驱动框架（部分启用）

这是"设备框架层"，只定义**抽象接口**，具体外设实现由工程里 `drivers/drv_*.c`（板级驱动）完成。开发时 `#include <rtdevice.h>`（`include/drivers/` 的总入口）即可使用全部框架 API。

> ⚠️ 注意：框架源码的**存放位置 ≠ 框架名称**。例如 `pin/` 目录是空的，GPIO 引脚框架源码实际在 `misc/pin.c`（头文件 `include/drivers/pin.h`）。判断一个框架看 `include/drivers/` 里的头文件即可。

**通信类**

| 目录 | 关键文件 | 作用 |
| --- | --- | --- |
| `serial/` | `serial.c`（V1）、`serial_v2.c`（V2） | 串口设备框架，二选一（本项目 `RT_USING_SERIAL_V1` 已开，`uart1` 由 `drv_usart.c` 实现） |
| `spi/` | `spi_core.c` 总线核心、`spi_dev.c` 设备接口、`spi-bit-ops.c` 软件模拟、`qspi_core.c` QSPI、`spi_flash_sfud.c` 万能 SPI Flash、`spi_msd.c` SPI 版 SD 卡、`enc28j60.c` / `spi_wifi_rw009.c` 驱动示例 | SPI 框架 + 周边适配 |
| `i2c/` | `i2c_core.c` + `i2c_dev.c` + `i2c-bit-ops.c` | I2C 框架（本项目用软件 I2C：`drv_soft_i2c.c`） |
| `can/` | `can.c` | CAN 框架（`drv_can.c` 对应） |
| `usb/` | `usbdevice/`：cdc_vcom(虚拟串口)/hid/mstorage(U盘)/rndis(网卡)/winusb/audio_mic/audio_speaker/ecm；`usbhost/`：hub/udisk/ukbd/umouse/mass/adk/hid | 完整 USB 设备 + 主机协议栈 |

**采集 / 控制 / 管理类**

| 目录 | 关键文件 | 作用 |
| --- | --- | --- |
| `rtc/` | `rtc.c`、`alarm.c`（闹钟回调）、`soft_rtc.c`（软件 RTC） | 实时时钟框架 |
| `watchdog/` | `watchdog.c` | 看门狗框架，应用 `rt_device_find("wdt")` |
| `sensors/` | `sensor.c` + `sensor_cmd.c`（msh 命令 `sensor`） | 传感器统一框架（温湿度/光照/加速度…） |
| `audio/` | `audio.c` + `audio_pipe.c` | 声卡设备框架 + 音频数据管道 |
| `touch/` | `touch.c` | 触摸屏框架 |
| `pm/` | `pm.c` + `lptimer.c` | 电源管理（休眠/唤醒/功耗档位）+ 低功耗定时器（`drv_pm.c` 对应） |

**`misc/` —— 杂项框架（独立框架，源码集中于此）**

| 文件 | 框架 | 头文件 |
| --- | --- | --- |
| `pin.c` | GPIO 引脚（`rt_pin_write/read/mode`） | `pin.h` |
| `adc.c` / `dac.c` | AD / DA 转换 | `adc.h` / `dac.h` |
| `rt_drv_pwm.c` | PWM 输出 | `rt_drv_pwm.h` |
| `pulse_encoder.c` | 脉冲编码器（正交解码/测速） | `pulse_encoder.h` |
| `rt_inputcapture.c` | 输入捕获（脉宽/频率测量） | `rt_inputcapture.h` |

**`ipc/` —— 内核内通信工具（给驱动内部用）**

`ringbuffer.c`（环形缓冲）、`ringblk_buf.c`（环形块缓冲）、`dataqueue.c`（数据队列）、`workqueue.c`（工作队列，中断下半部）、`completion.c`（完成量）、`pipe.c`（管道）、`waitqueue.c`（等待队列）。典型用法：**中断收数据 → 丢进 ringbuffer → 唤醒读线程**。

**时间 / 加密 / 存储抽象**

| 目录 | 作用 |
| --- | --- |
| `hwtimer/` | 硬件定时器框架（`drv_hwtimer.c` 对应） |
| `cputime/` | CPU 计时（`cputime_cortexm.c` 基于 SysTick/DWT） |
| `hwcrypto/` | 硬件加密框架：`hw_crc`/`hw_hash`/`hw_rng`/`hw_symmetric`/`hw_bignum`/`hw_gcm` |
| `mtd/` | `mtd_nor.c`（NOR Flash）/ `mtd_nand.c`（NAND）存储抽象 |
| `phy/` | 以太网物理层抽象（`phy.c` + `phy_mdio.h`），LAN8720 等 |

**`wlan/` —— WiFi 框架**

```
wlan/
├── wlan_dev.c / wlan_dev.h         ① 设备抽象层（最底层）
├── wlan_mgnt.c / wlan_mgnt.h       ② 管理 API 层（用户可见接口）
├── wlan_cfg.c / wlan_cfg.h         ③ 配置持久化（SSID/密码保存）
├── wlan_prot.c / wlan_prot.h       ④ 连接协议抽象（WPA/WPA2 等）
├── wlan_lwip.c                     ⑤ 与 lwIP 对接（网卡注册/DHCP/IP 配置）
├── wlan_workqueue.c / .h           ⑥ 工作队列（异步处理扫描/连接）
├── wlan_cmd.c                      ⑦ 命令封装（AT 型模组用）
└── SConscript
```

分层模型（从下到上）：

```
 用户代码 / msh 命令（wifi join / wifi scan）
        │  rt_wlan_connect("ssid","pwd") / rt_wlan_scan() ...
        ▼
② 管理层 wlan_mgnt.c   高层 API + 事件回调（连上/断开/扫描完成）
        │  内部派发到设备
        ▼
① 设备层 wlan_dev.c    struct rt_wlan_dev_ops 操作表 + 设备注册（wlan0）
        │
        ▼
   具体 WiFi 驱动（不在本目录！在 BSP 或软件包：rw007/esp8266/esp32/ap6212…）
        │  实现 wlan_init/scan/join/softap/send/recv...
        ▼
   WiFi 芯片 / 模组硬件
```

关键接口（源码实锤）：
- 驱动要实现：`wlan_dev.h` 里的 `struct rt_wlan_dev_ops`（20+ 函数指针：`wlan_init`/`wlan_scan`/`wlan_join`/`wlan_softap`/`wlan_send`/`wlan_recv`/`wlan_get_rssi`…），再 `rt_wlan_dev_register(&dev, "wlan0", &ops, ...)` 注册。
- 用户能调：`wlan_mgnt.h` 里的 `rt_wlan_connect(ssid,pwd)`/`rt_wlan_disconnect()`/`rt_wlan_scan()`/`rt_wlan_start_ap()`/`rt_wlan_is_ready()`/`rt_wlan_register_event_handler()`/`rt_wlan_get_rssi()`。

要点：
- `wlan_lwip.c` 是 WiFi 与 TCP/IP 的桥：把 wlan 设备注册成 lwIP 网卡（netdev）、处理 DHCP。**没它，WiFi 连上了也上不了网**。
- `wlan_cfg.c` 支持记住上次连接并自动重连（`rt_wlan_config_autoreconnect`）。
- 本项目 `rtconfig.h` 里 Wi-Fi 段为空（`RT_USING_WLAN` 未开），这套框架**当前未参与编译**；联网时在 Settings 里勾选 WLAN 框架 + 对应 WiFi 软件包即可。

### 4.4 `net/` —— 网络框架（本项目未启用）

```
net/
├── lwip/            lwIP 协议栈本体
├── netdev/          网络设备抽象（网卡管理）
├── sal/             Socket 抽象层（统一 BSD socket 接口）
├── at/              AT 命令框架（AT 模组上网）
├── lwip-dhcpd/ lwip-nat/  附加功能
```

### 4.5 `utilities/` 及其它

| 子目录 | 作用 |
| --- | --- |
| `utilities/` | `ulog/`（日志库）、`utest/`（单元测试框架）、`rt-link/`（RT-Thread 设备间通信）、`ymodem/` `zmodem/`（文件传输）、`var_export/` |
| `libc/` | 把 C 标准库（newlib/picolibc 等）接到 RT-Thread 上 |
| `fal/` | Flash 抽象层，分区表管理（常见于离线/OTA 项目） |
| `lwp/` | 用户态进程（复杂项目才用） |
| `vmm/` / `vbus/` | 双系统 / 虚拟总线（高级场景） |

---

## 5. 其余目录 + 整体关系

### 5.1 `examples/` —— 示例代码

按功能组织：`file`（文件系统示例）、`network`（网络示例）、`libc`、`pm`（电源管理）、`ulog`、`utest`、`ymodem`、`rt-link`、`var_export`、`test`。纯参考，编译时通常不参与。

### 5.2 `tools/` —— 构建工具脚本

RT-Thread 用 **SCons** 构建，这套脚本就是构建系统的"引擎"：

| 文件 | 作用 |
| --- | --- |
| `building.py` | 核心：递归收集源码、生成编译目标 |
| `gcc.py` / `keil.py` / `iar.py` / `makefile.py` / `vs.py` | 适配各编译器/IDE 的构建后端 |
| `menuconfig.py` / `kconfiglib.py` / `genconf.py` | 图形化配置（menuconfig）与 Kconfig 解析 |
| `rt_studio.py` | **RT-Thread Studio 专用构建后端**（本项目构建实际走它） |
| `cmake.py` / `eclipse.py` / `mkromfs.py` / `package.py` | 其它辅助能力 |

### 5.3 整体关系图

```
        ┌──────────────────────────────────────────┐
        │  应用层：applications/main.c              │  ← 你写的代码
        │  rt_thread_create / rt_sem_take / ...     │
        └──────────────────────────────────────────┘
                              │ 调用 API
        ┌──────────────────────────────────────────┐
        │  组件层：components/（finsh/dfs/drivers/…）│  ← 可选，按 RT_USING_* 裁剪
        └──────────────────────────────────────────┘
                              │
        ┌──────────────────────────────────────────┐
        │  内核：src/（实现）+ include/（接口）      │  ← 线程/调度/IPC/内存/定时器/设备
        └──────────────────────────────────────────┘
                              │ 调用 rthw.h 声明的硬件接口
        ┌──────────────────────────────────────────┐
        │  移植层：libcpu/arm/cortex-m4/            │  ← 上下文切换汇编 + cpuport.c
        │  （context_gcc.S / cpuport.c）            │
        └──────────────────────────────────────────┘
                              │
                      STM32F407ZG 硬件
```

### 5.4 这套源码和工程根目录的关系

1. 工程根目录的 `Kconfig`（`rtconfig.h` 由它生成）会 `source` 进 `rt-thread/Kconfig`，所以 **menuconfig 里的内核/组件选项来自这套源码树**。
2. 每个组件目录都有 `Kconfig`（定义可选项）和 `SConscript`（定义该组件源码怎么编译）。
3. 构建时，工程根 `SConstruct` → 读 `rtconfig.py`/`.config` → 调 `tools/building.py` 收集源码 → 只编译**被 `RT_USING_*` 勾选**的部分。
4. 所以：**同一个 `rt-thread/` 源码树，配合不同的 `rtconfig.h`，能产出不同"胖瘦"的系统**——这就是"可裁剪"。

---

## 6. 一分钟速查

| 目录 | 一句话 |
| --- | --- |
| `src/` | 内核实现：调度、线程、定时器、IPC、内存、设备、中断 |
| `include/` | 内核接口/类型定义，`rtthread.h` 是总入口 |
| `libcpu/` | 换芯片就换这里；Cortex-M4 看 `arm/cortex-m4/` |
| `components/` | 内核之上的可选模块（命令行/文件系统/网络/驱动框架…） |
| `examples/` | 示例代码，纯参考 |
| `tools/` | SCons 构建系统脚本，一般不用动 |
| `Kconfig` / `SConscript` | 配置菜单 + 构建脚本的入口 |
