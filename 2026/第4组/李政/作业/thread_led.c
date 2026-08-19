/*
 * 多线程实战：LED 工作线程 + 监控控制线程
 *
 * th_led1    动态创建，翻转 LED_R，打印计数与 tick
 * th_led2    静态初始化，翻转 LED_B，打印计数
 * th_monitor 监控两线程状态/栈，按键切换 th_led1 挂起与恢复
 * th_key     轮询 GPIO_BTN_LEFT，释放信号量通知 monitor
 */
#include <board.h>
#include <rtthread.h>
#include <drv_gpio.h>

#ifndef RT_USING_DM
#define GPIO_LED_B                  GET_PIN(F, 11)
#define GPIO_LED_R                  GET_PIN(F, 12)
#define GPIO_BTN_LEFT               GET_PIN(C, 0)   /* 板载 KEY_LEFT，按下接 GND 为低电平 */
#endif

/* LED 工作线程配置 */
#define LED_THREAD_PRIORITY         16U
#define LED_THREAD_STACK_SIZE       1024U
#define LED_THREAD_TIMESLICE        5U
#define LED_BLINK_INTERVAL_MS       500U

/* 监控线程配置 */
#define MONITOR_THREAD_PRIORITY     10U
#define MONITOR_THREAD_STACK_SIZE   1024U
#define MONITOR_THREAD_TIMESLICE    5U
#define MONITOR_PERIOD_MS           2000U

/* 按键扫描线程配置 */
#define KEY_THREAD_PRIORITY         15U
#define KEY_THREAD_STACK_SIZE       512U
#define KEY_THREAD_TIMESLICE        5U
#define KEY_DEBOUNCE_MS             100U
#define KEY_SCAN_INTERVAL_MS        10U

/* 按键信号量配置 */
#define KEY_SEM_NAME                "key_sem"
#define KEY_SEM_INIT_VALUE          0U
#define STACK_FILL_MARK             '#'

static rt_thread_t g_pThLed1 = RT_NULL;
static rt_thread_t g_pThMonitor = RT_NULL;
static rt_thread_t g_pThKey = RT_NULL;
static rt_sem_t g_pKeySem = RT_NULL;
static rt_bool_t g_led1Suspended = RT_FALSE;

static char g_thLed2Stack[LED_THREAD_STACK_SIZE];
static struct rt_thread g_thLed2;

/************************************************************************
函数名   : getThreadSchedStatStr
功能描述:    获取线程调度状态字符串
输入参数:    stat -- 线程状态值
输出参数:    无
返回值 :    状态字符串指针
************************************************************************/
static const char *getThreadSchedStatStr(rt_uint8_t stat)
{
    const char *pResult = "unknown";
    rt_uint8_t schedStat = (rt_uint8_t)(stat & RT_THREAD_STAT_MASK);

    if (RT_THREAD_READY == schedStat)
    {
        pResult = "ready";
    }
    else if (RT_THREAD_SUSPEND_MASK == (schedStat & RT_THREAD_SUSPEND_MASK))
    {
        pResult = "blocked";
    }
    else if (RT_THREAD_INIT == schedStat)
    {
        pResult = "init";
    }
    else if (RT_THREAD_CLOSE == schedStat)
    {
        pResult = "close";
    }
    else if (RT_THREAD_RUNNING == schedStat)
    {
        pResult = "running";
    }
    else
    {
        pResult = "unknown";
    }

    return pResult;
}

