th_led1.c
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define PIN_LED_R              GET_PIN(F, 12)      // PF12 :  LED_R        --> LED

static void led1_thread_entry(void *parameter);
rt_thread_t th_led1 = RT_NULL;
/* 模块初始化函数，使用 INIT_APP_EXPORT 自动加载 */
static int led1_thread_init(void)
{
    rt_thread_t th_led1 = RT_NULL;

    /* 动态创建线程 th_led1 */
    th_led1 = rt_thread_create("th_led1",           /* 线程名称 */
                               led1_thread_entry,   /* 线程入口函数 */
                               RT_NULL,             /* 线程入口参数 */
                               1024,                /* 线程栈大小，单位字节 */
                               16,                  /* 线程优先级 */
                               5);                  /* 线程时间片，单位 tick */

    /* 如果创建成功，则启动线程 */
    if (th_led1 != RT_NULL)
        rt_thread_startup(th_led1);
    else
        rt_kprintf("th_led1 create failed!\n");

    return 0;
}
/* 使用 INIT_APP_EXPORT 宏导出，系统启动后自动执行 */
INIT_APP_EXPORT(led1_thread_init);

/* 线程入口函数实现 */
static void led1_thread_entry(void *parameter)
{
    rt_uint32_t count = 0;


    while (1)
    {
        /* 翻转 LED_R */
        rt_pin_write(PIN_LED_R, PIN_LOW);
        rt_thread_mdelay(500);
        rt_pin_write(PIN_LED_R, PIN_HIGH);
        rt_thread_mdelay(500);

        count++;
//        rt_kprintf("th_led1 count: %d, tick: %d\n", count, rt_tick_get());
    }
}




th_led2.c
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
/* ===== 手动定义 LED_B 引脚 ===== */
#define PIN_LED_B              GET_PIN(F, 11)      // PF11 :  LED_B        --> LED

/* ===== 静态线程资源定义（全局变量） ===== */
struct rt_thread th_led2_tcb;          /* 线程控制块（TCB） */
static rt_uint8_t th_led2_stack[1024];        /* 线程栈，大小 1024 字节 */

/* 线程入口函数声明 */
static void led2_thread_entry(void *parameter);

/* 模块初始化函数，使用 INIT_APP_EXPORT 自动加载 */
static int led2_thread_init(void)
{
    rt_err_t result;

    /* 静态初始化线程 th_led2 */
    result = rt_thread_init(&th_led2_tcb,              /* 线程控制块地址 */
                            "th_led2",                 /* 线程名称 */
                            led2_thread_entry,         /* 线程入口函数 */
                            RT_NULL,                   /* 线程入口参数 */
                            &th_led2_stack[0],         /* 线程栈起始地址 */
                            sizeof(th_led2_stack),     /* 线程栈大小 */
                            16,                        /* 线程优先级 */
                            5);                        /* 线程时间片，单位 tick */

    if (result == RT_EOK) {
        /* 启动线程 */
        rt_thread_startup(&th_led2_tcb);
        rt_kprintf("th_led2 static init success!\n");
    } else {
        rt_kprintf("th_led2 static init failed!\n");
    }

    return 0;
}
/* 使用 INIT_APP_EXPORT 宏导出，系统启动后自动执行 */
INIT_APP_EXPORT(led2_thread_init);

/* ===== 线程入口函数实现 ===== */
static void led2_thread_entry(void *parameter)
{
    rt_uint32_t count = 0;

    /* 初始化 LED_B 引脚为推挽输出模式 */
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);

    while (1)
    {
        /* 翻转 LED_B：拉低点亮（假设低电平亮） */
        rt_pin_write(PIN_LED_B , PIN_LOW);
        rt_thread_mdelay(1000);
        rt_pin_write(PIN_LED_B , PIN_HIGH);
        rt_thread_mdelay(1000);

        count++;
//        rt_kprintf("th_led2 count: %d\n", count);
    }
}

th_monitor.c
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* ===== 外部引用：获取工作线程的句柄和控制块 ===== */
extern rt_thread_t th_led1;
extern struct rt_thread th_led2_tcb;

/* ===== 定义按键引脚 ===== */
#define PIN_KEY0        GET_PIN(C, 0)     /* PC0: KEY0 */

/* ===== 定义事件集 ===== */
static rt_event_t key_event = RT_NULL;
#define KEY_EVENT_FLAG   (1 << 0)

/* ===== 按键消抖时间（单位：毫秒） ===== */
#define KEY_DEBOUNCE_MS  50

