# RT-Thread 启动流程

> 对应《RT-Thread 星火1号内核学习手册》第 2.2 节。
系统上电后，先运行汇编启动代码完成堆栈初始化，随后根据编译器跳转到 RT-Thread 启动入口 `rtthread_startup()`：

- GCC（`arm-none-eabi-gcc`）：入口函数为 `entry()`
- ARMCC / CLANG：通过 `$Sub$$main()` 拦截 `main()` 实现
- IAR：通过 `__low_level_init()` 实现

```c
#if defined(__GNUC__)
int entry(void)
{
    rtthread_startup();
    return 0;
}
#endif
```

## 2.1 启动入口 rtthread_startup()

```c
int rtthread_startup(void)
{
    rt_hw_interrupt_disable();          /* 关全局中断 */

    /* 板级初始化：系统时钟、串口、堆等 */
    rt_hw_board_init();

    /* 显示 RT-Thread 版本号 */
    rt_show_version();

    /* 定时器系统初始化 */
    rt_system_timer_init();

    /* 调度器初始化 */
    rt_system_scheduler_init();

#ifdef RT_USING_SIGNALS
    /* 信号初始化 */
    rt_system_signal_init();
#endif

    /* main 线程初始化（即创建 main 线程） */
    rt_application_init();

    /* timer 定时器线程初始化 */
    rt_system_timer_thread_init();

    /* idle 空闲线程初始化 */
    rt_thread_idle_init();

#ifdef RT_USING_SMP
    rt_hw_spin_lock(&_cpus_lock);
#endif

    /* 启动调度器 */
    rt_system_scheduler_start();

    /* never reach here */
    return 0;
}
```

### 启动流程分四个部分

| 部分 | 对应函数 | 说明 |
| ---- | -------- | ---- |
| 1. 硬件初始化 | `rt_hw_board_init()` | 初始化与系统相关的硬件 |
| 2. 内核对象初始化 | `rt_system_timer_init()` `rt_system_scheduler_init()` `rt_system_signal_init()` | 初始化定时器、调度器、信号等系统内核对象 |
| 3. 创建 main 线程 | `rt_application_init()` | 在 main 线程中对各类模块依次进行初始化 |
| 4. 收尾并启动调度 | `rt_system_timer_thread_init()` `rt_thread_idle_init()` `rt_system_scheduler_start()` | 初始化定时器线程、空闲线程，并启动调度器 |

### 两个要点

1. **启动调度器之前线程不会运行**：系统创建的线程在执行 `rt_thread_startup()` 后并不会立马运行，而是处于**就绪状态**等待调度；待 `rt_system_scheduler_start()` 启动调度器之后，系统才转入第一个线程运行，按调度规则选择就绪队列中**优先级最高**的线程。
2. **`rt_hw_board_init()` 的职责**：完成系统时钟设置（为系统提供心跳 `tick`）、串口初始化（将系统输入输出终端绑定到该串口），后续系统运行信息就会从串口打印出来。堆的初始化也在板级初始化中完成。

## 2.2 创建 main 线程 rt_application_init()

`rtthread_startup()` 中通过 `rt_application_init()` 创建 main 线程：

```c
void rt_application_init(void)
{
    rt_thread_t tid;

#ifdef RT_USING_HEAP
    tid = rt_thread_create("main", main_thread_entry, RT_NULL,
                           RT_MAIN_THREAD_STACK_SIZE, RT_MAIN_THREAD_PRIORITY, 20);
    RT_ASSERT(tid != RT_NULL);
#else
    /* 未启用堆时，使用静态线程和栈 */
    ...
#endif

    rt_thread_startup(tid);
}
```


main 线程的入口函数 `main_thread_entry()` 中完成**组件初始化**，然后调用用户 `main()` 函数：

```c
void main_thread_entry(void *parameter)
{
    extern int main(void);

#ifdef RT_USING_COMPONENTS_INIT
    /* RT-Thread 组件初始化 */
    rt_components_init();
#endif

    /* 调用系统 main 函数 */
    main();
}
```

也就是说，用户代码 `main()` 是在一个**线程**里运行的，此时内核已经完全就绪，可以使用线程、信号量、消息队列等所有内核机制。

