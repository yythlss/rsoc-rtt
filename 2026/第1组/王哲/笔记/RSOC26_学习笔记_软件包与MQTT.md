# 【RSOC26】软件包、外设与 MQTT 学习笔记（AHT10 上云 + 文件系统 + 云端点灯）

> 实验板卡：RT-Thread Studio STM32F4 工程 spark_demo（板载 AHT10、RW007、W25Q64）
> 配套讲义：《夏令营.md》；工程代码：`applications/`（main.c、ath10_demo.c、mqtt_demo.c）

[TOC]

---

## 一、软件包（Software Package）

### 1.1 什么是软件包，为什么要用它

软件包是具有特定功能、用于完成特定任务的一个程序或一组程序。它通常已经封装好了底层驱动、协议栈或通用算法，应用程序只需调用它提供的 API，即可减少重复开发。

使用软件包的主要收益：

- **缩短开发周期**：传感器（AHT10）、网络模组（RW007）、协议栈（MQTT）拿来即用；
- **复用经过测试的驱动和协议实现**，不用自己从头写协议和时序；
- **通过 Kconfig / menuconfig 按需裁剪**：控制 ROM、RAM 以及线程栈占用。

软件包中心：<https://packages.rt-thread.org/index.html>

### 1.2 软件包的配置流程

在 RT-Thread Studio 中，基本流程是：**打开配置 → 勾选软件包并填参数 → 保存配置 → 下载包 → 重新构建工程**。

1. 菜单中找到配置入口（Kconfig/ENV 工具位于 Studio 安装目录的 `env` 目录）；
2. 在 `RT-Thread online packages` 中选择需要的软件包、指定版本、填好参数；
3. 保存配置（生成 `.config` 与 `rtconfig.h`）；
4. 执行 `pkgs --update`（Studio 中右键工程即可）下载软件包源码到 `packages/` 目录；
5. 重新构建并下载。

> 注意：不要一次勾选全部软件包，每个包都会增加编译体积和资源占用；只勾选本工程实际用到的。

本工程实际启用的软件包与配置（打开工程根目录 `.config` 可原样核对）：

| 配置项 | 值 | 说明 |
| --- | --- | --- |
| `CONFIG_PKG_USING_KAWAII_MQTT=y` | latest | MQTT 客户端软件包（与 paho、umqtt 二选一） |
| `CONFIG_KAWAII_MQTT_VERSION=4` | 4 | 对应 MQTT 3.1.1 |
| `CONFIG_KAWAII_MQTT_HOST="broker.emqx.io"` | — | 公共 MQTT Broker |
| `CONFIG_KAWAII_MQTT_PORT="1883"` | — | 明文 TCP 端口 |
| `CONFIG_KAWAII_MQTT_SUBTOPIC="rtt-sub112"` | — | 板卡订阅主题 |
| `CONFIG_KAWAII_MQTT_PUBTOPIC="rtt-pub122"` | — | 板卡发布主题 |
| `CONFIG_PKG_USING_RW007=y` | v2.1.0 | WiFi 模组软件包（SPI2 接口） |

### 1.3 外设与软件包：AHT10 温湿度传感器（I2C 设备）

驱动一个 I2C 传感器通常需要：读数据手册确定设备地址/命令/数据格式 → 确认传感器挂在哪条 I2C 总线 → 用 I2C API 完成读写 → 把原始数据换算成温度/湿度。

#### （1）先打开 I2C 外设配置

在工程配置中开启 I2C 总栈、I2C3 外设并配置其 SCL/SDA 引脚：

```text
CONFIG_RT_USING_I2C=y
CONFIG_BSP_USING_I2C=y
CONFIG_BSP_USING_I2C3=y
CONFIG_BSP_I2C3_SCL_PIN=64
CONFIG_BSP_I2C3_SDA_PIN=65
```

配置后 BSP 会在启动时注册 I2C 总线设备，串口日志可见：

```text
[I/I2C] I2C bus [i2c3] registered
```

#### （2）使用 aht10 软件包读取数据

开启软件包后，应用层只需三行核心代码（`ath10_demo.c`）：

```c
aht10_device_t ath10 = aht10_init("i2c3");      // 按总线名初始化
if (RT_NULL == ath10) { LOG_E("i2c3 not found"); return; }
float temp  = aht10_read_temperature(ath10);    // 返回 float℃，小数点后2位
float humid = aht10_read_humidity(ath10);       // 返回 float 湿度百分数
```

