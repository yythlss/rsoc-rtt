/*
 * app_led_threads.c  ——  星火一号 (STM32F407ZGT6) 多线程实战示例
 * ----------------------------------------------------------------------------
 * 板载资源（已核对星火一号原理图/BSP）：
 *   LED_R（红）  = PF12   共阳极：低电平点亮 / 高电平熄灭
 *   LED_B（蓝）  = PF11   共阳极：低电平点亮 / 高电平熄灭
 *   KEY_LEFT     = PC0    按下为低电平（下降沿触发）
 *
 * 线程规划：
 *   - 工作线程1  th_led1   : 动态创建(rt_thread_create)，优先级16/时间片5/栈1024
 *   - 工作线程2  th_led2   : 静态初始化(rt_thread_init)，全局 TCB + 全局栈
 *   - 监控线程   th_monitor : 优先级10(更高)，周期 rt_thread_mdelay(2000)，
 *                            打印 th_led1/th_led2 的状态与剩余栈；按键挂起/恢复 th_led1
 *   - INIT_APP_EXPORT() 自动加载（不碰 main.c）
 *   - MSH_CMD_EXPORT() 导出命令 thread_dump
 *
 * 用法：把本文件放进 <星火一号工程>/applications/ 目录，编译下载即可。
 * 注意：星火一号模板 main.c 里默认有一段闪烁 LED 的 demo，请把它注释掉，
 *       否则会和本文件的线程同时操作 PF11/PF12，造成竞争。
 * ----------------------------------------------------------------------------
 */
#include <rtthread.h>
#include <board.h>
#include <drv_gpio.h>   /* 星火一号 BSP 提供 GET_PIN */

/* ============== 1. 星火一号 板级引脚 ============== */
#define LED_R_PIN     GET_PIN(F, 12)   /* 红色 LED */
#define LED_B_PIN     GET_PIN(F, 11)   /* 蓝色 LED */
#define KEY_PIN       GET_PIN(C, 0)    /* KEY_LEFT，按下为低电平 */
#define KEY_IRQ_MODE  PIN_IRQ_MODE_FALLING

/* ============== 2. 线程参数 ============== */
#define LED_PRIORITY    16
#define LED_TICK        5
#define LED_STACK_SIZE  1024

#define MON_PRIORITY    10
#define MON_TICK        5
#define MON_STACK_SIZE  1024

/* ============== 3. 静态线程 th_led2 的 TCB 与栈（全局） ============== */
static struct rt_thread th_led2;
static rt_uint8_t led2_stack[LED_STACK_SIZE];

/* ============== 4. 全局句柄、计数与按键去抖 ============== */
static rt_thread_t th_led1    = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;
static volatile rt_uint32_t led1_count = 0;
static volatile rt_uint32_t led2_count = 0;
static volatile rt_bool_t   led1_suspended = RT_FALSE;
static       rt_tick_t      last_key_tick = 0;   /* 按键软件去抖用 */

/* ============== 5. 状态 → 中文 工具函数 ============== */
static const char *stat2str(rt_uint8_t stat)
{
    switch (stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:     return "初始态";
    case RT_THREAD_READY:    return "就绪态";
    case RT_THREAD_RUNNING:  return "运行态";
    case RT_THREAD_SUSPEND:  return "挂起态";
    case RT_THREAD_CLOSE:    return "关闭态";
    default:                 return "未知态";
    }
}

/* ============== 6. 工作线程1：动态创建 th_led1 ============== */
/* 星火一号 LED 为共阳：PIN_LOW 点亮，PIN_HIGH 熄灭；翻转即亮灭交替 */
static void led1_entry(void *parameter)
{
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);

    while (1)
    {
        led1_count++;
        rt_pin_write(LED_R_PIN, (led1_count % 2) ? PIN_HIGH : PIN_LOW);
        rt_kprintf("th_led1 count=%u, tick=%u\n", led1_count, rt_tick_get());
        rt_thread_mdelay(500);
    }
}

/* ============== 7. 工作线程2：静态初始化 th_led2 ============== */
static void led2_entry(void *parameter)
{
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);

    while (1)
    {
        led2_count++;
        rt_pin_write(LED_B_PIN, (led2_count % 2) ? PIN_HIGH : PIN_LOW);
        rt_kprintf("th_led2 count=%u\n", led2_count);
        rt_thread_mdelay(500);
    }
}

