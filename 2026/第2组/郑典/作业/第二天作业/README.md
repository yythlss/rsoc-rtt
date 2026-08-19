# 【DAY2】RT-Thread 启动流程、内核基础与多线程编程

## Part 1：理论知识梳理文档

### 1. STM32F407 启动流程与 RT-Thread 引导机制

- **启动路径**：详细画出/写出从上电复位 `Reset_Handler` 到进入 `main()` 的完整调用链。

    ![RT-Thread启动流程](.\figures\rtt_startup.png)

	上图完整地展示了RT-Thread的整个启动流程。以STM32F407系列芯片为例，整个过程大致可以分为以下六个部分：
    
    1.   RTT转入
         ```
         starup_stm32f407xx.s (Reset_Handler)
         |
	     $Sub$$main() / _low_level_init() / entry()
         |
         rethread_startup()
         ```
    
    2.   板级初始化
	     ```
         rt_hw_interrupt_disable()
         |
         rt_hw_board_init()
         |
         rt_components_board_init()
	     |
         board init functions
         ```
    
    3.   RTT内核初始化
         ```
	     rt_show_version()
         |
         rt_system_timer_init()
         |
         rt_system_scheduler_init()
         |
	     rt_system_signal_init()
         ```
    
    4.   系统线程初始化
         ```
         rt_application_init()
         |
         rt_system_timer_thread_init()
         |
         rt_thread_idle_init()
         ```
    
    5.   调度器启动
    
         ```
         rt_system_scheduler_startup()
         |
         main_thread_entry() / rt_thread_timer_entry() / rt_thread_idle_entry()
         ```
    
    6.   mian线程运行
         ```
         rt_components_init()
         |
         main()
         ```
    
- **自动初始化原理**：解释 `INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT` 的底层实现原理。

    自动初始化的底层代码如下所示：

    ```c
    /* 初始化函数指针类型 */
    typedef int (*init_fn_t)(void);
    
    /* 底层通用宏 */
    #define INIT_EXPORT(fn, level)                                  \
        RT_USED const init_fn_t __rt_init_##fn                      \
        RT_SECTION(".rti_fn." level) = fn
    
    /* 六个等级的对外宏 */
    #define INIT_BOARD_EXPORT(fn)       INIT_EXPORT(fn, "1")
    #define INIT_PREV_EXPORT(fn)        INIT_EXPORT(fn, "2")
    #define INIT_DEVICE_EXPORT(fn)      INIT_EXPORT(fn, "3")
    #define INIT_COMPONENT_EXPORT(fn)   INIT_EXPORT(fn, "4")
    #define INIT_ENV_EXPORT(fn)         INIT_EXPORT(fn, "5")
    #define INIT_APP_EXPORT(fn)         INIT_EXPORT(fn, "6")
    
    /* 初始化函数指针类型 */
    typedef int (*init_fn_t)(void);
    
    /* 底层通用宏 */
    #define INIT_EXPORT(fn, level)                                  \
        RT_USED const init_fn_t __rt_init_##fn                      \
        RT_SECTION(".rti_fn." level) = fn
    
    /* 六个等级的对外宏 */
    #define INIT_BOARD_EXPORT(fn)       INIT_EXPORT(fn, "1")
    #define INIT_PREV_EXPORT(fn)        INIT_EXPORT(fn, "2")
    #define INIT_DEVICE_EXPORT(fn)      INIT_EXPORT(fn, "3")
    #define INIT_COMPONENT_EXPORT(fn)   INIT_EXPORT(fn, "4")
    #define INIT_ENV_EXPORT(fn)         INIT_EXPORT(fn, "5")
    #define INIT_APP_EXPORT(fn)         INIT_EXPORT(fn, "6")
    ```

    对于一个初始化函数`x_init()`，`INIT_BOARD_EXPORT(x_init)`的实际执行逻辑为：

    >   ​	定义一个名叫 `__rt_init_led_board_init` 的**常量函数指针变量**，它的值是 `led_board_init` 这个函数的地址，并且强制把这个变量放到名为 `.rti_fn.1` （`INIT_APP_EXPORT`则对应`.rti_fn.6`）的段里，同时告诉编译器"就算没被调用也别删掉该函数"。

### 2. 内核基础与对象模型

- **对象容器与控制块**：简述 RT-Thread 面向对象的设计思想，线程控制块（`struct rt_thread`）中关键成员（如 `stat`、`current_priority`、`init_tick`、`sp`）的作用。

    RT-Thread 使用纯 C 语言实现了的面向对象设计，其核心思想是：**通过结构体嵌套模拟类的继承关系，并统一管理所有内核对象**。

    所有内核对象（线程、信号量、互斥量、消息队列等）都以 `struct rt_object` 作为第一个成员：

    ```c
    struct rt_object
    {
        char name[RT_NAME_MAX];     /* 对象名称 */
        rt_uint8_t type;            /* 对象类型 */
        rt_uint8_t flag;            /* 对象标志 */
        rt_list_t list;             /* 对象链表节点 */
    };
    ```

    通过把 `parent` 放在结构体首位，任何指向`rt_thread`的指针都可以被安全地强制转换为`struct rt_object *`，从而实现：**统一命名与类型识别**、**统一容器管理**、**类似多态的行为**。

    其中，部分关键成员的作用如下表所示：

    | 成员                         | 作用                             |
    | ---------------------------- | -------------------------------- |
    | `sp`                         | 保存线程栈指针，用于上下文切换   |
    | `stat`                       | 记录线程状态，决定线程是否可调度 |
    | `current_priority`           | 线程当前优先级，决定调度顺序     |
    | `init_tick`/`remaining_tick` | 实现时间片轮转调度               |
    | `thread_timer`               | 实现线程延时和超时机制           |
    | `cleanup`                    | 线程退出时执行清理工作           |
    | `user_data`                  | 提供线程私有数据存储             |

