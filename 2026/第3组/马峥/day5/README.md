# RT-Thread 软件包与文件系统 — DAY5 作业

- **目标平台**：STM32F407 星火号（`stm32f407-rtspark`）
- **学生**：马峥 | 第3组
- **基础工程**：`05_iot_mqtt`（RT-Thread Studio 模板，已含 RW007 WiFi + pahomqtt v1.2.0）
- **参考工程**：`03_driver_temp_humi_1`（已含 AHT10 v2.1.0 验证可用）

---

## 一、作业目标

1. 使用 AHT10 软件包采集温湿度
2. 每 2 秒通过 MQTT 上传到 EMQX（broker.emqx.io:1883）
3. 数据同时写入本地文件系统 `/flash/Data.txt`
4. 云端发送 `LED_ON/OFF/TOGGLE` 指令控制小灯
5. 整理笔记 + 论坛发表

---

## 二、硬件 & 网络配置

| 项目 | 配置 |
|------|------|
| AHT10 温湿度 | I2C3 总线（软件 I2C, SCL=PE0, SDA=PE1） |
| WiFi SSID | `mzshouji` |
| WiFi 密码 | `00000000` |
| MQTT Broker | `broker.emqx.io:1883`（公共免费） |
| 发布主题 | `rtt/data` |
| 订阅主题 | `rtt/cmd`（接收 LED_ON/OFF/TOGGLE） |
| LED 引脚 | PF12（星火号 LED_R） |
| 数据文件 | `/flash/Data.txt`（W25Q64 SPI Flash + FAT） |

---

## 三、文件结构

```
D:\stm32\RT-ThreadStudio\workspace\05_iot_mqtt\
├── applications/
│   ├── main.c              # 主程序：整合所有模块
│   └── SConscript          # 增加了 AHT10 include 路径
├── packages/
│   ├── aht10-v2.1.0/       # 从 03_driver_temp_humi_1 复制
│   ├── pahomqtt-v1.2.0/    # 已有
│   ├── rw007-v2.0.1/       # 已有
│   └── pkgs.json           # 添加 AHT10 项
├── .config                 # 启用 I2C、DFS_ELMFAT、AHT10 等
├── rtconfig.h              # 添加对应 #define
└── rt-thread/tools/building.py  # 修复 Python 3.14 兼容
```

---

## 四、关键代码说明（main.c）

### 4.1 模块划分

| 模块 | 入口 | 作用 |
|------|------|------|
| AHT10 采集 | `aht10_init_module()` | I2C3 上初始化 AHT10，每 2s 读温湿度 |
| 文件系统 | `filesystem_init()` | W25Q64 挂载 FAT 到 `/flash` |
| 数据写文件 | `data_log_append()` | 追加一行 `Temp:XX ; Humi:XX ; Count:N` |
| MQTT 连接 | `mqtt_start()` | WiFi 就绪后启动，订阅 rtt/cmd |
| MQTT 发布 | `mqtt_pub_thread_entry()` | 每 2s 推送 JSON 到 rtt/data |

### 4.2 共享数据

```c
typedef struct {
    float temperature;
    float humidity;
    rt_uint32_t count;
    rt_mutex_t lock;
} sensor_data_t;
```

通过 `rt_mutex` 保护，避免 AHT10 线程与 MQTT 发布线程同时读写。

### 4.3 MQTT 回调

```c
static void mqtt_sub_callback(MQTTClient *c, MessageData *msg_data)
{
    // 解析 payload：LED_ON / LED_OFF / LED_TOGGLE
    // 调用 led_set() / led_toggle()
}
```

---

## 五、调试报告（关键问题及解决）

### 问题 1：Python 3.14 与 SCons 不兼容

**现象**：
```
TypeError: can only concatenate deque (not "list") to deque
  File "rt-thread\tools\building.py", line 737
```

**原因**：Python 3.14 的 `collections.deque` 不再支持 `deque + list` 拼接。

**解决**：修改 `rt-thread/tools/building.py`：
```python
# 头部加 import
from collections import deque

# 第 737 行
CPPDEFINES = deque(Env.get('CPPDEFINES', [''])) + deque(group.get('LOCAL_CPPDEFINES', ['']))
```

---

### 问题 2：`dfs_posix.h` 头文件不存在

**现象**：
```
fatal error: dfs_posix.h: No such file or directory
```