/************************************************************************
函数名   : getThreadStackRemaining
功能描述:    计算线程剩余栈空间
输入参数:    pThread -- 线程句柄
输出参数:    无
返回值 :    剩余栈字节数
************************************************************************/
static rt_uint32_t getThreadStackRemaining(struct rt_thread *pThread)
{
    rt_uint8_t *pPtr = RT_NULL;
    rt_uint32_t remainBytes = 0U;
    rt_bool_t isValid = RT_FALSE;

    if (RT_NULL != pThread)
    {
        if (RT_NULL != pThread->stack_addr)
        {
            if (0U < pThread->stack_size)
            {
                isValid = RT_TRUE;
            }
            else
            {
                isValid = RT_FALSE;
            }
        }
        else
        {
            isValid = RT_FALSE;
        }
    }
    else
    {
        isValid = RT_FALSE;
    }

    if (RT_TRUE == isValid)
    {
#if defined(ARCH_CPU_STACK_GROWS_UPWARD)
        pPtr = (rt_uint8_t *)pThread->stack_addr + pThread->stack_size - 1U;
        while (STACK_FILL_MARK == *pPtr)
        {
            pPtr--;
        }
        remainBytes = (rt_uint32_t)(pPtr - (rt_uint8_t *)pThread->stack_addr);
#else
        pPtr = (rt_uint8_t *)pThread->stack_addr;
        while (STACK_FILL_MARK == *pPtr)
        {
            pPtr++;
        }
        remainBytes = pThread->stack_size -
                      (rt_uint32_t)(pPtr - (rt_uint8_t *)pThread->stack_addr);
#endif
    }
    else
    {
        remainBytes = 0U;
    }

    return remainBytes;
}

/************************************************************************
函数名   : monitorPrintThread
功能描述:    打印线程监控信息
输入参数:    pThread -- 线程句柄
输出参数:    无
返回值 :    无
************************************************************************/
static void monitorPrintThread(struct rt_thread *pThread)
{
    rt_uint8_t stat = 0U;
    const char *pCtrlStr = "n/a";
    rt_bool_t isLed1Thread = RT_FALSE;

    if (RT_NULL == pThread)
    {
        rt_kprintf("[monitor] thread is NULL\n");
    }
    else
    {
        stat = RT_SCHED_CTX(pThread).stat;
        if ((RT_NULL != g_pThLed1) && (pThread == g_pThLed1))
        {
            isLed1Thread = RT_TRUE;
        }
        else
        {
            isLed1Thread = RT_FALSE;
        }

        if (RT_TRUE == isLed1Thread)
        {
            if (RT_TRUE == g_led1Suspended)
            {
                pCtrlStr = "suspended";
            }
            else
            {
                pCtrlStr = "running";
            }
        }
        else
        {
            pCtrlStr = "n/a";
        }

        rt_kprintf("[monitor] %s: ctrl=%s, sched=%s, stack_remain=%u\n",
                   pThread->parent.name,
                   pCtrlStr,
                   getThreadSchedStatStr(stat),
                   getThreadStackRemaining(pThread));
    }
}

/************************************************************************
函数名   : monitorToggleLed1
功能描述:    切换 th_led1 挂起与恢复状态
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void monitorToggleLed1(void)
{
    if (RT_NULL == g_pThLed1)
    {
        rt_kprintf("[monitor] th_led1 is NULL\n");
    }
    else
    {
        if (RT_FALSE == g_led1Suspended)
        {
            rt_thread_suspend(g_pThLed1);
            g_led1Suspended = RT_TRUE;
            rt_kprintf("[monitor] th_led1 suspended\n");
        }
        else
        {
            rt_thread_resume(g_pThLed1);
            g_led1Suspended = RT_FALSE;
            rt_kprintf("[monitor] th_led1 resumed\n");
        }
    }
}

/************************************************************************
函数名   : monitorHandleKey
功能描述:    处理按键信号量，切换 th_led1 挂起与恢复
输入参数:    无
输出参数:    无
返回值 :    无
************************************************************************/
static void monitorHandleKey(void)
{
    rt_err_t takeResult = RT_EOK;

    if (RT_NULL == g_pKeySem)
    {
        rt_kprintf("[monitor] key semaphore is NULL\n");
    }
    else
    {
        takeResult = rt_sem_trytake(g_pKeySem);
        while (RT_EOK == takeResult)
        {
            monitorToggleLed1();
            takeResult = rt_sem_trytake(g_pKeySem);
        }
    }
}

