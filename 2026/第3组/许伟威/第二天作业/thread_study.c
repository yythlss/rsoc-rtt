/*
 * thread_study.c - RSOC26 Day2 多线程编程作业
 *
 * 目标平台 : STM32F407 星火1号 (stm32f407-rtspark)
 * 功能     : 动/静态线程创建、时间片轮转、优先级抢占、线程挂起/恢复、
 *            自动初始化(INIT_APP_EXPORT)、MSH 命令导出(MSH_CMD_EXPORT)
 *
 * 注意     : PIN_KEY 需按板卡原理图确认，下方为常见 F407 按键引脚，请自行核对。
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* ---------- 板载引脚定义 ---------- */
#define PIN_LED_R   GET_PIN(F, 12)   /* 红色 LED */
#define PIN_LED_B   GET_PIN(F, 11)   /* 蓝色 LED */
#define PIN_KEY     GET_PIN(E, 3)    /* 按键，按需修改为板卡实际按键引脚 */

/* ---------- 静态线程资源（th_led2 使用，编译期分配） ---------- */
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t      th_led2_stack[1024];   /* 静态栈（全局数组） */
static struct rt_thread th_led2_tcb;           /* 静态线程控制块 */

/* ---------- 动态线程句柄（运行期从堆分配） ---------- */
static rt_thread_t th_led1    = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;

/* th_led1 是否被挂起 */
static rt_bool_t led1_suspended = RT_FALSE;

/* ---------- 工具函数 ---------- */

/* 线程状态 -> 字符串 */
static const char *thread_stat_str(rt_uint8_t stat)
{
    switch (stat)
    {
    case RT_THREAD_INIT:    return "INIT";
    case RT_THREAD_READY:   return "READY";
    case RT_THREAD_SUSPEND: return "SUSPEND";
    case RT_THREAD_RUNNING: return "RUNNING";
    case RT_THREAD_CLOSE:   return "CLOSE";
    default:                return "UNKNOWN";
    }
}

/*
 * 计算剩余栈空间。
 * 内核在线程初始化时把整块栈填成 '#' 水印，运行后从栈底往上扫描，
 * 连续 '#' 的字节数即为未使用的栈空间。
 */
static rt_uint32_t remaining_stack(rt_thread_t thread)
{
    rt_uint8_t *base = (rt_uint8_t *)thread->stack_addr;
    rt_uint32_t unused = 0;

    while (unused < thread->stack_size && base[unused] == '#')
    {
        unused++;
    }
    return unused;
}

/* ---------- 工作线程 1：动态创建，翻转 LED_R ---------- */
static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t  level = PIN_LOW;

    while (1)
    {
        count++;
        level = (level == PIN_LOW) ? PIN_HIGH : PIN_LOW;
        rt_pin_write(PIN_LED_R, level);

        rt_kprintf("[th_led1] count=%u tick=%u\n",
                   count, (rt_uint32_t)rt_tick_get());

        rt_thread_mdelay(500);
    }
}

/* ---------- 工作线程 2：静态初始化，翻转 LED_B ---------- */
static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t  level = PIN_LOW;

    while (1)
    {
        count++;
        level = (level == PIN_LOW) ? PIN_HIGH : PIN_LOW;
        rt_pin_write(PIN_LED_B, level);

        rt_kprintf("[th_led2] count=%u\n", count);

        rt_thread_mdelay(500);
    }
}

/* ---------- 按键检测：单次触发切换 th_led1 挂起/恢复 ---------- */
static void check_key(void)
{
    if (rt_pin_read(PIN_KEY) == PIN_LOW)        /* 按下（低电平有效） */
    {
        rt_thread_mdelay(20);                   /* 消抖 */
        if (rt_pin_read(PIN_KEY) == PIN_LOW)
        {
            while (rt_pin_read(PIN_KEY) == PIN_LOW)
            {
                rt_thread_mdelay(10);           /* 等待松开 */
            }

            if (led1_suspended)
            {
                rt_thread_resume(th_led1);
                led1_suspended = RT_FALSE;
                rt_kprintf("[monitor] th_led1 resumed\n");
            }
            else
            {
                rt_thread_suspend(th_led1);
                led1_suspended = RT_TRUE;
                rt_kprintf("[monitor] th_led1 suspended\n");
            }
        }
    }
}

/* ---------- 监控线程：优先级 10，周期打印状态与剩余栈 ---------- */
static void th_monitor_entry(void *parameter)
{
    while (1)
    {
        rt_thread_mdelay(2000);

        check_key();

        rt_kprintf("[monitor] th_led1 stat=%s stack=%u/%u\n",
                   thread_stat_str(th_led1->stat),
                   remaining_stack(th_led1), th_led1->stack_size);
        rt_kprintf("[monitor] th_led2 stat=%s stack=%u/%u\n",
                   thread_stat_str(th_led2_tcb.stat),
                   remaining_stack(&th_led2_tcb), th_led2_tcb.stack_size);
    }
}

/* ---------- MSH 命令：thread_dump ---------- */
static void thread_dump(int argc, char **argv)
{
    rt_kprintf("---- thread study dump ----\n");
    rt_kprintf("th_led1   : stat=%s prio=%d tick=%d suspended=%s\n",
               thread_stat_str(th_led1->stat),
               th_led1->current_priority,
               th_led1->init_tick,
               led1_suspended ? "yes" : "no");
    rt_kprintf("th_led2   : stat=%s prio=%d tick=%d\n",
               thread_stat_str(th_led2_tcb.stat),
               th_led2_tcb.current_priority,
               th_led2_tcb.init_tick);
    rt_kprintf("th_monitor: stat=%s prio=%d\n",
               thread_stat_str(th_monitor->stat),
               th_monitor->current_priority);
}
MSH_CMD_EXPORT(thread_dump, dump thread study threads);

/* ---------- 应用自动初始化入口 ---------- */
static int thread_study_init(void)
{
    /* 初始化 LED 与按键引脚 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_KEY, PIN_MODE_INPUT_PULLUP);

    /* 工作线程 1：动态创建（优先级 16，时间片 5，栈 1024） */
    th_led1 = rt_thread_create("th_led1", th_led1_entry, RT_NULL,
                               1024, 16, 5);
    if (th_led1 != RT_NULL)
    {
        rt_thread_startup(th_led1);
    }

    /* 工作线程 2：静态初始化（全局栈 + 全局 TCB） */
    rt_thread_init(&th_led2_tcb, "th_led2", th_led2_entry, RT_NULL,
                   th_led2_stack, sizeof(th_led2_stack), 16, 5);
    rt_thread_startup(&th_led2_tcb);

    /* 监控线程：动态创建（优先级 10，高于工作线程） */
    th_monitor = rt_thread_create("th_monitor", th_monitor_entry, RT_NULL,
                                  1024, 10, 5);
    if (th_monitor != RT_NULL)
    {
        rt_thread_startup(th_monitor);
    }

    return 0;
}
INIT_APP_EXPORT(thread_study_init);
