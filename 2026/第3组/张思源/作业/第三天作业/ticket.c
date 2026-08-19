#include <rtthread.h>

static rt_mutex_t ticket_mutex = RT_NULL;

#define TICKET_TOTAL 20
static rt_uint32_t ticket_count = 20;
static rt_uint8_t ticket_flag = 0;


static void ticket_entry(void *parameter)
{
    rt_uint32_t window = (rt_uint32_t)parameter;
    char name;
    if (window == 0){
        name = 'A';
    }
    else{
        name = 'B';
    }

    while(1)
    {
        rt_err_t temp = rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);
        if(temp == RT_EOK){
            if (ticket_count > 0){
                rt_kprintf("window %c sold ticket %d\n", name,ticket_count);
                ticket_count--;
                rt_mutex_release(ticket_mutex);
                rt_thread_mdelay(500);

                ticket_flag++;
                if(ticket_flag == TICKET_TOTAL){
                    rt_mutex_delete(ticket_mutex);
                    ticket_mutex = RT_NULL;
                    ticket_count = 20;
                    ticket_flag = 0;
                    break;
                }
            }
            else {
                rt_mutex_release(ticket_mutex);
                break;
            }
        }
        else {
            rt_kprintf("ticket_mutex take failed\n");
            break;
        }
    }

}

static int ticket_demo(void)
{
    if (ticket_mutex != RT_NULL)
    {
        rt_kprintf("ticket demo already running\n");
        return -1;
    }
    else {
        ticket_mutex = rt_mutex_create("ticket", RT_IPC_FLAG_PRIO );
        if(ticket_mutex == RT_NULL){
            rt_kprintf("ticket_mutex create failed");
            return -1;
        }
        else {
            //创建售票口A线程
            rt_thread_t winA_thread = rt_thread_create("winA", ticket_entry, (void *)0, 1024, 20, 10);
            if(winA_thread == RT_NULL){
                rt_kprintf("winA thread create failed\n");
                return -1;
            }
            else {
                rt_thread_startup(winA_thread);
            }

            //创建售票口B线程
            rt_thread_t winB_thread = rt_thread_create("winB", ticket_entry, (void *)1, 1024, 20, 10);
            if(winB_thread == RT_NULL){
                rt_kprintf("winB thread create failed\n");
                return -1;
            }
            else {
                rt_thread_startup(winB_thread);
            }
        }
    }
    return 0;
}
MSH_CMD_EXPORT(ticket_demo, a ticket demo);
