/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-18     李文轩        Day2 homework: multi-thread demo
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define DBG_TAG "thread_study"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 星火1号 LED / 按键引脚定义 */
#define PIN_LED_R       GET_PIN(F, 12)      /* PF12 : 红色 LED */
#define PIN_LED_B       GET_PIN(F, 11)      /* PF11 : 蓝色 LED */
#define PIN_KEY0        GET_PIN(C, 0)       /* PC0  : 按键 KEY0 */

/* th_led1 的全局句柄（动态线程，供监控线程挂起/恢复） */
static rt_thread_t th_led1_tid = RT_NULL;


/* ==================== th_led1：动态线程（红色 LED） ==================== */

/* th_led1 线程入口：翻转红色 LED，打印计数和时间戳 */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        rt_pin_write(PIN_LED_R, count % 2);      /* 翻转 LED_R：奇偶交替 */
        rt_kprintf("th_led1 count: %d, tick: %d\n", count, rt_tick_get());
        count++;
        rt_thread_mdelay(500);                    /* 每 500ms 翻转一次，顺便让出 CPU */
    }
}

/* 创建并启动 th_led1 动态线程 */
static int thread_sample_led1(void)
{
    th_led1_tid = rt_thread_create("th_led1",              /* 线程名 */
                                   th_led1_entry,          /* 入口函数 */
                                   RT_NULL,                /* 参数 */
                                   1024,                   /* 栈大小 1024 字节 */
                                   16,                     /* 优先级 16 */
                                   5);                     /* 时间片 5 ticks */
    if (th_led1_tid != RT_NULL)
    {
        rt_thread_startup(th_led1_tid);                   /* 启动线程 */
        LOG_D("th_led1 created");
        return RT_EOK;
    }
    else
    {
        LOG_E("th_led1 create failed!");
        return -RT_ERROR;
    }
}


/* ==================== th_led2：静态线程（蓝色 LED） ==================== */

/* th_led2 静态资源：全局 TCB + 全局栈（编译期分配，用户提供） */
static struct rt_thread th_led2;                 /* 线程控制块（全局） */
static rt_uint8_t th_led2_stack[1024];           /* 线程栈（全局，1024 字节） */

/* th_led2 线程入口：翻转蓝色 LED，打印计数 */
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        rt_pin_write(PIN_LED_B, count % 2);      /* 翻转 LED_B */
        rt_kprintf("th_led2 count: %d\n", count);
        count++;
        rt_thread_mdelay(500);
    }
}

/* 静态初始化并启动 th_led2 */
static int thread_sample_led2(void)
{
    rt_err_t result;

    result = rt_thread_init(&th_led2,            /* 线程控制块（用户提供） */
                            "th_led2",           /* 线程名 */
                            th_led2_entry,       /* 入口函数 */
                            RT_NULL,             /* 参数 */
                            th_led2_stack,       /* 栈首地址（用户提供！） */
                            sizeof(th_led2_stack),/* 栈大小 1024 */
                            16,                  /* 优先级 16 */
                            5);                  /* 时间片 5 ticks */
    if (result == RT_EOK)
    {
        rt_thread_startup(&th_led2);             /* 启动线程 */
        LOG_D("th_led2 created");
        return RT_EOK;
    }
    else
    {
        LOG_E("th_led2 init failed!");
        return -RT_ERROR;
    }
}


/* ==================== th_monitor：监控线程 ==================== */

/* 计算线程剩余栈空间：用栈低地址指针与当前 sp 的差值（仅粗略参考） */
static rt_uint32_t thread_stack_remain(rt_thread_t tid)
{
    rt_uint8_t *stack_bottom;

    if (tid == RT_NULL)
        return 0;

    /* 栈从高地址向低地址增长，sp 离栈底越远，剩余越多 */
    stack_bottom = (rt_uint8_t *)tid->stack_addr;
    return (rt_uint32_t)((rt_uint32_t)tid->sp - (rt_uint32_t)stack_bottom);
}

