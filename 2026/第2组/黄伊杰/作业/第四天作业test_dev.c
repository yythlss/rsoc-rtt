/*
 * 虚拟设备驱动框架层 (test_dev)
 * 
 * 功能：
 * 1. 自定义虚拟设备结构体
 * 2. 设备注册表管理
 * 3. 设备操作接口（open/close/read/write/control）
 * 4. 虚拟内存回环设备实现
 * 
 * 执行 test_dev 命令后，完成：注册、初始化、打开、写入、读取、控制、关闭
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <string.h>

#define DRV_DEBUG
#define LOG_TAG     "vdev.test"
#include <drv_log.h>

/* ==================== 虚拟设备框架层定义 ==================== */

/* 虚拟设备结构体 */
struct vdev_device {
    char name[16];              /* 设备名称 */
    rt_uint8_t *buffer;         /* 数据缓冲区 */
    rt_size_t buffer_size;      /* 缓冲区大小 */
    rt_size_t data_len;         /* 当前数据长度 */
    rt_uint32_t ref_count;      /* 引用计数 */
    rt_bool_t initialized;      /* 是否已初始化 */
    
    /* 设备操作函数指针 */
    rt_err_t (*init)(struct vdev_device *dev);
    rt_err_t (*open)(struct vdev_device *dev, rt_uint16_t oflag);
    rt_err_t (*close)(struct vdev_device *dev);
    rt_size_t (*read)(struct vdev_device *dev, rt_off_t pos, void *buffer, rt_size_t size);
    rt_size_t (*write)(struct vdev_device *dev, rt_off_t pos, const void *buffer, rt_size_t size);
    rt_err_t (*control)(struct vdev_device *dev, int cmd, void *args);
};

/* 设备注册表 */
#define VDEV_MAX_DEVICES    8
static struct vdev_device *vdev_table[VDEV_MAX_DEVICES];
static rt_size_t vdev_count = 0;

/* ==================== 虚拟设备框架层接口实现 ==================== */

/**
 * 注册虚拟设备
 */
rt_err_t vdev_register(struct vdev_device *dev)
{
    rt_size_t i;
    
    if (dev == RT_NULL)
    {
        LOG_E("vdev register: device is NULL");
        return -RT_ERROR;
    }
    
    /* 检查设备是否已存在 */
    for (i = 0; i < vdev_count; i++)
    {
        if (rt_strcmp(vdev_table[i]->name, dev->name) == 0)
        {
            LOG_E("vdev register: device '%s' already exists", dev->name);
            return -RT_ERROR;
        }
    }
    
    /* 检查注册表是否已满 */
    if (vdev_count >= VDEV_MAX_DEVICES)
    {
        LOG_E("vdev register: table is full");
        return -RT_ERROR;
    }
    
    /* 注册设备 */
    vdev_table[vdev_count++] = dev;
    LOG_I("vdev register: device '%s' registered successfully", dev->name);
    
    return RT_EOK;
}

/**
 * 查找虚拟设备
 */
struct vdev_device *vdev_find(const char *name)
{
    rt_size_t i;
    
    if (name == RT_NULL)
        return RT_NULL;
    
    for (i = 0; i < vdev_count; i++)
    {
        if (rt_strcmp(vdev_table[i]->name, name) == 0)
            return vdev_table[i];
    }
    
    return RT_NULL;
}

/**
 * 打开虚拟设备
 */
rt_err_t vdev_open(struct vdev_device *dev, rt_uint16_t oflag)
{
    if (dev == RT_NULL)
        return -RT_ERROR;
    
    /* 首次打开时初始化 */
    if (!dev->initialized && dev->init != RT_NULL)
    {
        if (dev->init(dev) != RT_EOK)
        {
            LOG_E("vdev open: init failed for '%s'", dev->name);
            return -RT_ERROR;
        }
        dev->initialized = RT_TRUE;
    }
    
    /* 调用设备 open 回调 */
    if (dev->open != RT_NULL)
    {
        if (dev->open(dev, oflag) != RT_EOK)
            return -RT_ERROR;
    }
    
    dev->ref_count++;
    LOG_I("vdev open: '%s' opened, ref_count=%d", dev->name, dev->ref_count);
    
    return RT_EOK;
}

