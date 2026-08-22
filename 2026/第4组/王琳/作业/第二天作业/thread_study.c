/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-19     d0ubleU0x00       the first version
 */

/*
 * 程序清单：多线程管理程序，创建动态、静态线程、监控、按键挂起和恢复
 * 命令调用格式：导出thread_dump到终端
 * 程序功能：创建两个LED工作线程和一个监控线程，支持按键挂起/恢复工作线程1
*/


#include <rtthread.h>
#include <rtdevice.h>
#include <finsh.h>
#include <board.h>
#include <rtdbg.h>



#define LED_B_PIN       GET_PIN(F,11)
#define LED_R_PIN       GET_PIN(F,12)
#define KEY_PIN       GET_PIN(C,0)


#define LED_ON_LEVEL    PIN_LOW
#define LED_OFF_LEVEL   PIN_HIGH

/*
 * 按键极性：
 * 当前假设 PC5 使用内部上拉，按键按下接地，即低电平有效。
 *
 * 如果按键是高电平有效，改：
 * KEY_PIN_MODE       -> PIN_MODE_INPUT_PULLDOWN
 * KEY_IRQ_MODE       -> PIN_IRQ_MODE_RISING
 * KEY_PRESSED_LEVEL  -> PIN_HIGH
 */
#define KEY_PIN_MODE        PIN_MODE_INPUT_PULLUP
#define KEY_IRQ_MODE        PIN_IRQ_MODE_FALLING
#define KEY_PRESSED_LEVEL   PIN_LOW


/*  线程优先级    */
#define LED_THREAD_PRIORITY         16
#define MONITOR_THREAD_PRIORITY         16

/* ===================== 线程对象 ===================== */

/* 指向led线程的状态信号量的指针 */
static rt_sem_t led_sem = RT_NULL;

/* 工作线程 1：动态创建 */
static rt_thread_t th_led1 = RT_NULL;

/* 工作线程 2：静态初始化 */
/*　使用静态线程时，线程的栈需要设置字节对齐　*/
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t th_led2_stack[1024];
static struct rt_thread th_led2;

/* 监控线程：动态创建 */
static rt_thread_t th_monitor = RT_NULL;


/* 按键中断与线程通信的全局变量 */
static volatile rt_uint32_t key_event_count = 0; /* 按键按下次数计数器 */
static volatile rt_tick_t   last_isr_tick   = 0; /* 上次中断触发的系统时间 */




/* ===================== 工具函数 ===================== */

static const char *thread_state_str(rt_uint8_t stat)
{
    stat &= RT_THREAD_STAT_MASK;

    switch (stat)
    {
    case RT_THREAD_INIT:
        return "init";

    case RT_THREAD_READY:
        return "ready";

    case RT_THREAD_RUNNING:
        return "running";

    case RT_THREAD_SUSPEND:
        return "suspend";

    case RT_THREAD_CLOSE:
        return "close";

    default:
        return "unknown";
    }
}

//当前-sp
static rt_uint32_t thread_stack_free(struct rt_thread *thread)
{
    if (thread == RT_NULL || thread->stack_addr == RT_NULL)
    {
        return 0;
    }

    /* 使用当前栈指针 sp 计算剩余空间：sp 高于 stack_addr 的部分即为未使用区域 */
    if ((rt_uint32_t)thread->sp > (rt_uint32_t)thread->stack_addr)
    {
        return (rt_uint32_t)thread->sp - (rt_uint32_t)thread->stack_addr;
    }

    return 0;
}

static void print_thread_info(const char *tag, struct rt_thread *thread)
{
    if (thread == RT_NULL)
    {
        rt_kprintf("%-8s: thread not created\n", tag);
        return;
    }

    rt_kprintf("%-8s name=%.*s state=%-7s prio=%d tick=%u stack_free=%u/%u\n",
               tag,
               RT_NAME_MAX,
               thread->name,
               thread_state_str(thread->stat),
               thread->current_priority,
               (unsigned int)thread->init_tick,
               (unsigned int)thread_stack_free(thread),
               (unsigned int)thread->stack_size);
}

static void print_all_status(void)
{
    rt_kprintf("[thread status]\n");
    print_thread_info("led1", th_led1);
    print_thread_info("led2", &th_led2);
    print_thread_info("monitor",  th_monitor);
}


/* ===================== 按键中断服务函数 (ISR) ===================== */
static void key_isr_handler(void *args)
{
    rt_tick_t current_tick = rt_tick_get();

    /* 硬件级/ISR级软件消抖：
     * 如果距离上次触发时间不到 20ms，认为是机械抖动，直接丢弃。
     */
    if ((current_tick - last_isr_tick) >= rt_tick_from_millisecond(20))
    {
        last_isr_tick = current_tick;
        key_event_count++; /* 有效按键次数 +1 */
    }
}
/*
 * 就绪态延时。
 *
 * 与 rt_thread_mdelay() 不同：
 * rt_thread_mdelay() 会让线程进入 SUSPEND 状态，
 * 而这里通过不断判断 tick 并调用 rt_thread_yield()，
 * 使线程仍然处于 READY/RUNNING 状态。
 *
 * 这样监控线程调用 rt_thread_suspend(th_led1) 更容易成功。
 */
