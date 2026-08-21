# Day3 IPC 实验报告

## 实验一：停车场车位管理

### 实验大纲

| 项目           | 内容                                                         |
| -------------- | ------------------------------------------------------------ |
| 是什么知识点   | 计数信号量 `Semaphore`，主要涉及 `rt_sem_create()`、`rt_sem_take()`、`rt_sem_release()`、`rt_sem_delete()` 以及超时等待机制 |
| 为什么要这么做 | 停车场只有 3 个车位，但同时有 5 辆车申请进入，需要通过计数信号量限制同时获得资源的线程数量 |
| 意义是什么     | 掌握有限资源的管理方式，理解信号量如何在线程之间完成资源计数、阻塞等待、超时重试和资源释放 |

### 1. 实验目的

模拟只有 3 个车位的停车场，同时创建 5 个车辆线程。每辆车进入停车场前必须申请一个车位，离开时释放车位。当停车场已满时，车辆最多等待 2 秒，超时后打印等待信息，并在稍后重新申请。

通过该实验验证计数信号量对有限资源数量的控制作用。

### 2. 实验文件

```text
parking_demo.c
```

MSH 命令：

```text
parking_demo
```

### 3. 实验设计

停车场车位数量设置为：

```c
#define PARKING_SPACES 3
```

车辆线程数量设置为：

```c
#define CAR_NUM 5
```

创建初始值为 3 的计数信号量：

```c
parking_sem = rt_sem_create("park_sem",
                            PARKING_SPACES,
                            RT_IPC_FLAG_FIFO);
```

车辆进入前使用：

```c
rt_sem_take()
```

申请车位，并设置 2 秒超时时间。

车辆离开后使用：

```c
rt_sem_release()
```

归还车位。

所有车辆线程结束后使用：

```c
rt_sem_delete()
```

删除动态创建的信号量对象。

### 4. 实际运行结果

执行：

```text
msh >parking_demo
```

实际输出：

![524f1d54-c940-4f06-b499-3276fcf9362f](figures/524f1d54-c940-4f06-b499-3276fcf9362f.png)

```text
parking demo start
total spaces: 3, total cars: 5

car5 entered, used spaces: 1
car4 entered, used spaces: 2
car3 entered, used spaces: 3

car2 waiting for a space
car1 waiting for a space

car5 left, used spaces: 2
car2 entered, used spaces: 3

car4 left, used spaces: 2
car1 entered, used spaces: 3

car3 left, used spaces: 2
car2 left, used spaces: 1
car1 left, used spaces: 0

all cars finished
parking demo finished
```

### 5. 实验结果分析

实验启动后，car5、car4 和 car3 首先成功进入停车场：

```text
car5 entered, used spaces: 1
car4 entered, used spaces: 2
car3 entered, used spaces: 3
```

此时 3 个车位已经全部被占用，因此 car2 和 car1 无法获得信号量。在等待 2 秒后输出：

```text
car2 waiting for a space
car1 waiting for a space
```

car5 离开后释放一个信号量，car2 随后成功获得车位：

```text
car5 left, used spaces: 2
car2 entered, used spaces: 3
```

car4 离开后，car1 同样成功进入。

整个实验过程中：

```text
最大同时占用车位数 = 3
```

没有出现 4 辆或 5 辆车同时进入停车场的情况。

最终：

```text
car1 left, used spaces: 0
```

说明所有车辆均已离开，已使用车位数量恢复为 0。

### 6. 实验数据总结

| 项目             | 实际数据         |
| ---------------- | ---------------- |
| 总车位数量       | 3                |
| 车辆线程数量     | 5                |
| 首批进入车辆     | car5、car4、car3 |
| 首次等待车辆     | car2、car1       |
| 最大同时占用车位 | 3                |
| 最终占用车位     | 0                |
| 实验结果         | 正常完成         |

### 7. 问题与解决方法

运行过程中出现过：

```text
msh >car5 entered...
```

类似现象。

这是由于 MSH 命令执行完成后重新输出命令提示符，而车辆线程同时调用 `rt_kprintf()` 输出信息，导致两个输出在串口终端中发生交错。

该现象不会影响信号量以及线程的实际运行结果，因此无需修改核心逻辑。

### 8. 思考题

**为什么这里使用计数信号量，而不是互斥量？**

