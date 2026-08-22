# RTT-学习日志-Day4

## RT-Thread I/O 设备框架

###  为什么要有框架（驱动碎片化问题）

- 不同厂商同一外设 API 完全不同：GD `spi_i2s_data_transmit` / ST `HAL_SPI_Transmit` / NXP `LPSPI_MasterTransferBlocking`。
- 直接基于厂商 API 写设备驱动 → **换芯片就得重写**、学习成本高、代码复用率低。
- 解决思路：**驱动代码与设备代码分离 + 统一 API**。

```
应用层 → I/O 设备框架（统一 API）→ 对接驱动(各厂商) → 硬件
                ↑
          设备驱动（SPI/I2C/GPIO 等，可做成 Package 复用）
```

- 好处：换 MCU 只改对接驱动；设备驱动可入库复用（防反复造轮子）。

###  设备分类：字符设备 vs 块设备

| | 字符设备 | 块设备 |
|---|---|---|
| 访问方式 | 顺序、按字节/字符读写 | **可随机访问**，按块（512B 倍数）读写 |
| 例子 | 键盘、串口、LCD 模拟 | 硬盘、SD 卡、Flash |
| 根本区别 | 不能随机访问 | 可以随机访问 |

- 分类目的：MSH 可重定向到任意字符设备；FATFS 依赖块设备；同类设备统一控制。

## I/O 设备 API（核心）

###  设备生命周期 API

| 操作 | 函数 |
|---|---|
| 创建设备 | `rt_device_t rt_device_create(int type, int attach_size)` |
| 销毁设备 | `void rt_device_destroy(rt_device_t dev)` |
| 注册设备 | `rt_err_t rt_device_register(rt_device_t dev, const char *name, rt_uint8_t flags)` |
| 注销设备 | `rt_err_t rt_device_unregister(rt_device_t dev)` |
| 查找设备 | `rt_device_t rt_device_find(const char *name)` |

- 常用 flags：`RT_DEVICE_FLAG_RDWR`（可读写）、`RT_DEVICE_FLAG_RDONLY`、`RT_DEVICE_FLAG_WRONLY`。
- 设备类型：`RT_Device_Class_Char`（字符）、`RT_Device_Class_Block`（块）、`RT_Device_Class_Pin`（GPIO）、`RT_Device_Class_Sound`、`RT_Device_Class_Graphic` 等（见 `rtdef.h` 的 `rt_device_class_type`）。

### 访问设备 API（应用层）

| 操作 | 函数 |
|---|---|
| 初始化 | `rt_err_t rt_device_init(rt_device_t dev)` |
| 打开 | `rt_err_t rt_device_open(rt_device_t dev, rt_uint16_t oflags)` |
| 关闭 | `rt_err_t rt_device_close(rt_device_t dev)` |
| 控制 | `rt_err_t rt_device_control(rt_device_t dev, int cmd, void *arg)` |
| 读 | `rt_size_t rt_device_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)` |
| 写 | `rt_size_t rt_device_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)` |
| 数据到达回调 | `rt_err_t rt_device_set_rx_indicate(rt_device_t dev, rt_err_t (*rx_ind)(rt_device_t, rt_size_t))` |
| 发送完成回调 | `rt_err_t rt_device_set_tx_complete(rt_device_t dev, rt_err_t (*tx_done)(rt_device_t, void *))` |

- 打开标志：`RT_DEVICE_OFLAG_RDWR` / `RT_DEVICE_OFLAG_RDONLY` / `RT_DEVICE_OFLAG_WRONLY`。
- 应用层访问流程：`rt_device_find` → `rt_device_open` → `rt_device_read/write/control` → `rt_device_close`。

### OPS 接口（驱动层，实验二）

驱动要实现 6 个函数指针并挂到设备上，框架通过这些指针调用驱动：

| OPS | 函数原型 |
|---|---|
| init | `rt_err_t (*init)(rt_device_t dev)` |
| open | `rt_err_t (*open)(rt_device_t dev, rt_uint16_t oflag)` |
| close | `rt_err_t (*close)(rt_device_t dev)` |
| read | `rt_size_t (*read)(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)` |
| write | `rt_size_t (*write)(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)` |
| control | `rt_err_t (*control)(rt_device_t dev, int cmd, void *args)` |

核心注册代码（`test_drv.c`）：

```c
int rt_drv_test_init(void)
{
    rt_device_t test_dev = rt_device_create(RT_Device_Class_Char, 0);
    if (!test_dev) { LOG_E("create failed"); return -RT_ERROR; }

    test_dev->init    = drv_test_init;    /* 挂接 6 个 OPS */
    test_dev->open    = drv_test_open;
    test_dev->close   = drv_test_close;
    test_dev->read    = drv_test_read;
    test_dev->write   = drv_test_write;
    test_dev->control = drv_test_control;

    if (rt_device_register(test_dev, "test_drv", RT_DEVICE_FLAG_RDWR) != RT_EOK)
        return -RT_ERROR;
    return RT_EOK;
}
INIT_BOARD_EXPORT(rt_drv_test_init);   /* 自动初始化，无需手动调用 */
```