static void ready_delay_ms(rt_uint32_t ms)
{
    rt_tick_t start = rt_tick_get();
    rt_tick_t ticks = rt_tick_from_millisecond(ms);

    while ((rt_tick_get() - start) < ticks)
    {
        rt_thread_yield();
    }
}

/*      线程th_led1的入口函数                           */
static  void th_led1_entry(void *parameter)
{

    static rt_uint8_t count = 0;
    while(1)
    {
        if (rt_sem_take(led_sem, 0) == RT_EOK)
        {
            rt_kprintf("th_led1 suspend.\n");
            rt_thread_suspend(th_led1);    //挂起th_led1
        }
        else
        {
            count++;
            rt_pin_write(LED_R_PIN, !rt_pin_read(LED_R_PIN));
            rt_kprintf("th_led1 count=%u tick=%u\n",
                               (unsigned int)count,
                               (unsigned int)rt_tick_get());
            ready_delay_ms(1000);
        }
    }
}

/*      线程th_led2的入口函数                           */
static  void th_led2_entry(void *parameter)
{

    static rt_uint8_t count = 0;
    while(1)
    {
            count++;
            rt_pin_write(LED_B_PIN, !rt_pin_read(LED_B_PIN));
            rt_kprintf("th_led2 count=%u\n",(unsigned int)count);
            ready_delay_ms(1000);
    }
}

/*      线程th_monitor的入口函数                           */
static  void th_monitor_entry(void *parameter)
{
    rt_uint32_t key_cnt;
    rt_base_t level;
    while(1)
    {
        /* 关中断，安全地读取并清零计数器 (原子操作)    */
        level = rt_hw_interrupt_disable();
        key_cnt = key_event_count;
        key_event_count = 0; /* 读完就清零，防止重复处理 */
        rt_hw_interrupt_enable(level);

        /* 如果计数器大于 0，说明睡觉期间有人按了按键   */

        if (key_cnt > 0)
        {
            rt_thread_mdelay(20);
            if(rt_pin_read(KEY_PIN)==0)           //按键消抖20ms
            {

                //每次按下按键翻转一下线程th_led1的状态
                if((th_led1->stat &= RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
                {
                    rt_kprintf("th_led1 resume.\n");
                    rt_thread_resume(th_led1);  //使线程恢复运行
                }
                else
                {
                    rt_sem_release(led_sem);    //释放信号量
                }
                rt_kprintf("[KEY] key event \n");
            }

        }

        // 打印状态
        print_all_status();
        rt_thread_mdelay(2000);

    }
}


static void gpio_init(void)
{

    /* 初始化 LED 引脚 */
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);

    rt_pin_write(LED_R_PIN, LED_ON_LEVEL);
    rt_pin_write(LED_B_PIN, LED_ON_LEVEL);

    /* 初始化按键中断 */
    rt_pin_mode(KEY_PIN, KEY_PIN_MODE);
    rt_pin_attach_irq(KEY_PIN, KEY_IRQ_MODE, key_isr_handler, RT_NULL);
    rt_pin_irq_enable(KEY_PIN, PIN_IRQ_ENABLE);

}


/* ===================== 模块初始化 ===================== */

static int thread_study_init(void)
{
    /* 初始化GPIO*/
    gpio_init();
    /* 创建一个动态信号量，初始值是0 */
    led_sem = rt_sem_create("led_sem", 0, RT_IPC_FLAG_PRIO);
    /*
     * 工作线程 1：动态创建
     * 名称：th_led1
     * 优先级：16
     * 时间片：5 ticks
     * 栈大小：1024 Bytes
     */
    th_led1 = rt_thread_create("th_led1",
                                   th_led1_entry,
                                   RT_NULL,
                                   2048,
                                   16,
                                   5);
    rt_thread_startup(th_led1);//动态线程不能用&符号
    /*
     * 工作线程 2：静态初始化
     * 名称：th_led2
     * 优先级：16
     * 时间片：5 ticks
     * 栈大小：1024 Bytes
     */
    rt_thread_init(&th_led2,
                   "th_led2",
                   th_led2_entry,
                   RT_NULL,
                   th_led2_stack,
                   sizeof(th_led2_stack),
                   16,
                   5);

    rt_thread_startup(&th_led2);

    /*      监控线程：动态创建               */
    th_monitor     = rt_thread_create("th_mon",
                                      th_monitor_entry,
                                      RT_NULL,
                                      2048,
                                      10,
                                      5);
    rt_thread_startup(th_monitor);
    rt_kprintf("thread_study init done\n");
    rt_kprintf("LED_R=PF12, LED_B=PF11, KEY=PC5\n");
    rt_kprintf("Press KEY to suspend/resume th_led1\n");

    return 0;
}
INIT_APP_EXPORT(thread_study_init);

/* ===================== MSH 调试命令 ===================== */

void thread_dump(void)
{
    print_all_status();
}
MSH_CMD_EXPORT(thread_dump, dump thread study status);