/************************************************************************
函数名   : thKeyEntry
功能描述:    按键扫描线程入口，检测 GPIO_BTN_LEFT 并释放信号量
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void thKeyEntry(void *pParameter)
{
    rt_base_t pinLevel = PIN_HIGH;

    RT_UNUSED(pParameter);

    while (1)
    {
        pinLevel = rt_pin_read(GPIO_BTN_LEFT);
        if (PIN_LOW == pinLevel)
        {
            rt_thread_mdelay(KEY_DEBOUNCE_MS);
            pinLevel = rt_pin_read(GPIO_BTN_LEFT);
            if (PIN_LOW == pinLevel)
            {
                if (RT_NULL != g_pKeySem)
                {
                    rt_sem_release(g_pKeySem);
                    rt_kprintf("[key] GPIO_BTN_LEFT pressed\n");
                }
                else
                {
                    rt_kprintf("[key] key semaphore is NULL\n");
                }

                pinLevel = rt_pin_read(GPIO_BTN_LEFT);
                while (PIN_LOW == pinLevel)
                {
                    rt_thread_mdelay(KEY_SCAN_INTERVAL_MS);
                    pinLevel = rt_pin_read(GPIO_BTN_LEFT);
                }
            }
            else
            {
                /* 消抖失败，忽略本次抖动 */
            }
        }
        else
        {
            /* 按键未按下，继续扫描 */
        }

        rt_thread_mdelay(KEY_SCAN_INTERVAL_MS);
    }
}

/************************************************************************
函数名   : thLed1Entry
功能描述:    工作线程1入口，翻转 LED_R 并打印计数与 tick
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void thLed1Entry(void *pParameter)
{
    rt_uint32_t count = 0U;
    rt_base_t ledLevel = PIN_LOW;

    RT_UNUSED(pParameter);

    while (1)
    {
        if (PIN_LOW == ledLevel)
        {
            ledLevel = PIN_HIGH;
        }
        else
        {
            ledLevel = PIN_LOW;
        }

        rt_pin_write(GPIO_LED_R, ledLevel);
        rt_kprintf("th_led1 count: %u, tick: %u\n", count, rt_tick_get());
        count++;
        rt_thread_mdelay(LED_BLINK_INTERVAL_MS);
    }
}

/************************************************************************
函数名   : thLed2Entry
功能描述:    工作线程2入口，翻转 LED_B 并打印计数
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void thLed2Entry(void *pParameter)
{
    rt_uint32_t count = 0U;
    rt_base_t ledLevel = PIN_LOW;

    RT_UNUSED(pParameter);

    while (1)
    {
        if (PIN_LOW == ledLevel)
        {
            ledLevel = PIN_HIGH;
        }
        else
        {
            ledLevel = PIN_LOW;
        }

        rt_pin_write(GPIO_LED_B, ledLevel);
        rt_kprintf("th_led2 count: %u\n", count);
        count++;
        rt_thread_mdelay(LED_BLINK_INTERVAL_MS);
    }
}

/************************************************************************
函数名   : thMonitorEntry
功能描述:    监控线程入口，周期打印线程状态并处理按键
输入参数:    pParameter -- 线程参数
输出参数:    无
返回值 :    无
************************************************************************/
static void thMonitorEntry(void *pParameter)
{
    rt_err_t takeResult = RT_EOK;

    RT_UNUSED(pParameter);

    while (1)
    {
        if (RT_NULL == g_pKeySem)
        {
            rt_thread_mdelay(MONITOR_PERIOD_MS);
        }
        else
        {
            takeResult = rt_sem_take(g_pKeySem, rt_tick_from_millisecond(MONITOR_PERIOD_MS));
            if (RT_EOK == takeResult)
            {
                monitorToggleLed1();
                monitorHandleKey();
            }
            else
            {
                /* 超时未收到按键，继续周期监控 */
            }
        }

        rt_kprintf("[monitor] ---- thread status ----\n");
        monitorPrintThread(g_pThLed1);
        monitorPrintThread(&g_thLed2);
    }
}

