/*
 * 练习三：系统启动条件检查
 *
 * 三个模块并行初始化，主业务线程使用事件 AND 模式等待全部就绪。
 */
#include <rtthread.h>

/* 事件位定义 */
#define EVENT_NET_READY             (1U << 0)
#define EVENT_SENSOR_READY          (1U << 1)
#define EVENT_STORAGE_READY         (1U << 2)
#define EVENT_ALL_READY             (EVENT_NET_READY | EVENT_SENSOR_READY | EVENT_STORAGE_READY)

/* 事件对象配置 */
#define STARTUP_EVENT_NAME          "startup_evt"

/* 模块初始化延时 */
#define NET_INIT_MS                 1000U
#define SENSOR_INIT_MS              1500U
#define STORAGE_INIT_MS             5000U
#define BUSINESS_FIRST_WAIT_MS      3000U

/* 线程配置 */
#define INIT_THREAD_STACK_SIZE      1024U
#define INIT_THREAD_PRIORITY        20U
#define INIT_THREAD_TIMESLICE       5U
#define BUSINESS_THREAD_STACK_SIZE  1024U
#define BUSINESS_THREAD_PRIORITY    15U
#define BUSINESS_THREAD_TIMESLICE   5U
#define THREAD_WAIT_POLL_MS         100U
#define THREAD_EXIT_WAIT_MS         200U

static rt_event_t g_pStartupEvent = RT_NULL;
static rt_thread_t g_pNetInitThread = RT_NULL;
static rt_thread_t g_pSensorInitThread = RT_NULL;
static rt_thread_t g_pStorageInitThread = RT_NULL;
static rt_thread_t g_pBusinessThread = RT_NULL;
static rt_bool_t g_demoRunning = RT_FALSE;

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
函数名   : waitThreadClosed
功能描述:    阻塞等待指定线程退出
输入参数:    pThread -- 线程句柄
输出参数:    无
返回值 :    无
************************************************************************/
static void waitThreadClosed(rt_thread_t pThread)
{
    rt_bool_t closed = RT_FALSE;

    closed = RT_FALSE;
    while (RT_FALSE == closed)
    {
        closed = isThreadClosed(pThread);
        if (RT_FALSE == closed)
        {
            rt_thread_mdelay(THREAD_WAIT_POLL_MS);
        }
        else
        {
            /* 线程已退出 */
        }
    }
}

/************************************************************************
函数名   : isModuleReady
功能描述:    检查指定模块事件位是否已就绪
输入参数:    eventBit -- 模块事件位
输出参数:    无
返回值 :    RT_TRUE 已就绪，RT_FALSE 未就绪或事件对象无效
************************************************************************/
static rt_bool_t isModuleReady(rt_uint32_t eventBit)
{
    rt_bool_t ready = RT_FALSE;
    rt_err_t recvResult = RT_EOK;

    if (RT_NULL == g_pStartupEvent)
    {
        ready = RT_FALSE;
    }
    else
    {
        recvResult = rt_event_recv(g_pStartupEvent,
                                   eventBit,
                                   RT_EVENT_FLAG_AND,
                                   0,
                                   RT_NULL);
        if (RT_EOK == recvResult)
        {
            ready = RT_TRUE;
        }
        else
        {
            ready = RT_FALSE;
        }
    }

    return ready;
}

/************************************************************************
函数名   : printNotReadyModules
功能描述:    打印超时后尚未就绪的模块
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void printNotReadyModules(void)
{
    rt_bool_t netReady = RT_FALSE;
    rt_bool_t sensorReady = RT_FALSE;
    rt_bool_t storageReady = RT_FALSE;

    netReady = isModuleReady(EVENT_NET_READY);
    sensorReady = isModuleReady(EVENT_SENSOR_READY);
    storageReady = isModuleReady(EVENT_STORAGE_READY);

    if (RT_FALSE == netReady)
    {
        rt_kprintf("wait modules timeout, network is not ready\n");
    }
    else
    {
        /* 网络模块已就绪 */
    }

    if (RT_FALSE == sensorReady)
    {
        rt_kprintf("wait modules timeout, sensor is not ready\n");
    }
    else
    {
        /* 传感器模块已就绪 */
    }

    if (RT_FALSE == storageReady)
    {
        rt_kprintf("wait modules timeout, storage is not ready\n");
    }
    else
    {
        /* 存储模块已就绪 */
    }
}

