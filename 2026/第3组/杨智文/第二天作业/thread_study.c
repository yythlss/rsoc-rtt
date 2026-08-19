/*
 * Day2 作业：RT-Thread 内核基础与多线程编程
 * 开发板：STM32F407 星火号（stm32f407-rtspark）
 * 作者：杨智文
 *
 * 功能说明：
 * 1. 动态线程 th_led1 控制红灯 LED_R；
 * 2. 静态线程 th_led2 控制蓝灯 LED_B；
 * 3. 监控线程 th_monitor 每 2 秒打印线程状态和剩余栈空间；
 * 4. 按下 KEY_UP 可挂起或恢复 th_led1；
 * 5. 输入 FinSH 命令 thread_dump 可随时查看三个线程的信息。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <finsh.h>

#define LED1_PIN                    GET_PIN(F, 12) /* red LED, active low */
#define LED2_PIN                    GET_PIN(F, 11) /* blue LED, active low */
#define KEY_UP_PIN                  GET_PIN(C, 5)  /* KEY_UP, active low */

#define LED_THREAD_PRIORITY         16
#define LED_THREAD_TIMESLICE        5
#define LED_THREAD_STACK_SIZE       1024
#define MONITOR_THREAD_PRIORITY     10
#define MONITOR_THREAD_TIMESLICE    5
#define MONITOR_THREAD_STACK_SIZE   1024

/* 动态线程只保存线程句柄，TCB 和栈空间由 RT-Thread 从堆中分配。 */
static rt_thread_t led1_thread = RT_NULL;
static rt_thread_t monitor_thread = RT_NULL;

/* 静态线程的线程控制块（TCB）和栈空间均由程序预先分配。 */
static struct rt_thread led2_thread_tcb;
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t led2_thread_stack[LED_THREAD_STACK_SIZE];

static volatile rt_uint8_t key_up_event = 0;
static rt_bool_t led1_is_suspended = RT_FALSE;

/* stat 的低位保存基本线程状态，先用状态掩码去掉附加标志。 */
static const char *thread_state_name(rt_uint8_t stat)
{
    switch (stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:
        return "INIT";
    case RT_THREAD_READY:
        return "READY";
    case RT_THREAD_SUSPEND:
        return "SUSPEND";
    case RT_THREAD_RUNNING:
        return "RUNNING";
    case RT_THREAD_CLOSE:
        return "CLOSE";
    default:
        return "UNKNOWN";
    }
}

static rt_size_t thread_stack_free(rt_thread_t thread)
{
    const rt_uint8_t *stack;
    rt_size_t free_size = 0;

    if ((thread == RT_NULL) || (thread->stack_addr == RT_NULL))
    {
        return 0;
    }

    stack = (const rt_uint8_t *)thread->stack_addr;

    /*
     * RT-Thread 创建线程时会用字符 '#' 填充整个栈。
     * 从栈的空闲端连续统计仍为 '#' 的字节数，即可估算剩余栈空间。
     */
#ifdef ARCH_CPU_STACK_GROWS_UPWARD
    while ((free_size < thread->stack_size) &&
           (stack[thread->stack_size - 1U - free_size] == (rt_uint8_t)'#'))
    {
        free_size++;
    }
#else
    while ((free_size < thread->stack_size) &&
           (stack[free_size] == (rt_uint8_t)'#'))
    {
        free_size++;
    }
#endif

    return free_size;
}

static void print_thread_info(rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        return;
    }

    rt_kprintf("%-10s %-8s pri=%02u tick=%02u stack_free=%4u/%4u\n",
               thread->name,
               thread_state_name(thread->stat),
               (unsigned int)thread->current_priority,
               (unsigned int)thread->remaining_tick,
               (unsigned int)thread_stack_free(thread),
               (unsigned int)thread->stack_size);
}

static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_base_t level = PIN_HIGH;

    RT_UNUSED(parameter);

    /* 动态线程：翻转红灯，并输出运行次数与当前系统节拍。 */
    while (1)
    {
        level = (level == PIN_HIGH) ? PIN_LOW : PIN_HIGH;
        rt_pin_write(LED1_PIN, level);
        rt_kprintf("[th_led1] count=%lu tick=%lu LED_R=%s\n",
                   (unsigned long)++count,
                   (unsigned long)rt_tick_get(),
                   (level == PIN_LOW) ? "ON" : "OFF");
        rt_thread_mdelay(500);
    }
}

static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_base_t level = PIN_HIGH;

    RT_UNUSED(parameter);

    /* 静态线程：与 th_led1 同优先级、同时间片，翻转蓝灯。 */
    while (1)
    {
        level = (level == PIN_HIGH) ? PIN_LOW : PIN_HIGH;
        rt_pin_write(LED2_PIN, level);
        rt_kprintf("[th_led2] count=%lu tick=%lu LED_B=%s\n",
                   (unsigned long)++count,
                   (unsigned long)rt_tick_get(),
                   (level == PIN_LOW) ? "ON" : "OFF");
        rt_thread_mdelay(500);
    }
}