**原因**：RT-Thread 4.x 之后把 POSIX 文件操作接口（`open/read/write/close/mkdir`）从 `dfs_posix.h` 移到了 `<sys/unistd.h>` 和 `<sys/fcntl.h>`。

**解决**：修改 main.c：
```c
// 错误：#include <dfs_posix.h>
// 正确：
#include <dfs_fs.h>
#include <sys/unistd.h>
#include <sys/fcntl.h>
```

---

### 问题 3：`aht10.h` 找不到

**现象**：
```
fatal error: aht10.h: No such file or directory
```

**原因**：AHT10 包的 SConscript 里的 `CPPPATH` 只对包内源码生效，应用层 `applications/main.c` 不会自动继承。

**解决**：修改 `applications/SConscript`，手动加入 AHT10 包路径：
```python
CPPPATH = [cwd, os.path.join(cwd, '..', 'packages', 'aht10-v2.1.0')]
```

---

### 问题 4：`rt_wlan_set_mode` 参数错误

**现象**：
```
error: too few arguments to function 'rt_wlan_set_mode'
```

**原因**：当前 RT-Thread 版本要求两个参数 `(dev_name, mode)`，但旧代码只传一个。

**解决**：
```c
// 错误：rt_wlan_set_mode(RT_WLAN_STATION);
// 正确：
rt_wlan_set_mode(RT_WLAN_DEVICE_STA_NAME, RT_WLAN_STATION);
```

---

### 问题 5：AHT10 链接失败

**现象**：
```
undefined reference to `aht10_read_temperature'
undefined reference to `aht10_read_humidity'
undefined reference to `aht10_init'
```

**原因 1**：`pkgs.json` 缺少 AHT10 项。  
**原因 2**：`rtconfig.h` 缺少 `#define PKG_USING_AHT10`。  
**原因 3**：`sensor_asair_aht10.c` 引用了 sensor 框架但未启用。

**解决**：
1. 在 `packages/pkgs.json` 添加：
   ```json
   { "path": "/packages/peripherals/sensors/aht10", "ver": "v2.1.0", "name": "AHT10" }
   ```
2. 在 `rtconfig.h` 添加：
   ```c
   #define PKG_USING_AHT10
   #define PKG_USING_AHT10_V210
   ```
3. 修改 `packages/aht10-v2.1.0/SConscript`，只编译 `aht10.c`：
   ```python
   src = ['aht10.c']  # 不编译 sensor_asair_aht10.c（需 sensor 框架）
   ```

---

### 问题 6：`BSP_I2C3_SCL_PIN` / `BSP_I2C3_SDA_PIN` 未定义

**现象**：
```
drv_soft_i2c.h:53:16: error: 'BSP_I2C3_SCL_PIN' undeclared
```

**原因**：`drv_soft_i2c.c` 用这两个宏定义软 I2C 引脚，但 rtconfig.h 没声明。

**解决**：在 `rtconfig.h` 添加：
```c
#define BSP_USING_I2C
#define BSP_USING_I2C3
#define BSP_I2C3_SCL_PIN 64   /* PE0 = 4*16+0 */
#define BSP_I2C3_SDA_PIN 65   /* PE1 = 4*16+1 */
```

---

### 问题 7：`sensor_asair_aht10.c` 编译错误

**现象**：
```
error: invalid use of undefined type 'struct rt_sensor_intf'
error: unknown type name 'rt_sensor_t'
```

**原因**：`sensor_asair_aht10.c` 依赖 `RT_USING_SENSOR` 框架，但本项目未启用 sensor 框架。

**解决 1（scons 编译）**：修改 `packages/aht10-v2.1.0/SConscript`，只编译 `aht10.c`：
```python
src = ['aht10.c']  # 不编译 sensor_asair_aht10.c（需 sensor 框架）
```

**解决 2（RT-Thread Studio IDE 编译）**：直接删除文件：
```
删除 packages/aht10-v2.1.0/sensor_asair_aht10.c
删除 packages/aht10-v2.1.0/sensor_asair_aht10.h
删除 Debug/packages/aht10-v2.1.0/ 整个目录（IDE 缓存）
```

> 注意：IDE 用的是 makefile 系统（生成 Debug/subdir.mk），不是 scons，所以仅改 SConscript 不够，**必须物理删除文件**。

---

## 六、最终编译结果

```
scons: done building targets.
   text    data     bss     dec     hex filename
 319528    1820   61892  383240   5d908 rt-thread.elf
```

