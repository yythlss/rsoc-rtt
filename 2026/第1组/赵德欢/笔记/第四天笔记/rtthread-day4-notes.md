# RT-Thread 大学生夏令营 Day4 · 设备驱动框架学习笔记

## 一、IO 设备驱动框架

### 1.1 背景与动机

- 不同厂商的 SPI / I2C 等接口 **API 不统一**，更换芯片时需重复开发，降低效率、难以复用。
- RT-Thread 通过**抽象层将上层应用与底层硬件驱动解耦**：上层用统一 API（`open / read / write / control / close`）操作设备，底层适配各厂商驱动。
- 核心优势：上层开发成本低、易跨 MCU 移植、驱动可复用、减少"造轮子"。
- 代价：框架更复杂、Flash 占用略有增加——但优点远大于缺点。

### 1.2 框架的分层架构

```
应用层 API
   ↓
IO 设备管理层（rt_device_xxx）
   ↓
设备驱动框架层（SPI / I2C / GPIO / Pin ...）
   ↓
设备驱动层（各厂商 HAL / LL 库实现）
   ↓
硬件
```

- 所有设备继承自 `rt_device` 基类，采用面向对象思路。
- **核心纽带是 `rt_device_ops` 结构体**：封装 `init / open / read / write / control / close` 等函数指针，下层驱动实现这些接口，上层通过统一 API 调用——它是"上层统一调用、下层各自实现"的解耦点。
- 演进：从早期仅统一 SPI API → 统一为 IO 设备层 → 形成 SPI / I2C / GPIO 等多设备并行的架构。
- **并行结构**：既可走统一 IO 层调用，也可直接使用各设备专用接口（如 SPI 专用读写 / 传输函数）。

### 1.3 设备创建、注册与使用流程

| 步骤 | 关键 API / 操作 |
| :-- | :-- |
| 创建设备 | `rt_device_create(type, ...)` |
| 注册到 IO 管理系统 | `rt_device_register(device, "设备名", 打开标志)` |
| 查找设备 | `rt_device_find("设备名")` → 返回设备句柄 |
| 初始化 | `rt_device_init(handle)` |
| 打开设备 | `rt_device_open(handle, 读写模式)` |
| 读写 | `rt_device_read / write(handle, ...)` |
| 控制 | `rt_device_control(handle, ...)` |
| 关闭 | `rt_device_close(handle)` |

- 注册成功后，终端执行 `list device` 可查看已注册字符设备。
- **自动初始化机制**：系统在启动时自动执行被 `INIT_*` 宏标记的设备初始化函数，无需手动调用。
- **错误码规范**：成功返回 `RT_OK`（正数），失败返回负值错误码。

### 1.4 关键机制与踩坑要点

- **回调机制**：可通过 `rt_device_set_xxx_callback` 注册接收 / 发送回调函数，用于中断 / DMA 非阻塞场景下的数据通知与"发送完成"回执。
- **打开模式**：若需接收回调，必须以**中断或 DMA 方式**打开设备，避免阻塞。
- **隐式初始化**：系统会检测设备初始化标志位，未初始化时自动触发 `init` 并置位，防止重复初始化（防重入安全锁）。
- **典型坑**：读写失败常因"未先 `open` 设备"导致引用计数未增加、设备未真正打开——可先 `list device` 确认状态，补上 `open` 后重新编译下载即可打通。
- **日志级别**：调试时切勿把普通信息误用 `LOG_E`（会被识别为错误、干扰排查），应使用 `LOG_I`；**中断上下文中禁用阻塞式 `LOG_I`**，须开启 `ulog` 的 ISR / 异步输出功能。

---

## 二、GPIO 设备驱动开发实践

### 2.1 GPIO 基本特性

- MCU 引脚具备**功能复用**特性，可配置为电源、时钟、通用 IO 等。
- 支持多种中断触发模式（上升沿 / 下降沿 / 双边沿等）。
- 引脚模式多种（输出、输入上拉 / 下拉、开漏输出等），需根据电路特性精准选择。

### 2.2 GPIO 主要 API

通过设备管理接口操作 GPIO（`rt_pin_xxx`）：

