# 设备驱动

### 一、RT-Thread I/O设备框架概念

**SPI**：驱动和设备分离，提供统一的API

更换MCU只需要改变对应的对接驱动；重新驱动设备，只需要重新编写设备驱动相关代码；同一API接口，学习成本低；分离后设备驱动可以入库，供公司其他项目使用，减少碎片化开发，防止反复造轮子；代码框架会变复杂

I/O提供一套接口open write read control close

SPI I2C GPIO RTC WDG特定API

**字符设备**：提供连续的数据流，应用程序可以顺序读取，通常不支持随机存取。此类设备支持按字节/字符来读写数据

**块设备**：应用程序可以随机访问设备数据，程序可自行确定读取数据的位置。不支持基于字符的寻址

**字符设备只能顺序读取，块设备可以随机读取**

### 二、RT-Thread I/O API

**创建/销毁设备**

```
rt_device_t rt_device_create(int type,int attach_size);
void rt_device_destroy(rt_device_t device);
```

**注册/注销**

```
rt_err_t rt_device_register(rt_device_t dev,const char* name,rt_uint8_t flags);
rt_err_t rt_device_unregister(rt_device_t dev);
```

**注册字符设备**

![image-20260822145612979](D:\TYPORA\figures\image-20260822145612979.png)

![image-20260822145554303](D:\TYPORA\figures\image-20260822145554303.png)

**查找设备/初始化设备**

```
rt_device_t rt_device_find(const char* name);
rt_err_t rt_device_init(rt_device_t dev);
```

**打开和关闭设备**

```
rt_err_t rt_device_open(rt_device_t dev,rt_uint16_t oflags);
rt_err_t rt_device_close(rt_device_t dev);
```

**控制设备**

```
rt_err_t rt_device_control(rt_device_t dev,rt_uint8_t cmd,void* arg);
```

**读取设备**

```
rt_size_t rt_device_read(rt_device_t dev,rt_off_t pos,void* buffer,rt_size_t size);
rt_size_t rt_device_write(rt_device_t dev,rt_off_t pos,const void* buffer,rt_size_t size);
```

**数据接收回调**

```
rt_err_t rt_device_set_rx_indicate(rt_device_t dev,rt_err_t(*rx_ind)(rt_device_t dev,rt_size_t size));
```

**数据发送回调**

```
rt_err_t rt_device_set_tx_complete(rt_device_t dev,rt_err_t (*tx_done)(rt_device_t dev,void *buffer));
```

### 三、RT-Thread GPIO 外设开发

芯片上的引脚一般分为：电源、时钟、控制、I/O

**可编程控制中断**：上升沿触发、下降沿触发、高电平触发、低电平触发、双边沿触发

**相关接口**

rt_pin_mode()：设置引脚模式

rt_pin_write()：设置引脚电平

rt_pin_read() ：读取引脚电平

rt_pin_attach_irq() ：绑定引脚中断回调函数

rt_pin_irq_enable() 使能引脚中断

rt_pin_detach_irq() ：脱离引脚中断回调函数

**配置GPIO引脚模式**

```
void rt_pin_mode(rt_base_t pin,rt_base_t mode);
```

**输出电平**

```
void rt_pin_write(rt_base_t pin,rt_base_t value);
```

value可取：PIN_LOW低电平，PIN_HIGH高电平

**读取引脚电平**

```
int rt_pin_read(rt_base_t pin);
```



### 四、RT-Thread I2C 外设开发

常见的I2C总线：

低速模式：10Kbit/s

标准模式：100Kbit/s

快速模式：400Kbit/s

高速模式：3.4Mbit/s

**起始位（S）**：在SCL为高电平时，SDA由高电平变为低电平

**结束位（P）**：在SCL为高电平时，SDA由低电平变为高电平

![image-20260822154735492](D:\TYPORA\figures\image-20260822154735492.png)

**应答（ACK）**：拉低SDA线，并在SCL为高电平期间保持SDA线位低电平

**非应答（NOACK）**：不要拉低SDA线（此时SDA线位高电平），并在SCL为高电平期间保持SDA线为高电平

**查找设备**

```
rt_device_t rt_device_find(const char* name);
```

```
struct rt_i2c_bus_device *i2c_bus;
i2c_bus = (struct rt_i2c_bus_device *)rt_device_find(name);
```

**传输**

```
rt_size_t rt_i2c_transfer(struct rt_i2c_bus_device *bus,struct rt_i2c_msg msgs[],rt_uint32_t num);
struct rt_i2c_msg
{
  rt_uint16_t addr;
  rt_uint16_t flags;
  rt_uint16_t len;
  rt_uint8_t *buf;
}
```

**消息数据类型**：从机地址、读写标志、读写数据字节数、读写缓冲区指针

**I2C使用思路**：查找I2C总线设备；构造msgs消息；启动transfer传输；处理结果

### 五、RT-Thread SPI 外设开发

**SPI串行外设接口**：是一种高速、全双工、同步通信总线，常用语短距离通讯，主要应用于EEPROM、FLASH、实时时钟、AD转换器、还有数字信号处理器和数字信号解码器之间

**挂载SPI设备**

```
rt_err_t rt_spi_bus_attach_device(struct rt_spi_device *device,const char *name,const char *bus_name,void *user_data)
```

**查找设备**

```
rt_device_t rt_device_find(const char* name);
```

**传输数据（一次通信）**

```
rt_size_t rt_spi_transfer(struct rt_spi_device *device,const void *send_buf,void *recv_buf,rt_size_t length);
```

**接收一次数据**

```
rt_size_t rt_spi_recv(struct rt_spi_device *device,void *recv_bif,rt_size_t length);
```

**连续两次发送数据**

```
rt_err_t rt_spi_send_then_send(struct rt_spi_device *device,const void *send_buf1,rt_size_t send_length1,const void *send_buf2,rt_size_t send_length2);
```

