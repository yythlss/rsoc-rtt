/*
 * 文件名：thread_study.c
 * 说明：RT-Thread 多线程学习示例（星火1号 stm32f407-rtspark）
 *       - th_led1：动态创建，翻转红灯 LED_R(PF12)，打印计数与 rt_tick_get()
 *       - th_led2：静态初始化，翻转蓝灯 LED_B(PF11)，打印计数
 *       - th_monitor：动态创建，每 2 秒打印线程状态与剩余栈；按键 KEY0(PC0)
 *                     第一次按下请求挂起 th_led1，第二次按下恢复 th_led1
 *       - 通过 INIT_APP_EXPORT 自动加载（不在 main.c 手动调用）
 *       - 通过 MSH_CMD_EXPORT 导出 thread_dump 命令
 *
 * 注意：本板 LED 为「低电平点亮」（active-low），写 PIN_LOW 亮、PIN_HIGH 灭。
 *
 * 关于挂起/恢复（重点，踩坑点）：
 *   1) 不能直接在外面 rt_thread_suspend 一个正在 rt_thread_mdelay 休眠中的线程，
 *      因为它的 mdelay 定时器会到点自动把它唤醒，导致挂起无效、恢复也失败。
 *      正确做法：monitor 只发“请求挂起”信号，由 th_led1 在安全点（不在 mdelay 中）
 *      自行 rt_thread_suspend(rt_thread_self())；恢复时再由 monitor 调 rt_thread_resume。
 *   2) 不要用 th_led1->stat 去判断是否已挂起——stat 的取值随 RT-Thread 版本而变
 *      （本版挂起后 stat=10，而 RT_THREAD_SUSPEND 常量为 2，直接比较会永远不成立，
 *      导致第二次按键恢复不了）。正确做法：用本模块自己的标志位 led1_suspended
 *      跟踪状态，与 RT-Thread 版本解耦，100% 可靠。
 *   3) 两个线程并发 rt_kprintf 会出现串台乱码，这里用 print_mtx 互斥锁串行化打印。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* 引脚定义（与 board.h / main.c 例程一致） */
#define LED_R_PIN  GET_PIN(F, 12)   /* 红灯 */
#define LED_B_PIN  GET_PIN(F, 11)   /* 蓝灯 */
#define KEY0_PIN   GET_PIN(C, 0)    /* 用户按键，按下为低电平 */

/* 低电平输出封装（本板低电平点亮） */
#define LED_ON(pin)  rt_pin_write((pin), PIN_LOW)
#define LED_OFF(pin) rt_pin_write((pin), PIN_HIGH)

/* 线程句柄与栈 */
static struct rt_thread th_led2;                       /* 静态线程 TCB */
static char th_led2_stack[1024];                        /* 静态线程栈 */
static rt_thread_t th_led1 = RT_NULL;                  /* 动态线程句柄 */
static rt_thread_t th_monitor = RT_NULL;               /* 动态线程句柄 */

/* 打印互斥锁：串行化各线程的 rt_kprintf，避免双线程抢串口出现串台乱码 */
static struct rt_mutex print_mtx;

/* 计数与按键状态 */
static int led1_count = 0;
static int led2_count = 0;
static int key_press_cnt = 0;
static int led1_need_suspend = 0;   /* monitor 置 1，请求 th_led1 自行挂起 */
static int led1_suspended = 0;      /* th_led1 自行挂起后置 1，恢复后清 0；
                                       monitor 用它判断是否已挂起，与 RT-Thread 版本无关 */

/* 串行化打印：取锁 -> 打印 -> 放锁 */
#define P_KPRINTF(...)                                       \
    do {                                                     \
        rt_mutex_take(&print_mtx, RT_WAITING_FOREVER);       \
        rt_kprintf(__VA_ARGS__);                             \
        rt_mutex_release(&print_mtx);                        \
    } while (0)

/*
 * 计算线程剩余栈空间。
 * RT-Thread 在线程创建时会把整段栈填成 '#'，运行中被用掉的部分会被覆盖，
 * 仍保持 '#' 的部分即为未使用的空闲栈。Cortex-M 栈向低地址增长，
 * 空闲部分在栈底，从 stack_addr 向上数连续 '#' 的字节数即为剩余栈。
 */
static rt_uint32_t my_stack_residue(struct rt_thread *thread)
{
    rt_uint8_t *ptr = (rt_uint8_t *)thread->stack_addr;
    rt_uint32_t i;

    if (thread == RT_NULL)
        return 0;

    for (i = 0; i < thread->stack_size; i++)
    {
        if (ptr[i] != (rt_uint8_t)'#')
            break;
    }
    return i;
}

/* 线程1：动态创建，翻转红灯，打印计数与系统 tick */
static void led1_entry(void *parameter)
{
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);

    while (1)
    {
        /* 收到挂起请求：在“安全点”自行挂起（此时不在 mdelay 中，挂起可靠） */
        if (led1_need_suspend)
        {
            led1_need_suspend = 0;
            led1_suspended = 1;     /* 先置位，再挂起；monitor 据此判断已挂起 */
            P_KPRINTF("th_led1 self-suspend requested.\n");
            rt_thread_suspend(rt_thread_self());   /* 由 monitor 调 rt_thread_resume 唤醒 */
            /* 被恢复后从这里继续 */
            led1_suspended = 0;     /* 已恢复，清位 */
            P_KPRINTF("th_led1 resumed.\n");
        }

        LED_ON(LED_R_PIN);
        P_KPRINTF("th_led1 [red ON ] count: %d, tick: %d\n",
                  led1_count, rt_tick_get());
        led1_count++;
        rt_thread_mdelay(500);

        LED_OFF(LED_R_PIN);
        rt_thread_mdelay(500);
    }
}