/* ============== 8. 监控线程 th_monitor（优先级10，高于工作线程） ============== */
static void monitor_entry(void *parameter)
{
    while (1)
    {
        rt_thread_mdelay(2000);  /* 周期 2s */

        rt_kprintf("==== monitor @ tick=%u ====\n", rt_tick_get());

        if (th_led1 != RT_NULL)
            rt_kprintf("th_led1    stat=%s  free_stack=%lu\n",
                       stat2str(th_led1->stat),
                       (unsigned long)rt_thread_get_free_stack_size(th_led1));

        rt_kprintf("th_led2    stat=%s  free_stack=%lu\n",
                   stat2str(th_led2.stat),
                   (unsigned long)rt_thread_get_free_stack_size(&th_led2));

        rt_kprintf("th_led1_suspended=%s\n", led1_suspended ? "yes" : "no");
    }
}

/* ============== 9. 按键中断回调：单次挂起 / 再次恢复 th_led1 ============== */
static void key_callback(void *args)
{
    /* 中断上下文不能 sleep，用 tick 做 200ms 软件去抖 */
    rt_tick_t now = rt_tick_get();
    if (now - last_key_tick < rt_tick_from_millisecond(200))
        return;
    last_key_tick = now;

    if (!led1_suspended)
    {
        if (th_led1 != RT_NULL)
            rt_thread_suspend(th_led1);   /* 中断里挂起“另一个”线程是允许的 */
        led1_suspended = RT_TRUE;
        rt_kprintf("[key] th_led1 suspended\n");
    }
    else
    {
        if (th_led1 != RT_NULL)
            rt_thread_resume(th_led1);
        led1_suspended = RT_FALSE;
        rt_kprintf("[key] th_led1 resumed\n");
    }
}

/* ============== 10. MSH 命令 thread_dump ============== */
static void cmd_thread_dump(int argc, char **argv)
{
    rt_kprintf("---- thread_dump ----\n");
    if (th_led1 != RT_NULL)
        rt_kprintf("th_led1    stat=%s  free_stack=%lu  count=%u\n",
                   stat2str(th_led1->stat),
                   (unsigned long)rt_thread_get_free_stack_size(th_led1),
                   led1_count);
    rt_kprintf("th_led2    stat=%s  free_stack=%lu  count=%u\n",
               stat2str(th_led2.stat),
               (unsigned long)rt_thread_get_free_stack_size(&th_led2),
               led2_count);
    if (th_monitor != RT_NULL)
        rt_kprintf("th_monitor stat=%s  free_stack=%lu\n",
                   stat2str(th_monitor->stat),
                   (unsigned long)rt_thread_get_free_stack_size(th_monitor));
}
MSH_CMD_EXPORT(thread_dump, dump th_led1/th_led2/th_monitor thread info);

/* ============== 11. 自动初始化入口（app 级，禁止在 main.c 硬编码） ============== */
static int led_app_init(void)
{
    rt_err_t ret;

    /* 引脚初始化 */
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(KEY_PIN,   PIN_MODE_INPUT_PULLUP);   /* 上拉：按下为低 */

    /* ① 动态创建 th_led1 */
    th_led1 = rt_thread_create("th_led1", led1_entry, RT_NULL,
                               LED_STACK_SIZE, LED_PRIORITY, LED_TICK);
    if (th_led1 != RT_NULL)
        rt_thread_startup(th_led1);
    else
    {
        rt_kprintf("th_led1 create failed!\n");
        return -RT_ENOMEM;
    }

    /* ② 静态初始化 th_led2 */
    ret = rt_thread_init(&th_led2, "th_led2", led2_entry, RT_NULL,
                         &led2_stack[0], sizeof(led2_stack),
                         LED_PRIORITY, LED_TICK);
    if (ret == RT_EOK)
        rt_thread_startup(&th_led2);
    else
    {
        rt_kprintf("th_led2 init failed!\n");
        return (int)ret;
    }

    /* ③ 动态创建 th_monitor（优先级10 < 16，更高） */
    th_monitor = rt_thread_create("th_monitor", monitor_entry, RT_NULL,
                                  MON_STACK_SIZE, MON_PRIORITY, MON_TICK);
    if (th_monitor != RT_NULL)
        rt_thread_startup(th_monitor);
    else
    {
        rt_kprintf("th_monitor create failed!\n");
        return -RT_ENOMEM;
    }

    /* ④ 按键中断：KEY_LEFT 单次挂起 / 再次恢复 */
    rt_pin_attach_irq(KEY_PIN, KEY_IRQ_MODE, key_callback, RT_NULL);
    rt_pin_irq_enable(KEY_PIN, PIN_IRQ_ENABLE);

    rt_kprintf("led_app_init ok (INIT_APP_EXPORT) on 星火一号\n");
    return RT_EOK;
}
INIT_APP_EXPORT(led_app_init);