停车场存在 3 个相同的车位资源，因此需要允许最多 3 个车辆线程同时获得资源。

计数信号量可以通过初始值表示可用资源数量。本实验将信号量初始值设置为 3，每辆车进入时获取一次信号量，离开时释放一次信号量，从而保证同时进入停车场的车辆数量不超过 3。

互斥量主要用于保护只能由一个线程独占访问的共享资源。如果使用互斥量，同一时刻通常只能有一个车辆线程获得资源，无法正确表示停车场具有 3 个车位的实际情况。

------

## 实验二：多窗口售票系统

### 实验大纲

| 项目           | 内容                                                         |
| -------------- | ------------------------------------------------------------ |
| 是什么知识点   | 互斥量 `Mutex`、临界区以及多线程访问共享变量时产生的竞态条件 |
| 为什么要这么做 | 两个售票窗口同时修改同一个 `ticket_count`，如果缺少互斥保护，线程可能同时读取相同票号并产生重复售票 |
| 意义是什么     | 理解共享资源必须进行完整的互斥保护，掌握竞态条件产生的原因以及 Mutex 对临界区的保护作用 |

### 1. 实验目的

创建窗口 A 和窗口 B 两个售票线程，共同出售 20 张票。

首先进行无互斥量实验，在读取余票和修改余票之间加入延时，主动放大竞态条件。

随后使用互斥量保护完整售票过程，对比两次实验结果。

实验要求最终正确版本满足：

```text
20 张票全部售出
没有重复票号
余票不能小于 0
remaining tickets = 0
```

### 2. 实验文件

正确版本：

```text
ticket_demo.c
```

无互斥量对比版本：

```text
ticketdiffer_demo.c
```

对应 MSH 命令：

```text
ticket_demo
ticketdiffer_demo
```

------

### 3. 无互斥量对比实验

无互斥量版本中，两个线程均直接访问：

```c
ticket_count
```

在读取当前票号和修改余票之间故意加入：

```c
rt_thread_mdelay(50);
```

其主要过程为：

```c
ticket_no = ticket_count;

rt_thread_mdelay(50);

ticket_count--;
```

该延时增加了两个线程同时读取相同 `ticket_count` 的概率。

### 4. 无互斥量实际运行结果

实际运行后出现大量重复票号。

关键输出为：

![abd99143-5245-4c1d-a2c7-bc8249680eaf](figures/abd99143-5245-4c1d-a2c7-bc8249680eaf.png)

```text
window B sold ticket 20
window A sold ticket 20

window B sold ticket 18
window A sold ticket 18

window B sold ticket 16
window A sold ticket 16

window B sold ticket 14
window A sold ticket 14

window B sold ticket 12
window A sold ticket 12

window B sold ticket 10
window A sold ticket 10

window B sold ticket 8
window A sold ticket 8

window B sold ticket 6
window A sold ticket 6

...
```

后续票号 4 和票号 2 同样出现两个窗口同时输出的情况。

最终：

```text
all tickets sold
remaining tickets: 0
ticket differ demo finished
```

### 5. 无互斥量结果分析

虽然最终：

```text
remaining tickets: 0
```

但售票过程并不正确。

例如初始：

```text
ticket_count = 20
```

两个线程可能执行：

```text
窗口 A 读取 ticket_count = 20
窗口 B 读取 ticket_count = 20
```

因此两个线程保存的票号均为：

```text
ticket_no = 20
```

随后两个线程分别执行：

```c
ticket_count--;
```

虽然 `ticket_count` 最终从 20 减少到 18，但是两个窗口打印出的票号均为：

```text
ticket 20
```

于是出现：

```text
20、20
18、18
16、16
...
```

说明“读取票号”和“修改余票”并不是一个不可分割的操作，从而产生竞态条件。

------

### 6. 加入互斥量实验

正确版本使用：

```c
rt_mutex_take()
```

获取互斥量。

完整的以下过程均放入临界区：

```text
检查余票
   ↓
读取当前票号
   ↓
修改剩余票数
   ↓
打印售票结果
```

完成后调用：

```c
rt_mutex_release()
```

释放互斥量。

所有线程完成后调用：

![e5f26e3a-f99e-46ea-bd29-4e8cdb92c3d3](figures/e5f26e3a-f99e-46ea-bd29-4e8cdb92c3d3.png)

