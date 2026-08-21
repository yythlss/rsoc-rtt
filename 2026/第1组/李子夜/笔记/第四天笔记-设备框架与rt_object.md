# 【RSOC26】第四课：RT-Thread 设备框架深度解析——rt_object、设备生命周期与自定义设备

> 提交人：李子夜（第1组）｜Gitee：@CyberSprout｜日期：2026-08-21

本文基于 RT-Thread **4.1.1** 源码逐段阅读后整理，所有结构体定义、API 签名、源码行号均可在对应文件中核验。从最基础的 `rt_object` 讲起，回答"内核到底是什么结构"这个根本问题。

---

## 一、"rt" 是什么？—— RT-Thread 的命名体系

**`rt` = RT-Thread 的缩写** = Real-Time Thread。

整个内核的命名遵循同一个规则：

```
rt_ 前缀 + 这个东西的本质

rt_thread       → 线程（task）
rt_semaphore    → 信号量
rt_mutex        → 互斥量
rt_event        → 事件集
rt_timer        → 定时器
rt_object       → 内核对象（万物基类）
rt_device       → 设备（硬件抽象）
rt_mq           → 消息队列
rt_memheap      → 内存堆
```

对比其他 RTOS 的命名风格：

```
Linux 内核:      kthread / kobject / kmalloc     （k = kernel）
FreeRTOS:        xTaskCreate / vTaskDelay         （x = handle, v = void 返回值）
Zephyr:          k_mutex / k_timer                （k = kernel）
RT-Thread:       rt_thread / rt_sem / rt_device   （rt = RT-Thread）
```

**规则统一**：看到 `rt_` 就知道这是内核自己提供的东西；看到下划线后面的词就知道是什么类型。

---

## 二、rt_object—— 内核万物的基类

### 2.1 内核管什么？

你的板子上运行着所有这些"东西"：

```
线程 x N       → 多个线程在跑
信号量 x N      → 线程之间同步用
互斥量 x N      → 保护共享资源
事件集 x N      → 多条件等待
定时器 x N      → 超时/周期性触发
设备 x N        → UART / SPI / I2C / Pin ...
```

内核不能"管什么就写一套代码"——那样每个新组件（定时器、设备）都要重新实现一遍"创建/命名/查找/删除"。

### 2.2 最小公约数

这些"东西"的共同点是什么？

| 共同点 | 说明 |
|---|---|
| 都有名字 | 内核靠名字找到它 |
| 都有类型 | 线程？信号量？设备？ |
| 都有状态 | 激活了？删除了？ |
| 都在某个链表上 | 内核统一管理它们 |

内核把这些共同点抽出来，做成一个**最基础的结构体**：

```c
// rtdef.h 第 398 行
struct rt_object
{
    char       name[RT_NAME_MAX];   // 名字，比如 "main" "pin" "uart1"
    rt_uint8_t type;                 // 类型
    rt_uint8_t flag;                 // 状态标志
    rt_list_t  list;                 // 链表节点
};
```

这就是 **rt_object**——内核万物管理的最小公约数。

### 2.3 万物继承自 rt_object

每个具体组件都在 `rt_object` 基础上**增加自己的特有字段**：

```c
// 线程 = rt_object + 线程特有字段
struct rt_thread
{
    struct rt_object parent;    // ← 公共部分
    void *sp;                    // 栈指针（线程独有）
    void *entry;                 // 线程函数入口（线程独有）
    rt_uint8_t priority;         // 优先级（线程独有）
    ...
};

// 设备 = rt_object + 设备特有字段
struct rt_device
{
    struct rt_object parent;    // ← 公共部分
    enum rt_device_class_type type;   // 设备类型（设备独有）
    const struct rt_device_ops *ops;  // 操作函数表（设备独有）
    ...
};
```

### 2.4 C 语言的"继承"—— 结构体偏移 0 技巧

