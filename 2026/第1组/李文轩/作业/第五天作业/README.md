# 第五天作业：文件系统与综合实验（RSOC_App）

## 作业内容

在 RT-Thread 上完成文件系统（W25Q64）与综合应用实验：

1. **文件系统**：W25Q64 挂载为 FatFS（elm_fat），通过 FAL 分区（filesystem）管理，使用 POSIX 接口读写文件
2. **AHT10 采集 + 上传**：读取板载 AHT10 温湿度，通过 kawaii-mqtt 发布到 EMQX 公共 Broker（`rsoc/liwenxuan/device/up`）
3. **数据落盘**：温湿度同时追加写入 `/fal/Data.txt`（格式：`Temp：XX ; Humi：XX ; Count：N`，Count 自上电起计数）
4. **云端指令控灯**：订阅 `rsoc/liwenxuan/device/down`，收到消息翻转板载 LED（PF12）

## 文件说明

| 文件 | 说明 |
| --- | --- |
| `RSOC_App.c` | 综合程序（核心）：AHT10 采集 → MQTT 上传 + Data.txt 落盘 → 订阅指令翻转 LED；msh 命令 `rsoc_app` |
| `AHT10UsePackage.c` | AHT10 软件包示例：`aht10_init("i2c3")` 读取温湿度 + 线程循环打印；msh 命令 `AHT10_Creat_Thread` |
| `main.c` | 入口：LED 引脚初始化为熄灭（闪烁逻辑移除，LED 交由云端指令控制） |
| `spi_flash_init.c` | **BSP 修改**：SFUD 探测 W25Q64 失败时自动**重试 5 次**（对抗 SPI2 共享总线竞争，原为失败即放弃） |
| `fal_spi_flash_sfud_port.c` | **BSP 修改**：FAL 适配的 read/write/erase 由 `assert` 崩溃改为**判空返回错误**（探测失败时不再死机） |

> 另修改 `rt-thread/components/drivers/spi/spi_flash_sfud.c`（不在本目录）：SFUD 探测频率 50MHz → 10MHz（W25Q64 与 RW007 共享 SPI2 总线，降低频率提高探测可靠性）。

## 文件系统架构

```
应用层：open() / read() / write()        ← POSIX 接口
  ↓
DFS（虚拟文件系统）：统一路径 / 挂载
  ↓
FatFS (elm_fat)：FAT 格式
  ↓
BLK 块设备（filesyst）
  ↓
FAL 分区（filesystem：偏移 5MB，大小 3MB）
  ↓
SFUD → SPI2 → W25Q64（8MB SPI NOR Flash）
```

## Settings 关键配置

| 配置项 | 值 | 说明 |
| --- | --- | --- |
| `BSP_USING_FS`（Enable File System） | y | 文件系统组件 |
| `BSP_USING_FLASH_FATFS`（Enable FAL filesystem partition base on W25Q64） | y | FAL + SPI_FLASH + SFUD |
| `RT_USING_DFS_ELMFAT`（Enable elm-chan fatfs） | y | **必须手动勾选**（BSP 开关不会自动带出） |
| `RT_DFS_ELM_MAX_SECTOR_SIZE`（Maximum sector size to be handled.） | **4096** | **关键**：FatFs 默认 512，FAL 块设备扇区为 4096（W25Q64 擦除粒度），不匹配则 mkfs/mount 失败 |

## 使用说明

### 板子端（串口终端 115200）

```
msh > wifi join TP-LINK_0C38 05060606      // 连接 WiFi
msh > rsoc_app                              // 启动综合程序
```

**预期输出**（每 5 秒循环）：

```
[AHT10] init ok on i2c3
[MQTT] connect success: broker.emqx.io
[MQTT] subscribed: rsoc/liwenxuan/device/down
[MQTT] published: {"temperature":27.8,"humidity":49.9,"count":1}
[FS] saved: Temp： 27.8 ; Humi： 49.9 ; Count： 1
```

### 文件系统操作

```
msh > list_device                    // W25Q64 / filesyst 块设备
msh > fal probe                      // 分区表
msh > mkfs -t elm filesyst           // 首次格式化（清空分区，只做一次）
msh > mount filesyst /fal elm        // 挂载（3 个参数：设备 挂载点 fstype）
msh > ls /fal / df /fal / cat /fal/Data.txt
```

### 云端验证（MQTTX）

- 订阅 `rsoc/liwenxuan/device/up` → 实时收板子温湿度 JSON（每 5 秒）
- 发布到 `rsoc/liwenxuan/device/down` → 板载红灯翻转（灭↔亮），串口打印 `[CMD] LED toggled`

## 运行效果（实测日志）

```
[AHT10] init ok on i2c3
[MQTT] connect success: broker.emqx.io
[MQTT] subscribed: rsoc/liwenxuan/device/down
[MQTT] published: {"temperature":27.8,"humidity":49.9,"count":1}
[FS] saved: Temp： 27.8 ; Humi： 49.9 ; Count： 1
...
[CMD] received: {"cmd":"toggle","n":0}
[CMD] LED toggled -> ON
[CMD] received: {"cmd":"toggle","n":1}
[CMD] LED toggled -> OFF
```

**Data.txt 内容**（重启后数据保留，Count 自电起重新计数）：

```
Temp： 27.5 ; Humi： 48.1 ; Count： 25   ← 上次上电数据
Temp： 27.2 ; Humi： 48.9 ; Count： 1    ← 新电
Temp： 27.3 ; Humi： 48.8 ; Count： 2
```

## 注意事项

- 串口终端把全角冒号 `：` 显示成 `???` 是终端编码问题，**文件内保存的是正确全角格式**
- `mount` 命令必须带 3 个参数（设备、挂载点、文件系统类型），否则报用法错误
- kawaii-mqtt 断线重连后**不会自动恢复订阅**：收不到指令时，`reboot` 后重新执行 `rsoc_app`
- W25Q64 与 RW007 共享 SPI2 总线：SFUD 探测偶发失败时驱动已自动重试，若探测失败系统不再崩溃（防崩溃补丁）

## 引用参考

- [RT-Thread 文件系统](https://www.rt-thread.org/document/site/#/rt-thread-version/rt-thread-standard/programming-manual/filesystem/filesystem)
- [EMQX 公共 MQTT 5 Broker](https://www.emqx.com/zh/mqtt/public-mqtt5-broker)
- [SFUD 串行 Flash 通用驱动](https://github.com/armink/SFUD)