/**
 * 关闭虚拟设备
 */
rt_err_t vdev_close(struct vdev_device *dev)
{
    if (dev == RT_NULL || dev->ref_count == 0)
        return -RT_ERROR;
    
    /* 调用设备 close 回调 */
    if (dev->close != RT_NULL)
    {
        if (dev->close(dev) != RT_EOK)
            return -RT_ERROR;
    }
    
    dev->ref_count--;
    LOG_I("vdev close: '%s' closed, ref_count=%d", dev->name, dev->ref_count);
    
    return RT_EOK;
}

/**
 * 从虚拟设备读取数据
 */
rt_size_t vdev_read(struct vdev_device *dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    if (dev == RT_NULL || dev->read == RT_NULL)
        return 0;
    
    return dev->read(dev, pos, buffer, size);
}

/**
 * 向虚拟设备写入数据
 */
rt_size_t vdev_write(struct vdev_device *dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    if (dev == RT_NULL || dev->write == RT_NULL)
        return 0;
    
    return dev->write(dev, pos, buffer, size);
}

/**
 * 控制虚拟设备
 */
rt_err_t vdev_control(struct vdev_device *dev, int cmd, void *args)
{
    if (dev == RT_NULL || dev->control == RT_NULL)
        return -RT_ERROR;
    
    return dev->control(dev, cmd, args);
}

/* ==================== 虚拟内存回环设备实现 ==================== */

#define VDEV_MEM_BUFFER_SIZE    256

/* 虚拟内存设备私有数据 */
struct vdev_mem {
    struct vdev_device parent;
    rt_uint8_t data[VDEV_MEM_BUFFER_SIZE];
};

/* 虚拟内存设备操作回调 */
static rt_err_t vdev_mem_init(struct vdev_device *dev)
{
    struct vdev_mem *mem = (struct vdev_mem *)dev;
    
    LOG_I("vdev_mem init: buffer_size=%d", mem->parent.buffer_size);
    rt_memset(mem->data, 0, VDEV_MEM_BUFFER_SIZE);
    mem->parent.data_len = 0;
    
    return RT_EOK;
}

static rt_err_t vdev_mem_open(struct vdev_device *dev, rt_uint16_t oflag)
{
    LOG_I("vdev_mem open: oflag=0x%04x", oflag);
    return RT_EOK;
}

static rt_err_t vdev_mem_close(struct vdev_device *dev)
{
    LOG_I("vdev_mem close");
    return RT_EOK;
}

static rt_size_t vdev_mem_read(struct vdev_device *dev, rt_off_t pos, void *buffer, rt_size_t size)
{
    struct vdev_mem *mem = (struct vdev_mem *)dev;
    rt_size_t read_size;
    
    if (pos >= mem->parent.data_len)
    {
        LOG_W("vdev_mem read: pos=%d >= data_len=%d", pos, mem->parent.data_len);
        return 0;
    }
    
    read_size = (pos + size > mem->parent.data_len) ? (mem->parent.data_len - pos) : size;
    rt_memcpy(buffer, &mem->data[pos], read_size);
    
    LOG_I("vdev_mem read: pos=%d, size=%d, read_size=%d", pos, size, read_size);
    LOG_I("vdev_mem read: data=[%.*s]", read_size, (char *)buffer);
    
    return read_size;
}

static rt_size_t vdev_mem_write(struct vdev_device *dev, rt_off_t pos, const void *buffer, rt_size_t size)
{
    struct vdev_mem *mem = (struct vdev_mem *)dev;
    rt_size_t write_size;
    
    if (pos >= mem->parent.buffer_size)
    {
        LOG_W("vdev_mem write: pos=%d >= buffer_size=%d", pos, mem->parent.buffer_size);
        return 0;
    }
    
    write_size = (pos + size > mem->parent.buffer_size) ? (mem->parent.buffer_size - pos) : size;
    rt_memcpy(&mem->data[pos], buffer, write_size);
    
    if (pos + write_size > mem->parent.data_len)
        mem->parent.data_len = pos + write_size;
    
    LOG_I("vdev_mem write: pos=%d, size=%d, write_size=%d", pos, size, write_size);
    LOG_I("vdev_mem write: data=[%.*s]", write_size, (char *)buffer);
    
    return write_size;
}