`parent` 必须放在结构体的**第 0 个字节**：

```
struct rt_thread 的内存布局：
┌──────────────────────────────────────────┐
│ struct rt_object parent      偏移 0      │  ← 每个对象都有
│   name[8]  = "main\0\0\0\0\0"           │
│   type     = 0x01 (Thread)               │
│   flag     = 0x10 (ACTIVATED)            │
│   list     = ←→ 调度链表                 │
├──────────────────────────────────────────┤ 偏移 20
│ void *sp                                 │  ← 线程独有
│ void *entry                              │
│ rt_uint8_t priority                      │
│ ...                                      │
└──────────────────────────────────────────┘

struct rt_device 的内存布局：
┌──────────────────────────────────────────┐
│ struct rt_object parent      偏移 0      │  ← 每个对象都有
│   name[8]  = "pin\0\0\0\0\0"            │
│   type     = 0x09 (Device)               │
│   flag     = 0x003 (RDWR)                │
│   list     = ←→ 设备链表                 │
├──────────────────────────────────────────┤ 偏移 20
│ enum rt_device_class_type type           │  ← 设备独有
│ const struct rt_device_ops *ops          │
│ void *user_data                          │
└──────────────────────────────────────────┘
```

因为 `parent` 永远在偏移 0，所以：

```c
rt_device_t dev = rt_device_find("pin");
rt_object_t  obj = (rt_object_t)dev;     // 同一地址，不需要任何转换

// 现在可以用通用的对象函数操作它：
rt_object_get_name(obj);   // → "pin"
rt_object_get_type(obj);   // → RT_Object_Class_Device (0x09)
```

这就是 C 语言的"多态"——同一套管理代码，管不同的东西。

### 2.5 对象管理流程

```
rt_object 管理的是什么？

rt_object_init(name, type, flag)    创建时：给名字 + 设类型 + 进链表
rt_object_find(name, type)          查找时：按名字在链表中搜索
rt_object_detach(obj)               删除时：从链表移除

所有内核组件（线程/信号量/设备）复用同一套操作。
```

---

## 三、rt_device—— 硬件世界的统一入口

### 3.1 没有设备框架之前的世界

```c
// ❌ 没有设备框架：直接操作寄存器
*(volatile rt_uint32_t *)0x4001080C = 0x0C;   // GPIOF 时钟使能
*(volatile rt_uint32_t *)0x40011C00 = 0x00004000;  // PF11 = 输出
*(volatile rt_uint32_t *)0x40011C14 |= 0x00004000; // PF11 = LOW
```

问题：地址硬编码、没有统一接口、上层代码耦合到底层寄存器、换芯片全废。

### 3.2 有了设备框架之后

```c
// ✅ 统一接口
rt_pin_mode(PF11, PIN_MODE_OUTPUT);
rt_pin_write(PF11, PIN_LOW);
```

你不用知道 PF11 对应哪个寄存器，不用知道是 STM32F4 还是 RK3588——换硬件只换驱动，调用方式不变。

### 3.3 rt_device 结构体逐字段解读

```c
struct rt_device
{
    // ─── 继承自 rt_object ───
    struct rt_object          parent;       // 名字 + 类型 + flag + 链表节点

    // ─── 设备自己的字段 ───
    enum rt_device_class_type type;         // 设备类别
    rt_uint16_t               flag;         // 设备属性标志
    rt_uint16_t               open_flag;    // 当前打开模式

    rt_uint8_t                ref_count;    // 引用计数（被 open 了几次）
    rt_uint8_t                device_id;    // 内核内部编号

    // ─── 回调函数指针 ───
    rt_err_t (*rx_indicate)(rt_device_t dev, rt_size_t size);   // 接收回调
    rt_err_t (*tx_complete)(rt_device_t dev, void *buffer);     // 发送完成回调

    // ─── 核心操作函数表 ───
    const struct rt_device_ops *ops;         // init/open/close/read/write/control

    // ─── 驱动私有空间 ───
    void                     *user_data;     // 驱动上下文指针
};
```

