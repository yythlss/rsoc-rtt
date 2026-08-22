/*
 * @file    thread_study.c
 * @author  Kyle He
 * @date    2026-08-19
 * @brief   RT-Thread Day2 multithread study
 */

#include <rtthread.h>
#include <rtdevice.h>
#include "drv_common.h"

/* ==================== 硬件引脚定义 ==================== */

/* 星火 1 号红色 LED：PF12，低电平点亮 */
#define PIN_LED_R    GET_PIN(F, 12)

/* 星火 1 号蓝色 LED：PF11，低电平点亮 */
#define PIN_LED_B    GET_PIN(F, 11)

/* 星火 1 号按键 KEY0：PC0，按下为低电平 */
#define PIN_KEY0     GET_PIN(C, 0)


/* ==================== th_led1：动态线程 ==================== */

/* 动态线程句柄 */
static rt_thread_t th_led1 = RT_NULL;

/* LED1 手动挂起标志 */
static volatile rt_bool_t led1_suspended = RT_FALSE;


/* LED1 线程入口 */
static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t led_state = PIN_HIGH;

    /* 配置红色 LED 为输出模式 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);

    /* 默认关闭红色 LED */
    rt_pin_write(PIN_LED_R, PIN_HIGH);

    while (1)
    {
        /*
         * 检查挂起请求
         * 当前线程主动挂起自身
         */
        if (led1_suspended == RT_TRUE)
        {
            rt_kprintf("[th_led1] suspend self\n");

            /* RUNNING -> SUSPEND */
            rt_thread_suspend(rt_thread_self());

            /* 主动触发调度 */
            rt_schedule();

            /* 被 resume 后从这里继续运行 */
            rt_kprintf("[th_led1] resumed\n");
        }

        /* 翻转红色 LED */
        led_state = !led_state;
        rt_pin_write(PIN_LED_R, led_state);

        count++;

        /* 输出计数与系统 Tick */
        rt_kprintf("[th_led1] count = %d, tick = %d\n",
                   count,
                   rt_tick_get());

        /* 主动延时 500 ms */
        rt_thread_mdelay(500);
    }
}


/* ==================== th_led2：静态线程 ==================== */

/* 静态线程控制块 */
static struct rt_thread th_led2;

/* 静态线程栈：1024 Bytes */
static rt_uint8_t th_led2_stack[1024];


/* LED2 线程入口 */
static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t led_state = PIN_HIGH;

    /* 配置蓝色 LED 为输出模式 */
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);

    /* 默认关闭蓝色 LED */
    rt_pin_write(PIN_LED_B, PIN_HIGH);

    while (1)
    {
        /* 翻转蓝色 LED */
        led_state = !led_state;
        rt_pin_write(PIN_LED_B, led_state);

        count++;

        /* 输出执行次数 */
        rt_kprintf("[th_led2] count = %d\n", count);

        /* 主动延时 500 ms */
        rt_thread_mdelay(500);
    }
}


/* ==================== 线程状态工具函数 ==================== */

/* 将线程状态转换为字符串 */
static const char *thread_state_str(rt_thread_t thread)
{
    rt_uint8_t state;

    state = thread->stat & RT_THREAD_STAT_MASK;

    switch (state)
    {
    case RT_THREAD_INIT:
        return "INIT";

    case RT_THREAD_READY:
        return "READY";

    case RT_THREAD_RUNNING:
        return "RUNNING";

    case RT_THREAD_SUSPEND:
        return "SUSPEND";

    case RT_THREAD_CLOSE:
        return "CLOSE";

    default:
        return "UNKNOWN";
    }
}


/*
 * 估算线程剩余栈空间
 * RT-Thread 初始化线程栈时通常使用 0x23 ('#') 填充
 */
static rt_uint32_t thread_stack_free(rt_thread_t thread)
{
    rt_uint8_t *ptr;
    rt_uint32_t free_size = 0;

    ptr = (rt_uint8_t *)thread->stack_addr;

    while ((free_size < thread->stack_size) &&
           (*ptr == '#'))
    {
        ptr++;
        free_size++;
    }

    return free_size;
}


/* ==================== KEY0 按键检测 ==================== */

static void key_scan(void)
{
    /* 检测按键按下 */
    if (rt_pin_read(PIN_KEY0) == PIN_LOW)
    {
        /* 软件消抖 */
        rt_thread_mdelay(50);

        if (rt_pin_read(PIN_KEY0) == PIN_LOW)
        {
            if (led1_suspended == RT_FALSE)
            {
                /*
                 * 第一次按键：
                 * 请求 th_led1 挂起自身
                 */
                led1_suspended = RT_TRUE;

                rt_kprintf("[KEY] request th_led1 suspend\n");
            }
            else
            {
                /*
                 * 第二次按键：
                 * 恢复 th_led1
                 */
                led1_suspended = RT_FALSE;

                /* SUSPEND -> READY */
                rt_thread_resume(th_led1);

                rt_kprintf("[KEY] th_led1 resume request\n");

                /* 重新调度 */
                rt_schedule();
            }

            /*
             * 等待按键释放
             * 防止长按被识别为多次触发
             */
            while (rt_pin_read(PIN_KEY0) == PIN_LOW)
            {
                rt_thread_mdelay(10);
            }
        }
    }
}