```c
rt_mutex_delete()
```

删除互斥量对象。

### 7. 加互斥量实际运行结果

实际输出：

```text
window B sold ticket 20
window A sold ticket 19
window B sold ticket 18
window A sold ticket 17
window B sold ticket 16
window A sold ticket 15
window B sold ticket 14
window A sold ticket 13
window B sold ticket 12
window A sold ticket 11
window B sold ticket 10
window A sold ticket 9
window B sold ticket 8
window A sold ticket 7
window B sold ticket 6
window A sold ticket 5
window B sold ticket 4
window A sold ticket 3
window B sold ticket 2
window A sold ticket 1

all tickets sold
remaining tickets: 0
ticket demo finished
```

### 8. 对比结果

| 对比项           | 无 Mutex         | 使用 Mutex        |
| ---------------- | ---------------- | ----------------- |
| 初始票数         | 20               | 20                |
| 两个窗口并发运行 | 是               | 是                |
| 重复售票         | 大量出现         | 未出现            |
| 实际票号         | 20、20、18、18…… | 20、19、18、17……1 |
| 最终余票         | 0                | 0                 |
| 是否存在竞态条件 | 是               | 否                |
| 售票结果         | 错误             | 正确              |

实验说明，即使最终余票为 0，也不能说明并发程序一定正确，还必须检查每次共享数据操作是否保持一致性。

### 9. 问题与解决方法

无互斥量实验中出现了两个窗口售出相同票号的问题。

原因是两个线程可以在同一时刻读取同一个 `ticket_count`，随后分别修改共享变量。

解决方法是使用互斥量，将：

```text
检查余票
读取票号
余票减 1
记录结果
```

作为一个完整的临界区进行保护。

### 10. 思考题

**为什么只给 `ticket_count--` 加锁仍可能不安全？**

售票操作不仅包含 `ticket_count--`，还包含检查余票和读取当前票号。

如果两个线程在获取锁之前都执行：

```c
ticket_no = ticket_count;
```

那么两个线程仍然可能读取到相同票号。

即使随后：

```c
ticket_count--;
```

分别受到互斥保护，也已经无法避免重复售票。

因此需要保护完整的售票过程，而不能只保护单独的一条减法语句。

------

## 实验三：系统启动条件检查

### 实验大纲

| 项目           | 内容                                                         |
| -------------- | ------------------------------------------------------------ |
| 是什么知识点   | 事件集 `Event`、事件位、`RT_EVENT_FLAG_AND`、事件超时等待    |
| 为什么要这么做 | 系统启动需要同时满足网络、传感器和存储三个独立条件，单个条件完成时业务线程不能提前启动 |
| 意义是什么     | 掌握一个线程同时等待多个条件的方法，理解事件集在多条件同步场景中的优势 |

### 1. 实验目的

模拟系统启动过程中的三个独立模块：

```text
网络模块
传感器模块
存储模块
```

分别定义三个事件：

```c
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
```

主业务线程使用 AND 模式等待三个事件全部完成。

同时设置：

```text
网络初始化：1 秒
传感器初始化：2 秒
存储初始化：5 秒
主业务线程首次最大等待：3 秒
```

通过该设计验证事件集的 AND 等待以及超时机制。

### 2. 实验文件

```text
startup_demo.c
```

MSH 命令：

```text
startup_demo
```

### 3. 实验设计

三个初始化线程分别完成初始化后发送对应事件：

```c
rt_event_send()
```

主业务线程第一次调用：

```c
rt_event_recv()
```

并设置：

```c
RT_EVENT_FLAG_AND
```

要求三个事件必须全部满足。

首次等待时间设置为：

```text
3 秒
```

由于存储线程需要：

```text
5 秒
```

因此第一次等待必然发生超时。

超时后程序检查未完成模块，并再次等待三个事件全部到达。

### 4. 实际运行结果

执行：

```text
msh >startup_demo
```

实际输出：

![da3d734b-9772-4860-8a95-941c0695e303](figures/da3d734b-9772-4860-8a95-941c0695e303.png)

```text
startup demo start

network ready
sensor ready

wait modules timeout, storage is not ready

storage ready

all modules are ready
business task started
startup demo finished
```

### 5. 实验结果分析