/************************************************************************
函数名   : sendModuleReadyEvent
功能描述:    发送模块就绪事件
输入参数:    eventBit -- 模块事件位
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int sendModuleReadyEvent(rt_uint32_t eventBit)
{
    int result = RT_EOK;
    rt_err_t sendResult = RT_EOK;

    if (RT_NULL == g_pStartupEvent)
    {
        result = -RT_ERROR;
    }
    else
    {
        sendResult = rt_event_send(g_pStartupEvent, eventBit);
        if (RT_EOK == sendResult)
        {
            result = RT_EOK;
        }
        else
        {
            result = -RT_ERROR;
        }
    }

    return result;
}

/************************************************************************
函数名   : netInitEntry
功能描述:    网络模块初始化线程入口
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void netInitEntry(void *pParameter)
{
    RT_UNUSED(pParameter);

    rt_thread_mdelay(NET_INIT_MS);
    rt_kprintf("network ready\n");
    sendModuleReadyEvent(EVENT_NET_READY);
}

/************************************************************************
函数名   : sensorInitEntry
功能描述:    传感器模块初始化线程入口
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void sensorInitEntry(void *pParameter)
{
    RT_UNUSED(pParameter);

    rt_thread_mdelay(SENSOR_INIT_MS);
    rt_kprintf("sensor ready\n");
    sendModuleReadyEvent(EVENT_SENSOR_READY);
}

/************************************************************************
函数名   : storageInitEntry
功能描述:    存储模块初始化线程入口
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void storageInitEntry(void *pParameter)
{
    RT_UNUSED(pParameter);

    rt_thread_mdelay(STORAGE_INIT_MS);
    rt_kprintf("storage ready\n");
    sendModuleReadyEvent(EVENT_STORAGE_READY);
}

/************************************************************************
函数名   : cleanupStartupDemo
功能描述:    清理启动演示资源
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void waitThreadNameReleased(const char *pName)
{
    char threadName[RT_NAME_MAX];

    if (RT_NULL != pName)
    {
        rt_strncpy(threadName, pName, sizeof(threadName));
        threadName[sizeof(threadName) - 1U] = '\0';
        while (RT_NULL != rt_thread_find(threadName))
        {
            rt_thread_mdelay(THREAD_WAIT_POLL_MS);
        }
    }
    else
    {
        /* 无效线程名，不等待 */
    }
}

static void cleanupStartupDemo(void)
{
    if (RT_NULL != g_pStartupEvent)
    {
        rt_event_delete(g_pStartupEvent);
        g_pStartupEvent = RT_NULL;
    }
    else
    {
        /* 事件对象未创建，无需删除 */
    }

    if (RT_NULL != g_pNetInitThread)
    {
        g_pNetInitThread = RT_NULL;
        waitThreadNameReleased("net_init");
    }
    else
    {
        /* 网络初始化线程未创建，无需等待回收 */
    }

    if (RT_NULL != g_pSensorInitThread)
    {
        g_pSensorInitThread = RT_NULL;
        waitThreadNameReleased("sns_init");
    }
    else
    {
        /* 传感器初始化线程未创建，无需等待回收 */
    }

    if (RT_NULL != g_pStorageInitThread)
    {
        g_pStorageInitThread = RT_NULL;
        waitThreadNameReleased("sto_init");
    }
    else
    {
        /* 存储初始化线程未创建，无需等待回收 */
    }

    if (RT_NULL != g_pBusinessThread)
    {
        g_pBusinessThread = RT_NULL;
        waitThreadNameReleased("business");
    }
    else
    {
        /* 业务线程未创建，无需等待回收 */
    }

    g_demoRunning = RT_FALSE;
}

/************************************************************************
函数名   : businessEntry
功能描述:    主业务线程入口，AND 模式等待全部模块就绪
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void businessEntry(void *pParameter)
{
    rt_uint32_t recved = 0U;
    rt_err_t recvResult = RT_EOK;

    RT_UNUSED(pParameter);

    if (RT_NULL == g_pStartupEvent)
    {
        rt_kprintf("startup event is NULL\n");
    }
    else
    {
        recvResult = rt_event_recv(g_pStartupEvent,
                                   EVENT_ALL_READY,
                                   RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                   rt_tick_from_millisecond(BUSINESS_FIRST_WAIT_MS),
                                   &recved);
        if (-RT_ETIMEOUT == recvResult)
        {
            printNotReadyModules();
            recvResult = rt_event_recv(g_pStartupEvent,
                                       EVENT_ALL_READY,
                                       RT_EVENT_FLAG_AND | RT_EVENT_FLAG_CLEAR,
                                       RT_WAITING_FOREVER,
                                       &recved);
        }
        else if (RT_EOK == recvResult)
        {
            /* 首次等待内全部模块已就绪 */
        }
        else
        {
            rt_kprintf("wait modules failed, err=%d\n", recvResult);
        }

        if (RT_EOK == recvResult)
        {
            rt_kprintf("all modules are ready\n");
            rt_kprintf("business task started\n");
        }
        else
        {
            rt_kprintf("business task start failed\n");
        }
    }
}

