#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

/* 虚拟设备配置与IO控制命令定义 */
#define VIRT_DEV_NAME        "virt_dev"
#define VIRT_DEV_BUF_SIZE    256

enum virt_dev_ctrl_cmd
{
    VIRT_DEV_CMD_RESET  = 0x01,
    VIRT_DEV_CMD_STATUS = 0x02,
};
/* 虚拟设备状态结构体，control接口返回给应用 */
struct virt_dev_status
{
    rt_uint32_t open_cnt;
    rt_uint32_t read_cnt;
    rt_uint32_t write_cnt;
    rt_uint32_t ctrl_cnt;
    rt_size_t   data_len;
};
/* 虚拟设备私有数据，驱动层数据 */
struct virt_dev_private
{
    rt_uint8_t  is_opened;
    struct virt_dev_status status;
    rt_uint8_t  buffer[VIRT_DEV_BUF_SIZE];
};

/* --------------------- 底层虚拟硬件模拟(驱动私有层) --------------------- */
static struct virt_dev_private g_virt_dev;

/* --------------------- RT‑Thread标准设备驱动操作接口 框架层 --------------------- */
static rt_err_t virt_dev_init(rt_device_t dev)
{
    rt_memset(&g_virt_dev,0,sizeof(g_virt_dev));
    dev->user_data = &g_virt_dev;
    rt_kprintf("virt device driver init ok\n");
    return RT_EOK;
}

static rt_err_t virt_dev_open(rt_device_t dev, rt_uint16_t oflag)
{
    struct virt_dev_private *priv = dev->user_data;
    priv->is_opened = 1;
    priv->status.open_cnt ++;
    rt_kprintf("virt_dev open, flag:%d\n",oflag);
    return RT_EOK;
}

static rt_err_t virt_dev_close(rt_device_t dev)
{
    struct virt_dev_private *priv = dev->user_data;
    priv->is_opened = 0;
    rt_kprintf("virt_dev close\n");
    return RT_EOK;
}

static rt_size_t virt_dev_read(rt_device_t dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct virt_dev_private *priv = dev->user_data;
    rt_size_t copy_len;

    if(buffer == RT_NULL || size == 0)
        return 0;
    if(pos < 0 || (rt_size_t)pos >= priv->status.data_len)
        return 0;

    copy_len = size;
    if(copy_len > priv->status.data_len - pos)
    {
        copy_len = priv->status.data_len - pos;
    }
    rt_memcpy(buffer,priv->buffer + pos,copy_len);
    priv->status.read_cnt ++;
    rt_kprintf("virt_dev read pos:%d,read_len:%d\n",pos,copy_len);
    return copy_len;
}

static rt_size_t virt_dev_write(rt_device_t dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct virt_dev_private *priv = dev->user_data;
    rt_size_t copy_len;
    if(buffer == RT_NULL || size == 0)
        return 0;
    if(pos < 0 || (rt_size_t)pos >= VIRT_DEV_BUF_SIZE)
        return 0;

    copy_len = size;
    if(copy_len > VIRT_DEV_BUF_SIZE - pos)
    {
        copy_len = VIRT_DEV_BUF_SIZE - pos;
    }
    rt_memcpy(priv->buffer + pos,buffer,copy_len);
    if(priv->status.data_len < (rt_size_t)(pos + copy_len))
    {
        priv->status.data_len = pos + copy_len;
    }
    priv->status.write_cnt ++;
    rt_kprintf("virt_dev write pos:%d,write_len:%d\n",pos,copy_len);
    return copy_len;
}

static rt_err_t virt_dev_control(rt_device_t dev,int cmd,void *args)
{
    struct virt_dev_private *priv = dev->user_data;
    priv->status.ctrl_cnt ++;
    switch(cmd)
    {
        case VIRT_DEV_CMD_RESET:
            rt_memset(priv->buffer,0,sizeof(priv->buffer));
            priv->status.data_len = 0;
            rt_kprintf("virt_dev ctrl:reset buffer\n");
            break;
        case VIRT_DEV_CMD_STATUS:
            if(args == RT_NULL)
                return -RT_EINVAL;
            rt_memcpy(args,&priv->status,sizeof(struct virt_dev_status));
            rt_kprintf("virt_dev ctrl:get device status\n");
            break;
        default:
            rt_kprintf("virt_dev ctrl:unknown cmd %d\n",cmd);
            return -RT_EINVAL;
    }
    return RT_EOK;
}

/* ---------------------- 设备注册入口函数 ------------------------ */
static int virt_drv_register(void)
{
    rt_device_t dev;
    dev = rt_device_create(RT_Device_Class_Char,0);
    if(dev == RT_NULL)
    {
        rt_kprintf("virt device create fail\n");
        return -RT_ERROR;
    }
    /* 挂载驱动操作函数，构建驱动框架 */
    dev->init    = virt_dev_init;
    dev->open    = virt_dev_open;
    dev->close   = virt_dev_close;
    dev->read    = virt_dev_read;
    dev->write   = virt_dev_write;
    dev->control = virt_dev_control;

    if(rt_device_register(dev,VIRT_DEV_NAME,RT_DEVICE_FLAG_RDWR) != RT_EOK)
    {
        rt_kprintf("virt device register fail\n");
        rt_device_destroy(dev);
        return -RT_ERROR;
    }
    rt_kprintf("virt_dev driver register success,name:%s\n",VIRT_DEV_NAME);
    return RT_EOK;
}
INIT_BOARD_EXPORT(virt_drv_register);

/* ------------------------- 应用层测试demo -------------------------- */
static int virt_dev_app_demo(void)
{
    rt_device_t dev;
    char w_buf[] = "virtual device driver test";
    char r_buf[VIRT_DEV_BUF_SIZE];
    struct virt_dev_status dev_sta;

    dev = rt_device_find(VIRT_DEV_NAME);
    if(dev == RT_NULL)
    {
        rt_kprintf("can not find virt_dev\n");
        return -RT_ERROR;
    }
    rt_device_open(dev,RT_DEVICE_OFLAG_RDWR);

    rt_device_write(dev,0,w_buf,strlen(w_buf));

    rt_memset(r_buf,0,sizeof(r_buf));
    rt_device_read(dev,0,r_buf,sizeof(r_buf)-1);
    rt_kprintf("app read data: %s\n",r_buf);

    rt_device_control(dev,VIRT_DEV_CMD_STATUS,&dev_sta);
    rt_kprintf("app:open=%d read=%d write=%d ctrl=%d datalen=%d\n",
               dev_sta.open_cnt,dev_sta.read_cnt,dev_sta.write_cnt,
               dev_sta.ctrl_cnt,dev_sta.data_len);

    rt_device_control(dev,VIRT_DEV_CMD_RESET,RT_NULL);
    rt_device_close(dev);
    return RT_EOK;
}
MSH_CMD_EXPORT(virt_dev_app_demo,run virtual device demo);
