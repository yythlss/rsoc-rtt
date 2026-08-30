/* Day3: RT-Thread IPC exercises. Run with parking_demo, ticket_demo,
 * startup_demo, or package_demo from msh. */
#include <rtthread.h>
#include <finsh.h>

#define STK 1024
#define PRI 18
#define SLICE 10

static rt_thread_t make_thread(const char *name, void (*entry)(void *), void *arg)
{
    rt_thread_t th = rt_thread_create(name, entry, arg, STK, PRI, SLICE);
    if (th == RT_NULL || rt_thread_startup(th) != RT_EOK)
    {
        rt_kprintf("create/start %s failed\n", name);
        return RT_NULL;
    }
    return th;
}

/* -------------------------- parking semaphore ------------------------- */
struct parking_ctx { rt_sem_t space, done; rt_mutex_t lock; int used; volatile rt_bool_t busy; };
static struct parking_ctx park;
static rt_uint8_t car_id[5] = {1, 2, 3, 4, 5};

static void car_entry(void *p)
{
    int id = *(rt_uint8_t *)p;
    while (rt_sem_take(park.space, rt_tick_from_millisecond(2000)) != RT_EOK)
        rt_kprintf("car%d waiting for a space (timeout)\n", id);

    rt_mutex_take(park.lock, RT_WAITING_FOREVER);
    park.used++;
    rt_kprintf("car%d entered, used spaces: %d\n", id, park.used);
    rt_mutex_release(park.lock);
    rt_thread_mdelay(2300 + id * 120);
    rt_mutex_take(park.lock, RT_WAITING_FOREVER);
    park.used--;
    rt_kprintf("car%d left, used spaces: %d\n", id, park.used);
    rt_mutex_release(park.lock);
    rt_sem_release(park.space);
    rt_sem_release(park.done);
}
static void parking_ctl(void *p)
{
    int i; RT_UNUSED(p);
    park.space = rt_sem_create("space", 3, RT_IPC_FLAG_FIFO);
    park.done = rt_sem_create("park_dn", 0, RT_IPC_FLAG_FIFO);
    park.lock = rt_mutex_create("park_lk", RT_IPC_FLAG_FIFO);
    if (!park.space || !park.done || !park.lock) goto out;
    for (i = 0; i < 5; i++) if (!make_thread("car", car_entry, &car_id[i])) goto out;
    for (i = 0; i < 5; i++) rt_sem_take(park.done, RT_WAITING_FOREVER);
    rt_kprintf("parking demo finished: used spaces: %d\n", park.used);
out:
    if (park.space) rt_sem_delete(park.space);
    if (park.done) rt_sem_delete(park.done);
    if (park.lock) rt_mutex_delete(park.lock);
    park.space = RT_NULL; park.done = RT_NULL; park.lock = RT_NULL; park.busy = RT_FALSE;
}
static int parking_demo(int argc, char **argv)
{
    RT_UNUSED(argc); RT_UNUSED(argv);
    if (park.busy) { rt_kprintf("parking demo is running\n"); return -RT_EBUSY; }
    park.busy = RT_TRUE; park.used = 0;
    if (!make_thread("park_ctl", parking_ctl, RT_NULL)) park.busy = RT_FALSE;
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, manage 3 parking spaces with 5 cars);