---

## 四、设备类型—— 为什么固定模板？

```c
enum rt_device_class_type {
    RT_Device_Class_Char       = 0,    // 字符设备（UART、Console）
    RT_Device_Class_Block      = 1,    // 块设备（SD 卡、SPI Flash）
    RT_Device_Class_NetIf      = 2,    // 网络接口
    RT_Device_Class_Miscellaneous = 19,// 杂项（Pin、PWM、ADC、DAC）
    RT_Device_Class_Sensor     = 20,   // 传感器
    ...                              // 共 21 种
};
```

### 4.1 不会自动配置硬件

选 `RT_Device_Class_Pin` 不会帮你写 GPIO 寄存器。它只是一个**分类标签**。

### 4.2 实际作用

**作用 1：命令空间命名**

```c
// 不同设备类型的 control 命令在不同的数值区间
#define RT_DEVICE_CTRL_BASE(Type)    (RT_Device_Class_##Type * 0x100)

RT_Device_Class_Char  = 0  → 命令从 0x000 开始（UART 设波特率 = 0x01）
RT_Device_Class_Block = 1  → 命令从 0x100 开始（获取扇区信息 = 0x101）
RT_Device_Class_NetIf = 2  → 命令从 0x200 开始（获取 MAC = 0x201）

// Pin 设备属于 Miscellaneous(19) → 命令从 0x1900 开始
// 这样 Pin 的命令（0x1901）不会跟 UART 的命令（0x0001）冲突
```

**作用 2：框架区分处理方式**

块设备处理扇区对齐，字符设备处理字节流——框架根据类型选择处理逻辑。

---

## 五、flag—— 位域属性开关

```c
RT_DEVICE_FLAG_RDONLY     0x001   // 只读（传感器）
RT_DEVICE_FLAG_WRONLY     0x002   // 只写
RT_DEVICE_FLAG_RDWR       0x003   // 读写（= RDONLY | WRONLY，Pin/UART 用这个）
RT_DEVICE_FLAG_REMOVABLE  0x004   // 可热插拔（SD 卡）
RT_DEVICE_FLAG_STANDALONE 0x008   // 独立设备（不用 open 就能用）
RT_DEVICE_FLAG_ACTIVATED  0x010   // 已初始化
RT_DEVICE_FLAG_INT_RX     0x100   // 接收用中断
RT_DEVICE_FLAG_DMA_RX     0x200   // 接收用 DMA
```

### 为什么用位域？

一个设备可以同时有多个属性。Pin 设备的 flag：

```c
RT_DEVICE_FLAG_RDWR | RT_DEVICE_FLAG_STANDALONE
= 0x003 | 0x008 = 0x00B
```

一个数字打包了"可读写 + 独立设备"两个属性。

### flag 在源码中的实际效果

`rt_device_open` 中：

```c
// 独立设备不允许被多次 open
if ((dev->flag & RT_DEVICE_FLAG_STANDALONE) && (dev->ref_count > 1))
    return -RT_EBUSY;

// 未激活的设备自动 init
if (!(dev->flag & RT_DEVICE_FLAG_ACTIVATED))
{
    result = device_init(dev);        // 调用 ops->init
    dev->flag |= RT_DEVICE_FLAG_ACTIVATED;
}
```

**flag 不是"自动配置"——这是驱动开发者手动填的属性，框架根据它做行为决策。**

---

## 六、ref_count—— 不是数设备数量

### 6.1 它数的是"谁打开了这个设备"

```c
// device.c 源码
// open 时：
dev->ref_count++;          // 0 → 1

// close 时：
dev->ref_count--;          // 2 → 1
if (dev->ref_count == 0)
{
    device_close(dev);     // 最后一个关闭者才真正关硬件
}
```

### 6.2 为什么要计数？

