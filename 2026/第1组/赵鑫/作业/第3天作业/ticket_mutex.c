#include <rtthread.h>
#include <rthw.h>               /* rt_hw_interrupt_disable / enable */

#define TICKET_TOTAL        20         /* 车票总数 */
#define WIN_NUM             2          /* 售票窗口数 */
#define WIN_PRIO            15         /* 窗口线程优先级 */
#define THREAD_TIMESLICE    10         /* 时间片 */
#define THREAD_STACK_SIZE   1024       /* 线程栈 */
#define NOLOCK_DELAY        5          /* 无锁版"读票-改票"间延时（ms），放大竞争 */
#define SOLD_MAX            64         /* 售出记录容量（无锁版最多 2 倍记录数 + 余量） */

static rt_mutex_t  ticket_mutex = RT_NULL;  /* 售票互斥量（仅加锁版创建） */
static rt_sem_t    done_sem     = RT_NULL;  /* 完成信号量：命令等待两窗口退出 */
static rt_thread_t win_thread[WIN_NUM];     /* 窗口线程句柄 */
static volatile rt_uint8_t demo_running = 0;    /* 任务运行标志，防止重复启动 */
static volatile rt_uint8_t locked_mode = 0;     /* 1=加锁版，0=无锁版（线程入口读取） */

/* 共享资源：剩余票数（全局变量，两种模式共用） */
static volatile int ticket_count = TICKET_TOTAL;

/* 售出记录：记录每次"成功售出"的票号（含无锁版重复售出的记录） */
static int sold_no[SOLD_MAX];
static int sold_num = 0;

/* 记录一次售出票号；返回 1 表示该票号此前已售出（重复） */
static int record_sold(int ticket_no)
{
    rt_base_t level = rt_hw_interrupt_disable();
    int i;
    int dup = 0;

    for (i = 0; i < sold_num; i++)
    {
        if (sold_no[i] == ticket_no)
        {
            dup = 1;
            break;
        }
    }

    if (sold_num < SOLD_MAX)
    {
        sold_no[sold_num++] = ticket_no;
    }
    rt_hw_interrupt_enable(level);

    return dup;
}

/* 窗口线程入口：循环售票，无票后退出 */
static void window_entry(void *param)
{
    int win_id = (int)param;        /* 1 = 窗口 A，2 = 窗口 B */
    char win = (win_id == 1) ? 'A' : 'B';
    int ticket_no;

    while (1)
    {
        if (locked_mode)
        {
            /* ---- 加锁版：互斥量保护完整售票过程（检查+记录+减票原子进行） ---- */
            rt_mutex_take(ticket_mutex, RT_WAITING_FOREVER);

            if (ticket_count <= 0)      /* 无余票，退出 */
            {
                rt_mutex_release(ticket_mutex);
                break;
            }

            ticket_no = ticket_count;
            ticket_count--;             /* 余票减 1（与检查、记录在锁内原子完成） */

            rt_mutex_release(ticket_mutex);

            record_sold(ticket_no);     /* 锁内互斥保证票号唯一，此处不会出现重复 */
            rt_kprintf("window %c sold ticket %d\n", win, ticket_no);
        }
        else
        {
            /* ---- 无锁版：读票与改票之间插入延时，制造竞争窗口 ---- */
            ticket_no = ticket_count;   /* ① 读取余票 */
            if (ticket_no <= 0)         /* 无余票，退出 */
            {
                break;
            }
            rt_thread_mdelay(NOLOCK_DELAY);     /* ② 延时：另一窗口可能已卖同一张 */
            ticket_count--;             /* ③ 修改余票（可能覆盖他人修改） */

            if (record_sold(ticket_no))
            {
                rt_kprintf("window %c sold ticket %d [DUPLICATE]\n", win, ticket_no);
            }
            else
            {
                rt_kprintf("window %c sold ticket %d\n", win, ticket_no);
            }
        }

        rt_thread_mdelay(10);
    }

    rt_kprintf("window %c no more tickets, exit\n", win);
    rt_sem_release(done_sem);           /* 通知命令：本窗口已退出 */
}

