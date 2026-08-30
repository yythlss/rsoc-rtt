/*
 * 温湿度数据文件存储模块
 *
 * 文件路径：/fal/Data.txt
 * 容量上限：DATA_FILE_MAX_BYTES（512KB，FAL filesystem 分区约 3MB）
 * 超限策略：删除文件头部最旧行，保留较新数据后继续追加
 */
#include <rtthread.h>
#include <string.h>

#include "data_storage.h"

#if defined(RT_USING_DFS) && defined(DFS_USING_POSIX)

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>

/* 数据文件路径 */
#define DATA_FILE_PATH              "/fal/Data.txt"
/* 单文件最大字节数 */
#define DATA_FILE_MAX_BYTES         (512U * 1024U)
/* 单行文本缓冲区 */
#define DATA_LINE_BUF_SIZE          80U
/* 文件操作互斥量名 */
#define DATA_FILE_MUTEX_NAME        "data_mtx"

static rt_uint32_t g_sampleCount = 0U;
static rt_bool_t g_sessionActive = RT_FALSE;
static rt_mutex_t g_pFileMutex = RT_NULL;

/************************************************************************
函数名   : ensureFileMutex
功能描述:    确保文件互斥量已创建
输入参数:    无
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static rt_err_t ensureFileMutex(void)
{
    rt_err_t result = RT_EOK;

    if (RT_NULL == g_pFileMutex)
    {
        g_pFileMutex = rt_mutex_create(DATA_FILE_MUTEX_NAME, RT_IPC_FLAG_PRIO);
        if (RT_NULL == g_pFileMutex)
        {
            result = -RT_ERROR;
        }
        else
        {
            result = RT_EOK;
        }
    }
    else
    {
        result = RT_EOK;
    }

    return result;
}

/************************************************************************
函数名   : formatSampleLine
功能描述:    格式化单行温湿度记录
输入参数:    temp -- 温度
            humi -- 湿度
            count -- 采集序号
            pBuf -- 输出缓冲区
            bufSize -- 缓冲区大小
输出参数:    pBuf -- 格式化结果
返回值 :    写入长度，失败返回 0
************************************************************************/
static rt_uint32_t formatSampleLine(float temp,
                                    float humi,
                                    rt_uint32_t count,
                                    char *pBuf,
                                    rt_uint32_t bufSize)
{
    rt_int32_t lineLen = 0;
    rt_uint32_t written = 0U;

    if ((RT_NULL == pBuf) || (0U == bufSize))
    {
        written = 0U;
    }
    else
    {
        lineLen = rt_snprintf(pBuf, bufSize,
                              "Temp：%.2f ; Humi：%.2f ; Count： %u\r\n",
                              temp, humi, (unsigned int)count);
        if ((0 >= lineLen) || ((rt_int32_t)bufSize <= lineLen))
        {
            written = 0U;
        }
        else
        {
            written = (rt_uint32_t)lineLen;
        }
    }

    return written;
}

/************************************************************************
函数名   : findKeepOffset
功能描述:    计算保留文件内容起始偏移，丢弃头部最旧数据
输入参数:    fd -- 文件描述符
            fileSize -- 当前文件大小
            newLineLen -- 待追加行长度
输出参数:    无
返回值 :    保留内容起始偏移
************************************************************************/
static rt_off_t findKeepOffset(int fd,
                               rt_uint32_t fileSize,
                               rt_uint32_t newLineLen)
{
    rt_uint32_t maxContentSize = 0U;
    rt_uint32_t bytesToDrop = 0U;
    rt_off_t keepOffset = 0;
    rt_off_t scanOffset = 0;
    char ch = 0;
    rt_ssize_t readLen = 0;

    if ((0U == fileSize) || (0U == newLineLen))
    {
        keepOffset = 0;
    }
    else
    {
        maxContentSize = DATA_FILE_MAX_BYTES - newLineLen;
        if (fileSize <= maxContentSize)
        {
            keepOffset = 0;
        }
        else
        {
            bytesToDrop = fileSize - maxContentSize;
            scanOffset = lseek(fd, (rt_off_t)bytesToDrop, SEEK_SET);
            if (0 > scanOffset)
            {
                keepOffset = (rt_off_t)fileSize;
            }
            else
            {
                keepOffset = scanOffset;
                while (RT_TRUE)
                {
                    readLen = read(fd, &ch, 1);
                    if (1 != readLen)
                    {
                        break;
                    }
                    else if ('\n' == ch)
                    {
                        keepOffset = lseek(fd, 0, SEEK_CUR);
                        break;
                    }
                    else
                    {
                        /* 继续扫描到行尾 */
                    }
                }
            }
        }
    }

    return keepOffset;
}

