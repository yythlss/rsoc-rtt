/* Day3 exercise: mutex-protected ticket transactions. */
#include <rtthread.h>

#define TICKET_WINDOW_COUNT 2
#define TICKET_TOTAL         20
#define TICKET_STACK_SIZE    1024

struct ticket_window
{
    char name;
};

static struct rt_mutex ticket_lock;
static struct rt_thread ticket_threads[TICKET_WINDOW_COUNT];
static ALIGN(RT_ALIGN_SIZE) rt_uint8_t ticket_stacks[TICKET_WINDOW_COUNT][TICKET_STACK_SIZE];
static struct ticket_window ticket_windows[TICKET_WINDOW_COUNT] = {{'A'}, {'B'}};
static rt_uint8_t ticket_sold[TICKET_TOTAL + 1];
static rt_int32_t ticket_count;
static rt_uint32_t ticket_finished;
static rt_uint32_t ticket_expected;
static rt_bool_t ticket_abort;
static rt_bool_t ticket_running;

static void ticket_cleanup(void)
{
    rt_mutex_detach(&ticket_lock);
    ticket_running = RT_FALSE;
}

static void ticket_detach_threads(rt_uint32_t first, rt_uint32_t count)
{
    rt_uint32_t index;

    for (index = first; index < count; index++)
        rt_thread_detach(&ticket_threads[index]);
}

static void ticket_finish(void)
{
    rt_bool_t last = RT_FALSE;

    rt_mutex_take(&ticket_lock, RT_WAITING_FOREVER);
    ticket_finished++;
    if (ticket_finished == ticket_expected)
        last = RT_TRUE;
    rt_mutex_release(&ticket_lock);

    if (last)
    {
        ticket_cleanup();
        rt_kprintf("ticket_demo finished: remaining=%d\n", ticket_count);
    }
}

static void ticket_window_entry(void *parameter)
{
    struct ticket_window *window = (struct ticket_window *)parameter;
    rt_int32_t ticket_no;

    while (!ticket_abort)
    {
        rt_mutex_take(&ticket_lock, RT_WAITING_FOREVER);
        if (ticket_abort)
        {
            rt_mutex_release(&ticket_lock);
            break;
        }

        if (ticket_count <= 0)
        {
            rt_mutex_release(&ticket_lock);
            break;
        }

        /* Check, allocate, record, and decrement form one transaction. */
        ticket_no = ticket_count;
        ticket_sold[ticket_no]++;
        ticket_count--;
        rt_kprintf("window %c sold ticket %d\n", window->name, ticket_no);
        rt_mutex_release(&ticket_lock);
        rt_thread_mdelay(30);
    }

    ticket_finish();
}

int ticket_demo(void)
{
    rt_err_t result;
    rt_uint32_t index;

    if (ticket_running)
    {
        rt_kprintf("ticket_demo is already running\n");
        return -RT_EBUSY;
    }

    result = rt_mutex_init(&ticket_lock, "ticketlk", RT_IPC_FLAG_FIFO);
    if (result != RT_EOK)
    {
        rt_kprintf("ticket mutex init failed: %d\n", result);
        return result;
    }

    rt_memset(ticket_sold, 0, sizeof(ticket_sold));
    ticket_count = TICKET_TOTAL;
    ticket_finished = 0;
    ticket_expected = TICKET_WINDOW_COUNT;
    ticket_abort = RT_FALSE;
    ticket_running = RT_TRUE;

    for (index = 0; index < TICKET_WINDOW_COUNT; index++)
    {
        result = rt_thread_init(&ticket_threads[index], "window",
                                ticket_window_entry, &ticket_windows[index],
                                ticket_stacks[index], TICKET_STACK_SIZE,
                                19, 5);
        if (result != RT_EOK)
        {
            ticket_detach_threads(0, index);
            ticket_cleanup();
            rt_kprintf("window %c thread init failed: %d\n",
                       ticket_windows[index].name, result);
            return result;
        }
    }

    for (index = 0; index < TICKET_WINDOW_COUNT; index++)
    {
        result = rt_thread_startup(&ticket_threads[index]);
        if (result != RT_EOK)
        {
            rt_bool_t cleanup_now;

            rt_mutex_take(&ticket_lock, RT_WAITING_FOREVER);
            ticket_abort = RT_TRUE;
            ticket_expected = index;
            cleanup_now = (ticket_finished == ticket_expected);
            rt_mutex_release(&ticket_lock);
            ticket_detach_threads(index, TICKET_WINDOW_COUNT);
            if (cleanup_now)
                ticket_cleanup();
            rt_kprintf("window %c thread startup failed: %d\n",
                       ticket_windows[index].name, result);
            return result;
        }
    }

    rt_kprintf("ticket_demo started: total=%d, windows=%u\n",
               TICKET_TOTAL, TICKET_WINDOW_COUNT);
    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, mutex ticket demo);
