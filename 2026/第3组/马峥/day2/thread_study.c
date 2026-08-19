/*
 * RT-Thread 多线程编程实验 - DAY2 作业
 *
 * 目标平台：STM32F407 星火号（stm32f407-rtspark）
 * 包含：动态线程 + 静态线程 + 监控线程 + INIT_EXPORT + MSH 命令
 *
 * 作者：马峥 | 第3组
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* 日志TAG */
#define DBG_TAG    "thread_study"
#define DBG_LVL    DBG_INFO
#include <rtdbg.h>

/* ===================== 硬件定义 ===================== */
#define LED_R_PIN    GET_PIN(F, 9)   /* LED_R  PF9  */
#define LED_B_PIN    GET_PIN(F, 10)  /* LED_B  PF10 */
#define KEY_PIN      GET_PIN(A, 0)   /* KEY0   PA0  */

/* ===================== 全局变量 ===================== */
/* th_led2 静态线程：TCB、栈、计数 */
static struct rt_thread th_led2;
static rt_uint8_t th_led2_stack[1024];
static rt_uint32_t led2_count = 0;

/* th_led1 动态线程句柄（用于挂起/恢复） */
static rt_thread_t th_led1_handle = RT_NULL;
static rt_uint32_t led1_count = 0;

/* 线程状态统计（挂起计数，用于按键控制） */
static rt_uint8_t th_led1_suspended = 0;

/* ===================== 工作线程1（动态创建）===================== */
/*
 * th_led1：动态创建，优先级16，时间片5 ticks，栈1024 Bytes
 * 功能：循环翻转 LED_R，每次打印计数与 rt_tick_get()
 */
static void th_led1_entry(void *parameter)
{
    (void)parameter;

    /* 配置 LED_R 为输出模式 */
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);

    while (1)
    {
        led1_count++;

        /* 翻转 LED_R */
        static rt_uint8_t led_state = 0;
        led_state = !led_state;
        rt_pin_write(LED_R_PIN, led_state ? PIN_HIGH : PIN_LOW);

        /* 打印计数与系统 tick */
        rt_kprintf("[th_led1] toggle LED_R, count=%u, tick=%u\n",
                   (unsigned int)led1_count,
                   (unsigned int)rt_tick_get());

        /* 延时 500ms（让出 CPU，允许其他线程执行） */
        rt_thread_mdelay(500);
    }
}

/* ===================== 工作线程2（静态初始化）===================== */
/*
 * th_led2：静态初始化，优先级16，时间片5 ticks，静态TCB+全局栈
 * 功能：循环翻转 LED_B，每次打印计数
 */
static void th_led2_entry(void *parameter)
{
    (void)parameter;

    /* 配置 LED_B 为输出模式 */
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);

    while (1)
    {
        led2_count++;

        /* 翻转 LED_B */
        static rt_uint8_t led_state = 0;
        led_state = !led_state;
        rt_pin_write(LED_B_PIN, led_state ? PIN_HIGH : PIN_LOW);

        /* 打印计数 */
        rt_kprintf("[th_led2] toggle LED_B, count=%u\n",
                   (unsigned int)led2_count);

        /* 延时 500ms */
        rt_thread_mdelay(500);
    }
}

/* ===================== 监控线程（动态创建）===================== */
/*
 * th_monitor：动态创建，优先级10（高于工作线程），周期 2000ms
 * 功能：查询并打印 th_led1/th_led2 的运行状态与剩余栈空间
 *        检测按键：单次按下挂起 th_led1，再次按下恢复 th_led1
 */
