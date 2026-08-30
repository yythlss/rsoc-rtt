/*
 * 练习二：多窗口售票系统
 *
 * 两个窗口同时售票，互斥量保护剩余票数的读改写过程。
 */
#include <rtthread.h>

/* 票务配置 */
#define TOTAL_TICKETS               20U
#define WINDOW_COUNT                2U
#define TICKET_MUTEX_NAME           "ticket_mtx"

/* 窗口线程配置 */
#define WINDOW_THREAD_STACK_SIZE    1024U
#define WINDOW_THREAD_PRIORITY      20U
#define WINDOW_THREAD_TIMESLICE     5U
#define WINDOW_SELL_INTERVAL_MS     50U
#define TICKET_WAIT_POLL_MS         100U
#define THREAD_EXIT_WAIT_MS         200U

static rt_uint32_t g_remainingTickets = TOTAL_TICKETS;
static rt_mutex_t g_pTicketMutex = RT_NULL;
static rt_thread_t g_pWindowThreads[WINDOW_COUNT];
static rt_bool_t g_demoRunning = RT_FALSE;

static const char *g_windowNames[WINDOW_COUNT] =
{
    "A",
    "B"
};

/************************************************************************
函数名   : sellOneTicket
功能描述:    在互斥保护下售出一张票
输入参数:    pWindowName -- 窗口名称
输出参数:    无
返回值 :    RT_TRUE 售出成功，RT_FALSE 无票可售或失败
************************************************************************/
static rt_bool_t sellOneTicket(const char *pWindowName)
{
    rt_bool_t sold = RT_FALSE;
    rt_uint32_t ticketNo = 0U;
    rt_err_t mutexResult = RT_EOK;

    if (RT_NULL == pWindowName)
    {
        sold = RT_FALSE;
    }
    else if (RT_NULL == g_pTicketMutex)
    {
        rt_kprintf("window %s: ticket mutex is NULL\n", pWindowName);
        sold = RT_FALSE;
    }
    else
    {
        mutexResult = rt_mutex_take(g_pTicketMutex, RT_WAITING_FOREVER);
        if (RT_EOK == mutexResult)
        {
            if (0U < g_remainingTickets)
            {
                ticketNo = g_remainingTickets;
                g_remainingTickets--;
                sold = RT_TRUE;
                rt_kprintf("window %s sold ticket %u\n", pWindowName, ticketNo);
            }
            else
            {
                sold = RT_FALSE;
            }
            rt_mutex_release(g_pTicketMutex);
        }
        else
        {
            rt_kprintf("window %s: mutex take failed\n", pWindowName);
            sold = RT_FALSE;
        }
    }

    return sold;
}

/************************************************************************
函数名   : windowEntry
功能描述:    售票窗口线程入口
输入参数:    pParameter -- 窗口名称指针
输出参数:    无
返回值 :    无
************************************************************************/
static void windowEntry(void *pParameter)
{
    const char *pWindowName = RT_NULL;
    rt_bool_t sold = RT_TRUE;

    if (RT_NULL != pParameter)
    {
        pWindowName = (const char *)pParameter;
    }
    else
    {
        pWindowName = "unknown";
    }

    sold = RT_TRUE;
    while (RT_TRUE == sold)
    {
        sold = sellOneTicket(pWindowName);
        if (RT_TRUE == sold)
        {
            rt_thread_mdelay(WINDOW_SELL_INTERVAL_MS);
        }
        else
        {
            /* 无票可售，线程退出 */
        }
    }
}

/************************************************************************
函数名   : isThreadClosed
功能描述:    判断线程是否已结束
输入参数:    pThread -- 线程句柄
输出参数:    无
返回值 :    RT_TRUE 已结束，RT_FALSE 未结束或句柄为空
************************************************************************/
static rt_bool_t isThreadClosed(rt_thread_t pThread)
{
    rt_bool_t closed = RT_FALSE;
    rt_uint8_t stat = 0U;

    if (RT_NULL == pThread)
    {
        closed = RT_TRUE;
    }
    else
    {
        stat = (rt_uint8_t)(RT_SCHED_CTX(pThread).stat & RT_THREAD_STAT_MASK);
        if (RT_THREAD_CLOSE == stat)
        {
            closed = RT_TRUE;
        }
        else
        {
            closed = RT_FALSE;
        }
    }

    return closed;
}

/************************************************************************
函数名   : waitTicketsSoldOut
功能描述:    阻塞等待全部车票售完
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void waitTicketsSoldOut(void)
{
    rt_bool_t soldOut = RT_FALSE;

    soldOut = RT_FALSE;
    while (RT_FALSE == soldOut)
    {
        if (0U == g_remainingTickets)
        {
            soldOut = RT_TRUE;
        }
        else
        {
            rt_thread_mdelay(TICKET_WAIT_POLL_MS);
        }
    }
}

/************************************************************************
函数名   : waitWindowsExit
功能描述:    阻塞等待两个窗口线程退出
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void waitWindowsExit(void)
{
    rt_bool_t allClosed = RT_FALSE;
    rt_uint32_t index = 0U;

    allClosed = RT_FALSE;
    while (RT_FALSE == allClosed)
    {
        allClosed = RT_TRUE;
        for (index = 0U; index < WINDOW_COUNT; index++)
        {
            if (RT_FALSE == isThreadClosed(g_pWindowThreads[index]))
            {
                allClosed = RT_FALSE;
                break;
            }
            else
            {
                /* 当前窗口线程已结束，继续检查下一个 */
            }
        }

        if (RT_FALSE == allClosed)
        {
            rt_thread_mdelay(TICKET_WAIT_POLL_MS);
        }
        else
        {
            /* 全部窗口线程已退出 */
        }
    }

    rt_thread_mdelay(THREAD_EXIT_WAIT_MS);
}

