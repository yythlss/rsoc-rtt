/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-18     Lenovo       the first version
 */

/*
 * 程序清单：线程动态创建 / 静态初始化、线程挂起与恢复、线程监控
 *
 * 本模块通过 INIT_APP_EXPORT() 自动加载（main.c 中无任何硬编码调用）：
 *
 *   th_led1   : 动态创建，优先级 16、时间片 5 tick、栈 1024 字节。
 *               循环翻转板载 LED_R（PF12），每次执行打印计数与 rt_tick_get()。
 *   th_led2   : 静态初始化（全局静态栈 + 静态 TCB），优先级 16、时间片 5 tick。
 *               循环翻转板载 LED_B（PF11），每次执行打印计数。
 *   th_monitor: 动态创建，优先级 10（高于工作线程），周期 2000 ms
 *               （100 ms 切片实现，按键响应最大延迟 100 ms）。
 *               唤醒后打印 th_led1 / th_led2 的运行状态与剩余栈空间；
 *               板载按键 KEY0（PC0，按下为低电平）采用下降沿中断投递事件：
 *               第一次触发挂起 th_led1，再次触发恢复 th_led1。
 *
 * 重要（挂起机制）：
 *   本 SDK 内核的 rt_thread_suspend() 带有 RT_ASSERT(thread == rt_thread_self())，
 *   即只允许"线程挂起自身"；RT_DEBUG 开启时对其它线程调用会断言失败并使
 *   调用者死循环（这正是此前"按键无法恢复"的根因：th_monitor 挂死）。
 *   因此挂起 th_led1 采用"挂起请求 + 自我挂起"方式：
 *     th_monitor 置挂起请求标志 -> th_led1 检测到后调用
 *     rt_thread_suspend(rt_thread_self()) 并 rt_schedule() 让出 CPU；
 *   恢复仍由 th_monitor 调用 rt_thread_resume() 完成（该函数无此限制）。
 *
 * MSH 调试命令：thread_dump —— 打印三个线程的状态、优先级与剩余栈空间。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* 板载 LED 引脚（共阳 RGB-LED，输出低电平点亮） */
#define PIN_LED_R           GET_PIN(F, 12)      /* PF12 : LED_R --> LED */
#define PIN_LED_B           GET_PIN(F, 11)      /* PF11 : LED_B --> LED */

/* 板载按键引脚（按下为低电平） */
#define PIN_KEY0            GET_PIN(C, 0)       /* PC0  : KEY0  --> KEY */

/* 工作线程参数：优先级 16、时间片 5 tick、栈 1024 字节 */
#define LED_THREAD_PRIORITY     16
#define LED_THREAD_TIMESLICE    5
#define LED_THREAD_STACK_SIZE   1024
/* 两线程翻转周期保持一致（500 ms），同优先级 + 同时间片下严格轮转交替闪烁 */
#define LED_BLINK_PERIOD_TICK   (RT_TICK_PER_SECOND / 2)       /* 500 ms */

/* 监控线程参数：优先级 10（高于工作线程）、周期 2000 ms、切片 100 ms */
#define MONITOR_PRIORITY        10
#define MONITOR_TIMESLICE       5
#define MONITOR_STACK_SIZE      2048
#define MONITOR_PERIOD_MS       2000
#define MONITOR_SLICE_MS        100

/* KEY0 软件消抖窗口：200 ms 内只响应一次 */
#define KEY_DEBOUNCE_TICK       (RT_TICK_PER_SECOND / 5)

/* 线程句柄 */
static rt_thread_t th_led1 = RT_NULL;      /* 动态线程 */
static rt_thread_t th_led2 = RT_NULL;      /* 静态线程，指向静态 TCB */
static rt_thread_t th_monitor = RT_NULL;   /* 动态线程 */

/* th_led2 的全局静态栈与静态线程控制块 */
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t th_led2_stack[LED_THREAD_STACK_SIZE];
static struct rt_thread th_led2_tcb;

/* KEY0 按键事件信号量：中断中释放，th_monitor 中获取 */
static struct rt_semaphore key_sem;

/* th_led1 挂起请求标志：th_monitor 置位，th_led1 检测后自我挂起 */
static volatile rt_uint8_t led1_pause_request = 0;

/* th_led1 当前挂起状态（仅 th_monitor 访问） */
static volatile rt_uint8_t led1_suspended = 0;

/* KEY0 下降沿中断回调：只做消抖与事件投递，不做业务逻辑 */
static void key0_irq_handler(void *args)
{
    static rt_tick_t last_tick = 0;
    rt_tick_t now = rt_tick_get();

    (void)args;

    if (now - last_tick > KEY_DEBOUNCE_TICK)
    {
        last_tick = now;
        rt_sem_release(&key_sem);
    }
}

/* 线程状态码 --> 可读字符串 */
static const char *thread_stat_str(rt_uint8_t stat)
{
    switch (stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_READY:   return "ready";
    case RT_THREAD_SUSPEND: return "suspend";
    case RT_THREAD_RUNNING: return "running";
    case RT_THREAD_INIT:    return "init";
    case RT_THREAD_CLOSE:   return "close";
    default:                return "unknown";
    }
}

