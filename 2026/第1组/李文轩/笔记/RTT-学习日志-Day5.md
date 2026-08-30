# RTT-学习日志-Day5

## 第一篇：MQTT/WiFi 工程搭建与排错复盘

> **目录**：
> 1. 第一篇：MQTT/WiFi 工程搭建与排错复盘（软件包拉取失败 → wifi -10 → msh 恢复 → RW007 Stage1 → .cproject 坑）
> 2. 第二篇：MQTT 连接成功（静默失败根因 → Settings 修复 → 双向打通 → MQTTX 主题方向坑）
> 3. 第三篇：AHT10 温湿度传感器（40 编译错误 → 命令导入 → 成功读数）
> 4. 第四篇：文件系统与综合实验（W25Q64 挂载 → AHT10+MQTT 上传+落盘+LED 指令 全通）

> 内容范围：从软件包拉取失败开始，到 msh 恢复、RW007 报错分析的完整过程。
> 涉及工程：`01_Mqtt` → `MQTT_test`（星火一号 STM32F407 + 板载 RW007 SPI WiFi + kawaii-mqtt）。

---

### 一、软件包拉取失败（网络问题）

#### 1. 现象

在 RT-Thread Settings 里勾选软件包后点「保存」，Studio 会自动下载软件包源码，控制台报错：

```
kawaii-mqtt-latest downloaded successfully. [Use Gitee server]
...
api.rt-thread.org:443  Max retries exceeded (SSLError: EOF)
github.com:443  rw007-2.1.0.zip download fail!（重试 6 次）
Operation failed.
```

#### 2. 结论

| 包 | 结果 | 原因 |
|---|---|---|
| kawaii-mqtt | ✅ 下载成功 | 走了 **Gitee 镜像** |
| rw007 | ❌ 下载失败 | 走了 **GitHub**，被墙/网络不通 |

- `api.rt-thread.org`：软件包元数据 API 服务器，国内访问常被中断（SSL EOF）。
- `github.com`：直接连不上 → rw007 的 zip 下载 6 次全失败。

#### 3. 解决方法

**关键点**：软件包源码最终是放在工程的 `packages/` 目录下的，配置里勾选只是写宏，**源码要另外下载**。GitHub 不通就改用 Gitee 镜像：

```bash
git clone --depth 1 --branch v2.1.0 \
  https://gitee.com/rt-thread-mirror/rw007 \
  packages/rw007-v2.1.0
```

- 镜像仓库地址：`https://gitee.com/rt-thread-mirror/rw007`
- `--branch v2.1.0` 拉指定版本，`--depth 1` 只拉最近一次提交（快）。
- 下载后 `packages/SConscript` 会自动遍历子目录里带 `SConscript` 的包并编译，**不需要那个 zip 缓存**。

---

### 二、核心概念：配置宏 ≠ 软件包源码

这是本次踩坑的总根源，务必分清两件事：

| 操作 | 实际做了什么 | 结果 |
|---|---|---|
| 在 Settings 里勾选 RW007 | 只往 `.config` / `rtconfig.h` 写 `PKG_USING_RW007` 等**宏** | 配置说"要用"，但**源码还没进工程** |
| `pkgs --update` / 手动 clone | 把包源码下载到 `packages/rw007-v2.1.0` | 源码就位，编译才会把驱动编进去 |

- 只勾选不下载 → 编译产物里没有驱动代码 → 运行时报错。
- 验证方法：编译后看 `Debug/rtthread.map` 里有没有对应符号（如 `rw007_sn_get`、`rt_hw_wifi_init`）。

---

### 三、wifi scan 报 `-10`

#### 1. 错误码含义

`-10` = **`-RT_EINVAL`（参数无效）**，定义在 `rt-thread/include/rtdef.h`：

```c
#define RT_EOK       0
#define RT_ERROR     1
#define RT_ETIMEOUT  2
#define RT_EBUSY     7
#define RT_EIO       8
#define RT_EINVAL   10   /* 参数无效 */
```

#### 2. 报错链路（读源码定位）

```
msh > wifi scan
  → wlan_cmd.c: rt_wlan_scan_with_info(info)
  → wlan_mgnt.c: if (_sta_is_null()) return -RT_EINVAL;   ← 命中这里
```

`_sta_is_null()` 判断的是 `_sta_mgnt.device == RT_NULL`，而这个指针只有在某个 WLAN 驱动调用 `rt_wlan_dev_register()` 注册 STA 设备时才会被赋值。

#### 3. 根因与修复

- **根因**：RW007 驱动源码没下载 → 固件里没有驱动 → 没人注册 `wlan0` → `_sta_mgnt.device` 永远为空 → `wifi scan` 返回 -10。
- **修复**：下载 rw007 包 + 在 `.cproject` 的 include 路径里加上 `packages/rw007-v2.1.0` 和 `packages/rw007-v2.1.0/inc`。

> 排查技巧：从报错字符串 `"Scan with info error"` 反查源码（`grep -rn "Scan with info error"`），再顺着返回值往上追，几行就能定位到 `_sta_is_null()`。

---

### 四、串口无法输入（msh 消失）

#### 1. 现象

- 串口**能显示**日志（内核 `rt_kprintf` 输出正常）；
- 但**敲命令没反应**，且没有 `msh >` 提示符。

#### 2. 根因

新工程生成的 `.config` 里是 `# CONFIG_RT_USING_MSH is not set`——**FinSH/msh 命令行组件没被编译进固件**。

- 固件里没有 shell → 没有命令解释器 → 串口只能"输出"、不能"输入"。
- 这不是串口硬件问题（uart1 驱动正常，否则连日志都看不到），是**软件功能缺失**。

#### 3. 修复三件套（缺一不可）

| 文件 | 改什么 | 作用 |
|---|---|---|
| `rtconfig.h` | 加 `RT_USING_MSH` / `RT_USING_FINSH` / `FINSH_USING_MSH` 等宏 | 让 finsh 源码被编译（源码内部用 `#ifdef RT_USING_FINSH` 包裹） |
| `.config` | `CONFIG_RT_USING_MSH=y` + 全套 finsh 项 | 让"保存设置"时不再把 msh 关掉 |
| `.cproject` | ① 从 `excluding` 排除列表移除 `rt-thread/components/finsh`；② include 路径加 `rt-thread/components/finsh` | 让 Eclipse make 构建真的去编译 finsh，并找到 `finsh.h` |