关键点：**`aht10_init("i2c3")` 里的总线名必须与 BSP 注册的设备名一致**，不一致时函数返回 `RT_NULL`，需要用 `list_device` 命令核对实际注册名。

#### （3）实验现象

```text
msh >ath10_init
msh >temp = 24.98
humid = 80.40%
temp = 24.41
humid = 80.12%
temp = 24.46℃
humid = 80.10%
```

#### （4）常见问题

| 现象 | 优先检查项 |
| --- | --- |
| 找不到 I2C 总线 | I2C 外设是否开启、总线名与 `aht10_init()` 是否一致 |
| 读写返回错误 | SDA/SCL 接线、上拉电阻、设备供电电压 |
| `%f` 输出为字面值 | 未开启 `rt_vsprintf_full` 软件包（精简版 vsnprintf 不支持浮点） |
| `i2c3 not found` | `list_device` 核对真实总线名 |

### 1.4 MQTT 协议介绍

MQTT（Message Queuing Telemetry Transport）是一种轻量级的**发布/订阅**消息协议，适合资源受限设备以及带宽有限、延迟较高或连接不稳定的网络环境。

#### （1）基本模型

由客户端、服务端、主题和消息组成：

| 名词 | 说明 |
| --- | --- |
| Client | 客户端（板卡、手机 App、MQTTX 等），可以连接、发布、订阅、断开 |
| Server / Broker | 服务端，维护连接、接收发布消息、按订阅关系转发给匹配的客户端 |
| Topic | 主题名，分层分类，例如 `rsoc/sensor/temperature` |
| Publish / Subscribe | 发布 / 订阅，双方互不相识，只要约定主题就能通 |

MQTT 的优势：**轻量**（报文开销小）、**解耦**（发布者与订阅者相互独立）、**可扩展**（主题分层+通配符支持海量设备）、**可配置可靠性**（QoS、保活、会话参数按需取舍）。

#### （2）QoS 三级交付语义

QoS 不是"网络绝对可靠"的保证，而是客户端与 Broker 之间的交付语义：

| 等级 | 含义 | 典型场景 |
| --- | --- | --- |
| QoS 0 | 至多一次：不等待确认，可能丢 | 高频环境数据、状态刷新 |
| QoS 1 | 至少一次：可能重复，应用层需去重 | 报警、控制命令、重要状态 |
| QoS 2 | 恰好一次：交互次数最多、开销最高 | 不允许重复、消息量小的业务 |

主题通配符：`+` 匹配单层（如 `rsoc/+/temperature`），`#` 匹配多层。生产项目应为设备规划清晰的主题层级，并限制发布/订阅权限。

#### （3）在 RT-Thread 上使用 MQTT

本实验使用 **kawaii-mqtt** 软件包，连接 EMQX 免费公共 Broker（`broker.emqx.io:1883`，明文 TCP，无需用户名密码，仅适合学习验证，不能用于生产）。

推荐按"**先联网 → 再 MQTT → 最后业务**"的顺序调试：

1. 开启 RW007 网络模组，确认网卡能拿到 IP（`wifi join <SSID> <PASSWORD>`，看 `Got IP address`）；
2. 用 `ifconfig`、`ping` 检查网络、网关和 DNS；
3. 在 `RT-Thread online packages` 中启用 `kawaii-mqtt`，协议版本设为 4（MQTT 3.1.1）；
4. 配置 Broker（`broker.emqx.io`）、端口（1883）、唯一 Client ID、独立的发布/订阅主题；
5. 先做 MQTTX 与板卡双向收发，再接入传感器数据；
6. 为断线重连、发布失败、接收回调增加日志。

公共 Broker 上主题和消息对所有人可见，主题需带唯一标识（如学号/板号/随机后缀）。Client ID 也必须唯一，否则同名客户端互相顶掉连接。

#### （4）kawaii-mqtt 应用层代码结构（`mqtt_demo.c`）

