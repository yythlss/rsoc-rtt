/*
 * 文件    : ipc_study.c
 * 项目    : 03_IPC_sample (RT-Thread 4.1.1, STM32F407 RT-SPARK)
 * 内容    : RT-Thread 线程间同步与通信课后作业
 *
 *   练习一 parking_demo  停车场车位管理     —— 计数信号量
 *   练习二 ticket_demo   多窗口售票系统     —— 互斥量（含无锁对比实验 ticket_demo unsafe）
 *   练习三 startup_demo  系统启动条件检查   —— 事件集（AND 等待 + 超时重试）
 *   练习四 package_demo  快递分拣中心       —— 消息队列
 *
 * 说明:
 *   1. 本文件通过 MSH_CMD_EXPORT 导出 4 个 MSH 命令, 全部由命令手动启动;
 *      main.c 只保留最小占位, 不含任何与本作业相关的逻辑.
 *   2. 每个演示中所有线程与 IPC 对象均检查创建结果, 失败时打印错误并回滚.
 *   3. 命令重复执行时, 若上一轮任务仍在运行则拒绝再次启动(运行标志检测);
 *      任务结束后线程入口函数直接 return, 由内核 _thread_exit() 自动回收资源;
 *      IPC 对象采用"惰性创建 + 跨轮复用", 每轮开始时复位到初始状态.
 *   4. 演示输出统一走 demo_printf()(内部加打印互斥量), 避免多线程 rt_kprintf
 *      按字符交错导致乱码.
 */

#include <rtthread.h>
#include <rthw.h>
#include <stdarg.h>

/* 演示线程统一栈大小与优先级(高于 tshell 线程, 保证命令一启动演示线程就能运行) */
#define DEMO_THREAD_STACK_SIZE   1024
#define DEMO_THREAD_PRIORITY     16

/***************************************************************************
 * 公共设施
 **************************************************************************/

/* 打印互斥量: 串行化演示输出, 防止多个线程的 rt_kprintf 相互交错 */
static rt_mutex_t g_print_mutex = RT_NULL;

/* 命令入口处调用一次, 创建打印互斥量(命令由 tshell 单线程执行, 无并发创建竞态) */
static void demo_print_init(void)
{
    if (g_print_mutex == RT_NULL)
    {
        g_print_mutex = rt_mutex_create("prnt", RT_IPC_FLAG_PRIO);
        if (g_print_mutex == RT_NULL)
        {
            rt_kprintf("create print mutex failed\n");
        }
    }
}

/* 原子打印: 先格式化到局部缓冲, 再整段一次性输出 */
static void demo_printf(const char *fmt, ...)
{
    va_list args;
    char buf[160];

    va_start(args, fmt);
    rt_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (g_print_mutex != RT_NULL)
    {
        rt_mutex_take(g_print_mutex, RT_WAITING_FOREVER);
    }
    rt_kprintf("%s", buf);
    if (g_print_mutex != RT_NULL)
    {
        rt_mutex_release(g_print_mutex);
    }
}

/*
 * 计数"已结束"的演示线程数, 由最后一个结束的线程负责打印总结并复位运行标志
 * (用临界区保护计数, 避免并发自增丢失).
 */
static rt_uint8_t demo_finish_one(volatile rt_uint8_t *done_count, rt_uint8_t total)
{
    rt_base_t level;
    rt_uint8_t is_last;

    level = rt_hw_interrupt_disable();
    (*done_count)++;
    is_last = (*done_count >= total);
    rt_hw_interrupt_enable(level);

    return is_last;
}

/* 按名字删除可能残留的线程(仅用于启动失败时回滚"其它"线程) */
static void demo_thread_delete_by_name(const char *name)
{
    rt_thread_t tid = rt_thread_find((char *)name);

    if (tid != RT_NULL)
    {
        rt_thread_delete(tid);
    }
}