/* ==================== th_monitor：监控线程 ==================== */

/* 动态监控线程句柄 */
static rt_thread_t th_monitor = RT_NULL;


/* monitor 线程入口 */
static void monitor_entry(void *parameter)
{
    rt_tick_t last_monitor_tick;

    last_monitor_tick = rt_tick_get();

    while (1)
    {
        /* 高频扫描按键 */
        key_scan();

        /* 每 2 秒打印一次线程状态 */
        if ((rt_tick_get() - last_monitor_tick) >=
            rt_tick_from_millisecond(2000))
        {
            last_monitor_tick = rt_tick_get();

            rt_kprintf("\n");
            rt_kprintf("----- thread monitor -----\n");

            rt_kprintf("th_led1: state=%s, stack_free=%d Bytes\n",
                       thread_state_str(th_led1),
                       thread_stack_free(th_led1));

            rt_kprintf("th_led2: state=%s, stack_free=%d Bytes\n",
                       thread_state_str(&th_led2),
                       thread_stack_free(&th_led2));

            rt_kprintf("--------------------------\n");
        }

        /* monitor 扫描周期约 10 ms */
        rt_thread_mdelay(10);
    }
}


/* ==================== MSH 命令 ==================== */

/*
 * thread_dump
 * 手动查看工作线程状态、优先级和剩余栈
 */
static void thread_dump(void)
{
    rt_kprintf("\n");
    rt_kprintf("========== thread dump ==========\n");

    rt_kprintf("th_led1:\n");
    rt_kprintf("  state      : %s\n",
               thread_state_str(th_led1));
    rt_kprintf("  priority   : %d\n",
               th_led1->current_priority);
    rt_kprintf("  stack free : %d Bytes\n",
               thread_stack_free(th_led1));

    rt_kprintf("\n");

    rt_kprintf("th_led2:\n");
    rt_kprintf("  state      : %s\n",
               thread_state_str(&th_led2));
    rt_kprintf("  priority   : %d\n",
               th_led2.current_priority);
    rt_kprintf("  stack free : %d Bytes\n",
               thread_stack_free(&th_led2));

    rt_kprintf("=================================\n");
}


/* 导出 MSH 命令 */
MSH_CMD_EXPORT(thread_dump, show thread status and stack usage);


/* ==================== 模块初始化 ==================== */

static int thread_study_init(void)
{
    rt_err_t result;

    rt_kprintf("thread_study_init success!\n");


    /* ---------- 初始化 KEY0 ---------- */

    /* KEY0 上拉输入，按下为低电平 */
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);


    /* ---------- 动态创建 th_led1 ---------- */

    /*
     * th_led1
     * 栈大小：1024 Bytes
     * 优先级：16
     * 时间片：5 ticks
     */
    th_led1 = rt_thread_create("th_led1",
                               led1_entry,
                               RT_NULL,
                               1024,
                               16,
                               5);

    if (th_led1 != RT_NULL)
    {
        /* INIT -> READY */
        rt_thread_startup(th_led1);

        rt_kprintf("th_led1 create success!\n");
    }
    else
    {
        rt_kprintf("th_led1 create failed!\n");
    }


    /* ---------- 静态初始化 th_led2 ---------- */

    /*
     * th_led2
     * 静态 TCB
     * 静态栈：1024 Bytes
     * 优先级：16
     * 时间片：5 ticks
     */
    result = rt_thread_init(&th_led2,
                            "th_led2",
                            led2_entry,
                            RT_NULL,
                            th_led2_stack,
                            sizeof(th_led2_stack),
                            16,
                            5);

    if (result == RT_EOK)
    {
        /* INIT -> READY */
        rt_thread_startup(&th_led2);

        rt_kprintf("th_led2 init success!\n");
    }
    else
    {
        rt_kprintf("th_led2 init failed!\n");
    }


    /* ---------- 动态创建 th_monitor ---------- */

    /*
     * th_monitor
     * 栈大小：1024 Bytes
     * 优先级：10
     * 时间片：5 ticks
     *
     * 优先级 10 高于两个 LED 线程的 16
     */
    th_monitor = rt_thread_create("th_monitor",
                                  monitor_entry,
                                  RT_NULL,
                                  1024,
                                  10,
                                  5);

    if (th_monitor != RT_NULL)
    {
        /* INIT -> READY */
        rt_thread_startup(th_monitor);

        rt_kprintf("th_monitor create success!\n");
    }
    else
    {
        rt_kprintf("th_monitor create failed!\n");
    }


    return 0;
}


/*
 * 自动初始化
 */
INIT_APP_EXPORT(thread_study_init);