/* ---------------------------- ticket mutex ---------------------------- */
struct ticket_ctx { rt_mutex_t lock; rt_sem_t done; int left; rt_bool_t unsafe; volatile rt_bool_t busy; };
static struct ticket_ctx ticket;
static char win_name[2] = {'A','B'};
static void window_entry(void *p)
{
    char name = *(char *)p;
    for (;;)
    {
        int number;
        if (ticket.unsafe)
        {
            if (ticket.left <= 0) break;
            number = ticket.left; rt_thread_mdelay(20); ticket.left--;
            rt_kprintf("window %c sold ticket %d (unsafe)\n", name, number);
        }
        else
        {
            rt_mutex_take(ticket.lock, RT_WAITING_FOREVER);
            if (ticket.left <= 0) { rt_mutex_release(ticket.lock); break; }
            number = ticket.left--; /* Check, decrement and record are one critical section. */
            rt_kprintf("window %c sold ticket %d\n", name, number);
            rt_mutex_release(ticket.lock);
        }
        rt_thread_mdelay(70 + (name == 'A' ? 10 : 0));
    }
    rt_sem_release(ticket.done);
}
static void ticket_ctl(void *p)
{
    RT_UNUSED(p); ticket.done = rt_sem_create("tick_dn", 0, RT_IPC_FLAG_FIFO);
    ticket.lock = ticket.unsafe ? RT_NULL : rt_mutex_create("tick_lk", RT_IPC_FLAG_FIFO);
    if (!ticket.done || (!ticket.unsafe && !ticket.lock)) goto out;
    if (!make_thread("window_a", window_entry, &win_name[0]) || !make_thread("window_b", window_entry, &win_name[1])) goto out;
    rt_sem_take(ticket.done, RT_WAITING_FOREVER); rt_sem_take(ticket.done, RT_WAITING_FOREVER);
    rt_kprintf("all tickets sold, remaining tickets: %d\n", ticket.left);
out:
    if (ticket.lock) rt_mutex_delete(ticket.lock); if (ticket.done) rt_sem_delete(ticket.done);
    ticket.lock = RT_NULL; ticket.done = RT_NULL; ticket.busy = RT_FALSE;
}
static int ticket_demo(int argc, char **argv)
{
    if (ticket.busy) return -RT_EBUSY;
    ticket.unsafe = (argc > 1 && !rt_strcmp(argv[1], "unsafe")); ticket.left = 20; ticket.busy = RT_TRUE;
    if (!make_thread("tick_ctl", ticket_ctl, RT_NULL)) ticket.busy = RT_FALSE;
    return RT_EOK;
}
MSH_CMD_EXPORT(ticket_demo, sell 20 tickets; use ticket_demo unsafe for comparison);

/* ----------------------------- startup event -------------------------- */
#define EVENT_NET_READY      (1U << 0)
#define EVENT_SENSOR_READY   (1U << 1)
#define EVENT_STORAGE_READY  (1U << 2)
#define EVENT_ALL_READY (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)
struct startup_ctx { rt_event_t event; rt_sem_t done; rt_uint32_t ready; volatile rt_bool_t busy; };
static struct startup_ctx boot;
static void init_entry(void *p)
{
    rt_uint32_t bit = (rt_uint32_t)p; rt_uint32_t delay = bit == EVENT_NET_READY ? 500 : bit == EVENT_SENSOR_READY ? 1200 : 5000;
    rt_thread_mdelay(delay); rt_event_send(boot.event, bit);
    boot.ready |= bit;
    rt_kprintf("%s ready\n", bit == EVENT_NET_READY ? "network" : bit == EVENT_SENSOR_READY ? "sensor" : "storage");
    rt_sem_release(boot.done);
}
static void business_entry(void *p)
{
    rt_uint32_t recved; RT_UNUSED(p);
    if (rt_event_recv(boot.event, EVENT_ALL_READY, RT_EVENT_FLAG_AND, rt_tick_from_millisecond(3000), &recved) != RT_EOK)
    {
        rt_kprintf("wait modules timeout,");
        if (!(boot.ready & EVENT_NET_READY)) rt_kprintf(" network");
        if (!(boot.ready & EVENT_SENSOR_READY)) rt_kprintf(" sensor");
        if (!(boot.ready & EVENT_STORAGE_READY)) rt_kprintf(" storage");
        rt_kprintf(" is not ready\n");
        rt_event_recv(boot.event, EVENT_ALL_READY, RT_EVENT_FLAG_AND, RT_WAITING_FOREVER, &recved);
    }
    rt_kprintf("all modules are ready\nbusiness task started\n"); rt_sem_release(boot.done);
}
static void startup_ctl(void *p)
{
    RT_UNUSED(p); boot.event = rt_event_create("ready_ev", RT_IPC_FLAG_FIFO); boot.done = rt_sem_create("boot_dn", 0, RT_IPC_FLAG_FIFO); boot.ready = 0;
    if (!boot.event || !boot.done) goto out;
    if (!make_thread("net_init", init_entry, (void *)EVENT_NET_READY) || !make_thread("sen_init", init_entry, (void *)EVENT_SENSOR_READY) || !make_thread("sto_init", init_entry, (void *)EVENT_STORAGE_READY) || !make_thread("business", business_entry, RT_NULL)) goto out;
    rt_sem_take(boot.done, RT_WAITING_FOREVER); rt_sem_take(boot.done, RT_WAITING_FOREVER); rt_sem_take(boot.done, RT_WAITING_FOREVER); rt_sem_take(boot.done, RT_WAITING_FOREVER);
out:
    if (boot.event) rt_event_delete(boot.event); if (boot.done) rt_sem_delete(boot.done);
    boot.event = RT_NULL; boot.done = RT_NULL; boot.busy = RT_FALSE;
}
static int startup_demo(int argc, char **argv)
{
    RT_UNUSED(argc); RT_UNUSED(argv); if (boot.busy) return -RT_EBUSY; boot.busy = RT_TRUE;
    if (!make_thread("boot_ctl", startup_ctl, RT_NULL)) boot.busy = RT_FALSE; return RT_EOK;
}
MSH_CMD_EXPORT(startup_demo, wait for three modules with an event set);