你的 UART 同时被两个地方打开：

```
系统初始化：rt_device_open("uart1", RDWR)  → ref_count: 0 → 1
你的代码：  rt_device_open("uart1", RDWR)   → ref_count: 1 → 2

你的代码关：rt_device_close("uart1")        → ref_count: 2 → 1（UART 不关）
系统退出： rt_device_close("uart1")         → ref_count: 1 → 0（UART 真正关闭）
```

没有 ref_count，你的代码一 close，控制台就断了。

### 6.3 溢出保护

```c
dev->ref_count++;
RT_ASSERT(dev->ref_count != 0);
//      ↑ 从 255 跳到 0 时触发断言
```

---

## 七、device_id—— 不是 UART1/2

```c
rt_uint8_t device_id;   // 0 ~ 255
```

这是内核内部给设备的**自动编号**。你注册设备时内核自动分配。

**UART1/2 靠名称区分**：`rt_device_find("uart1")` vs `rt_device_find("uart2")`。

`device_id` 的实际用途是内核对象信息表中的快速索引 + `list_device` 排序。应用层几乎不需要关心。

---

## 八、rx_indicate 和 tx_complete—— 上下文的"喊话"机制

### 8.1 定义

```c
rt_err_t (*rx_indicate)(rt_device_t dev, rt_size_t size);
//                         ↑ 哪个设备     ↑ 收到了多少字节

rt_err_t (*tx_complete)(rt_device_t dev, void *buffer);
//                         ↑ 哪个设备     ↑ 哪块数据发完了
```

### 8.2 实际调用场景—— UART 接收中断

```
硬件收到字节
  │
  ▼
USARTx_IRQHandler()                    // 中断入口
  │
  ├─ 字节存入 rx_fifo 环形缓冲区
  │
  └─ if (serial->parent.rx_indicate != RT_NULL)
           │
           └─ serial->parent.rx_indicate(&serial->parent, rx_length);
                    │
                    ▼
           finsh_rx_ind()               ← 你注册的回调
                    │
                    └─ rt_sem_release(&rx_sem);  // 释放信号量
                         │
                         ▼
                    finsh_thread 被唤醒         // msh 终于能读输入了
```

**`rx_indicate` = 数据来了 = 驱动的中断 → 通知 fns "FIFO 有数据了，你可以读了"**

**`size` 参数 = 这次收到的字节数**。上层据此决定要读多少。

### 8.3 tx_complete 的场景

```c
// DMA 发送完成的回调
if (serial->parent.tx_complete != RT_NULL)
{
    serial->parent.tx_complete(&serial->parent, tx_buffer);
}
```

- DMA 模式传完一批数据后触发中断，回调通知上层"buffer 可以释放了"
- 适用于需要确认发送成功的协议（如 Modbus 等对方 ACK 的场景）

### 8.4 回调 vs 轮询

| 方式 | 优点 | 缺点 |
|---|---|---|
| rx_indicate 回调 | 收到数据立刻通知 | 回调在中断上下文，不能做耗时操作 |
| 轮询 rt_device_read | 简单 | 浪费 CPU |

回调的核心价值：**数据来了我才处理**，而不是一直问"有数据了吗？"

---

## 九、ops—— 不是"延时量"，是"操作函数表"

**ops = operations**。

### 9.1 它是什么？

把设备的 6 个操作打包在一起的结构体：

```c
struct rt_device_ops
{
    rt_err_t  (*init)   (rt_device_t dev);
    rt_err_t  (*open)   (rt_device_t dev, rt_uint16_t oflag);
    rt_err_t  (*close)  (rt_device_t dev);
    rt_size_t (*read)   (rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);
    rt_size_t (*write)  (rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
    rt_err_t  (*control)(rt_device_t dev, int cmd, void *args);
};
```

### 9.2 为什么用 ops 表？

Pin 设备的 ops：

