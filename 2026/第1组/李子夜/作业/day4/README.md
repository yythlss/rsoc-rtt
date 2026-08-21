# 【RSOC26】第4天作业 - 环境传感器云上传 + 本地日志 + LED 云端控制

> 提交人：李子夜（第1组）｜Gitee：@CyberSprout｜日期：2026-08-21

---

## 作业概述

| 功能 | 实现方式 | IPC 机制 |
|---|---|---|
| AHT10 温湿度采集 → MQTT 发布到 EMQX | `sensor_thread` + `mqtt_pub_thread` | 互斥量保护全局数据 |
| FAT 文件系统挂载 → Data.txt 日志 | `datalog_thread` 定时写入 | — |
| 云端 MQTT 命令 → LED 控制 | `mqtt_sub_thread` 解析命令 | 消息队列传递消息 |
| 双向 MQTT 通信（发布 + 订阅） | paho-mqtt 客户端 | 事件集标记连接/消息 |

```
线程拓扑：
┌────────────┐     rt_mutex      ┌───────────────┐
│ sens       │ ──g_sensor数据──→ │               │
│ (传感器采样)│                   │               │
└────────────┘                   │               │             EMQX Broker
                                 │ mqttpub       ├──── publish ──→ stm32/demo/sensor
┌────────────┐                   │               │
│ dlog       │                   │               │
│ (Data.txt) │                   │               │── subscribe ← stm32/demo/command
└────────────┘                   │               │
                                 │ mqtsub        │ ← 解析 JSON 控制 LED
                                 │               │
                                 └───────────────┘
```

---

## 环境确认

| 项目 | 值 |
|---|---|
| RT-Thread 版本 | 4.1.1 |
| SOC | STM32F407ZG（星火一号） |
| 编译环境 | RT-Thread Studio（Gons 工具链） |
| I2C | 已启用 (`RT_USING_I2C`, `RT_USING_I2C_BITOPS`) |
| FINSH | 已启用 |
| AHT10 包 | **需手动启用**（见 §环境配置） |
| MQTT (paho-mqtt) | **需手动启用**（见 §环境配置） |
| DFS / elmfat | **需手动启用**（见 §环境配置） |

---

## 文件结构

```
2026/第1组/李子夜/作业/
└── day4/
    ├── main.c              ← 作业主代码（传感器 + MQTT + DFS + LED）
    └── README.md           ← 本文档
```

---

## 环境配置（关键步骤）

### 1. 启用 RT-Thread PMS（包管理服务）

在 `rtconfig.h` 或工程配置中，确保开启：

```c
#define RT_USING_PM
```

### 2. 安装 AHT10 传感器包

在 **RT-Thread Studio** 中：
- 菜单 → **Env** → 配置（或使用 `pkgs --update`）
- 搜索 → 勾选 `tinymodt/aht10`
- 选择 `latest` 版本
- 保存并重新编译

安装后自动添加的头文件和源码：
```
packages/tinymodt-aht10/
  ├── inc/aht10.h
  ├── src/aht10.c
  └── package.json
```

### 3. 安装 paho-mqtt 包

- Env 中搜索 → 勾选 `RT-Thread-packages/paho-mqtt`
- 保存 → 重新编译

安装后的 API：
```c
#include <mqtt_client.h>
struct mqtt_client *mqtt_lease(...);
rt_err_t mqtt_connect(...);
rt_err_t mqtt_publish(...);
rt_err_t mqtt_subscribe(...);
```

### 4. 安装 DFS / elmfat

- Env 中搜索 → 勾选 `RT-Thread-packages/dfs` 和 `elmfat`
- 确保 Board Support Package 中有块设备（sd0/sdmmc）

---

## 核心模块解析

### 4.1 传感器采集（AHT10 + I2C）

```c
//sensor_read_raw() 的完整调用链：
struct rt_i2c_msg msg = {AHT10_ADDR, RT_I2C_WR, 3, {0xAC, 0x33, 0x00}};
rt_i2c_transfer(i2c1, &msg, 1);  // ← 写测量命令
rt_thread_delay(80);             // ← 等待 75ms 转换
// 读 6 字节 → raw[0]为状态, raw[1:3]为温度, raw[4:5]为湿度
```

原始 I2C 实现作为兜底（不依赖 AHT10 软件包也能工作）。如果已安装 `aht10.h` 包，可替换为：

```c
#include <aht10.h>
float t, h;
aht10_read(&t, &h);  // 一行搞定
```

### 4.2 互斥量保护共享数据

```c
// 写线程（sensor_thread）：
rt_mutex_take(sensor_mutex, RT_WAITING_FOREVER);
g_sensor.temperature = t;
g_sensor.count++;
rt_mutex_release(sensor_mutex);

// 读线程（datalog_thread, mqttpub_thread）：
rt_mutex_take(sensor_mutex, RT_WAITING_FOREVER);
// 读取 g_sensor 字段
rt_mutex_release(sensor_mutex);
```

**为什么要用互斥量而不是普通读/写？**

