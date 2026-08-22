
# DAY4 虚拟 LED 设备驱动框架设计
平台：STM32F407 星火一号 `stm32f407‑rt‑spark`，RT‑Thread Studio 工程 `04_device_sample`
提交：`README.md` + `virtu_dev.h` + `virtu_dev.c`
MSH测试命令：`virtu_dev_test`

## 一、设计目标
仿照 RT‑Thread PIN 设备驱动框架三层架构，实现一套自定义LED设备驱动框架。不直接使用原生`rt_device`的`read/write/control`通用接口，自定义LED专属`rt_led_ops`操作接口表，向外提供语义化用户API，复刻PIN框架的分层、接口隔离、可移植的设计模式。

本实现BSP层可以对接真实STM32 GPIO硬件，底层调用RTT PIN驱动接口操作引脚；框架层只负责转发，不关心底层芯片硬件细节。

## 二、框架架构

应用层          rt_led_init() / rt_led_on() / rt_led_off() / rt_led_toggle()
                用户直接调用语义化API
  │
  ▼
框架层          virtu_dev.h / virtu_dev.c（上半部分）
                定义 rt_led_ops、rt_device_led；实现注册函数、对外用户API
                连接应用层与BSP硬件层
  │
  ▼
BSP硬件层       virtu_dev.c（下半部分）
                stm32_led_init / stm32_led_on / stm32_led_off / stm32_led_toggle
                _stm32_led_ops ops实例
                rt_hw_led_init() 配合INIT_BOARD_EXPORT实现上电自动注册

## 三、与PIN框架对应关系
|组件|PIN框架|本LED框架(virtu_dev)|
|---|---|---|
|头文件|`pin.h`|`virtu_dev.h`|
|框架源码|`pin.c`|`virtu_dev.c`(上半部分)|
|BSP源码|`drv_gpio.c`|`virtu_dev.c`(下半部分)|
|ops结构体|`struct rt_pin_ops`|`struct rt_led_ops`|
|设备结构体|`struct rt_device_pin`|`struct rt_device_led`|
|注册函数|`rt_device_pin_register()`|`rt_device_led_register()`|
|用户API|`rt_pin_write()`|`rt_led_on()`|
|BSP实现函数|`stm32_pin_write()`|`stm32_led_on()`|
|ops实例|`_stm32_pin_ops`|`_stm32_led_ops`|
|初始化函数|`rt_hw_pin_init()`|`rt_hw_led_init()`|
|自动注册宏|`INIT_BOARD_EXPORT(rt_hw_pin_init)`|`INIT_BOARD_EXPORT(rt_hw_led_init)`|

## 四、关键数据结构
### 1. rt_led_ops 操作接口表
```c
struct rt_led_ops
{
    void (*led_init)(struct rt_device *device, rt_base_t pin, rt_base_t mode);
    void (*led_on)(struct rt_device *device, rt_base_t pin);
    void (*led_off)(struct rt_device *device, rt_base_t pin);
    void (*led_toggle)(struct rt_device *device, rt_base_t pin);
};
```
- `led_init`：初始化指定LED引脚，设置引脚工作模式
- `led_on`：点亮指定LED引脚
- `led_off`：熄灭指定LED引脚
- `led_toggle`：LED翻转，示例中实现循环闪烁10次逻辑

### 2. rt_device_led 自定义设备结构体
```c
struct rt_device_led
{
    struct rt_device parent;
    const struct rt_led_ops *ops;
};
```
- `parent`：继承RT‑Thread标准`rt_device`，放在结构体第一个成员，用于向系统设备管理器注册
- `ops`：保存BSP层传入的LED操作函数指针表，框架层依靠该指针间接调用底层硬件实现