static void key_up_irq_handler(void *args)
{
    RT_UNUSED(args);
    /* 中断服务函数只置位事件，线程控制操作留给监控线程执行。 */
    key_up_event = 1;
}

static void toggle_led1_thread(void)
{
    rt_err_t result;

    if (led1_thread == RT_NULL)
    {
        return;
    }

    if (led1_is_suspended == RT_FALSE)
    {
        /*
         * 延时中的线程同样显示为 SUSPEND。先等待延时结束并回到 READY，
         * 再执行主动挂起，避免把“延时挂起”误判为“按键挂起”。
         */
        while ((led1_thread->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
        {
            rt_thread_mdelay(10);
        }

        result = rt_thread_suspend(led1_thread);
        if (result == RT_EOK)
        {
            led1_is_suspended = RT_TRUE;
            rt_kprintf("[monitor] KEY_UP: th_led1 suspended\n");
            rt_schedule();
        }
        else
        {
            rt_kprintf("[monitor] suspend th_led1 failed: %d\n", result);
        }
    }
    else
    {
        result = rt_thread_resume(led1_thread);
        if (result == RT_EOK)
        {
            led1_is_suspended = RT_FALSE;
            rt_kprintf("[monitor] KEY_UP: th_led1 resumed\n");
            rt_schedule();
        }
        else
        {
            rt_kprintf("[monitor] resume th_led1 failed: %d\n", result);
        }
    }
}

static void monitor_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        /* 周期延时会主动让出 CPU，2 秒后由调度器重新唤醒本线程。 */
        rt_thread_mdelay(2000);

        if (key_up_event != 0)
        {
            key_up_event = 0;
            toggle_led1_thread();
        }

        rt_kprintf("\n[monitor] thread state and remaining stack\n");
        print_thread_info(led1_thread);
        print_thread_info(&led2_thread_tcb);
    }
}

static int thread_dump(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("name       state    priority/tick/remaining stack\n");
    print_thread_info(led1_thread);
    print_thread_info(&led2_thread_tcb);
    print_thread_info(monitor_thread);

    return 0;
}
/* 将 thread_dump 导出为 FinSH/MSH 命令，无需在 main() 中注册。 */
MSH_CMD_EXPORT(thread_dump, show Day2 thread state and remaining stack);

static int thread_study_init(void)
{
    rt_err_t result;

    rt_pin_mode(LED1_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED2_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED1_PIN, PIN_HIGH);
    rt_pin_write(LED2_PIN, PIN_HIGH);

    rt_pin_mode(KEY_UP_PIN, PIN_MODE_INPUT_PULLUP);
    result = rt_pin_attach_irq(KEY_UP_PIN, PIN_IRQ_MODE_FALLING,
                               key_up_irq_handler, RT_NULL);
    if (result != RT_EOK)
    {
        rt_kprintf("[thread_study] attach KEY_UP irq failed: %d\n", result);
        return result;
    }
    rt_pin_irq_enable(KEY_UP_PIN, PIN_IRQ_ENABLE);

    /* 动态创建：TCB 和 1024 字节栈均从系统堆申请。 */
    led1_thread = rt_thread_create("th_led1", led1_entry, RT_NULL,
                                   LED_THREAD_STACK_SIZE,
                                   LED_THREAD_PRIORITY,
                                   LED_THREAD_TIMESLICE);
    if (led1_thread == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    /* 静态创建：使用上方定义的全局 TCB 和全局栈数组。 */
    result = rt_thread_init(&led2_thread_tcb, "th_led2", led2_entry, RT_NULL,
                            led2_thread_stack, sizeof(led2_thread_stack),
                            LED_THREAD_PRIORITY, LED_THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_thread_delete(led1_thread);
        led1_thread = RT_NULL;
        return result;
    }

    monitor_thread = rt_thread_create("th_monitor", monitor_entry, RT_NULL,
                                      MONITOR_THREAD_STACK_SIZE,
                                      MONITOR_THREAD_PRIORITY,
                                      MONITOR_THREAD_TIMESLICE);
    if (monitor_thread == RT_NULL)
    {
        rt_thread_detach(&led2_thread_tcb);
        rt_thread_delete(led1_thread);
        led1_thread = RT_NULL;
        return -RT_ENOMEM;
    }

    /* 创建/初始化成功后，依次加入就绪队列等待调度。 */
    rt_thread_startup(led1_thread);
    rt_thread_startup(&led2_thread_tcb);
    rt_thread_startup(monitor_thread);

    rt_kprintf("[thread_study] Day2 threads started; press KEY_UP to pause/resume th_led1.\n");
    return RT_EOK;
}
/* 应用初始化阶段自动执行，main() 中不需要硬编码调用。 */
INIT_APP_EXPORT(thread_study_init);
