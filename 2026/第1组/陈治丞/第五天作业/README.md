# 【DAY5】AHT10 采集、文件系统与云端命令框架

- 姓名：陈治丞
- 组别：第 1 组
- 开发板：STM32F407 星火一号

## 一、任务与边界

本作业设计 AHT10 温湿度采集、文件追加保存和云端命令控制 LED 的程序框架。源码为 `aht10_data_logger.c`。本提交不包含未实际采集的截图、温湿度数值或云端上报结果；所有运行数据应在完成硬件配置和实际测试后补充。

## 二、AHT10 温湿度采集

AHT10 通过 I2C 总线工作，默认地址为 `0x38`。程序在 `aht10_read()` 中发送触发测量命令 `AC 33 00`，等待转换完成后读取 6 字节原始数据，并按 AHT10 数据手册换算温度和湿度。

连接时需要确认：

| AHT10 引脚 | 开发板 I2C 引脚 |
| --- | --- |
| VCC | 3.3 V |
| GND | GND |
| SDA | 所选 I2C 总线 SDA |
| SCL | 所选 I2C 总线 SCL |

代码默认总线名为 `i2c1`；如果实际 BSP 注册的总线名称不同，需要修改 `AHT10_I2C_BUS`。

## 三、文件系统启动与 Data.txt

要把数据保存到 Flash，需要先启用 FAL 和 DFS，并创建、格式化后挂载一个分区。挂载成功后，程序使用 POSIX `open()`、`write()` 和 `close()` 将数据追加写入：

```text
/Data.txt
Temp: XX.XX; Humi: XX.XX; Count: N
```

`day5_collect` 每执行一次就读取一次 AHT10 并追加一行。计数值应由实际项目保存或在读取文件后统计；本框架保留了数据格式和追加逻辑，实际运行时可扩展为持久化计数。

## 四、云端命令与 LED

`day5_handle_cloud_command()` 是云端接收回调的统一入口。网络组件（如 MQTT 或 HTTP 客户端）收到字符串后，只需要调用此函数：

```text
led:on
led:off
led:toggle
```

函数会控制星火一号 PF12 的 LED_R。`day5_led on/off/toggle` 是对应的本地 MSH 测试命令，便于先验证命令处理逻辑，再接入云端回调。

## 五、需要启用的组件

当前基础工程只启用了 PIN 和 FinSH；使用完整功能前需要在 RT-Thread Studio 的 `menuconfig` 中启用：

1. `RT_USING_I2C` 和对应 STM32 I2C 驱动；
2. AHT10 所使用 I2C 总线；
3. `RT_USING_DFS`、相应文件系统和 FAL/Flash 分区；
4. 云端通信所需网络、AT 客户端、MQTT 或 WebClient 组件。

未启用 I2C 或 DFS 时，`day5_collect` 会明确提示缺少配置，不会生成虚构的传感器数据。

## 六、MSH 使用方法

```text
msh >day5_led on
msh >day5_led toggle
msh >day5_collect
msh >cat Data.txt
```

在文件系统正确挂载、AHT10 正确接线后，`cat Data.txt` 应显示逐次追加的温湿度记录。每一条记录都必须来自实际采样。