/* ===== 函数声明 ===== */
static void monitor_thread_entry(void *parameter);
static void key_irq_callback(void *args);

/* ===== 获取线程状态字符串 ===== */
static const char* get_thread_state_str(rt_uint8_t state)
{
    state &= RT_THREAD_STAT_MASK;
    switch (state) {
        case RT_THREAD_INIT:      return "INIT";
        case RT_THREAD_READY:     return "READY";
        case RT_THREAD_RUNNING:   return "RUNNING";
        case RT_THREAD_SUSPEND:   return "SUSPEND";
        case RT_THREAD_CLOSE:     return "CLOSE";
        default:                  return "UNKNOWN";
    }
}

/* ===== 模块初始化函数 ===== */
static int monitor_thread_init(void)
{
    /* 1. 创建事件集 */
    key_event = rt_event_create("key_evt", RT_IPC_FLAG_PRIO);
    if (key_event == RT_NULL) {
        rt_kprintf("key_event create failed!\n");
        return -1;
    }

    /* 2. 配置按键引脚为输入上拉模式 */
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);

    /* 3. 注册按键中断（下降沿触发） */
    if (rt_pin_attach_irq(PIN_KEY0, PIN_IRQ_MODE_FALLING, key_irq_callback, RT_NULL) != RT_EOK) {
        rt_kprintf("key attach irq failed!\n");
        return -1;
    }
    rt_pin_irq_enable(PIN_KEY0, PIN_IRQ_ENABLE);
    rt_kprintf("key interrupt registered on pin %d\n", PIN_KEY0);

    /* 4. 动态创建监控线程 */
    rt_thread_t th_monitor = rt_thread_create("th_monitor",
                                              monitor_thread_entry,
                                              RT_NULL,
                                              2048,
                                              10,
                                              10);
    if (th_monitor != RT_NULL) {
        rt_thread_startup(th_monitor);
        rt_kprintf("th_monitor create success!\n");
    } else {
        rt_kprintf("th_monitor create failed!\n");
        return -1;
    }

    return 0;
}
//INIT_APP_EXPORT(monitor_thread_init);

/* ===== 按键中断回调函数 ===== */
static uint32_t last_key_tick = 0;

static void key_irq_callback(void *args)
{
    uint32_t current_tick = rt_tick_get();

    /* 软件消抖：50ms 内只响应一次 */
    if (current_tick - last_key_tick > KEY_DEBOUNCE_MS) {
        last_key_tick = current_tick;
        rt_event_send(key_event, KEY_EVENT_FLAG);
    }
}

/* ===== 获取线程剩余栈空间 ===== */
static size_t get_thread_stack_free(rt_thread_t thread)
{
    if (thread == RT_NULL)
        return 0;

    rt_ubase_t sp = (rt_ubase_t)thread->sp;
    rt_ubase_t stack_addr = (rt_ubase_t)thread->stack_addr;

    if (sp >= stack_addr) {
        return thread->stack_size - (sp - stack_addr);
    }
    return 0;
}

/* ===== 监控线程入口函数 ===== */
static void monitor_thread_entry(void *parameter)
{
    rt_uint32_t recv_events = 0;
    rt_err_t result;

    while (1)
    {
        /* ===== 1. 周期延时 2 秒 ===== */
        rt_thread_mdelay(2000);

        /* ===== 2. 打印两个工作线程的状态和剩余栈空间 ===== */
        rt_kprintf("\n========== Monitor Dump ==========\n");

        if (th_led1 != RT_NULL) {
            rt_uint8_t state1 = th_led1->stat & RT_THREAD_STAT_MASK;
            rt_kprintf("[th_led1] state: %s", get_thread_state_str(state1));
            if (state1 == RT_THREAD_SUSPEND) {
                rt_kprintf(" (SUSPENDED)");
            }
            rt_kprintf(", free stack: %d bytes\n", get_thread_stack_free(th_led1));
        } else {
            rt_kprintf("[th_led1] thread not exist\n");
        }

        {
            rt_uint8_t state2 = th_led2_tcb.stat & RT_THREAD_STAT_MASK;
            rt_kprintf("[th_led2] state: %s", get_thread_state_str(state2));
            if (state2 == RT_THREAD_SUSPEND) {
                rt_kprintf(" (SUSPENDED)");
            }
            rt_kprintf(", free stack: %d bytes\n", get_thread_stack_free((rt_thread_t)&th_led2_tcb));
        }

        rt_kprintf("==================================\n");

        /* ===== 3. 非阻塞检查按键事件 ===== */
        result = rt_event_recv(key_event,
                               KEY_EVENT_FLAG,
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               RT_WAITING_NO,
                               &recv_events);

        if (result == RT_EOK) {
            /* 切换 th_led1 的挂起/恢复状态 */
            if (th_led1 != RT_NULL) {
                rt_uint8_t state = th_led1->stat & RT_THREAD_STAT_MASK;

                if (state == RT_THREAD_SUSPEND) {
                    if (rt_thread_resume(th_led1) == RT_EOK) {
                        rt_kprintf(">>> th_led1 resumed by key press <<<\n");
                        rt_schedule();
                    }
                } else if (state == RT_THREAD_READY || state == RT_THREAD_RUNNING) {
                    if (rt_thread_suspend(th_led1) == RT_EOK) {
                        rt_kprintf(">>> th_led1 suspended by key press <<<\n");
                        rt_schedule();
                    }
                } else {
                    rt_kprintf("th_led1 state (%d) cannot suspend/resume\n", state);
                }
            }
        }
    }
}

