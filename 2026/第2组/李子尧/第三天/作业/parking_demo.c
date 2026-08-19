#include <rtthread.h>
#include <rthw.h>
#include <finsh.h>

#define PARK_CAPACITY 3
#define PARK_CARS 5
static rt_sem_t park_sem;
static rt_thread_t park_workers[PARK_CARS];
static rt_uint8_t park_running, park_done, park_used, park_max, park_bad;
static int park_ids[PARK_CARS] = {1, 2, 3, 4, 5};

static void park_car(void *p)
{
    int id = *(int *)p;
    rt_thread_mdelay(80 * id);
    while (rt_sem_take(park_sem, rt_tick_from_millisecond(2000)) != RT_EOK)
    {
        rt_kprintf("[parking] car%d waiting timeout, retry later\n", id);
        rt_thread_mdelay(150 + id * 20);
    }
    { rt_base_t l = rt_hw_interrupt_disable(); ++park_used; if (park_used > park_max) park_max = park_used; if (park_used > PARK_CAPACITY) park_bad = 1; rt_hw_interrupt_enable(l); }
    rt_kprintf("[parking] car%d entered, used spaces: %d\n", id, park_used);
    rt_thread_mdelay(500 + (id % 3) * 180);
    { rt_base_t l = rt_hw_interrupt_disable(); --park_used; rt_hw_interrupt_enable(l); }
    rt_sem_release(park_sem);
    rt_kprintf("[parking] car%d left\n", id);
    { rt_base_t l = rt_hw_interrupt_disable(); ++park_done; rt_hw_interrupt_enable(l); }
}

static void park_manager(void *p)
{
    int i; (void)p;
    park_done = park_used = park_max = park_bad = 0;
    park_sem = rt_sem_create("parksem", PARK_CAPACITY, RT_IPC_FLAG_FIFO);
    if (!park_sem) goto out;
    for (i = 0; i < PARK_CARS; i++)
    {
        park_workers[i] = rt_thread_create("parkcar", park_car, &park_ids[i], 768, 18, 10);
        if (!park_workers[i] || rt_thread_startup(park_workers[i]) != RT_EOK) { rt_kprintf("[parking] car thread create/start failed\n"); goto wait; }
    }
wait:
    while (park_done < PARK_CARS) rt_thread_mdelay(50);
    rt_kprintf("========== PARKING RESULT ==========\n cars completed: %d/%d\n max used spaces: %d\n capacity violation: %s\n result: %s\n", park_done, PARK_CARS, park_max, park_bad ? "YES" : "NO", (!park_bad && park_done == PARK_CARS) ? "PASS" : "FAIL");
    rt_sem_delete(park_sem); park_sem = RT_NULL;
out:
    { rt_base_t l = rt_hw_interrupt_disable(); park_running = 0; rt_hw_interrupt_enable(l); }
}

static void parking_demo(void)
{
    rt_thread_t t; rt_base_t l = rt_hw_interrupt_disable(); if (park_running) { rt_hw_interrupt_enable(l); rt_kprintf("[parking] demo already running\n"); return; } park_running = 1; rt_hw_interrupt_enable(l);
    t = rt_thread_create("parkmgr", park_manager, RT_NULL, 1024, 17, 10);
    if (!t || rt_thread_startup(t) != RT_EOK) { rt_kprintf("[parking] manager create/start failed\n"); l = rt_hw_interrupt_disable(); park_running = 0; rt_hw_interrupt_enable(l); }
}
MSH_CMD_EXPORT(parking_demo, parking counting semaphore demo);
