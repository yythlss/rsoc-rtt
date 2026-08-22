# test_dev —— 一个自包含的虚拟设备驱动框架层

## 一、为什么要有"框架层"

Day4 讲 IO 设备驱动框架时，重点就是下面这条分层链：

```
应用层 API
   ↓
IO 设备管理层（rt_device_xxx）
   ↓
设备驱动框架层（SPI / I2C / GPIO / Pin）   ← test_dev 要扮演的就是这一层
   ↓
设备驱动层（HAL / LL）
   ↓
硬件
```

框架层的核心价值只有一句话：**让"管理层"只认一套统一接口，让"驱动层"只管具体硬件**，两边互不关心对方长什么样。test_dev 把这一层单独抽出来，做成不依赖 RT-Thread、不依赖真实硬件、纯 C 就能编译运行的最小例子，专门用来看清"框架层到底干了什么"。

## 二、test_dev 的整体设计

### 2.1 分层映射

| 真实 RT-Thread 里的角色 | test_dev 里的对应 |
|---|---|
| IO 设备管理层（rt_device_xxx） | `test_dev` 框架本身：register / find / init / open / read / write / control / close |
| 设备驱动框架层（夹在中间那层） | 这里就是 test_dev 演示的对象 |
| 设备驱动层 / 硬件 | `test_dev_driver_ops` 回调 + 一个具体驱动（虚拟 LED） |

> 真实 RT-Thread 里框架层还会再细分 SPI / I2C / Pin，各自有独立的 ops。test_dev 用"一个统一框架 + 可换的 driver ops"把同一套机制一次性演示清楚。

### 2.2 核心数据结构

`test_dev_t` —— 框架层统一的设备对象（类比 `struct rt_device`）：

```c
struct test_dev {
    const char *name;                  /* 设备名，应用层按名字查找 */
    test_dev_type_t type;              /* 设备类型（LED / KEY / SENSOR...） */
    uint8_t ref_count;                 /* 打开计数，open/close 配对 */
    uint8_t initialized;               /* 是否已 init */
    void *user_data;                   /* 驱动私有数据（虚拟寄存器/状态） */
    const test_dev_driver_ops_t *dops; /* 指向具体驱动的回调表 */
};
```

`test_dev_driver_ops_t` —— 框架层对驱动层要求的统一回调（类比 `struct rt_device_ops`）：

```c
struct test_dev_driver_ops {
    int (*init)   (test_dev_t *dev);
    int (*deinit) (test_dev_t *dev);
    int (*read)   (test_dev_t *dev, void *buf, size_t len);
    int (*write)  (test_dev_t *dev, const void *buf, size_t len);
    int (*control)(test_dev_t *dev, int cmd, void *args);
};
```

解耦的关键就在 `dops` 这个指针：框架层只调用 `dev->dops->xxx`，永远不直接碰硬件。换一个设备，只是换一张 `dops` 表。

### 2.3 生命周期与 API

```
create  → 分配设备对象（含私有数据结构）
register→ 挂到全局设备表（名字 -> 设备）
find    → 应用层按名字拿到设备句柄
init/open/close/read/write/control → 统一入口，内部分发到 driver ops
```

`open` 里做了"懒初始化"：首次 open 若未 init 会自动 init；`close` 在引用计数归零时才调驱动的 `deinit`。这和 RT-Thread 里 `rt_device_open` 的引用计数行为一致。

## 三、调用链走读（以点亮 LED 为例）

应用层一句：

```c
test_dev_write(dev, &on, 1);   /* on = 1，点亮 */
```

实际发生的调用栈：

```
test_dev_write(dev, &on, 1)          // 框架层统一入口
   └─ dev->dops->write(dev, &on, 1)  // 分发到驱动回调
        └─ led_write(dev, &on, 1)    // 具体驱动实现
             └─ hw->on = 1;          // 改写"虚拟寄存器"
                printf(... "ON")     // 模拟硬件动作
```

应用层从头到尾只认 `test_dev_write` 这个框架 API，既不知道背后是 LED 还是按键，也不知道 `led_write` 里发生了什么。这就是框架层存在的意义。

## 四、与 RT-Thread 真实框架对照

| test_dev | RT-Thread | 说明 |
|---|---|---|
| `test_dev_t` | `struct rt_device` | 设备对象 |
| `test_dev_driver_ops_t` | `struct rt_device_ops` | 统一回调接口 |
| `test_dev_register` | `rt_device_register` | 注册到系统 |
| `test_dev_find` | `rt_device_find` | 按名查找 |
| `test_dev_read/write/control` | `rt_device_read/write/control` | 管理层 API |
| `led_ops`（具体驱动回调） | `rt_pin_ops` / `rt_spi_ops` | 框架/驱动层的具体实现 |
| `virt_led_hw_t` | 寄存器基址 / HAL 句柄 | 硬件状态载体 |

一一对应，只是 test_dev 把"管理层 + 框架层"合成了一层来简化演示。

## 五、编译运行（虚拟 LED 例子）

代码全在 `test_dev/` 目录，纯标准 C99，无第三方依赖。本地用任意 C 编译器：

```bash
cd test_dev
gcc test_dev.c test_dev_led.c main.c -o test_dev_demo
./test_dev_demo
```

预期输出：

```
[test_dev] register 'led0' (type=0)
[virt_led] led0 init: off, brightness=100%
[virt_led] led0 -> ON
app read back: led is ON
[virt_led] led0 brightness=40%
[virt_led] led0 toggle -> OFF
[virt_led] led0 deinit
```

## 六、如何扩展第二个设备（验证框架价值）

框架层"加设备不改应用"的价值，靠扩展来验证。照着 `test_dev_led.c` 再写一个 `test_dev_key.c`（虚拟按键）：

1. 定义 `virt_key_hw_t`（记录按键电平、是否按下）；
2. 实现 `key_init / key_read / key_control` 填进一张 `key_ops`；
3. `test_dev_key_create` 时把 `type` 设成 `TEST_DEV_TYPE_KEY`；
4. `test_dev_register` 后，应用层用**完全相同的** `test_dev_find / test_dev_read` 就能操作按键。

应用层代码一行都不用改——这正是 Day4 说的"框架层解耦"的真正含义。