```c
client = mqtt_lease();                              // 租借客户端对象
mqtt_set_host(client, KAWAII_MQTT_HOST);            // 4 个参数来源自 Kconfig 配置
mqtt_set_port(client, KAWAII_MQTT_PORT);
mqtt_set_client_id(client, KAWAII_MQTT_CLIENTID);
mqtt_set_clean_session(client, 1);
mqtt_connect(client);                               // 建立 TCP + MQTT 连接

mqtt_subscribe(client, KAWAII_MQTT_SUBTOPIC, QOS0, sub_topic_handle1);  // 订阅+注册回调

mqtt_message_t msg = {0};
msg.qos = QOS0;
msg.payload = (void *)data;                         // 帧数据
msg.payloadlen = strlen(data);                      // 长度必须正确
mqtt_publish(client, KAWAII_MQTT_PUBTOPIC, &msg);   // 发布
```

订阅回调获得的消息结构：`msg->topic_name`（主题名）、`msg->message->payload`（消息内容数组）、`msg->message->qos`。

```text
msh >ka_mqtt
[I] >> The ID of the Kawaii client is: rtthread001
[I] >> ../packages/kawaii-mqtt-latest/mqttclient/mqttclient.c:991 mqtt_connect_with_results()... mqtt connect success...
```

#### （5）用 MQTTX 完成双向收发验证

MQTTX 连接参数：协议 `mqtt://`、Host `broker.emqx.io`、Port `1883`、Version `3.1.1`、Client ID 自定义唯一值。

- 验证方向一：MQTTX **订阅**板卡的发布主题（`rtt-pub122`），板卡发布后 MQTTX 应显示主题、QoS、Payload；
- 验证方向二：MQTTX **向**板卡订阅主题（`rtt-sub112`）发消息，板卡订阅回调触发：

```text
[I] >> -----------------------------------------------------------------------------------
[I] >> sub_topic_handle1()...
topic: rtt-sub112
message:{
  "msg": "hello"
}
[I] >> -----------------------------------------------------------------------------------
```

> 排查要点：主题**区分大小写**，`/` 不多写也不少写；两端主题方向必须交叉对应（板卡发布= MQTTX 订阅，板卡订阅= MQTTX 发布）。

![mqttex](C:\Users\zhe\Desktop\RSOC\RSOC26_学习笔记_软件包与MQTT.assets\mqttex.png)

------



## 二、组件（Component）

组件是可以独立开发、测试、部署和维护的软件单元。与单个驱动相比，组件通常包含更完整的接口、配置项和示例，为上层提供稳定的功能边界。

### 2.1 文件系统（DFS）

#### （1）DFS 是什么

DFS（Device File System）是 RT-Thread 的**虚拟文件系统**组件。向上提供统一的路径和文件操作接口，向下对接 FatFS、RomFS、DevFS 等具体文件系统以及块设备。

系统使用统一根目录 `/`。一个具体文件系统通常先格式化到块设备，再**挂载**到某个目录（如 `/fal`）；挂载后应用只需按路径访问文件，不用关心底层 Flash 的擦写细节。

#### （2）文件系统启动流程（任务 2 梳理点）

1. **DFS 组件初始化**：系统启动时初始化文件系统注册表、文件描述符表及相关锁（启用 DevFS 时还会初始化设备文件系统）；
2. **注册文件系统类型**：FatFS、RomFS 等通过各自的初始化函数注册到 DFS 后，才能被 `mount()` 识别；
3. **格式化并挂载**：块设备首次使用需选择文件系统并格式化，然后挂载到指定路径；自动挂载功能可在系统启动阶段完成这一步，但量产项目仍应检查挂载返回值与分区版本。

#### （3）常见文件系统对比

| 类型 | 特点 |
| --- | --- |
| FatFS（elm_fat） | 兼容 FAT 格式、移植性好，适合 SD 卡与可读写 Flash；本实验采用 |
| RomFS | 结构简单、只读，适合固件内置资源；不适合频繁写入 |
| DevFS | 把设备映射到 `/dev` 目录，设备可用类似文件的 read/write 接口访问 |
| UFFS | 面向 NAND Flash 的轻量文件系统，适合资源受限场景 |
| NFS | 通过网络共享主机文件系统，用于开发调试，不建议量产使用 |

#### （4）POSIX 接口层

- 文件：`open()` `close()` `read()` `write()` `lseek()` `rename()` `stat()` `unlink()`
- 目录：`mkdir()` `opendir()` `readdir()` `closedir()`
- 文件系统：`mkfs()` `mount()` `umount()`