/***************************************************************************
 * 练习一: 停车场车位管理(计数信号量)
 *
 * 3 个车位, 5 辆车. 计数信号量初值为 3, 表示剩余车位.
 * 每辆车拿不到车位最多等 2 秒, 超时打印等待信息后稍后再试;
 * 拿到车位打印进入信息, 停 3 秒后离开并释放车位.
 **************************************************************************/
#define PARK_TOTAL_SPACES        3          /* 车位总数 */
#define PARK_TOTAL_CARS          5          /* 车辆总数 */
#define PARK_TAKE_TIMEOUT_MS     2000       /* 申请车位的等待上限 */
#define PARK_RETRY_MS            100        /* 超时后重新申请的间隔 */
#define PARK_STAY_MS             3000       /* 车辆停留时间 */
#define PARK_ARRIVAL_GAP_MS      300        /* 车辆到达的错峰间隔 */

static rt_sem_t   g_park_sem      = RT_NULL; /* 计数信号量: 剩余车位 */
static rt_uint8_t g_park_running  = 0;       /* 演示运行标志 */
static rt_uint8_t g_car_done      = 0;       /* 已离场的车辆数 */

/* 车辆线程: 参数为车辆编号(1~5) */
static void car_thread_entry(void *parameter)
{
    rt_uint8_t car_no = (rt_uint8_t)(rt_base_t)parameter;

    /* 错峰到达, 模拟 5 辆车先后开到停车场 */
    rt_thread_mdelay((car_no - 1) * PARK_ARRIVAL_GAP_MS);

    while (1)
    {
        /* 申请一个车位, 最多等 2 秒 */
        if (rt_sem_take(g_park_sem, rt_tick_from_millisecond(PARK_TAKE_TIMEOUT_MS)) != RT_EOK)
        {
            /* 2 秒内没有拿到车位: 打印等待信息, 稍后重新申请 */
            demo_printf("car%d waiting for a space\n", car_no);
            rt_thread_mdelay(PARK_RETRY_MS);
            continue;
        }

        /*
         * 拿到车位. 已用车位数 = 总车位 - 剩余车位.
         * 信号量 value 字段为公共结构体字段, 表示当前剩余车位.
         */
        demo_printf("car%d entered, used spaces: %d\n",
                    car_no, PARK_TOTAL_SPACES - (int)g_park_sem->value);

        /* 停留一段时间后离开, 释放车位 */
        rt_thread_mdelay(PARK_STAY_MS);
        rt_sem_release(g_park_sem);
        demo_printf("car%d left\n", car_no);
        break;
    }

    /* 全部车辆离场后允许再次执行命令 */
    if (demo_finish_one(&g_car_done, PARK_TOTAL_CARS))
    {
        g_park_running = 0;
        demo_printf("parking demo finished\n");
    }

    /* 直接返回, 内核 _thread_exit() 会回收本线程 */
}

