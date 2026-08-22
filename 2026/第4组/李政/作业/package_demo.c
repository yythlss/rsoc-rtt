/*
 * 练习四：快递分拣中心
 *
 * 两个发件线程向消息队列投递快递信息，分拣线程分类统计。
 */
#include <rtthread.h>

struct package_msg
{
    rt_uint32_t id;      /* 快递编号 */
    rt_uint8_t region;   /* 目的地区域 */
    rt_uint16_t weight;  /* 重量(g) */
};

/* 区域定义 */
#define REGION_EAST             1U
#define REGION_SOUTH            2U
#define REGION_NORTH            3U

/* 结束消息标记 */
#define PKG_ID_END              0U

/* 消息队列配置 */
#define PACKAGE_MQ_NAME         "pkg_mq"
#define MQ_MAX_MSGS             5U
#define PACKAGE_MSG_SIZE        sizeof(struct package_msg)

/* 发件配置 */
#define PACKAGES_PER_SENDER     5U
#define SENDER_COUNT            2U
#define SENDER1_ID_BASE         1000U
#define SENDER2_ID_BASE         2000U

/* 线程配置 */
#define SENDER_THREAD_STACK_SIZE    1024U
#define SORT_THREAD_STACK_SIZE      1024U
#define SENDER_THREAD_PRIORITY      20U
#define SORT_THREAD_PRIORITY        15U
#define THREAD_TIMESLICE            5U
#define SEND_INTERVAL_MS            30U
#define SEND_RETRY_MS               10U
#define THREAD_WAIT_POLL_MS         100U
#define THREAD_EXIT_WAIT_MS         200U

typedef struct
{
    rt_uint8_t region;   /* 目的地区域 */
    rt_uint16_t weight;  /* 重量(g) */
} PackageAttr;

static rt_mq_t g_pPackageMq = RT_NULL;
static rt_mutex_t g_pSendFailMutex = RT_NULL;
static rt_thread_t g_pSenderThreads[SENDER_COUNT];
static rt_thread_t g_pSortThread = RT_NULL;
static rt_uint32_t g_sendFailedCount = 0U;
static rt_bool_t g_demoRunning = RT_FALSE;

static rt_uint32_t g_senderNoTable[SENDER_COUNT] =
{
    1U, 2U
};

/* 发件线程1：1001~1005 */
static const PackageAttr g_pkgAttrsSender1[PACKAGES_PER_SENDER] =
{
    { REGION_EAST,  1200U },
    { REGION_NORTH,  960U },
    { REGION_SOUTH,  850U },
    { REGION_EAST,  1100U },
    { REGION_NORTH, 1050U }
};

/* 发件线程2：2001~2005 */
static const PackageAttr g_pkgAttrsSender2[PACKAGES_PER_SENDER] =
{
    { REGION_SOUTH,  850U },
    { REGION_EAST,   980U },
    { REGION_NORTH,  990U },
    { REGION_SOUTH,  920U },
    { REGION_EAST,  1350U }
};

static const char *getRegionName(rt_uint8_t region)
{
    const char *pName = "unknown";

    if (REGION_EAST == region)
    {
        pName = "east";
    }
    else if (REGION_SOUTH == region)
    {
        pName = "south";
    }
    else if (REGION_NORTH == region)
    {
        pName = "north";
    }
    else
    {
        pName = "unknown";
    }

    return pName;
}

static void incSendFailedCount(void)
{
    if (RT_NULL != g_pSendFailMutex)
    {
        if (RT_EOK == rt_mutex_take(g_pSendFailMutex, RT_WAITING_FOREVER))
        {
            g_sendFailedCount++;
            rt_mutex_release(g_pSendFailMutex);
        }
        else
        {
            /* 互斥量获取失败，忽略计数 */
        }
    }
    else
    {
        g_sendFailedCount++;
    }
}

static rt_bool_t sendPackageMsg(struct package_msg *pMsg)
{
    rt_bool_t sent = RT_FALSE;
    rt_err_t sendResult = RT_EOK;

    if (RT_NULL == pMsg)
    {
        sent = RT_FALSE;
    }
    else if (RT_NULL == g_pPackageMq)
    {
        sent = RT_FALSE;
    }
    else
    {
        sendResult = rt_mq_send(g_pPackageMq, pMsg, PACKAGE_MSG_SIZE);
        if (RT_EOK == sendResult)
        {
            sent = RT_TRUE;
        }
        else
        {
            incSendFailedCount();
            sent = RT_FALSE;
        }
    }

    return sent;
}

static void sendPackageMsgWait(struct package_msg *pMsg)
{
    rt_bool_t sent = RT_FALSE;

    if (RT_NULL != pMsg)
    {
        sent = sendPackageMsg(pMsg);
        while (RT_FALSE == sent)
        {
            rt_thread_mdelay(SEND_RETRY_MS);
            sent = sendPackageMsg(pMsg);
        }
    }
    else
    {
        /* 无效消息指针，不发送 */
    }
}