/*
 * 计算线程剩余栈空间（字节）。
 * 线程栈在创建/初始化时被填充为 '#'，Cortex-M 栈向下增长，
 * 因此从栈底向上扫描连续 '#' 的个数即为剩余栈空间（与 list_thread 命令算法一致）。
 */
static rt_uint32_t thread_stack_free(rt_thread_t thread)
{
    rt_uint8_t *ptr;
    rt_uint8_t *end;

    if (thread == RT_NULL)
    {
        return 0;
    }

    ptr = (rt_uint8_t *)thread->stack_addr;
    end = (rt_uint8_t *)thread->stack_addr + thread->stack_size;

    while ((ptr < end) && (*ptr == '#'))
    {
        ptr++;
    }

    return (rt_uint32_t)(ptr - (rt_uint8_t *)thread->stack_addr);
}

/* 打印单个线程的运行状态、优先级、剩余栈空间 */
static void dump_thread(rt_thread_t thread)
{
    rt_uint8_t stat;
    rt_uint32_t free_size;

    if (thread == RT_NULL)
    {
        rt_kprintf("  (null thread)\n");
        return;
    }

    stat = thread->stat & RT_THREAD_STAT_MASK;
    free_size = thread_stack_free(thread);

    rt_kprintf("  %-10s pri=%2d  stat=%-8s  stack=%4d  free=%4d  used=%3d%%\n",
               thread->name,
               thread->current_priority,
               thread_stat_str(stat),
               thread->stack_size,
               free_size,
               (thread->stack_size - free_size) * 100 / thread->stack_size);
}

/* 工作线程 1：动态创建，循环翻转 LED_R，每次执行打印计数与系统节拍 */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    (void)parameter;

    while (1)
    {
        /* 收到 th_monitor 的挂起请求：本内核仅允许线程挂起自身，
           因此由 th_led1 自行挂起，并手动触发调度让出 CPU，
           直到 th_monitor 调用 rt_thread_resume() 恢复。 */
        if (led1_pause_request)
        {
            led1_pause_request = 0;
            rt_thread_suspend(rt_thread_self());
            rt_schedule();
        }

        count++;

        /* 翻转 LED_R：读回当前电平并取反写入 */
        rt_pin_write(PIN_LED_R, !rt_pin_read(PIN_LED_R));
        rt_kprintf("[th_led1] count=%06u | tick=%010u\n", count, rt_tick_get());

        rt_thread_mdelay(LED_BLINK_PERIOD_TICK);
    }
}

/* 工作线程 2：静态初始化，循环翻转 LED_B，每次执行打印计数 */
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    (void)parameter;

    while (1)
    {
        count++;

        /* 翻转 LED_B：读回当前电平并取反写入 */
        rt_pin_write(PIN_LED_B, !rt_pin_read(PIN_LED_B));
        rt_kprintf("[th_led2] count=%06u\n", count);

        rt_thread_mdelay(LED_BLINK_PERIOD_TICK);
    }
}

/* 按键事件处理：切换 th_led1 的挂起/恢复 */
static void key_event_handle(void)
{
    rt_err_t ret;

    if (th_led1 == RT_NULL)
    {
        rt_kprintf("[KEY] th_led1 is NULL, cannot control\n");
        return;
    }

    if (led1_suspended == 0)
    {
        /* --- 请求挂起：置标志，由 th_led1 自行挂起（见 th_led1_entry） --- */
        led1_pause_request = 1;
        led1_suspended = 1;
        rt_kprintf(">>> KEY0: suspend th_led1 request sent\n");
    }
    else
    {
        /* --- 恢复 --- */
        ret = rt_thread_resume(th_led1);
        if (ret == RT_EOK)
        {
            led1_suspended = 0;
            rt_kprintf(">>> KEY0: th_led1 resumed\n");
        }
        else
        {
            rt_kprintf(">>> KEY0: resume th_led1 failed, err=%d\n", ret);
        }
    }
}

/* 监控与控制线程：周期打印工作线程状态，按键控制 th_led1 挂起/恢复 */
static void th_monitor_entry(void *parameter)
{
    rt_uint32_t round = 0;
    rt_uint32_t i;
    const rt_uint32_t slice_num = MONITOR_PERIOD_MS / MONITOR_SLICE_MS;

    (void)parameter;

    while (1)
    {
        round++;

        /* 周期打印 th_led1 / th_led2 的运行状态与剩余栈空间 */
        rt_kprintf("\n+------------------------------------------+\n");
        rt_kprintf("| MONITOR #%-4u | tick=%-8u            |\n", round, rt_tick_get());
        rt_kprintf("+------------------------------------------+\n");
        dump_thread(th_led1);
        dump_thread(th_led2);
        if (led1_suspended)
        {
            rt_kprintf("  [hint] th_led1 suspended, press KEY0 to resume\n");
        }
        else
        {
            rt_kprintf("  [hint] press KEY0 to suspend th_led1\n");
        }
        rt_kprintf("+------------------------------------------+\n");

        /*
         * 周期 2000ms：切成 20 x 100ms，每片结束后非阻塞地检查按键事件，
         * 按键响应最大延迟从 2000ms 降到 100ms。
         */
        for (i = 0; i < slice_num; i++)
        {
            rt_thread_mdelay(MONITOR_SLICE_MS);

            if (rt_sem_take(&key_sem, RT_WAITING_NO) == RT_EOK)
            {
                key_event_handle();
            }
        }
    }
}

