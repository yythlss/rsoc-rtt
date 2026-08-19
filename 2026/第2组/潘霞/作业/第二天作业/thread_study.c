#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define LED_R_PIN       GET_PIN(F, 12)
#define LED_B_PIN       GET_PIN(F, 11)

/* Spark-1: LEFT key = PC0, pressed = low level */
#define KEY_LEFT_PIN    GET_PIN(C, 0)

#define LED_ON          PIN_LOW
#define LED_OFF         PIN_HIGH

static rt_thread_t th_led1 = RT_NULL;
static rt_thread_t th_monitor = RT_NULL;

static struct rt_thread th_led2;
static rt_uint8_t th_led2_stack[1024];

static struct rt_semaphore led1_sem;
static struct rt_semaphore key_sem;

static volatile rt_bool_t led1_pause_requested = RT_FALSE;
static volatile rt_bool_t led1_waiting = RT_FALSE;

static rt_tick_t key_last_tick = 0;

static const char *thread_state_name(rt_uint8_t stat)
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

static rt_uint32_t get_stack_free(rt_thread_t thread)
{
    rt_uint32_t i = 0;
    rt_uint8_t *stack;

    if (thread == RT_NULL || thread->stack_addr == RT_NULL || thread->stack_size == 0)
    {
        return 0;
    }

    stack = (rt_uint8_t *)thread->stack_addr;

    while (i < thread->stack_size && stack[i] == '#')
    {
        i++;
    }

    return i;
}

static void key_irq_cb(void *args)
{
    RT_UNUSED(args);

    rt_sem_release(&key_sem);
}

static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t led_state = LED_OFF;

    RT_UNUSED(parameter);

    while (1)
    {
        if (led1_pause_requested)
        {
            rt_pin_write(LED_R_PIN, LED_OFF);
            led1_waiting = RT_TRUE;

         //   rt_kprintf("[th_led1] wait semaphore, LED_R paused\r\n");

            rt_sem_take(&led1_sem, RT_WAITING_FOREVER);

            led1_waiting = RT_FALSE;
            led1_pause_requested = RT_FALSE;

         //   rt_kprintf("[th_led1] semaphore released, LED_R resume\r\n");
        }

        led_state = (led_state == LED_OFF) ? LED_ON : LED_OFF;
        rt_pin_write(LED_R_PIN, led_state);

        count++;
    //    rt_kprintf("[th_led1] count=%u tick=%lu\r\n",
    //               count,
    //               (unsigned long)rt_tick_get());

        rt_thread_mdelay(500);
    }
}

static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t led_state = LED_OFF;

    RT_UNUSED(parameter);

    while (1)
    {
        led_state = (led_state == LED_OFF) ? LED_ON : LED_OFF;
        rt_pin_write(LED_B_PIN, led_state);

        count++;
   //     rt_kprintf("[th_led2] count=%u\r\n", count);

        rt_thread_mdelay(500);
    }
}

static void thread_dump_one(const char *name, rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        rt_kprintf("[%s] NULL\r\n", name);
        return;
    }

    rt_kprintf("[%s] stat=%s(0x%02x) stack_free=%u stack_size=%u\r\n",
               name,
               thread_state_name(thread->stat),
               thread->stat & RT_THREAD_STAT_MASK,
               get_stack_free(thread),
               thread->stack_size);
}

static void handle_key_event(void)
{
    rt_tick_t now = rt_tick_get();

    if ((now - key_last_tick) < rt_tick_from_millisecond(200))
    {
        return;
    }
    key_last_tick = now;

    if (!led1_pause_requested && !led1_waiting)
    {
        led1_pause_requested = RT_TRUE;
        rt_kprintf("[key] LEFT pressed, request pause th_led1\r\n");
    }
    else if (led1_waiting)
    {
        rt_sem_release(&led1_sem);
        rt_kprintf("[key] LEFT pressed, release semaphore for th_led1\r\n");
    }
    else
    {
        rt_kprintf("[key] LEFT pressed, th_led1 is preparing to pause\r\n");
    }
}

static void monitor_entry(void *parameter)
{
    RT_UNUSED(parameter);

    while (1)
    {
        if (rt_sem_take(&key_sem, rt_tick_from_millisecond(2000)) == RT_EOK)
        {
            handle_key_event();
        }

        thread_dump_one("th_led1", th_led1);
        thread_dump_one("th_led2", (rt_thread_t)&th_led2);
        thread_dump_one("th_monitor", th_monitor);
    }
}

static int thread_dump(int argc, char **argv)
{
    RT_UNUSED(argc);
    RT_UNUSED(argv);

    rt_kprintf("===== thread dump =====\r\n");
    thread_dump_one("th_led1", th_led1);
    thread_dump_one("th_led2", (rt_thread_t)&th_led2);
    thread_dump_one("th_monitor", th_monitor);
    rt_kprintf("=======================\r\n");

    return 0;
}
MSH_CMD_EXPORT(thread_dump, show thread status and stack space);

static int thread_study_init(void)
{
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(LED_R_PIN, LED_OFF);
    rt_pin_write(LED_B_PIN, LED_OFF);

    rt_sem_init(&led1_sem, "led1sem", 0, RT_IPC_FLAG_FIFO);
    rt_sem_init(&key_sem, "keysem", 0, RT_IPC_FLAG_FIFO);

    rt_pin_mode(KEY_LEFT_PIN, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(KEY_LEFT_PIN, PIN_IRQ_MODE_FALLING, key_irq_cb, RT_NULL);
    rt_pin_irq_enable(KEY_LEFT_PIN, PIN_IRQ_ENABLE);

    th_led1 = rt_thread_create("th_led1",
                               led1_entry,
                               RT_NULL,
                               1024,
                               16,
                               5);
    if (th_led1 == RT_NULL)
    {
        rt_kprintf("th_led1 create failed\r\n");
        return -RT_ERROR;
    }

    rt_thread_init(&th_led2,
                   "th_led2",
                   led2_entry,
                   RT_NULL,
                   th_led2_stack,
                   sizeof(th_led2_stack),
                   16,
                   5);

    th_monitor = rt_thread_create("th_monitor",
                                  monitor_entry,
                                  RT_NULL,
                                  1024,
                                  10,
                                  5);
    if (th_monitor == RT_NULL)
    {
        rt_kprintf("th_monitor create failed\r\n");
        return -RT_ERROR;
    }

    rt_thread_startup(th_led1);
    rt_thread_startup(&th_led2);
    rt_thread_startup(th_monitor);

    rt_kprintf("thread study init done, press LEFT key to pause/resume th_led1\r\n");

    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);