完整 finsh 宏（对照同工作区正常工程抄来的）：

```c
#define RT_USING_MSH
#define RT_USING_FINSH
#define FINSH_USING_MSH
#define FINSH_THREAD_NAME "tshell"
#define FINSH_THREAD_PRIORITY 20
#define FINSH_THREAD_STACK_SIZE 4096
#define FINSH_USING_HISTORY
#define FINSH_HISTORY_LINES 5
#define FINSH_USING_SYMTAB
#define FINSH_CMD_SIZE 80
#define MSH_USING_BUILT_IN_COMMANDS
#define FINSH_USING_DESCRIPTION
#define FINSH_ARG_MAX 10
```

> 注意：RT-Thread Settings 每次「保存」会用设置编辑器里的状态重写 `.config`/`rtconfig.h`。如果编辑器里「组件 → MSH: command shell」没勾，保存一次 msh 就丢一次。**要么先在 GUI 勾上，要么保存后重新补这三个文件。**

---

### 五、RW007 开机日志分析（Stage 1 报错）

#### 1. 报错内容

```
[E/[RW007]] The wifi Stage 1 status 0 0 0 1
```

#### 2. 报错含义

RW007 通过 SPI 与主机通信，每次命令分两个阶段握手，第一次命令（`seq=1`）在 **Stage 1** 校验失败。

- 主机发出 `MASTER_MAGIC1 = 0x67452301`、`MASTER_MAGIC2 = 0xEFCDAB89`；
- 期望从机应答 `SLAVE_MAGIC1 = 0x98BADCFE`、`SLAVE_MAGIC2 = 0x10325476`；
- 实际读到 `0 0 0` → **MISO 全 0**，说明第一次命令时模块的 SPI 从机接口还没就绪、没驱动 MISO。

> 补充：还有一种情况会读到 `67452301 efcdab89`（主机自己发出去的数据被回读），同样是"从机没应答"的表现。两者本质相同。

#### 3. 为什么"有报错却还能用"

驱动 `spi_wifi_transfer` 里有 **3 次重试**：

```c
retry = 3;
while (retry > 0) {
    result = wifi_data_transfer(dev, cmd_seq, rx_buffer, tx_buffer);
    if (result != TRANSFER_DATA_ERROR) break;
    retry--;
}
```

第一次失败 → 立即重试 → 重试成功。证据：后续 `wlan init success` ×2、`rw007 sn`、`rw007 ver` 全部正常读取，说明 SPI 链路是通的、模块是好的。**这条报错是开机时序问题（模块复位后第一个命令发早了），属于无害的瞬时错误。**

#### 4. 消除方法

- 首选：`RW007_SPI_MAX_HZ` 从 `30000000` 降到 `15000000`（驱动注释明确 "15M 007 max 30M"，30M 是极限值）；
- 或：把 `rw007_stm32_port.c` 里 `rw007_gpio_init()` 的复位后 200ms 延时加大到 500ms。

#### 5. 行首的 "m" / ">" 是什么

`spi_wifi_rw007.c` 是**全工程唯一开了 `DBG_COLOR`** 的文件，日志原始输出带 ANSI 颜色：

```
\033[31m[E/[RW007]] The wifi Stage 1 status ...\033[0m
```

串口工具把 `\033[31` 吞掉后只剩尾巴 `m`，所以显示成 `m[E/[RW007]]`；恢复 msh 后，`msh >` 提示符也可能和它交错，出现 `>[E/[RW007]]`。**都是显示层现象，不是串口硬件问题。**

---

### 六、.cproject 的坑（Eclipse make 构建）

#### 1. 为什么打开 msh 后突然 31 个报错

打开 `RT_USING_FINSH` 后，内核头文件 `rtthread.h` 会执行：

```c
#ifdef RT_USING_FINSH
#include <finsh.h>
#endif
```

但 Eclipse make 构建的 `-I` include 路径来自 `.cproject`（**不是** SConscript），而 `.cproject` 里还没有 finsh 目录 → 所有内核源文件都报：

```
fatal error: finsh.h: No such file or directory
```

#### 2. .cproject 里两个关键位置

| 位置 | 作用 |
|---|---|
| `<sourceEntries>` 的 `excluding="..."` | 决定哪些文件**不编译**（finsh 原本被排除） |
| `<option ... include.paths ...>` 的 `<listOptionValue>` | 决定 `-I` 头文件搜索路径 |

修复 = ① 从排除列表删掉 finsh；② 在 C 和 C++ 两处 include 列表补上 `rt-thread/components/finsh`。

#### 3. 两种构建系统的差异（重要）

| | scons 构建 | Eclipse make 构建 |
|---|---|---|
| 头文件路径来源 | 各目录 `SConscript` 的 `CPPPATH`（会自动合并） | `.cproject` 里手写的 include 路径 |
| 源文件选择 | `SConscript` 按配置宏选择 | `.cproject` 的排除列表 + 目录扫描 |
| 手动加包后 | 通常能自动识别 | 需要手动改 `.cproject` |

这就是"手动往工程里加软件包后，Studio 构建报找不到头文件"的根因——**make 构建不会自动读包的 SConscript**。

---

### 七、复盘：为什么"只使能了 RW007 + MQTT"却出问题

#### 结论：两个问题是独立的，都不是"使能 RW007/MQTT"直接造成的

| 问题 | 真实根因 | 与 RW007/MQTT 使能的关系 |
|---|---|---|
| 串口无法输入 | 新工程模板默认 **msh 没开**（`RT_USING_MSH` 未设置） | 无关，是"组件"菜单的独立选项 |
| `wifi scan` 报 -10 | RW007 **包源码没下载**，驱动没进固件 | 相关，但根因是"勾选≠下载" |
| Stage 1 报错 | RW007 **SPI 时序/30MHz 极限值** | 是 RW007 驱动自身现象，与 msh/MQTT 无关 |