实验启动约 1 秒后：

```text
network ready
```

说明网络初始化线程完成。

约 2 秒后：

```text
sensor ready
```

说明传感器初始化线程完成。

此时存储模块尚未初始化完成，因此即使已经有两个事件到达，业务线程仍然不能启动。

主业务线程等待 3 秒后输出：

```text
wait modules timeout, storage is not ready
```

说明第一次 `rt_event_recv()` 等待发生超时，并正确判断出存储模块尚未完成。

约 5 秒时：

```text
storage ready
```

此时：

```text
NETWORK
    AND
SENSOR
    AND
STORAGE
```

三个条件全部满足，因此业务线程继续执行：

```text
all modules are ready
business task started
```

实验结果证明 `RT_EVENT_FLAG_AND` 可以正确实现多个条件全部满足后再继续运行的同步要求。

### 6. 实验数据总结

| 项目             | 实际结果         |
| ---------------- | ---------------- |
| 网络初始化时间   | 约 1 s           |
| 传感器初始化时间 | 约 2 s           |
| 存储初始化时间   | 约 5 s           |
| 首次最大等待时间 | 3 s              |
| 第一次等待       | 超时             |
| 未完成模块       | Storage          |
| 第二次等待       | 成功             |
| 业务线程启动条件 | 三个事件全部到达 |
| 最终结果         | 正常启动         |

### 7. 问题与解决方法

本实验中的首次超时属于主动设计的测试条件，并不是程序异常。

存储模块延时设置为 5 秒，而业务线程第一次仅等待 3 秒，因此能够稳定触发超时。

超时后没有直接退出业务线程，而是打印未完成模块并再次调用事件等待，使程序能够在存储模块最终完成后继续启动。

### 8. 思考题

**如果使用三个独立信号量，与使用事件集相比有什么区别？**

如果使用三个独立信号量，需要分别创建三个信号量，并由主业务线程依次获取：

```text
net_sem
sensor_sem
storage_sem
```

程序需要分别维护三个同步对象。

事件集可以使用不同的 bit 表示多个状态：

```text
bit0：network
bit1：sensor
bit2：storage
```

再使用：

```c
RT_EVENT_FLAG_AND
```

一次等待多个条件全部满足。

因此，对于“多个独立条件全部完成后才能继续”的场景，事件集的状态表示和等待逻辑更加集中、直观。

------

## 实验四：快递分拣中心

### 实验大纲

| 项目           | 内容                                                         |
| -------------- | ------------------------------------------------------------ |
| 是什么知识点   | 消息队列 `Message Queue`、结构体消息传递、生产者与消费者模型、队列满处理 |
| 为什么要这么做 | 两个收件线程需要把包含编号、区域和重量的完整快递信息发送给分拣线程，仅使用同步机制无法直接完成结构化数据传输 |
| 意义是什么     | 掌握线程之间传递复杂数据的方法，并理解消息缓存、发送失败、重试以及生产速度和消费速度不一致时的处理方式 |

### 1. 实验目的

创建两个快递发送线程，每个线程产生 5 条快递信息。

快递使用以下结构：

```c
struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t region;
    rt_uint16_t weight;
};
```

两个发送线程通过消息队列将快递信息发送给分拣线程。

分拣线程完成：

```text
快递编号输出
区域分类
重量输出
总数量统计
各区域数量统计
总重量统计
```

消息队列容量限制为 5 条，并记录队列满时的发送失败次数。

### 2. 实验文件

```text
package_demo.c
```

MSH 命令：

```text
package_demo
```

### 3. 实验设计

消息队列容量设置为：

```c
#define PACKAGE_QUEUE_SIZE 5
```

Sender 1 产生：

```text
1001 ~ 1005
```

Sender 2 产生：

```text
2001 ~ 2005
```

因此不存在快递编号重复问题。

两个发送线程使用：

```c
rt_mq_send()
```

发送消息。

分拣线程使用：

```c
rt_mq_recv()
```

接收完整的 `package_msg` 数据。

当消息队列已满时：

```c
rt_mq_send()
```

发送失败，程序记录：

```c
send_failed++;
```

随后等待一段时间并重新发送当前消息，避免快递数据丢失。

最后一个发送线程完成后发送结束消息，分拣线程收到结束消息后输出统计结果并退出。

