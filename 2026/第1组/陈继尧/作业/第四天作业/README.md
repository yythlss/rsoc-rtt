# RT-Thread 夏令营第四天作业

## 作业主题

本次作业围绕 RT-Thread 的设备驱动框架与 GPIO/PIN 设备访问展开，完成两项内容：

1. 整理 PIN 设备如何被上层应用访问，分析统一 API 到芯片 HAL 驱动的调用链；
2. 自主设计并实现一个不依赖真实硬件的虚拟设备驱动框架层 `test_dev`。

## 一、PIN 设备访问分析

### 1.1 为什么需要 PIN 抽象

不同 MCU 厂商对 GPIO 的寄存器、时钟、复用和中断控制方式不同。如果应用直接操作 STM32、NXP 或其他芯片的寄存器，代码会和具体芯片强耦合，迁移时需要重写大量业务逻辑。

RT-Thread 提供 PIN 设备抽象后，应用层只关注引脚编号、模式、电平和中断触发方式，底层 BSP 再将这些操作转换为芯片 HAL 或寄存器操作。这样按键、LED、片选等应用代码可以保持稳定。

### 1.2 常用 API

```c
rt_pin_mode(pin, PIN_MODE_INPUT_PULLUP);
rt_pin_write(pin, PIN_HIGH);
level = rt_pin_read(pin);
rt_pin_attach_irq(pin, PIN_IRQ_MODE_FALLING, key_irq, user_data);
rt_pin_irq_enable(pin, PIN_IRQ_ENABLE);
```

不再使用中断时，应先关闭中断，再解除回调：

```c
rt_pin_irq_enable(pin, PIN_IRQ_DISABLE);
rt_pin_detach_irq(pin);
```

常见模式包括普通输入、上拉输入、下拉输入、推挽输出和开漏输出。I2C 等需要释放总线的场景通常使用开漏结构，并配合外部上拉电阻。

### 1.3 调用链

```text
应用代码
  -> rt_pin_mode / rt_pin_write / rt_pin_read
  -> RT-Thread PIN 设备接口
  -> BSP pin_ops 函数表
  -> 芯片 GPIO 驱动
  -> STM32 HAL 或寄存器
  -> 实际 GPIO 电平
```

中断路径则是：

```text
GPIO 外部边沿
  -> 芯片 EXTI 中断入口
  -> BSP 中断处理函数
  -> rt_pin_attach_irq 注册的回调
  -> 应用线程处理事件
```

中断回调应尽量短小。复杂处理、打印和阻塞等待应通过信号量、事件或消息队列转交给线程完成。

### 1.4 按键中断的实际问题

机械按键在一次按下或松开过程中可能产生多次电平跳变，因此一次物理动作可能触发多次中断。不能简单把每一次中断都当作一次有效按键。

常见消抖方法：

- 在中断中只记录时间和置位标志，在线程中延时 10~20 ms 后再次读取电平；
- 使用软件定时器延迟确认；
- 使用已有的 button 软件包统一处理按键状态机；
- 对重复中断设置最小间隔，过滤过快的边沿。

## 二、`test_dev` 虚拟设备设计

### 2.1 设计目标

`test_dev` 不占用 GPIO、UART、SPI、I2C、DMA 或定时器资源，使用 RAM 模拟一段可寻址设备存储区。它用于验证 RT-Thread 设备生命周期、统一读写接口、控制命令、边界处理和线程安全设计。

当前版本的行为约定如下：

| 项目 | 设计 |
| --- | --- |
| 设备名 | `test_dev` |
| 设备类型 | 字符设备 |
| 模拟容量 | 256 字节 |
| 地址方式 | `pos` 表示字节偏移 |
| 并发保护 | RT-Thread 互斥量 |
| 越界写入 | 截断为剩余容量并返回实际长度 |
| 未写入区域 | 初始化为 0 |
| 数据统计 | 读写次数和累计字节数 |

### 2.2 逻辑分层

```text
Shell 自测层
  -> rt_device_find/open/read/write/control
  -> RT-Thread 设备适配层
  -> test_dev 公共接口与生命周期
  -> RAM 内存后端行为
  -> 256 字节虚拟存储区
```

RT-Thread 的 `init/open/close/read/write/control` 回调只负责适配和转发；边界检查、互斥锁、统计和控制命令集中在 `test_dev` 接口中。以后若增加 FIFO、模拟传感器或 SPI Flash 后端，可以在保持上层调用方式不变的情况下替换底层数据实现。

本次归档代码将核心逻辑和内存后端放在一个 `test_dev.c` 中，先保证教学工程容易阅读；后续可以进一步拆分为 `core`、`adapter` 和 `backend` 三个源文件。