因为 `g_sensor` 是一个包含 float 和 uint32_t 的结构体：
- 32 位 Cortex-M4 上，`float` 读写是原子的（4 字节对齐）
- 但结构体整体赋值**不是原子的**——sensor 更新到一半，另一个线程读出"半新半旧"的数字

互斥量保证：**一个线程读/写 g_sensor 时，其他线程全部等待。**

### 4.3 MQTT 通信架构

```
发布路径：
  sensor_thread → g_sensor → mqtt_pub_thread
                                   │
                                   ├─ sensor_to_json()    造 JSON
                                   └─ mqtt_publish()      写入 Socket → EMQX

订阅路径：
  EMQX → mqtt_sub_thread（netconn 层回调）
               │
               ├─ mqtt_msg_inbox()      包 payload
               ├─ rt_mq_send(mqtt_sub_mq, ...)  消息入队
               └─ rt_event_send(mqtt_evt)        通知处理线程
```

**INFRA 解释**：

paho-mqtt RT-Thread 端口中：
- **INFRA** = MQTT 协议栈基础（connect/ping/pub/sub/disconnect）
- **qos** = Quality of Service 级别：
  - AT_MOST_ONCE (0) = 发出去不计较
  - AT_LEAST_ONCE  (1) = 至少到达一次（带确认, 丢失自动重发）
  - EXACTLY_ONCE   (2) = 恰好一次（Server 去重）
- **handler** = paho-mqtt 的网络 I/O 回调函数，处理 Socket 收发
- **MVP** = Minimum Viable Packet（最小可行数据包），MQTT PUBLISH 包最小 2 字节头 + 主题名 + payload

本代码使用 **AT_LEAST_ONCE (QoS=1)** — 传感器数据可以容忍偶尔重复，但不能丢。

### 4.4 Data.txt 日志

```c
// 文件操作使用 POSIX 风格接口（DFS 封装）
int fd = open("/font/Data.txt", O_WRONLY | O_CREAT | O_APPEND, 0);
// ... O_APPEND：每次写入都在文件末尾
// ... O_CREAT：文件不存在则创建
write(fd, buf, len);
close(fd);
```

**FAT 文件系统重要说明**：

- 需要块设备（SD 卡或 SPI Flash）存储文件
- 如果板子没有 SD 卡插槽，可改为使用 RT-Thread 的 finsh shell 的 `save` 命令或 RAM 盘
- `elmfat` 挂载路径 `/font` → 目录名可自定义

---

## 云端命令协议

### EMQX 侧 WebHook

在 EMQX Dashboard → WebHook 中设置：
- **触发主题**: `stm32/demo/command`
- **请求类型**: POST

或者直接用 **MQTTX** 桌面客户端手动发送：

```json
// 发布到 stm32/demo/command
{"led": "on"}       // LED 亮
{"led": "off"}      // LED 灭
{"led": "toggle"}   // LED 翻转
{"LED": "1"}       // 等价于 {"led": "on"}
{"LED": "0"}       // 等价于 {"led": "off"}
{"LED": "t"}       // 等价于 toggle
```

### 用 mosquitto_pub 工具测试

```bash
# 终端 1：订阅 LED 状态反馈（可选）
mosquitto_sub -h broker.emqx.io -t "stm32/demo/command" -v

#  terminal 2：发送 LED 控制命令
mosquitto_pub -h broker.emqx.io -t "stm32/demo/command" -m '{"led":"on"}'
mosquitto_pub -h broker.emqx.io -t "stm32/demo/command" -m '{"led":"off"}'
mosquitto_pub -h broker.emqx.io -t "stm32/demo/command" -m '{"led":"toggle"}'
```

### 完整测试流程

```bash
# 板端启动后，PC 上依次执行：

# 查看传感器数据
mosquitto_sub -h broker.emqx.io -t "stm32/demo/sensor" -v
# <-- 每 5 秒收到一条 JSON 邮

# 控制 LED
mosquitto_pub -h broker.emqx.io -t "stm32/demo/command" -m '{"led":"on"}'  # LED 亮
mosquitto_pub -h broker.emqx.io -t "stm32/demo/command" -m '{"led":"toggle"}' # 翻转
```

---

## SDK 组件使用说明

### Pin 设备（已完成）

```
rt_pin_mode(PF11, PIN_MODE_OUTPUT);   // 实现在 SDK/generic/components/drivers/misc/pin.c
rt_pin_write(PF11, 0);                // 调用 HAL_GPIO_WritePin()
rt_pin_read(PF11);                    // 读取引脚电平
```

### PWM 设备（课程内容）

RT-Thread PWM 设备提供占空比/周期/脉冲控制：

```c
rt_pwm_set(dev, channel, period, pulse);
// period:  周期（纳秒）- 决定频率
// pulse:   高电平时间（纳秒）- 决定占空比
// 频率 50Hz(period=20ms), 占空比 5%(pulse=1ms) → 舵机转到 0°
```

### ADC 设备（课程内容）

```c
rt_adc_read(dev, channel, &value);
// value: 原始 12-bit ADC 值 (0~4095)
// 实际电压: value * Vref / 4095  (e.g. 3.3V)
```

### FINSH / MSH（已完成）

