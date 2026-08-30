# 【DAY5】AHT10 温湿度采集、MQTT 云端发布、DFS 文件持久化与下行反控综合实验报告

* **学员姓名**：杜马辰
* **所在组别**：第 2 组
* **指导导师**：张丙儒
* **目标硬件**：STM32F407 星火号（Spark-1）
* **源码文件**：`main.c`、`aht10_demo.c`、`mqtt_demo.c`
* **MSH 启动命令**：`app_init`
* **提交日期**：2026-08-22

---

## 一、 系统架构与功能概述

本项目基于 RT-Thread 物联网操作系统及其丰富的软件包生态，构建了一个完整的 **IoT 边缘感知与云端协同系统**：

1. **传感器感知层**：通过 I2C3 硬件总线周期性（2s）采集 AHT10 芯片的高精度温湿度数据；
2. **跨线程数据传递**：采用 `rt_mailbox`（邮箱机制）实现传感器采集线程与 MQTT 网络发布线程的高效低开销解耦通信；
3. **边缘持久化存储**：利用 DFS 虚拟文件系统（基于 FAL 分区 + SFUD + W25Q64 SPI Flash + Elm-FatFS），将温湿度历史日志追加写入 `/fal/Data.txt`，并通过每 10 次写入执行一次 `fsync()` 确保 Flash 安全落盘；
4. **MQTT 云端上行发布**：基于 `kawaii-mqtt` 客户端，将传感器数据结构体格式化为 JSON 字符串，以 QoS 0 发布至云端 Broker（`broker.emqx.io`）；
5. **云端下行控制反向响应**：订阅下行控制主题（`rtt/control/dumachen`），接收云端下发的 JSON 控制指令（如 `{"msg":"led on"}` / `{"msg":"led off"}`），利用 `cJSON` 库解析后实时控制板载红色 LED（PF12）。

```text
┌────────────────┐     I2C3     ┌───────────────────┐     Mailbox     ┌───────────────────┐     MQTT     ┌────────────────┐
│  AHT10 传感器   │ ──────────▶  │  AHT10 采集线程    │  ────────────▶  │  MQTT 客户端线程  │  ─────────▶  │   EMQX 云端    │
└────────────────┘              └─────────┬─────────┘                 └─────────▲─────────┘  (TCP/IP)    │  MQTT Broker   │
                                          │ POSIX write                         │                        └───────┬────────┘
                                          ▼                                     │ JSON 反控                      │
                                ┌───────────────────┐                 ┌─────────┴─────────┐                      │
                                │   /fal/Data.txt   │                 │ 板载 LED (PF12)   │ ◀────────────────────┘
                                └───────────────────┘                 └───────────────────┘
```

---

## 二、 依赖组件与硬件引脚分配

### 1. 软件组件与驱动依赖
* **`aht10` 软件包**：温湿度硬件驱动与通信协议解析；
* **`kawaii-mqtt` 软件包**：轻量级、可重连的异步 MQTT 客户端；
* **`cJSON` 软件包**：C 语言超轻量级 JSON 数据解析与生成器；
* **DFS + FAL + SFUD + FatFS**：统一 POSIX 文件系统抽象层与 Flash 驱动。

### 2. 板载硬件引脚映射
* **I2C3 总线**：连接板载 AHT10 传感器；
* **红色 LED (`PIN_LED_R`)**：`GET_PIN(F, 12)`，低电平点亮，受云端下行指令控制；
* **WiFi 芯片片选**：PF10，板级初始化拉高，规避 SPI 总线冲突。

---

## 三、 实板运行日志（FinSH 控制台输出）

```text
msh >app_init
[I/aht10_app] AHT10 sensor initialized successfully on i2c3.
[I/mqtt_app] Connecting to MQTT Broker: broker.emqx.io:1883 (ClientID: dumachen_spark_01)...
[I/mqtt_app] MQTT Connected successfully!
[I/mqtt_app] Subscribed to control topic: rtt/control/dumachen
[I/main] IoT Comprehensive App initialized successfully.

[I/aht10_app] [Sensor Sample] Temp: 26.85 C, Humi: 58.40 %
[I/mqtt_app] Published Sensor Data -> [rtt/sensor/dumachen]: {"client_id":"dumachen_spark_01","temp":26.85,"humi":58.40,"tick":4210}

[I/aht10_app] [Sensor Sample] Temp: 26.90 C, Humi: 58.35 %
[I/mqtt_app] Published Sensor Data -> [rtt/sensor/dumachen]: {"client_id":"dumachen_spark_01","temp":26.90,"humi":58.35,"tick":6210}

/* 云端 MQTT 客户端向 rtt/control/dumachen 主题下发指令 {"msg":"led on"} */
[I/mqtt_app] >> Received MQTT Message on Topic [rtt/control/dumachen]: {"msg":"led on"}
[W/mqtt_app] >> Cloud Command Executed: LED Turned ON <<

/* 云端下发指令 {"msg":"led off"} */
[I/mqtt_app] >> Received MQTT Message on Topic [rtt/control/dumachen]: {"msg":"led off"}
[W/mqtt_app] >> Cloud Command Executed: LED Turned OFF <<

[D/aht10_app] Flushed 10 records to Flash filesystem via fsync.
```

---

## 四、 关键技术细节与工程经验

1. **Client ID 唯一性约束**：
   * MQTT 协议规定 Broker 同一时刻仅允许一个唯一的 Client ID 在线。若多个设备使用相同 Client ID，将导致频繁互踢重连。
2. **Flash 写入寿命与落盘平衡**：
   * Flash 写入操作相对耗时且擦写寿命有限。采用每 10 次写入调用一次 `fsync()` 的策略，既保证了掉电数据丢失上限可控（最多 20s 数据），又极大地降低了 Flash 擦写频率。
3. **动态内存生命周期管控**：
   * 传感器线程通过 `rt_malloc` 分配数据帧并压入邮箱，MQTT 线程从邮箱取出数据并发布完毕后必须调用 `rt_free`，确保高频运行下堆内存零泄漏。
