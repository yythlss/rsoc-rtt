# RT-Thread 虚拟字符设备 test_dev

本实验实现了基于 RT-Thread I/O 设备框架的虚拟字符设备 `test_dev`。

## 完成内容

- 创建 `RT_Device_Class_Char` 字符设备并调用 `rt_device_register()` 注册。
- 用 `struct rt_device_ops` 实现 `init`、`open`、`close`、`read`、`write`、`control`。
- 使用 `INIT_DEVICE_EXPORT()` 自动注册，未修改 `main.c`。
- 提供 MSH 命令 `test_dev_test`，按 find、init、open、read、write、control、close 验证完整调用链。

`read` 返回 `RT-Thread test device`；`write` 输出接收到的文本；`TEST_DEV_GET_STATUS` 返回初始化状态和累计打开次数。