/************************************************************************
函数名   : rewriteFileTail
功能描述:    将文件指定偏移后的内容写回，实现丢弃最旧数据
输入参数:    keepOffset -- 保留起始偏移
            fileSize -- 当前文件大小
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static rt_err_t rewriteFileTail(rt_off_t keepOffset, rt_uint32_t fileSize)
{
    rt_err_t result = RT_EOK;
    int readFd = -1;
    int writeFd = -1;
    rt_uint32_t remainSize = 0U;
    rt_uint8_t *pRemainBuf = RT_NULL;
    rt_ssize_t readLen = 0;
    rt_ssize_t writeLen = 0;

    if ((0 > keepOffset) || ((rt_off_t)fileSize < keepOffset))
    {
        result = -RT_ERROR;
    }
    else
    {
        remainSize = fileSize - (rt_uint32_t)keepOffset;
        readFd = open(DATA_FILE_PATH, O_RDONLY, 0);
        if (0 > readFd)
        {
            result = -RT_ERROR;
        }
        else if ((0U < remainSize) && (RT_NULL == (pRemainBuf = rt_malloc(remainSize))))
        {
            close(readFd);
            result = -RT_ERROR;
        }
        else
        {
            if (0U < remainSize)
            {
                if ((rt_off_t)keepOffset != lseek(readFd, keepOffset, SEEK_SET))
                {
                    result = -RT_ERROR;
                }
                else
                {
                    readLen = read(readFd, pRemainBuf, remainSize);
                    if ((rt_ssize_t)remainSize != readLen)
                    {
                        result = -RT_ERROR;
                    }
                    else
                    {
                        /* 读取剩余内容成功 */
                    }
                }
            }
            else
            {
                /* 全部丢弃，重写空文件 */
            }

            close(readFd);

            if (RT_EOK == result)
            {
                writeFd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (0 > writeFd)
                {
                    result = -RT_ERROR;
                }
                else
                {
                    if ((0U < remainSize) && (RT_NULL != pRemainBuf))
                    {
                        writeLen = write(writeFd, pRemainBuf, remainSize);
                        if ((rt_ssize_t)remainSize != writeLen)
                        {
                            result = -RT_ERROR;
                        }
                        else
                        {
                            /* 写回剩余内容成功 */
                        }
                    }
                    else
                    {
                        /* 空文件 */
                    }
                    close(writeFd);
                }
            }
            else
            {
                /* 读文件失败，不再重写 */
            }

            if (RT_NULL != pRemainBuf)
            {
                rt_free(pRemainBuf);
            }
        }
    }

    return result;
}

