/*
 * 练习二：多窗口售票系统（互斥量）
 * 场景：20 张票，窗口 A / B 两个线程竞争售票，互斥量保护完整售票过程。
 * 对比：ticket_demo         -> 带互斥量（正确）
 *       ticket_demo raw     -> 不加互斥量，读取与修改间故意延时，暴露重复售票
 * 命令：ticket_demo [raw]
 * 注：本版 RT-Thread 在动态线程 entry 返回后由调度器自动 close 线程对象，
 *      期间对象 type 字段可能被回收，因此 mgr 不再显式 rt_thread_delete 已返回的
 *      worker 句柄——会触发 `rt_object_get_type(...) == RT_Object_Class_Thread` 断言。
 *      改为仅清空句柄；重复执行时通过为每个线程加 #seq 后缀避免对象名冲突。
 */
#include <rtthread.h>
#include <string.h>

#define TICKET_TOTAL 20

static rt_mutex_t  tkt_mutex = RT_NULL;
static rt_sem_t    done_sem  = RT_NULL;
static rt_int32_t  tickets   = TICKET_TOTAL;
static rt_thread_t win_t[2];
static volatile rt_bool_t tkt_run = RT_FALSE;
static volatile rt_bool_t tkt_use_mutex = RT_TRUE;
static rt_int32_t   tkt_run_seq = 0;     /* 同名对象冲突防护 */

static void window_entry(void *p)
{
    rt_uint32_t w = (rt_uint32_t)p;
    char c = (w == 0) ? 'A' : 'B';

    while (1)
    {
        if (tkt_use_mutex) rt_mutex_take(tkt_mutex, RT_WAITING_FOREVER);

        if (tickets > 0)
        {
            rt_thread_mdelay(10);                    /* 读取与修改之间故意延时，raw 模式暴露竞争 */
            rt_int32_t sold = tickets;               /* 当前售出的票号 */
            tickets--;
            rt_kprintf("window %c sold ticket %d\n", c, sold);

            if (tkt_use_mutex) rt_mutex_release(tkt_mutex);

            if (tickets == 0) break;
            rt_thread_mdelay(15);
        }
        else
        {
            if (tkt_use_mutex) rt_mutex_release(tkt_mutex);
            break;
        }
    }
    rt_sem_release(done_sem);
}

static void tkt_mgr_entry(void *p)
{
    for (int i = 0; i < 2; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    /* 不能 rt_thread_delete 已返回的 worker 句柄（type 字段可能已被调度器置 NULL）。
       仅清空句柄、清理 IPC 即可。 */
    win_t[0] = RT_NULL;
    win_t[1] = RT_NULL;

    if (tkt_use_mutex && tkt_mutex) { rt_mutex_delete(tkt_mutex); tkt_mutex = RT_NULL; }
    if (done_sem) { rt_sem_delete(done_sem); done_sem = RT_NULL; }

    tkt_run = RT_FALSE;
    rt_kprintf("all tickets sold, remaining tickets: %d\n", tickets);
    /* mgr 函数返回后由 RT-Thread 调度器自动 close；下次 demo 通过 #seq 后缀避冲突 */
}

static int ticket_demo(int argc, char **argv)
{
    if (tkt_run) { rt_kprintf("ticket_demo is already running\n"); return -RT_EBUSY; }

    tkt_use_mutex = RT_TRUE;
    if (argc > 1 && strcmp(argv[1], "raw") == 0) tkt_use_mutex = RT_FALSE;
    tkt_run = RT_TRUE;
    tickets = TICKET_TOTAL;

    done_sem = rt_sem_create("tkt_done", 0, RT_IPC_FLAG_FIFO);
    if (tkt_use_mutex)
    {
        tkt_mutex = rt_mutex_create("tkt_mtx", RT_IPC_FLAG_FIFO);
        if (!tkt_mutex) { rt_kprintf("mutex create failed\n"); tkt_run = RT_FALSE; return -RT_ENOMEM; }
    }
    if (!done_sem) { rt_kprintf("sem create failed\n"); tkt_run = RT_FALSE; return -RT_ENOMEM; }

    memset(win_t, 0, sizeof(win_t));
    char wnames[2][16], mname[16];
    for (int i = 0; i < 2; i++)
    {
        rt_snprintf(wnames[i], sizeof(wnames[i]), "win%c#%d", (i == 0) ? 'A' : 'B', ++tkt_run_seq);
        win_t[i] = rt_thread_create(wnames[i], window_entry, (void *)(rt_uint32_t)i,
                                    1024, 20, 10);
        if (win_t[i]) rt_thread_startup(win_t[i]);
        else { rt_kprintf("window %d create failed\n", i); rt_sem_release(done_sem); }
    }

    rt_snprintf(mname, sizeof(mname), "tkt_mgr#%d", tkt_run_seq);
    rt_thread_t mgr_t = rt_thread_create(mname, tkt_mgr_entry, RT_NULL, 1024, 15, 10);
    if (mgr_t) rt_thread_startup(mgr_t);
    return 0;
}
MSH_CMD_EXPORT(ticket_demo, ticket sale: mutex demo (raw = run without mutex));
