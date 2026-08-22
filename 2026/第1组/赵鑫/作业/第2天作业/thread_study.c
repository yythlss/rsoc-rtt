/**
 * 【DAY2】多线程实战编程（STM32F407 星火号，RT-Thread 4.1.1）
 *
 * 功能：
 *  1. 动态线程 th_led1 ：优先级 16，时间片 5 ticks，栈 1024B
 *                        循环翻转 LED_R(PF12)，每次打印计数与 rt_tick_get()
 *  2. 静态线程 th_led2 ：优先级 16，时间片 5 ticks，静态全局 TCB 与栈
 *                        循环翻转 LED_B(PF11)，每次打印计数
 *  3. 监控线程 monitor ：优先级 10（高于工作线程），每 2s 报告 th_led1/th_led2
 *                        运行状态与剩余栈空间；轮询按键 KEY0(PC0)，单击挂起
 *                        th_led1，再次单击恢复
 *  4. 使用 INIT_APP_EXPORT() 自动加载本模块（禁止在 main.c 中硬编码调用）
 *  5. 使用 MSH_CMD_EXPORT() 导出 thread_dump 查看命令
 *
 * 设计说明：
 *  - 本 SDK 定制内核（RT-Thread 4.1.1）中 rt_thread_suspend() 带
 *    RT_ASSERT(thread == rt_thread_self())，只允许挂起当前线程，且不会主动
 *    触发调度。因此"按键挂起 th_led1"采用【请求标志 + 线程自挂】方式实现：
 *    monitor 置位 led1_suspend_req，th_led1 在循环顶部自检后执行
 *    rt_thread_suspend(rt_thread_self()) + rt_schedule()；恢复仍使用
 *    rt_thread_resume()（该接口无"只能恢复自己"的限制）。
 *  - th_led1 采用 rt_tick_get() 忙等待代替 rt_thread_mdelay()：线程保持
 *    READY 态，既保证挂起操作确定性（mdelay 期间线程已是 SUSPEND 态，
 *    无法被再次挂起），又便于观察同优先级时间片轮转；th_led2 使用
 *    rt_thread_mdelay() 阻塞延时，两种方式对比演示。
 *  - 剩余栈空间算法与 FinSH ps 命令一致：stack_size + stack_addr - sp。
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define PIN_LED_R       GET_PIN(F, 12)   /* PF12: 板载红色 LED（低电平点亮） */
#define PIN_LED_B       GET_PIN(F, 11)   /* PF11: 板载蓝色 LED（低电平点亮） */
#define PIN_KEY0        GET_PIN(C, 0)    /* PC0 : 板载按键 KEY0（上拉，按下为低） */

#define LED_ON          0
#define LED_OFF         1

#define TH_LED1_PRIO    16
#define TH_LED2_PRIO    16
#define TH_MON_PRIO     10
#define TH_TIMESLICE    5
#define TH_STACK_SIZE   1024

#define LED_SWITCH_TICKS    (RT_TICK_PER_SECOND / 2)
#define REPORT_INTERVAL     (RT_TICK_PER_SECOND * 2)
/*--------------------------- 全局对象 ---------------------------*/
static rt_thread_t th_led1    = RT_NULL;    /* 动态线程句柄 */
static rt_thread_t th_monitor = RT_NULL;    /* 动态线程句柄 */


static rt_uint8_t th_led2_stack[TH_STACK_SIZE];
static struct rt_thread th_led2_tcb;

/* 挂起请求标志：monitor 置位，th_led1 自检后执行自我挂起 */
static volatile rt_uint8_t led1_suspend_req = 0;

static struct rt_mutex print_mutex;

static const char *thread_stat_str(rt_uint8_t stat)
{
    switch (stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:    return "INIT";
    case RT_THREAD_READY:   return "READY";
    case RT_THREAD_RUNNING: return "RUNNING";
    case RT_THREAD_SUSPEND: return "SUSPEND";
    case RT_THREAD_CLOSE:   return "CLOSE";
    default:                return "UNKNOWN";
    }
}

/* 打印线程运行状态与剩余栈空间（算法与 FinSH ps 命令一致） */
static void report_thread(rt_thread_t thread)
{
    rt_uint32_t remain = thread->stack_size +
                         (rt_uint32_t)thread->stack_addr -
                         (rt_uint32_t)thread->sp;

    rt_kprintf("[%-8s] stat=%-8s remain_stack=%4d/%d\n",
               thread->name,
               thread_stat_str(thread->stat),
               remain, thread->stack_size);
}

/*--------------------------- 线程 1：动态创建，翻转 LED_R ---------------------------*/
static void led1_entry(void *param)
{
    rt_uint32_t count = 0;
    rt_tick_t start;

    while (1)
    {
        /* 收到挂起请求：自我挂起（本内核仅允许挂起当前线程） */
        if (led1_suspend_req)
        {
            led1_suspend_req = 0;
            rt_kprintf("[th_led1] suspend self, tick=%d\n", rt_tick_get());
            rt_thread_suspend(rt_thread_self());
            rt_schedule();              /* rt_thread_suspend 不会主动触发调度 */
            rt_kprintf("[th_led1] resumed, continue, tick=%d\n", rt_tick_get());
            continue;
        }

        /* 翻转 LED_R（低电平点亮） */
        rt_pin_write(PIN_LED_R, (count & 0x1) ? LED_ON : LED_OFF);
        rt_mutex_take(&print_mutex, RT_WAITING_FOREVER);
        rt_kprintf("[th_led1] count=%d, tick=%d\n", count, rt_tick_get());
        rt_mutex_release(&print_mutex);
        count++;

        /* 忙等待 200ms：线程保持 READY 态，便于挂起与时间片轮转观察 */
        start = rt_tick_get();
        while (rt_tick_get() - start < LED_SWITCH_TICKS)
        {
        }
    }
}