/************************************************************************
函数名   : createLed1Thread
功能描述:    动态创建并启动 th_led1
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createLed1Thread(void)
{
    int result = RT_EOK;

    g_pThLed1 = rt_thread_create("th_led1",
                                 thLed1Entry,
                                 RT_NULL,
                                 LED_THREAD_STACK_SIZE,
                                 LED_THREAD_PRIORITY,
                                 LED_THREAD_TIMESLICE);
    if (RT_NULL == g_pThLed1)
    {
        rt_kprintf("create th_led1 failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_thread_startup(g_pThLed1);
    }

    return result;
}

/************************************************************************
函数名   : createLed2Thread
功能描述:    静态初始化并启动 th_led2
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createLed2Thread(void)
{
    int result = RT_EOK;

    rt_thread_init(&g_thLed2,
                   "th_led2",
                   thLed2Entry,
                   RT_NULL,
                   &g_thLed2Stack[0],
                   sizeof(g_thLed2Stack),
                   LED_THREAD_PRIORITY,
                   LED_THREAD_TIMESLICE);
    rt_thread_startup(&g_thLed2);

    return result;
}

/************************************************************************
函数名   : createKeyThread
功能描述:    动态创建并启动 th_key
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createKeyThread(void)
{
    int result = RT_EOK;

    g_pThKey = rt_thread_create("th_key",
                                thKeyEntry,
                                RT_NULL,
                                KEY_THREAD_STACK_SIZE,
                                KEY_THREAD_PRIORITY,
                                KEY_THREAD_TIMESLICE);
    if (RT_NULL == g_pThKey)
    {
        rt_kprintf("create th_key failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_thread_startup(g_pThKey);
    }

    return result;
}

/************************************************************************
函数名   : createMonitorThread
功能描述:    动态创建并启动 th_monitor
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int createMonitorThread(void)
{
    int result = RT_EOK;

    g_pThMonitor = rt_thread_create("th_monitor",
                                    thMonitorEntry,
                                    RT_NULL,
                                    MONITOR_THREAD_STACK_SIZE,
                                    MONITOR_THREAD_PRIORITY,
                                    MONITOR_THREAD_TIMESLICE);
    if (RT_NULL == g_pThMonitor)
    {
        rt_kprintf("create th_monitor failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        rt_thread_startup(g_pThMonitor);
    }

    return result;
}

/************************************************************************
函数名   : threadLedStart
功能描述:    初始化引脚、信号量并创建启动各工作线程
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int threadLedStart(void)
{
    int result = RT_EOK;

#ifndef RT_USING_DM
    rt_pin_mode(GPIO_LED_R, PIN_MODE_OUTPUT);
    rt_pin_mode(GPIO_LED_B, PIN_MODE_OUTPUT);
    rt_pin_mode(GPIO_BTN_LEFT, PIN_MODE_INPUT_PULLUP);
#endif

    g_pKeySem = rt_sem_create(KEY_SEM_NAME, KEY_SEM_INIT_VALUE, RT_IPC_FLAG_PRIO);
    if (RT_NULL == g_pKeySem)
    {
        rt_kprintf("create key semaphore failed.\n");
        result = -RT_ERROR;
    }
    else
    {
        result = createLed1Thread();
    }

    if (RT_EOK == result)
    {
        result = createLed2Thread();
    }
    else
    {
        /* 前置步骤失败，跳过后续创建 */
    }

    if (RT_EOK == result)
    {
        result = createKeyThread();
    }
    else
    {
        /* 前置步骤失败，跳过后续创建 */
    }

    if (RT_EOK == result)
    {
        result = createMonitorThread();
    }
    else
    {
        /* 前置步骤失败，跳过后续创建 */
    }

    return result;
}
INIT_APP_EXPORT(threadLedStart);
MSH_CMD_EXPORT(threadLedStart, thread led sample);
