/*
 * 练习一：停车场车位管理
 *
 * 3 个车位，5 辆车竞争进入；使用计数信号量管理有限资源。
 */
#include <rtthread.h>

/* 停车场配置 */
#define PARKING_CAPACITY            3U
#define CAR_COUNT                   5U
#define PARKING_SEM_NAME            "park_sem"
#define PARKING_SEM_INIT_VALUE      PARKING_CAPACITY

/* 车辆线程配置 */
#define CAR_THREAD_STACK_SIZE       1024U
#define CAR_THREAD_PRIORITY         20U
#define CAR_THREAD_TIMESLICE        5U

/* 时间配置 */
#define PARKING_TIMEOUT_MS          2000U
#define CAR_ARRIVAL_STAGGER_MS      300U
#define CAR_STAY_MS                 4000U
#define CAR_RETRY_DELAY_MS          500U
#define THREAD_WAIT_POLL_MS         100U
#define THREAD_EXIT_WAIT_MS         200U

static rt_sem_t g_pParkingSem = RT_NULL;
static rt_thread_t g_pCarThreads[CAR_COUNT];
static rt_bool_t g_demoRunning = RT_FALSE;
static rt_uint32_t g_carIdTable[CAR_COUNT] =
{
    1U, 2U, 3U, 4U, 5U
};

/************************************************************************
函数名   : getUsedSpaces
功能描述:    获取当前已占用车位数
输入参数:    无
输出参数:    无
返回值 :    已占用车位数
************************************************************************/
static rt_uint32_t getUsedSpaces(void)
{
    rt_uint32_t usedSpaces = 0U;
    rt_uint32_t remainSpaces = 0U;

    if (RT_NULL != g_pParkingSem)
    {
        remainSpaces = (rt_uint32_t)g_pParkingSem->value;
        usedSpaces = PARKING_CAPACITY - remainSpaces;
    }
    else
    {
        usedSpaces = 0U;
    }

    return usedSpaces;
}

/************************************************************************
函数名   : carEntry
功能描述:    车辆线程入口，申请车位、停留、离开
输入参数:    pParameter -- 车辆编号指针
输出参数:    无
返回值 :    无
************************************************************************/
static void carEntry(void *pParameter)
{
    rt_uint32_t carId = 0U;
    rt_err_t takeResult = RT_EOK;
    rt_bool_t parked = RT_FALSE;
    rt_uint32_t *pCarId = RT_NULL;

    if (RT_NULL != pParameter)
    {
        pCarId = (rt_uint32_t *)pParameter;
        carId = *pCarId;
    }
    else
    {
        carId = 0U;
    }

    if (0U < carId)
    {
        rt_thread_mdelay(carId * CAR_ARRIVAL_STAGGER_MS);
    }
    else
    {
        /* 无效车辆编号，不处理 */
    }

    while (RT_FALSE == parked)
    {
        if (RT_NULL == g_pParkingSem)
        {
            rt_kprintf("car%u: parking semaphore is NULL\n", carId);
            break;
        }
        else
        {
            takeResult = rt_sem_take(g_pParkingSem,
                                     rt_tick_from_millisecond(PARKING_TIMEOUT_MS));
            if (-RT_ETIMEOUT == takeResult)
            {
                rt_kprintf("car%u waiting for a space\n", carId);
                rt_thread_mdelay(CAR_RETRY_DELAY_MS);
            }
            else if (RT_EOK == takeResult)
            {
                rt_kprintf("car%u entered, used spaces: %u\n",
                           carId, getUsedSpaces());
                rt_thread_mdelay(CAR_STAY_MS);
                rt_kprintf("car%u left\n", carId);
                rt_sem_release(g_pParkingSem);
                parked = RT_TRUE;
            }
            else
            {
                rt_kprintf("car%u take semaphore failed\n", carId);
                rt_thread_mdelay(CAR_RETRY_DELAY_MS);
            }
        }
    }
}

