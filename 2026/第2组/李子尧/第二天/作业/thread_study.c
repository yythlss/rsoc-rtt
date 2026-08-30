/*
 * Day2: RT-Thread thread lifecycle practice on STM32F407 RT-Spark.
 *
 * Pin mapping is verified against the installed RT-Spark BSP examples:
 *   LED_B: PF11, LED_R: PF12, KEY0: PC0.
 * LEDs and KEY0 are active-low on that board.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define PIN_LED_B      GET_PIN(F, 11)
#define PIN_LED_R      GET_PIN(F, 12)
#define PIN_KEY0       GET_PIN(C, 0)

#define LED_ON          PIN_LOW
#define LED_OFF         PIN_HIGH

#define LED_STACK_SIZE  1024
#define MONITOR_PERIOD  20
#define REPORT_PERIOD   2000

static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;
static volatile rt_bool_t led1_suspend_requested = RT_FALSE;
static volatile rt_bool_t led1_manually_suspended = RT_FALSE;

static struct rt_thread th_led2;
static rt_uint8_t th_led2_stack[LED_STACK_SIZE] ALIGN(RT_ALIGN_SIZE);

static void led_toggle(rt_base_t pin)
{
    rt_pin_write(pin, rt_pin_read(pin) == LED_ON ? LED_OFF : LED_ON);
}

static rt_uint8_t thread_state(rt_thread_t thread)
{
    rt_base_t level;
    rt_uint8_t state;

    level = rt_hw_interrupt_disable();
    state = thread->stat & RT_THREAD_STAT_MASK;
    rt_hw_interrupt_enable(level);

    return state;
}

static const char *thread_state_name(rt_uint8_t state)
{
    switch (state)
    {
    case RT_THREAD_INIT:
        return "init";
    case RT_THREAD_READY:
        return "ready";
    case RT_THREAD_SUSPEND:
        return "suspend";
    case RT_THREAD_RUNNING:
        return "running";
    case RT_THREAD_CLOSE:
        return "close";
    default:
        return "unknown";
    }
}

/* RT-Thread initializes each stack byte to '#'. Cortex-M stacks grow down. */
static rt_size_t thread_stack_free(rt_thread_t thread)
{
    const rt_uint8_t *stack = (const rt_uint8_t *)thread->stack_addr;
    rt_size_t free_size = 0;

    while ((free_size < thread->stack_size) && (stack[free_size] == '#'))
    {
        free_size++;
    }

    return free_size;
}

static void print_thread_status(const char *tag, rt_thread_t thread)
{
    rt_uint8_t state = thread_state(thread);

    rt_kprintf("[%s] state=%s, priority=%d, stack_free=%d/%d bytes\n",
               tag,
               thread_state_name(state),
               thread->current_priority,
               thread_stack_free(thread),
               thread->stack_size);
}

static void th_led1_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);
    while (RT_TRUE)
    {
        /*
         * RT-Thread 4.1.1 requires rt_thread_suspend() to be called by the
         * target thread itself. The monitor therefore posts this request and
         * th_led1 performs the suspend at this safe point in its own context.
         */
        if (led1_suspend_requested)
        {
            led1_manually_suspended = RT_TRUE;
            rt_kprintf("[th_led1] manual suspend entered\n");
            rt_thread_suspend(rt_thread_self());
            rt_schedule();
            led1_manually_suspended = RT_FALSE;
            rt_kprintf("[th_led1] resumed by monitor\n");
        }

        led_toggle(PIN_LED_R);
        count++;
        rt_kprintf("[th_led1] tick=%u, count=%u, LED_R toggled\n",
                   (unsigned int)rt_tick_get(), (unsigned int)count);
        rt_thread_mdelay(500);
    }
}

static void th_led2_entry(void *parameter)
{
    rt_uint32_t count = 0;

    RT_UNUSED(parameter);
    while (RT_TRUE)
    {
        led_toggle(PIN_LED_B);
        count++;
        rt_kprintf("[th_led2] tick=%u, count=%u, LED_B toggled\n",
                   (unsigned int)rt_tick_get(), (unsigned int)count);
        rt_thread_mdelay(700);
    }
}