/* MSH 命令: parking_demo */
static void parking_demo(int argc, char **argv)
{
    rt_thread_t tid;
    rt_uint8_t i;
    char name[RT_NAME_MAX];

    (void)argv;
    (void)argc;

    if (g_park_running)
    {
        demo_printf("parking demo is already running, please wait for it to finish\n");
        return;
    }

    demo_print_init();
    demo_printf("parking demo started: %d spaces, %d cars\n",
                PARK_TOTAL_SPACES, PARK_TOTAL_CARS);

    /* 惰性创建计数信号量, 初始值 3 表示剩余车位 */
    if (g_park_sem == RT_NULL)
    {
        g_park_sem = rt_sem_create("park", PARK_TOTAL_SPACES, RT_IPC_FLAG_FIFO);
        if (g_park_sem == RT_NULL)
        {
            demo_printf("create parking semaphore failed\n");
            return;
        }
    }

    /* 防御: 把信号量恢复到初始值(上一轮异常退出时可能残留未释放车位) */
    while (rt_sem_take(g_park_sem, 0) == RT_EOK)
    {
    }
    for (i = 0; i < PARK_TOTAL_SPACES; i++)
    {
        rt_sem_release(g_park_sem);
    }

    g_park_running = 1;
    g_car_done     = 0;

    /* 创建 5 个车辆线程, 逐一检查创建结果 */
    for (i = 1; i <= PARK_TOTAL_CARS; i++)
    {
        rt_snprintf(name, sizeof(name), "car%d", i);
        tid = rt_thread_create(name, car_thread_entry, (void *)(rt_base_t)i,
                               DEMO_THREAD_STACK_SIZE, DEMO_THREAD_PRIORITY, 10);
        if (tid == RT_NULL)
        {
            demo_printf("create thread %s failed\n", name);
            /* 回滚: 删除已创建线程, 复位运行标志 */
            while (i > 1)
            {
                i--;
                rt_snprintf(name, sizeof(name), "car%d", i);
                demo_thread_delete_by_name(name);
            }
            g_park_running = 0;
            return;
        }
        rt_thread_startup(tid);
    }
}
MSH_CMD_EXPORT(parking_demo, parking demo: counting semaphore for 3-space parking lot);

/***************************************************************************
 * 练习二: 多窗口售票系统(互斥量 + 无锁对比实验)
 *
 * 窗口 A / 窗口 B 同时出售 20 张票, 剩余票数保存在全局变量中.
 * 加锁版本: 互斥量保护"检查 -> 减票 -> 记录票号"整个过程;
 * 对比版本: 不加锁, 并在读取与修改之间插入延时放大竞态.
 * 两个版本都统计已售、重复票号与剩余票数.
 *
 * 命令: ticket_demo          -> 加锁版本(默认)
 *       ticket_demo unsafe   -> 无锁对比实验
 **************************************************************************/
#define TICKET_TOTAL          20         /* 总票数 */
#define TICKET_RACE_DELAY_MS  10         /* 无锁实验: 读取与修改之间的延时 */
#define TICKET_WINDOW_COUNT   2          /* 售票窗口数 */
#define TICKET_YIELD_MS       1          /* 两次售票之间的让出时间, 使窗口交替 */

static rt_mutex_t  g_ticket_mutex  = RT_NULL; /* 售票互斥量 */
static rt_int32_t  g_ticket_left   = 0;       /* 全局变量: 剩余票数 */
static rt_uint8_t  g_ticket_sold[TICKET_TOTAL]; /* 票号标记: 检测重复售票 */
static rt_uint32_t g_ticket_sold_cnt   = 0;   /* 已售票数 */
static rt_uint32_t g_ticket_dup_cnt    = 0;   /* 重复票号数 */
static rt_uint32_t g_ticket_invalid    = 0;   /* 越界票号数 */
static rt_uint8_t  g_ticket_use_mutex  = 1;   /* 1: 加锁; 0: 无锁对比 */
static rt_uint8_t  g_ticket_window_done = 0;  /* 已退出窗口数 */
static rt_uint8_t  g_ticket_running    = 0;   /* 演示运行标志 */

