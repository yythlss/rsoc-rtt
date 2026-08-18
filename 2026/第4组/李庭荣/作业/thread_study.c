/*
 * Day2 RT-Thread thread study.
 *
 * Board mapping from the Spark-1 schematic:
 *   GPIO_LED_R -> PF12
 *   GPIO_LED_B -> PF11
 *   KEY0       -> PC0, active low
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <drv_common.h>

#define DBG_TAG "thread_study"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define DAY2_LED_R_PIN        GET_PIN(F, 12)
#define DAY2_LED_B_PIN        GET_PIN(F, 11)
#define DAY2_KEY_PIN          GET_PIN(C, 0)
#define DAY2_KEY_ACTIVE       PIN_LOW

#define LED_THREAD_STACK_SIZE     1024
#define LED_THREAD_PRIORITY       16
#define LED_THREAD_TIMESLICE      5
#define MONITOR_THREAD_PRIORITY   10
#define MONITOR_THREAD_STACK_SIZE 1024
#define MONITOR_THREAD_TIMESLICE  5
#define KEY_SCAN_PERIOD_MS         20
#define MONITOR_PERIOD_TICKS       (2 * RT_TICK_PER_SECOND)

static rt_thread_t led1_thread;
static rt_thread_t monitor_thread;
static struct rt_thread led2_thread;
static ALIGN(RT_ALIGN_SIZE) rt_uint8_t led2_stack[LED_THREAD_STACK_SIZE];
static struct rt_semaphore led1_resume_sem;
static volatile rt_bool_t led1_paused;

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

static rt_uint32_t thread_stack_remaining(rt_thread_t thread)
{
    rt_uint8_t *base = (rt_uint8_t *)thread->stack_addr;
    rt_uint8_t *ptr = base;
    rt_uint8_t *end = base + thread->stack_size;

    while ((ptr < end) && (*ptr == '#'))
    {
        ptr++;
    }

    return (rt_uint32_t)(ptr - base);
}

static const char *led1_control_state(void)
{
    return led1_paused ? "manual_suspend" : "periodic_running";
}

static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_err_t result;

    (void)parameter;

    while (1)
    {
        while (led1_paused)
        {
            result = rt_sem_take(&led1_resume_sem, RT_WAITING_FOREVER);
            if (result != RT_EOK)
            {
                LOG_E("wait led1 resume semaphore failed: %d", result);
            }
        }

        rt_pin_write(DAY2_LED_R_PIN, !rt_pin_read(DAY2_LED_R_PIN));
        LOG_I("th_led1 count=%u tick=%u", count++, rt_tick_get());
        rt_thread_mdelay(500);
    }
}

static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    (void)parameter;

    while (1)
    {
        rt_pin_write(DAY2_LED_B_PIN, !rt_pin_read(DAY2_LED_B_PIN));
        LOG_I("th_led2 count=%u tick=%u", count++, rt_tick_get());
        rt_thread_mdelay(700);
    }
}

static void print_thread_info(const char *label, rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        rt_kprintf("%s: unavailable\n", label);
        return;
    }

    rt_kprintf("%s name=%s state=%s priority=%u remaining_stack=%u/%u\n",
               label,
               thread->name,
               thread_state_name(thread),
               thread->current_priority,
               thread_stack_remaining(thread),
               thread->stack_size);
}

static void thread_dump(void)
{
    print_thread_info("led1", led1_thread);
    print_thread_info("led2", &led2_thread);
    print_thread_info("monitor", monitor_thread);
    rt_kprintf("led1 control=%s\n", led1_control_state());
}
MSH_CMD_EXPORT(thread_dump, show Day2 thread status);

static void monitor_entry(void *parameter)
{
    rt_tick_t last_report = rt_tick_get();
    rt_bool_t key_armed = RT_TRUE;

    (void)parameter;

    while (1)
    {
        rt_tick_t now = rt_tick_get();

        if ((now - last_report) >= MONITOR_PERIOD_TICKS)
        {
            last_report = now;
            print_thread_info("led1", led1_thread);
            print_thread_info("led2", &led2_thread);
        }

        if (rt_pin_read(DAY2_KEY_PIN) == DAY2_KEY_ACTIVE)
        {
            if (key_armed)
            {
                rt_thread_mdelay(KEY_SCAN_PERIOD_MS);
                if (rt_pin_read(DAY2_KEY_PIN) == DAY2_KEY_ACTIVE)
                {
                    rt_err_t result;

                    if (led1_paused)
                    {
                        result = rt_sem_release(&led1_resume_sem);
                        if (result == RT_EOK)
                        {
                            led1_paused = RT_FALSE;
                            rt_kprintf("led1 control=%s\n", led1_control_state());
                        }
                    }
                    else
                    {
                        /* 清理可能残留的唤醒令牌，避免旧令牌误放行。 */
                        while (rt_sem_trytake(&led1_resume_sem) == RT_EOK)
                        {
                        }

                        led1_paused = RT_TRUE;
                        result = RT_EOK;
                        rt_kprintf("led1 control=%s\n", led1_control_state());
                    }

                    if (result != RT_EOK)
                    {
                        LOG_E("th_led1 control failed: %d", result);
                    }
                    key_armed = RT_FALSE;
                }
            }
        }
        else
        {
            key_armed = RT_TRUE;
        }

        rt_thread_mdelay(KEY_SCAN_PERIOD_MS);
    }
}

static int thread_study_init(void)
{
    rt_err_t result;

    rt_pin_mode(DAY2_LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(DAY2_LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(DAY2_LED_R_PIN, PIN_HIGH);
    rt_pin_write(DAY2_LED_B_PIN, PIN_HIGH);
    rt_pin_mode(DAY2_KEY_PIN, PIN_MODE_INPUT_PULLUP);

    result = rt_sem_init(&led1_resume_sem, "led1sem", 0, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        LOG_E("init led1 semaphore failed: %d", result);
        return result;
    }

    led1_thread = rt_thread_create("th_led1", led1_entry, RT_NULL,
                                   LED_THREAD_STACK_SIZE,
                                   LED_THREAD_PRIORITY,
                                   LED_THREAD_TIMESLICE);
    if (led1_thread == RT_NULL)
    {
        LOG_E("create th_led1 failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(led1_thread);

    result = rt_thread_init(&led2_thread, "th_led2", led2_entry, RT_NULL,
                            led2_stack, sizeof(led2_stack),
                            LED_THREAD_PRIORITY, LED_THREAD_TIMESLICE);
    if (result != RT_EOK)
    {
        LOG_E("init th_led2 failed: %d", result);
        return result;
    }
    rt_thread_startup(&led2_thread);

    monitor_thread = rt_thread_create("th_monitor", monitor_entry, RT_NULL,
                                      MONITOR_THREAD_STACK_SIZE,
                                      MONITOR_THREAD_PRIORITY,
                                      MONITOR_THREAD_TIMESLICE);
    if (monitor_thread == RT_NULL)
    {
        LOG_E("create th_monitor failed");
        return -RT_ENOMEM;
    }
    rt_thread_startup(monitor_thread);

    LOG_I("Day2 thread study initialized");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