/* 线程2：静态初始化，翻转蓝灯，打印计数（与 th_led1 反相，便于观察交替） */
static void led2_entry(void *parameter)
{
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);

    while (1)
    {
        LED_OFF(LED_B_PIN);            /* 先保持蓝灯灭，与红灯反相 */
        P_KPRINTF("th_led2 [blue OFF] count: %d\n", led2_count);
        led2_count++;
        rt_thread_mdelay(500);

        LED_ON(LED_B_PIN);
        rt_thread_mdelay(500);
    }
}

/* 线程3（监视器）：频繁轮询按键；每 2 秒打印一次状态 */
static void monitor_entry(void *parameter)
{
    rt_tick_t last_print = 0;

    rt_pin_mode(KEY0_PIN, PIN_MODE_INPUT_PULLUP);

    while (1)
    {
        /* 检测按键：下降沿触发一次动作（消抖 + 等待松开，避免一次按下被连续触发） */
        if (rt_pin_read(KEY0_PIN) == PIN_LOW)
        {
            rt_thread_mdelay(30);  /* 简单消抖 */
            if (rt_pin_read(KEY0_PIN) == PIN_LOW)
            {
                key_press_cnt++;
                if (key_press_cnt % 2 == 1)
                {
                    /* 奇数次：请求挂起 th_led1（由它自己在安全点挂起） */
                    if (!led1_suspended)
                    {
                        led1_need_suspend = 1;
                        P_KPRINTF("[monitor] request suspend th_led1 (red will stop)\n");
                    }
                }
                else
                {
                    /* 偶数次：恢复 th_led1——用本模块标志位判断，不依赖 stat 魔数 */
                    if (led1_suspended)
                    {
                        rt_thread_resume(th_led1);
                        P_KPRINTF("[monitor] resume th_led1 (red restart)\n");
                    }
                    else
                    {
                        P_KPRINTF("[monitor] th_led1 not suspended, skip resume\n");
                    }
                }
                /* 等待按键松开 */
                while (rt_pin_read(KEY0_PIN) == PIN_LOW)
                    rt_thread_mdelay(20);
            }
        }

        /* 每 2 秒打印一次线程状态与剩余栈（stat 仅作展示，参与判断的是 led1_suspended） */
        if (rt_tick_get() - last_print >= 2000)
        {
            last_print = rt_tick_get();
            P_KPRINTF("[monitor] led1 stat=%d remain=%d | led2 stat=%d remain=%d | key=%d | suspended=%d\n",
                      th_led1 ? th_led1->stat : -1,  my_stack_residue(th_led1),
                      th_led2.stat,                 my_stack_residue(&th_led2),
                      rt_pin_read(KEY0_PIN), led1_suspended);
        }

        rt_thread_mdelay(50);   /* 50ms 轮询一次按键，保证响应及时 */
    }
}

/* MSH 命令：thread_dump —— 打印三个线程的状态、优先级、剩余栈 */
static void thread_dump(void)
{
    P_KPRINTF("=== thread dump ===\n");
    P_KPRINTF("th_led1    stat=%d prio=%d remain=%d suspended=%d\n",
               th_led1 ? th_led1->stat : -1,
               th_led1 ? th_led1->current_priority : -1,
               my_stack_residue(th_led1), led1_suspended);
    P_KPRINTF("th_led2    stat=%d prio=%d remain=%d\n",
               th_led2.stat, th_led2.current_priority, my_stack_residue(&th_led2));
    P_KPRINTF("th_monitor stat=%d prio=%d remain=%d\n",
               th_monitor ? th_monitor->stat : -1,
               th_monitor ? th_monitor->current_priority : -1,
               my_stack_residue(th_monitor));
}
MSH_CMD_EXPORT(thread_dump, dump thread status and stack residue);

/* 自动初始化入口（INIT_APP_EXPORT，系统启动时自动调用，无需在 main 里手动调用） */
static int thread_study_init(void)
{
    rt_err_t ret;

    /* 打印互斥锁（需在打印前初始化好） */
    rt_mutex_init(&print_mtx, "print_mtx", RT_IPC_FLAG_FIFO);

    /* 静态线程 th_led2：优先级 16，时间片 5 tick */
    ret = rt_thread_init(&th_led2,
                         "th_led2",
                         led2_entry,
                         RT_NULL,
                         th_led2_stack,
                         sizeof(th_led2_stack),
                         16,
                         5);
    if (ret != RT_EOK)
    {
        P_KPRINTF("th_led2 init failed!\n");
        return -RT_ERROR;
    }
    rt_thread_startup(&th_led2);

    /* 动态线程 th_led1：优先级 16，时间片 5 tick，栈 1024 字节 */
    th_led1 = rt_thread_create("th_led1",
                               led1_entry,
                               RT_NULL,
                               1024,
                               16,
                               5);
    if (th_led1 == RT_NULL)
    {
        P_KPRINTF("th_led1 create failed!\n");
        return -RT_ERROR;
    }
    rt_thread_startup(th_led1);

    /* 动态线程 th_monitor：优先级 10（高于上面两个），栈 2048，时间片 20 */
    th_monitor = rt_thread_create("th_monitor",
                                  monitor_entry,
                                  RT_NULL,
                                  2048,
                                  10,
                                  20);
    if (th_monitor == RT_NULL)
    {
        P_KPRINTF("th_monitor create failed!\n");
        return -RT_ERROR;
    }
    rt_thread_startup(th_monitor);

    P_KPRINTF("thread_study module loaded.\n");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