```c
const static struct rt_device_ops pin_ops =    // 只读，在 Flash 中
{
    RT_NULL,      // init      → Pin 不需要特别初始化
    RT_NULL,      // open      → 不需要
    RT_NULL,      // close     → 不需要
    _pin_read,    // read      → 读引脚电平
    _pin_write,   // write     → 写引脚电平
    _pin_control  // control   → 设置引脚模式
};
```

注册时只存一个指针：

```c
_hw_pin.parent.ops = &pin_ops;   // 24 字节（6 个指针）
```

**好处**：
1. 省内存：多个 Pin 设备共用同一个 `pin_ops`
2. 只读安全：`const` 修饰，运行时不改变
3. 统一调用：`dev->ops->read(dev, ...)` 对所有设备都一样

### 9.3 本质上是 C 的"虚函数表"

```
Python (自动):
  class Pin:
      def read(self): ...

  pin = Pin()
  pin.read()          # Python 自动查找 vtable

C (手动):
  struct rt_device_ops pin_ops = {_pin_read, ...};
  dev->ops = &pin_ops;
  dev->ops->read(dev, ...)    # 手动查表
```

---

## 十、user_data—— 驱动上下文的指针

```c
void *user_data;
```

不是"新数据库"，不是"独立数据空间"。它是指向**驱动私有数据**的指针——让上层框架能访问到底层硬件的操作函数。

Pin 设备的完整调用链展示它的作用：

```
rt_pin_mode(PF11, PIN_MODE_OUTPUT)
  │
  ├─ rt_device_find("pin")                    // 找到设备
  ├─ rt_device_open(pin_dev, RDWR)            // open（自动 init）
  │
  └─ _pin_control(dev, 0, &mode)             // 中间层函数
            │
            ├─ mode = {PF11, PIN_MODE_OUTPUT}
            └─ pin = (struct rt_device_pin *)dev   // ← 通过 dev 拿到具体设备
                 pin->ops->pin_mode(dev, PF11, 0x00)  // ← 调更底层的操作
                          │
                          └─ HAL_GPIO_Init(GPIOF, ...)  // ← 真正的寄存器
```

不同设备的 user_data 各不相同：

| 设备 | user_data 指向 | 存放什么 |
|---|---|---|
| Pin | `struct rt_device_pin` | 底层 GPIO ops 表 |
| UART | `struct rt_serial` | FIFO 缓冲区、波特率配置 |
| LCD | `struct rt_device_graphic` | 显存地址、分辨率 |

---

## 十一、设备完整生命周期

```
rt_device_create(type, attach_size)   // ① 分配内存（堆），零初始化
       ↓
rt_device_register(dev, name, flag)   // ② 挂入内核对象链表，可以被 find()
       ↓
rt_device_find(name)                  // ③ 通过名称查找
       ↓
rt_device_open(dev, oflag)            // ④ 引用计数+1，自动 init（首次）
       ↓
rt_device_read/write/control(...)     // ⑤ 六大操作
       ↓
rt_device_close(dev)                  // ⑥ 引用计数-1，归零时真正关硬件
       ↓
rt_device_unregister(dev)             // ⑦ 从内核链表移除
       ↓
rt_device_destroy(dev)                // ⑧ 释放内存
```

### 重要区别

| 操作 | 之后能被 find() 找到？ |
|---|---|
| `rt_device_create` | **否** — 还没注册 |
| `rt_device_register` | **是** |
| `rt_device_unregister` | **否** — 已注销 |

---

## 十二、自定义设备完整示例

### 设计目标：一个基于消息队列的自定义设备

### Step 1：定义设备类型和命令集

```c
#define RT_Device_Class_MyMQ    21    // 自定义设备类型
#define MYMQ_CMD_SEND          0x10  // control 命令：发送消息
#define MYMQ_CMD_RECV          0x11  // control 命令：接收消息
#define MYMQ_CMD_RESET         0x12  // control 命令：重置队列
```