- **内存分配方式**：对比静态对象与动态对象的区别（分配位置、生命周期、释放机制），分析为什么某些高可靠场景强制使用静态线程。

    | 分配方式     | 静态 | 动态 |
    | :-: | :-: | :-: |
    | **分配位置** | 放在RW段和ZI段中，在程序中初始化 | 从内存堆中创建，在程序中手动初始化 |
    | **生命周期** | 在整个程序运行期间始终存在 | 由程序运行逻辑控制，按需创建、销毁 |
    | **释放机制** | 长期占用RAM空间，但可以被复用 | 当对象被删除后，占用的RAM空间被释放 |

### 3. 线程状态机与调度算法

- **五态模型**：绘制或描述 RT-Thread 线程状态（初始态、就绪态、运行态、挂起态、关闭态）及触发状态转换的典型 API。

     1.    **初始态`RT_THREAD_INIT`**：线程刚开始创建还没开始运行时的状态；在该状态下，线程不参与调度。

     2.    **就绪态`RT_THREAD_READY`**：线程按照优先级排队，等待被执行的状态；一旦当前线程运行完毕让出处理器，操作系统会马上寻找最高优先级的就绪态线程运行。

     3.    **运行态**`RT_THREAD_RUNNING`：线程当前正在运行的状态。

     4.    **挂起态（阻塞态）`RT_THREAD_SUSPEND`**：线程因为资源不可用或主动延时一段时间而挂起等待的状态；在该状态下，线程不参与调度。

     5.    **关闭态**`RT_THREAD_CLOSE`：线程运行结束时的状态。该状态下的线程不参与调度。

	**五态转化**：
	 ![RT-Thread状态切换函数](.\figures\thread_sta.png)

- **调度机制剖析**：
  
    - **抢占式调度**
    
        抢占式调度解决的是**不同优先级线程之间**的CPU分配问题。它的核心原则是：**系统始终保证当前处于就绪态中优先级最高的线程占用CPU执行**——当一个高优先级的线程因为某种条件（如延时结束、等待的信号量被释放、接收到消息）从挂起态转变为就绪态时，如果当前正在运行的线程优先级比它低，调度器会立刻打断当前线程的执行，将CPU移交给这个高优先级线程。这个过程就叫做“抢占”。
    
    - **时间片轮转**
    
        时间片轮转解决的是**相同优先级线程之间**的CPU公平分配问题。如果系统中存在多个优先级完全相同的就绪线程，如果没有时间片机制，最先运行的线程如果不主动阻塞，将会一直霸占 CPU（即“饿死”其他同优先级线程）。系统依靠硬件定时器（SysTick）产生周期性的Tick中断（例如1ms一次）。在每次SysTick中断处理函数中，当前运行线程的`remaining_tick`会减1。
        当`remaining_tick`减到0时，系统会执行以下动作：
    
        1.  将该线程的`remaining_tick`重新恢复为`init_tick`；
        2.  将该线程从其所在优先级就绪队列的**头部移到尾部**；
        3.  触发调度器，就绪队列中排在它后面的下一个同优先级线程获得CPU开始运行。

## Part 2：多线程实战编程（C 语言实现）

### 任务完成情况

1. **工作线程 1（动态创建 `th_led1`）**：
    -   [x] 优先级 `16`，时间片 `5 ticks`，栈 `1024 Bytes`。
    -   [x] 循环翻转板载 LED_R，每次执行打印计数与 `rt_tick_get()`。
2. **工作线程 2（静态初始化 `th_led2`）**：
    -   [x] 优先级 `16`，时间片 `5 ticks`，静态定义全局栈与 TCB。
    -   [x] 循环翻转板载 LED_B，每次执行打印计数。
3. **监控与控制线程（动态创建 `th_monitor`）**：
    -   [x] 优先级 `10`（高于工作线程），周期延时 `rt_thread_mdelay(2000)`。
    -   [x] 唤醒后获取并打印 `th_led1` 与 `th_led2` 的运行状态与剩余栈空间大小。
    -   [x] 结合板载按键检测（或按键中断）：单次触发时调用 `rt_thread_suspend()` 挂起 `th_led1`；再次触发时调用 `rt_thread_resume()` 恢复。
4. **系统集成与调试命令**：
    -   [x] 使用 `INIT_APP_EXPORT()` 自动加载测试模块，禁止在 `main.c` 中硬编码调用。
    -   [x] 使用 `MSH_CMD_EXPORT()` 导出自定义查看命令（如 `thread_dump`）。

### 过程记录

1.   **工作线程1、2交替运行，定时监控（按键第偶数次按下）**

     ![](E:\RT-Thread\figures\led1_running.png)

2.   **工作线程2单独运行，定时监控（按键第奇数次按下）**

     ![](E:\RT-Thread\figures\led1_suspend.png)

3.   **自定义查看命令**

     ![](E:\RT-Thread\figures\check.png)