/* 售票窗口线程: 参数为窗口名(静态字符串, "window A"/"window B") */
static void window_thread_entry(void *parameter)
{
    const char *win = (const char *)parameter;
    rt_int32_t no;

    while (1)
    {
        if (g_ticket_use_mutex)
        {
            /* 加锁: 保护完整的售票过程 */
            if (rt_mutex_take(g_ticket_mutex, RT_WAITING_FOREVER) != RT_EOK)
            {
                continue;
            }
        }

        /* ---------- 售票临界区开始 ---------- */
        if (g_ticket_left > 0)
        {
            if (!g_ticket_use_mutex)
            {
                /* 对比实验: 在"读取余票"与"修改余票"之间加入延时, 放大竞态 */
                rt_thread_mdelay(TICKET_RACE_DELAY_MS);
            }

            no = g_ticket_left;      /* 记录本窗口售出的票号 */
            g_ticket_left--;         /* 修改剩余票数 */

            /* 检错: 票号重复 / 越界(无锁版本会暴露问题) */
            if (no >= 1 && no <= TICKET_TOTAL)
            {
                if (g_ticket_sold[no - 1])
                {
                    g_ticket_dup_cnt++;
                    demo_printf("%s sold ticket %d (duplicate!)\n", win, no);
                }
                else
                {
                    g_ticket_sold[no - 1] = 1;
                    demo_printf("%s sold ticket %d\n", win, no);
                }
            }
            else
            {
                g_ticket_invalid++;
                demo_printf("%s sold invalid ticket %d\n", win, no);
            }
            g_ticket_sold_cnt++;
        }
        /* ---------- 售票临界区结束 ---------- */

        if (g_ticket_use_mutex)
        {
            rt_mutex_release(g_ticket_mutex);
        }

        /* 无余票则本窗口退出 */
        if (g_ticket_left <= 0)
        {
            break;
        }

        /* 让出 CPU, 让两个窗口交替售票, 输出更直观 */
        rt_thread_mdelay(TICKET_YIELD_MS);
    }

    /* 两个窗口都退出后, 由最后退出的窗口打印总结 */
    if (demo_finish_one(&g_ticket_window_done, TICKET_WINDOW_COUNT))
    {
        demo_printf("all tickets sold\n");
        demo_printf("remaining tickets: %d\n", (int)g_ticket_left);
        demo_printf("sold total: %d, duplicate: %d, invalid: %d\n",
                    (int)g_ticket_sold_cnt, (int)g_ticket_dup_cnt, (int)g_ticket_invalid);

        if (g_ticket_use_mutex)
        {
            demo_printf("ticket demo finished: all %d tickets sold, no duplicate\n",
                        TICKET_TOTAL);
        }
        else
        {
            demo_printf("unsafe demo finished: race exposed duplicate/wrong remaining\n");
        }
        g_ticket_running = 0;
    }
}

/* MSH 命令: ticket_demo [unsafe|mutex] */
static void ticket_demo(int argc, char **argv)
{
    rt_thread_t tid;

    /* 解析参数: 默认加锁, "unsafe" 为无锁对比实验 */
    g_ticket_use_mutex = 1;
    if (argc >= 2)
    {
        if (rt_strcmp(argv[1], "unsafe") == 0)
        {
            g_ticket_use_mutex = 0;
        }
        else if (rt_strcmp(argv[1], "mutex") == 0)
        {
            g_ticket_use_mutex = 1;
        }
        else
        {
            demo_printf("usage: ticket_demo [mutex | unsafe]\n");
            return;
        }
    }

    if (g_ticket_running)
    {
        demo_printf("ticket demo is already running, please wait for it to finish\n");
        return;
    }

    demo_print_init();
    demo_printf("ticket demo started: %d tickets, mode = %s\n",
                TICKET_TOTAL, g_ticket_use_mutex ? "mutex" : "unsafe (no mutex)");

    /* 惰性创建互斥量(仅加锁版本需要) */
    if (g_ticket_use_mutex && g_ticket_mutex == RT_NULL)
    {
        g_ticket_mutex = rt_mutex_create("tkmutex", RT_IPC_FLAG_PRIO);
        if (g_ticket_mutex == RT_NULL)
        {
            demo_printf("create ticket mutex failed\n");
            return;
        }
    }

    /* 每轮开始复位全局状态 */
    g_ticket_left        = TICKET_TOTAL;
    g_ticket_sold_cnt    = 0;
    g_ticket_dup_cnt     = 0;
    g_ticket_invalid     = 0;
    g_ticket_window_done = 0;
    g_ticket_running     = 1;
    rt_memset(g_ticket_sold, 0, sizeof(g_ticket_sold));

    /* 创建窗口 A 线程 */
    tid = rt_thread_create("winA", window_thread_entry, (void *)"window A",
                           DEMO_THREAD_STACK_SIZE, DEMO_THREAD_PRIORITY, 10);
    if (tid == RT_NULL)
    {
        demo_printf("create thread winA failed\n");
        g_ticket_running = 0;
        return;
    }
    rt_thread_startup(tid);

    /* 创建窗口 B 线程 */
    tid = rt_thread_create("winB", window_thread_entry, (void *)"window B",
                           DEMO_THREAD_STACK_SIZE, DEMO_THREAD_PRIORITY, 10);
    if (tid == RT_NULL)
    {
        demo_printf("create thread winB failed\n");
        demo_thread_delete_by_name("winA");
        g_ticket_running = 0;
        return;
    }
    rt_thread_startup(tid);
}
MSH_CMD_EXPORT(ticket_demo, ticket demo: mutex-protected ticket selling (ticket_demo unsafe));