/************************************************************************
函数名   : createParkingSem
功能描述:    创建停车场计数信号量
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createParkingSem(void)
{
    int result = RT_EOK;

    g_pParkingSem = rt_sem_create(PARKING_SEM_NAME,
                                  PARKING_SEM_INIT_VALUE,
                                  RT_IPC_FLAG_PRIO);
    if (RT_NULL == g_pParkingSem)
    {
        rt_kprintf("create parking semaphore failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_kprintf("parking semaphore created, spaces: %u\n", PARKING_CAPACITY);
    }

    return result;
}

/************************************************************************
函数名   : createCarThreads
功能描述:    创建并启动车辆线程
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createCarThreads(void)
{
    int result = RT_EOK;
    rt_uint32_t index = 0U;
    char threadName[RT_NAME_MAX];

    for (index = 0U; index < CAR_COUNT; index++)
    {
        rt_snprintf(threadName, sizeof(threadName), "car%u", g_carIdTable[index]);
        g_pCarThreads[index] = rt_thread_create(threadName,
                                              carEntry,
                                              &g_carIdTable[index],
                                              CAR_THREAD_STACK_SIZE,
                                              CAR_THREAD_PRIORITY,
                                              CAR_THREAD_TIMESLICE);
        if (RT_NULL == g_pCarThreads[index])
        {
            rt_kprintf("create %s failed.\n", threadName);
            result = -RT_ERROR;
            break;
        }
        else
        {
            rt_thread_startup(g_pCarThreads[index]);
        }
    }

    return result;
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
函数名   : waitCarsExit
功能描述:    阻塞等待全部车辆线程退出
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void waitCarsExit(void)
{
    rt_bool_t allClosed = RT_FALSE;
    rt_uint32_t index = 0U;

    allClosed = RT_FALSE;
    while (RT_FALSE == allClosed)
    {
        allClosed = RT_TRUE;
        for (index = 0U; index < CAR_COUNT; index++)
        {
            if (RT_FALSE == isThreadClosed(g_pCarThreads[index]))
            {
                allClosed = RT_FALSE;
                break;
            }
            else
            {
                /* 当前车辆线程已结束，继续检查下一个 */
            }
        }

        if (RT_FALSE == allClosed)
        {
            rt_thread_mdelay(THREAD_WAIT_POLL_MS);
        }
        else
        {
            /* 全部车辆线程已退出 */
        }
    }

    rt_thread_mdelay(THREAD_EXIT_WAIT_MS);
}

/************************************************************************
函数名   : waitThreadNameReleased
功能描述:    等待动态线程被 idle 回收（自然退出后不得 rt_thread_delete）
输入参数:    pName -- 线程名
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

/************************************************************************
函数名   : cleanupParkingDemo
功能描述:    清理停车场演示资源
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void cleanupParkingDemo(void)
{
    rt_uint32_t index = 0U;
    char threadName[RT_NAME_MAX];

    for (index = 0U; index < CAR_COUNT; index++)
    {
        if (RT_NULL != g_pCarThreads[index])
        {
            rt_snprintf(threadName, sizeof(threadName), "car%u", g_carIdTable[index]);
            g_pCarThreads[index] = RT_NULL;
            waitThreadNameReleased(threadName);
        }
        else
        {
            /* 线程未创建，无需等待回收 */
        }
    }

    if (RT_NULL != g_pParkingSem)
    {
        rt_sem_delete(g_pParkingSem);
        g_pParkingSem = RT_NULL;
    }
    else
    {
        /* 信号量未创建，无需删除 */
    }

    g_demoRunning = RT_FALSE;
}

/************************************************************************
函数名   : parkingDemo
功能描述:    停车场演示入口，创建信号量与车辆线程
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int parkingDemo(void)
{
    int result = RT_EOK;

    if (RT_TRUE == g_demoRunning)
    {
        rt_kprintf("parking demo already running.\n");
        result = -RT_ERROR;
    }
    else
    {
        g_demoRunning = RT_TRUE;
        result = createParkingSem();
    }

    if (RT_EOK == result)
    {
        result = createCarThreads();
    }
    else
    {
        /* 信号量创建失败，跳过后续步骤 */
    }

    if (RT_EOK == result)
    {
        waitCarsExit();
    }
    else
    {
        /* 创建失败时，仍等待已启动的车辆线程结束 */
        waitCarsExit();
    }

    cleanupParkingDemo();

    return result;
}

int parking_demo(void)
{
    return parkingDemo();
}

MSH_CMD_EXPORT(parking_demo, parking lot demo);