static void th_monitor_entry(void *parameter)
{
    (void)parameter;
    struct rt_thread *t_led1 = RT_NULL;
    struct rt_thread *t_led2 = RT_NULL;
    rt_uint32_t last_key_state = 1;  /* 按键上拉，默认高电平（未按下） */
    rt_uint32_t debounce_tick = 0;

    /* 配置 KEY 引脚为上拉输入 */
    rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);

    /* 查找目标线程（监控线程优先级更高，th_led1/th_led2 已启动） */
    t_led1 = rt_thread_find("led1");
    t_led2 = rt_thread_find("led2");

    if (!t_led1 || !t_led2)
    {
        LOG_E("[th_monitor] thread not found, led1=%p, led2=%p",
              t_led1, t_led2);
        return;
    }

    rt_kprintf("[th_monitor] started, monitor period=2000ms\n");

    while (1)
    {
        /* ---------- 按键检测（带消抖）---------- */
        rt_uint32_t key_level = rt_pin_read(KEY_PIN);

        if (key_level == 0 && last_key_state == 1)
        {
            /* 检测到下降沿（按键按下），延时消抖 */
            rt_thread_mdelay(20);

            if (rt_pin_read(KEY_PIN) == 0)
            {
                /* 确认按键按下 */
                if (th_led1_suspended == 0)
                {
                    /* th_led1 当前运行，挂起它 */
                    rt_err_t err = rt_thread_suspend(t_led1);
                    if (err == RT_EOK)
                    {
                        th_led1_suspended = 1;
                        rt_kprintf("[KEY] pressed, suspend th_led1\n");
                    }
                    else
                    {
                        rt_kprintf("[KEY] suspend failed: %d\n", err);
                    }
                }
                else
                {
                    /* th_led1 当前挂起，恢复它 */
                    rt_err_t err = rt_thread_resume(t_led1);
                    if (err == RT_EOK)
                    {
                        th_led1_suspended = 0;
                        rt_kprintf("[KEY] pressed, resume th_led1\n");
                    }
                    else
                    {
                        rt_kprintf("[KEY] resume failed: %d\n", err);
                    }
                }
            }
        }
        last_key_state = key_level;

        /* ---------- 打印线程状态与剩余栈空间 ---------- */
        /* 获取当前 tick */
        rt_tick_t now_tick = rt_tick_get();

        /* 获取线程状态字符串 */
        const char *get_thread_stat_str(rt_uint8_t stat)
        {
            switch (stat)
            {
            case RT_THREAD_READY:    return "READY   ";
            case RT_THREAD_SUSPEND:   return "SUSPEND ";
            case RT_THREAD_INIT:     return "INIT    ";
            case RT_THREAD_CLOSE:    return "CLOSE   ";
            default:                  return "UNKNOWN ";
            }
        }

        /* 打印分隔线 */
        rt_kprintf("\n========== [tick=%u] Thread Status ==========\n",
                   (unsigned int)now_tick);

        /* th_led1 状态 */
        if (t_led1)
        {
            rt_kprintf("[THREAD] th_led1: count=%u, stat=%s, priority=%u, "
                        "timeslice_left=%u, stack_left=~%d\n",
                       (unsigned int)led1_count,
                       get_thread_stat_str(t_led1->stat),
                       (unsigned int)t_led1->current_priority,
                       (unsigned int)t_led1->remaining_tick,
                       (int)(t_led1->stack_size - (rt_uint32_t)
                        (t_led1->sp - (void *)t_led1->stack_addr)));
        }

        /* th_led2 状态 */
        if (t_led2)
        {
            rt_kprintf("[THREAD] th_led2: count=%u, stat=%s, priority=%u, "
                        "timeslice_left=%u, stack_left=~%d\n",
                       (unsigned int)led2_count,
                       get_thread_stat_str(t_led2->stat),
                       (unsigned int)t_led2->current_priority,
                       (unsigned int)t_led2->remaining_tick,
                       (int)(t_led2->stack_size - (rt_uint32_t)
                        (t_led2->sp - (void *)t_led2->stack_addr)));
        }

        rt_kprintf("==============================================\n\n");

        /* 周期延时 2000ms（让出 CPU） */
        rt_thread_mdelay(2000);
    }
}

/* ===================== MSH 命令：thread_dump ===================== */
/*
 * 功能：导出为 msh 命令，用户可在 FinSH 终端执行 thread_dump 查看线程状态
 * 用法：msh >thread_dump
 */