- "串口不能用" 的真相：新建工程时 msh 就是关的，只是正好在需要敲命令时才被发现。
- 教训：**新建工程第一步先确认「组件 → MSH: command shell」勾上**；用软件包时**确认源码真的下载到了 `packages/`**（而不是只看勾选框）。

---

### 八、关键知识点速查

| 知识点 | 一句话总结 |
|---|---|
| 软件包机制 | 勾选写宏，`pkgs --update` 才下载源码到 `packages/` |
| Gitee 镜像 | GitHub 不通时用 `gitee.com/rt-thread-mirror/<包名>` |
| `-RT_EINVAL` (-10) | 参数无效，WLAN 场景=设备未注册（`_sta_mgnt.device` 为空） |
| `RT_USING_MSH` | 关闭它 = 固件没有 shell = 串口只能输出不能输入 |
| RW007 握手 | 主机 MAGIC `67452301/EFCDAB89`，从机应答 `98BADCFE/10325476` |
| Stage 1 报错 | 开机首命令模块未就绪，驱动 3 次重试自愈，无害 |
| `DBG_COLOR` | 开启后日志带 `\033[31m` 等 ANSI 转义，串口工具显示成 "m" |
| `.cproject` | Eclipse make 构建的 include 路径/源文件排除都在这里，手动加包要改它 |
| 构建系统差异 | scons 读 SConscript（自动），make 读 .cproject（手动） |

---

## 第二篇：MQTT 连接成功——从「静默失败」到「双向打通」

> 日期：2026-08-22
> 板卡：STM32F407 星火一号（板载 RW007 SPI WiFi）
> 工程：MQTT_test（RT-Thread 4.1.1 + kawaii-mqtt 软件包）
> 成果：`ka_mqtt` 连接 broker.emqx.io 成功，MQTT 双向收发验证通过

---

### 一、最终结果（先看战果）

#### 1.1 板子串口日志（成功）

```
msh >ka_mqtt
msh >
[I] >> The ID of the Kawaii client is: lwx_board01
[I] >> ../packages/kawaii-mqtt-latest/mqttclient/mqttclient.c:991 mqtt_connect_with_results()... mqtt connect success...
```

- 第 1 行：kawaii-mqtt 客户端 ID（已改为唯一 ID `lwx_board01`）
- 第 2 行：**TCP 连接成功**的标志日志（`mqttclient.c` 第 991 行）——这条日志之前怎么都等不到

#### 1.2 MQTT 双向链路验证结果（实测）

| 方向 | 验证方式 | 结果 |
|------|---------|------|
| 板子 → 电脑 | 电脑订阅 `lwx/pub` | ✅ 每 4 秒收到 `this is a kawaii mqtt test ...` |
| 电脑 → 板子 | 电脑发布到 `lwx/sub` | ✅ 板子串口打印 `sub_topic_handle1` 收到消息 |

板子收到的消息日志：

```
[I] >> ../packages/kawaii-mqtt-latest/test/test.c:41 sub_topic_handle1()...
topic: lwx/sub
message:hello from PC, board are you there?
```

**MQTT 双向链路完全打通。**

---

### 二、问题回顾：为什么之前一直连不上

#### 2.1 现象

运行 `ka_mqtt` 后**只有**这一条日志，再无任何输出：

```
[I] >> The ID of the Kawaii client is: rtthread001
```

既没有 `mqtt connect success...`，**也没有任何报错**——kawaii-mqtt 的连接失败路径是**完全静默**的。

#### 2.2 最让人困惑的矛盾点

| 检查项 | 结果 |
|--------|------|
| `wifi join` + DHCP 拿 IP | ✅ 正常 |
| `ping 192.168.1.1`（网关） | ✅ 正常 |
| `ping 223.5.5.5` / `8.8.8.8`（外网 IP） | ✅ 正常 |
| `ping baidu.com`（DNS 解析） | ✅ 能解析、能通 |
| 电脑 TCP 连接 `broker.emqx.io:1883` | ✅ 255ms 就通 |
| 板子 MQTT TCP 连接 | ❌ **永远失败，且无日志** |

**网络全通、DNS 能解析、电脑能连，唯独板子的 MQTT 起不来**——问题藏在 kawaii-mqtt 的代码路径里。

---

### 三、排查过程（分层测试法）

> 排查工具：PowerShell 直接操作串口 COM21（ST-Link 虚拟串口，115200），自动化发命令、收日志

#### 3.1 第一步：确认 WiFi 和网络层

```
msh> wifi status    → SSID 正确，但 MAC 全 0、Channel -1（僵尸信息，驱动上报缺陷，不影响功能）
msh> ifconfig       → w0: UP LINK_UP，IP 192.168.1.9 ✅
msh> ping 223.5.5.5 → 通（10ms）✅
```

**结论**：WiFi 连接正常、外网可达。

#### 3.2 第二步：确认 DNS

```
msh> ping baidu.com          → 解析到 111.63.65.247，通 ✅
msh> ping broker.emqx.io     → 解析到 35.172.255.228（ICMP 超时是服务器禁 ping，正常）
```

**结论**：DNS 解析正常。

#### 3.3 第三步：抓 TCP 层（关键）

运行 `ka_mqtt` 后立刻抓 `netstat`：

```
Active PCB states:      ← 空的！没有任何 TCP 连接
Active UDP PCB states:
#0 4 0.0.0.0:68 <==> 0.0.0.0:67   ← 只有 DHCP
```

**结论**：`connect` **根本没走到 TCP 阶段**（没有 SYN_SENT 的 PCB），在更早的地方就静默失败了。

#### 3.4 第四步：源码分析（找到静默失败点）

看 kawaii-mqtt 的 `platform_net_socket_connect()`（`platform/RT-Thread/platform_net_socket.c`）：

```c
if (getaddrinfo(host, port, &hints, &addr_list) != 0) {
    return ret;        // ← getaddrinfo 失败，静默返回！没有任何日志！
}
```

**`getaddrinfo`（域名解析）失败 → 静默返回**，这正是"无日志、无 PCB"的原因。