/***************************************************************************
 * 练习三: 系统启动条件检查(事件集)
 *
 * 网络/传感器/存储三个模块并行初始化, 主业务线程用 AND 模式等待
 * 三个就绪事件全部到达. 存储模块耗时 5 秒, 主业务线程最多等 3 秒,
 * 超时后打印尚未就绪的模块并继续等待.
 **************************************************************************/
#define EVENT_NET_READY       (1U << 0)
#define EVENT_SENSOR_READY    (1U << 1)
#define EVENT_STORAGE_READY   (1U << 2)
#define EVENT_ALL_READY       (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

#define STARTUP_WAIT_MS       3000      /* 主业务线程单次等待上限 */

#define MODULE_COUNT          3

/* 模块描述表: 名称 / 事件位 / 模拟初始化耗时 */
struct module_desc
{
    const char  *name;
    rt_uint32_t  event_bit;
    rt_uint32_t  init_ms;
};

static const struct module_desc g_modules[MODULE_COUNT] =
{
    { "network", EVENT_NET_READY,     1000 },
    { "sensor",  EVENT_SENSOR_READY,  2000 },
    { "storage", EVENT_STORAGE_READY, 5000 },
};

static rt_event_t g_startup_event   = RT_NULL; /* 启动事件集 */
static rt_uint8_t g_startup_running = 0;       /* 演示运行标志 */

/* 把"尚未就绪"的模块拼成 "a, b" 形式的字符串 */
static const char *missing_modules_str(rt_uint32_t ready_bits)
{
    static char buf[48];
    rt_uint8_t i;
    int len = 0;

    buf[0] = '\0';
    for (i = 0; i < MODULE_COUNT; i++)
    {
        if (!(ready_bits & g_modules[i].event_bit))
        {
            if (len > 0)
            {
                len += rt_snprintf(buf + len, sizeof(buf) - len, ", ");
            }
            len += rt_snprintf(buf + len, sizeof(buf) - len, "%s", g_modules[i].name);
        }
    }
    return buf;
}

/* 模块初始化线程: 参数为模块下标 */
static void module_init_entry(void *parameter)
{
    rt_uint8_t idx = (rt_uint8_t)(rt_base_t)parameter;
    const struct module_desc *mod = &g_modules[idx];

    demo_printf("%s init start\n", mod->name);

    /* 模拟该模块的初始化耗时 */
    rt_thread_mdelay(mod->init_ms);

    /* 发送本模块的就绪事件 */
    if (rt_event_send(g_startup_event, mod->event_bit) != RT_EOK)
    {
        demo_printf("%s send ready event failed\n", mod->name);
    }
    demo_printf("%s ready\n", mod->name);
}

