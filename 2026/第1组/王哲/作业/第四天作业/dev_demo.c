#include <rtthread.h>
//#include <rtdevice.h>
#define DBG_TAG "dev_demo"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

rt_err_t dev_demo_init(rt_device_t dev)
{
    LOG_I("init\n");
    return RT_EOK;
}
rt_err_t dev_demo_open(rt_device_t dev, rt_uint16_t oflag)
{
    LOG_I("open\n");
    return RT_EOK;
}
rt_err_t dev_demo_close(rt_device_t dev)
{
    LOG_I("close\n");
    return RT_EOK;
}
rt_size_t dev_demo_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    LOG_I("read\n");
    return RT_EOK;
}
rt_size_t dev_demo_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    LOG_I("write\n");
    return RT_EOK;
}
rt_err_t  dev_demo_control(rt_device_t dev, int cmd, void *args)
{
    LOG_I("control\n");
    return RT_EOK;
}
static const struct rt_device_ops ops = {
    .init = dev_demo_init,
    .open = dev_demo_open,
    .close = dev_demo_close,
    .read = dev_demo_read,
    .write = dev_demo_write,
    .control = dev_demo_control,
};
static int dev_demo(void)
{
    rt_device_t text;
    rt_err_t ret;

    text = rt_device_create(RT_Device_Class_Char, 0);
    if(text == RT_NULL)
    {
     return RT_ERROR;
    }
    text->ops = &ops;
    ret = rt_device_register(text, "dev_demo", RT_DEVICE_FLAG_RDWR);
    if(RT_EOK != ret)
    {
        return RT_ERROR;
    }

    return RT_EOK;

}
INIT_DEVICE_EXPORT(dev_demo);