| 函数 | 作用 |
| :-- | :-- |
| `rt_pin_mode(pin, mode)` | 设置引脚模式（输入 / 输出 / 上拉 / 下拉 / 开漏） |
| `rt_pin_read(pin)` | 读引脚电平（仅高 / 低两种状态） |
| `rt_pin_write(pin, value)` | 写引脚电平 |
| `rt_pin_attach_irq(pin, trigger, callback, args)` | 绑定中断回调函数 |
| `rt_pin_detach_irq(pin)` | 解绑中断回调 |

> `rt_pin_mode` 常用模式宏：`PIN_MODE_OUTPUT`（推挽输出）、`PIN_MODE_INPUT`（浮空输入）、`PIN_MODE_INPUT_PULLUP`（上拉输入）、`PIN_MODE_INPUT_PULLDOWN`（下拉输入）、`PIN_MODE_OUTPUT_OD`（开漏输出）。

### 2.3 按键中断实验要点

- 按键按下为**低电平**，故配置引脚为**上拉输入模式**，确保未按下时读取高电平。
- 注册回调函数并设定**下降沿触发**，实现按键按下响应。
- **按键消抖**：硬件通常无消抖，建议通过软件延时或引入 RT-Thread 的 `button` 软件包（支持单击 / 双击等），参考其 README / 示例即可快速上手。

### 2.4 引脚编号计算

- 每组 GPIO 含 16 个引脚；如引脚编号 `37` = 前两组（32 个）+ C 组第 5 个引脚（GPIOC5）。
- 通用公式：**`pin = 组号 × 16 + 组内序号`**（A 组 = 0，B 组 = 1，C 组 = 2 …），例如 C5 → `2 × 16 + 5 = 37`。
- 采用通用 API 编写应用，可轻松迁移至其他 MCU。

### 2.5 PIN 设备与驱动框架的对接关系（调用链）

RT-Thread 把 GPIO 也纳入了 IO 设备驱动框架，以"Pin 设备"的形式统一管理。其对接分三层：

| 层级 | 角色 | 关键结构 / 函数 |
| :-- | :-- | :-- |
| 应用层 | 应用调用统一的 Pin API | `rt_pin_mode` / `rt_pin_write` / `rt_pin_read` / `rt_pin_attach_irq` |
| Pin 框架层 | 中转并调用底层实现 | `struct rt_device_pin`（内含 `struct rt_pin_ops *ops`） |
| 板级驱动层 | 实现具体硬件操作 | `drv_gpio.c` 中实现 `rt_pin_ops` 各函数 → HAL / LL 库 |

- **对接核心**：板级驱动用 `rt_device_pin_register("pin", &pin_ops, ...)` 把实现了 `rt_pin_ops` 函数指针集合的驱动注册为名为 `"pin"` 的设备；`rt_pin_ops` 包含 `pin_mode` / `pin_write` / `pin_read` / `pin_attach_irq` / `pin_detach_irq` / `pin_irq_enable` 等函数指针。
- **调用链（以设置引脚模式为例）**：

  ```text
  rt_pin_mode(pin, mode)
     → Pin 框架查找 "pin" 设备（全局句柄）
     → 调用 device->ops->pin_mode(device, pin, mode)
     → 板级 stm32_pin_mode()        // drv_gpio.c
     → HAL_GPIO_Init() / LL 库
     → 硬件寄存器
  ```

- **中断对接**：`rt_pin_attach_irq()` 把用户回调登记到 Pin 框架的中断回调表；当 GPIO 外部中断（EXTI）触发时，板级 `GPIO_IRQHandler` 进入中断上下文，经 `rt_interrupt_enter()` 后由框架调用已登记的回调函数，实现"按键按下 → 回调响应"的闭环。

> 这正是"统一设备 API → 框架层 ops → 驱动层 HAL"解耦思想的体现：应用只认 `rt_pin_xxx`，换 MCU 只需重写板级 `drv_gpio.c` 的 `rt_pin_ops` 实现，上层代码无需改动。

---

## 三、I2C 外设开发

### 3.1 I2C 协议要点（飞利浦 1980 年提出）

- 三种速率模式：低速 / 标准 / 快速模式。
- **起始位**：SCL 高电平时 SDA 由高跳低；**停止位**：SCL 高电平时 SDA 由低跳高。
- **应答机制**：从机拉低 SDA 表示应答（ACK），否则为非应答（NACK）。
- **地址**：一个字节中前 7 位为设备地址，最低 1 位为读写标志（0 写 / 1 读）；常见为 7 位地址，也存在 10 位扩展。

