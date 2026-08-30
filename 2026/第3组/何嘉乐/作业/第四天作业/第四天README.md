# Day4 实验报告：虚拟 I2C 设备驱动框架 test_dev 设计与实现

## 一、实验目的

- 理解 RT-Thread 设备驱动框架的分层设计思想
- 理解 `struct rt_device` 在设备管理中的作用
- 掌握 `ops` 函数指针表的设计与使用
- 理解 `user_data` 保存设备私有数据的方法
- 掌握 `rt_device_register()` 和 `rt_device_find()` 的设备注册与查找机制
- 模仿 RT-Thread PIN 设备框架，自主设计一个虚拟设备驱动框架 `test_dev`
- 使用虚拟 I2C 寄存器设备验证框架层与底层驱动层之间的调用关系


## 二、实验环境

- RT-Thread 版本：RT-Thread 4.1.1
- 开发平台：STM32 + RT-Thread
- 开发工具：RT-Thread Studio
- 调试方式：串口 Console + msh
- 虚拟设备名称：test_dev
- 虚拟 I2C 地址：0x50
- 虚拟寄存器空间：256 Byte


## 三、实验原理

### 3.1 RT-Thread 设备驱动分层思想

应用层
    ↓
test_dev 框架层
    ↓
ops 函数指针
    ↓
virtual_i2c 虚拟驱动层
    ↓
虚拟寄存器 regs[256]


### 3.2 test_dev 设备结构

重点分析：

- `struct rt_device parent`
- `const struct rt_test_ops *ops`

说明：

- parent 用于接入 RT-Thread 通用设备管理框架
- ops 用于保存 test_dev 专用设备操作函数


### 3.3 rt_test_ops 操作接口

介绍：

- init
- write_reg
- read_reg
- write_regs
- read_regs

说明为什么不直接在框架层调用 virtual_i2c_xxx()。


### 3.4 ops 与 user_data

重点说明：

ops：
用于找到“设备应该怎样操作”

user_data：
用于找到“设备自己的数据在哪里”

形成：

ops       -> 操作方法
user_data -> 设备数据


### 3.5 虚拟 I2C 寄存器设计

虚拟 I2C 地址：

0x50

虚拟寄存器：

| 地址      | 名称       | 初始值 | 属性   |
| --------- | ---------- | ------ | ------ |
| 0x00      | DEVICE_ID  | 0xA5   | 只读   |
| 0x01      | STATUS     | 0x01   | 可读   |
| 0x02      | CONFIG     | 0x00   | 可读写 |
| 0x03      | DATA       | 0x00   | 可读写 |
| 0x04~0xFF | 普通寄存器 | 0x00   | 可读写 |


## 四、实验内容一：设计 test_dev 框架层

### 实验内容大纲

#### 是什么知识点

- 自定义设备结构体
- struct rt_device
- ops 函数指针表
- 设备专用 API

#### 为什么这样做

- 模仿 RT-Thread PIN 驱动框架
- 将框架层与具体硬件实现分离
- 避免应用直接依赖底层 virtual_i2c 实现

#### 意义是什么

- 提高驱动代码的模块化程度
- 实现接口与实现解耦
- 为不同底层实现提供统一上层 API


### 4.1 test_dev.h 设计

介绍：

- TEST_DEV_REG_SIZE
- struct rt_test_ops
- struct rt_test_device
- test_dev_register()
- test_dev_read/write API


### 4.2 test_dev.c 框架实现

重点分析：

test_dev_write_reg()
    ↓
dev->ops->write_reg()

test_dev_read_reg()
    ↓
dev->ops->read_reg()

说明框架层本身不操作虚拟寄存器。


## 五、实验内容二：设计虚拟 I2C 底层驱动

### 实验内容大纲

#### 是什么知识点

- 设备私有数据
- user_data
- 虚拟寄存器
- ops 具体实现

#### 为什么这样做

- 没有依赖真实 I2C 硬件
- 使用内存数组模拟 I2C 寄存器设备
- 将重点放在设备驱动框架设计上

#### 意义是什么

- 可以验证完整设备驱动调用链
- 降低硬件因素对实验的影响
- 方便理解框架层与驱动实现层的关系


### 5.1 virtual_i2c_data

包含：

- addr
- regs[256]


### 5.2 virtual_i2c_init()

初始化：

- I2C 地址 0x50
- DEVICE_ID = 0xA5
- STATUS = 0x01
- CONFIG = 0x00
- DATA = 0x00


### 5.3 单寄存器读写

实现：

- virtual_i2c_write_reg()
- virtual_i2c_read_reg()


### 5.4 连续寄存器读写

实现：

- virtual_i2c_write_regs()
- virtual_i2c_read_regs()


### 5.5 保护机制

实现：

- DEVICE_ID 只读保护
- 256 Byte 寄存器边界保护


## 六、实验内容三：ops 接口绑定与设备注册

### 实验内容大纲

#### 是什么知识点

- 函数指针表
- rt_device_register()
- INIT_DEVICE_EXPORT()
- RT-Thread 自动初始化机制

#### 为什么这样做

- 需要将 test_dev 框架定义的接口和 virtual_i2c 的具体实现连接
- 需要把 test_dev 加入 RT-Thread 设备管理器

#### 意义是什么

- 实现框架层和驱动层的动态绑定
- 使设备能够通过设备名称进行统一管理
- 体现 RT-Thread 驱动注册机制


### 6.1 virtual_i2c_ops

建立：

rt_test_ops
    ↓
virtual_i2c_ops
    ├── virtual_i2c_init
    ├── virtual_i2c_write_reg
    ├── virtual_i2c_read_reg
    ├── virtual_i2c_write_regs
    └── virtual_i2c_read_regs


