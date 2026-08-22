/*
 * 练习一：停车场车位管理（计数信号量）
 * 场景：3 个车位、5 辆车，车辆申请车位（2 秒超时），停留后释放。
 *      任意时刻进场车辆数不能超过 3。
 * 命令：parking_demo
 */
#include <rtthread.h>
#include <string.h>

#define PARK_SPACES 3
#define CAR_NUM     5
#define ENTER_TO    2000   /* 申请车位超时：2 秒 */

static rt_sem_t    park_sem = RT_NULL;
static rt_sem_t    done_sem = RT_NULL;
static rt_thread_t car_t[CAR_NUM];
static rt_thread_t park_mgr = RT_NULL;
static volatile rt_bool_t park_run = RT_FALSE;

static void car_entry(void *p)
{
    rt_uint32_t id = (rt_uint32_t)p;
    rt_uint32_t val = 0;

    if (rt_sem_take(park_sem, ENTER_TO) == RT_EOK)
    {
        val = park_sem->value;                       /* 剩余车位 */
        rt_kprintf("car%d entered, used spaces: %d\n", id, PARK_SPACES - val);
        rt_thread_mdelay(1000 + id * 200);           /* 停留一段时间 */
        rt_sem_release(park_sem);
        rt_kprintf("car%d left\n", id);
    }
    else
    {
        rt_kprintf("car%d waiting for a space (timeout)\n", id);
        rt_thread_mdelay(300);
        rt_sem_take(park_sem, RT_WAITING_FOREVER);  /* 超时后重新申请 */
        val = park_sem->value;
        rt_kprintf("car%d entered, used spaces: %d\n", id, PARK_SPACES - val);
        rt_thread_mdelay(800);
        rt_sem_release(park_sem);
        rt_kprintf("car%d left\n", id);
    }
    rt_sem_release(done_sem);
}

static void park_mgr_entry(void *p)
{
    for (int i = 0; i < CAR_NUM; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    for (int i = 0; i < CAR_NUM; i++)
    {
        if (car_t[i]) { rt_thread_delete(car_t[i]); car_t[i] = RT_NULL; }
    }
    if (park_sem) { rt_sem_delete(park_sem); park_sem = RT_NULL; }
    if (done_sem) { rt_sem_delete(done_sem); done_sem = RT_NULL; }
    park_run = RT_FALSE;
    rt_kprintf("parking demo finished\n");
    rt_thread_delete(rt_thread_self());
}

static int parking_demo(int argc, char **argv)
{
    if (park_run) { rt_kprintf("parking_demo is already running\n"); return -RT_EBUSY; }
    park_run = RT_TRUE;

    park_sem = rt_sem_create("park_sem", PARK_SPACES, RT_IPC_FLAG_FIFO);
    done_sem = rt_sem_create("park_done", 0, RT_IPC_FLAG_FIFO);
    if (!park_sem || !done_sem) { rt_kprintf("sem create failed\n"); park_run = RT_FALSE; return -RT_ENOMEM; }

    memset(car_t, 0, sizeof(car_t));
    for (int i = 0; i < CAR_NUM; i++)
    {
        car_t[i] = rt_thread_create("car", car_entry, (void *)(rt_uint32_t)(i + 1),
                                    1024, 20, 10);
        if (car_t[i]) rt_thread_startup(car_t[i]);
        else { rt_kprintf("car%d create failed\n", i + 1); rt_sem_release(done_sem); }
    }

    park_mgr = rt_thread_create("park_mgr", park_mgr_entry, RT_NULL, 1024, 15, 10);
    if (park_mgr) rt_thread_startup(park_mgr);
    else { rt_kprintf("mgr create failed\n"); }
    return 0;
}
MSH_CMD_EXPORT(parking_demo, parking lot: counting semaphore demo);