- 应用验证：`rt_device_find("test_drv")` → open/write/read/control/close，串口打印对应日志。
- `INIT_BOARD_EXPORT`：编译期放进特殊段，启动时按序自动执行（BOARD → DEVICE → APP）。

## GPIO（PIN 设备）

- 芯片引脚分电源/时钟/控制/IO；IO 分 **GPIO（通用输入输出）** 与功能复用（SPI/I2C/UART）。
- GPIO 可编程中断，5 种触发模式：上升沿、下降沿、双沿、高电平、低电平。

| 函数 | 作用 |
|---|---|
| `rt_pin_mode(pin, mode)` | 设置引脚模式（`PIN_MODE_INPUT_PULLUP` 等） |
| `rt_pin_write(pin, value)` | 输出电平（`PIN_HIGH` / `PIN_LOW`） |
| `rt_pin_read(pin)` | 读取电平 |
| `rt_pin_attach_irq(pin, mode, hdr, args)` | 绑定中断回调（`PIN_IRQ_MODE_FALLING` 下降沿等） |
| `rt_pin_irq_enable(pin, enabled)` | 使能/禁止中断（`PIN_IRQ_ENABLE`） |
| `rt_pin_detach_irq(pin)` | 脱离中断回调 |

- 按键中断流程：`rt_pin_mode` 设输入上拉 → `rt_pin_attach_irq` 绑回调 → `rt_pin_irq_enable` 使能。
- 可勾选 **FlexibleButton** 按键库简化按键处理。

### PIN 设备如何对接（三层结构）

PIN 设备是"专用设备框架"的典型例子，对接分三层：

```
应用层：rt_pin_mode / rt_pin_write / rt_pin_read / rt_pin_attach_irq ...
        │  （统一 API，与厂商无关）
        ▼
框架层 pin.c：struct rt_pin_ops（7 个函数指针）+ _hw_pin 设备 + rt_device_pin_register()
        │  （内核提供，负责转发）
        ▼
对接层 drv_gpio.c：实现 _stm32_pin_ops，用 HAL 库操作 GPIO 寄存器
        │  （厂商/BSP 提供）
        ▼
硬件（STM32 GPIO 寄存器）
```

**① 框架层定义接口**（`pin.c`）：用 `struct rt_pin_ops` 定义 7 个函数指针，并提供注册函数：

```c
struct rt_pin_ops {
    void (*pin_mode)(rt_device_t, rt_base_t, rt_base_t);      /* 设模式 */
    void (*pin_write)(rt_device_t, rt_base_t, rt_base_t);     /* 写电平 */
    int  (*pin_read)(rt_device_t, rt_base_t);                 /* 读电平 */
    rt_err_t (*pin_attach_irq)(...);                          /* 绑中断 */
    rt_err_t (*pin_detach_irq)(...);                          /* 解绑中断 */
    rt_err_t (*pin_irq_enable)(...);                          /* 使能中断 */
    rt_base_t (*pin_get)(const char *name);                   /* 名字转编号 */
};

int rt_device_pin_register(const char *name, const struct rt_pin_ops *ops, void *user_data)
{
    _hw_pin.parent.type = RT_Device_Class_Pin;
    _hw_pin.ops = ops;                        /* 挂接厂商 ops */
    rt_device_register(&_hw_pin.parent, name, RT_DEVICE_FLAG_RDWR);
}
```

**② 应用 API 内部转发**（`pin.c`）——应用调的是框架，框架再调 ops：

```c
void rt_pin_mode(rt_base_t pin, rt_base_t mode)
{
    _hw_pin.ops->pin_mode(&_hw_pin.parent, pin, mode);   /* 转发给厂商函数 */
}
```

**③ 对接层实现 ops 并注册**（`drv_gpio.c`）：

```c
const static struct rt_pin_ops _stm32_pin_ops = {
    stm32_pin_mode,      /* 内部调用 HAL_GPIO_Init 设模式 */
    stm32_pin_write,     /* 内部调用 HAL_GPIO_WritePin */
    stm32_pin_read,      /* 内部调用 HAL_GPIO_ReadPin */
    stm32_pin_attach_irq,
    stm32_pin_dettach_irq,
    stm32_pin_irq_enable,
    stm32_pin_get,       /* 把 "PC.5" 解析成编号 */
};

int rt_hw_pin_init(void) {
    return rt_device_pin_register("pin", &_stm32_pin_ops, RT_NULL);
}
```

