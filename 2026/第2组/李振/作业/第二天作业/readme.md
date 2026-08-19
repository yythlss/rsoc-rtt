## Part 1：理论知识梳理文档

### 1. STM32F407 启动流程与 RT‑Thread 引导机制

#### 1.1 上电复位 Reset_Handler → main() 完整调用链

```
上电复位
↓
Reset_Handler(启动文件 startup_stm32f4xx.s 汇编)
 1. 设置栈指针 SP
 2. BSS段清零、Data段从Flash拷贝到RAM
 3. 跳转到编译器入口
 MDK‑Keil：$Sub$$main()
 GCC：entry()
↓
rtthread_startup() // RT‑Thread系统总入口函数
 ├─ rt_hw_interrupt_disable() //关闭全局中断
 ├─ rt_hw_board_init() //BSP板级硬件初始化
 │ └─ rt_components_board_init() //执行INIT_BOARD_EXPORT导出函数
 ├─ rt_show_version() //打印RT‑Thread版本LOGO
 ├─ rt_system_timer_init() //初始化内核定时器模块
 ├─ rt_system_scheduler_init() //初始化调度器
 ├─ rt_system_signal_init()
 ├─ rt_application_init() //创建main主线程
 ├─ rt_system_timer_thread_init() //创建软件定时器线程
 ├─ rt_thread_idle_init() //创建空闲线程
 └─ rt_system_scheduler_start() //启动调度器，多线程正式运行
↓
调度器选中 main_thread_entry 主线程运行
 └─ rt_components_init() //执行INIT_APP_EXPORT导出函数
 └─ $Super$$main()
 └─ main() //到达用户main函数
```

#### 1.2 自动初始化原理 `INIT_BOARD_EXPORT` / `INIT_APP_EXPORT`

RT‑Thread 使用**段地址、编译器宏**实现自动初始化，不需要手动调用函数。
底层原理：

1. 宏定义会把我们的初始化函数，加上指定的段属性，放到内存中一段特殊的自定义段
- `INIT_BOARD_EXPORT(fn)`：放到 **board 初始化段**
- `INIT_APP_EXPORT(fn)`：放到 **application 初始化段**
2. 链接脚本把同类型初始化函数，在内存里排成一张函数指针数组
3. 在系统启动的时候，内核自动遍历整张函数表，依次执行里面的函数
   执行时机：
- `INIT_BOARD_EXPORT`：`rt_hw_board_init()` →`rt_components_board_init()`阶段，硬件板级初始化，**调度器开启之前执行**
- `INIT_APP_EXPORT`：主线程里面 `rt_components_init()`，调度已经启动，可以使用内核API
  执行顺序优先级：`BOARD` 优先于 `APP`。

---

### 2. 内核基础与对象模型

#### 2.1 RT‑Thread面向对象设计思想 & 线程控制块关键成员

##### 面向对象设计思想

C语言本身没有类，RT‑Thread采用**结构体嵌套模拟继承**。
所有内核资源（线程、信号量、定时器、设备、内存池）都派生自基类`rt_object`。
`rt_object`提供通用属性：名字name、类型type、链表节点list；子类在它基础上扩展自己独有的成员。

##### struct rt_thread 线程控制块关键成员

| 成员               | 作用                                                              |
| ------------------ | ----------------------------------------------------------------- |
| `stat`             | 线程状态标记：初始、就绪、运行、挂起、关闭                        |
| `current_priority` | 线程当前实际优先级，优先级数值越小优先级越高                      |
| `init_tick`        | 线程时间片，同一个优先级下线程可分得的运行时长                    |
| `sp`               | 栈指针，保存线程被切走那一刻CPU寄存器现场，线程切换用来恢复上下文 |

#### 2.2 静态对象 VS 动态对象

##### 静态对象

- 分配位置：编译期分配在全局RAM(.data/.bss)
- 生命周期：整个程序运行全程有效，上电即存在
- 释放：**不能使用 rt_thread_delete**，只能手动脱离内核管理

##### 动态对象

- 分配位置：运行的时候从RT‑Thread堆(heap)里面`rt_malloc`申请内存
- 生命周期：创建时分配，调用`rt_thread_delete()`释放归还堆内存
- 释放机制：内核自动回收内存

##### 高可靠场景强制用静态线程原因

堆内存动态分配`malloc`存在内存碎片、分配失败风险；

堆内存动态分配`malloc`存在内存碎片、分配失败风险；
工业、高可靠项目不能允许运行时线程创建失败，**静态内存编译期就已经分配完成，运行不会申请失败**，稳定性更高。

### 3. 线程状态机与调度算法

#### 3.1 RT‑Thread线程五态模型

1. **初始态 RT_THREAD_INIT**
    刚调用`rt_thread_init` / `rt_thread_create`创建完成，还没启动。
    转换API：`rt_thread_startup()` → **就绪态**
2. **就绪态 RT_THREAD_READY**
    线程一切准备完毕，只等待CPU。放在就绪链表，等待调度。
- 高优先级线程抢占 → 当前正在运行线程→就绪态
- 调用`rt_thread_suspend()` → 挂起态
3. **运行态 RT_THREAD_RUNNING**
    当前这个线程正在CPU上面执行，同一时刻只能有1个线程处于运行态
- `rt_thread_delay()` / 获取不到信号量 → 挂起态
- 时间片耗尽 → 就绪态
4. **挂起态 RT_THREAD_SUSPEND**
    线程被阻塞，暂时不能运行（延时、等待信号量、被手动挂起）
- `rt_thread_resume()` / 延时时间到 / 获取到资源 →就绪态
5. **关闭态 RT_THREAD_CLOSE**
    线程执行完毕或者被删除，不再参与调度。

> 状态转换简图

```
初始态 →(startup)→ 就绪态 ←(resume)← 挂起态
 ↓(调度选中)
 运行态
 运行态→(delay/suspend)→挂起态
 任意状态 →(rt_thread_delete)→关闭态
```

#### 3.2 调度机制剖析

##### 抢占式调度

- **高优先级线程一旦就绪，可以立刻打断正在运行的低优先级线程**，CPU马上切换给高优先级任务。
- 保障紧急、高实时任务能够立刻得到响应。

##### 时间片轮转调度

- 同一个优先级的就绪线程，按照各自分配的init_tick时间片轮流使用CPU。一个线程时间片用完，让出CPU，同优先级下一个线程开始运行。

# Part 2：多线程实战编程

### 上电正常运行、没有按下按键时串口输出

![屏幕截图 2026-08-19 173545.png](.\屏幕截图%202026-08-19%20173545.png)

### 第一次按下按键（挂起th_led1，红灯停）

![屏幕截图 2026-08-19 173607.png](.\屏幕截图%202026-08-19%20173607.png)





### 输入 shell 命令 `thread_dump` 的打印样例

![屏幕截图 2026-08-19 173952.png](.\屏幕截图%202026-08-19%20173952.png)