#### 3.5 第五步：为什么 getaddrinfo 失败，而 ping 的 DNS 却正常？

看 lwIP 源码发现**两条完全不同的 DNS 路径**：

| 路径 | 谁在用 | 在哪里执行 | 结果 |
|------|--------|-----------|------|
| `dns_gethostbyname`（直接 API） | `ping` 命令 | 调用线程（tshell，栈 4096） | ✅ 正常 |
| `getaddrinfo` → `netconn_gethostbyname_addrtype`（netconn API） | kawaii-mqtt | **tcpip 线程**（栈只有 1024） | ❌ 失败 |

`getaddrinfo` 的 DNS 查询必须通过 **netconn 机制投递给 tcpip 线程**执行。而查 `list_thread`：

```
tcpip     10  suspend 0x000000f0 0x00000400    90%   ...
```

**tcpip 线程栈 1024 字节，日常使用已达 90%（921 字节）**——再叠加 DNS 查询处理的栈帧 → **栈溢出 → getaddrinfo 静默失败**。

#### 3.6 根因总结

```
kawaii 调 getaddrinfo("broker.emqx.io")
    ↓
走 lwIP netconn API（与 ping 的 dns_gethostbyname 不是同一条路）
    ↓
DNS 查询必须在 tcpip 线程里执行
    ↓
tcpip 线程栈只有 1024 字节，日常已用 90%
    ↓
处理 DNS 查询时栈溢出 → getaddrinfo 返回错误
    ↓
kawaii 失败路径静默 → 只见 ID 日志，再无输出
```

---

### 四、修复方案（全部 Settings 配置侧，零代码改动）

#### 4.1 修改清单

| 配置项 | 原值 | 新值 | 作用 |
|--------|------|------|------|
| `RT_LWIP_TCPTHREAD_STACKSIZE`（lwIP 线程栈） | 1024 | **4096** | 治本：netconn DNS 处理有足够栈 |
| `RT_LWIP_ETHTHREAD_STACKSIZE`（以太网线程栈） | 1024 | **4096** | 保险 |
| `KAWAII_MQTT_HOST`（mqtt host） | broker.emqx.io | **35.172.255.228** | IP 直连，绕过 DNS 解析路径（双保险） |
| `KAWAII_MQTT_USERNAME/PASSWORD` | 123/123 | **空** | 公共 broker 无需认证 |
| `KAWAII_MQTT_CLIENTID`（mqtt client id） | test066 | **lwx_board01** | 唯一，避免公共服务器上被踢 |

#### 4.2 两个关键配置的作用

1. **线程栈 4096**：治本——即使以后用域名（host 填 broker.emqx.io），netconn DNS 也能正常执行
2. **host 填 IP**：治标兜底——`getaddrinfo` 对纯 IP 直接返回，**完全不触发 DNS 查询**，从源头绕开问题

> 两者同时上，一次烧录解决问题的概率最大。

---

### 五、实现过程（从改配置到验证成功）

#### 5.1 修改配置文件

RT-Thread 配置有两处文件，必须**同步修改**（否则 Studio 编译时以 rtconfig.h 为准，而下次 GUI 保存会以 .config 为准，两者不一致会互相覆盖）：

- `.config`（GUI 保存时的源）
- `rtconfig.h`（编译实际用的头文件）

修改示例（rtconfig.h）：

```c
#define RT_LWIP_TCPTHREAD_STACKSIZE 4096    // 原来是 1024
#define RT_LWIP_ETHTHREAD_STACKSIZE 4096    // 原来是 1024
#define KAWAII_MQTT_HOST "35.172.255.228"   // 原来是 "broker.emqx.io"
#define KAWAII_MQTT_CLIENTID "lwx_board01"  // 原来是 "test066"
#define KAWAII_MQTT_USERNAME ""             // 原来是 "123"
#define KAWAII_MQTT_PASSWORD ""             // 原来是 "123"
```

#### 5.2 scons 编译

```powershell
# 环境变量
$env:Path = "…\Python27;…\Python27\Scripts;…\gnu_gcc\arm_gcc\mingw\bin;" + $env:Path
$env:RTT_CC = "gcc"
$env:RTT_EXEC_PATH = "…\gnu_gcc\arm_gcc\mingw\bin"
$env:RTT_ROOT = "D:\app\RT-ThreadStudio\workspace\MQTT_test\rt-thread"

# 在工程目录下编译
scons.bat -j8
```

编译成功（约 1 分钟，仅警告无错误），产物 `rt-thread.elf`（text 248KB）。

#### 5.3 烧录（踩了一个坑）

用 Studio 自带的 STM32CubeProgrammer CLI：

```powershell
STM32_Programmer_CLI.exe -c port=SWD mode=HOTPLUG -w rt-thread.elf -v -rst
```

第一次报错：

```
Error: failed to erase memory
```

**排查**：读选项字节 `-ob displ` → RDP Level 0（无读保护）、WRP 全不活跃 → 不是保护问题。

**解决**：改用 **under-reset 模式**（MCU 停止运行时不干扰 Flash 擦除）：

```powershell
STM32_Programmer_CLI.exe -c port=SWD mode=UR -w rt-thread.elf -v -rst
```

→ `Download verified successfully` ✅

#### 5.4 串口验证（成功）

```
msh >wifi join TP-LINK_0C38 05060606
[I/WLAN.mgnt] wifi connect success ssid:TP-LINK_0C38
[I/WLAN.lwip] Got IP address : 192.168.1.9

msh >ka_mqtt
[I] >> The ID of the Kawaii client is: lwx_board01
[I] >> …mqttclient.c:991 mqtt_connect_with_results()... mqtt connect success...
```

#### 5.5 双向验证

电脑上写 Python 脚本（paho-mqtt 库）扮演第二个客户端：

```python
client.subscribe("lwx/pub")                       # 收板子的消息
client.publish("lwx/sub", "hello from PC...")     # 给板子发消息
```

结果：

```
[PC] RECV topic=lwx/pub payload=b'this is a kawaii mqtt test ...'   ×4  ✅
[板子] sub_topic_handle1: message:hello from PC, board are you there?  ✅
```

