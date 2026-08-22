#include <rtthread.h>

#define TICKET_TOTAL 20
static int g_ticket = TICKET_TOTAL;
static rt_mutex_t ticket_mutex;
static rt_sem_t done_sem;

void ticket_sell_entry(void *param)
{
    char *win_name = (char *)param;
    while (1)
    {
        rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

        if(g_ticket <= 0)
        {
            rt_mutex_release(ticket_mutex);
            break;
        }

        rt_thread_mdelay(1);
        g_ticket --;
        rt_kprintf("%s sold ticket %d\n", win_name, g_ticket + 1);

        rt_mutex_release(ticket_mutex);
        rt_thread_mdelay(2);
    }
    rt_sem_release(done_sem);
}

int ticket_demo(int argc, char *argv[])
{
    rt_thread_t t_a, t_b;
    char *name_a = "window A";
    char *name_b = "window B";

    g_ticket = TICKET_TOTAL;

    ticket_mutex = rt_mutex_create("tick_mtx", RT_IPC_FLAG_PRIO);
    done_sem = rt_sem_create("done",0,RT_IPC_FLAG_PRIO);
    if(ticket_mutex == RT_NULL || done_sem == RT_NULL)
    {
        rt_kprintf("create ipc fail\n");
        return -1;
    }

    t_a = rt_thread_create("winA", ticket_sell_entry, name_a, 1024, 10, 5);
    t_b = rt_thread_create("winB", ticket_sell_entry, name_b, 1024, 10, 5);

    if(t_a != RT_NULL && t_b != RT_NULL)
    {
        rt_thread_startup(t_a);
        rt_thread_startup(t_b);
    }

    rt_sem_take(done_sem, RT_WAITING_FOREVER);
    rt_sem_take(done_sem, RT_WAITING_FOREVER);

    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", g_ticket);

    return 0;
}
MSH_CMD_EXPORT(ticket_demo, multi window sell ticket with mutex);