/* 主业务线程: AND 模式等待三个事件全部到达 */
static void business_thread_entry(void *parameter)
{
    rt_uint32_t recved = 0;
    rt_err_t result;

    (void)parameter;

    while (1)
    {
        /*
         * AND 模式 + CLEAR: 三个事件全部到达才返回, 成功时自动清除事件位;
         * 超时时已到达的事件位保留, 下一次等待可以继续用.
         */
        result = rt_event_recv(g_startup_event, EVENT_ALL_READY,
                               RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                               rt_tick_from_millisecond(STARTUP_WAIT_MS), &recved);
        if (result == RT_EOK)
        {
            demo_printf("all modules are ready\n");
            demo_printf("business task started\n");
            break;
        }

        /* 超时: 打印尚未就绪的模块, 然后再次等待 */
        demo_printf("wait modules timeout, %s not ready\n",
                    missing_modules_str(g_startup_event->set));
    }

    g_startup_running = 0;
}

/* MSH 命令: startup_demo */
static void startup_demo(int argc, char **argv)
{
    rt_thread_t tid;
    rt_uint8_t i;
    static const char *init_thread_names[MODULE_COUNT] =
    {
        "netinit", "snrinit", "stoinit",
    };

    (void)argv;
    (void)argc;

    if (g_startup_running)
    {
        demo_printf("startup demo is already running, please wait for it to finish\n");
        return;
    }

    demo_print_init();
    demo_printf("startup demo started: wait all modules ready within %d ms\n",
                STARTUP_WAIT_MS);

    /* 惰性创建事件集 */
    if (g_startup_event == RT_NULL)
    {
        g_startup_event = rt_event_create("bsev", RT_IPC_FLAG_FIFO);
        if (g_startup_event == RT_NULL)
        {
            demo_printf("create startup event set failed\n");
            return;
        }
    }

    /* 防御: 清掉上一轮可能残留的事件位 */
    rt_event_control(g_startup_event, RT_IPC_CMD_RESET, RT_NULL);

    g_startup_running = 1;

    /* 创建三个模块初始化线程, 逐一检查创建结果 */
    for (i = 0; i < MODULE_COUNT; i++)
    {
        tid = rt_thread_create(init_thread_names[i], module_init_entry,
                               (void *)(rt_base_t)i,
                               DEMO_THREAD_STACK_SIZE, DEMO_THREAD_PRIORITY, 10);
        if (tid == RT_NULL)
        {
            demo_printf("create thread %s failed\n", init_thread_names[i]);
            while (i > 0)
            {
                i--;
                demo_thread_delete_by_name(init_thread_names[i]);
            }
            g_startup_running = 0;
            return;
        }
        rt_thread_startup(tid);
    }

    /* 创建主业务线程 */
    tid = rt_thread_create("biz", business_thread_entry, RT_NULL,
                           DEMO_THREAD_STACK_SIZE, DEMO_THREAD_PRIORITY, 10);
    if (tid == RT_NULL)
    {
        demo_printf("create thread biz failed\n");
        for (i = 0; i < MODULE_COUNT; i++)
        {
            demo_thread_delete_by_name(init_thread_names[i]);
        }
        g_startup_running = 0;
        return;
    }
    rt_thread_startup(tid);
}
MSH_CMD_EXPORT(startup_demo, startup demo: event set wait for all modules ready);

/***************************************************************************
 * 练习四: 快递分拣中心(消息队列)
 *
 * 两个发件线程各产生 5 条快递消息(编号唯一), 通过容量为 5 的消息队列
 * 交给分拣线程. 队列满时发送失败会计数并稍后重试, 不会崩溃.
 * 每个发件线程完成后发送一条结束消息(region = 0), 分拣线程收到
 * 两条结束消息后打印统计结果并退出.
 **************************************************************************/
#define PKG_QUEUE_SIZE         5         /* 消息队列容量 */
#define PKG_PER_SENDER         5         /* 每个发件线程的快递数 */
#define PKG_SENDER_COUNT       2         /* 发件线程数 */
#define PKG_END_REGION         0         /* region = 0 表示结束消息 */
#define PKG_SORT_MS            80        /* 分拣一条快递的耗时 */
#define PKG_SEND_RETRY_MS      50        /* 发送失败后的重试间隔 */