/************************************************************************
函数名   : trimFileIfNeeded
功能描述:    若追加后将超限，则删除最旧行
输入参数:    newLineLen -- 待追加行长度
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static rt_err_t trimFileIfNeeded(rt_uint32_t newLineLen)
{
    rt_err_t result = RT_EOK;
    struct stat fileStat;
    rt_uint32_t fileSize = 0U;
    rt_off_t keepOffset = 0;
    int readFd = -1;

    if (0U == newLineLen)
    {
        result = -RT_ERROR;
    }
    else if (0 != stat(DATA_FILE_PATH, &fileStat))
    {
        result = RT_EOK;
    }
    else
    {
        fileSize = (rt_uint32_t)fileStat.st_size;
        if ((fileSize + newLineLen) <= DATA_FILE_MAX_BYTES)
        {
            result = RT_EOK;
        }
        else
        {
            readFd = open(DATA_FILE_PATH, O_RDONLY, 0);
            if (0 > readFd)
            {
                result = -RT_ERROR;
            }
            else
            {
                keepOffset = findKeepOffset(readFd, fileSize, newLineLen);
                close(readFd);
                result = rewriteFileTail(keepOffset, fileSize);
            }
        }
    }

    return result;
}

/************************************************************************
函数名   : appendLineToFile
功能描述:    追加一行温湿度记录
输入参数:    pLine -- 行内容
            lineLen -- 行长度
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static rt_err_t appendLineToFile(const char *pLine, rt_uint32_t lineLen)
{
    rt_err_t result = RT_EOK;
    int fd = -1;
    rt_ssize_t writeLen = 0;

    if ((RT_NULL == pLine) || (0U == lineLen))
    {
        result = -RT_ERROR;
    }
    else if (RT_EOK != trimFileIfNeeded(lineLen))
    {
        result = -RT_ERROR;
    }
    else
    {
        fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (0 > fd)
        {
            result = -RT_ERROR;
        }
        else
        {
            writeLen = write(fd, pLine, lineLen);
            close(fd);
            if ((rt_ssize_t)lineLen != writeLen)
            {
                result = -RT_ERROR;
            }
            else
            {
                result = RT_EOK;
            }
        }
    }

    return result;
}

rt_err_t dataStorageSessionStart(void)
{
    rt_err_t result = RT_EOK;
    int fd = -1;

    if (RT_EOK != ensureFileMutex())
    {
        result = -RT_ERROR;
    }
    else if (RT_EOK != rt_mutex_take(g_pFileMutex, RT_WAITING_FOREVER))
    {
        result = -RT_ERROR;
    }
    else
    {
        g_sampleCount = 0U;
        g_sessionActive = RT_FALSE;
        fd = open(DATA_FILE_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (0 > fd)
        {
            rt_kprintf("data storage: create %s failed, check /fal mount.\n", DATA_FILE_PATH);
            rt_kprintf("data storage: run [mount filesystem /fal elm] or mkfs first.\n");
            result = -RT_ERROR;
        }
        else
        {
            close(fd);
            g_sessionActive = RT_TRUE;
            rt_kprintf("data storage: session start, max %u bytes.\n",
                       (unsigned int)DATA_FILE_MAX_BYTES);
            result = RT_EOK;
        }
        rt_mutex_release(g_pFileMutex);
    }

    return result;
}

rt_err_t dataStorageAppendSample(float temp, float humi)
{
    rt_err_t result = RT_EOK;
    char lineBuf[DATA_LINE_BUF_SIZE];
    rt_uint32_t lineLen = 0U;
    rt_uint32_t nextCount = 0U;

    if (RT_FALSE == g_sessionActive)
    {
        result = -RT_ERROR;
    }
    else if (RT_NULL == g_pFileMutex)
    {
        result = -RT_ERROR;
    }
    else if (RT_EOK != rt_mutex_take(g_pFileMutex, RT_WAITING_FOREVER))
    {
        result = -RT_ERROR;
    }
    else
    {
        nextCount = g_sampleCount + 1U;
        lineLen = formatSampleLine(temp, humi, nextCount, lineBuf, sizeof(lineBuf));
        if (0U == lineLen)
        {
            result = -RT_ERROR;
        }
        else if (RT_EOK != appendLineToFile(lineBuf, lineLen))
        {
            rt_kprintf("data storage: append failed.\n");
            result = -RT_ERROR;
        }
        else
        {
            g_sampleCount = nextCount;
            result = RT_EOK;
        }
        rt_mutex_release(g_pFileMutex);
    }

    return result;
}

rt_uint32_t dataStorageGetSampleCount(void)
{
    rt_uint32_t count = 0U;

    if (RT_NULL != g_pFileMutex)
    {
        if (RT_EOK == rt_mutex_take(g_pFileMutex, RT_WAITING_FOREVER))
        {
            count = g_sampleCount;
            rt_mutex_release(g_pFileMutex);
        }
        else
        {
            count = 0U;
        }
    }
    else
    {
        count = 0U;
    }

    return count;
}

#else

rt_err_t dataStorageSessionStart(void)
{
    rt_kprintf("data storage: DFS/POSIX not enabled.\n");
    return -RT_ENOSYS;
}

rt_err_t dataStorageAppendSample(float temp, float humi)
{
    RT_UNUSED(temp);
    RT_UNUSED(humi);
    return -RT_ENOSYS;
}

rt_uint32_t dataStorageGetSampleCount(void)
{
    return 0U;
}

#endif /* RT_USING_DFS && DFS_USING_POSIX */
