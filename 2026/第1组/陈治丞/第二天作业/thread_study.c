/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 * SPDX-License-Identifier: Apache-2.0
 *
 * Day2: RT-Thread thread creation, scheduling and thread control.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <rthw.h>
#include <board.h>

#define LED_R_PIN              GET_PIN(F, 12)
#define LED_B_PIN              GET_PIN(F, 11)
#define KEY_UP_PIN             GET_PIN(C, 5)

#define WORKER_STACK_SIZE      1024
#define WORKER_PRIORITY        16
#define WORKER_TIMESLICE       5
#define WORKER_PERIOD_MS       500

#define MONITOR_STACK_SIZE     1024
#define MONITOR_PRIORITY       10
#define MONITOR_TIMESLICE      5
#define MONITOR_PERIOD_MS      2000
#define KEY_DEBOUNCE_MS        50

static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;

ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t th_led2_stack[WORKER_STACK_SIZE];
static struct rt_thread th_led2_tcb;

static volatile rt_uint8_t key_press_count;
static volatile rt_tick_t key_last_tick;
static volatile rt_bool_t led1_suspend_request;
static rt_bool_t led1_is_suspended;

static void toggle_pin(rt_base_t pin)
{
    rt_pin_write(pin, rt_pin_read(pin) == PIN_LOW ? PIN_HIGH : PIN_LOW);
}

static const char *thread_state_name(rt_thread_t thread)
{
    switch (thread->stat & RT_THREAD_STAT_MASK)
    {
    case RT_THREAD_INIT:
        return "init";
    case RT_THREAD_READY:
        return "ready";
    case RT_THREAD_RUNNING:
        return "running";
    case RT_THREAD_SUSPEND:
        return "suspend";
    case RT_THREAD_CLOSE:
        return "close";
    default:
        return "unknown";
    }
}

/* RT-Thread fills a new stack with '#'. Untouched bytes are free stack. */
static rt_size_t thread_stack_free(rt_thread_t thread)
{
    const rt_uint8_t *cursor;
    const rt_uint8_t *end;

    if (thread == RT_NULL)
    {
        return 0;
    }

    cursor = (const rt_uint8_t *)thread->stack_addr;
    end = cursor + thread->stack_size;

    while (cursor < end && *cursor == '#')
    {
        cursor++;
    }

    return (rt_size_t)(cursor - (const rt_uint8_t *)thread->stack_addr);
}

static void print_thread_info(rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        rt_kprintf("thread not created\n");
        return;
    }

    rt_kprintf("%-10.*s state=%-7s prio=%2u free=%4u/%4u bytes\n",
               RT_NAME_MAX,
               thread->name,
               thread_state_name(thread),
               (unsigned int)thread->current_priority,
               (unsigned int)thread_stack_free(thread),
               (unsigned int)thread->stack_size);
}

static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        if (led1_suspend_request == RT_TRUE)
        {
            led1_suspend_request = RT_FALSE;
            led1_is_suspended = RT_TRUE;
            rt_kprintf("[key] th_led1 suspended\n");

            /* RT-Thread 4.x requires a thread to suspend itself. */
            if (rt_thread_suspend(rt_thread_self()) == RT_EOK)
            {
                rt_schedule();
            }
            continue;
        }

        toggle_pin(LED_R_PIN);
        count++;
        rt_kprintf("[th_led1] count=%lu tick=%lu\n",
                   (unsigned long)count,
                   (unsigned long)rt_tick_get());
        rt_thread_mdelay(WORKER_PERIOD_MS);
    }
}

static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);

    while (1)
    {
        toggle_pin(LED_B_PIN);
        count++;
        rt_kprintf("[th_led2] count=%lu\n", (unsigned long)count);
        rt_thread_mdelay(WORKER_PERIOD_MS);
    }
}

static void key_up_irq(void *args)
{
    rt_tick_t now = rt_tick_get();
    rt_tick_t debounce = rt_tick_from_millisecond(KEY_DEBOUNCE_MS);

    RT_UNUSED(args);

    if ((rt_tick_t)(now - key_last_tick) >= debounce)
    {
        if (key_press_count < 0xff)
        {
            key_press_count++;
        }
        key_last_tick = now;
    }
}