struct package_msg
{
    rt_uint32_t id;
    rt_uint8_t  region;                 /* 1 华东, 2 华南, 3 华北, 0 结束 */
    rt_uint16_t weight;
};

static rt_mq_t     g_pkg_mq       = RT_NULL;  /* 快递消息队列 */
static rt_uint8_t  g_pkg_running  = 0;        /* 演示运行标志 */
static rt_uint32_t g_pkg_total    = 0;        /* 快递总数 */
static rt_uint32_t g_pkg_region_cnt[3] = { 0, 0, 0 }; /* 各区域数量 */
static rt_uint32_t g_pkg_weight_total = 0;    /* 总重量 */
static rt_uint32_t g_pkg_send_failed = 0;     /* 发送失败次数 */
static rt_uint8_t  g_pkg_end_received = 0;    /* 收到的结束消息数 */

/* 区域编号转区域名 */
static const char *package_region_name(rt_uint8_t region)
{
    switch (region)
    {
    case 1:
        return "east";
    case 2:
        return "south";
    case 3:
        return "north";
    default:
        return "unknown";
    }
}

/* 发件线程: 参数为发件人编号(1/2) */
static void sender_thread_entry(void *parameter)
{
    rt_uint8_t sender_no = (rt_uint8_t)(rt_base_t)parameter;
    rt_uint32_t base = (rt_uint32_t)sender_no * 1000;  /* 1号: 100x, 2号: 200x */
    struct package_msg pkg;
    rt_uint8_t i;

    for (i = 1; i <= PKG_PER_SENDER; i++)
    {
        pkg.id     = base + i;                            /* 编号全局唯一 */
        pkg.region = (rt_uint8_t)(((sender_no + i) % 3) + 1); /* 区域轮转 */
        pkg.weight = (rt_uint16_t)(800 + i * 100 + sender_no * 50);

        /*
         * 非阻塞发送: 队列满时返回 -RT_EFULL, 计数失败并稍后重试,
         * 发送线程不会阻塞或崩溃.
         */
        while (rt_mq_send(g_pkg_mq, &pkg, sizeof(pkg)) != RT_EOK)
        {
            g_pkg_send_failed++;
            rt_thread_mdelay(PKG_SEND_RETRY_MS);
        }
    }

    /* 本线程的快递全部发出后, 发送一条结束消息 */
    pkg.id     = 0;
    pkg.region = PKG_END_REGION;
    pkg.weight = 0;
    while (rt_mq_send(g_pkg_mq, &pkg, sizeof(pkg)) != RT_EOK)
    {
        rt_thread_mdelay(PKG_SEND_RETRY_MS);
    }
}

/* 分拣线程: 取出快递并按区域分类统计 */
static void sorter_thread_entry(void *parameter)
{
    struct package_msg pkg;
    rt_err_t result;

    (void)parameter;

    while (1)
    {
        result = rt_mq_recv(g_pkg_mq, &pkg, sizeof(pkg), RT_WAITING_FOREVER);
        if (result != RT_EOK)
        {
            demo_printf("recv package failed, error %d\n", result);
            continue;
        }

        /* 结束消息: 两个发件线程都结束后统计并退出 */
        if (pkg.region == PKG_END_REGION)
        {
            g_pkg_end_received++;
            if (g_pkg_end_received >= PKG_SENDER_COUNT)
            {
                break;
            }
            continue;
        }

        /* 分拣打印 */
        demo_printf("package %d -> %s, weight: %d g\n",
                    (int)pkg.id, package_region_name(pkg.region), (int)pkg.weight);

        /* 分类统计 */
        g_pkg_total++;
        if (pkg.region >= 1 && pkg.region <= 3)
        {
            g_pkg_region_cnt[pkg.region - 1]++;
        }
        g_pkg_weight_total += pkg.weight;

        /* 模拟分拣耗时: 让队列被填满的情况真实出现 */
        rt_thread_mdelay(PKG_SORT_MS);
    }

    /* 打印统计结果 */
    demo_printf("total packages: %d\n", (int)g_pkg_total);
    demo_printf("east: %d, south: %d, north: %d\n",
                (int)g_pkg_region_cnt[0], (int)g_pkg_region_cnt[1],
                (int)g_pkg_region_cnt[2]);
    demo_printf("total weight: %d g\n", (int)g_pkg_weight_total);
    demo_printf("send failed: %d\n", (int)g_pkg_send_failed);

    g_pkg_running = 0;
}

