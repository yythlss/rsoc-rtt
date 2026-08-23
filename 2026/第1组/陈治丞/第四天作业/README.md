# 【DAY4】RT-Thread PIN 设备对接与 test_dev 驱动框架

- 姓名：陈治丞
- 组别：第 1 组
- 开发板：STM32F407 星火一号

## 一、作业说明

本次作业围绕 RT-Thread PIN 设备对接和自定义设备框架展开。源码文件为 `test_dev.c`，使用星火一号的板载红灯和按键作为测试对象：红灯对应 `PF12`，`KEY_UP` 对应 `PC5`。

本版本暂不填写串口截图和实测数值，避免把未实际运行的结果当作实验数据。完成烧录后，可根据真实输出补充实验现象。

## 二、PIN 设备对接

RT-Thread 通过 PIN 设备接口屏蔽底层 GPIO 寄存器差异。应用层使用 `rt_pin_mode()` 配置引脚方向，使用 `rt_pin_write()` 输出电平，使用 `rt_pin_read()` 读取输入电平。

| 功能 | 引脚 | 配置 |
| --- | --- | --- |
| `test_dev` 输出 | PF12 | 推挽输出，低电平点亮 LED_R |
| `test_dev` 输入 | PC5 | 上拉输入，按下 KEY_UP 时读取低电平 |

## 三、test_dev 设备框架

源码定义静态 `struct rt_device`，并通过 `rt_device_register()` 注册为 `test_dev`。设备操作函数包括初始化、打开、关闭、读取、写入和控制。`read` 读取按键电平，`write` 设置 LED 状态，`control` 提供状态读取和 LED 翻转。

初始化函数使用 `INIT_APP_EXPORT(test_dev_register)` 自动执行，不需要在 `main.c` 中手动调用。

## 四、MSH 测试命令

下载程序并进入 FinSH 后，在 `msh >` 后输入：

```text
test_dev read
test_dev on
test_dev off
test_dev toggle
```

`read` 读取 KEY_UP，`on` 点亮 LED_R，`off` 熄灭 LED_R，`toggle` 翻转 LED_R。

## 五、编译与验证

将 `test_dev.c` 放入 `applications` 目录，重新构建并下载。打开 `COM13`、`115200` 串口，逐条执行上述命令。按下和松开 KEY_UP 后再次执行 `test_dev read`，根据真实串口输出补充实验结果和截图。