static rt_uint8_t take_key_events(void)
{
    rt_base_t level;
    rt_uint8_t count;

    level = rt_hw_interrupt_disable();
    count = key_press_count;
    key_press_count = 0;
    rt_hw_interrupt_enable(level);

    return count;
}

static void request_led1_toggle(void)
{
    rt_err_t result;

    if (led1_is_suspended == RT_FALSE)
    {
        if (led1_suspend_request == RT_FALSE)
        {
            led1_suspend_request = RT_TRUE;
            rt_kprintf("[key] suspend requested\n");
        }
        else
        {
            rt_kprintf("[key] suspend request is pending\n");
        }
    }
    else
    {
        result = rt_thread_resume(th_led1);
        if (result == RT_EOK)
        {
            led1_is_suspended = RT_FALSE;
            rt_kprintf("[key] th_led1 resumed\n");
        }
        else
        {
            rt_kprintf("[key] resume failed: %d\n", result);
        }
    }
}

static void th_monitor_entry(void *parameter)
{
    rt_uint8_t pending;

    RT_UNUSED(parameter);

    while (1)
    {
        pending = take_key_events();
        while (pending > 0)
        {
            request_led1_toggle();
            pending--;
        }

        rt_kprintf("\n[monitor] tick=%lu\n", (unsigned long)rt_tick_get());
        print_thread_info(th_led1);
        print_thread_info(&th_led2_tcb);

        rt_thread_mdelay(MONITOR_PERIOD_MS);
    }
}

static int thread_dump(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("\nname       state   priority stack-free\n");
    rt_kprintf("------------------------------------------\n");
    print_thread_info(th_led1);
    print_thread_info(&th_led2_tcb);
    print_thread_info(th_monitor);

    return RT_EOK;
}
MSH_CMD_EXPORT(thread_dump, show Day2 thread state and remaining stack);

static int thread_study_init(void)
{
    rt_err_t result;

    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, PIN_HIGH);
    rt_pin_write(LED_B_PIN, PIN_HIGH);

    rt_pin_mode(KEY_UP_PIN, PIN_MODE_INPUT_PULLUP);
    key_last_tick = rt_tick_get() - rt_tick_from_millisecond(KEY_DEBOUNCE_MS);
    result = rt_pin_attach_irq(KEY_UP_PIN, PIN_IRQ_MODE_FALLING,
                               key_up_irq, RT_NULL);
    if (result != RT_EOK)
    {
        rt_kprintf("attach KEY_UP irq failed: %d\n", result);
        return result;
    }

    result = rt_pin_irq_enable(KEY_UP_PIN, PIN_IRQ_ENABLE);
    if (result != RT_EOK)
    {
        rt_kprintf("enable KEY_UP irq failed: %d\n", result);
        return result;
    }

    th_led1 = rt_thread_create("th_led1", th_led1_entry, RT_NULL,
                               WORKER_STACK_SIZE, WORKER_PRIORITY,
                               WORKER_TIMESLICE);
    if (th_led1 == RT_NULL)
    {
        rt_kprintf("create th_led1 failed\n");
        return -RT_ENOMEM;
    }

    result = rt_thread_init(&th_led2_tcb, "th_led2", th_led2_entry, RT_NULL,
                            th_led2_stack, sizeof(th_led2_stack),
                            WORKER_PRIORITY, WORKER_TIMESLICE);
    if (result != RT_EOK)
    {
        rt_kprintf("init th_led2 failed: %d\n", result);
        return result;
    }

    th_monitor = rt_thread_create("th_monitor", th_monitor_entry, RT_NULL,
                                  MONITOR_STACK_SIZE, MONITOR_PRIORITY,
                                  MONITOR_TIMESLICE);
    if (th_monitor == RT_NULL)
    {
        rt_kprintf("create th_monitor failed\n");
        return -RT_ENOMEM;
    }

    result = rt_thread_startup(th_led1);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_thread_startup(&th_led2_tcb);
    if (result != RT_EOK)
    {
        return result;
    }

    result = rt_thread_startup(th_monitor);
    if (result != RT_EOK)
    {
        return result;
    }

    rt_kprintf("Day2 thread study initialized\n");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