### 3.2 RT-Thread 中的 I2C 使用

- I2C 为总线结构，一条总线可挂载多个设备（地址不冲突即可），`list device` 可查看 I2C / SPI 总线设备。
- 访问流程：
  1. `rt_device_find("i2c总线名")` 查找总线设备；
  2. 构建 `struct rt_i2c_msg` 消息结构体（含设备地址、读写标志、数据长度、缓冲区指针）；
  3. 调用传输函数完成读写（地址会自动左移 1 位并补读写位）。
- 读写通过修改消息结构体的 `flag`（读 / 写模式）实现；单字节长度设为 1，多字节传结构体数组。
- 注意事项：读写操作互斥，不可同时进行；可设置"忽略应答"等标志。

`rt_i2c_msg` 结构体关键字段：

| 字段 | 含义 |
| :-- | :-- |
| `addr` | 从机 7 位地址（框架自动左移并补读写位，应用时填 7 位即可） |
| `flags` | 读写标志（`RT_I2C_RD` 读 / `RT_I2C_WR` 写） |
| `len` | 数据字节数 |
| `buf` | 数据缓冲区指针 |

### 3.3 硬件 I2C vs 软件模拟 I2C

- 软件模拟通常带 `sw` 前缀；硬件 I2C 需先配置对应 GPIO 引脚再启用。
- 当前部分芯片支持包版本较老可能不支持硬件 I2C，可先用软件模拟兜底；启用硬件 I2C 需在 Kconfig / board.h 中开启对应支持。
- 软件 I2C 驱动已简化：只要设备对接了驱动框架，一般无需额外编写软件 I2C 驱动。

---

## 四、SPI 外设开发

### 4.1 SPI 特点

- 同步、全双工通信，速率较高。
- 系统分为**总线控制器**与挂载的**子设备**，每个设备有独立片选（CS）引脚。
- 使用流程：先注册 SPI 总线 → 再挂载 SPI 设备 → `rt_device_find` 查找 → 打开 → 调用框架 API 收发。

### 4.2 设备命名规则

- 总线：`spix`（如 `spi2`）；
- 挂载设备：`spi x.y`（x = 总线编号，y = 设备序号），如 `spi2.0` 表示 SPI2 总线上的第 0 号设备；
- 片选引脚信息通过 `user_data` 参数传递（记录具体 GPIO 编号，如 PB12）。

### 4.3 配置与使用要点

- 获取 SPI 句柄后需配置：工作模式、数据宽度、最大频率（Hz）、字节序（MSB / LSB）、主从模式、时钟相位（采样边沿）、片选极性（默认低有效，部分器件需高有效）。
- **四种工作模式**（由时钟极性 CPOL 与时钟相位 CPHA 组合），常用**模式 0 与模式 2**（均在时钟第一个边沿采样数据）：

| 模式 | CPOL | CPHA | 采样边沿 |
| :-- | :-- | :-- | :-- |
| 0 | 0 | 0 | 第一个边沿（常用） |
| 1 | 0 | 1 | 第二个边沿 |
| 2 | 1 | 0 | 第一个边沿（常用） |
| 3 | 1 | 1 | 第二个边沿 |

- SPI 全双工本质：每次发送同时接收数据；`rt_spi_send` / `rt_spi_receive` 底层均封装了双向传输。
- 三线模式可简化传输（仅需三条线）。

---

## 五、工程与工具链要点

### 5.1 两种工程创建方式对比

| 维度 | 基于主线导入 | 基于芯片创建 |
| :-- | :-- | :-- |
| 配置方式 | 直接在 config 中配置 | 需新建工程并手动拷贝引脚初始化函数 |
| SPI 等外设 | 自动配置 | 须在 library / menuconfig 中手动使能 |
| 开发体验 | 更省事 | 链路更长，手动搬运易埋隐患 |

### 5.2 CubeMX / Qmax 工具

