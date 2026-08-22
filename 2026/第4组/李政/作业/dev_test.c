#include <rtthread.h>
#include <string.h>

#define TEST_DEV_NAME               "test"
#define TEST_DEV_BUF_SIZE           64U

static rt_device_t g_pTestDev = RT_NULL;
static char g_testDevBuf[TEST_DEV_BUF_SIZE] = "hello test dev\n";
static rt_bool_t g_testDevInited = RT_FALSE;

/************************************************************************
函数名   : testDevRead
功能描述:    字符设备读操作
输入参数:    dev -- 设备句柄
            pos -- 偏移
            pBuffer -- 读缓冲区
            size -- 读取长度
输出参数:    pBuffer -- 读出的数据
返回值 :    实际读取字节数
************************************************************************/
static rt_ssize_t testDevRead(rt_device_t dev,
                              rt_off_t pos,
                              void *pBuffer,
                              rt_size_t size)
{
    rt_ssize_t readLen = 0;
    rt_size_t dataLen = 0U;
    rt_size_t copyLen = 0U;

    RT_UNUSED(dev);
    RT_UNUSED(pos);

    if (RT_NULL == pBuffer)
    {
        readLen = 0;
    }
    else if (0U == size)
    {
        readLen = 0;
    }
    else
    {
        dataLen = rt_strlen(g_testDevBuf);
        copyLen = size;
        if (dataLen < copyLen)
        {
            copyLen = dataLen;
        }
        else
        {
            /* 请求长度不超过缓冲区内容长度 */
        }

        rt_memcpy(pBuffer, g_testDevBuf, copyLen);
        readLen = (rt_ssize_t)copyLen;
    }

    return readLen;
}

/************************************************************************
函数名   : testDevWrite
功能描述:    字符设备写操作
输入参数:    dev -- 设备句柄
            pos -- 偏移
            pBuffer -- 写缓冲区
            size -- 写入长度
输出参数:    无
返回值 :    实际写入字节数
************************************************************************/
static rt_ssize_t testDevWrite(rt_device_t dev,
                               rt_off_t pos,
                               const void *pBuffer,
                               rt_size_t size)
{
    rt_ssize_t writeLen = 0;
    rt_size_t copyLen = 0U;

    RT_UNUSED(dev);
    RT_UNUSED(pos);

    if (RT_NULL == pBuffer)
    {
        writeLen = 0;
    }
    else if (0U == size)
    {
        writeLen = 0;
    }
    else
    {
        copyLen = size;
        if ((TEST_DEV_BUF_SIZE - 1U) < copyLen)
        {
            copyLen = TEST_DEV_BUF_SIZE - 1U;
        }
        else
        {
            /* 写入长度未超过设备缓冲区 */
        }

        rt_memcpy(g_testDevBuf, pBuffer, copyLen);
        g_testDevBuf[copyLen] = '\0';
        writeLen = (rt_ssize_t)copyLen;
    }

    return writeLen;
}

/************************************************************************
函数名   : testDevControl
功能描述:    字符设备控制操作
输入参数:    dev -- 设备句柄
            cmd -- 控制命令
            pArgs -- 命令参数
输出参数:    无
返回值 :    RT_EOK 成功
************************************************************************/
static rt_err_t testDevControl(rt_device_t dev, int cmd, void *pArgs)
{
    RT_UNUSED(dev);
    RT_UNUSED(cmd);
    RT_UNUSED(pArgs);

    return RT_EOK;
}

/************************************************************************
函数名   : testDevOpen
功能描述:    字符设备打开操作
输入参数:    dev -- 设备句柄
            oflag -- 打开标志
输出参数:    无
返回值 :    RT_EOK 成功
************************************************************************/
static rt_err_t testDevOpen(rt_device_t dev, rt_uint16_t oflag)
{
    RT_UNUSED(dev);
    RT_UNUSED(oflag);

    rt_kprintf("test device opened\n");
    return RT_EOK;
}

/************************************************************************
函数名   : testDevClose
功能描述:    字符设备关闭操作
输入参数:    dev -- 设备句柄
输出参数:    无
返回值 :    RT_EOK 成功
************************************************************************/
static rt_err_t testDevClose(rt_device_t dev)
{
    RT_UNUSED(dev);

    rt_kprintf("test device closed\n");
    return RT_EOK;
}