static void toggle_led1_suspend_state(void)
{
    rt_err_t result;

    if (led1_suspend_requested)
    {
        led1_suspend_requested = RT_FALSE;
        if (led1_manually_suspended)
        {
            result = rt_thread_resume(th_led1);
            rt_kprintf("[monitor] KEY0 pressed: resume th_led1, result=%d\n", result);
        }
        else
        {
            rt_kprintf("[monitor] KEY0 pressed: cancel pending suspend request\n");
        }
    }
    else
    {
        led1_suspend_requested = RT_TRUE;
        rt_kprintf("[monitor] KEY0 pressed: request th_led1 suspend\n");
    }

    print_thread_status("monitor/th_led1", th_led1);
    rt_kprintf("[monitor] th_led1 manual_suspend=%s\n",
               led1_manually_suspended ? "yes" : "no");
}

static void th_monitor_entry(void *parameter)
{
    rt_uint8_t last_sample = PIN_HIGH;
    rt_uint8_t stable_level = PIN_HIGH;
    rt_uint8_t same_count = 0;
    rt_uint32_t elapsed = 0;

    RT_UNUSED(parameter);
    print_thread_status("monitor/th_led1", th_led1);
    print_thread_status("monitor/th_led2", &th_led2);

    while (RT_TRUE)
    {
        rt_uint8_t sample = rt_pin_read(PIN_KEY0);

        if (sample == last_sample)
        {
            if (same_count < 3)
            {
                same_count++;
            }
        }
        else
        {
            last_sample = sample;
            same_count = 0;
        }

        /* 3 consecutive 20 ms samples are used as a simple debounce filter. */
        if ((same_count == 3) && (sample != stable_level))
        {
            stable_level = sample;
            if (stable_level == PIN_LOW)
            {
                toggle_led1_suspend_state();
            }
        }

        elapsed += MONITOR_PERIOD;
        if (elapsed >= REPORT_PERIOD)
        {
            elapsed = 0;
            print_thread_status("monitor/th_led1", th_led1);
            print_thread_status("monitor/th_led2", &th_led2);
        }

        rt_thread_mdelay(MONITOR_PERIOD);
    }
}

static void thread_dump(void)
{
    rt_kprintf("Day2 thread study status:\n");
    if (th_led1 != RT_NULL)
    {
        print_thread_status("th_led1", th_led1);
        rt_kprintf("[th_led1] manual_suspend=%s\n",
                   led1_manually_suspended ? "yes" : "no");
    }
    print_thread_status("th_led2", &th_led2);
    if (th_monitor != RT_NULL)
    {
        print_thread_status("th_monitor", th_monitor);
    }
}
MSH_CMD_EXPORT(thread_dump, show Day2 thread states and stack usage);

static int thread_study_init(void)
{
    rt_err_t result;

    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);
    rt_pin_write(PIN_LED_R, LED_OFF);
    rt_pin_write(PIN_LED_B, LED_OFF);

    th_led1 = rt_thread_create("th_led1", th_led1_entry, RT_NULL,
                               LED_STACK_SIZE, 16, 5);
    if (th_led1 == RT_NULL)
    {
        rt_kprintf("[thread_study] create th_led1 failed\n");
        return -RT_ERROR;
    }

    result = rt_thread_init(&th_led2, "th_led2", th_led2_entry, RT_NULL,
                            th_led2_stack, sizeof(th_led2_stack), 16, 5);
    if (result != RT_EOK)
    {
        rt_kprintf("[thread_study] init th_led2 failed: %d\n", result);
        return result;
    }

    th_monitor = rt_thread_create("th_monitor", th_monitor_entry, RT_NULL,
                                  LED_STACK_SIZE, 10, 5);
    if (th_monitor == RT_NULL)
    {
        rt_kprintf("[thread_study] create th_monitor failed\n");
        return -RT_ERROR;
    }

    rt_thread_startup(th_led1);
    rt_thread_startup(&th_led2);
    rt_thread_startup(th_monitor);

    rt_kprintf("[thread_study] initialized: KEY0 toggles th_led1 suspend/resume\n");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
