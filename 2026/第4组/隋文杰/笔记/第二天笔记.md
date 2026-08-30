# Part 1:

## 1. STM32F407 启动流程与 RT-Thread 引导机制

- **启动路径**

  硬件自动行为：上电/复位 → 从向量表取 MSP 初始值 → 跳转到 Reset_Handler

  startup_xxx.S  (汇编启动文件)：Reset_Handler → 设置 MSP 主栈指针→ 拷贝 .data 段 (Flash → RAM)   → 清零 .bss 段    → bl SystemInit  → bl entry
  
  components.c：int entry(void) { rtthread_startup(); return 0;}
  
  然后到了rtt的启动流程
  
  ![](.\day2图1.png)



- **自动初始化原理**

上电
 │
 ├─ Reset_Handler
 │   └─ entry()
 │       └─ rtthread_startup()
 │           ├─ rt_hw_board_init()
 │           │   └─ rt_components_board_init()
 │           │       **└─ INIT_BOARD_EXPORT **
 │           ├─ rt_system_scheduler_start()
 │           │   └─ 启动调度器
 │           └─ main 线程
 │               └─ main_thread_entry()
 │                   └─ rt_components_init()
 │                       **└─ INIT_APP_EXPORT **
 │                   └─ main()

## 2.内核基础与对象模型

- **对象容器与控制块**：RT-Thread 用 C 语言通过“结构体嵌套基类 + 统一 API + 不暴露内部实现”的方式，实现了面向对象的内核设计，使内核对象具备继承、封装和统一管理的特性。

rt_uint8_t stat;

| 宏定义            | 含义   |
| ----------------- | ------ |
| RT_THREAD_INIT    | 初始态 |
| RT_THREAD_READY   | 就绪态 |
| RT_THREAD_SUSPEND | 挂起态 |
| RT_THREAD_RUNNING | 运行态 |
| RT_THREAD_CLOSE   | 关闭态 |

current_priority — 当前优先级

init_tick — 时间片初始值

sp — 线程栈指针

- **内存分配方式**： **静态对象** 在编译期分配于 `.data/.bss`，生命周期贯穿系统运行，无分配失败和碎片风险；**动态对象**在运行期从堆中分配，灵活但不可靠。高可靠系统强制使用静态线程，是为了保证确定性、可验证性、避免内存碎片和运行时失败。

## 3. 线程状态机与调度算法

* **五态模型**：

![](.\day2图2.png)

* **调度机制剖析** ：**抢占式调度**保证高优先级线程可随时打断低优先级线程，确保实时响应；**时间片轮转**则在同一优先级的多线程间，按固定时间片轮流分配 CPU，防止单一线程独占。

# Part2:

线程th_led2 黄色LED等按照500ms间隔闪烁

因为在其他线程里面挂起线程会assert 报错，所以使用信号量，在线程th_monitor中扫描按键DOWN，会释放信号量，运行th_led1 红色LED灯闪烁，并且打印 `th_led1` 与 `th_led2` 的运行状态与剩余栈空间大小。运行状态和终端如下图所示

![](.\day2图3.jpg)

![](.\day2图4.jpg)

![](.\day2图5.png)