/************************************************************************
函数名   : createStartupEvent
功能描述:    创建启动事件对象
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createStartupEvent(void)
{
    int result = RT_EOK;

    g_pStartupEvent = rt_event_create(STARTUP_EVENT_NAME, RT_IPC_FLAG_PRIO);
    if (RT_NULL == g_pStartupEvent)
    {
        rt_kprintf("create startup event failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_kprintf("startup event created.\n");
    }

    return result;
}

/************************************************************************
函数名   : createInitThreads
功能描述:    创建并启动三个模块初始化线程
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createInitThreads(void)
{
    int result = RT_EOK;

    g_pNetInitThread = rt_thread_create("net_init",
                                        netInitEntry,
                                        RT_NULL,
                                        INIT_THREAD_STACK_SIZE,
                                        INIT_THREAD_PRIORITY,
                                        INIT_THREAD_TIMESLICE);
    if (RT_NULL == g_pNetInitThread)
    {
        rt_kprintf("create net_init thread failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_thread_startup(g_pNetInitThread);
    }

    if (RT_EOK == result)
    {
        g_pSensorInitThread = rt_thread_create("sns_init",
                                               sensorInitEntry,
                                               RT_NULL,
                                               INIT_THREAD_STACK_SIZE,
                                               INIT_THREAD_PRIORITY,
                                               INIT_THREAD_TIMESLICE);
        if (RT_NULL == g_pSensorInitThread)
        {
            rt_kprintf("create sensor_init thread failed.\n");
            result = -RT_ERROR;
        }
        else
        {
            rt_thread_startup(g_pSensorInitThread);
        }
    }
    else
    {
        /* 前置步骤失败，跳过后续创建 */
    }

    if (RT_EOK == result)
    {
        g_pStorageInitThread = rt_thread_create("sto_init",
                                                storageInitEntry,
                                                RT_NULL,
                                                INIT_THREAD_STACK_SIZE,
                                                INIT_THREAD_PRIORITY,
                                                INIT_THREAD_TIMESLICE);
        if (RT_NULL == g_pStorageInitThread)
        {
            rt_kprintf("create storage_init thread failed.\n");
            result = -RT_ERROR;
        }
        else
        {
            rt_thread_startup(g_pStorageInitThread);
        }
    }
    else
    {
        /* 前置步骤失败，跳过后续创建 */
    }

    return result;
}

/************************************************************************
函数名   : createBusinessThread
功能描述:    创建并启动主业务线程
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createBusinessThread(void)
{
    int result = RT_EOK;

    g_pBusinessThread = rt_thread_create("business",
                                         businessEntry,
                                         RT_NULL,
                                         BUSINESS_THREAD_STACK_SIZE,
                                         BUSINESS_THREAD_PRIORITY,
                                         BUSINESS_THREAD_TIMESLICE);
    if (RT_NULL == g_pBusinessThread)
    {
        rt_kprintf("create business thread failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_thread_startup(g_pBusinessThread);
    }

    return result;
}

/************************************************************************
函数名   : startupDemo
功能描述:    启动演示入口，创建事件与各线程
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int startupDemo(void)
{
    int result = RT_EOK;

    if (RT_TRUE == g_demoRunning)
    {
        rt_kprintf("startup demo already running.\n");
        result = -RT_ERROR;
    }
    else
    {
        g_demoRunning = RT_TRUE;
        result = createStartupEvent();
    }

    if (RT_EOK == result)
    {
        result = createInitThreads();
    }
    else
    {
        /* 事件创建失败，跳过后续步骤 */
    }

    if (RT_EOK == result)
    {
        result = createBusinessThread();
    }
    else
    {
        /* 初始化线程创建失败，跳过后续步骤 */
    }

    if (RT_EOK == result)
    {
        waitThreadClosed(g_pBusinessThread);
    }
    else
    {
        /* 创建失败时，仍等待已启动的线程结束 */
        if (RT_NULL != g_pBusinessThread)
        {
            waitThreadClosed(g_pBusinessThread);
        }
        else
        {
            /* 业务线程未创建，无需等待 */
        }
    }

    if (RT_NULL != g_pNetInitThread)
    {
        waitThreadClosed(g_pNetInitThread);
    }
    else
    {
        /* 网络初始化线程未创建，无需等待 */
    }

    if (RT_NULL != g_pSensorInitThread)
    {
        waitThreadClosed(g_pSensorInitThread);
    }
    else
    {
        /* 传感器初始化线程未创建，无需等待 */
    }

    if (RT_NULL != g_pStorageInitThread)
    {
        waitThreadClosed(g_pStorageInitThread);
    }
    else
    {
        /* 存储初始化线程未创建，无需等待 */
    }

    rt_thread_mdelay(THREAD_EXIT_WAIT_MS);
    cleanupStartupDemo();

    return result;
}

int startup_demo(void)
{
    return startupDemo();
}

MSH_CMD_EXPORT(startup_demo, system startup demo);