### Step 2：设备私有数据结构

```c
struct my_mq_device
{
    struct rt_device parent;          // 必须在第一个位置！
    void *mq;                         // 内部消息队列
    rt_uint32_t msg_size;             // 每条消息大小
    rt_uint32_t max_msgs;             // 队列容量
};
```

**`parent` 必须在第一个字段**——因为 `rt_device_t` 指针实际指向的就是 `parent` 的首地址。如果 `parent` 不在偏移 0，类型转换会得到错误的地址。

### Step 3：实现操作函数

```c
static rt_err_t mymq_init(rt_device_t dev)
{
    struct my_mq_device *mq_dev = (struct my_mq_device *)dev;
    mq_dev->mq = rt_mq_create("mymq", mq_dev->msg_size,
                               mq_dev->max_msgs, RT_IPC_FLAG_FIFO);
    return (mq_dev->mq) ? RT_EOK : -RT_ERROR;
}

static rt_err_t mymq_control(rt_device_t dev, int cmd, void *args)
{
    struct my_mq_device *mq_dev = (struct my_mq_device *)dev;
    switch (cmd)
    {
        case MYMQ_CMD_SEND:
            return rt_mq_send(mq_dev->mq, args, mq_dev->msg_size);
        case MYMQ_CMD_RECV:
            return rt_mq_recv(mq_dev->mq, args, mq_dev->msg_size,
                              RT_WAITING_FOREVER);
        case MYMQ_CMD_RESET:
            rt_mq_delete(mq_dev->mq);
            return mymq_init(dev);
        default:
            return -RT_EINVAL;
    }
}
```

### Step 4：组装 ops 表并注册

```c
const static struct rt_device_ops mymq_ops =
{
    .init    = mymq_init,
    .open    = RT_NULL,       // 不需要
    .close   = RT_NULL,
    .read    = RT_NULL,       // 不用 read/write，全走 control
    .write   = RT_NULL,
    .control = mymq_control,
};

static struct my_mq_device mymq_dev;

int mymq_device_init(void)
{
    mymq_dev.parent.type   = RT_Device_Class_Pipe;  // 选合适的内置类型
    mymq_dev.msg_size      = 16;
    mymq_dev.max_msgs      = 8;
    mymq_dev.parent.ops    = &mymq_ops;
    mymq_dev.parent.user_data = &mymq_dev;

    return rt_device_register(&mymq_dev.parent, "mymq", RT_DEVICE_FLAG_RDWR);
}
```

### Step 5：使用

```c
// 像操作普通设备一样操作它
rt_device_t dev = rt_device_find("mymq");
if (dev)
{
    char msg[16] = "hello";
    rt_device_control(dev, MYMQ_CMD_SEND, msg);
}
```

---

## 十三、设备框架核心价值总结

| 问题 | 没有设备框架 | 有了设备框架 |
|---|---|---|
| 硬件操作代码在哪？ | 散落在每个应用的代码里 | 只在驱动层写一次 |
| 换硬件要改什么？ | 改所有调用了寄存器的代码 | 只换驱动 |
| 上层用什么接口？ | 各不相同 | 统一的 read/write/control |
| 新加一个硬件？ | 重写一套操作函数 | 实现 6 个 ops 函数 + 注册 |

---

## 十四、扩展阅读（源码路径）

| 文件 | 内容 |
|---|---|
| `rt-thread/include/rtdef.h` | `rt_object`、`rt_device`、`rt_device_ops` 定义 |
| `rt-thread/include/rtthread.h` | API 声明（491-521 行） |
| `rt-thread/src/device.c` | 设备框架完整实现（460 行） |
| `rt-thread/components/drivers/misc/pin.c` | Pin 设备完整实现（参考模板） |
| `rt-thread/components/drivers/serial/serial.c` | UART 设备 + rx_indicate 回调 |
