# 第五天作业：AHT10 温湿度 MQTT 上云 + 文件系统落盘 + 云端控灯

> 目标平台：STM32F407 星火号（stm32f407-rtspark）· RT-Thread 4.1.1 · Studio 2.3.1 + ST-LINK（COM7/115200）
> 提交人：霍渝陈（第 4 组）
> 理论部分见《笔记/第五天笔记.md》

## 一、作业要求

1. 使用 **AHT10 软件包**采集温湿度，上传到 **EMQX**（broker.emqx.io）或自建 MQTT 服务器；
2. 梳理 **DFS 文件系统**启动流程，把 **font 分区**挂载上，数据同步写入 `Data.txt`
   （格式 `Temp: XX ; Humi: XX ; Count: N`，N 为自上电起采集次数，逐条追加）；
3. 云端下发指令 → 开发板**小灯翻转**；
4. 熟悉其他软件包（见笔记第六章）。

## 二、实现说明（day5_app.c）

单线程主循环 + 订阅回调，条件编译逐项可关：

| 功能 | 实现 | 对应作业 |
|---|---|---|
| 温湿度采集 | `aht10_init("i2c3")` + `aht10_read_temperature/humidity`，每 5s 一轮，`collect_count++` | 1 |
| MQTT 上行 | kawaii-mqtt 连 `broker.emqx.io:1883`，发布 `rsoc/huoyuchen/device/up`（`Temp: 26.3 ; Humi: 58.1 ; Count: N`） | 1 |
| 文件落盘 | `fal_blk_device_create("font")` → 挂载 font 分区到 `/font` → `open("/font/Data.txt", O_APPEND)` 逐条追加 + `close` 落盘 | 2 |
| 云端控灯 | 订阅 `rsoc/huoyuchen/device/down`，回调 `strstr(payload,"led_toggle")` 匹配 → 翻转 LED_R（PF12） | 3 |

启动：msh 输入 `start_app`（需要上电自动运行则取消 main 底部 `INIT_APP_EXPORT` 注释）。

## 三、Studio 配置手册（照做步骤）

新建基于芯片 STM32F407ZG 工程后，打开 **RT-Thread Settings** 逐项勾选：

1. **硬件**：
   - 使能 SPI2、SPI FLASH（W25Q64）→ 自动带上 SFUD；
   - 使能 FAL（`BSP_USING_FAL`）；
   - 使能 AHT21（swi2c3）→ 自动带上 AHT10 软件包 + Sensor 框架；
   - 使能 RW007（SPI2）WiFi。
2. **组件**：DFS → 文件系统 + elm（FATFS）。
3. **软件包**：kawaii-mqtt、rt_vsnprintf_full（浮点打印，可选）。
4. **FAL 分区表**：工程生成 fal_cfg.h 后，分区表填入以下内容（来自官方 rt-spark BSP，W25Q64 8MB）：

```c
#define FAL_PART_TABLE                                                                                              \
{                                                                                                                   \
    {FAL_PART_MAGIC_WROD,        "app", "onchip_flash_128k",                            0,       384 * 1024, 0}, \
    {FAL_PART_MAGIC_WROD,      "param", "onchip_flash_128k",                   384 * 1024,       640 * 1024, 0}, \
    {FAL_PART_MAGIC_WROD,  "easyflash",    "W25Q64",                                    0,       512 * 1024, 0}, \
    {FAL_PART_MAGIC_WROD,   "download",    "W25Q64",                           512 * 1024,      1024 * 1024, 0}, \
    {FAL_PART_MAGIC_WROD, "wifi_image",    "W25Q64",                  (512 + 1024) * 1024,       512 * 1024, 0}, \
    {FAL_PART_MAGIC_WROD,       "font",    "W25Q64",            (512 + 1024 + 512) * 1024,  3 * 1024 * 1024, 0}, \
    {FAL_PART_MAGIC_WROD, "filesystem",    "W25Q64", (512 + 1024 + 512 + 3 * 1024) * 1024,  3 * 1024 * 1024, 0}, \
}
```

5. **RW007 引脚修正**（官方默认，对照原理图核验）：CS=90（PF10）、BOOT0=29、BOOT1=90、INT=107（PG11）、RST=111（PG15）。
6. 把 `day5_app.c` 拷入 `applications/`，编译下载。

## 四、验证步骤（预期结果）

```text
msh />start_app
[FS] mount font -> /font ok          （首次为 format 后挂载）
[APP] sensor thread started
[APP] waiting for network...
[APP] AHT10 init ok
[MQTT] connected to broker.emqx.io, sub: rsoc/huoyuchen/device/down
[DATA] Temp: 26.3, Humi: 58.1, Count: 1
[FS] saved: Temp: 26.3 ; Humi: 58.1 ; Count: 1
[MQTT UP] Temp: 26.3 ; Humi: 58.1 ; Count: 1
[DATA] Temp: 26.3, Humi: 58.1, Count: 2   ← 每 5s 一条，Count 递增
...
```

1. **文件系统**：`list_device` 可见 `font` 块设备；`ls /font` 可见 `Data.txt`；`cat /font/Data.txt` 输出逐条累积的温湿度记录；
2. **MQTT 上行**：电脑 MQTTX 连接 `broker.emqx.io:1883`，订阅 `rsoc/huoyuchen/device/up`，可见板卡数据；
3. **云端控灯**：MQTTX 向 `rsoc/huoyuchen/device/down` 发 `led_toggle`，板载红灯翻转，串口打印 `[LED] toggle -> ON/OFF`；
4. 断电重上电后 `cat /font/Data.txt`：文件保留，Count 从 1 重新计数（自上电起）。

## 五、踩坑记录

| 问题 | 现象 | 解决 |
|---|---|---|
| 找不到 W25Q64 | `w25q64 not found` | WiFi 与 Flash 共用 SPI2，初始化时把 WiFi 片选 PF10 上拉 |
| 写文件后 cat 是空 | `write` 返回成功但内容丢失 | DFS 写带缓存，**close(fd) 才落盘**；每次 open/write/close |
| `ls /font` BAD file 乱码 | FAT 目录损坏 | `mkfs -t elm font` 格式化（目标是**分区设备名 font**，不是挂载点） |
| RTC 警告刷屏 | `Cannot find a RTC device!` | `ffconf.h` 中 `FF_FS_NORTC` 设为 1 |
| MQTT 被踢下线 | 连上一会儿断开 | 消息循环用 500ms 短超时取队列 + 周期 `mqtt_sleep_ms(500)` 保活心跳 |
| AHT10 读不到 | init 返回 NULL | 确认使能 AHT21(swi2c3) 且总线名传 `"i2c3"` |
| `%f` 输出异常 | 浮点打不出来 | 使能 rt_vsnprintf_full；本程序用整数拼串不依赖 |
| 传感器 undefined reference | 编译报 sensor ops | AHT10 依赖 Sensor 框架，需手动使能 |
