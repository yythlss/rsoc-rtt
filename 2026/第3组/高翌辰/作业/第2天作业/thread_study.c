/*
 * 第二天作业：多线程 LED 与按键控制
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <drv_common.h>

#define LED_R_PIN               GET_PIN(F, 12)
#define LED_B_PIN               GET_PIN(F, 11)
#define BTN_LEFT_PIN            GET_PIN(C, 0)

#define WORK_THREAD_PRIORITY    16
#define MONITOR_THREAD_PRIORITY 10
#define THREAD_STACK_SIZE       1024
#define THREAD_TIMESLICE        5
#define LED_PERIOD_MS           500
#define MONITOR_PERIOD_MS       2000
#define KEY_DEBOUNCE_MS         50

static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;
static volatile rt_bool_t led1_suspended = RT_FALSE;
static volatile rt_bool_t led1_suspend_requested = RT_FALSE;
static volatile rt_uint32_t led1_toggle_count;
static rt_tick_t last_key_tick;

#ifdef rt_align
rt_align(RT_ALIGN_SIZE)
#else
ALIGN(RT_ALIGN_SIZE)
#endif
static rt_uint8_t th_led2_stack[THREAD_STACK_SIZE];
static struct rt_thread th_led2;

static void led_toggle(rt_base_t pin)
{
    rt_pin_write(pin, !rt_pin_read(pin));
}

static rt_size_t thread_stack_space(rt_thread_t thread)
{
    rt_uint8_t *stack = (rt_uint8_t *)thread->stack_addr;
    rt_size_t space = 0;

    while ((space < thread->stack_size) && (stack[space] == '#'))
    {
        space++;
    }

    return space;
}

static const char *thread_state(rt_thread_t thread)
{
    switch (thread->stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:    return "init";
    case RT_THREAD_READY:   return "ready";
    case RT_THREAD_SUSPEND: return "suspend";
    case RT_THREAD_RUNNING: return "running";
    case RT_THREAD_CLOSE:   return "close";
    default:                return "unknown";
    }
}

static void thread_dump(void)
{
    if (th_led1 != RT_NULL)
    {
        rt_kprintf("th_led1: %-7s stack: %u/%u bytes\n",
                   thread_state(th_led1),
                   (unsigned int)thread_stack_space(th_led1),
                   (unsigned int)th_led1->stack_size);
    }
    else
    {
        rt_kprintf("th_led1: not created\n");
    }

    if (th_led2.stack_addr != RT_NULL)
    {
        rt_kprintf("th_led2: %-7s stack: %u/%u bytes\n",
                   thread_state(&th_led2),
                   (unsigned int)thread_stack_space(&th_led2),
                   (unsigned int)th_led2.stack_size);
    }
    else
    {
        rt_kprintf("th_led2: not initialized\n");
    }
}
MSH_CMD_EXPORT(thread_dump, show LED thread state and free stack);

static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        if (led1_suspend_requested)
        {
            led1_suspend_requested = RT_FALSE;
            led1_suspended = RT_TRUE;
            rt_thread_suspend(rt_thread_self());
            rt_schedule();
        }

        led_toggle(LED_R_PIN);
        rt_kprintf("th_led1 count: %u, tick: %u\n",
                   (unsigned int)count++, (unsigned int)rt_tick_get());
        rt_thread_mdelay(LED_PERIOD_MS);
    }
}

static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    while (1)
    {
        led_toggle(LED_B_PIN);
        rt_kprintf("th_led2 count: %u\n", (unsigned int)count++);
        rt_thread_mdelay(LED_PERIOD_MS);
    }
}

static void button_callback(void *parameter)
{
    rt_tick_t now = rt_tick_get();

    if ((last_key_tick == 0) ||
        ((rt_tick_t)(now - last_key_tick) >= rt_tick_from_millisecond(KEY_DEBOUNCE_MS)))
    {
        last_key_tick = now;
        led1_toggle_count++;
    }
}

static void monitor_entry(void *parameter)
{
    while (1)
    {
        rt_base_t level;
        rt_uint32_t toggle_count;

        rt_thread_mdelay(MONITOR_PERIOD_MS);

        level = rt_hw_interrupt_disable();
        toggle_count = led1_toggle_count;
        led1_toggle_count = 0;
        rt_hw_interrupt_enable(level);

        if (toggle_count & 1U)
        {
            if (led1_suspended)
            {
                if (rt_thread_resume(th_led1) == RT_EOK)
                {
                    led1_suspended = RT_FALSE;
                    rt_kprintf("BTN_LEFT: th_led1 resumed\n");
                }
            }
            else if (led1_suspend_requested)
            {
                led1_suspend_requested = RT_FALSE;
                rt_kprintf("BTN_LEFT: th_led1 suspend cancelled\n");
            }
            else
            {
                led1_suspend_requested = RT_TRUE;
                rt_kprintf("BTN_LEFT: th_led1 suspend requested\n");
            }
        }

        thread_dump();
    }
}

static int thread_study(void)
{
    rt_err_t result;

    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, PIN_LOW);
    rt_pin_write(LED_B_PIN, PIN_LOW);

    rt_pin_mode(BTN_LEFT_PIN, PIN_MODE_INPUT_PULLUP);
    result = rt_pin_attach_irq(BTN_LEFT_PIN, PIN_IRQ_MODE_FALLING, button_callback, RT_NULL);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_pin_irq_enable(BTN_LEFT_PIN, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        return result;
    }

    th_led1 = rt_thread_create("th_led1", led1_entry, RT_NULL,
                               THREAD_STACK_SIZE, WORK_THREAD_PRIORITY, THREAD_TIMESLICE);
    if (th_led1 == RT_NULL)
    {
        return -RT_ENOMEM;
    }

    th_monitor = rt_thread_create("th_monitor", monitor_entry, RT_NULL,
                                  THREAD_STACK_SIZE, MONITOR_THREAD_PRIORITY, THREAD_TIMESLICE);
    if (th_monitor == RT_NULL)
    {
        rt_thread_delete(th_led1);
        th_led1 = RT_NULL;
        return -RT_ENOMEM;
    }

    result = rt_thread_init(&th_led2, "th_led2", led2_entry, RT_NULL,
                            th_led2_stack, sizeof(th_led2_stack),
                            WORK_THREAD_PRIORITY, THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_thread_delete(th_monitor);
        th_monitor = RT_NULL;
        rt_thread_delete(th_led1);
        th_led1 = RT_NULL;
        return result;
    }

    rt_thread_startup(th_led1);
    rt_thread_startup(&th_led2);
    rt_thread_startup(th_monitor);

    return RT_EOK;
}
INIT_APP_EXPORT(thread_study);