/* MSH 命令: package_demo */
static void package_demo(int argc, char **argv)
{
    rt_thread_t threads[PKG_SENDER_COUNT + 1];  /* 2 个发件 + 1 个分拣 */
    rt_uint8_t created = 0;
    rt_uint8_t i;
    static const char *thread_names[PKG_SENDER_COUNT + 1] =
    {
        "sndr1", "sndr2", "sorter",
    };

    (void)argv;
    (void)argc;

    if (g_pkg_running)
    {
        demo_printf("package demo is already running, please wait for it to finish\n");
        return;
    }

    demo_print_init();
    demo_printf("package demo started: %d senders x %d packages, queue size %d\n",
                PKG_SENDER_COUNT, PKG_PER_SENDER, PKG_QUEUE_SIZE);

    /* 惰性创建消息队列: 每条消息 sizeof(struct package_msg), 容量 5 */
    if (g_pkg_mq == RT_NULL)
    {
        g_pkg_mq = rt_mq_create("pkgmq", sizeof(struct package_msg),
                                PKG_QUEUE_SIZE, RT_IPC_FLAG_FIFO);
        if (g_pkg_mq == RT_NULL)
        {
            demo_printf("create package message queue failed\n");
            return;
        }
    }

    /* 每轮开始复位统计量 */
    g_pkg_total         = 0;
    g_pkg_region_cnt[0] = 0;
    g_pkg_region_cnt[1] = 0;
    g_pkg_region_cnt[2] = 0;
    g_pkg_weight_total  = 0;
    g_pkg_send_failed   = 0;
    g_pkg_end_received  = 0;
    g_pkg_running       = 1;

    /*
     * 先全部创建、后统一启动: 任一创建失败都不会产生半截数据,
     * 回滚时只需删除已创建但未启动的线程.
     */
    for (i = 0; i < PKG_SENDER_COUNT; i++)
    {
        threads[i] = rt_thread_create(thread_names[i], sender_thread_entry,
                                      (void *)(rt_base_t)(i + 1),
                                      DEMO_THREAD_STACK_SIZE, DEMO_THREAD_PRIORITY, 10);
        if (threads[i] == RT_NULL)
        {
            demo_printf("create thread %s failed\n", thread_names[i]);
            break;
        }
        created++;
    }

    if (created == PKG_SENDER_COUNT)
    {
        threads[PKG_SENDER_COUNT] = rt_thread_create(thread_names[PKG_SENDER_COUNT],
                                                     sorter_thread_entry, RT_NULL,
                                                     DEMO_THREAD_STACK_SIZE,
                                                     DEMO_THREAD_PRIORITY, 10);
        if (threads[PKG_SENDER_COUNT] == RT_NULL)
        {
            demo_printf("create thread %s failed\n", thread_names[PKG_SENDER_COUNT]);
        }
        else
        {
            created++;
        }
    }

    /* 创建不完整: 回滚并复位运行标志 */
    if (created != PKG_SENDER_COUNT + 1)
    {
        for (i = 0; i < created; i++)
        {
            rt_thread_delete(threads[i]);
        }
        g_pkg_running = 0;
        return;
    }

    /* 全部创建成功, 统一启动 */
    for (i = 0; i < PKG_SENDER_COUNT + 1; i++)
    {
        rt_thread_startup(threads[i]);
    }
}
MSH_CMD_EXPORT(package_demo, package demo: message queue sorting center);