```
msh />rt_thread_self()     ← 查看当前线程ID
msh />list_thread          ← 列出全部线程
msh />pin_set PF11 0       ← 通过 FINSH 控制引脚（如果注册了命令）
msh />log_trace start      ← 开启跟踪
msh />version              ← 内核版本信息
```

---

## FINSH 自定义命令（finit → msh → finsh 指令）

在 `rtconfig.h` 中开启 `FINSH_USING_MSH` 后，可以用：

```c
#include <finsh_config.h>

#ifdef FINSH_USING_MSH
MSH_CMD_EXPORT(my_func, "description of my command");
#endif
```

注册后，在 MSH 提示符中直接输入命令名即可执行：
```
msh />my_func    ← 直接调用 my_func()
```

本代码中注册的命令：
```
msh />day4_info   ← 打印 Day4 任务状态
```

---

## 编译与运行

### 在 RT-Thread Studio 中

1. 确认 `rtconfig.h` 中已开启：
   ```
   RT_USING_I2C
   RT_USING_I2C_BITOPS
   RT_USING_PIN
   RT_USING_MQTT         ← 需 package 安装后自动添加
   RT_USING_DFS          ← 需 elm-fat 包安装后自动添加
   RT_USING_FINSH
   RT_USING_MSH
   ```

2. 编译 → 下载 → 运行

3. MSH 中没有 ms 断电查看输出，使用串口终端或 RTT 日志

### 串口终端输出样例

```
[main] PB0 LED initialized
[sensor] AHT10 init done, reading every 3000 ms
[mqtt] starting pub thread: broker.emqx.io:1883
[datalog] FAT mounted at /font
[mqtt] connected
[mqtt] subscribe stm32/demo/command: OK
[main] day4 init OK
[sensor] T=25.60C  H=61.20%  cnt=1
[mqtt] --> stm32/demo/sensor  {"temperature":25.60,"humidity":61.20,"count":1,"ts":12847}
[sensor] T=25.55C  H=61.30%  cnt=2
...
```

---

## 常见问题排查

### Q1：MQTT broker 显示连接失败

1. 先确认网络（ping 通 broker.emqx.io）
2. 确认 DNS 可用 → 或改用 IP 地址
3. 查看 paho-mqtt 配置：`RT_MQTT_USING_TLS` / `MICO_TLS_ENABLED`
4. 使用 mosquitto_sub 先在 PC 上验证 broker 可用性

### Q2：DFS 挂载失败

1. 确认块设备驱动已注册（`sd0` 或 `sdmmc`）
2. 无 SD 卡：可用 RAM 盘代替（`ramfs` 伪文件系统）
3. 检查 elmfat 包是否已编译进工程

### Q3：AHT10 持续读取错误

1. 检查 I2C 接线（SCL/SDA 上拉电阻 4.7K）
2. nd 确认 AHT10 I2C 地址为 0x38
3. 初始化时序：上电后需等待 ≥ 100ms（代码已处理）
4. 开启 `RT_USING_I2C_BITOPS` 以支持 AHT10 需要的 bit-banging

### Q4：LED 不响应云端命令

1. 确认订阅主题是 `stm32/demo/command`
2. 确认 mosquitto_pub 使用正确的主题
3. 可用 `rt_kprintf` 打印收到的 payload 定位问题
4. 检查 JSON 解析是否匹配（"ledon" vs "led_on" 等变体）

---

## 测试验证清单

| # | 验证项 | 方法 | 预期结果 |
|---|---|---|---|
| 1 | 传感器能正确读取温湿度 | 串口输出看 T/H 数值在合理范围 | T: 20-35℃, H: 40-80% |
| 2 | MQTT 成功发布 | mosquitto_sub 订阅 | 每 5 秒收到一条 JSON |
| 3 | MQTT 成功订阅并解析命令 | mosquitto_pub 发命令 | LED 状态改变 |
| 4 | Data.txt 能正常写入 | 读取 SD 卡上的文件 | 文件中有日志行 |
| 5 | 互斥量有效 | 长时间运行后 count 连续递增 | 无多线程冲突数据 |
| 6 | 消息队列传递正确 | 在 mqtt 线程中打印收到的 payload | 与发布内容一致 |

---

## 学习要点总结

1. **多线程协作**：4 个线程共享 g_sensor，用互斥量确保原子读写
2. **消息队列解耦**：MQTT publish 线程和 command 处理线程通过消息队列通信，不直接耦合
3. **事件集作为信号**：mqtt_evt 用事件标志通知连接成功和消息到达
4. **DFS 文件操作**：open/write/close 的 POSIX 风格，File Operations completion 后可靠关闭
5. **云端控制闭环**：EMQX subscribe → parse JSON → LED control，完整的 IoT 应用模式

---

## 未上板项（待验证）

- [ ] AHT10 硬件 I2C 通信（需连接 I2C 传感器到 STM32F407）
- [ ] EMQX MQTT 连接（需网络连接+DNS 解析）
- [ ] Data.txt 存储在 FAT 分区（需 SD 卡）
- [ ] 云端 LED 控制实际验证

*,2026-08-21*
