# 第五天作业

## 文件说明

### mqtt_sample.c

整合 AHT10 温湿度采集、kawaii-mqtt 上传、文件系统写入、云端 LED 控制的完整程序。

#### 功能

1. **温湿度采集 + MQTT上传**：每5秒采集一次 AHT10 数据，通过 kawaii-mqtt 发布到 `rsoc/zhangsiyuan/device/up`
2. **文件系统写入**：同时将数据追加写入 `/font/Data.txt`，格式为 `Temp: XX.X ; Humi: XX.X ; Count: N`
3. **云端 LED 控制**：订阅 `rsoc/zhangsiyuan/device/down`，收到 `toggle`/`led` 翻转 LED，收到 `on` 点亮，收到 `off` 熄灭
4. **font 分区挂载**：自动创建块设备、格式化并挂载 font 分区到 `/font`

#### 使用方式

方式一：上电自动运行（main.c 中调用 `mqtt_aht10_init()`）
方式二：msh 手动执行 `mqtt_aht10_init`

#### 依赖软件包

- `aht10` — 温湿度传感器驱动
- `kawaii-mqtt` — MQTT 客户端
- `rw007` — WiFi 模块驱动

#### 依赖组件

- DFS + elm-fat + romfs — 文件系统
- FAL + SFUD — Flash 抽象层
- SAL + netdev + lwIP — 网络协议栈

#### MQTT 主题

| 主题 | 方向 | 用途 |
|------|------|------|
| `rsoc/zhangsiyuan/device/up` | 板卡 → 云端 | 发布温湿度数据 |
| `rsoc/zhangsiyuan/device/down` | 云端 → 板卡 | 接收LED控制指令 |