static int fillPackageMsg(rt_uint32_t senderNo, rt_uint32_t seq, struct package_msg *pMsg)
{
    int result = RT_EOK;
    rt_uint32_t index = 0U;
    const PackageAttr *pAttr = RT_NULL;

    if (RT_NULL == pMsg)
    {
        result = -RT_ERROR;
    }
    else if ((0U == seq) || (PACKAGES_PER_SENDER < seq))
    {
        result = -RT_ERROR;
    }
    else
    {
        index = seq - 1U;
        if (1U == senderNo)
        {
            pMsg->id = SENDER1_ID_BASE + seq;
            pAttr = &g_pkgAttrsSender1[index];
        }
        else if (2U == senderNo)
        {
            pMsg->id = SENDER2_ID_BASE + seq;
            pAttr = &g_pkgAttrsSender2[index];
        }
        else
        {
            result = -RT_ERROR;
        }

        if (RT_EOK == result)
        {
            pMsg->region = pAttr->region;
            pMsg->weight = pAttr->weight;
        }
        else
        {
            /* 无效发件编号 */
        }
    }

    return result;
}

static void senderEntry(void *pParameter)
{
    rt_uint32_t senderNo = 0U;
    rt_uint32_t seq = 0U;
    struct package_msg msg;
    rt_uint32_t *pSenderNo = RT_NULL;

    if (RT_NULL != pParameter)
    {
        pSenderNo = (rt_uint32_t *)pParameter;
        senderNo = *pSenderNo;
    }
    else
    {
        senderNo = 0U;
    }

    if ((1U == senderNo) || (2U == senderNo))
    {
        for (seq = 1U; seq <= PACKAGES_PER_SENDER; seq++)
        {
            if (RT_EOK == fillPackageMsg(senderNo, seq, &msg))
            {
                sendPackageMsgWait(&msg);
                rt_thread_mdelay(SEND_INTERVAL_MS);
            }
            else
            {
                /* 填充消息失败，跳过 */
            }
        }

        msg.id = PKG_ID_END;
        msg.region = 0U;
        msg.weight = 0U;
        sendPackageMsgWait(&msg);
    }
    else
    {
        rt_kprintf("invalid sender number: %u\n", senderNo);
    }
}

static void sortEntry(void *pParameter)
{
    struct package_msg msg;
    rt_uint32_t endCount = 0U;
    rt_uint32_t totalCount = 0U;
    rt_uint32_t eastCount = 0U;
    rt_uint32_t southCount = 0U;
    rt_uint32_t northCount = 0U;
    rt_uint32_t totalWeight = 0U;
    rt_ssize_t recvLen = 0;

    RT_UNUSED(pParameter);

    if (RT_NULL == g_pPackageMq)
    {
        rt_kprintf("package mq is NULL\n");
    }
    else
    {
        while (endCount < SENDER_COUNT)
        {
            recvLen = rt_mq_recv(g_pPackageMq, &msg, PACKAGE_MSG_SIZE, RT_WAITING_FOREVER);
            if (0 < recvLen)
            {
                if (PKG_ID_END == msg.id)
                {
                    endCount++;
                }
                else
                {
                    rt_kprintf("package %u -> %s, weight: %u g\n",
                               msg.id,
                               getRegionName(msg.region),
                               msg.weight);
                    totalCount++;
                    totalWeight += (rt_uint32_t)msg.weight;

                    if (REGION_EAST == msg.region)
                    {
                        eastCount++;
                    }
                    else if (REGION_SOUTH == msg.region)
                    {
                        southCount++;
                    }
                    else if (REGION_NORTH == msg.region)
                    {
                        northCount++;
                    }
                    else
                    {
                        /* 未知区域，不计入区域统计 */
                    }
                }
            }
            else
            {
                rt_kprintf("package mq recv failed\n");
                break;
            }
        }

        rt_kprintf("total packages: %u\n", totalCount);
        rt_kprintf("east: %u, south: %u, north: %u\n",
                   eastCount, southCount, northCount);
        rt_kprintf("total weight: %u g\n", totalWeight);
        rt_kprintf("send failed: %u\n", g_sendFailedCount);
    }
}

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

