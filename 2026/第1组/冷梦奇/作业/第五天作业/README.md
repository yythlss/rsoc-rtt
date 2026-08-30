# 【DAY5作业】AHT10 温湿度采集上云、DFS 文件系统落盘与云端控制小灯实战

> **提交人**：冷梦奇（第1组）  
> **日期**：2026-08-27  
> **目标平台**：STM32F407 星火1号（`stm32f407-rtspark`）  
> **工程目录**：`D:\RT-ThreadStudio\workspace\05_aht`  
> **提交物清单**：
> 1. 作业实验报告（`README.md`）
> 2. 综合应用源码（`day5_app.c`）
> 3. Day5 学习笔记（`../../笔记/第五天笔记.md`）

---

## 目录
- [一、 实验背景与任务目标](#一-实验背景与任务目标)
- [二、 系统架构设计与数据流向](#二-系统架构设计与数据流向)
- [三、 AHT10 传感器采集设计](#三-aht10-传感器采集设计)
- [四、 DFS 虚拟文件系统与 FAL 存储全链路深度剖析](#四-dfs-虚拟文件系统与-fal-存储全链路深度剖析)
  - [4.1 DFS 虚拟文件系统分层架构](#41-dfs-虚拟文件系统分层架构)
  - [4.2 FAL 与 SFUD 驱动协同机制](#42-fal-与-sfud-驱动协同机制)
  - [4.3 文件系统完整启动流程](#43-文件系统完整启动流程)
  - [4.4 font 分区挂载与 Data.txt 规范落盘](#44-font-分区挂载与-datatxt-规范落盘)
  - [4.5 实用 FinSH / MSH Shell 存储操作指令集](#45-实用-finsh--msh-shell-存储操作指令集)
- [五、 MQTT 协议与设备上云通信](#五-mqtt-协议与设备上云通信)
  - [5.1 MQTT 通信模型与 EMQX Broker 配置](#51-mqtt-通信模型与-emqx-broker-配置)
  - [5.2 周期性数据上报设计](#52-周期性数据上报设计)
  - [5.3 云端下发指令与板载小灯（PF12）翻转](#53-云端下发指令与板载小灯pf12翻转)
- [六、 核心源码设计与解析 (day5_app.c)](#六-核心源码设计与解析-day5_appc)
- [七、 编译与运行验证](#七-编译与运行验证)
  - [7.1 交叉编译结果](#71-交叉编译结果)
  - [7.2 串口运行日志与测试输出](#72-串口运行日志与测试输出)
  - [7.3 文件落盘与云端控制验证](#73-文件落盘与云端控制验证)
- [八、 实验总结与工程思考](#八-实验总结与工程思考)

---

## 一、 实验背景与任务目标

在物联网（IoT）边缘设备开发中，完整的业务闭环通常包含三个核心维度：**感知（Sensors）**、**通信（Cloud Connectivity）** 与 **持久化存储（Storage & File System）**。

本次实验基于 STM32F407 星火一号开发板与 RT-Thread 实时操作系统，重点完成以下三项任务：
1. **温湿度采集与上云**：利用 `aht10` 软件包在 `i2c3` 总线上周期性采集环境温湿度数据，并通过 `kawaii-mqtt` 客户端将数据实时上传至开源 EMQX MQTT Broker；
2. **文件系统启动与落盘**：深度梳理 DFS 虚拟文件系统、FAL 抽象层与 SFUD 底层驱动的协同启动流程，熟练掌握 FinSH Shell 存储命令。成功将外部 SPI Flash（W25Q64）上的 `font` 分区挂载为 Elm-FatFS 文件系统，并在数据上传的同时，同步将温湿度及上电采集计数值追加写入 `/font/Data.txt`；
3. **云端远程控制**：在 MQTT 订阅回调中解析云端下发控制指令，实现对星火一号板载红色指示灯（`PF12`）的远程翻转控制。

---

## 二、 系统架构设计与数据流向

整个系统在 RT-Thread 操作系统上以微内核组件化方式高效协作，整体架构与数据流向如下图所示：

```text
 ┌────────────────────────────────────────────────────────────────────────┐
 │                        RT-Thread 操作系统内核                          │
 ├──────────────────┬───────────────────────┬─────────────────────────────┤
 │ 1. 传感器感知层  │  2. 网络与云端协同层  │     3. 存储与文件系统层     │
 │  (AHT10 on I2C3) │   (RW007 + MQTT)      │    (DFS + FAL + W25Q64)     │
 └────────┬─────────┴───────────┬───────────┴──────────────┬──────────────┘
          │ 周期采集            │ 发布/订阅                │ 同步追加落盘
          ▼                     ▼                          ▼
 ┌─────────────────┐   ┌─────────────────┐       ┌────────────────────────┐
 │   AHT10 传感器   │   │  EMQX Broker    │       │     /font/Data.txt     │
 │   temp / humi   │   │ (broker.emqx.io)│       │ Temp:XX; Humi:XX; Ct:N │
 └─────────────────┘   └────────┬────────┘       └────────────────────────┘
                                │ 订阅下行指令
                                ▼
                       ┌─────────────────┐
                       │ LED_R 翻转控制  │
                       │ (PF12 Pin GPIO) │
                       └─────────────────┘
```

---

## 三、 AHT10 传感器采集设计

AHT10 是一款高精度、全校准的数字温湿度传感器，采用标准 I2C 接口通信：
1. **硬件连接**：在星火一号板载外设中，AHT10 连接至软件 I2C 总线 `i2c3`（SCL: `PC0`, SDA: `PC1`）；
2. **软件包驱动**：使能 `PKG_USING_AHT10` 软件包后，直接调用标准驱动 API：
   - `aht10_init(AHT10_I2C_BUS_NAME)`：获取设备操作句柄；
   - `aht10_read_temperature(dev)`：读取环境温度（℃）；
   - `aht10_read_humidity(dev)`：读取相对湿度（%RH）；
3. **浮点格式化支持**：使能 `PKG_USING_RT_VSNPRINTF_FULL` 软件包，支持在 `rt_snprintf` 和控制台打印中直接输出 `%.2f` 浮点数值。

---

## 四、 DFS 虚拟文件系统与 FAL 存储全链路深度剖析

### 4.1 DFS 虚拟文件系统分层架构

RT-Thread 虚拟文件系统（DFS, Device File System）为应用程序屏蔽了不同存储介质和底层文件系统格式的差异：

```text
┌──────────────────────────────────────────────────────────────┐
│                1. POSIX 标准接口层 (POSIX API)              │
│       open() / read() / write() / close() / unlink() ...     │
└──────────────────────────────┬───────────────────────────────┘
                               │ 统一 VFS 调度
┌──────────────────────────────▼───────────────────────────────┐
│                2. DFS 虚拟文件系统层 (VFS Layer)              │
│       统一根目录树 '/' 管理、文件描述符表 (fd)、文件系统挂载点   │
└──────────────────────────────┬───────────────────────────────┘
                               │ 适配接口 (dfs_filesystem_ops)
┌──────────────────────────────▼───────────────────────────────┐
│                3. 具体文件系统层 (Specific FS)               │
│          RomFS (只读根系统)  │  Elm-FatFS (FAT32 兼容)       │
└──────────────────────────────┬───────────────────────────────┘
                               │ 块设备读写接口
┌──────────────────────────────▼───────────────────────────────┐
│                4. 块设备层 / FAL 抽象层 (Block Device)       │
│          FAL 逻辑分区 (font / filesystem / easyflash)        │
└──────────────────────────────┬───────────────────────────────┘
                               │ SFUD 统一接口
┌──────────────────────────────▼───────────────────────────────┐
│                5. 底层驱动与物理存储 (Physical Flash)        │
│          W25Q64 Nor Flash (8MB SPI 闪存)                     │
└──────────────────────────────────────────────────────────────┘
```

---

### 4.2 FAL 与 SFUD 驱动协同机制

1. **SFUD（串行 Flash 通用驱动）**：
   - 自动读取 Flash 芯片的 JEDEC ID 和 SFDP 参数表，屏蔽 Winbond、Macronix、GigaDevice 等芯片的读写擦除指令差异；
   - 在星火一号上探测到 `W25Q64`（8MB 容量，扇区大小 4096 字节）。
2. **FAL（Flash 抽象层）**：
   - 将整片物理 Flash 划分为具备明确边界的逻辑分区：
     - `"easyflash"`：512 KB（环境变量存储）
     - `"download"`：1024 KB（OTA 固件下载区）
     - `"wifi_image"`：512 KB（WiFi 固件）
     - `"font"`：3072 KB（字库及数据存储分区）
     - `"filesystem"`：3072 KB（通用文件系统分区）
   - 通过 `fal_blk_device_create("font")` 将 `font` 分区动态注册为名为 `"font"` 的 RT-Thread 标准块设备（`RT_Device_Class_Block`）。

---

### 4.3 文件系统完整启动流程

系统从上电启动到应用程序可执行文件读写的全链路时序如下：

```text
[系统上电启动]
   │
   ▼
1. 板级初始化 (board.c / spi_flash_init.c)
   └── SPI 总线初始化 -> SFUD 探测 W25Q64 注册为 norflash0 设备
   │
   ▼
2. FAL 抽象层初始化 (fal_init())
   └── 加载 FAL_PART_TABLE 分区表，识别 font 与 filesystem 分区
   │
   ▼
3. DFS 核心组件初始化 (dfs_init())
   └── 初始化文件描述符表 (DFS_FD_MAX)、注册 RomFS 和 Elm-FatFS 类型
   │
   ▼
4. 根文件系统挂载 (drv_filesystem.c)
   └── dfs_mount(RT_NULL, "/", "rom", 0, &romfs_root)
       在内存中建立只读根目录树，包含 /fal 与 /font 静态挂载节点
   │
   ▼
5. font 分区块设备创建与 FatFS 挂载 (mount_font_partition())
   ├── fal_blk_device_create("font") -> 注册 "font" 块设备
   └── dfs_mount("font", "/font", "elm", 0, 0)
       ├── 若已格式化 -> 挂载成功!
       └── 若未格式化 -> 调用 dfs_mkfs("elm", "font") 格式化后重新挂载
   │
   ▼
6. 应用层 POSIX 文件读写 (write_data_to_file())
   └── open("/font/Data.txt", O_WRONLY | O_CREAT | O_APPEND, 0666) -> write() -> close()
```

---

### 4.4 font 分区挂载与 Data.txt 规范落盘

1. **根目录节点扩展**：
   在 `libraries/Board_Drivers/drv_filesystem.c` 的 `_romfs_root[]` 表中显式增加 `{ROMFS_DIRENT_DIR, "font", RT_NULL, 0}`，确保只读根文件系统 `romfs` 下预留 `/font` 挂载目录；
2. **挂载保护与自愈**：
   `mount_font_partition()` 函数首先尝试直接挂载，若检测到分区尚未建立 FAT 文件系统，则自动执行 `dfs_mkfs("elm", "font")` 格式化，随后自动挂载，保障了固件的首次开箱可用性；
3. **数据格式规范**：
   每次采集后，严格按照如下格式追加写入：
   ```text
   Temp：25.30 ; Humi：58.20 ; Count： 1
   Temp：25.32 ; Humi：58.18 ; Count： 2
   Temp：25.35 ; Humi：58.15 ; Count： 3
   ```
   其中 `Count` 从上电启动第 1 次采集起精确递增。

---

### 4.5 实用 FinSH / MSH Shell 存储操作指令集

| 指令 | 作用与用法示例 | 说明 |
|---|---|---|
| `fal probe` | 查看系统中所有 Flash 设备与 FAL 分区表 | 确认 `font` 分区起始地址与长度 |
| `fal probe font` | 选中并探测 `font` 分区 | 检查分区健康度 |
| `mkfs -t elm font` | 手动在 `font` 分区上格式化 Elm-FatFS 文件系统 | 首次挂载前格式化（清空数据） |
| `mount font /font elm` | 手动将 `font` 分区挂载到 `/font` 目录 | 挂载文件系统 |
| `ls /` / `ls /font` | 查看目录下的文件及子目录 | 列出文件 |
| `cat /font/Data.txt` | 查看数据落盘文件内容 | 快速查验温湿度落盘记录 |
| `rm /font/Data.txt` | 删除指定的落盘文件 | 清理历史文件 |
| `df` | 查看各挂载文件系统的剩余空间与容量 | 查看磁盘利用率 |

---

## 五、 MQTT 协议与设备上云通信

### 5.1 MQTT 通信模型与 EMQX Broker 配置

- **Broker 地址**：`broker.emqx.io`（TCP 端口：`1883`）；
- **客户端 ID (ClientID)**：`spark_rsoc_day5_lmq`（唯一标识，避免多设备冲突）；
- **上报主题 (Pub Topic)**：`rtt-pub0721`；
- **下行控制主题 (Sub Topic)**：`rtt-sub0721`；
- **服务质量 (QoS)**：`QoS 0`（轻量低开销）。

---

### 5.2 周期性数据上报设计

在独立工作线程 `day5_app` 中，每隔 3 秒执行一次完整的采集-存储-上报循环：
```c
/* 构造标准 JSON 上报报文 */
rt_snprintf(mqtt_payload, sizeof(mqtt_payload),
            "{\"temperature\":%.2f,\"humidity\":%.2f,\"count\":%d}",
            temp, humi, g_collect_count);

/* 发布至 EMQX */
pub_msg.qos = QOS0;
pub_msg.payload = (void *)mqtt_payload;
mqtt_publish(g_mqtt_client, KAWAII_MQTT_PUBTOPIC, &pub_msg);
```

---

### 5.3 云端下发指令与板载小灯（PF12）翻转

1. **下行指令格式约定**：
   - 翻转命令：`toggle` / `TOGGLE` / `flip`
   - 点亮命令：`on` / `1`
   - 熄灭命令：`off` / `0`
2. **控制回调实现**：
   ```c
   static void cloud_message_arrive_callback(void *client, message_data_t *msg)
   {
       char payload_str[128];
       rt_size_t payload_len = msg->message->payloadlen;
       rt_memcpy(payload_str, msg->message->payload, payload_len);
       payload_str[payload_len] = '\0';

       if (rt_strstr(payload_str, "toggle") || rt_strstr(payload_str, "TOGGLE"))
       {
           led_toggle();
       }
       else if (rt_strstr(payload_str, "on") || rt_strcmp(payload_str, "1") == 0)
       {
           rt_pin_write(LED_PIN, PIN_LOW);
       }
       else if (rt_strstr(payload_str, "off") || rt_strcmp(payload_str, "0") == 0)
       {
           rt_pin_write(LED_PIN, PIN_HIGH);
       }
       else
       {
           led_toggle(); // 默认行为
       }
   }
   ```

---

## 六、 核心源码设计与解析 (`day5_app.c`)

完整源码位于 `applications/day5_app.c`，主要包含 5 大功能函数：

1. `mount_font_partition()`：自动完成 FAL 初始化、创建 `font` 块设备、自动格式化及挂载到 `/font`；
2. `write_data_to_file(temp, humi, count)`：POSIX 追加写文件操作，实现指定格式的落盘记录；
3. `cloud_message_arrive_callback()`：云端下行指令解析器，控制 PF12 引脚电平翻转；
4. `day5_task_entry()`：主调度线程，完成网络就绪等待、AHT10 驱动初始化、MQTT 注册连接、周期性采样、上报与落盘循环；
5. `MSH_CMD_EXPORT` 命令集：导出 `day5_start`、`mount_font_partition`、`led_toggle`、`cat_data`、`clear_data` 等命令。

---

## 七、 编译与运行验证

### 7.1 交叉编译结果

在 RT-Thread Studio 环境下通过 SCons 与 ARM GCC 交叉编译器构建工程，**0 错误、0 警告**：

```text
scons: Reading SConscript files ...
Newlib version:2.4.0
scons: done reading SConscript files.
scons: Building targets ...
scons: building associated VariantDir targets: build
CC build\applications\day5_app.o
CC build\applications\main.o
CC build\libraries\Board_Drivers\drv_filesystem.o
LINK rt-thread.elf
arm-none-eabi-objcopy -O binary rt-thread.elf rtthread.bin
arm-none-eabi-size rt-thread.elf
   text	   data	    bss	    dec	    hex	filename
 368508	   2232	  61668	 432408	  69918	rt-thread.elf
scons: done building targets.
```

---

### 7.2 串口运行日志与测试输出

#### 1. 系统开机与组件自动启动
```text
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 28 2026 00:13:18
 2006 - 2022 Copyright by RT-Thread team
lwIP-2.0.3 initialized!
[I/sal.skt] Socket Abstraction Layer initialize success.
[I/SFUD] Find a Winbond flash chip. Size is 8388608 bytes.
[I/SFUD] W25Q64 flash device is initialize success.
[I/SFUD] Probe SPI flash W25Q64 by SPI device spi20 success.
[I/WLAN.dev] wlan init success
[I/WLAN.lwip] eth device init ok name:w0
[I/WLAN.dev] wlan init success
[I/WLAN.lwip] eth device init ok name:w1

rw007  sn: [rw007a8a355defc584afaa303]
rw007 ver: [RW007_2.1.0-c7747420-52]

[D/FAL] (fal_flash_init:49) Flash device |        onchip_flash_128k | addr: 0x08000000 | len: 0x00100000 | blk_size: 0x00020000 |initialized finish.
[D/FAL] (fal_flash_init:49) Flash device |                   W25Q64 | addr: 0x00000000 | len: 0x00800000 | blk_size: 0x00001000 |initialized finish.
[I/FAL] ==================== FAL partition table ====================
[I/FAL] | name       | flash_dev         |   offset   |    length  |
[I/FAL] -------------------------------------------------------------
[I/FAL] | app        | onchip_flash_128k | 0x00000000 | 0x00060000 |
[I/FAL] | param      | onchip_flash_128k | 0x00060000 | 0x000a0000 |
[I/FAL] | easyflash  | W25Q64            | 0x00000000 | 0x00080000 |
[I/FAL] | download   | W25Q64            | 0x00080000 | 0x00100000 |
[I/FAL] | wifi_image | W25Q64            | 0x00180000 | 0x00080000 |
[I/FAL] | font       | W25Q64            | 0x00200000 | 0x00300000 |
[I/FAL] | filesystem | W25Q64            | 0x00500000 | 0x00300000 |
[I/FAL] =============================================================
[I/FAL] RT-Thread Flash Abstraction Layer initialize success.
[I/FAL] The FAL block device (filesystem) created successfully
Day 5 任务已成功启动!

=======================================================
   RT-Thread 夏令营 Day5 综合实战工程 (冷梦奇)
   1. AHT10 温湿度采集并上传 EMQX
   2. DFS + FAL 挂载 font 分区并写入 Data.txt
   3. MQTT 订阅云端指令翻转板载 LED (PF12)
   可用命令: day5_start, mount_font_partition, led_toggle, cat_data, clear_data
=======================================================

[I/day5.app] Day5 综合应用任务启动中，等待系统网络就绪 (约 6 秒)...
[I/FAL] The FAL block device (font) created successfully
[I/day5.app] 成功在 'font' 分区创建块设备: font
[I/day5.app] 文件系统已成功挂载到 '/font' 分区!
[I/day5.app] AHT10 传感器在 i2c3 总线初始化成功!
[I/WLAN.mgnt] wifi connect success ssid:1
[I/WLAN.lwip] Got IP address : 192.168.198.62
[I/day5.app] MQTT Broker 连接成功!
[I/day5.app] 已成功订阅云端指令主题: rtt-sub0721
```

#### 2. 周期采集、落盘与 MQTT 上报日志
```text
[Sensor #20] 温度: 29.23 ℃ , 湿度: 71.41 %
[FS Log] 同步落盘成功: Temp：29.23 ; Humi：71.41 ; Count： 20
[MQTT Pub] 成功上传至主题 [rtt-pub0721]: {"temperature":29.23,"humidity":71.41,"count":20}

[Sensor #21] 温度: 29.21 ℃ , 湿度: 71.46 %
[FS Log] 同步落盘成功: Temp：29.21 ; Humi：71.46 ; Count： 21
[MQTT Pub] 成功上传至主题 [rtt-pub0721]: {"temperature":29.21,"humidity":71.46,"count":21}

[Sensor #22] 温度: 29.20 ℃ , 湿度: 71.50 %
[FS Log] 同步落盘成功: Temp：29.20 ; Humi：71.50 ; Count： 22
[MQTT Pub] 成功上传至主题 [rtt-pub0721]: {"temperature":29.20,"humidity":71.50,"count":22}
```

---

### 7.3 文件落盘与云端控制验证

#### 1. 查看落盘文件内容 (`cat /font/Data.txt`)
```text
msh />cd font
msh /font>cat Data.txt
Temp：28.91 ; Humi：74.10 ; Count： 2
Temp：28.94 ; Humi：74.27 ; Count： 3
Temp：28.95 ; Humi：74.27 ; Count： 4
Temp：28.97 ; Humi：74.28 ; Count： 5
...
Temp：29.19 ; Humi：71.28 ; Count： 13
Temp：29.21 ; Humi：71.37 ; Count： 14
Temp：29.21 ; Humi：71.35 ; Count： 15
Temp：29.21 ; Humi：71.35 ; Count： 16
Temp：29.21 ; Humi：71.30 ; Count： 17
Temp：29.24 ; Humi：71.30 ; Count： 18
Temp：29.22 ; Humi：71.35 ; Count： 19
Temp：29.23 ; Humi：71.41 ; Count： 20
Temp：29.21 ; Humi：71.46 ; Count： 21
```

#### 2. 云端下发控制指令测试 (MQTTX 下发 `{"msg":"toggle"}`)
通过 MQTTX 客户端向主题 `rtt-sub0721` 发送指令 `{"msg":"toggle"}`：

```text
==================================================
[MQTT Cloud] 收到云端下发指令:
  Topic  : rtt-sub0721
  Payload: {
  "msg": "toggle"
}
==================================================
[LED] >> 小灯状态已翻转! 当前状态: 点亮 (电平: 0)

==================================================
[MQTT Cloud] 收到云端下发指令:
  Topic  : rtt-sub0721
  Payload: {
  "msg": "toggle"
}
==================================================
[LED] >> 小灯状态已翻转! 当前状态: 熄灭 (电平: 1)
```

---

## 八、 实验总结与工程思考

1. **打通了感知-存储-上云全链路**：
   通过 AHT10 传感器采集、FAL/DFS 文件系统持久化落盘以及 MQTT 双向通信，系统性掌握了 RT-Thread 物联网终端标准开发模式。
2. **深入理解了存储协议栈分层架构**：
   厘清了 POSIX API $\rightarrow$ DFS 虚拟文件系统 $\rightarrow$ Elm-FatFS 文件系统 $\rightarrow$ FAL 块设备 $\rightarrow$ SFUD 驱动 $\rightarrow$ W25Q64 物理硬件的完整调用链路，对操作系统解耦设计的优雅性有了深刻认识。
3. **掌握了多模块与总线冲突处理经验**：
   深入掌握了 SPI 总线多从机（W25Q64 与 RW007）片选管理、以及 I2C 总线传感器驱动生命周期的管理技巧。

