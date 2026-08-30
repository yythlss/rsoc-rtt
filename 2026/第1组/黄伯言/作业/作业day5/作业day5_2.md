此项目在项目一的基础上，增加将数据写入开发板 Flash 文件系统的功能。

#### 1. 配置 Flash 与文件系统组件

在 `RT-Thread Settings` 中，按顺序使能以下组件（保存后会自动更新工程）：

- **SFUD**：`RT-Thread Components → Device Drivers → Using SFUD drivers`

- **FAL**：`RT-Thread Components → Device Drivers → Using FAL (Flash Abstraction Layer)`

- **DFS**：`RT-Thread Components → DFS: device virtual file system`

- **ELM-FAT**：`DFS → Using ELM FAT file system`

#### 2. 修改并注册分区表

在 `board.h` 或 `fal_cfg.h` 中（根据你的BSP位置）定义 Flash 分区表，确保有一个分区用于挂载文件系统。

```
/* 示例：在 fal_cfg.h 中 */
#define NOR_FLASH_DEV_NAME             "nor_flash"
#define FAL_PART_TABLE                                                                          \
{                                                                                               \
    {FAL_PART_MAGIC_WORD, "fs_part", NOR_FLASH_DEV_NAME, 0, 1024*1024, 0}, \
}
```



#### 3. 修改应用代码，增加文件写入逻辑

在项目一的 `aht10_publish_thread_entry` 函数中，增加文件操作代码。

```
/* 在 while(1) 循环内，发布消息后添加 */
int fd = -1;
char log_line[64];
sprintf(log_line, "Temp: %.2f ; Humi: %.2f ; Count: %d\n", temp, humi, count);
fd = open("/flash/Data.txt", O_WRONLY | O_CREAT | O_APPEND, 0);
if (fd >= 0)
{
 write(fd, log_line, strlen(log_line));
 close(fd);
}
```