### 2.3 文件说明

```text
test_dev/
├── test_dev.h          公共接口、设备名、容量和控制命令
├── test_dev.c          设备对象、互斥锁、读写实现和 RT-Thread 注册
├── test_dev_demo.c     MSH 自测命令 test_dev_run
└── SConscript          将模块加入 RT-Thread 构建
```

公共接口包括：

```c
int test_dev_start(void);
rt_err_t test_dev_open(void);
rt_err_t test_dev_close(void);
rt_int32_t test_dev_read(rt_off_t pos, void *buffer, rt_size_t size);
rt_int32_t test_dev_write(rt_off_t pos, const void *buffer, rt_size_t size);
rt_err_t test_dev_control(int command, void *args);
```

`test_dev_start()` 使用 `INIT_DEVICE_EXPORT()` 自动注册。当前 RT-Thread 版本要求自动初始化函数为 `int (*)(void)`，因此启动函数使用 `int` 返回类型；设备操作回调仍使用 RT-Thread 规定的 `rt_err_t` 和 `rt_size_t` 类型。

### 2.4 控制命令

```c
#define TEST_DEV_CMD_CLEAR       0x1001
#define TEST_DEV_CMD_INFO        0x1002
#define TEST_DEV_CMD_RESET_STATS 0x1003
```

- `TEST_DEV_CMD_CLEAR`：清零 256 字节数据，并将 `used` 设为 0；
- `TEST_DEV_CMD_INFO`：查询容量、有效长度、打开次数、读写次数和累计字节数；
- `TEST_DEV_CMD_RESET_STATS`：只清除读写统计，不影响数据内容。

## 三、实验操作与预期现象

### 3.1 查看设备

系统启动后，在 FinSH 中输入：

```text
list_device
```

设备列表中应出现：

```text
test_dev
```

该设备没有真实引脚和外设动作，启动时不会闪灯或输出数据。

### 3.2 执行自测

输入：

```text
test_dev_run
```

自测命令依次完成：

1. 查找并打开 `test_dev`；
2. 清空虚拟存储区；
3. 从偏移 12 写入 4 字节并读回比较；
4. 从偏移 254 写入 4 字节，验证只实际写入 2 字节；
5. 查询状态并确认 `used == 256`；
6. 清空设备并确认 `used == 0`；
7. 读取空区域并确认返回 0；
8. 关闭设备。

成功输出：

```text
test_dev_run: PASS
```

失败时会打印 `open failed`、`write failed`、`compare failed`、`boundary failed` 或 `clear failed` 等具体步骤。

### 3.3 应用层调用示例

```c
rt_device_t dev;
rt_uint8_t tx[] = {1, 2, 3, 4};
rt_uint8_t rx[sizeof(tx)];

dev = rt_device_find("test_dev");
rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
rt_device_write(dev, 0, tx, sizeof(tx));
rt_device_read(dev, 0, rx, sizeof(rx));
rt_device_close(dev);
```

应用层只依赖 RT-Thread 标准设备接口，不需要知道数据实际存放在数组中。

## 四、边界与线程安全分析

- 负偏移、空指针和零长度操作返回 `-RT_EINVAL`；
- 未打开设备时，读写返回 0，不访问内部存储区；
- 偏移达到容量末尾时，写入返回 0；
- 写入跨越末尾时自动截断，避免数组越界；
- 所有数据、状态和统计字段由同一把互斥量保护；
- `open_count` 采用引用计数方式，多次打开需要对应次数的关闭；
- 读写接口为同步接口，不在中断服务程序中调用。

## 五、验证结果与限制

已完成的源码检查：

- `test_dev.h`、`test_dev.c`、`test_dev_demo.c` 和 `SConscript` 均已归档；
- `INIT_DEVICE_EXPORT(test_dev_start)` 的函数签名已修正为 `int`，消除了函数指针类型警告；
- 自测命令覆盖正常读写、边界截断、清空和空区读取。

当前实际构建仍受开发环境影响：工程 `rtconfig.py` 的 `EXEC_PATH` 为空，系统未找到 `arm-none-eabi-gcc`，因此尚未完成目标板上的编译、下载和串口运行验证。配置 ARM GCC 后应重新执行 `scons -Q -j1`，再在板端运行 `test_dev_run`。

当前版本没有实现：

- 阻塞式 FIFO 读取；
- 异步通知和 DMA；
- 真实 SPI/I2C 后端；
- 多实例设备注册表；
- 动态卸载和热插拔。

这些功能可以作为后续框架演进方向。