/* 打印最终统计（两种模式共用） */
static void print_ticket_result(void)
{
    int i, j;
    int dup_count = 0;

    for (i = 0; i < sold_num; i++)
    {
        for (j = 0; j < i; j++)
        {
            if (sold_no[j] == sold_no[i])
            {
                dup_count++;
                break;
            }
        }
    }

    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %d\n", ticket_count);
    rt_kprintf("sold records: %d, unique tickets: %d, duplicates: %d\n",
               sold_num, sold_num - dup_count, dup_count);
}

/* 售票演示公共流程：locked=1 加锁，locked=0 无锁 */
static int ticket_demo_common(int locked)
{
    static const char *win_name[WIN_NUM] = { "winA", "winB" };
    int i;

    if (demo_running)
    {
        rt_kprintf("ticket_demo is already running\n");
        return -RT_ERROR;
    }

    /* 重置共享数据（支持重复执行） */
    ticket_count = TICKET_TOTAL;
    sold_num = 0;
    locked_mode = locked;

    /* 1. 完成信号量 */
    done_sem = rt_sem_create("done", 0, RT_IPC_FLAG_FIFO);
    if (done_sem == RT_NULL)
    {
        rt_kprintf("create done semaphore failed\n");
        return -RT_ERROR;
    }

    /* 2. 加锁版创建互斥量 */
    if (locked)
    {
        ticket_mutex = rt_mutex_create("t_mutex", RT_IPC_FLAG_PRIO);
        if (ticket_mutex == RT_NULL)
        {
            rt_sem_delete(done_sem);
            done_sem = RT_NULL;
            rt_kprintf("create ticket mutex failed\n");
            return -RT_ERROR;
        }
    }

    /* 3. 创建两个窗口线程（暂不启动，失败统一回收） */
    for (i = 0; i < WIN_NUM; i++)
    {
        win_thread[i] = rt_thread_create(win_name[i], window_entry, (void *)(i + 1),
                                         THREAD_STACK_SIZE, WIN_PRIO, THREAD_TIMESLICE);
        if (win_thread[i] == RT_NULL)
        {
            int j;

            rt_kprintf("create thread %s failed\n", win_name[i]);
            for (j = 0; j < i; j++)
            {
                rt_thread_delete(win_thread[j]);
            }
            if (ticket_mutex != RT_NULL)
            {
                rt_mutex_delete(ticket_mutex);
                ticket_mutex = RT_NULL;
            }
            rt_sem_delete(done_sem);
            done_sem = RT_NULL;
            return -RT_ERROR;
        }
    }

    /* 4. 统一启动 */
    demo_running = 1;
    for (i = 0; i < WIN_NUM; i++)
    {
        rt_thread_startup(win_thread[i]);
    }

    /* 5. 等待两个窗口线程退出 */
    for (i = 0; i < WIN_NUM; i++)
    {
        rt_sem_take(done_sem, RT_WAITING_FOREVER);
    }

    /* 6. 统一清理，保证可重复执行 */
    for (i = 0; i < WIN_NUM; i++)
    {
        if (win_thread[i] != RT_NULL)
        {
            rt_thread_delete(win_thread[i]);
            win_thread[i] = RT_NULL;
        }
    }
    if (ticket_mutex != RT_NULL)
    {
        rt_mutex_delete(ticket_mutex);
        ticket_mutex = RT_NULL;
    }
    rt_sem_delete(done_sem);
    done_sem = RT_NULL;
    demo_running = 0;

    /* 7. 统计结果 */
    print_ticket_result();
    return RT_EOK;
}

/* 加锁版：互斥量保护完整售票过程 */
static int ticket_demo(void)
{
    rt_kprintf("=== ticket demo with mutex protection ===\n");
    return ticket_demo_common(1);
}
MSH_CMD_EXPORT(ticket_demo, ticket selling demo with mutex);

/* 无锁版：对比实验，读-写间加延时，观察重复售票 */
static int ticket_demo_nolock(void)
{
    rt_kprintf("=== ticket demo WITHOUT mutex (race enlarged) ===\n");
    return ticket_demo_common(0);
}
MSH_CMD_EXPORT(ticket_demo_nolock, ticket selling demo without mutex for comparison);