---

### 六、关键知识点总结

#### 6.1 lwIP 的两条 DNS 路径（本次核心）

| | `dns_gethostbyname` | `getaddrinfo`（netconn） |
|---|---|---|
| 使用场景 | ping 命令等 | POSIX socket 程序（kawaii-mqtt） |
| 执行线程 | 调用线程直接执行 | **投递给 tcpip 线程** |
| 对 tcpip 栈的依赖 | 低 | **高（栈不够就挂）** |
| 失败表现 | 有返回码 | **可能静默** |

**教训：ping 能解析域名 ≠ 程序里 getaddrinfo 能解析域名。**

#### 6.2 MQTT 连接链路

```
客户端(getaddrinfo 域名解析)
    → socket() 创建
    → connect() TCP 三次握手（SYN → SYN-ACK → ACK）
    → 发 CONNECT 报文
    → 等 CONNACK（Code 0 = 成功）
    → 订阅 SUBSCRIBE → 收到 SUBACK
    → 发布 PUBLISH / 收消息
```

每一环都可能失败，且 kawaii-mqtt 大部分失败路径**不打日志**，排查要靠 `netstat`/`list_thread` 等旁证。

#### 6.3 Client ID 必须唯一（公共 broker 上）

- 同一 broker 上 Client ID 相同 → **互相踢下线**
- 板子 `lwx_board01` ≠ MQTTX `mqtt_720e8ad7` ✅
- 命名建议：`设备名_序号`，如 `lwx_board01`

#### 6.4 公共测试 broker 注意事项

- `broker.emqx.io:1883` 免费公共服务器，**任何人可订阅你的主题** → 不传敏感数据
- 不需要 username/password（留空）
- 服务器禁 ICMP，`ping broker.emqx.io` 超时是**正常现象**，不代表连不上
- 主题带个人前缀（`lwx/...`），避免被他人消息刷屏

---

### 七、踩坑记录

| 坑 | 现象 | 解决 |
|----|------|------|
| tcpip 线程栈太小 | getaddrinfo 静默失败，MQTT 永远连不上 | Settings 里把 lwIP 线程栈 1024 → 4096 |
| 烧录擦除失败 | `failed to erase memory`（不是读保护） | 改用 `mode=UR`（under reset）烧录 |
| GUI 保存覆盖 | Settings 里点保存会用 GUI 值覆盖 .config/rtconfig.h | 先在 GUI 改对再保存；或别保存直接用现有固件 |
| Client ID 冲突 | 两端同 ID → 互踢 | 改成唯一 ID |
| 僵尸 WiFi 状态 | `wifi status` 显示连接但 MAC 全 0 / Channel -1 | rw007 驱动事件上报缺陷，重连可恢复，不影响功能 |

---

### 八、总结

1. **问题表象**：MQTT 连不上且无报错（静默失败最坑）
2. **排查方法**：分层测试（WiFi → 网关 → 外网 → DNS → TCP），配合 `netstat`/`list_thread`/源码阅读
3. **根因**：lwIP `getaddrinfo` 走 netconn 路径，DNS 查询在 tcpip 线程执行，线程栈 1024 太小溢出
4. **修复**：Settings 配置侧改 5 项（线程栈 4096 + IP 直连 + 空认证 + 唯一 Client ID），零代码
5. **验证**：双向 MQTT 消息收发全通 ✅

---

### 九、补充：MQTTX 双向收发最终打通（完整闭环）

> 补充于 2026-08-22：板子与 MQTTX 双向验证全部通过，问题彻底解决。

#### 9.1 板子收到 MQTTX 消息的日志（原文）

MQTTX 往 `lwx/sub` 发布 JSON 消息，板子串口打印：

```
[I] >> -----------------------------------------------------------------------------------
[I] >> ../packages/kawaii-mqtt-latest/test/test.c:41 sub_topic_handle1()...
topic: lwx/sub
message:{
  "msg": "hello"
}
[I] >> -----------------------------------------------------------------------------------

[I] >> -----------------------------------------------------------------------------------
[I] >> ../packages/kawaii-mqtt-latest/test/test.c:41 sub_topic_handle1()...
topic: lwx/sub
message:{
  "msg": "hello-MQTTx"
}
[I] >> -----------------------------------------------------------------------------------
```

收到一条消息，板子就打一条这样的日志（`sub_topic_handle1` 回调）。

#### 9.2 最后的坑：MQTTX 主题方向搞反了

最初板子"收不到"MQTTX 的消息，查了板子半天（线程、订阅、网络全正常），最后发现是 **MQTTX 发布主题填错了**：

| 主题 | 板子用它做什么 | MQTTX 里应该 |
|------|--------------|-------------|
| `lwx/pub` | 板子**发布**（往外发） | **订阅**它 → 收板子的消息 |
| `lwx/sub` | 板子**订阅**（往里收） | **发布**到它 → 给板子发消息 |

**踩坑过程**：MQTTX 订阅和发布都填了 `lwx/pub` → 发布的消息发到了板子"往外发"的那条道 → 板子没订阅 `lwx/pub` → 收不到 → 串口无日志。

> **记忆口诀**：板子发的 `lwx/pub`，你用它来**收**；板子收的 `lwx/sub`，你用它来**发**。

#### 9.3 MQTTX 完整使用总结（测试板子用）

1. **新建连接**：`mqtt://broker.emqx.io`、端口 `1883`、Client ID 用唯一值（≠ 板子的 `lwx_board01`）
2. **订阅** `lwx/pub`（QoS 0）→ 实时收板子每 4 秒发的消息
3. **发布**到 `lwx/sub` → 板子串口打印 `sub_topic_handle1` 日志
4. 转发格式随意（纯文本 / JSON 均可）

#### 9.4 补充观察：GUI 保存会覆盖离线修改

调试中发现：**在 Settings GUI 里点"保存"会把 .config/rtconfig.h 覆盖回 GUI 显示的值**（即使是从命令行手动改过的也会被冲掉），本次实测把 `TCPTHREAD_STACKSIZE` 从 4096 冲回 1024、host 从 IP 冲回域名。当前配置下连接依然成功，但**如果以后又出现连接失败，先检查这两项是否被 GUI 覆盖**。

