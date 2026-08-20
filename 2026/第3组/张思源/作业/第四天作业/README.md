# 【DAY4】虚拟 LED 设备驱动框架设计 -- 张思源（第3组）

- **目标平台**：STM32F407 星火一号（`stm32f407-rt-spark`，RT-Thread Studio 工程 `04_device_sample`）
- **提交内容**：本 `README.md`（设计说明 + 实验报告）+ 2 份源码（`led_dev.h` / `led_dev.c`）
- **MSH 命令**：`led_test`

---

## 一、设计目标

仿照 RT-Thread PIN 设备驱动框架的三层架构，设计一个虚拟 LED 设备驱动框架（test_dev）。与直接使用通用 `rt_device` 的 `test_drv.c` 不同，本框架自定义了专属的 ops 接口表和语义化用户 API，实现了与 PIN 框架相同的设计模式。

---

## 二、框架架构

```
应用层          rt_led_init() / rt_led_on() / rt_led_off() / rt_led_toggle() / rt_led_set_brightness()
  |             用户直接调用的语义化 API
  v
框架层          led_dev.h / led_dev.c（上半部分）
  |             定义 rt_led_ops 接口表，提供注册函数和用户 API
  |             连接应用层与虚拟 BSP 层
  v
虚拟BSP层       led_dev.c（下半部分）
                virtual_led_init / virtual_led_on / ... 用日志模拟硬件操作
                _virtual_led_ops（ops 的具体实现）
                rt_hw_led_init()（启动时自动注册）
```

---

## 三、与 PIN 框架的对应关系

| 组件 | PIN 框架 | LED 框架（本项目） |
| --- | --- | --- |
| 头文件 | `pin.h` | `led_dev.h` |
| 框架源码 | `pin.c` | `led_dev.c`（上半部分） |
| BSP 源码 | `drv_gpio.c` | `led_dev.c`（下半部分） |
| ops 结构体 | `struct rt_pin_ops` | `struct rt_led_ops` |
| 设备结构体 | `struct rt_device_pin` | `struct rt_device_led` |
| 注册函数 | `rt_device_pin_register()` | `rt_device_led_register()` |
| 用户 API | `rt_pin_write()` | `rt_led_on()` |
| BSP 实现 | `stm32_pin_write()` | `virtual_led_on()` |
| ops 实例 | `_stm32_pin_ops` | `_virtual_led_ops` |
| 初始化函数 | `rt_hw_pin_init()` | `rt_hw_led_init()` |
| 自动注册 | `INIT_BOARD_EXPORT(rt_hw_pin_init)` | `INIT_BOARD_EXPORT(rt_hw_led_init)` |

---

## 四、关键数据结构

### 1. ops 接口表

```c
struct rt_led_ops
{
    rt_err_t (*led_init)(struct rt_device *device);
    rt_err_t (*led_on)(struct rt_device *device, rt_uint8_t led_id);
    rt_err_t (*led_off)(struct rt_device *device, rt_uint8_t led_id);
    rt_err_t (*led_toggle)(struct rt_device *device, rt_uint8_t led_id);
    rt_err_t (*led_set_brightness)(struct rt_device *device, rt_uint8_t led_id, rt_uint8_t led_level);
};
```

5 个操作接口：
- `led_init`：初始化 LED 设备
- `led_on`：点亮指定 LED
- `led_off`：熄灭指定 LED
- `led_toggle`：翻转指定 LED 状态
- `led_set_brightness`：设置指定 LED 亮度（0~100）

### 2. 设备结构体

```c
struct rt_device_led
{
    struct rt_device parent;       // 继承 RT-Thread 通用设备
    const struct rt_led_ops *ops;  // LED 设备专属操作接口表
};
```

---

## 五、注册流程