**④ 引脚编号**：`GET_PIN(C, 5)` 宏 → 内部调 `rt_pin_get("PC.5")` → `stm32_pin_get` 解析字符串得到 `PIN_NUM(port, no)` 编号。应用层不接触寄存器，只用编号。

- **与 test_dev 的异同**：思想相同（都是"函数指针表 + 注册"），但 PIN 是**专用框架**（有专门的 `rt_pin_ops` 和 `rt_pin_xxx` API），而 test_dev 是**通用字符设备**（用通用 `rt_device` 的 init/open/close/read/write/control 六个 OPS + 通用 `rt_device_xxx` API）。

## I2C

###  协议要点

- 速率：低速 10K / 标准 100K / 快速 400K / 高速 3.4M bit/s。
- 起始位 S：SCL 高时 SDA 高→低；结束位 P：SCL 高时 SDA 低→高。
- ACK：从机拉低 SDA；NACK：SDA 保持高。
- 写数据：起始位 → 从机地址+写标志 → 数据 → ACK → 停止位。
- 读数据：先发地址+写 → 再发地址+读 → 从机应答并返回数据。

###  使用思路（4 步）

1. `rt_device_find("i2c2")` 查找总线 → 2. 构造 `struct rt_i2c_msg` 消息 → 3. `rt_i2c_transfer()` 传输 → 4. 处理结果。

```c
struct rt_i2c_msg msgs;
msgs.addr  = 0x68;              /* 从机地址（不含读写位！） */
msgs.flags = RT_I2C_WR;         /* 或 RT_I2C_RD，读写位靠 flags 控制 */
msgs.buf   = buf;
msgs.len   = 1;
rt_i2c_transfer(i2c_bus, &msgs, 1);   /* 返回实际传输的消息数 */
```

- 读写寄存器常见模式：写 = 1 条 `RT_I2C_WR` 消息；读 = 2 条消息 `[WR(寄存器地址), RD(读数据)]`。
- 调试：勾选 **i2c-tools** 软件包，`i2c scan i2c3` 探测总线上的设备；`list_device` 查看总线。

###  常见错误

- **总线死锁**：主机复位瞬间从机还在等 SCL 变低而拉低 SDA，互相等待。解锁：传输前让 SCL 产生 9 个时钟脉冲。
- **`rt_i2c_transfer` 返回 -5**：通常是从机地址错误。

## SPI

- SPI 4 根线：SCK / MOSI / MISO / CS（片选），高速、全双工、同步，常用于 FLASH/EEPROM/AD 转换器。
- RT-Thread 中分 **SPI 总线**（控制器）与 **SPI 设备**（CS 连接的从设备）。
- 命名：总线 `spi1/spi2`，设备 `spi10`（= spi1 总线上 0 号设备）。
- 开发模式：写 BUS 驱动 → 注册 SPI 设备 → 打开 → 传输 → 关闭。

| 函数 | 作用 |
|---|---|
| `rt_hw_spi_device_attach("spi2", "spi20", GET_PIN(B,12))` | 把设备挂到总线（CS 引脚） |
| `rt_spi_configure(dev, &cfg)` | 配置（`struct rt_spi_configuration`：data_width/mode/max_hz） |
| `rt_spi_transfer(dev, send, recv, len)` | 全双工传一次（自动片选） |
| `rt_spi_send(dev, send, len)` / `rt_spi_recv(dev, recv, len)` | 只发 / 只收 |
| `rt_spi_send_then_send(dev, b1, l1, b2, l2)` | 连续两次发送，**中途不释放片选**（先发地址再发数据） |
| `rt_spi_send_then_recv(dev, send, slen, recv, rlen)` | 先发命令/地址，再收数据（读 FLASH 常用） |
| `rt_spi_transfer_message(dev, msg)` | 自定义多消息传输 |

```c
struct rt_spi_configuration cfg;
cfg.data_width = 8;
cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
cfg.max_hz = 1 * 1000 * 1000;         /* 1MHz */
rt_spi_configure(spi20, &cfg);
```

## 今日实验小结

| 实验 | 内容 | 关键点 |
|---|---|---|
| 实验一 | 注册字符设备 test | `rt_device_create` + `rt_device_register`（无 OPS） |
| 实验二 | 添加 TEST 驱动 OPS 接口 | 实现 6 个 OPS 并挂接，`INIT_BOARD_EXPORT` 自动初始化 |
| 实验三 | GPIO 外部中断（按键） | PIN 设备 6 个 API + 中断回调 |
| 实验四 | I2C 读写（ICM20608 传感器） | `rt_i2c_msg` + `rt_i2c_transfer`，传感器初始化/校准/读三轴 |
| 实验五 | SPI 收发（W25Q128 等） | 总线/设备分离，`rt_spi_*` 系列 API |

- 核心思想：**统一 I/O API 屏蔽厂商差异，驱动层实现 OPS，应用层只调框架 API**。
