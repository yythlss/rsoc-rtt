#include <rtthread.h>
#include <finsh.h>

#define TICKET_TOTAL 20
#define TICKET_WINDOW_COUNT 2

struct ticket_context
{
    rt_mutex_t lock;
    rt_int32_t remaining;
    rt_uint32_t sold;
    rt_uint32_t finished;
    rt_bool_t safe_mode;
    rt_bool_t running;
    rt_thread_t threads[TICKET_WINDOW_COUNT];
};

static struct ticket_context ticket;

static void ticket_worker(void *parameter)
{
    char window = (char)(rt_ubase_t)parameter;

    while (1)
    {
        rt_int32_t ticket_no = 0;

        if (ticket.safe_mode)
        {
            rt_mutex_take(ticket.lock, RT_WAITING_FOREVER);
            if (ticket.remaining > 0)
            {
                /* Check, decrement, and ticket assignment form one operation. */
                ticket_no = ticket.remaining;
                ticket.remaining--;
                ticket.sold++;
            }
            rt_mutex_release(ticket.lock);
        }
        else
        {
            /* Intentionally unsafe: both windows can read the same value. */
            ticket_no = ticket.remaining;
            if (ticket_no > 0)
            {
                rt_thread_mdelay(10);
                ticket.remaining = ticket_no - 1;
                ticket.sold++;
            }
        }

        if (ticket_no == 0)
            break;

        rt_kprintf("window %c sold ticket %ld\n", window, (long)ticket_no);
        rt_thread_mdelay(80);
    }

    rt_mutex_take(ticket.lock, RT_WAITING_FOREVER);
    ticket.finished++;
    if (ticket.finished == TICKET_WINDOW_COUNT)
    {
        rt_kprintf("all tickets sold\n");
        rt_kprintf("remaining tickets: %ld, sold: %u\n",
                   (long)ticket.remaining, ticket.sold);
        rt_mutex_release(ticket.lock);
        rt_mutex_delete(ticket.lock);
        ticket.running = RT_FALSE;
    }
    else
    {
        rt_mutex_release(ticket.lock);
    }

    rt_thread_delete(rt_thread_self());
}

static int ticket_demo(int argc, char **argv)
{
    rt_uint32_t i;

    if (ticket.running)
    {
        rt_kprintf("ticket_demo is already running\n");
        return -1;
    }

    ticket.lock = rt_mutex_create("ticket_lock", RT_IPC_FLAG_FIFO);
    if (ticket.lock == RT_NULL)
    {
        rt_kprintf("ticket_demo: mutex create failed\n");
        return -1;
    }
    ticket.remaining = TICKET_TOTAL;
    ticket.sold = 0;
    ticket.finished = 0;
    ticket.safe_mode = !((argc > 1) && (argv[1][0] == 'u'));
    ticket.running = RT_TRUE;

    for (i = 0; i < TICKET_WINDOW_COUNT; i++)
    {
        ticket.threads[i] = rt_thread_create(i == 0 ? "window_a" : "window_b",
                                             ticket_worker,
                                             (void *)(rt_ubase_t)('A' + i),
                                             768, 16, 10);
        if (ticket.threads[i] == RT_NULL)
        {
            rt_uint32_t j;
            ticket.running = RT_FALSE;
            for (j = 0; j < i; j++)
                rt_thread_delete(ticket.threads[j]);
            rt_mutex_delete(ticket.lock);
            rt_kprintf("ticket_demo: window %u create failed\n", i);
            return -1;
        }
    }

    for (i = 0; i < TICKET_WINDOW_COUNT; i++)
        rt_thread_startup(ticket.threads[i]);
    rt_kprintf("ticket_demo started in %s mode with %d tickets\n",
               ticket.safe_mode ? "safe" : "unsafe", TICKET_TOTAL);
    return 0;
}
MSH_CMD_EXPORT(ticket_demo, ticket demo safe or unsafe);