`fd`（文件描述符）是一个整数句柄，代表打开的文件或设备。**`open()` 成功后必须在所有退出路径调用 `close()`**，否则长期运行会耗尽文件描述符。`readdir()` 返回 `RT_NULL` 表示遍历结束，异常路径也要调用 `closedir()`。

#### （5）常用 Shell 命令（任务 2 熟悉项）

```text
msh > list_device      # 查看设备列表（寻找总线名、块设备名）
msh > fal probe        # 查看 FAL 设备/分区
msh > ls /             # 查看根目录挂载情况
msh > mkfs -t elm <blk_device>   # 格式化块设备（fat 文件系统类型）
msh > mount <blk_device> /fal    # 挂载到 /fal
msh > ls /fal          # 查看挂载点文件
```

> `<blk_device>` 必须替换为 `list_device` 输出中的真实块设备名。**格式化会清除分区原有数据**，首次操作前应确认分区范围。

### 2.2 FAL 与 SFUD（Flash 抽象层 + 串行 Flash 通用驱动）

#### （1）各自职责

- **SFUD**（Serial Flash Universal Driver）：屏蔽不同 SPI NOR Flash 在 JEDEC ID、读写擦除命令上的差异，提供统一设备接口（`sfud_get_device()`、`sfud_read()`、`sfud_erase()`、`sfud_write()`）——负责"如何和具体 Flash 芯片通信"；
- **FAL**（Flash Abstraction Layer）：管理 Flash 设备和分区，向文件系统、OTA、NVM 等上层提供统一的分区访问接口——负责"如何把 Flash 划分成可管理的逻辑分区"。

支持静态配置分区表、分区自动装载、也可以在 Bootloader/裸机环境运行；提供基于 FINSH/MSH 的测试命令，便于按地址读写擦除。

#### （2）本次任务的数据链路（以板载 W25Q64 为例）

```text
SPI 控制器
  -> spi1x/spi2x 设备（以工程实际注册名为准）
  -> SFUD SPI_FLASH 设备（W25Q64）
  -> FAL 分区
  -> BLK 块设备
  -> FatFS / elm_fat
  -> DFS 挂载点（如 /fal）
  -> POSIX 文件 API：open/read/write
```

在 BSP 中通常：把 SPI 设备挂到总线 → 通过 `rt_sfud_flash_probe()` 绑定为 SPI_FLASH 设备 → 在 FAL 分区表（`fal_cfg.h`）中定义分区 → 为目标分区创建块设备 → 文件系统格式化挂载后即可按路径访问。

#### （3）menuconfig 配置步骤（参照讲义截图）

1. 开启 SPI Flash、文件系统、FAL 相关外设；
2. 在文件系统菜单中启用基于 W25Q64 的 FAL 分区和自动挂载；
3. 确认 FAL 分区配置已启用；
4. 检查自动装载/自动挂载选项并保存；
5. 按工程需要勾选其他存储兼容选项——不要无条件全部勾选。

#### （4）验证顺序

先确认设备和分区名，再操作：

```text
msh > list_device
msh > fal probe
msh > ls /
```

若未启用自动挂载，手工格式化并挂载（本工程按实际设备名执行）：

```text
msh > mkfs -t elm <blk_device>
msh > mount <blk_device> /fal
msh > ls /fal
```

### 2.3 任务 2：温湿度数据同步写入文件系统 Data.txt

思路：AHT10 采集线程每读到一组温湿度，除了发给 MQTT 线程，同时把数据追加写入挂载点下的 `Data.txt`，并记录**自上电以来的采集次数 Count**。

文件内容格式（按任务要求）：

```text
Temp：26.1 ; Humi：45.2 ; Count： 1
Temp：26.2 ; Humi：45.5 ; Count： 2
Temp：26.1 ; Humi：45.4 ; Count： 3
```

实现要点（示例，`applications/` 新增日志模块，采集线程每周期调用一次）：

```c
#include <dfs_posix.h>
#include <fcntl.h>

static rt_uint32_t count = 0;                       /* 自上电起的采集次数 */

void data_log_write(float temp, float humid)
{
    char buf[64];
    int fd = open("/fal/Data.txt", O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) { rt_kprintf("open Data.txt failed\n"); return; }

    count++;
    rt_snprintf(buf, sizeof(buf),
                "Temp：%.2f ; Humi：%.2f ; Count： %d\n", temp, humid, count);
    write(fd, buf, rt_strlen(buf));
    close(fd);                                      /* 关闭，防止 fd 耗尽 */
}
```

