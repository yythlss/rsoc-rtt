# 1. 分析PIN设备如何对接的
RT-Thread 将 GPIO 抽象成一个名为 `pin` 的设备。应用程序不直接操作 STM32 的 GPIO 寄存器，而是调用统一的 PIN API；PIN 设备再调用底层 STM32 GPIO 驱动，最终由 HAL 库完成实际的引脚操作。
```c
应用程序
    ↓
rt_pin_mode()
rt_pin_read()
rt_pin_write()
rt_pin_attach_irq()
    ↓
RT-Thread PIN 设备管理层
    ↓
rt_pin_ops 操作函数
    ↓
STM32 GPIO 驱动
    ↓
HAL_GPIO_Init()
HAL_GPIO_ReadPin()
HAL_GPIO_WritePin()
    ↓
STM32 GPIO 硬件
```
- 例如，KEY0 设置为输入上拉并读取电平：
```c
rt_pin_mode(KEY0_PIN, PIN_MODE_INPUT_PULLUP);

if (rt_pin_read(KEY0_PIN) == PIN_LOW)
{
    rt_kprintf("KEY0 pressed\n");
}
```
- LED 设置为输出并点亮：
```
rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
rt_pin_write(LED_R_PIN, PIN_LOW);
```
- 在 STM32 PIN 驱动中，底层会定义一组操作函数：
```c
static const struct rt_pin_ops stm32_pin_ops =
{
    stm32_pin_mode,
    stm32_pin_write,
    stm32_pin_read,
    stm32_pin_attach_irq,
    stm32_pin_detach_irq,
    stm32_pin_irq_enable,
    stm32_pin_get,
};
```
- 然后注册为 RT-Thread 的 `pin` 设备：
```c
rt_device_pin_register("pin",
                       &stm32_pin_ops,
                       RT_NULL);
```
注册成功后，应用层使用的 `rt_pin_mode()`、`rt_pin_read()` 和 `rt_pin_write()` 就会自动调用对应的底层操作函数。

底层驱动会完成以下工作：
1. 保存按键引脚、中断模式和回调函数；
2. 配置 STM32 EXTI 外部中断；
3. 设置 NVIC 中断优先级；
4. 按键触发后进入 GPIO 中断服务函数；
5. 中断服务函数调用之前保存的回调函数。

