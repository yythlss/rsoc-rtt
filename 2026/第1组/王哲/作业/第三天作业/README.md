# RT-Thread 线程间同步与通信课后作业

## 一、作业要求

要求：

1. 不得直接复制 `kernel_samples` 示例后只修改变量名和打印内容。
2. 每个练习至少创建两个线程。
3. 所有线程和 IPC 对象都要检查创建结果。
4. 程序需要通过 MSH 命令启动。
5. 提交理论总结与实验报告（`README.md`）+ 源代码（`*.c`）。

---

## 练习一：停车场车位管理

### 场景

一个停车场只有 3 个车位，同时有 5 辆车尝试进入。车位属于有限资源，车辆只有获得车位后才能进入。

### 功能要求

1. 创建一个初始值为 3 的计数信号量，表示剩余车位。
2. 创建 5 个车辆线程。
3. 每辆车到达后申请一个车位。
4. 获得车位后打印车辆编号和进入信息。
5. 车辆停留一段时间后离开，并释放车位。
6. 未在 2 秒内获得车位的车辆打印等待超时信息，稍后重新申请。
7. 任意时刻进入停车场的车辆数不能超过 3。

### 示例输出

```text
car1 entered, used spaces: 1
car2 entered, used spaces: 2
car3 entered, used spaces: 3
car4 waiting for a space
car2 left
car4 entered, used spaces: 3
```

### 思考题

为什么这里使用计数信号量，而不是互斥量？

---

## 练习二：多窗口售票系统

### 场景

两个售票窗口同时出售同一批车票。系统初始有 20 张票，每次只能由一个窗口修改剩余票数。

### 功能要求

1. 创建两个售票线程，分别代表窗口 A 和窗口 B。
2. 使用一个全局变量保存剩余票数。
3. 每个窗口循环完成以下操作：
   - 检查是否还有余票；
   - 将余票减 1；
   - 记录售出的票号；
   - 打印售票结果。
4. 使用互斥量保护完整的售票过程。
5. 售完后两个线程自动退出。
6. 最终必须满足：
   - 20 张票全部售出；
   - 不出现相同票号；
   - 剩余票数不能小于 0。

### 示例输出

```text
window A sold ticket 20
window B sold ticket 19
window B sold ticket 18
...
all tickets sold
remaining tickets: 0
```

### 对比实验

先暂时不使用互斥量，并在“读取余票”和“修改余票”之间加入短暂延时，观察是否会出现重复售票或余票错误。

随后加入互斥量并再次运行，对比两次结果。

### 思考题

为什么只给 `ticket_count--` 加锁仍可能不安全？

---

## 练习三：系统启动条件检查

### 场景

设备启动前需要完成三个独立模块的初始化：

- 网络模块初始化；
- 传感器模块初始化；
- 存储模块初始化。

只有三个模块全部准备完成，主业务线程才能启动。

PS：只是举例，模拟即可。

### 功能要求

定义三个事件位：

```c
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
```

创建以下线程：

- 网络初始化线程；
- 传感器初始化线程；
- 存储初始化线程；
- 主业务线程。

每个初始化线程经过不同时间的延时后，发送对应的就绪事件。

主业务线程必须使用 AND 模式等待三个事件全部到达，然后打印：

```text
all modules are ready
business task started
```

另外增加一次超时测试：

- 将存储模块初始化时间设置为 5 秒；
- 主业务线程最多等待 3 秒；
- 超时后打印尚未完成的模块，并再次等待。

### 示例输出

```text
network ready
sensor ready
wait modules timeout, storage is not ready
storage ready
all modules are ready
business task started
```

### 思考题

如果用三个独立信号量实现这个功能，与使用事件集相比有什么区别？

---

## 练习四：快递分拣中心

### 场景

两个收件线程不断产生快递信息，一个分拣线程从消息队列中取出快递并按照目的地区域分类。

### 消息结构

```c
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};
```

其中：

```text
region = 1：华东
region = 2：华南
region = 3：华北
```

### 功能要求

1. 创建一个消息队列，队列容量为 5 条消息。
2. 创建两个发件线程，每个线程产生 5 条快递消息。
3. 快递编号不得重复。
4. 分拣线程接收消息，并打印快递编号、区域和重量。
5. 分拣完成后统计：
   - 总快递数量；
   - 各区域快递数量；
   - 所有快递的总重量。
6. 当队列已满时，发送线程不得崩溃，应记录发送失败次数。
7. 两个发送线程完成后，向分拣线程发送一条结束消息。
8. 分拣线程收到结束消息后打印统计结果并退出。

### 示例输出

```text
package 1001 -> east, weight: 1200 g
package 2001 -> south, weight: 850 g
package 1002 -> north, weight: 960 g
...
total packages: 10
east: 4, south: 3, north: 3
total weight: 10250 g
send failed: 0
```

---

## 二、MSH 命令要求

每个练习导出一个独立命令：

```text
parking_demo
ticket_demo
startup_demo
package_demo
```

重复执行命令时，不得因同名线程或 IPC 对象残留而发生异常。可以在任务结束后主动释放资源，也可以检测任务是否仍在运行并拒绝重复启动。