cmd_thread_dump.c
#include <rtthread.h>

/* ===== 外部引用：获取工作线程的句柄和控制块 ===== */
extern rt_thread_t th_led1;
extern struct rt_thread th_led2_tcb;

/* ===== 获取线程状态字符串 ===== */
static const char* get_thread_state_str(rt_uint8_t state)
{
    state &= RT_THREAD_STAT_MASK;
    switch (state) {
        case RT_THREAD_INIT:      return "INIT";
        case RT_THREAD_READY:     return "READY";
        case RT_THREAD_RUNNING:   return "RUNNING";
        case RT_THREAD_SUSPEND:   return "SUSPEND";
        case RT_THREAD_CLOSE:     return "CLOSE";
        default:                  return "UNKNOWN";
    }
}

/* ===== 获取线程剩余栈空间 ===== */
static size_t get_thread_stack_free(rt_thread_t thread)
{
    if (thread == RT_NULL)
        return 0;

    rt_ubase_t sp = (rt_ubase_t)thread->sp;
    rt_ubase_t stack_addr = (rt_ubase_t)thread->stack_addr;

    if (sp >= stack_addr) {
        return thread->stack_size - (sp - stack_addr);
    }
    return 0;
}

/* ===== 打印单个线程信息 ===== */
static void print_thread_info(const char* name,
                              rt_thread_t thread,
                              const char* type,
                              rt_uint8_t priority)
{
    if (thread == RT_NULL) {
        rt_kprintf("%-12s | %-10s | %-8s | %-6s | %-10s | %-8s\n",
                   name, type, "N/A", "N/A", "N/A", "NON-EXIST");
        return;
    }

    rt_uint8_t state = thread->stat & RT_THREAD_STAT_MASK;
    size_t free_stack = get_thread_stack_free(thread);

    rt_kprintf("%-12s | %-10s | %-8d | %-6d | %-10d | %-8s\n",
               name,
               type,
               priority,
               thread->stack_size,
               free_stack,
               get_thread_state_str(state));
}

/* ===== 自定义命令：thread_dump ===== */
static void cmd_thread_dump(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    rt_kprintf("\n");
    rt_kprintf("========== RT-Thread Thread Dump ==========\n");
    rt_kprintf("%-12s | %-10s | %-8s | %-6s | %-10s | %-8s\n",
               "Thread Name", "Type", "Priority", "Stack", "Free Stack", "State");
    rt_kprintf("------------|------------|----------|--------|------------|----------\n");

    /* 打印 th_led1（动态线程） */
    print_thread_info("th_led1", th_led1, "Dynamic", 16);

    /* 打印 th_led2（静态线程） */
    print_thread_info("th_led2", (rt_thread_t)&th_led2_tcb, "Static", 16);

    /* 打印 th_monitor（动态线程） */
    rt_thread_t monitor_thread = rt_thread_find("th_monitor");
    if (monitor_thread != RT_NULL) {
        print_thread_info("th_monitor", monitor_thread, "Dynamic", 10);
    } else {
        print_thread_info("th_monitor", RT_NULL, "Dynamic", 10);
    }

    /* 打印当前运行线程 */
    rt_thread_t current = rt_thread_self();
    if (current != RT_NULL) {
        rt_kprintf("\n[Current Running Thread: %s]\n", current->name);
    }

    rt_kprintf("==============================================\n");
}

/* ===== 导出命令到 MSH ===== */
MSH_CMD_EXPORT(cmd_thread_dump, dump thread info);   /* 修复：去掉描述中的逗号 */