### 6.2 test_dev_register()

注册：

virtual_i2c_dev
    +
virtual_i2c_ops
    +
virtual_i2c
    ↓
test_dev_register()
    ↓
rt_device_register()


### 6.3 自动初始化

INIT_DEVICE_EXPORT(virtual_i2c_hw_init)

启动输出：

[test_dev] virtual i2c init
[test_dev] addr = 0x50
[test_dev] device id = 0xA5
[test_dev] register success


## 七、实验内容四：应用层功能测试

### 实验内容大纲

#### 是什么知识点

- rt_device_find()
- 设备对象类型转换
- 设备专用 API 调用
- 驱动功能验证

#### 为什么这样做

- 验证 test_dev 是否真正加入 RT-Thread 设备管理器
- 验证应用层是否能够经过框架层访问虚拟 I2C 设备

#### 意义是什么

- 验证整个设备驱动框架是否真正打通
- 验证框架抽象不是停留在代码结构上，而是可以实际运行


### 7.1 查找设备

rt_device_find("test_dev")

真实结果：

[test_demo] test_dev found


### 7.2 DEVICE_ID 读取测试

读取：

0x00

真实结果：

[test_dev] read reg[0x00] = 0xA5
[test_demo] device id = 0xA5


### 7.3 单寄存器读写测试

写入：

CONFIG(0x02) = 0x55

真实结果：

[test_dev] write reg[0x02] = 0x55
[test_dev] read reg[0x02] = 0x55
[test_demo] config = 0x55


### 7.4 连续寄存器读写测试

写入：

0x10 -> 0x11
0x11 -> 0x22
0x12 -> 0x33
0x13 -> 0x44

真实结果：

[test_dev] write 4 regs from 0x10
[test_dev] read 4 regs from 0x10
[test_demo] read data: 0x11 0x22 0x33 0x44


## 八、实验内容五：异常访问与保护测试

### 实验内容大纲

#### 是什么知识点

- 参数合法性检查
- 只读寄存器
- 地址越界保护
- 驱动健壮性

#### 为什么这样做

- 驱动程序不能只处理正常输入
- 错误访问如果不进行检查可能导致寄存器数据异常或内存越界

#### 意义是什么

- 提高驱动程序可靠性
- 验证设备框架具有基本的错误处理能力


### 8.1 DEVICE_ID 只读测试

尝试：

DEVICE_ID = 0x66

真实结果：

[test_demo] test read-only register
[test_dev] reg 0x00 is read only
[test_demo] read-only protection works
[test_dev] read reg[0x00] = 0xA5
[test_demo] device id after write = 0xA5

结论：

DEVICE_ID 没有被修改，只读保护正常。


### 8.2 地址越界测试

从：

0xFE

连续写：

4 Byte

实际需要访问：

0xFE
0xFF
0x100
0x101

其中 0x100、0x101 已超出 256 Byte 寄存器空间。

真实结果：

[test_demo] test boundary protection
[test_demo] boundary protection works

结论：

寄存器边界保护正常。


## 九、完整调用链分析

### 9.1 单寄存器写调用链

test_demo
    ↓
test_dev_write_reg()
    ↓
dev->ops->write_reg()
    ↓
virtual_i2c_write_reg()
    ↓
dev->user_data
    ↓
virtual_i2c.regs[reg] = value


### 9.2 单寄存器读调用链

test_demo
    ↓
test_dev_read_reg()
    ↓
dev->ops->read_reg()
    ↓
virtual_i2c_read_reg()
    ↓
dev->user_data
    ↓
value = virtual_i2c.regs[reg]


## 十、与 RT-Thread PIN 驱动框架对比

| PIN 框架                 | test_dev                |
| ------------------------ | ----------------------- |
| struct rt_device_pin     | struct rt_test_device   |
| struct rt_pin_ops        | struct rt_test_ops      |
| _stm32_pin_ops           | virtual_i2c_ops         |
| rt_device_pin_register() | test_dev_register()     |
| rt_pin_write()           | test_dev_write_reg()    |
| stm32_pin_write()        | virtual_i2c_write_reg() |
| GPIO 硬件                | regs[256] 虚拟寄存器    |

说明：

test_dev 并不是复制 PIN 驱动，而是将 PIN 框架中的设备抽象、ops、注册机制和分层思想迁移到新的虚拟设备中。


## 十一、实验中出现的问题及解决方法

### 11.1 virtual_i2c_ops 未声明

编译错误：

virtual_i2c_ops undeclared

同时出现：

virtual_i2c_init defined but not used
virtual_i2c_write_reg defined but not used
...

#### 原因：

缺少 virtual_i2c_ops 函数指针表。

#### 解决：

定义：

static const struct rt_test_ops virtual_i2c_ops

并将 5 个 virtual_i2c_xxx() 函数绑定到对应接口。


## 十二、实验结果

汇总真实实验结果：

![image-20260820204023110](figures/image-20260820204023110.png)

- test_dev 注册成功
- rt_device_find() 查找成功
- DEVICE_ID = 0xA5
- CONFIG 写入并读回 0x55
- 连续读写结果为 0x11 0x22 0x33 0x44
- DEVICE_ID 只读保护正常
- 寄存器越界保护正常


## 十三、理论总结

重点总结：

1. RT-Thread 设备驱动的分层思想
2. struct rt_device 的作用
3. ops 函数指针表的作用
4. user_data 的作用
5. 设备注册与查找机制
6. 框架层与底层实现解耦
7. 虚拟驱动对于理解真实设备驱动框架的意义


## 十四、实验总结

总结本次从 PIN 驱动框架源码分析，到自主设计 test_dev，再到完成虚拟 I2C 底层驱动、设备注册、功能测试和异常保护的完整过程。