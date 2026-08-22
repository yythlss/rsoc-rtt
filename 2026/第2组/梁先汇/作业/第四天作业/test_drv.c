#include <rtthread.h>
#include <rtdevice.h>
#include "test_dev.h"

#define LOG_TAG "test.drv"
#define LOG_LVL LOG_LVL_DBG
#include <ulog.h>

/* 实现 6 个函数（虚拟设备，用 LOG 打印假装操作硬件） */
static rt_err_t test_drv_init(rt_device_t dev)      
{
    LOG_I("init");  
    return RT_EOK; 
}
static rt_err_t test_drv_open(rt_device_t dev, rt_uint16_t oflag) 
{
    LOG_I("open");  
    return RT_EOK;
}
static rt_err_t test_drv_close(rt_device_t dev)     
{
    LOG_I("close");  
    return RT_EOK;
}
static rt_size_t test_drv_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    LOG_I("read");  
    return size;
}
static rt_size_t test_drv_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size) 
{
    LOG_I("write");  
    return size;
}
static rt_err_t test_drv_control(rt_device_t dev, int cmd, void *args) 
{ 
    LOG_I("control");  
    return RT_EOK;
}


static const struct rt_test_ops test_ops =
{
    test_drv_init,
    test_drv_open,
    test_drv_close,
    test_drv_read,
    test_drv_write,
    test_drv_control,
};

/* 注册入口：系统启动自动调用，把 ops 交给框架层 */
static int rt_test_drv_init(void)
{
    if (rt_device_test_register("test_dev", &test_ops, RT_NULL) != RT_EOK)
    {
        LOG_E("test dev register failed!");
        return -RT_ERROR;
    }
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_test_drv_init);