/*--------------------------- 线程 2：静态初始化，翻转 LED_B ---------------------------*/
static void led2_entry(void *param)
{
    rt_uint32_t count = 0;

    while (1)
    {
        rt_pin_write(PIN_LED_B, (count & 0x1) ? LED_ON : LED_OFF);
        rt_mutex_take(&print_mutex, RT_WAITING_FOREVER);
        rt_kprintf("[th_led2] count=%d, tick=%d\n", count, rt_tick_get());
        rt_mutex_release(&print_mutex);
        count++;

        /* 阻塞延时：延时期间线程进入 SUSPEND 态，超时后由定时器唤醒 */
        rt_thread_mdelay(LED_SWITCH_TICKS);
    }
}

/*--------------------------- 监控线程：周期报告 + 按键控制 ---------------------------*/
static void monitor_entry(void *param)
{
    rt_tick_t last_report = 0;
    rt_base_t level;

    while (1)
    {
        if (th_led1 == RT_NULL)         /* 创建失败的保护 */
        {
            rt_thread_mdelay(1000);
            continue;
        }

        /* ---- 按键检测：10ms 轮询 + 50ms 去抖，单击切换挂起/恢复 ---- */
        if (rt_pin_read(PIN_KEY0) == PIN_LOW)
        {
            rt_thread_mdelay(50);
            if (rt_pin_read(PIN_KEY0) == PIN_LOW)
            {
                /* 等待按键释放，避免一次按压多次触发 */
                while (rt_pin_read(PIN_KEY0) == PIN_LOW)
                {
                    rt_thread_mdelay(10);
                }

                level = rt_hw_interrupt_disable();
                if ((th_led1->stat & RT_THREAD_STAT_MASK) == RT_THREAD_SUSPEND)
                {
                    /* 已挂起 → 恢复 */
                    led1_suspend_req = 0;
                    rt_hw_interrupt_enable(level);
                    if (rt_thread_resume(th_led1) == RT_EOK)
                        rt_kprintf("[monitor] KEY0: resume th_led1\n");
                    else
                        rt_kprintf("[monitor] KEY0: resume th_led1 failed\n");
                }
                else if (!led1_suspend_req)
                {
                    /* 运行中 → 发出挂起请求 */
                    led1_suspend_req = 1;
                    rt_hw_interrupt_enable(level);
                    rt_kprintf("[monitor] KEY0: suspend request -> th_led1\n");
                }
                else
                {
                    rt_hw_interrupt_enable(level);
                    rt_kprintf("[monitor] KEY0: th_led1 is about to suspend, ignore\n");
                }
            }
        }

        /* ---- 周期报告：每 2s 打印 th_led1/th_led2 状态与剩余栈 ---- */
        if (rt_tick_get() - last_report >= REPORT_INTERVAL)
        {
            last_report = rt_tick_get();
            rt_kprintf("---------- thread status @tick=%d ----------\n", last_report);
            report_thread(th_led1);
            report_thread(&th_led2_tcb);
            report_thread(th_monitor);
        }

        rt_thread_mdelay(10);
    }
}


static int thread_dump(void)
{
    struct rt_object_information *info;
    struct rt_list_node *node;
    struct rt_object *object;
    struct rt_thread *thread;
    rt_uint32_t remain;

    info = rt_object_get_information(RT_Object_Class_Thread);
    if (info == RT_NULL)
    {
        return -RT_ERROR;
    }

    rt_kprintf("%-8s %-10s %-5s %-8s %-8s\n",
               "name", "stat", "prio", "remain", "stack");
    for (node = info->object_list.next;
         node != &(info->object_list);
         node = node->next)
    {
        object = rt_list_entry(node, struct rt_object, list);
        thread = (struct rt_thread *)object;
        remain = thread->stack_size +
                 (rt_uint32_t)thread->stack_addr -
                 (rt_uint32_t)thread->sp;
        rt_kprintf("%-8s %-10s %-5d %-8d %-8d\n",
                   thread->name,
                   thread_stat_str(thread->stat),
                   thread->current_priority,
                   remain, thread->stack_size);
    }
    return RT_EOK;
}
MSH_CMD_EXPORT(thread_dump, dump all threads status and remaining stack);

static int thread_study_init(void)
{
    /* 引脚初始化 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, LED_OFF);
    rt_pin_write(PIN_LED_B, LED_OFF);
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);

    rt_mutex_init(&print_mutex, "print", RT_IPC_FLAG_FIFO);

    /* 1. 动态创建工作线程 th_led1 */
    th_led1 = rt_thread_create("th_led1", led1_entry, RT_NULL,
                               TH_STACK_SIZE, TH_LED1_PRIO, TH_TIMESLICE);
    if (th_led1 != RT_NULL)
    {
        rt_thread_startup(th_led1);
    }

    /* 2. 静态初始化工作线程 th_led2 */
    rt_thread_init(&th_led2_tcb, "th_led2", led2_entry, RT_NULL,
                   &th_led2_stack[0], sizeof(th_led2_stack),
                   TH_LED2_PRIO, TH_TIMESLICE);
    rt_thread_startup(&th_led2_tcb);

    /* 3. 动态创建监控线程 monitor */
    th_monitor = rt_thread_create("monitor", monitor_entry, RT_NULL,
                                  TH_STACK_SIZE, TH_MON_PRIO, TH_TIMESLICE);
    if (th_monitor != RT_NULL)
    {
        rt_thread_startup(th_monitor);
    }

    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
