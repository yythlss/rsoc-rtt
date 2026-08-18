/*
 * Day2 多线程实验
 * 1. led1：动态线程，控制红灯
 * 2. led2：静态线程，控制蓝灯
 * 3. monitor：监控线程，检测按键并打印线程信息
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

/* 硬件引脚：LED 低电平点亮，KEY0 低电平按下。 */
#define PIN_LED_R               GET_PIN(F, 12)
#define PIN_LED_B               GET_PIN(F, 11)
#define PIN_KEY0                GET_PIN(C, 0)

/* led1 和 monitor 使用动态线程句柄。 */
static rt_thread_t th_led1;
static rt_thread_t th_monitor;

/* led2 使用静态 TCB 和静态栈。 */
static struct rt_thread th_led2;
ALIGN(RT_ALIGN_SIZE)
static rt_uint8_t th_led2_stack[1024];

/* 这是控制 led1 的二值信号量，初始值为 1。 */
static struct rt_semaphore led1_sem;
static rt_uint8_t led1_paused;

static const char *get_thread_state(rt_uint8_t state)
{
    switch (state & RT_THREAD_STAT_MASK)
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

/* Cortex-M 栈向低地址增长，未使用区域仍保持初始化字符 '#'。 */
static rt_uint32_t get_stack_free(rt_thread_t thread)
{
    rt_uint8_t *stack;
    rt_uint32_t free_size = 0;

    stack = (rt_uint8_t *)thread->stack_addr;
    while ((free_size < thread->stack_size) && (stack[free_size] == '#'))
    {
        free_size++;
    }

    return free_size;
}

static void print_thread_info(rt_thread_t thread)
{
    if (thread == RT_NULL)
    {
        return;
    }

    rt_kprintf("name=%s pri=%u state=%s stack=%u free=%u\n",
               thread->name,
               thread->current_priority,
               get_thread_state(thread->stat),
               thread->stack_size,
               get_stack_free(thread));
}

/* 动态线程：翻转红灯，打印计数和 tick。 */
static void led1_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t led_state = PIN_HIGH;

    RT_UNUSED(parameter);
    rt_thread_mdelay(1000);
    while (1)
    {
        /* 正常时取到后马上归还;暂停时 monitor会移植。 */
        if (rt_sem_take(&led1_sem, RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }
        rt_sem_release(&led1_sem);

        led_state = !led_state;
        rt_pin_write(PIN_LED_R, led_state);

        count++;
        rt_kprintf("[led1] count=%u tick=%u\n", count, rt_tick_get());
        rt_thread_mdelay(500);
    }
}

/* 静态线程：翻转蓝灯并打印计数。 */
static void led2_entry(void *parameter)
{
    rt_uint32_t count = 0;
    rt_uint8_t led_state = PIN_HIGH;

    RT_UNUSED(parameter);
    rt_thread_mdelay(1000);
    while (1)
    {
        led_state = !led_state;
        rt_pin_write(PIN_LED_B, led_state);

        count++;
        rt_kprintf("[led2] count=%u\n", count);
        rt_thread_mdelay(500);
    }
}

static void control_led1(void)
{
    rt_err_t result;

    if (!led1_paused)
    {
        /* 取走信号量但不归还，led1 下一轮就会阻塞。 */
        result = rt_sem_take(&led1_sem, RT_WAITING_FOREVER);
        if (result == RT_EOK)
        {
            led1_paused = 1;
            rt_pin_write(PIN_LED_R, PIN_HIGH);
            rt_kprintf("[monitor] suspend led1\n");
        }
        return;
    }

    /* 归还信号量，唤醒等待中的 led1。 */
    result = rt_sem_release(&led1_sem);
    if (result == RT_EOK)
    {
        led1_paused = 0;
        rt_kprintf("[monitor] resume led1\n");
    }
    else
    {
        rt_kprintf("[monitor] release led1 semaphore failed: %d\n", result);
    }
}

/* 监控线程：扫描按键，并且每 2 秒打印一次线程信息。 */
static void monitor_entry(void *parameter)
{
    rt_tick_t last_print_tick = rt_tick_get();

    RT_UNUSED(parameter);
    rt_thread_mdelay(1000);
    while (1)
    {
        rt_tick_t now;

        if (rt_pin_read(PIN_KEY0) == PIN_LOW)
        {
            /* 延时后仍为低电平，才认为按键真正按下。 */
            rt_thread_mdelay(20);
            if (rt_pin_read(PIN_KEY0) == PIN_LOW)
            {
                control_led1();

                /* 等待松手，避免一次按下触发多次。 */
                while (rt_pin_read(PIN_KEY0) == PIN_LOW)
                {
                    rt_thread_mdelay(10);
                }
            }
        }

        now = rt_tick_get();
        if ((rt_tick_t)(now - last_print_tick) >=
            rt_tick_from_millisecond(2000))
        {
            last_print_tick = now;
            rt_kprintf("[monitor] thread information:\n");
            print_thread_info(th_led1);
            print_thread_info(&th_led2);
        }

        rt_thread_mdelay(10);
    }
}

static int thread_study_init(void)
{
    rt_err_t result;

    /* 初始化 LED 和按键。 */
    rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_LED_R, PIN_HIGH);
    rt_pin_write(PIN_LED_B, PIN_HIGH);
    rt_pin_mode(PIN_KEY0, PIN_MODE_INPUT_PULLUP);

    /* 信号量初始值为 1，表示 led1 默认可以运行。 */
    result = rt_sem_init(&led1_sem, "ledsem", 1, RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_kprintf("init led1 semaphore failed\n");
        return result;
    }

    /* 1. 动态创建 led1。 */
    th_led1 = rt_thread_create("led1",
                                led1_entry,
                                RT_NULL,
                                1024,
                                16, 5);
    if (th_led1 == RT_NULL)
    {
        rt_kprintf("create led1 failed\n");
        return -RT_ERROR;
    }

    /* 2. 静态初始化 led2。 */
    result = rt_thread_init(&th_led2,
                            "led2",
                            led2_entry,
                            RT_NULL,
                            th_led2_stack,
                            sizeof(th_led2_stack),
                            16, 5);
    if (result != RT_EOK)
    {
        rt_kprintf("init led2 failed\n");
        return result;
    }

    /* 3. 动态创建 monitor。 */
    th_monitor = rt_thread_create("monitor",
                                    monitor_entry,
                                    RT_NULL,
                                    1024,
                                    10, 5);
    if (th_monitor == RT_NULL)
    {
        rt_kprintf("create monitor failed\n");
        return -RT_ERROR;
    }

    /* 创建完成后依次启动三个线程。 */
    if (rt_thread_startup(th_led1) != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (rt_thread_startup(&th_led2) != RT_EOK)
    {
        return -RT_ERROR;
    }
    if (rt_thread_startup(th_monitor) != RT_EOK)
    {
        return -RT_ERROR;
    }

    rt_kprintf("thread study started, key=PC0\n");
    return RT_EOK;
}
INIT_APP_EXPORT(thread_study_init);

static long thread_dump(void)
{
    print_thread_info(th_led1);
    print_thread_info(&th_led2);
    return 0;
}
MSH_CMD_EXPORT(thread_dump, dump led thread information);
