# RT-Thread 文件系统与 MQTT 实战学习笔记

## 一、任务目标

基于星火一号开发板（STM32F4 + W25Q64 SPI Flash），完成以下三个任务：

1. **温湿度采集与上传**：使用 AHT10 软件包采集温湿度数据，通过 MQTT 协议上传到 emqx 云端
2. **文件系统与数据存储**：梳理文件系统启动流程，将 font 分区挂载为文件系统，同步存储温湿度数据到 `Data.txt`
3. **云端远程控制**：通过 MQTT 发送指令控制开发板 LED 翻转

---

## 二、系统架构

```
┌─────────────────────────────────────────────────────────────┐
│                     应用层 (applications)                     │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ aht10_example│  │  test.c      │  │  drv_filesystem  │  │
│  │ (采集+存储)  │  │ (MQTT客户端) │  │  (文件系统初始化)│  │
│  └──────────────┘  └──────────────┘  └──────────────────┘  │
├─────────────────────────────────────────────────────────────┤
│                     组件层 (components)                       │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │   DFS    │  │  ELM-FAT │  │   FAL    │  │  Kawaii  │   │
│  │(文件系统)│  │(FAT32驱动)│  │(Flash抽象)│  │  MQTT    │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
├─────────────────────────────────────────────────────────────┤
│                     驱动层 (drivers)                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │   SFUD   │  │  W25Q64  │  │  SPI     │  │  I2C     │   │
│  │(Flash驱动)│  │(SPI Flash)│  │  总线    │  │  总线    │   │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘   │
├─────────────────────────────────────────────────────────────┤
│                     硬件层                                    │
│  STM32F407 + W25Q64 (8MB) + AHT10 + RW007 WiFi + LED       │
└─────────────────────────────────────────────────────────────┘
```

---

## 三、关键配置

### 3.1 .config 配置项

```ini
# 文件系统相关
CONFIG_RT_USING_DFS=y
CONFIG_DFS_USING_POSIX=y
CONFIG_RT_USING_DFS_ELMFAT=y
CONFIG_RT_DFS_ELM_MAX_SECTOR_SIZE=4096

# Flash 抽象层
CONFIG_RT_USING_FAL=y
CONFIG_FAL_PART_HAS_TABLE_CFG=y

# SPI Flash 驱动
CONFIG_RT_USING_SFUD=y
CONFIG_BSP_USING_SPI_FLASH=y
CONFIG_BSP_USING_FAL=y

# MQTT 客户端
CONFIG_PKG_USING_KAWAII_MQTT=y
```

### 3.2 rtconfig.h 关键宏定义

```c
#define RT_USING_DFS
#define DFS_USING_POSIX
#define RT_USING_DFS_ELMFAT
#define RT_DFS_ELM_MAX_SECTOR_SIZE 4096

#define RT_USING_FAL
#define FAL_PART_HAS_TABLE_CFG

#define RT_USING_SFUD
#define BSP_USING_SPI_FLASH
#define BSP_USING_FAL
```

### 3.3 FAL 分区表配置

```c
// fal_cfg.h
#define FAL_PART_TABLE                                    \
{                                                         \
    {FAL_PART_MAGIC_WORD, "app",        "onchip_flash_128k", 0x00000000, 0x00060000, 0}, \
    {FAL_PART_MAGIC_WORD, "param",      "onchip_flash_128k", 0x00060000, 0x000a0000, 0}, \
    {FAL_PART_MAGIC_WORD, "easyflash",  "W25Q64",           0x00000000, 0x00080000, 0}, \
    {FAL_PART_MAGIC_WORD, "download",   "W25Q64",           0x00080000, 0x00100000, 0}, \
    {FAL_PART_MAGIC_WORD, "wifi_image", "W25Q64",           0x00180000, 0x00080000, 0}, \
    {FAL_PART_MAGIC_WORD, "font",       "W25Q64",           0x00200000, 0x00300000, 0}, \
    {FAL_PART_MAGIC_WORD, "filesystem", "W25Q64",           0x00500000, 0x00300000, 0}, \
}
```

