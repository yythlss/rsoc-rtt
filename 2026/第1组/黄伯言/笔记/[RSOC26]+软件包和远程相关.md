# [RSOC26]+软件包和远程相关

## 一、软件包机制与 ENV 版本匹配

- **软件包 Kconfig 引入机制**：由 ENV 安装目录下的 `package/` 目录引入。
- **软件包索引信息**：记录在 RT-Thread 软件包仓库的 `package.json` 中，向仓库提 PR 即可共建软件包。
- **Studio 的 ENV 版本适配策略**：
  - 内核版本 **≥ 5.1.0** → 使用 **ENV 2.0** 系列
  - 内核版本 **< 5.1.0**（如 4.1.1）→ 必须使用 **ENV 1.5.x** 系列（例如 4.1.1 内核配 ENV 1.5.2，用新 ENV 会报错）
- **软件包下载**：`menuconfig` 勾选软件包后退出保存，ENV 自动从远程拉取软件包源码到工程 `packages/` 目录。

## 二、AHT10 温湿度传感器（软件包方式）

- **使能路径**：硬件 → 传感器 → AHT10 软件包（依赖 Sensor 设备框架 + I2C 总线）。
- **浮点打印问题**：默认 libc 不支持 `%f`，需使能 `printf` 浮点支持软件包。
- **代码流程**：
  1. `aht10_init("i2c3")` 获取设备句柄（需做非空判断）。
  2. 循环调用 `aht10_read_temperature()` / `aht10_read_humidity()`。
  3. 使用 `ulog` 打印，创建线程（栈 1024、优先级 10）周期读取（如 500ms / 2s）。
  4. 使用 `MSH_CMD_EXPORT` 导出到命令行。

## 三、RW007 WiFi 联网

- **使能 RW007 软件包**，按原理图核对引脚：
  - `CS` / `BOOT` / `INT`（PG11 → 编号 107）/ `RESET` 必须与板子一一对应，配错会导致扫不到 WiFi。
- **终端命令**：
  - `wifi scan` → `wifi connect <ssid> <pwd>` → `ping baidu.com` 验证联网。

## 四、MQTT 发布订阅（Káiyī MQTT 软件包）

- **使能配置**：使能软件包及示例（`enable kai_mqtt test`），填写 EMQX 公共服务器 host（1883 端口）。
- **注意事项**：每人 `client id`、发布/订阅主题必须不同，否则公共服务器会互相串数据。
- **启动与测试**：终端输入 `k_mqtt` 启动示例线程，自动连接 + 订阅 + 定时发；电脑端 MQTTX 填写同服务器/主题可互发。
- **示例源码结构**：建客户端实例 → 设地址/端口/id → 设订阅回调 → `connect` → 循环 `publish`（QoS0）。

## 五、DFS 虚拟文件系统 + SPI Flash 持久化

- **使能组件**：
  - DFS 虚拟文件系统 + 旧版兼容
  - ELM-FAT 文件系统
  - 硬件开启 Flash 自动挂载
- **分层抽象链**：`W25Q64` → `SFUD`（串行 Flash 通用驱动库，屏蔽型号差异） → `FAL`（Flash 抽象层/分区表） → `DFS` 挂载到 `/flash`。
- **分区表**：在 `library/drv` 下定义，W25Q64 末尾分区挂载为 `/flash`，扇区大小 4096 字节。
- **应用层 POSIX 接口**：
  - `open("/flash/test.txt", O_RDWR|O_CREAT|O_APPEND)` → `write` → `close`
- **常见坑点与解决**：
  - **SPI 总线冲突**：RW007 与 W25Q64 共用 SPI 总线，需在初始化时把 WiFi 的 CS 引脚（如 PG10）拉高，否则会干扰 Flash 读写，报“找不到 W25Q64”。
  - **挂载失败**：可先使用 `mkfs` 格式化再挂载。