static int createPackageMq(void)
{
    int result = RT_EOK;

    g_pPackageMq = rt_mq_create(PACKAGE_MQ_NAME,
                                PACKAGE_MSG_SIZE,
                                MQ_MAX_MSGS,
                                RT_IPC_FLAG_PRIO);
    if (RT_NULL == g_pPackageMq)
    {
        rt_kprintf("create package mq failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_kprintf("package mq created, capacity: %u\n", MQ_MAX_MSGS);
    }

    return result;
}

static int createSendFailMutex(void)
{
    int result = RT_EOK;

    g_pSendFailMutex = rt_mutex_create("send_fail", RT_IPC_FLAG_PRIO);
    if (RT_NULL == g_pSendFailMutex)
    {
        rt_kprintf("create send fail mutex failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        result = RT_EOK;
    }

    return result;
}

static int createSortThread(void)
{
    int result = RT_EOK;

    g_pSortThread = rt_thread_create("sort",
                                     sortEntry,
                                     RT_NULL,
                                     SORT_THREAD_STACK_SIZE,
                                     SORT_THREAD_PRIORITY,
                                     THREAD_TIMESLICE);
    if (RT_NULL == g_pSortThread)
    {
        rt_kprintf("create sort thread failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_thread_startup(g_pSortThread);
    }

    return result;
}

static int createSenderThreads(void)
{
    int result = RT_EOK;
    rt_uint32_t index = 0U;
    char threadName[RT_NAME_MAX];

    for (index = 0U; index < SENDER_COUNT; index++)
    {
        rt_snprintf(threadName, sizeof(threadName), "sender_%u", g_senderNoTable[index]);
        g_pSenderThreads[index] = rt_thread_create(threadName,
                                                 senderEntry,
                                                 &g_senderNoTable[index],
                                                 SENDER_THREAD_STACK_SIZE,
                                                 SENDER_THREAD_PRIORITY,
                                                 THREAD_TIMESLICE);
        if (RT_NULL == g_pSenderThreads[index])
        {
            rt_kprintf("create %s failed.\n", threadName);
            result = -RT_ERROR;
            break;
        }
        else
        {
            rt_thread_startup(g_pSenderThreads[index]);
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
            rt_thread_mdelay(THREAD_WAIT_POLL_MS);
        }
    }
    else
    {
        /* 无效线程名，不等待 */
    }
}

static void cleanupPackageDemo(void)
{
    rt_uint32_t index = 0U;
    char threadName[RT_NAME_MAX];

    for (index = 0U; index < SENDER_COUNT; index++)
    {
        if (RT_NULL != g_pSenderThreads[index])
        {
            rt_snprintf(threadName, sizeof(threadName), "sender_%u", g_senderNoTable[index]);
            g_pSenderThreads[index] = RT_NULL;
            waitThreadNameReleased(threadName);
        }
        else
        {
            /* 发件线程未创建，无需等待回收 */
        }
    }

    if (RT_NULL != g_pSortThread)
    {
        g_pSortThread = RT_NULL;
        waitThreadNameReleased("sort");
    }
    else
    {
        /* 分拣线程未创建，无需等待回收 */
    }

    if (RT_NULL != g_pPackageMq)
    {
        rt_mq_delete(g_pPackageMq);
        g_pPackageMq = RT_NULL;
    }
    else
    {
        /* 消息队列未创建，无需删除 */
    }

    if (RT_NULL != g_pSendFailMutex)
    {
        rt_mutex_delete(g_pSendFailMutex);
        g_pSendFailMutex = RT_NULL;
    }
    else
    {
        /* 互斥量未创建，无需删除 */
    }

    g_sendFailedCount = 0U;
    g_demoRunning = RT_FALSE;
}

static int packageDemo(void)
{
    int result = RT_EOK;
    rt_uint32_t index = 0U;

    if (RT_TRUE == g_demoRunning)
    {
        rt_kprintf("package demo already running.\n");
        result = -RT_ERROR;
    }
    else
    {
        g_demoRunning = RT_TRUE;
        g_sendFailedCount = 0U;
        result = createPackageMq();
    }

    if (RT_EOK == result)
    {
        result = createSendFailMutex();
    }
    else
    {
        /* 消息队列创建失败 */
    }

    if (RT_EOK == result)
    {
        result = createSortThread();
    }
    else
    {
        /* 互斥量创建失败 */
    }

    if (RT_EOK == result)
    {
        result = createSenderThreads();
    }
    else
    {
        /* 分拣线程创建失败 */
    }

    if (RT_EOK == result)
    {
        waitThreadClosed(g_pSortThread);
        rt_thread_mdelay(THREAD_EXIT_WAIT_MS);

        for (index = 0U; index < SENDER_COUNT; index++)
        {
            waitThreadClosed(g_pSenderThreads[index]);
        }
    }
    else
    {
        /* 创建失败时，仍等待已启动的线程结束 */
        if (RT_NULL != g_pSortThread)
        {
            waitThreadClosed(g_pSortThread);
        }
        else
        {
            /* 分拣线程未创建，无需等待 */
        }

        for (index = 0U; index < SENDER_COUNT; index++)
        {
            if (RT_NULL != g_pSenderThreads[index])
            {
                waitThreadClosed(g_pSenderThreads[index]);
            }
            else
            {
                /* 发件线程未创建，无需等待 */
            }
        }

        rt_thread_mdelay(THREAD_EXIT_WAIT_MS);
    }

    cleanupPackageDemo();

    return result;
}

int package_demo(void)
{
    return packageDemo();
}

MSH_CMD_EXPORT(package_demo, package sorting demo);
