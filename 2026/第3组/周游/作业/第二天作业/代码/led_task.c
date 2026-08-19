#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define LED_R_PIN   GET_PIN(F, 12)
#define LED_B_PIN   GET_PIN(F, 11)


rt_thread_t th_led1 = RT_NULL;
struct rt_thread th_led2_tcb;
ALIGN(RT_ALIGN_SIZE)
static char th_led2_stack[1024];

static void th_led1_entry(void *parameter)
{
    rt_uint32_t cnt = 0;
    rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
    while(1)
    {
        static rt_base_t s = PIN_LOW;
        s = (s == PIN_LOW) ? PIN_HIGH : PIN_LOW;
        rt_pin_write(LED_R_PIN, s);
        cnt++;
        rt_kprintf("th_led1 cnt: %d , tick: %d\n", cnt, rt_tick_get());
        rt_thread_mdelay(500);
    }
}

static void th_led2_entry(void *parameter)
{
    rt_uint32_t cnt = 0;
    rt_pin_mode(LED_B_PIN, PIN_MODE_OUTPUT);
    while(1)
    {
        static rt_base_t s = PIN_LOW;
        s = (s == PIN_LOW) ? PIN_HIGH : PIN_LOW;
        rt_pin_write(LED_B_PIN, s);
        cnt++;
        rt_kprintf("th_led2 cnt: %d\n", cnt);
        rt_thread_mdelay(500);
    }
}

static int led_task_init(void)
{
    th_led1 = rt_thread_create("th_led1", th_led1_entry, RT_NULL,
                               1024, 16, 5);
    if (th_led1 != RT_NULL) rt_thread_startup(th_led1);

    if (rt_thread_init(&th_led2_tcb, "th_led2", th_led2_entry, RT_NULL,
                       th_led2_stack, sizeof(th_led2_stack),
                       16, 5) == RT_EOK)
        rt_thread_startup(&th_led2_tcb);

    return 0;
}
INIT_APP_EXPORT(led_task_init);
