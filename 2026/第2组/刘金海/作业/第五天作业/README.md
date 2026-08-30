# AHT10 采集 + MQTT 上传 + 文件存储 + 云端控制

## 功能概述

- 采集 AHT10 温湿度（I2C3），通过 `kawaii-mqtt` 发布到 EMQX  
- 同步追加写入 `/fal/Data.txt`，每 10 次调用 `fsync` 落盘  
- 订阅下行主题 `rtt-sub666`，接收 JSON 指令（`{"msg":"led on/off"}`）控制 PF12 LED  
- 导出 MSH 命令 `aht10_thread` 启动采集线程

---

## 依赖软件包/组件

| 组件 | 用途 |
|------|------|
| `aht10` | 传感器驱动（需开启 Sensor 框架） |
| `kawaii-mqtt` | MQTT 客户端（Broker 地址、端口、Client ID 需配置） |
| `cJSON` | 解析下行 JSON |
| DFS + elm-fat + FAL + SFUD | 文件系统与 Flash 分区挂载 |
| `rt_vsnprintf_full` | 支持 `%.2f` 浮点打印 |

---

## 硬件引脚

- AHT10：I2C3 总线  
- WiFi 片选：PF10（板级初始化拉高，避免 SPI 总线冲突）  
- LED：PF12（低电平点亮）

---

## 配置要点

1. **MQTT 参数**  
   在 `kawaii-mqtt` 的 Kconfig 中设置 Host、Port、Client ID（需唯一），发布主题由 `KAWAII_MQTT_PUBTOPIC` 宏定义（在 `mqttclient.h` 或配置中修改）。

2. **文件路径**  
   代码使用 `/fal/Data.txt`，确保 `fal` 分区已挂载。若需改用 `font` 分区，修改路径并确保该分区已创建并挂载。

3. **下行订阅**  
   回调函数 `msg_to_led` 注册到主题 `"rtt-sub666"`（需在 MQTT 初始化时调用 `mqtt_subscribe` 绑定）。

## 注意事项

- **Client ID 唯一**：多个设备使用相同 ID 会互踢。  
- **文件系统挂载**：确保 `/fal` 或 `/font` 已正确挂载，否则 `open` 失败。  
- **SPI 总线冲突**：若 W25Q64 探测失败，可增大初始化延时或调整片选时序。  
- **JSON 异常**：下行非 JSON 或缺失 `msg` 字段时，代码无处理，建议增加判空。