#### 9.5 最终成果

| 方向 | MQTTX 操作 | 板子反应 | 状态 |
|------|-----------|---------|------|
| 板子 → MQTTX | 订阅 `lwx/pub` | 每 4 秒发 `this is a kawaii mqtt test ...` | ✅ |
| MQTTX → 板子 | 发布到 `lwx/sub` | 打印 `sub_topic_handle1` 收到消息 | ✅ |

**MQTT 双向链路完整打通，学习闭环完成。**

---

## 第三篇：AHT10 温湿度传感器——从编译失败到成功读数

> 日期：2026-08-22
> 板卡：STM32F407 星火一号（板载 AHT10，接 I2C3）
> 工程：MQTT_test（RT-Thread 4.1.1 + aht10 软件包 v2.1.0）
> 成果：`AHT10_Creat_Thread` 命令成功读取温湿度

---

### 一、最终成果（成功日志）

```
msh >AHT10_Creat_Thread
Humi: 44.6
Temp: 25.3
Humi: 44.5
Temp: 25.3
Humi: 44.4
Temp: 25.3
Humi: 44.3
Temp: 25.3
Humi: 44.2
Temp: 25.3
```

- 每 1 秒打印一次温湿度
- 温度 25.3℃、湿度 44%RH——典型室内值，数据合理 ✅

---

### 二、AHT10 是什么

| 项目 | 说明 |
|------|------|
| 类型 | 数字式温湿度传感器（温湿度一体） |
| 接口 | I2C（设备地址 **0x38**） |
| 测量范围 | 温度 -40 ~ +85 ℃；湿度 0 ~ 100 %RH |
| 精度 | 典型 ±0.3℃ / ±2%RH |
| 本板位置 | 星火一号**板载**，接 I2C3 总线 |

**使用方式**：通过 RT-Thread 的 `aht10` 软件包（RT-Thread 官方驱动包）：

```c
aht10_device_t dev = aht10_init("i2c3");          // 初始化，挂到 I2C3
float temp = aht10_read_temperature(dev);          // 读温度
float humi = aht10_read_humidity(dev);             // 读湿度
```

I2C 总线配置（rtconfig.h）：

```c
#define BSP_USING_I2C
#define BSP_USING_I2C3
#define BSP_I2C3_SCL_PIN 64
#define BSP_I2C3_SDA_PIN 65
```

---

### 三、踩坑 1：勾了 AHT10 软件包，编译 40 个报错

#### 3.1 现象

Settings 里勾选 aht10 软件包后编译，报 **40 个错误**，全是这种类型：

```
error: unknown type name 'rt_sensor_t'
error: dereferencing pointer to incomplete type 'struct rt_sensor_device'
error: 'RT_SENSOR_MODE_POLLING' undeclared
```

#### 3.2 根因：传感器框架没启用

aht10 包的代码（`sensor_asair_aht10.c`）依赖 RT-Thread 的**传感器框架**类型（`rt_sensor_t`、`struct rt_sensor_device`、`rt_hw_sensor_register` 等）。框架没使能 → 类型全部不存在 → 报错。

按理说勾选 aht10 包应该**自动**使能框架，但看包 Kconfig：

```kconfig
config PKG_AHT10_USING_SENSOR_V2
    bool "Enable sensor divce framework"
    depends on RT_VER_NUM >= 0x50001     ← ⚠️ 需要 RT-Thread 5.0.1+
    select RT_USING_SENSOR
```

**RT-Thread 4.1.1（RT_VER_NUM = 0x40101）不满足 ≥ 0x50001** → 这个"自动勾选"选项在 Settings 里**被隐藏** → `RT_USING_SENSOR` 没被使能。

#### 3.3 解决（Settings 侧）

```
RT-Thread Components（组件）
    └─ Device Drivers（设备驱动）
        └─ ☑ Using Sensor device drivers    ← 勾选（RT_USING_SENSOR）
```

勾选后自动带出 `RT_USING_PIN`、`RT_USING_SENSOR_CMD`，重新编译 → 40 个错误消失。

> **经验**：`select` 被版本条件（`depends on RT_VER_NUM`）挡住时，相关框架要手动去"设备驱动"里勾。

---

### 四、踩坑 2：AHT10_Creat_Thread command not found

#### 4.1 现象

编译成功了，但串口执行时报错：

```
msh >AHT10_Creat_Thread
AHT10_Creat_Thread: command not found.
```

#### 4.2 根因：命令根本不在工程里

全工程搜索 `AHT10_Creat_Thread` → **0 处匹配**。这个命令名来自例程文件 `AHT10UsePackage.c`（其中 `MSH_CMD_EXPORT(AHT10_Creat_Thread, ...)` 导出），**但这个文件从没被导入过工程**——命令自然不存在。

#### 4.3 解决：导入 .c 文件

把 `AHT10UsePackage.c` 复制到工程的 `applications/` 目录：

```
D:\app\RT-ThreadStudio\workspace\MQTT_test\applications\AHT10UsePackage.c
```

`applications/SConscript` 里是 `src = Glob('*.c')`——**放进这个目录的文件会自动被编译**，`AHT10_Creat_Thread` 命令就被编进固件了。

---

### 五、导入 .c 文件的正确方法（重要）

❌ **不能直接复制粘贴文件夹**——SConscript 不扫描工程外的目录，`.cproject` 也不认识。

✅ **正确步骤**：

1. 把 `.c` 文件放到工程的 `applications/` 目录（`Glob('*.c')` 自动编译）
2. 构建（二选一）：
   - Studio 界面：右键工程 → **刷新(F5)** → 编译（若 make 构建不识别新文件，清理+重建）
   - 命令行：`scons.bat -j8`（最可靠，自动扫描）
3. 配套的 `.h` 头文件：放进 `applications/` 或对应库目录

---

### 六、代码解析：AHT10UsePackage.c（例程）