static rt_err_t vdev_mem_control(struct vdev_device *dev, int cmd, void *args)
{
    struct vdev_mem *mem = (struct vdev_mem *)dev;
    
    switch (cmd)
    {
    case 0:  /* 获取设备信息 */
        LOG_I("vdev_mem control: cmd=GET_INFO, data_len=%d, buffer_size=%d", 
              mem->parent.data_len, mem->parent.buffer_size);
        break;
        
    case 1:  /* 清空数据 */
        rt_memset(mem->data, 0, VDEV_MEM_BUFFER_SIZE);
        mem->parent.data_len = 0;
        LOG_I("vdev_mem control: cmd=CLEAR, data cleared");
        break;
        
    default:
        LOG_W("vdev_mem control: unknown cmd=%d", cmd);
        return -RT_ERROR;
    }
    
    return RT_EOK;
}

/* ==================== 测试命令实现 ==================== */

static int test_dev_cmd(void)
{
    struct vdev_mem *mem_dev;
    char write_data[] = "Hello, Virtual Device!";
    char read_data[64];
    rt_size_t ret;
    
    LOG_I("========== test_dev command start ==========");
    
    /* 1. 创建虚拟内存设备 */
    mem_dev = (struct vdev_mem *)rt_malloc(sizeof(struct vdev_mem));
    if (mem_dev == RT_NULL)
    {
        LOG_E("malloc vdev_mem failed");
        return -RT_ENOMEM;
    }
    
    rt_memset(mem_dev, 0, sizeof(struct vdev_mem));
    rt_strncpy(mem_dev->parent.name, "vdev_mem", sizeof(mem_dev->parent.name) - 1);
    mem_dev->parent.buffer = mem_dev->data;
    mem_dev->parent.buffer_size = VDEV_MEM_BUFFER_SIZE;
    
    /* 2. 设置设备操作回调 */
    mem_dev->parent.init = vdev_mem_init;
    mem_dev->parent.open = vdev_mem_open;
    mem_dev->parent.close = vdev_mem_close;
    mem_dev->parent.read = vdev_mem_read;
    mem_dev->parent.write = vdev_mem_write;
    mem_dev->parent.control = vdev_mem_control;
    
    /* 3. 注册设备 */
    LOG_I("[Step 1] Register device...");
    if (vdev_register(&mem_dev->parent) != RT_EOK)
    {
        LOG_E("register device failed");
        rt_free(mem_dev);
        return -RT_ERROR;
    }
    
    /* 4. 打开设备（会自动调用 init） */
    LOG_I("[Step 2] Open device...");
    if (vdev_open(&mem_dev->parent, 0x0003) != RT_EOK)  /* RT_DEVICE_OFLAG_RDWR */
    {
        LOG_E("open device failed");
        return -RT_ERROR;
    }
    
    /* 5. 写入数据 */
    LOG_I("[Step 3] Write data...");
    ret = vdev_write(&mem_dev->parent, 0, write_data, rt_strlen(write_data));
    if (ret == 0)
    {
        LOG_E("write data failed");
        return -RT_ERROR;
    }
    
    /* 6. 控制设备 - 获取信息 */
    LOG_I("[Step 4] Control device (GET_INFO)...");
    vdev_control(&mem_dev->parent, 0, RT_NULL);
    
    /* 7. 读取数据 */
    LOG_I("[Step 5] Read data...");
    rt_memset(read_data, 0, sizeof(read_data));
    ret = vdev_read(&mem_dev->parent, 0, read_data, sizeof(read_data) - 1);
    if (ret == 0)
    {
        LOG_E("read data failed");
        return -RT_ERROR;
    }
    LOG_I("Read back: [%s]", read_data);
    
    /* 8. 控制设备 - 清空数据 */
    LOG_I("[Step 6] Control device (CLEAR)...");
    vdev_control(&mem_dev->parent, 1, RT_NULL);
    
    /* 9. 关闭设备 */
    LOG_I("[Step 7] Close device...");
    vdev_close(&mem_dev->parent);
    
    LOG_I("========== test_dev command end ==========");
    LOG_I("All operations completed successfully!");
    
    return RT_EOK;
}
MSH_CMD_EXPORT(test_dev_cmd, test virtual device framework);