#ifdef RT_USING_DEVICE_OPS
const static struct rt_device_ops g_testDevOps =
{
    RT_NULL,
    testDevOpen,
    testDevClose,
    testDevRead,
    testDevWrite,
    testDevControl
};
#endif

/************************************************************************
函数名   : testDeviceInit
功能描述:    创建并注册 test 字符设备
输入参数:    无
输出参数:    无
返回值 :    0 成功，-1 失败
************************************************************************/
static int testDeviceInit(void)
{
    int result = 0;
    rt_err_t regResult = RT_EOK;

    if (RT_TRUE == g_testDevInited)
    {
        result = 0;
    }
    else if (RT_NULL != rt_device_find(TEST_DEV_NAME))
    {
        rt_kprintf("device %s already exists.\n", TEST_DEV_NAME);
        result = -1;
    }
    else
    {
        g_pTestDev = rt_device_create(RT_Device_Class_Char, 0);
        if (RT_NULL == g_pTestDev)
        {
            rt_kprintf("create test device failed.\n");
            result = -1;
        }
        else
        {
#ifdef RT_USING_DEVICE_OPS
            g_pTestDev->ops = &g_testDevOps;
#else
            g_pTestDev->init = RT_NULL;
            g_pTestDev->open = testDevOpen;
            g_pTestDev->close = testDevClose;
            g_pTestDev->read = testDevRead;
            g_pTestDev->write = testDevWrite;
            g_pTestDev->control = testDevControl;
#endif
            g_pTestDev->user_data = RT_NULL;

            regResult = rt_device_register(g_pTestDev, TEST_DEV_NAME, RT_DEVICE_FLAG_RDWR);
            if (RT_EOK != regResult)
            {
                rt_device_destroy(g_pTestDev);
                g_pTestDev = RT_NULL;
                rt_kprintf("register test device failed.\n");
                result = -1;
            }
            else
            {
                g_testDevInited = RT_TRUE;
                rt_kprintf("device %s registered.\n", TEST_DEV_NAME);
                result = 0;
            }
        }
    }

    return result;
}

/************************************************************************
函数名   : devTestRun
功能描述:    test 设备的 open/read/write/close 流程
输入参数:    无
输出参数:    无
返回值 :    0 成功，-1 失败
************************************************************************/
static int devTestRun(void)
{
    int result = 0;
    rt_device_t pDev = RT_NULL;
    char readBuf[TEST_DEV_BUF_SIZE];
    const char writeMsg[] = "RT-Thread test write\n";
    rt_ssize_t readLen = 0;
    rt_ssize_t writeLen = 0;
    rt_err_t openResult = RT_EOK;

    rt_memset(readBuf, 0, sizeof(readBuf));

    pDev = rt_device_find(TEST_DEV_NAME);
    if (RT_NULL == pDev)
    {
        rt_kprintf("device %s not found\n", TEST_DEV_NAME);
        result = -1;
    }
    else
    {
        openResult = rt_device_open(pDev, RT_DEVICE_FLAG_RDWR);
        if (RT_EOK != openResult)
        {
            rt_kprintf("open %s failed, err=%d\n", TEST_DEV_NAME, openResult);
            result = -1;
        }
        else
        {
            readLen = rt_device_read(pDev, 0, readBuf, sizeof(readBuf) - 1U);
            rt_kprintf("read %d bytes: %s", (int)readLen, readBuf);

            writeLen = rt_device_write(pDev, 0, writeMsg, rt_strlen(writeMsg));
            rt_kprintf("write %d bytes\n", (int)writeLen);

            rt_memset(readBuf, 0, sizeof(readBuf));
            readLen = rt_device_read(pDev, 0, readBuf, sizeof(readBuf) - 1U);
            rt_kprintf("read again %d bytes: %s", (int)readLen, readBuf);

            rt_device_close(pDev);
            result = 0;
        }
    }

    return result;
}

INIT_DEVICE_EXPORT(testDeviceInit);

int dev_test_run(void)
{
    return devTestRun();
}

MSH_CMD_EXPORT(dev_test_run, test device open/read/write demo);