```c
#include "aht10.h"                        // aht10 软件包的头文件

#define AHT10_I2C_BUS "i2c3"              // AHT10 挂载的总线

void AHT10_Test(void *parameter)
{
    aht10_device_t Dev = aht10_init(AHT10_I2C_BUS);   // 初始化（返回设备句柄）
    if (Dev == RT_NULL) { rt_kprintf("AHT10_init Fail"); return; }

    while (1)
    {
        float Humi = aht10_read_humidity(Dev);        // 读湿度
        float Temp = aht10_read_temperature(Dev);     // 读温度
        rt_kprintf("Humi: %d.%d\n", (int)Humi, (int)(Humi * 10) % 10);
        rt_kprintf("Temp: %d.%d\n", (int)Temp, (int)(Temp * 10) % 10);
        rt_thread_mdelay(1000);                       // 每 1 秒一次
    }
}

void AHT10_Creat_Thread(void)                          // 创建线程
{
    AHT10 = rt_thread_create("AHT10", AHT10_Test, RT_NULL, 2048, 25, 5);
    if (AHT10 != RT_NULL) rt_thread_startup(AHT10);
}
MSH_CMD_EXPORT(AHT10_Creat_Thread, This Function will creat a AHT10 thread.);
```

**要点**：

| 点 | 说明 |
|----|------|
| `MSH_CMD_EXPORT(AHT10_Creat_Thread, ...)` | 把函数导出为 msh 命令，串口直接输入函数名执行 |
| `rt_thread_create` | 创建线程（栈 2048、优先级 25、时间片 5） |
| `aht10_init("i2c3")` | 在 I2C3 总线上查找并初始化 AHT10（返回 NULL = 失败） |
| `%d.%d` 打印 | 整数部分 + 小数位（避免用 %f，省资源；需要开 rt_vsprintf_full 才能用 %f） |

---

### 七、同一文件夹里另外两个 .c 是什么

| 文件 | 是什么 | 能不能直接用 |
|------|--------|------------|
| `AHT10UsePackage.c` | **用 aht10 软件包**的示例（当前导入的） | ✅ 已用 |
| `AHT10NotUsePackage.c` | **不用软件包**的 AHT10 驱动（自己写 I2C 底层操作，理解协议用） | ⚠️ 二选一，不能和包同用 |
| `MQTT_Example.c` | **阿里云 IoT SDK** 的 MQTT 示例（`dev_sign_api.h`/`mqtt_api.h` 是阿里云 SDK 的 API） | ❌ 需要阿里云 IOT 包，不是 kawaii-mqtt |

---

### 八、知识总结

1. **传感器框架依赖链**：aht10 包 → 依赖 `RT_USING_SENSOR`（框架）→ 框架没开 → 40 个类型错误
2. **Kconfig 版本条件**：`depends on RT_VER_NUM >= 0x50001` 不满足时选项隐藏，`select` 自动使能失效
3. **MSH_CMD_EXPORT**：函数导出为串口命令的关键宏，命令不存在=代码没编入
4. **Glob('*.c')**：applications 目录放入即编译，是导入 .c 文件的标准位置
5. **I2C 设备使用三步**：总线配置（Settings）→ `xxx_init("i2c3")` → 读数据

---

## 第四篇：文件系统与综合实验——AHT10+MQTT+落盘+LED 指令

> 日期：2026-08-23
> 板卡：STM32F407 星火一号（板载 W25Q64 SPI NOR Flash，接 SPI2 总线）
> 工程：MQTT_test（RT-Thread 4.1.1）
> 成果：AHT10 采集上传、文件系统落盘、云端指令控灯、软件包应用 全部验证通过

---

### 一、为什么嵌入式需要文件系统

板载 **W25Q64** 是一颗 8MB 的 SPI NOR Flash。裸操作痛点与文件系统的对比：

| 裸操作 Flash 的问题 | 文件系统解决 |
|---|---|
| 必须整块擦除（先搬走不删的数据） | 自动管理 |
| 靠手工算偏移地址 | 路径名 `open("/fal/test.txt")` |
| 数据混在一起，无"文件"概念 | 文件/目录/大小/时间戳 |
| 断电可能丢数据 | FAT 基本可靠性 + 持久化 |

**一句话：文件系统 = Flash 的"管家"，把难用的 Flash 变成好用的"文件夹"。**

### 二、文件系统分层架构（W25Q64 链路）

```
应用层：open() / read() / write() / mkdir()        ← POSIX 接口
  ↓
DFS（虚拟文件系统）：统一路径 / 管理挂载
  ↓
FatFS (elm_fat)：FAT 格式，文件/目录逻辑
  ↓
BLK 块设备：把分区包装成"可读写的块"（filesyst）
  ↓
FAL（Flash 抽象层）：划分逻辑分区（分区表见 fal_cfg.h）
  ↓
SFUD（串行 Flash 通用驱动）：识别/擦写芯片
  ↓
SPI 控制器(SPI2) → W25Q64（8MB SPI NOR Flash）
```

FAL 分区表（fal_cfg.h，BSP 预置）——本次实验用 **filesystem 分区（偏移 5MB，大小 3MB）**：

| 分区名 | 起点 | 大小 | 用途 |
|---|---|---|---|
| easyflash | 0 | 512KB | EasyFlash 参数 |
| download | 512KB | 1MB | OTA 下载 |
| wifi_image | 1.5MB | 512KB | WiFi 固件 |
| font | 2MB | 3MB | 字库（只读） |
| **filesystem** | 5MB | 3MB | **文件系统（本次挂载）** |

### 三、Settings 配置（文件系统组件）

**BSP 板级配置（硬件/Board 分类）**：

| 勾选项 | 配置名 | 自动带出 |
|---|---|---|
| ☑ Enable File System | `BSP_USING_FS` | DFS + RomFS |
| ☑ Enable FAL filesystem partition base on W25Q64 | `BSP_USING_FLASH_FATFS` | FAL + SPI_FLASH + SFUD |
| ✔ Enable filesystem auto mount（默认勾） | `BSP_USING_FLASH_FS_AUTO_MOUNT` | 开机自动挂载 /fal |

**组件配置（必须手动勾！）**：

