/*
 * 练习三：系统启动条件检查（事件集 AND 模式）
 * 场景：网络 / 传感器 / 存储三模块初始化完成后，主业务线程才能启动。
 * 命令：startup_demo           -> 三模块均在 3 秒内就绪（正常）
 *       startup_demo slow       -> 存储模块设为 5 秒，触发 3 秒超时，打印未就绪模块后继续等待
 * 注：本版 RT-Thread 在动态线程 entry 返回后由调度器自动 close 线程对象，
 *      期间对象 type 字段可能被回收，因此 mgr 不再显式 rt_thread_delete 已返回的
 *      worker 句柄——会触发 `rt_object_get_type(...) == RT_Object_Class_Thread` 断言。
 *      改为仅清空句柄；重复执行时通过为每个线程加 #seq 后缀避免对象名冲突。
 */
#include <rtthread.h>
#include <string.h>

#define EVENT_NET      (1U << 0)
#define EVENT_SENSOR   (1U << 1)
#define EVENT_STORAGE  (1U << 2)
#define EVENT_ALL      (EVENT_NET | EVENT_SENSOR | EVENT_STORAGE)

static rt_event_t   evt = RT_NULL;
static rt_sem_t    done_sem = RT_NULL;
static rt_thread_t init_t[3];
static rt_thread_t biz_t = RT_NULL;
static volatile rt_bool_t st_run = RT_FALSE;
static volatile rt_uint32_t storage_delay = 1000;
static rt_int32_t   st_run_seq = 0;      /* 同名对象冲突防护 */

static void init_entry(void *p)
{
    rt_uint32_t which = (rt_uint32_t)p;
    if (which == 0)      { rt_thread_mdelay(400); rt_event_send(evt, EVENT_NET);    rt_kprintf("network ready\n"); }
    else if (which == 1) { rt_thread_mdelay(600); rt_event_send(evt, EVENT_SENSOR); rt_kprintf("sensor ready\n"); }
    else                 { rt_thread_mdelay(storage_delay); rt_event_send(evt, EVENT_STORAGE); rt_kprintf("storage ready\n"); }
    rt_sem_release(done_sem);
}

static void biz_entry(void *p)
{
    rt_uint32_t recved = 0;
    if (rt_event_recv(evt, EVENT_ALL, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, 3000, &recved) == RT_EOK)
    {
        rt_kprintf("all modules are ready\nbusiness task started\n");
    }
    else
    {
        rt_kprintf("wait modules timeout, storage is not ready\n");
        rt_event_recv(evt, EVENT_ALL, RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR, RT_WAITING_FOREVER, &recved);
        rt_kprintf("all modules are ready\nbusiness task started\n");
    }
    rt_sem_release(done_sem);
}

static void st_mgr_entry(void *p)
{
    for (int i = 0; i < 3; i++)
        rt_sem_take(done_sem, RT_WAITING_FOREVER);   /* 3 个 init_entry */
    rt_sem_take(done_sem, RT_WAITING_FOREVER);       /* biz_entry */
    /* 不能 rt_thread_delete 已返回的 worker 句柄（type 字段可能已被调度器置 NULL） */
    for (int i = 0; i < 3; i++) init_t[i] = RT_NULL;
    biz_t = RT_NULL;

    if (evt) { rt_event_delete(evt); evt = RT_NULL; }
    if (done_sem) { rt_sem_delete(done_sem); done_sem = RT_NULL; }

    st_run = RT_FALSE;
    rt_kprintf("startup demo finished\n");
    /* mgr 函数返回后由 RT-Thread 调度器自动 close；下次 demo 通过 #seq 后缀避冲突 */
}

static int startup_demo(int argc, char **argv)
{
    if (st_run) { rt_kprintf("startup_demo is already running\n"); return -RT_EBUSY; }
    st_run = RT_TRUE;
    storage_delay = 1000;
    if (argc > 1 && strcmp(argv[1], "slow") == 0) storage_delay = 5000;

    evt = rt_event_create("start_evt", RT_IPC_FLAG_FIFO);
    done_sem = rt_sem_create("st_done", 0, RT_IPC_FLAG_FIFO);
    if (!evt || !done_sem) { rt_kprintf("create failed\n"); st_run = RT_FALSE; return -RT_ENOMEM; }

    memset(init_t, 0, sizeof(init_t));
    biz_t = RT_NULL;
    const char *names[3] = { "net", "sensor", "storage" };
    ++st_run_seq;
    for (int i = 0; i < 3; i++)
    {
        char tname[16];
        rt_snprintf(tname, sizeof(tname), "%s#%d", names[i], st_run_seq);
        init_t[i] = rt_thread_create(tname, init_entry, (void *)(rt_uint32_t)i, 1024, 20, 10);
        if (init_t[i]) rt_thread_startup(init_t[i]);
        else rt_sem_release(done_sem);
    }

    {
        char bname[16];
        rt_snprintf(bname, sizeof(bname), "biz#%d", st_run_seq);
        biz_t = rt_thread_create(bname, biz_entry, RT_NULL, 1024, 10, 10);
        if (biz_t) rt_thread_startup(biz_t);
        else rt_sem_release(done_sem);
    }

    {
        char mname[16];
        rt_snprintf(mname, sizeof(mname), "st_mgr#%d", st_run_seq);
        rt_thread_t mgr_t = rt_thread_create(mname, st_mgr_entry, RT_NULL, 1024, 15, 10);
        if (mgr_t) rt_thread_startup(mgr_t);
    }
    return 0;
}
MSH_CMD_EXPORT(startup_demo, startup check: event AND (slow = storage 5s timeout test));