/************************************************************************
函数名   : printTicketSummary
功能描述:    打印售票汇总信息
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void printTicketSummary(void)
{
    rt_kprintf("all tickets sold\n");
    rt_kprintf("remaining tickets: %u\n", g_remainingTickets);
}

/************************************************************************
函数名   : createTicketMutex
功能描述:    创建售票互斥量
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createTicketMutex(void)
{
    int result = RT_EOK;

    g_pTicketMutex = rt_mutex_create(TICKET_MUTEX_NAME, RT_IPC_FLAG_PRIO);
    if (RT_NULL == g_pTicketMutex)
    {
        rt_kprintf("create ticket mutex failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_kprintf("ticket mutex created, total tickets: %u\n", TOTAL_TICKETS);
    }

    return result;
}

/************************************************************************
函数名   : createWindowThreads
功能描述:    创建并启动窗口售票线程
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createWindowThreads(void)
{
    int result = RT_EOK;
    rt_uint32_t index = 0U;
    char threadName[RT_NAME_MAX];

    for (index = 0U; index < WINDOW_COUNT; index++)
    {
        rt_snprintf(threadName, sizeof(threadName), "win%c", g_windowNames[index][0]);
        g_pWindowThreads[index] = rt_thread_create(threadName,
                                                  windowEntry,
                                                  (void *)g_windowNames[index],
                                                  WINDOW_THREAD_STACK_SIZE,
                                                  WINDOW_THREAD_PRIORITY,
                                                  WINDOW_THREAD_TIMESLICE);
        if (RT_NULL == g_pWindowThreads[index])
        {
            rt_kprintf("create %s failed.\n", threadName);
            result = -RT_ERROR;
            break;
        }
        else
        {
            rt_thread_startup(g_pWindowThreads[index]);
        }
    }

    return result;
}

static void waitThreadNameReleased(const char *pName)
{
    char threadName[RT_NAME_MAX];

    if (RT_NULL != pName)
    {
        rt_strncpy(threadName, pName, sizeof(threadName));
        threadName[sizeof(threadName) - 1U] = '\0';
        while (RT_NULL != rt_thread_find(threadName))
        {
            rt_thread_mdelay(TICKET_WAIT_POLL_MS);
        }
    }
    else
    {
        /* 无效线程名，不等待 */
    }
}

/************************************************************************
函数名   : cleanupTicketDemo
功能描述:    清理售票演示资源
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void cleanupTicketDemo(void)
{
    rt_uint32_t index = 0U;
    char threadName[RT_NAME_MAX];

    for (index = 0U; index < WINDOW_COUNT; index++)
    {
        if (RT_NULL != g_pWindowThreads[index])
        {
            rt_snprintf(threadName, sizeof(threadName), "win%c", g_windowNames[index][0]);
            g_pWindowThreads[index] = RT_NULL;
            waitThreadNameReleased(threadName);
        }
        else
        {
            /* 线程未创建，无需等待回收 */
        }
    }

    if (RT_NULL != g_pTicketMutex)
    {
        rt_mutex_delete(g_pTicketMutex);
        g_pTicketMutex = RT_NULL;
    }
    else
    {
        /* 互斥量未创建，无需删除 */
    }

    g_demoRunning = RT_FALSE;
}

/************************************************************************
函数名   : ticketDemo
功能描述:    售票演示入口，阻塞等待售完后打印汇总
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int ticketDemo(void)
{
    int result = RT_EOK;

    if (RT_TRUE == g_demoRunning)
    {
        rt_kprintf("ticket demo already running.\n");
        result = -RT_ERROR;
    }
    else
    {
        g_demoRunning = RT_TRUE;
        g_remainingTickets = TOTAL_TICKETS;
        result = createTicketMutex();
    }

    if (RT_EOK == result)
    {
        result = createWindowThreads();
    }
    else
    {
        /* 互斥量创建失败，跳过后续步骤 */
    }

    if (RT_EOK == result)
    {
        waitTicketsSoldOut();
        waitWindowsExit();
        printTicketSummary();
    }
    else
    {
        /* 创建失败时，仍等待已启动的窗口线程结束 */
        waitWindowsExit();
    }

    cleanupTicketDemo();

    return result;
}

int ticket_demo(void)
{
    return ticketDemo();
}

MSH_CMD_EXPORT(ticket_demo, ticket selling demo);