| 勾选项 | 配置名 | 为什么手动 |
|---|---|---|
| ☑ Enable elm-chan fatfs | `RT_USING_DFS_ELMFAT` | BSP 的 Flash 开关**不自动带出** elm_fat |
| ⚠️ **Maximum sector size to be handled. = 4096** | `RT_DFS_ELM_MAX_SECTOR_SIZE` | FatFs 默认 512，而 FAL 块设备扇区是 4096（W25Q64 擦除粒度），**不匹配必挂载失败** |

### 四、Shell 命令实操（实测输出）

```
msh > list_device
  W25Q64   Block Device     ← SFUD 探测成功
  filesyst Block Device     ← FAL 块设备（filesystem 分区）
  spi20 / spi2 / i2c3 ...

msh > fal probe             → 打印分区表（easyflash/download/wifi_image/font/filesystem）

msh > mkfs -t elm filesyst  → 格式化（⚠️ 清空分区数据，只做一次）
msh > mount filesyst /fal elm   → 挂载到 /fal（块设备名+挂载点+文件系统类型，3 个参数！）
msh > ls /fal               → 看文件
msh > df /fal               → disk free: 2.9 MB [761 block, 4096 bytes per block]
msh > cat /fal/Data.txt     → 直接看文件内容
```

> `mount` 命令 4.1.1 必须有 3 个参数：`mount <device> <mountpoint> <fstype>`（少了 fstype 会报用法错误）。

### 五、综合实验：RSOC_App.c（任务 ① ② ③）

**功能设计**（applications/RSOC_App.c，msh 命令 `rsoc_app`）：

```c
rsoc_app()                       // msh 启动命令（带防重复启动保护）
  └─ rsoc_app_entry() 线程（栈 4096，优先级 20）
       ├─ ensure_mount()        // /fal 未挂载则 dfs_mount 兜底
       ├─ aht10_init("i2c3")    // AHT10 初始化
       ├─ mqtt_connect()        // 连接 broker.emqx.io
       ├─ mqtt_subscribe(device/down, cmd_handle)   // 订阅指令主题
       └─ while(1)：
            ├─ 读 AHT10 温湿度
            ├─ mqtt_publish(device/up, JSON)        // 上传 EMQX
            ├─ save_to_data_file()                  // 追加写 /fal/Data.txt
            └─ delay 5s

cmd_handle()                     // 收到云端指令 → 翻转 LED(PF12)
```

**实测成功日志（完整）**：

```
[AHT10] init ok on i2c3
[MQTT] connect success: broker.emqx.io
[MQTT] subscribed: rsoc/liwenxuan/device/down
[MQTT] published: {"temperature":27.8,"humidity":49.9,"count":1}
[FS] saved: Temp： 27.8 ; Humi： 49.9 ; Count： 1
（每 5 秒循环，count 递增；Windows 串口终端把全角冒号显示成 ??? 是编码显示问题，文件内容正确）

[CMD] received: {"cmd":"toggle","n":0}
[CMD] LED toggled -> ON          ← 云端指令 → 红灯亮！
[CMD] received: {"cmd":"toggle","n":1}
[CMD] LED toggled -> OFF         ← 红灯灭！
```

**Data.txt 内容**（重启持久化验证通过）：

```
Temp： 27.5 ; Humi： 48.1 ; Count： 25   ← 上次上电的数据（重启不丢！）
...
Temp： 27.2 ; Humi： 48.9 ; Count： 1    ← 新电从 1 重新计数
Temp： 27.3 ; Humi： 48.8 ; Count： 2
```

### 六、踩坑与修复

| 坑 | 现象 | 根因 | 解决 |
|---|---|---|---|
| **mkfs/mount 失败** | `format error` / mount failed | FAL 块设备扇区 **4096** vs FatFs `FF_MAX_SS=512` | `RT_DFS_ELM_MAX_SECTOR_SIZE` 改 **4096** |
| **SFUD 探测间歇失败** | 约 1/3 概率 W25Q64 probe 失败 | W25Q64 与 RW007 **共享 SPI2 总线**，初始化竞争 | 三层防护：① 探测频率 50M→10M；② 探测失败**重试 5 次**；③ read/write/erase 判空防崩溃（原 assert 直接死机） |
| **自动挂载偶发失败** | 开机 /fal 没挂上，`open failed` | 时序竞争 | rsoc_app 里 `ensure_mount()` 兜底（dfs_mount 重试） |
| **LED 一直闪、指令看不出** | 指令发了 LED 看不出翻转 | main.c 的 LED 闪烁循环每 500ms 翻转 PF12，**覆盖了指令翻转** | main.c 去掉闪烁，LED 交给指令控制 |
| **MQTTX 收不到** | MQTTX 无数据 | ① 订阅主题还是旧 `lwx/pub`（应为 `rsoc/liwenxuan/device/up`）；② kawaii 运行时订阅丢失 | 改订阅主题；连接后立即发指令/重启恢复 |
| **Data.txt 出现 BAD file** | `ls /fal` 数百个 `BAD file` | 早期 512 扇区时代写坏的残留 | 重新 `mkfs -t elm filesyst` 格式化 |

### 七、最终成果

| 任务 | 实现 | 状态 |
|---|---|---|
| ① AHT10 采集 + 上传 EMQX | `rsoc/l...` RSOC_App.c：AHT10 + kawaii publish | ✅ MQTTX 实时收 `{"temperature":27.8,"humidity":49.9,"count":N}` |
| ② 文件系统 + Data.txt | W25Q64 filesystem 分区挂载 `/fal` | ✅ `Temp：X ; Humi：X ; Count：N` 追加 + 持久化 |
| ③ 云端指令 → LED 翻转 | 订阅 device/down → cmd_handle → PF12 | ✅ `[CMD] LED toggled -> ON/OFF` |
| ④ 熟悉软件包 | aht10 / kawaii-mqtt / rw007 / sfud / fal / elm_fat | ✅ 全部实战 |

**MQTTX 配置**：订阅 `rsoc/liwenxuan/device/up`（收数据）；发布到 `rsoc/liwenxuan/device/down`（发指令）；broker.emqx.io:1883；Client ID 唯一。