**分区说明**：
| 分区名 | Flash 设备 | 偏移 | 大小 | 用途 |
|--------|-----------|------|------|------|
| app | onchip_flash_128k | 0x000000 | 384KB | 应用程序 |
| param | onchip_flash_128k | 0x060000 | 640KB | 参数存储 |
| easyflash | W25Q64 | 0x000000 | 512KB | EasyFlash |
| download | W25Q64 | 0x080000 | 1MB | OTA 下载 |
| wifi_image | W25Q64 | 0x180000 | 512KB | WiFi 固件 |
| **font** | W25Q64 | 0x200000 | 3MB | **字体/数据** |
| **filesystem** | W25Q64 | 0x500000 | 3MB | **文件系统** |

---

## 四、文件系统启动流程

### 4.1 启动时序

```
系统启动
    │
    ▼
┌─────────────────┐
│  fal_init()     │  初始化 FAL 框架
│  - 注册 Flash   │  注册 onchip_flash_128k 和 W25Q64
│  - 加载分区表   │  创建 7 个分区
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ SFUD 初始化     │  探测 SPI Flash
│ - 读取 SFDP    │  识别 W25Q64CV (8MB)
│ - 设置参数     │  创建 spi20 设备
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ FAL 块设备创建  │  fal_blk_device_create("filesystem")
│ - 创建块设备   │  注册为 "filesystem" 块设备
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ romfs 挂载      │  dfs_mount("romfs", "/", "rom", 0, 0)
│ - 挂载根目录   │  提供 /fal, /font 等目录入口
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ ELM-FAT 挂载    │  dfs_mount("filesystem", "/fal", "elm", 0, 0)
│ - 挂载 FAT 分区 │  成功挂载到 /fal
└─────────────────┘
```

### 4.2 关键代码解析

#### 4.2.1 FAL 初始化

```c
// fal_flash.c
int fal_init(void)
{
    /* 初始化 Flash 设备 */
    fal_flash_init();
    
    /* 加载分区表 */
    fal_partition_init();
    
    return 0;
}
```

#### 4.2.2 FAL 块设备创建

```c
// fal.c
rt_device_t fal_blk_device_create(const char *partition_name)
{
    /* 查找分区 */
    fal_part = fal_partition_find(partition_name);
    
    /* 创建块设备 */
    blk_dev = rt_calloc(1, sizeof(struct fal_blk_device));
    blk_dev->fal_part = fal_part;
    
    /* 注册为 RT-Thread 块设备 */
    rt_device_register(&blk_dev->parent, partition_name, RT_DEVICE_FLAG_RDWR);
    
    return &blk_dev->parent;
}
```

#### 4.2.3 ELM-FAT 挂载

```c
// dfs_elm.c
int dfs_elm_mount(struct dfs_filesystem *fs, unsigned long rwflag)
{
    /* 打开块设备 */
    rt_device_open(fs->dev_id, RT_DEVICE_OFLAG_RDWR);
    
    /* 挂载 FatFs */
    result = f_mount(fat, (const TCHAR *)logic_nbr, 1);
    
    /* 打开根目录验证 */
    f_opendir(dir, drive);
    
    return result;
}
```

### 4.3 挂载点结构

```
/ (romfs)
├── fal/          ← filesystem 分区 (ELM-FAT)
│   └── (FAT 文件系统内容)
├── font/         ← font 分区 (ELM-FAT)
│   └── Data.txt  ← 温湿度数据
└── sdcard/       ← SD 卡 (如果启用)
```

---

## 五、温湿度采集与存储实现

### 5.1 数据采集线程