因此，PIN 设备完成了应用层和芯片 GPIO 硬件之间的适配，使上层代码只依赖 RT-Thread 的统一接口，而不依赖具体 STM32 寄存器。更换芯片时，只需要修改底层 PIN 驱动，上层的 LED、按键和 GPIO 应用代码通常不需要改变。
# 2. 自己设计一个虚拟的设备驱动框架层（test_dev）
## 代码
```c
/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-20     RT-Thread    first version
 */


/*
int main(void)
{
    int count = 1;

    while (count++)
    {
        LOG_D("Hello RT-Thread!");
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}
*/

#include <rtthread.h>
#include <rtdevice.h>
#ifdef RT_USING_MSH
#include <finsh.h>
#endif

#define DBG_TAG "test_dev"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 宏定义 */
#define TEST_DEV_NAME    "test_dev"
#define TEST_BUFFER_SIZE 64

struct test_dev_data
{
    rt_uint8_t buffer[TEST_BUFFER_SIZE];
    rt_size_t length;
};

static rt_device_t test_dev;
static struct test_dev_data test_data;

/* 设备初始化 */
static rt_err_t test_dev_init(rt_device_t dev)
{
    struct test_dev_data *data;

    data = (struct test_dev_data *)dev->user_data;
    data->length = 0;
    rt_memset(data->buffer, 0, sizeof(data->buffer));

    rt_kprintf("test_dev init\n");
    return RT_EOK;
}

/* 打开设备 */
static rt_err_t test_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    RT_UNUSED(dev);

    rt_kprintf("test_dev open, flag=%u\n",
               (unsigned int)oflag);
    return RT_EOK;
}

/* 关闭设备 */
static rt_err_t test_dev_close(rt_device_t dev)
{
    RT_UNUSED(dev);

    rt_kprintf("test_dev close\n");
    return RT_EOK;
}

/* 读取设备 */
static rt_size_t test_dev_read(rt_device_t dev,
                               rt_off_t pos,
                               void *buffer,
                               rt_size_t size)
{
    struct test_dev_data *data;
    rt_size_t read_size;

    if (dev == RT_NULL || buffer == RT_NULL || pos < 0)
    {
        return 0;
    }

    data = (struct test_dev_data *)dev->user_data;

    if ((rt_size_t)pos >= data->length)
    {
        return 0;
    }

    read_size = data->length - (rt_size_t)pos;
    if (read_size > size)
    {
        read_size = size;
    }

    rt_memcpy(buffer, &data->buffer[pos], read_size);

    rt_kprintf("test_dev read, pos=%d size=%u\n",
               (int)pos,
               (unsigned int)read_size);

    return read_size;
}

/* 写入设备 */
static rt_size_t test_dev_write(rt_device_t dev,
                                rt_off_t pos,
                                const void *buffer,
                                rt_size_t size)
{
    struct test_dev_data *data;
    rt_size_t write_size;

    if (dev == RT_NULL || buffer == RT_NULL || pos < 0)
    {
        return 0;
    }

    if ((rt_size_t)pos >= TEST_BUFFER_SIZE)
    {
        return 0;
    }

    data = (struct test_dev_data *)dev->user_data;

    write_size = TEST_BUFFER_SIZE - (rt_size_t)pos;
    if (write_size > size)
    {
        write_size = size;
    }

    rt_memcpy(&data->buffer[pos], buffer, write_size);

    if ((rt_size_t)pos + write_size > data->length)
    {
        data->length = (rt_size_t)pos + write_size;
    }

    rt_kprintf("test_dev write, pos=%d size=%u\n",
               (int)pos,
               (unsigned int)write_size);

    return write_size;
}

/* 控制设备 */
static rt_err_t test_dev_control(rt_device_t dev,
                                  int cmd,
                                  void *args)
{
    RT_UNUSED(dev);
    RT_UNUSED(args);

    rt_kprintf("test_dev control, cmd=%d\n", cmd);
    return RT_EOK;
}

/* 创建并注册虚拟设备 */
static int test_dev_register(void)
{
    rt_err_t result;

    if (test_dev != RT_NULL)
    {
        return RT_EOK;
    }

    if (rt_device_find(TEST_DEV_NAME) != RT_NULL)
    {
        rt_kprintf("%s already exists\n", TEST_DEV_NAME);
        return -RT_EBUSY;
    }

    test_dev = rt_device_create(RT_Device_Class_Char, 0);
    if (test_dev == RT_NULL)
    {
        rt_kprintf("test_dev create failed\n");
        return -RT_ENOMEM;
    }

    test_dev->user_data = &test_data;
    test_dev->init = test_dev_init;
    test_dev->open = test_dev_open;
    test_dev->close = test_dev_close;
    test_dev->read = test_dev_read;
    test_dev->write = test_dev_write;
    test_dev->control = test_dev_control;

    result = rt_device_register(test_dev,
                                TEST_DEV_NAME,
                                RT_DEVICE_FLAG_RDWR);
    if (result != RT_EOK)
    {
        rt_kprintf("test_dev register failed: %d\n",
                   result);
        rt_device_destroy(test_dev);
        test_dev = RT_NULL;
        return result;
    }

    rt_kprintf("test_dev registered\n");
    return RT_EOK;
}

/* MSH 测试命令 */
static int test_dev_demo(int argc, char *argv[])
{
    rt_device_t dev;
    char write_data[] = "hello rt-thread";
    char read_data[TEST_BUFFER_SIZE];
    rt_size_t read_size;
    rt_err_t result;

    RT_UNUSED(argc);
    RT_UNUSED(argv);

    result = test_dev_register();
    if (result != RT_EOK && result != -RT_EBUSY)
    {
        return result;
    }

    dev = rt_device_find(TEST_DEV_NAME);
    if (dev == RT_NULL)
    {
        rt_kprintf("test_dev not found\n");
        return -RT_ERROR;
    }

    result = rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);
    if (result != RT_EOK)
    {
        rt_kprintf("test_dev open failed: %d\n",
                   result);
        return result;
    }

    rt_memset(read_data, 0, sizeof(read_data));

    rt_device_write(dev, 0, write_data, sizeof(write_data) - 1);

    read_size = rt_device_read(dev,
                               0,
                               read_data,
                               sizeof(read_data) - 1);

    read_data[read_size] = '\0';

    rt_device_control(dev,
                      RT_DEVICE_CTRL_CONFIG,
                      RT_NULL);

    rt_kprintf("read data: %s\n", read_data);

    result = rt_device_close(dev);
    if (result != RT_EOK)
    {
        rt_kprintf("test_dev close failed: %d\n",
                   result);
        return result;
    }

    return RT_EOK;
}

MSH_CMD_EXPORT(test_dev_demo, test virtual device);

int main(void)
{
    return RT_EOK;
}

```
## 终端输出
```c

 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 20 2026 23:05:12
 2006 - 2022 Copyright by RT-Thread team
msh >test_dev_demo
test_dev registered
test_dev init
test_dev open, flag=3
test_dev write, pos=0 size=15
test_dev read, pos=0 size=15
test_dev control, cmd=3
read data: hello rt-thread
test_dev close
msh >list_device
device           type         ref count
-------- -------------------- ----------
test_dev Character Device     0
uart1    Character Device     2
pin      Pin Device           0
msh >
```
## 结果分析
- 执行 `test_dev_demo` 后，虚拟字符设备成功完成注册、初始化、打开、写入、读取、控制和关闭操作。读取出的数据与写入数据一致，说明设备的基本操作接口工作正常。
- 执行 `list_device` 可以看到 `test_dev` 显示为 Character Device，同时系统中的 `pin` 设备正常存在，说明虚拟设备已经接入 RT-Thread I/O 设备框架。