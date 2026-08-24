/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-24     Vincent       the first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#include "aht10.h"

#include <unistd.h>
#include <fcntl.h>

// 创建filesystem和AHT线程时待用
#define THREAD_PRIORITY 25
#define THREAD_STACK_SIZE 2048
#define THREAD_TIMESLICE 5

// filesystem线程指针
rt_thread_t filesystem = RT_NULL;

// AHT挂载的总线名字
#define AHT10_I2C_BUS "i2c3"
// AHT线程指针
rt_thread_t AHT10 = RT_NULL;

// Humi:湿度值,Temp:温度值,aht_count:采集次数
float Humi, Temp = 0.0;
rt_uint32_t aht_count = 0;
// AHT测试样例
void AHT10_Test(void *parameter)
{
    // AHT设备指针
    aht10_device_t Dev = RT_NULL;

    // 初始化设备
    Dev = aht10_init(AHT10_I2C_BUS);
    if (Dev == RT_NULL)
    {
        rt_kprintf("AHT10_init Fail");
        return;
    }

    while (1)
    {
        // 读取温湿度值
        Humi = aht10_read_humidity(Dev);
        Temp = aht10_read_temperature(Dev);
        aht_count++;
        // 没有下载rt_vsprintf_full软件包的时候
//        rt_kprintf("Humi: %d.%d\n", (int)Humi, (int)(Humi * 10) % 10);
//        rt_kprintf("Temp: %d.%d\n", (int)Temp, (int)(Temp * 10) % 10);

        // 配合rt_vsnprintf_full软件包使用
        //  rt_kprintf("Humi: %f, Temp: %f\n", Humi, Temp);

        rt_thread_mdelay(1000);
    }
}

void AHT10_Creat_Thread(void)
{
    // 创建线程
    AHT10 = rt_thread_create("AHT10", AHT10_Test, RT_NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    // 创建成功就启动
    if (AHT10 != RT_NULL)
        rt_thread_startup(AHT10);
    else
        rt_kprintf("AHT10_Thread Create Fail");
}
// 导出Shell命令
MSH_CMD_EXPORT(AHT10_Creat_Thread, This Function will creat a AHT10 thread.);

int filesysytem_init(void)
{
    static int fd;
    fd = open("/fal/Data.txt", O_RDWR|O_CREAT);

    return fd;
}

void filesystem_test(void)
{
//    while(filesysytem_init() > 0)
//    {
//        static char *str;
//        sprintf(str, "Temp：%.2f ; Humi：%.2f ; Count： %d\n", Temp, Humi, aht_count);
//        write(filesysytem_init(), str, strlen(str));
//        close(filesysytem_init());
//        rt_thread_mdelay(1000);
//    }
//    rt_kprintf("fd open failed!,error: %d \n ",filesysytem_init());
    static int fd;
    static char str[128];
    fd = open("/fal/Data.txt", O_RDWR|O_CREAT|O_APPEND,0666);
    if(fd < 0)
    {
        rt_kprintf("open failed\n");
        return;
    }
    while(1)
    {
        sprintf(str,
                "Temp:%d.%d ; Humi:%d.%d ; Count:%d\r\n",
                (int)Temp,
                ((int)(Temp*10))%10,
                (int)Humi,
                ((int)(Humi*10))%10,
                aht_count);
        write(fd, str, strlen(str));
        fsync(fd);
        rt_thread_mdelay(1000);
    }
}

void filesystem_Creat_Thread(void)
{
    // 创建线程
    filesystem = rt_thread_create("filesystem", filesystem_test, RT_NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    // 创建成功就启动
    if (filesystem != RT_NULL)
        rt_thread_startup(filesystem);
    else
        rt_kprintf("filesystem Create Fail");

    // 创建线程
    AHT10 = rt_thread_create("AHT10", AHT10_Test, RT_NULL, THREAD_STACK_SIZE, THREAD_PRIORITY, THREAD_TIMESLICE);
    // 创建成功就启动
    if (AHT10 != RT_NULL)
        rt_thread_startup(AHT10);
    else
        rt_kprintf("AHT10_Thread Create Fail");
}
MSH_CMD_EXPORT(filesystem_Creat_Thread,filesystem_Creat_Thread);