```c
// aht10_example.c
static void aht10_thread_entry(void *param)
{
    while (1)
    {
        /* 读取温湿度 */
        aht10_read_temp_humi(&temp, &humi);
        
        /* 打印到终端 */
        rt_kprintf("temp = %.2f humi = %.2f\n", temp, humi);
        
        /* 上传到 MQTT */
        if (mqtt_ready)
        {
            aht10_publish_mqtt(temp, humi, data_count);
        }
        
        /* 存储到文件 */
        aht10_save_to_file(temp, humi, data_count);
        
        data_count++;
        rt_thread_mdelay(2000);  // 2 秒采集一次
    }
}
```

### 5.2 MQTT 发布

```c
static void aht10_publish_mqtt(float temp, float humi, int count)
{
    char payload[128];
    rt_snprintf(payload, sizeof(payload),
        "{\"temp\":%.2f,\"humi\":%.2f,\"count\":%d}",
        temp, humi, count);
    
    mqtt_publish(&client, "aht10-send", payload, strlen(payload), 0, 0);
}
```

### 5.3 文件存储

```c
static void aht10_save_to_file(float temp, float humi, int count)
{
    char buf[96];
    int fd, len;
    
    len = rt_snprintf(buf, sizeof(buf),
        "Temp：%.2f ; Humi：%.2f ; Count：%d\n",
        temp, humi, count);
    
    /* 使用 POSIX API 打开文件 */
    fd = open("/font/Data.txt", O_WRONLY | O_CREAT | O_APPEND);
    if (fd >= 0)
    {
        write(fd, buf, len);
        close(fd);
    }
}
```

---

## 六、MQTT 远程控制实现

### 6.1 订阅回调

```c
// test.c
static void sub_topic_handle1(mqtt_client_t *client, mqtt_message_t *msg)
{
    char *payload = (char *)msg->payload;
    
    /* 检测 LED 控制指令 */
    if (rt_strstr(payload, "led") != RT_NULL)
    {
        rt_kprintf("Received LED control command\n");
        aht10_led_toggle();  // 翻转 LED
    }
}
```

### 6.2 LED 控制函数

```c
// aht10_example.c
void aht10_led_toggle(void)
{
    static int led_state = 0;
    led_state = !led_state;
    
    if (led_state)
    {
        rt_pin_write(LED_PIN, PIN_HIGH);
        rt_kprintf("LED ON\n");
    }
    else
    {
        rt_pin_write(LED_PIN, PIN_LOW);
        rt_kprintf("LED OFF\n");
    }
}
```

### 6.3 测试方法

在 MQTTX 发布消息到 `sub_topic1`：

```json
{"cmd": "led"}
```

或简单发送：
```
led
```

---

## 七、遇到的问题与解决方案

### 7.1 编译错误：dfs_file.h 找不到

**问题**：
```
fatal error: dfs_file.h: No such file or directory
```

**原因**：`.config` 未启用 DFS 组件，makefile 未包含 DFS 头文件路径

**解决**：
1. 在 RT-Thread Settings 中启用 DFS、ELM-FAT、FAL、SFUD 组件
2. 清理项目并重新构建，让 scons 重新生成 makefile

### 7.2 链接错误：undefined reference to `aht10_led_toggle`

**问题**：
```
undefined reference to `aht10_led_toggle'
```

**原因**：`aht10_led_toggle` 在 `aht10_example.c` 中定义为 `static`，无法被 `test.c` 链接

**解决**：去掉 `static` 关键字，改为外部链接

### 7.3 文件系统挂载失败

**问题**：
```
mount failed (ret=-1)
```

**原因**：
1. `mkdir("/font", 0)` 试图在 romfs（只读）上创建目录失败
2. `dfs_mount` 检查挂载点是否存在时失败

**解决**：在 `drv_filesystem.c` 的 romfs 根目录预定义 `font` 目录：
```c
const struct romfs_dirent _romfs_root[] =
{
    {ROMFS_DIRENT_DIR, "fal", RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "font", RT_NULL, 0},  // 新增
};
```

### 7.4 重复挂载冲突

**问题**：`filesystem` 分区已被 `drv_filesystem.c` 挂载到 `/fal`，再次挂载失败

**解决**：改用 `font` 分区，避免冲突

---

## 八、Shell 命令速查

```bash
# 文件系统操作
ls /                    # 列出根目录
ls /font                # 列出 font 分区
cat /font/Data.txt      # 查看数据文件
df -h                   # 查看文件系统使用情况

