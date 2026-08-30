#include <rtthread.h>
#include <rtdevice.h>

static int test_dev_app(void)
{
    rt_device_t dev = rt_device_find("test_dev");          /* ① 找到设备 */
    if (dev == RT_NULL)
    {
        rt_kprintf("can not find test_dev!\n");
        return -RT_ERROR;
    }

    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR);             /* ② 打开 */
    rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, RT_NULL);/* ③ 控制 */
    rt_device_write(dev, 100, RT_NULL, 1024);              /* ④ 写 1024 字节 */
    rt_device_read(dev, 20, RT_NULL, 128);                 /* ⑤ 读 128 字节 */
    rt_device_close(dev);                                  /* ⑥ 关闭 */

    return RT_EOK;
}
MSH_CMD_EXPORT(test_dev_app, test test_dev framework);