```
系统启动
  -> rt_hw_led_init()                          [INIT_BOARD_EXPORT 自动调用]
    -> rt_device_led_register("led", &_virtual_led_ops, RT_NULL)
      -> _hw_led.parent.type = RT_Device_Class_Miscellaneous
      -> _hw_led.ops = ops                     [保存 BSP 层传入的 ops]
      -> rt_device_register(&_hw_led.parent, "led", RT_DEVICE_FLAG_RDWR)
        -> 设备进入 RT-Thread 设备管理器
```

注册完成后，应用层即可通过 `rt_led_on()` 等专用 API 调用。

---

## 六、用户 API 调用链

以 `rt_led_on(1)` 为例：

```
rt_led_on(1)                                    [应用层, led_dev.c:24]
  -> _hw_led.ops->led_on(&_hw_led.parent, 1)   [框架层, 通过函数指针跳转]
    -> virtual_led_on(device, 1)                [虚拟BSP层, led_dev.c:54]
      -> rt_kprintf("[virtual_led] LED 1 ON\n") [日志模拟硬件操作]
      -> return RT_EOK
```

---

## 七、与 test_drv.c 的区别

`test_drv.c` 是直接使用通用 `rt_device` 框架，将函数挂到 `dev->init/open/read/write/control` 上，应用层通过 `rt_device_find()` + `rt_device_write()` 访问。

本 LED 框架与 PIN 框架一样，**自定义了专属 ops 接口表和语义化 API**：

| 对比项 | test_drv.c（通用框架） | LED 框架（自定义框架） |
| --- | --- | --- |
| ops 定义 | 无，直接用 rt_device 标准接口 | `struct rt_led_ops`（5 个语义化函数指针） |
| 用户 API | `rt_device_write(dev, pos, buf, size)` | `rt_led_on(led_id)` 语义化 |
| 参数传递 | 通过 buffer + offset | 直接传 led_id 和 brightness |
| 可读性 | 需要查文档才知道 buffer 里放什么 | 参数含义一目了然 |
| 可移植性 | 换设备需改应用层 | 只需实现新的 ops 并注册 |

---

## 八、实验验证

### 测试代码

```c
static int led_test(void)
{
    rt_led_init();
    rt_led_on(1);
    rt_led_on(2);
    rt_led_set_brightness(1, 50);
    rt_led_toggle(1);
    rt_led_off(2);
    return 0;
}
MSH_CMD_EXPORT(led_test, test virtual led device);
```

### 串口日志

```
msh >led_test
[virtual_led] LED device init
[virtual_led] LED 1 ON
[virtual_led] LED 2 ON
[virtual_led] LED 1 SET BRIGHTNESS 50
[virtual_led] LED 1 TOGGLE
[virtual_led] LED 2 OFF
```

### 结果分析

- `rt_led_init()` 正确调用 `virtual_led_init()`，打印初始化日志
- `rt_led_on(1)` 和 `rt_led_on(2)` 正确传递 led_id 到 BSP 层
- `rt_led_set_brightness(1, 50)` 正确传递 led_id 和 led_level 两个参数
- `rt_led_toggle(1)` 和 `rt_led_off(2)` 状态切换正常
- 所有 API 通过框架层的 ops 函数指针表跳转到 BSP 层实现，调用链完整

---

## 九、总结

本框架完整复现了 RT-Thread 设备驱动框架的核心设计模式：

1. **分层设计**：应用层 -> 框架层 -> BSP 层，各层职责清晰，互不依赖具体实现
2. **接口隔离**：框架层定义 `rt_led_ops` 接口，BSP 层实现，通过函数指针表连接
3. **注册机制**：BSP 层在启动时通过 `INIT_BOARD_EXPORT` 自动调用注册函数，将 ops 注入框架层
4. **语义化 API**：用户 API（`rt_led_on/off/toggle`）参数直观，比通用 `rt_device_write` 更易用
5. **可移植性**：若要支持真实硬件，只需实现新的 `rt_led_ops` 并在 init 函数中注册，框架和应用代码零修改
