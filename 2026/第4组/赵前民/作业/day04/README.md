## RT-Thread PIN 设备对接完整分析

### 一、PIN设备整体架构

RT-Thread PIN 设备是对 MCU GPIO 的统一抽象框架，分层结构如下：

1. **应用层**：调用标准 `rt_pin_xxx` API，硬件无关
2. **PIN 框架层（pin.c/pin.h）**：实现通用封装，向上提供API，向下回调底层驱动函数指针
3. **BSP驱动适配层（drv_gpio.c）**：实现 `rt_pin_ops` 底层硬件操作，适配当前MCU寄存器/HAL库
4. **硬件层**：MCU GPIO外设寄存器

核心思想：**框架定义统一接口，不同芯片各自实现底层操作，应用代码可跨BSP移植**

> 头文件路径：`components/drivers/include/drivers/pin.h`

### 二、核心数据结构：rt_pin_ops

对接PIN设备最关键的结构体，驱动层必须实现全部函数指针：

```c
struct rt_pin_ops
{
    void (*pin_mode)(struct rt_device *device, rt_base_t pin, rt_base_t mode);
    void (*pin_write)(struct rt_device *device, rt_base_t pin, rt_base_t value);
    int (*pin_read)(struct rt_device *device, rt_base_t pin);
    rt_err_t (*pin_attach_irq)(struct rt_device *device, rt_int32_t pin,
                      rt_uint32_t mode, void (*hdr)(void *args), void *args);
    rt_err_t (*pin_detach_irq)(struct rt_device *device, rt_int32_t pin);
    rt_err_t (*pin_irq_enable)(struct rt_device *device, rt_base_t pin, rt_uint32_t enabled);

    rt_base_t (*pin_get)(const char *name);
};
```

| 成员           | 功能说明                              |
| -------------- | ------------------------------------- |
| pin_mode       | 设置引脚模式：输入/输出/上下拉/开漏等 |
| pin_write      | 输出高低电平                          |
| pin_read       | 读取引脚输入电平                      |
| pin_attach_irq | 绑定外部中断回调                      |
| pin_detach_irq | 解绑外部中断回调                      |
| pin_irq_enable | 开启/关闭引脚中断                     |
| pin_get        | 根据引脚名字获取PIN编号（可选）       |

### 三、完整对接流程（BSP驱动适配步骤）

#### 步骤1：定义引脚编号映射表

RTT PIN编号 ≠ 芯片GPIO编号，需要自定义映射： 示例：`PA0 → PIN_NUM_0(0)`，`PA1 → PIN_NUM_1(1)`

```c
#define LED_PIN        1
#define KEY_PIN        2
```

> 重点：驱动内部要实现 **PIN编号 → GPIO端口+引脚号** 的转换逻辑

#### 步骤2：实现 rt_pin_ops 全部底层回调函数

在 `drv_gpio.c` 实现：

- xxx_pin_mode()
- xxx_pin_write()
- xxx_pin_read()
- xxx_pin_attach_irq()
- xxx_pin_detach_irq()
- xxx_pin_irq_enable()
- xxx_pin_get()

#### 步骤3：调用注册接口，接入PIN框架

使用 `rt_device_pin_register` 将 ops 注册到系统设备管理器：

```c
int rt_hw_pin_init(void)
{
    // name固定一般为"pin"，ops为上面实现的操作集，user_data一般RT_NULL
    return rt_device_pin_register("pin", &stm32_pin_ops, RT_NULL);
}
```

> 调用时机：`rt_hw_board_init()` 中执行 `rt_hw_pin_init()`，系统上电初始化注册

#### 步骤4：Kconfig 开启PIN驱动支持

`Kconfig` 配置项：

```c
config RT_USING_PIN
    bool "Enable PIN driver"
    default y
```

`rtconfig.h` 中自动生成宏 `#define RT_USING_PIN`，开启PIN框架编译

### 四、应用层使用PIN设备（对接完成后调用）

注册成功后，应用层直接调用标准API，无需关心底层硬件：

```c
#include <drivers/pin.h>

void pin_sample(void)
{
    /* 设置LED引脚为推挽输出 */
    rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_PIN, PIN_HIGH);

    /* 设置按键为上拉输入，下降沿中断 */
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(KEY_PIN, PIN_IRQ_MODE_FALLING, key_cb, RT_NULL);
    rt_pin_irq_enable(KEY_PIN, PIN_IRQ_ENABLE);
}
```

标准应用API汇总：

| API                 | 功能              |
| ------------------- | ----------------- |
| rt_pin_mode()       | 设置引脚模式      |
| rt_pin_write()      | 输出电平          |
| rt_pin_read()       | 读取电平          |
| rt_pin_attach_irq() | 绑定中断回调      |
| rt_pin_detach_irq() | 解绑中断          |
| rt_pin_irq_enable() | 使能/关闭中断     |
| rt_pin_get()        | 根据名字获取PIN号 |

### 五、调用数据流（完整调用链路）

```
应用层 rt_pin_mode()
        ↓
PIN框架层 pin.c 封装接口
        ↓
框架内部调用 ops->pin_mode()
        ↓
BSP驱动 stm32_pin_mode()
        ↓
HAL库 / 寄存器操作 GPIO硬件
```



## 虚拟的设备驱动框架层（test_dev）

### 虚拟设备代码实现

```c
static rt_err_t drv_test_init(rt_device_t dev)
{
    LOG_I("test drv init\n");
    return RT_EOK;
}

static rt_err_t drv_test_open(rt_device_t dev, rt_uint16_t oflag)
{
    LOG_I("test drv open flag = %d\n", oflag);
    return RT_EOK;
}

static rt_err_t drv_test_close(rt_device_t dev)
{
    LOG_I("test drv close");
    return RT_EOK;
}

static rt_size_t drv_test_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    LOG_I("test drv read pos = %d, size = %d\n", pos, size);
    return size;
}

static rt_size_t drv_test_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    LOG_I("test drv write pos = %d, size = %d\n", pos, size);
    return size;
}

static rt_err_t drv_test_control(rt_device_t dev, int cmd, void *args)
{
    LOG_I("test drv control cmd = %d\n", cmd);
    return RT_EOK;
}

static int rt_dev_test_init(void)
{
    rt_device_t test_dev =rt_device_create(RT_Device_Class_Char, 0);
    if(!test_dev)
    {
        LOG_E("test_dev create failed");
        return -RT_ERROR;
    }

    test_dev->init = drv_test_init;
    test_dev->open = drv_test_open;
    test_dev->close = drv_test_close;
    test_dev->read = drv_test_read;
    test_dev->write = drv_test_write;
    test_dev->control = drv_test_control;

    if(rt_device_register(test_dev,"test_dev", RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        LOG_E("test_dev register failed!");
        return -RT_ERROR;
    }
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_dev_test_init);
```

### 串口输出结果

```c
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 20 2026 11:46:44
 2006 - 2022 Copyright by RT-Thread team
[2] I/main: test drv init

[5] I/main: test drv open flag = 3

[10] I/main: test drv control cmd = 1

[15] I/main: test drv read pos = 0, size = 2

[20] I/main: test drv write pos = 0, size = 2

[26] I/main: test drv close
```