/* -------------------------- package message queue --------------------- */
struct package_msg { rt_uint32_t id; rt_uint8_t region; rt_uint16_t weight; };
struct package_ctx { rt_mq_t mq; rt_sem_t sender_done, sorter_done; int failed; volatile rt_bool_t busy; };
static struct package_ctx pkg;
static rt_uint8_t sender_id[2] = {1,2};
static const char *region_name(rt_uint8_t r) { return r == 1 ? "east" : r == 2 ? "south" : "north"; }
static void sender_entry(void *p)
{
    int n, who = *(rt_uint8_t *)p;
    for (n = 0; n < 5; n++)
    {
        struct package_msg m = { (rt_uint32_t)(who * 1000 + n + 1), (rt_uint8_t)((who + n) % 3 + 1), (rt_uint16_t)(700 + who * 100 + n * 80) };
        while (rt_mq_send(pkg.mq, &m, sizeof(m)) != RT_EOK) { pkg.failed++; rt_thread_mdelay(80); }
        rt_thread_mdelay(30);
    }
    rt_sem_release(pkg.sender_done);
}
static void sorter_entry(void *p)
{
    struct package_msg m; int total = 0, count[4] = {0}, weight = 0; RT_UNUSED(p);
    for (;;)
    {
        if (rt_mq_recv(pkg.mq, &m, sizeof(m), RT_WAITING_FOREVER) != RT_EOK) continue;
        if (m.id == 0) break;
        total++; count[m.region]++; weight += m.weight;
        rt_kprintf("package %lu -> %s, weight: %u g\n", (unsigned long)m.id, region_name(m.region), m.weight);
        rt_thread_mdelay(120);
    }
    rt_kprintf("total packages: %d\neast: %d, south: %d, north: %d\ntotal weight: %d g\nsend failed: %d\n", total, count[1], count[2], count[3], weight, pkg.failed);
    rt_sem_release(pkg.sorter_done);
}
static void package_ctl(void *p)
{
    struct package_msg end = {0,0,0}; RT_UNUSED(p); pkg.failed = 0;
    pkg.mq = rt_mq_create("pkg_mq", sizeof(struct package_msg), 5, RT_IPC_FLAG_FIFO);
    pkg.sender_done = rt_sem_create("send_dn", 0, RT_IPC_FLAG_FIFO); pkg.sorter_done = rt_sem_create("sort_dn", 0, RT_IPC_FLAG_FIFO);
    if (!pkg.mq || !pkg.sender_done || !pkg.sorter_done) goto out;
    if (!make_thread("sender_1", sender_entry, &sender_id[0]) || !make_thread("sender_2", sender_entry, &sender_id[1]) || !make_thread("sorter", sorter_entry, RT_NULL)) goto out;
    rt_sem_take(pkg.sender_done, RT_WAITING_FOREVER); rt_sem_take(pkg.sender_done, RT_WAITING_FOREVER);
    while (rt_mq_send(pkg.mq, &end, sizeof(end)) != RT_EOK) rt_thread_mdelay(50);
    rt_sem_take(pkg.sorter_done, RT_WAITING_FOREVER);
out:
    if (pkg.mq) rt_mq_delete(pkg.mq); if (pkg.sender_done) rt_sem_delete(pkg.sender_done); if (pkg.sorter_done) rt_sem_delete(pkg.sorter_done);
    pkg.mq = RT_NULL; pkg.sender_done = RT_NULL; pkg.sorter_done = RT_NULL; pkg.busy = RT_FALSE;
}
static int package_demo(int argc, char **argv)
{
    RT_UNUSED(argc); RT_UNUSED(argv); if (pkg.busy) return -RT_EBUSY; pkg.busy = RT_TRUE;
    if (!make_thread("pkg_ctl", package_ctl, RT_NULL)) pkg.busy = RT_FALSE; return RT_EOK;
}
MSH_CMD_EXPORT(package_demo, sort 10 packages through a message queue);
