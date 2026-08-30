/*
 * RT-Thread thread management experiment for STM32F407 RT-Spark.
 *
 * LED_R: PF12, LED_B: PF11, KEY_UP: PC5 (active low)
 */

#include <board.h>
#include <drv_gpio.h>
#include <rtdevice.h>
#include <rtthread.h>

#define THREAD_PRIORITY_LED       16
#define THREAD_PRIORITY_MONITOR   10
#define THREAD_TIMESLICE           5
#define THREAD_STACK_SIZE       1024

#define LED_R_PIN    GET_PIN(F, 12)
#define LED_B_PIN    GET_PIN(F, 11)
#define KEY_UP_PIN   GET_PIN(C, 5)

static rt_thread_t led1_thread = RT_NULL;
static rt_thread_t monitor_thread = RT_NULL;

static struct rt_thread led2_thread;
rt_align(RT_ALIGN_SIZE)
static rt_uint8_t led2_stack[THREAD_STACK_SIZE];

static volatile rt_uint8_t key_event = 0;
static volatile rt_uint8_t led1_suspend_request = 0;
static volatile rt_uint8_t led1_manually_suspended = 0;

static const char *thread_state_name(rt_uint8_t state)
{
    switch (state & RT_THREAD_STAT_MASK)
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

static rt_size_t thread_stack_unused(rt_thread_t thread)
{
    const rt_uint8_t *stack;
    rt_size_t unused = 0;

    if ((thread == RT_NULL) || (thread->stack_addr == RT_NULL))
    {
        return 0;
    }

    stack = (const rt_uint8_t *)thread->stack_addr;
    while ((unused < thread->stack_size) && (stack[unused] == '#'))
    {
        unused++;
    }

    return unused;
}

static void print_thread_info(rt_thread_t thread)
{
    rt_uint8_t state;
    rt_uint8_t priority;
    rt_tick_t remaining_tick;
    rt_base_t critical_level;

    if (thread == RT_NULL)
    {
        return;
    }

    critical_level = rt_enter_critical();
    state = RT_SCHED_CTX(thread).stat;
    priority = RT_SCHED_PRIV(thread).current_priority;
    remaining_tick = RT_SCHED_PRIV(thread).remaining_tick;
    rt_exit_critical_safe(critical_level);

    rt_kprintf("%-10s state=%-7s prio=%2d slice=%u stack_free=%u/%u\n",
               thread->parent.name,
               thread_state_name(state),
               priority,
               (unsigned int)remaining_tick,
               (unsigned int)thread_stack_unused(thread),
               (unsigned int)thread->stack_size);
}

static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        rt_pin_write(LED_R_PIN, !rt_pin_read(LED_R_PIN));
        count++;
        rt_kprintf("[th_led1] count=%u tick=%u\n",
                   (unsigned int)count, (unsigned int)rt_tick_get());

        if (led1_suspend_request)
        {
            led1_suspend_request = 0;
            led1_manually_suspended = 1;
            rt_kprintf("[th_led1] suspend itself\n");
            rt_thread_suspend(rt_thread_self());
            rt_schedule();
            led1_manually_suspended = 0;
            rt_kprintf("[th_led1] resumed\n");
        }

        rt_thread_mdelay(500);
    }
}

static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        rt_pin_write(LED_B_PIN, !rt_pin_read(LED_B_PIN));
        count++;
        rt_kprintf("[th_led2] count=%u tick=%u\n",
                   (unsigned int)count, (unsigned int)rt_tick_get());
        rt_thread_mdelay(1000);
    }
}

static void key_up_irq(void *args)
{
    static rt_tick_t last_tick = 0;
    rt_tick_t now = rt_tick_get();

    RT_UNUSED(args);

    if ((last_tick == 0) || ((now - last_tick) >= rt_tick_from_millisecond(100)))
    {
        key_event = 1;
        last_tick = now;
    }
}

static void monitor_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        if (key_event)
        {
            key_event = 0;

            if (led1_manually_suspended)
            {
                if (rt_thread_resume(led1_thread) == RT_EOK)
                {
                    rt_kprintf("[monitor] KEY_UP: resume th_led1\n");
                }
            }
            else if (!led1_suspend_request)
            {
                led1_suspend_request = 1;
                rt_kprintf("[monitor] KEY_UP: request th_led1 suspend\n");
            }
        }

        rt_kprintf("\n[monitor] tick=%u manual_suspend=%d\n",
                   (unsigned int)rt_tick_get(), led1_manually_suspended);
        print_thread_info(led1_thread);
        print_thread_info(&led2_thread);
        print_thread_info(monitor_thread);
        rt_kprintf("\n");

        rt_thread_mdelay(2000);
    }
}

static void thread_dump(void)
{
    rt_kprintf("\n[thread_dump] tick=%u\n", (unsigned int)rt_tick_get());
    print_thread_info(led1_thread);
    print_thread_info(&led2_thread);
    print_thread_info(monitor_thread);
}
MSH_CMD_EXPORT(thread_dump, show thread study status and unused stack);

static int thread_study_init(void)
{
    rt_err_t result;

    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, PIN_LOW);
    rt_pin_write(LED_B_PIN, PIN_LOW);

    rt_pin_mode(KEY_UP_PIN, PIN_MODE_INPUT_PULLUP);
    result = rt_pin_attach_irq(KEY_UP_PIN, PIN_IRQ_MODE_FALLING,
                               key_up_irq, RT_NULL);
    if (result != RT_EOK)
    {
        rt_kprintf("[thread_study] attach KEY_UP irq failed: %d\n", result);
        return result;
    }

    result = rt_pin_irq_enable(KEY_UP_PIN, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        rt_kprintf("[thread_study] enable KEY_UP irq failed: %d\n", result);
        return result;
    }

    led1_thread = rt_thread_create("th_led1",
                                   led1_entry,
                                   RT_NULL,
                                   THREAD_STACK_SIZE,
                                   THREAD_PRIORITY_LED,
                                   THREAD_TIMESLICE);
    if (led1_thread == RT_NULL)
    {
        rt_kprintf("[thread_study] create th_led1 failed\n");
        return -RT_ENOMEM;
    }

    result = rt_thread_init(&led2_thread,
                            "th_led2",
                            led2_entry,
                            RT_NULL,
                            led2_stack,
                            sizeof(led2_stack),
                            THREAD_PRIORITY_LED,
                            THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_kprintf("[thread_study] init th_led2 failed: %d\n", result);
        rt_thread_delete(led1_thread);
        led1_thread = RT_NULL;
        return result;
    }

    monitor_thread = rt_thread_create("th_monitor",
                                      monitor_entry,
                                      RT_NULL,
                                      THREAD_STACK_SIZE,
                                      THREAD_PRIORITY_MONITOR,
                                      THREAD_TIMESLICE);
    if (monitor_thread == RT_NULL)
    {
        rt_kprintf("[thread_study] create th_monitor failed\n");
        rt_thread_detach(&led2_thread);
        rt_thread_delete(led1_thread);
        led1_thread = RT_NULL;
        return -RT_ENOMEM;
    }

    rt_thread_startup(led1_thread);
    rt_thread_startup(&led2_thread);
    rt_thread_startup(monitor_thread);

    rt_kprintf("[thread_study] initialized by INIT_APP_EXPORT\n");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