/* MSH 命令 thread_dump：打印三个线程的状态与剩余栈空间 */
static int thread_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("\n========== thread dump @ tick %u ==========\n", rt_tick_get());
    rt_kprintf("  %-10s %-4s %-9s %-6s %-6s %-6s\n",
               "name", "pri", "stat", "stack", "free", "used%");
    rt_kprintf("----------------------------------------------\n");
    dump_thread(th_led1);
    dump_thread(th_led2);
    dump_thread(th_monitor);
    rt_kprintf("----------------------------------------------\n");
    rt_kprintf("  led1_suspended = %u\n", led1_suspended);
    rt_kprintf("=============================================\n\n");

    return 0;
}
MSH_CMD_EXPORT(thread_dump, dump th_led1/th_led2/th_monitor status and free stack);

/* 模块初始化：由 INIT_APP_EXPORT 自动加载，main.c 中无需任何调用 */
static int led_monitor_init(void)
{
    rt_err_t ret;

    /* 配置 LED 引脚为输出模式，初始熄灭（高电平） */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);
    rt_pin_write(PIN_LED_B, PIN_HIGH);

    /* 初始化按键事件信号量（必须先于中断使能完成） */
    rt_sem_init(&key_sem, "key_sem", 0, RT_IPC_FLAG_FIFO);

    /* 配置 KEY0 为上拉输入，下降沿触发中断（按下为低电平） */
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);
    ret = rt_pin_attach_irq(PIN_KEY0, PIN_IRQ_MODE_FALLING, key0_irq_handler, RT_NULL);
    if (ret != RT_EOK)
    {
        rt_kprintf("[ledmon] attach key0 irq failed, err=%d\n", ret);
    }
    else
    {
        ret = rt_pin_irq_enable(PIN_KEY0, PIN_IRQ_ENABLE);
        if (ret != RT_EOK)
        {
            rt_kprintf("[ledmon] enable key0 irq failed, err=%d\n", ret);
        }
        else
        {
            rt_kprintf("[ledmon] key0 irq attached, pin=%d\n", PIN_KEY0);
        }
    }

    /* 动态创建 th_led1：优先级 16，时间片 5，栈 1024 字节 */
    th_led1 = rt_thread_create("th_led1",
                               th_led1_entry,
                               RT_NULL,
                               LED_THREAD_STACK_SIZE,
                               LED_THREAD_PRIORITY,
                               LED_THREAD_TIMESLICE);
    if (th_led1 != RT_NULL)
    {
        rt_thread_startup(th_led1);
        rt_kprintf("[ledmon] th_led1 created (dynamic), pri=%d, timeslice=%d, stack=%d B\n",
                   LED_THREAD_PRIORITY, LED_THREAD_TIMESLICE, LED_THREAD_STACK_SIZE);
    }
    else
    {
        rt_kprintf("[ledmon] create th_led1 failed\n");
        return -RT_ENOMEM;
    }

    /* 静态初始化 th_led2：全局静态栈 + 静态 TCB，优先级 16，时间片 5 */
    ret = rt_thread_init(&th_led2_tcb,
                         "th_led2",
                         th_led2_entry,
                         RT_NULL,
                         &th_led2_stack[0],
                         sizeof(th_led2_stack),
                         LED_THREAD_PRIORITY,
                         LED_THREAD_TIMESLICE);
    if (ret == RT_EOK)
    {
        rt_thread_startup(&th_led2_tcb);
        th_led2 = &th_led2_tcb;
        rt_kprintf("[ledmon] th_led2 initialized (static), pri=%d, timeslice=%d, stack=%d B\n",
                   LED_THREAD_PRIORITY, LED_THREAD_TIMESLICE, sizeof(th_led2_stack));
    }
    else
    {
        rt_kprintf("[ledmon] init th_led2 failed, err=%d\n", ret);
        return ret;
    }

    /* 动态创建 th_monitor：优先级 10，周期 2000 ms（100 ms 切片） */
    th_monitor = rt_thread_create("th_monitor",
                                  th_monitor_entry,
                                  RT_NULL,
                                  MONITOR_STACK_SIZE,
                                  MONITOR_PRIORITY,
                                  MONITOR_TIMESLICE);
    if (th_monitor != RT_NULL)
    {
        rt_thread_startup(th_monitor);
        rt_kprintf("[ledmon] th_monitor created (dynamic), pri=%d, period=%d ms\n",
                   MONITOR_PRIORITY, MONITOR_PERIOD_MS);
    }
    else
    {
        rt_kprintf("[ledmon] create th_monitor failed\n");
        return -RT_ENOMEM;
    }

    rt_kprintf("[ledmon] module loaded by INIT_APP_EXPORT\n");
    return RT_EOK;
}
INIT_APP_EXPORT(led_monitor_init);