- **0 error**，链接通过
- 生成 `rt-thread.elf` 和 `rtthread.bin`
- 资源占用：text 319KB、data 1.8KB、bss 61KB

---

## 七、运行测试流程（待板上验证）

### 7.1 MSH 测试命令

| 命令 | 功能 |
|------|------|
| `led on` / `led off` / `led toggle` | 本地控制 LED |
| `show_data` | 打印当前温湿度和计数 |
| `cat_data` | 打印 `/flash/Data.txt` 内容 |

### 7.2 MQTT 测试（用 MQTT.fx 或类似客户端）

| 主题 | 方向 | 内容 |
|------|------|------|
| `rtt/data` | 设备→云 | `{"count":1,"temp":25.3,"humidity":60.2}` |
| `rtt/cmd` | 云→设备 | `LED_ON` / `LED_OFF` / `LED_TOGGLE` |

### 7.3 文件系统

启动后会自动：
1. `dfs_mount("W25Q64", "/flash", "elm", 0, 0)` 挂载 FAT
2. 首次格式化（`mkfs`）会自动执行
3. 创建 `/flash/Data.txt` 并写入表头
4. 每 2 秒追加一行：
   ```
   Temp:25.3 ; Humi:60.2 ; Count:1
   Temp:25.4 ; Humi:60.1 ; Count:2
   ...
   ```

---

## 八、笔记 — PIN 设备 / WiFi / MQTT / DFS 总结

### 8.1 软件包生态

RT-Thread 通过 env 工具（`studio安装目录/env/`）管理软件包索引：
- 软件包源代码独立维护在 RT-Thread 官方仓库
- 通过 Kconfig 配置使能 → `pkgs --update` 自动下载到 `packages/` 目录
- `pkgs.json` 记录已下载包
- 多个项目可共享同一个 env 安装路径

### 8.2 AHT10 软件包使用

```c
#include "aht10.h"

aht10_device_t dev = aht10_init("i2c3");  // 初始化（指定 I2C 总线名）
float t = aht10_read_temperature(dev);    // 读温度（摄氏度）
float h = aht10_read_humidity(dev);       // 读湿度（%RH）
```

注意：需要 I2C 总线已经存在，依赖 `RT_USING_I2C` + `RT_USING_I2C_BITOPS`。

### 8.3 RW007 WiFi 配置要点

- SPI 总线 + 4 个 GPIO：CS、BOOT0、BOOT1、INT/BUSY、RESET
- 默认引脚：CS=PB12, BOOT0=PB13, BOOT1=PB12, INT=PD14, RESET=PD15
- WiFi 共享 SPI2 总线与 SPI Flash，需注意 CS 引脚拉高防止冲突
- 连接后会自动触发 `RT_WLAN_EVT_READY` 事件

### 8.4 MQTT（Paho）框架使用

```c
// 1. 定义 client
MQTTClient client;
client.uri = "tcp://broker.emqx.io:1883";
client.condata.clientID.cstring = "rtt_001";
// 2. 分配 buffer
client.buf_size = client.readbuf_size = 1024;
client.buf = rt_malloc(1024);
client.readbuf = rt_malloc(1024);
// 3. 设置回调
client.messageHandlers[0].topicFilter = "rtt/cmd";
client.messageHandlers[0].callback = my_sub_cb;
client.messageHandlers[0].qos = QOS1;
// 4. 启动
paho_mqtt_start(&client);
// 5. 发布
MQTTMessage msg = {0};
msg.qos = QOS1;
msg.payload = "hello";
msg.payloadlen = 5;
MQTTPublish(&client, "rtt/data", &msg);
```

### 8.5 DFS 文件系统挂载

```c
// 1. 初始化设备（SFUD 已自动探测 W25Q64）
// 2. 挂载 FAT
dfs_mount("W25Q64", "/flash", "elm", 0, 0);
// 3. POSIX 操作
int fd = open("/flash/Data.txt", O_WRONLY | O_CREAT | O_APPEND);
write(fd, "data", 4);
close(fd);
```

注意：需要 `RT_USING_DFS` + `RT_USING_DFS_ELMFAT`（FAT 兼容层）。

---

## 九、提交清单

```
2026/第3组/马峥/day5/
├── README.md           # 本文档（笔记+调试报告）
├── main.c              # 整合后的应用层代码
└── 调试报告.md         # 问题及解决清单
```

---

## 十、待办

- [ ] 烧录到星火号板验证
- [ ] MQTT.fx 测试云端指令翻转 LED