- 在 Qmax 中配置引脚后，会自动生成对应引脚初始化代码，设备驱动框架直接调用这些生成的函数（最终调到 HAL 库）。
- 图形化界面降低手动编写底层代码出错概率；配置 CubeMX 路径本质是让系统识别 Qmax 安装位置（类似环境变量）。
- 工具会生成大量文件，实际只需关注核心配置文件（如时钟、SPI / ADC 引脚初始化相关）。

### 5.3 CubeMX 与 RT-Thread Studio 的两种转换方法

RT-Thread Studio 内置了对 STM32CubeMX 的支持。所谓"两种转换方法"，本质是把 CubeMX 的图形化配置与 RT-Thread Studio 工程互相打通的两条路径——二者底层机制相同（都由 CubeMX 生成 `cubemx/` 代码，再由 RT-Thread 的构建系统编译链接），区别仅在**起点**。

| 方法 | 起点 → 终点 | 典型场景 |
| :-- | :-- | :-- |
| 方法一：CubeMX → Studio | 已有 `.ioc`，搬进 Studio 接管 | 手头有 CubeMX 工程，想接入 RT-Thread |
| 方法二：Studio → CubeMX | 已有 Studio 工程，用 CubeMX 插件增改 | 从 Studio 起步，中途要加 / 改外设 |

**方法一：CubeMX 工程导入 RT-Thread Studio**

1. 在 Studio 中新建 / 打开 RT-Thread 工程（基于芯片或 BSP），把 `.ioc` 放到 `board/CubeMX_Config/`（BSP 通常已自带默认 `.ioc`）。
2. 在 Studio 设置里配置 CubeMX 可执行文件路径（首次需指定，相当于把 CubeMX 安装位置告诉 Studio）。
3. 右键 `.ioc` → **Open with CubeMX**（或 Studio 的 CubeMX 菜单）启动 CubeMX，图形化配置引脚、时钟、外设。
4. 点击 **Generate Code**：CubeMX 把代码生成到工程的 `cubemx/`（含 `main.c`、`gpio.c`、`spi.c`、`MX_GPIO_Init()` / `MX_SPI2_Init()` 等 `HAL_*` 初始化函数）。
5. Studio 的构建系统（SConscript）自动把 `cubemx/` 纳入编译链接。**注意**：CubeMX 生成的 `main()` 不是系统入口（RT-Thread 用自己的入口），真正被调用的是那些 `MX_*` 初始化函数——它们在 `rt_hw_board_init()`（或 `board.c`）中被调用。
6. 初始化完成后，上层即可用 `rt_pin_*` / `rt_device_*` 设备框架操作外设。

**方法二：在 RT-Thread Studio 工程内用 CubeMX 插件增改**

1. 在 Studio 工程里找到 `board/CubeMX_Config/` 下的 `.ioc`（Studio 建 BSP 工程时会预置）。
2. 右键 `.ioc` → **Open with CubeMX** 启动 CubeMX。
3. 勾选 / 修改外设引脚与时钟 → **Generate Code** → 关闭 CubeMX。
4. 回到 Studio 直接 **Build**，构建系统重新编译 `cubemx/` 新生成的 `MX_*` 初始化。
5. 在应用 / board 初始化里调用新增的 `MX_*` 函数，并用设备框架注册、使用。

> 两种方法共用同一套机制：CubeMX 只负责生成"底层 HAL 初始化"，RT-Thread 设备框架叠加其上；改引脚 / 时钟只需重开 CubeMX 重新 Generate，无需手写 `board.h`。与笔记 5.2 的 Qmax 工具作用等价（Qmax 是 RT-Thread 自家的图形化引脚配置工具，可作为 CubeMX 替代）；若 CubeMX 不可用，可退回"手动在 `board.h` 写 `MX_*` / HAL 初始化"或改用软件模拟。

### 5.4 弱符号机制

- 部分底层函数采用**弱符号（`__weak`）**定义，链接器会优先使用用户 / board.h 中的重定义实现。
- 即在 board.h 中重新实现该函数，最终会调用 board.h 内版本——框架已预留接口，开发者按需重写即可定制。

### 5.5 自动初始化机制

- 系统在 Flash 特定地址写入初始化函数指针，启动时遍历执行，统一收口管理（空间换时间）。
- 串口、Pin 脚等需提前初始化以供中断使用；SPI 等其余外设依赖该自动初始化机制。