static void thread_dump(void)
{
    rt_kprintf("\n========== thread_dump ==========\n");
    rt_kprintf(" %-12s | %-8s | %-8s | %-10s | %-10s | %-10s\n",
               "thread", "priority", "state", "sp", "stack_size", "stack_left");
    rt_kprintf("------------|----------|----------|------------|------------|------------\n");

    /* 遍历内核对象容器的线程链表 */
    struct rt_object *object;
    struct rt_list_node *node;

    for (node = rt_object_get_next(RT_NULL); node != RT_NULL; node = rt_list_next(node))
    {
        object = (struct rt_object *)rt_list_entry(node, struct rt_object, list);

        /* 仅处理线程对象 */
        if (object->type == RT_Object_Class_Thread)
        {
            struct rt_thread *t = (struct rt_thread *)object;

            /* 计算栈使用量（粗略估算） */
            int stack_used = (int)((rt_uint32_t)t->stack_size -
                         ((rt_uint32_t)t->sp - (rt_uint32_t)t->stack_addr));

            /* 状态转字符串 */
            const char *stat_str;
            switch (t->stat)
            {
            case RT_THREAD_READY:    stat_str = "READY";   break;
            case RT_THREAD_SUSPEND:  stat_str = "SUSPEND"; break;
            case RT_THREAD_RUNNING:  stat_str = "RUNNING"; break;
            case RT_THREAD_INIT:     stat_str = "INIT";    break;
            case RT_THREAD_CLOSE:    stat_str = "CLOSE";   break;
            default:                 stat_str = "UNKNOWN";  break;
            }

            rt_kprintf(" %-12s | %-8u | %-8s | 0x%08X | %-10u | %-10d\n",
                       t->name,
                       (unsigned int)t->current_priority,
                       stat_str,
                       (unsigned int)t->sp,
                       (unsigned int)t->stack_size,
                       stack_used);
        }
    }

    rt_kprintf("==================================\n\n");
}
/* 导出到 msh 命令 */
MSH_CMD_EXPORT(thread_dump, dump all threads status);

/* ===================== 模块初始化 ===================== */
/*
 * 功能：创建并启动所有工作线程和监控线程
 * 触发方式：通过 INIT_APP_EXPORT() 自动调用（无需在 main.c 中手动调用）
 */
static int thread_module_init(void)
{
    rt_err_t err;

    LOG_INFO("thread module init");
    rt_kprintf("[SYSTEM] thread module init\n");

    /* ---- 1. 创建并启动工作线程1（动态创建） ---- */
    th_led1_handle = rt_thread_create("led1",
                                       th_led1_entry,
                                       RT_NULL,
                                       1024,     /* 栈大小 1024 Bytes */
                                       16,       /* 优先级 16 */
                                       5);       /* 时间片 5 ticks */

    if (th_led1_handle != RT_NULL)
    {
        err = rt_thread_startup(th_led1_handle);
        if (err == RT_EOK)
        {
            rt_kprintf("[SYSTEM] th_led1 started (dynamic), "
                       "priority=16, timeslice=5, stack=1024\n");
        }
        else
        {
            LOG_E("[SYSTEM] th_led1 startup failed: %d", err);
        }
    }
    else
    {
        LOG_E("[SYSTEM] th_led1 create failed");
    }

    /* ---- 2. 初始化并启动工作线程2（静态初始化） ---- */
    err = rt_thread_init(&th_led2,
                         "led2",
                         th_led2_entry,
                         RT_NULL,
                         th_led2_stack,
                         sizeof(th_led2_stack),
                         16,      /* 优先级 16 */
                         5);      /* 时间片 5 ticks */

    if (err == RT_EOK)
    {
        err = rt_thread_startup(&th_led2);
        if (err == RT_EOK)
        {
            rt_kprintf("[SYSTEM] th_led2 started (static), "
                       "priority=16, timeslice=5, stack=%u\n",
                       (unsigned int)sizeof(th_led2_stack));
        }
        else
        {
            LOG_E("[SYSTEM] th_led2 startup failed: %d", err);
        }
    }
    else
    {
        LOG_E("[SYSTEM] th_led2 init failed: %d", err);
    }

    /* ---- 3. 创建并启动监控线程（动态创建） ---- */
    rt_thread_t th_monitor_handle = rt_thread_create("monitor",
                                                      th_monitor_entry,
                                                      RT_NULL,
                                                      1024,     /* 栈大小 1024 Bytes */
                                                      10,       /* 优先级 10（高于工作线程）*/
                                                      10);      /* 时间片 10 ticks */

    if (th_monitor_handle != RT_NULL)
    {
        err = rt_thread_startup(th_monitor_handle);
        if (err == RT_EOK)
        {
            rt_kprintf("[SYSTEM] th_monitor started, priority=10\n");
        }
        else
        {
            LOG_E("[SYSTEM] th_monitor startup failed: %d", err);
        }
    }
    else
    {
        LOG_E("[SYSTEM] th_monitor create failed");
    }

    return 0;
}
/*
 * INIT_APP_EXPORT：应用初始化宏，将 thread_module_init 注册到自动初始化链表
 * 执行时机：最晚（优先级6），在内核组件（FinSH等）初始化之后，用户 main 之前
 */
INIT_APP_EXPORT(thread_module_init);
