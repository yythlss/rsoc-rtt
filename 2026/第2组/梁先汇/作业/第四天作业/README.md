# 【DAY4】RT-Thread 设备驱动框架与 PIN 设备对接

---

## Part 1：理论知识梳理

### 1. 为什么需要设备驱动框架层

RT-Thread 把驱动分成三层，层与层之间通过一张**方法表（ops 函数指针表）**联系起来：

| 层 | 职责 | PIN 的例子 |
| --- | --- | --- |
| 应用层 | 只调统一 API，不关心硬件 | `rt_pin_write()` / `rt_pin_read()` |
| 设备驱动框架层 | 定义抽象接口、注册设备、统一分发 | `rt-thread/components/drivers/misc/pin.c` |
| 驱动层 | 实现具体硬件操作，填进方法表 | `drivers/drv_gpio.c` |

### 2. PIN 设备如何对接（三层源码剖析）

先看框架层定义的抽象接口（pin.h），它规定了驱动层要实现哪些函数：

```c
struct rt_pin_ops
{
    void (*pin_mode)(struct rt_device *device, rt_base_t pin, rt_base_t mode);
    void (*pin_write)(struct rt_device *device, rt_base_t pin, rt_base_t value);
    int  (*pin_read)(struct rt_device *device, rt_base_t pin);
    rt_err_t (*pin_attach_irq)(...);
    rt_err_t (*pin_detach_irq)(...);
    rt_err_t (*pin_irq_enable)(...);
};
```

**② 驱动层实现并填表（drv_gpio.c）**——用 STM32 HAL 实现后填成表：

```c
const static struct rt_pin_ops _stm32_pin_ops =
{
    stm32_pin_mode, stm32_pin_write, stm32_pin_read,
    stm32_pin_attach_irq, stm32_pin_dettach_irq, stm32_pin_irq_enable,
    stm32_pin_get,
};
```

**③ 框架层挂载 + 分发（pin.c）**——定义扩展结构体 `struct rt_device_pin`（内核 `parent` + 自己的 `ops`），`rt_device_pin_register()` 把 `parent` 的 `read/write/control` 指到框架自己的分发函数 `_pin_read/_pin_write/_pin_control`，再 `rt_device_register()` 挂进内核。分发函数只做"转手"：

```c
static rt_size_t _pin_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct rt_device_pin *pin = (struct rt_device_pin *)dev;   /* 强转取 ops */
    ...
    status->status = pin->ops->pin_read(dev, status->pin);     /* 下行到驱动层 */
    return size;
}
```

**④ 应用层 API**——直接经 `_hw_pin.ops` 下行：

```c
void rt_pin_write(rt_base_t pin, rt_base_t value)
{
    _hw_pin.ops->pin_write(&_hw_pin.parent, pin, value);
}
```

**对接总结**：`rt_pin_write()` → 框架层 `pin.c: _hw_pin.ops->pin_write` → 驱动层 `drv_gpio.c: stm32_pin_write` → `HAL_GPIO_WritePin`。换 MCU 只需重写 `drv_gpio.c` 这张 ops 表。

---

## Part 2：虚拟设备驱动框架层 test_dev（作业 2）

### 1. 文件结构

```text
第四天作业/
├─ test_dev.h   # 框架层：方法表 + 设备对象 + 注册 API 声明
├─ test_dev.c   # 框架层：静态设备 + 6 个分发函数 + rt_device_test_register
├─ test_drv.c   # 驱动层：6 个实现 + test_ops 方法表 + INIT_DEVICE_EXPORT 注册
└─ test_app.c   # 应用层：test_dev_app 命令
```

### 2. 框架层接口（test_dev.h）

```c
struct rt_test_ops
{
    rt_err_t  (*init)(rt_device_t dev);
    rt_err_t  (*open)(rt_device_t dev, rt_uint16_t oflag);
    rt_err_t  (*close)(rt_device_t dev);
    rt_size_t (*read)(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size);
    rt_size_t (*write)(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size);
    rt_err_t  (*control)(rt_device_t dev, int cmd, void *args);
};

struct rt_device_test
{
    struct rt_device parent;          /* 内核设备，放第一个成员以便强转 */
    const struct rt_test_ops *ops;
};
```

### 3. 框架层实现要点（test_dev.c）

- 用**静态设备对象** `static struct rt_device_test _test_dev;`（对标 `pin.c` 的 `_hw_pin`），不用 `rt_device_create()`——后者只分配内核 `rt_device`，放不下扩展的 `ops`。
- 分发函数强转取 ops 后下行：

```c
static rt_size_t _test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct rt_device_test *test_dev = (struct rt_device_test *)dev;
    if (test_dev->ops == RT_NULL || test_dev->ops->read == RT_NULL) return 0;
    return test_dev->ops->read(dev, pos, buffer, size);
}
```

- `rt_device_test_register()` 把 **init/open/close/read/write/control 6 个内核接口全部**接上分发函数，再 `rt_device_register()` 挂载。

### 4. 驱动层（test_drv.c）

- 6 个 `test_drv_xxx()` 用 `LOG_I` 打印假装操作硬件，read/write 返回 `size`（实际字节数）。
- 填方法表，顺序与 `struct rt_test_ops` 一致；`INIT_DEVICE_EXPORT` 里调 `rt_device_test_register("test_dev", &test_ops, RT_NULL)`。

### 5. 应用层（test_app.c）

```c
MSH_CMD_EXPORT(test_dev_app, test test_dev framework);
```

内部流程：`rt_device_find("test_dev")` → `open` → `control` → `write(1024)` → `read(128)` → `close`。

### 6. 验证结果

```text
msh >list_device
device           type         ref count
-------- -------------------- ----------
test_dev Character Device     0
uart1    Character Device     2
pin      Pin Device           0

msh >test_dev_app
[test.drv] init
[test.drv] open
[test.drv] control
[test.drv] write
[test.drv] read
[test.drv] close
```

从输出可见，`test_dev_app` 依次触发了 init、open、control、write、read、close 六个操作，说明应用层的每次调用都经过内核、框架层分发，最终到达驱动层实现，整个虚拟设备框架的链路已经打通。

---

## Part 3：踩坑记录

1. **`rt_ssize_t` 在 4.1.1 中不存在**，read/write 返回类型要用 `rt_size_t`（与内核 `struct rt_device` 一致）。
2. **`MSH_CMD_EXPORT` 重复定义**：同一命令导出两次会报 `__fsym_xxx` 重复定义。
3. **框架层必须接全 6 个接口**：只接 read/write/control 时，`dev->init/open/close` 为 NULL，内核 `rt_device_open/close` 直接跳过，驱动层不打印。
4. **框架层用静态对象**，不用 `rt_device_create()`。