/* th_monitor 监控线程：打印线程状态/栈 + 按键控制 th_led1 */
static void th_monitor_entry(void *parameter)
{
    rt_thread_t led1 = th_led1_tid;
    rt_thread_t led2 = &th_led2;
    rt_bool_t led1_suspended = RT_FALSE;

    while (1)
    {
        /* 打印两个工作线程的状态与剩余栈空间 */
        rt_kprintf("[monitor] th_led1 stat:%d remain_stack:%d\n",
                   led1->stat, thread_stack_remain(led1));
        rt_kprintf("[monitor] th_led2 stat:%d remain_stack:%d\n",
                   led2->stat, thread_stack_remain(led2));

        /* 检测 KEY0：按下（低电平）则挂起/恢复 th_led1 */
        if (rt_pin_read(PIN_KEY0) == PIN_LOW)
        {
            rt_thread_mdelay(50);                 /* 消抖 */
            if (rt_pin_read(PIN_KEY0) == PIN_LOW)
            {
                if (led1_suspended == RT_FALSE)
                {
                    rt_thread_suspend(led1);
                    rt_schedule();                /* 手动触发调度 */
                    led1_suspended = RT_TRUE;
                    LOG_D("KEY0: th_led1 suspended!");
                }
                else
                {
                    rt_thread_resume(led1);
                    led1_suspended = RT_FALSE;
                    LOG_D("KEY0: th_led1 resumed!");
                }
                /* 等待按键释放，防止重复触发 */
                while (rt_pin_read(PIN_KEY0) == PIN_LOW)
                {
                    rt_thread_mdelay(10);
                }
            }
        }

        rt_thread_mdelay(2000);                   /* 周期 2s */
    }
}

/* 创建并启动 th_monitor 监控线程 */
static int thread_sample_monitor(void)
{
    rt_thread_t tid = RT_NULL;

    tid = rt_thread_create("th_monitor",          /* 线程名 */
                           th_monitor_entry,      /* 入口函数 */
                           RT_NULL,               /* 参数 */
                           1024,                  /* 栈大小 */
                           10,                    /* 优先级 10（高于工作线程） */
                           5);                    /* 时间片 */
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
        LOG_D("th_monitor created");
        return RT_EOK;
    }
    else
    {
        LOG_E("th_monitor create failed!");
        return -RT_ERROR;
    }
}


/* ==================== MSH 命令：thread_dump ==================== */

/* 自定义查看命令：用 rt_thread_find 按名字查找并打印线程信息 */
static void cmd_thread_dump(void)
{
    rt_thread_t tid1 = rt_thread_find("th_led1");
    rt_thread_t tid2 = rt_thread_find("th_led2");
    rt_thread_t tmon = rt_thread_find("th_monitor");

    rt_kprintf("name         status   prio   remain_stack\n");
    rt_kprintf("----------------------------------------\n");

    if (tid1 != RT_NULL)
        rt_kprintf("%-10s  %-6d  %-5d  %d\n", "th_led1", tid1->stat,
                   tid1->current_priority, thread_stack_remain(tid1));
    if (tid2 != RT_NULL)
        rt_kprintf("%-10s  %-6d  %-5d  %d\n", "th_led2", tid2->stat,
                   tid2->current_priority, thread_stack_remain(tid2));
    if (tmon != RT_NULL)
        rt_kprintf("%-10s  %-6d  %-5d  %d\n", "th_monitor", tmon->stat,
                   tmon->current_priority, thread_stack_remain(tmon));
}
MSH_CMD_EXPORT_ALIAS(cmd_thread_dump, thread_dump, dump thread info);


/* ==================== 自动初始化 ==================== */

/* 应用自动初始化：依次创建三个线程（INIT_APP_EXPORT，无需在 main 中调用） */
static int thread_sample_init(void)
{
    /* 先创建两个工作线程，再创建监控线程（监控线程引用它们的句柄） */
    if (thread_sample_led1() != RT_EOK)
        return -RT_ERROR;
    if (thread_sample_led2() != RT_EOK)
        return -RT_ERROR;
    if (thread_sample_monitor() != RT_EOK)
        return -RT_ERROR;

    LOG_D("thread study sample initialized");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_sample_init);