# 手动挂载
mkfs -t elm font        # 格式化 font 分区
mount font /font elm    # 手动挂载

# WiFi 操作
wifi scan               # 扫描 WiFi
wifi join <ssid> <pwd>  # 连接 WiFi
ifconfig                # 查看网络状态

# MQTT 操作
ka_mqtt                 # 启动 MQTT 客户端

# 应用命令
fs_mount_data           # 挂载 font 分区
aht10_thread_init       # 启动温湿度采集
```

---

## 九、关键文件清单

| 文件路径 | 说明 |
|---------|------|
| `applications/aht10_example.c` | 温湿度采集、存储、LED 控制主逻辑 |
| `packages/kawaii-mqtt-latest/test/test.c` | MQTT 客户端实现 |
| `libraries/Board_Drivers/drv_filesystem.c` | 文件系统初始化 |
| `libraries/Board_Drivers/fal/fal_cfg.h` | FAL 分区表配置 |
| `rt-thread/components/dfs/filesystems/elmfat/dfs_elm.c` | ELM-FAT 驱动 |
| `rt-thread/components/dfs/src/dfs_fs.c` | DFS 文件系统框架 |
| `rt-thread/components/finsh/msh.c` | Shell 命令处理 |

---

## 十、学习总结

### 10.1 文件系统核心概念

1. **DFS (Device File System)**：RT-Thread 的设备文件系统框架，提供统一的文件操作接口
2. **ELM-FAT**：基于 FatFs 的 FAT32 文件系统驱动
3. **FAL (Flash Abstraction Layer)**：Flash 抽象层，统一管理片内 Flash 和片外 SPI Flash
4. **SFUD (Serial Flash Universal Driver)**：串行 Flash 通用驱动库

### 10.2 挂载流程要点

1. 先初始化 FAL，加载分区表
2. 使用 `fal_blk_device_create()` 将分区封装为块设备
3. 使用 `dfs_mount()` 挂载文件系统
4. 挂载点必须在已挂载的文件系统中存在

### 10.3 注意事项

1. **扇区大小**：W25Q64 的扇区大小是 4096 字节，需要配置 `RT_DFS_ELM_MAX_SECTOR_SIZE=4096`
2. **romfs 只读**：romfs 是只读文件系统，不能在其上创建目录
3. **分区冲突**：同一个分区不能被多次挂载
4. **POSIX API**：使用 `open/write/close` 等 POSIX API 操作文件

---

## 附录：完整运行日志

```
msh />fs_mount_data
[I/FAL] The FAL block device (font) created successfully
[17406] I/aht10: font block device ready
[17411] I/aht10: mount point /font created
[17416] I/aht10: trying to mount font to /font ...
f_mount result: 0
[17431] I/aht10: mount success

msh />ka_mqtt
[I] >> The ID of the Kawaii client is: hyj123
[I] >> mqtt connect success...
[I] >> MQTT connected, ready to auto publish AHT10 data.

msh />aht10_thread_init
temp = 33.85 humi = 66.42
temp = 33.85 humi = 66.49
temp = 33.83 humi = 66.60
...

msh />ls /font
Directory /font:
Data.txt            2246

msh />cat /font/Data.txt
Temp：33.85 ; Humi：66.42 ; Count：1
Temp：33.85 ; Humi：66.49 ; Count：2
Temp：33.83 ; Humi：66.60 ; Count：3
...
```

---

