/*
 * 练习一：停车场车位管理（计数信号量）
 * 场景：3 个车位、5 辆车，车辆申请车位（2 秒超时），停留后释放。
 *      任意时刻进场车辆数不能超过 3。
 * 命令：parking_demo
 * 注：本版 RT-Thread 在动态线程 entry 返回后由调度器自动 close 线程对象，
 *      期间对象 type 字段可能被回收，因此 mgr 不再显式 rt_thread_delete 已返回的
 *      worker 句柄——会触发 `rt_object_get_type(...) == RT_Object_Class_Thread` 断言。
 *      改为仅清空句柄；重复执行时通过为每个线程加 #seq 后缀避免对象名冲突。
 */
#include <rtthread.h>
#include <string.h>

#define PARK_SPACES 3
#define CAR_NUM     5
#define ENTER_TO    2000   /* 申请车位超时：2 秒 */

static rt_sem_t    park_sem = RT_NULL;
static rt_sem_t    done_sem = RT_NULL;
static rt_thread_t car_t[CAR_NUM];
static volatile rt_bool_t park_run = RT_FALSE;
static rt_int32_t   park_run_seq = 0;     /* 同名对象冲突防护 */

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
    /* 不能 rt_thread_delete 已返回的 worker 句柄（type 字段可能已被调度器置 NULL，
       会触发 RT_OBJECT_GET_TYPE 断言）。仅清空句柄、清理 IPC 即可。 */
    for (int i = 0; i < CAR_NUM; i++) car_t[i] = RT_NULL;

    if (park_sem) { rt_sem_delete(park_sem); park_sem = RT_NULL; }
    if (done_sem) { rt_sem_delete(done_sem); done_sem = RT_NULL; }

    park_run = RT_FALSE;
    rt_kprintf("parking demo finished\n");
    /* mgr 函数返回后由 RT-Thread 调度器自动 close；下次 demo 通过 #seq 后缀建线程避冲突 */
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
        char cname[16];
        rt_snprintf(cname, sizeof(cname), "car#%d_%d", ++park_run_seq, i);
        car_t[i] = rt_thread_create(cname, car_entry, (void *)(rt_uint32_t)(i + 1),
                                    1024, 20, 10);
        if (car_t[i]) rt_thread_startup(car_t[i]);
        else { rt_kprintf("car%d create failed\n", i + 1); rt_sem_release(done_sem); }
    }

    char mname[16];
    rt_snprintf(mname, sizeof(mname), "park_mgr#%d", park_run_seq);
    rt_thread_t mgr_t = rt_thread_create(mname, park_mgr_entry, RT_NULL, 1024, 15, 10);
    if (mgr_t) rt_thread_startup(mgr_t);
    else { rt_kprintf("mgr create failed\n"); }
    return 0;
}
MSH_CMD_EXPORT(parking_demo, parking lot: counting semaphore demo);