要点：

- `O_APPEND` 追加写入，重启后历史数据不丢；`Count` 用 static 变量实现"自上电起计数"；
- 每次 `open` 后必须 `close`；
- 若尚未挂载：上电后先执行 `mkfs -t elm <blk_device>`（首次）与 `mount <blk_device> /fal`，或启用自动挂载；
- 挂载点 `/fal` 必须以工程实际挂载点为准。

### 2.4 任务 3：云端下发指令翻转小灯

思路：板卡保持订阅 `rtt-sub112`，订阅回调中**解析 payload**：若收到约定指令（如 `led` / `led toggle`），执行一次引脚翻转；否则作为普通消息打印。

实现要点（在 `sub_topic_handle1` 回调中）：

```c
#include <rtdevice.h>

#define PIN_LED_R  GET_PIN(F, 12)

static void sub_topic_handle1(void *client, message_data_t *msg)
{
    if (rt_strncmp(msg->message->payload, "led", 3) == 0) {
        rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
        static rt_bool_t b_led_on = RT_FALSE;
        b_led_on = !b_led_on;
        rt_pin_write(PIN_LED_R, b_led_on ? PIN_LOW : PIN_HIGH);   /* 翻转 */
        rt_kprintf("led toggled\n");
    } else {
        rt_kprintf("recv: %s\n", msg->message->payload);
    }
}
```

云端操作：MQTTX 向 `rtt-sub112` 发布消息 `led`（或 `test` 主题的自定义指令），板卡收到后 LED 状态翻转，同时串口打印日志。类似地还可以扩展为控制多个设备（如 `led1-ledr` 分开管理）并通过回调确认。

### 2.5 任务 4：熟悉其他软件包

本次实验顺带接触的软件包：

| 软件包 | 用途 |
| --- | --- |
| `rw007` | WiFi 模组驱动（本实验：SPI2 接口，提供 `wifi scan/join/status` 等命令） |
| `kawaii-mqtt` | MQTT 客户端（3.1.1/5.0，带日志、重连、自测命令 `ka_mqtt`） |
| `aht10` | AHT10 温湿度传感器驱动 |
| `sfud` | 串行 Flash 通用驱动，配合 FAL 使用 |
| `rt_vsprintf_full` | 完整版 vsnprintf（支持 `%f`） |
| `dfs_yaaffs / dfs_jffs2 / dfs_uffs` | 各类嵌入式文件系统（本工程未启用） |

扩展方向：在软件包中心按分类浏览（外设/物联网/通信/文件系统等），后续还可尝试 `webclient`（HTTP）、OTA、`cJSON`、传感器框架（sensor）等软件包，触类旁通——都是"**Kconfig 开门 → 设备注册 → 调用 API**"这一套。

---

## 三、小结与踩坑心得

- **主线**：软硬件复用在 RT-Thread 里是同一件事——Kconfig 开门（开外设、开软件包、填参数）→ 设备被注册 → 应用只调 API。I2C3 门开给 AHT10 包，SPI2 门开给 RW007 包，网络栈门开给 kawaii-mqtt 包；
- **联网 > MQTT > 业务**：先 `wifi join` 拿到 IP（`Got IP address`），再 `ping` 验证 DNS 与网关，最后才连 Broker；
- **三个高频坑**：
  1. `%f` 输出字面值 → 开启 `rt_vsprintf_full`；
  2. Shell 提示 `command not found` → 函数未 `MSH_CMD_EXPORT`，或命令拼写错误（Shell 不自动纠错）；
  3. MQTTX 收不到数据 → 先核对主题方向与大小写，再查 Client ID 唯一性；
- **文件系统首次使用**：先 `list_device` 确认设备 → `mkfs -t elm` 格式化 → `mount` → `ls` 验证，生产环境务必检查挂在返回值。

---

### 附：本工程应用层线程结构（回顾）

```text
main.c         LED 心跳（GPIO 输出演示）
ath10_demo.c   AHT10 线程：每 1s 采集一次 → 数据包塞入邮箱
mqtt_demo.c    MQTT 线程：从邮箱取数据 → publish 到 broker；订阅回调接收云端指令
               两者之间用邮箱（rt_mailbox）传递指针：谁分配谁释放
```