### 4. 实际运行结果

执行：

```text
msh >package_demo
```

实际输出：

![b375f5f5-5d33-47d4-973c-6646adb2c1aa](figures/b375f5f5-5d33-47d4-973c-6646adb2c1aa.png)

```text
package demo start

package 2001 -> south, weight: 980 g
package 1001 -> east, weight: 880 g
package 2002 -> north, weight: 1060 g
package 1002 -> south, weight: 960 g
package 1003 -> north, weight: 1040 g
package 2003 -> east, weight: 1140 g
package 1004 -> east, weight: 1120 g
package 2004 -> south, weight: 1220 g
package 1005 -> south, weight: 1200 g
package 2005 -> north, weight: 1300 g

total packages: 10
east: 3, south: 4, north: 3
total weight: 10900 g
send failed: 6
package demo finished
```

### 5. 实验结果分析

两个发送线程总共产生：

```text
5 + 5 = 10
```

条快递消息。

实际分拣结果：

```text
total packages: 10
```

说明 10 条快递最终全部被分拣线程成功接收。

各区域统计为：

```text
east: 3
south: 4
north: 3
```

满足：

```text
3 + 4 + 3 = 10
```

总重量统计为：

```text
10900 g
```

与 10 条实际快递重量之和一致。

实验中还得到：

```text
send failed: 6
```

说明运行过程中消息队列曾经达到容量上限。

由于消息队列只能存储 5 条消息，而两个发送线程同时不断产生消息，分拣线程还需要一定时间处理每一条快递，因此在部分时刻出现：

```text
生产速度 > 消费速度
```

导致：

```c
rt_mq_send()
```

返回队列满错误。

程序没有直接丢弃该快递，而是在记录一次失败后重新尝试发送，因此虽然出现：

```text
send failed: 6
```

最终仍然完成：

```text
total packages: 10
```

说明队列满处理和重试逻辑正常。

### 6. 实验数据总结

| 项目            | 实际数据 |
| --------------- | -------- |
| 消息队列容量    | 5        |
| 发送线程数量    | 2        |
| Sender 1 消息数 | 5        |
| Sender 2 消息数 | 5        |
| 总快递数量      | 10       |
| East            | 3        |
| South           | 4        |
| North           | 3        |
| 总重量          | 10900 g  |
| 发送失败次数    | 6        |
| 最终丢失消息    | 0        |
| 实验结果        | 正常完成 |

### 7. 问题与解决方法

实验中出现：

```text
send failed: 6
```

说明部分消息发送时消息队列已经达到容量上限。

如果发送失败后直接进入下一条消息，会导致部分快递丢失，最终：

```text
total packages
```

可能小于 10。

因此在发送失败后采用：

```text
记录失败次数
    ↓
短暂延时
    ↓
重新发送当前消息
```

的方式处理。

最终虽然出现 6 次发送失败，但 10 条快递均成功进入消息队列并完成分拣。

同时运行过程中出现：

```text
msh >package 2001...
```

属于 MSH 命令提示符与线程打印输出交错，对实验结果无影响。

------

# 实验总结

本次实验分别使用不同 IPC 机制解决不同类型的多线程问题。

| 实验           | IPC 机制      | 主要解决的问题       | 实际结果                       |
| -------------- | ------------- | -------------------- | ------------------------------ |
| 停车场车位管理 | Semaphore     | 有限资源数量控制     | 最大同时停车 3 辆              |
| 多窗口售票     | Mutex         | 共享资源互斥访问     | 20 张票无重复售出              |
| 售票对比实验   | 无 Mutex      | 验证竞态条件         | 出现大量重复票号               |
| 系统启动检查   | Event         | 多条件同步           | 3 秒超时，5 秒后三模块全部就绪 |
| 快递分拣       | Message Queue | 线程间结构化数据通信 | 10 条全部完成，队满 6 次       |

实验结果表明，不同 IPC 机制解决的问题并不相同：

```text
Semaphore
    ↓
控制有限资源数量

Mutex
    ↓
保护共享资源

Event
    ↓
等待多个条件

Message Queue
    ↓
在线程之间传递完整消息
```

通过实际的线程竞争、超时等待和队列满实验，可以更加直观地观察 RT-Thread IPC 机制在线程同步与线程通信中的作用。