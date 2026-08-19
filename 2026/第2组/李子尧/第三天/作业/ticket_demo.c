#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>

#define TICKET_TOTAL 20
static rt_mutex_t ticket_lock;
static volatile int tickets, ticket_done, ticket_sold, ticket_dup, ticket_neg;
static rt_uint8_t ticket_safe, ticket_running, ticket_seen[TICKET_TOTAL + 1];

static void ticket_window(void *p)
{
    char name = (char)(rt_ubase_t)p;
    for (;;)
    {
        int snapshot, number;
        if (ticket_safe) rt_mutex_take(ticket_lock, RT_WAITING_FOREVER);
        snapshot = tickets;
        if (snapshot <= 0) { if (ticket_safe) rt_mutex_release(ticket_lock); break; }
        if (!ticket_safe) rt_thread_mdelay(12); /* intentionally leaves read-modify-write unprotected */
        tickets = snapshot - 1; number = snapshot;
        { rt_base_t l = rt_hw_interrupt_disable(); if (ticket_seen[number]++) ++ticket_dup; ++ticket_sold; if (tickets < 0) ticket_neg = 1; rt_hw_interrupt_enable(l); }
        if (ticket_safe) rt_mutex_release(ticket_lock);
        rt_kprintf("[ticket %c] sold ticket %d\n", name, number);
        rt_thread_mdelay(5);
    }
    { rt_base_t l = rt_hw_interrupt_disable(); ++ticket_done; rt_hw_interrupt_enable(l); }
}
static void ticket_phase(rt_uint8_t safe)
{
    rt_thread_t a, b; int i; ticket_safe = safe; tickets = TICKET_TOTAL; ticket_done = ticket_sold = ticket_dup = ticket_neg = 0; for (i=0;i<=TICKET_TOTAL;i++) ticket_seen[i]=0;
    if (safe && !(ticket_lock = rt_mutex_create("ticketmx", RT_IPC_FLAG_FIFO))) { rt_kprintf("[ticket] mutex create failed\n"); return; }
    a=rt_thread_create("winA",ticket_window,(void *)(rt_ubase_t)'A',768,19,10); b=rt_thread_create("winB",ticket_window,(void *)(rt_ubase_t)'B',768,19,10);
    if (!a || !b || rt_thread_startup(a)!=RT_EOK || rt_thread_startup(b)!=RT_EOK) { rt_kprintf("[ticket] window create/start failed\n"); return; }
    while(ticket_done<2) rt_thread_mdelay(20);
    rt_kprintf("========== TICKET %s RESULT ==========\n sold: %d, remaining: %d, duplicates: %d, negative: %s\n result: %s\n", safe?"SAFE":"UNSAFE",ticket_sold,tickets,ticket_dup,ticket_neg?"YES":"NO", safe?(ticket_sold==20&&tickets==0&&!ticket_dup&&!ticket_neg?"PASS":"FAIL"):(ticket_dup||ticket_sold!=20?"RACE OBSERVED":"RACE NOT OBSERVED"));
    if(safe) { rt_mutex_delete(ticket_lock); ticket_lock=RT_NULL; }
}
static void ticket_manager(void *p) { (void)p; ticket_phase(0); ticket_phase(1); { rt_base_t l=rt_hw_interrupt_disable(); ticket_running=0; rt_hw_interrupt_enable(l); } }
static void ticket_demo(void) { rt_thread_t t; rt_base_t l=rt_hw_interrupt_disable(); if(ticket_running){rt_hw_interrupt_enable(l);rt_kprintf("[ticket] demo already running\n");return;}ticket_running=1;rt_hw_interrupt_enable(l);t=rt_thread_create("ticketmgr",ticket_manager,0,1024,17,10);if(!t||rt_thread_startup(t)!=RT_EOK){rt_kprintf("[ticket] manager create/start failed\n");l=rt_hw_interrupt_disable();ticket_running=0;rt_hw_interrupt_enable(l);} }
MSH_CMD_EXPORT(ticket_demo, ticket mutex unsafe and safe comparison);