## 五、设备注册流程
```
系统上电启动
  -> rt_hw_led_init()                              // INIT_BOARD_EXPORT自动调用
    -> rt_device_led_register("led", &_stm32_led_ops, RT_NULL)
      -> 填充 _hw_led.parent 的设备基础属性，全部原生rt_device接口置RT_NULL
      -> _hw_led.ops = ops;                         // 保存BSP传入ops函数指针表
      -> rt_device_register(&_hw_led.parent, "led", RT_DEVICE_FLAG_RDWR)
        -> LED设备注册进入RT‑Thread设备管理器
```
注册完成后，应用层直接调用`rt_led_xxx()`系列语义API完成LED控制。

## 六、API调用链示例（以 rt_led_on(PIN_LED_B) 为例）
```
rt_led_on(PIN_LED_B)                     // 应用层调用对外API
  -> _hw_led.ops->led_on(&_hw_led.parent, pin)   // 框架层，通过函数指针转发
    -> stm32_led_on(device, pin)         // BSP硬件层实现
      -> rt_kprintf("led on\n");
      -> rt_pin_write(pin,PIN_LOW);      // 底层调用RTT PIN框架操作真实GPIO
```

## 七、与通用rt_device驱动(test_drv.c)对比
`test_drv.c`直接复用`rt_device`内置`init/read/write/control`，上层需要使用`rt_device_find` + `rt_device_write`传递缓冲区完成控制。
本LED框架仿照PIN框架，自定义专属ops与语义API。

|对比项|test_drv.c(通用rt_device)|本LED自定义框架|
|---|---|---|
|ops定义|无，直接复用rt_device原生接口|自定义`struct rt_led_ops`|
|用户调用API|`rt_device_write(dev,pos,buf,size)`|`rt_led_on(pin)`语义化函数|
|参数传递|依靠buffer缓冲区+offset传递参数|直接传入pin引脚号参数|
|代码可读性|需要查阅协议文档解析buffer内容|函数名直观，参数含义明确|
|可移植性|更换硬件，应用层代码需要修改|仅需要重新实现一套`rt_led_ops`，应用层代码无需改动|

## 八、实验验证
### 测试代码
```c
#define PIN_LED_B       rt_pin_get("PF.11")
#define PIN_MODE        PIN_MODE_OUTPUT
static void virtu_dev_test(void)
{
    rt_led_init(PIN_LED_B,PIN_MODE);
    rt_led_on(PIN_LED_B);
    rt_thread_mdelay(2000);
    rt_led_off(PIN_LED_B);
    rt_thread_mdelay(2000);
    rt_led_toggle(PIN_LED_B);
}
MSH_CMD_EXPORT(virtu_dev_test, virtu_dev sample);
```

### 预期串口输出
```
msh >virtu_dev_test
led init success
led on
led off
led toggle...
count: 1
count: 2
count: 3
count: 4
count: 5
count: 6
count: 7
count: 8
count: 9
count: 10
```

### 结果分析
1. `rt_led_init()`通过ops调用BSP层`stm32_led_init`，打印日志，调用`rt_pin_mode`完成GPIO初始化。
2. `rt_led_on()`点亮LED，延时2秒；`rt_led_off()`熄灭LED，延时2秒。底层调用PIN框架接口操作真实PF.11引脚。
3. `rt_led_toggle()`执行闪烁逻辑，循环10次，打印count计数。
4. 全部上层API都通过框架层保存的ops函数指针跳转到BSP层实现；框架层完全不直接操作硬件引脚，做到软硬件解耦。

## 九、总结
本项目完整复刻RT‑Thread PIN驱动框架核心思想：
1. **分层设计**：应用层‑框架层‑BSP硬件层三层分离，各层职责明确。框架层不包含硬件操作代码。
2. **接口隔离**：框架层定义`rt_led_ops`接口契约；BSP层完成硬件实现，通过函数指针表和框架层解耦。
3. **自动注册机制**：`INIT_BOARD_EXPORT`完成上电自动注册，BSP将ops函数指针注入框架。
4. **语义化API**：对外提供`rt_led_on/off/init/toggle`，调用直观，相比原生`rt_device`的read/write更友好。
5. **良好可移植性**：切换不同硬件平台时，仅需要重新实现一套`rt_led_ops`；框架层代码、上层应用测试代码完全不用修改。
