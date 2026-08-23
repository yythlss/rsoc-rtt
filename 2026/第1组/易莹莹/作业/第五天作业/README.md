# 作业1 论坛笔记
[RT-Thread-【RSOC】软件包与组件入门RT-Thread问答社区 - RT-Thread](https://club.rt-thread.org/ask/article/338a5a73f4fb796d.html)
# 作业2 使用AHT10软件包采集温湿度并上传到emqx
## 运行截图
![[ca0f6245a201499f33a303f3637b96d3.png]]
# 作业3 温湿度数据同步系统文件
## 运行截图
![[ea5ace48cb4c5c18e5899b70db084133.png]]
# 作业4 云端控制开发板上小灯翻转
## 运行截图
![[481b50ce039e77903e151603b97cb79c.png]]
# 作业代码
## aht10读取温湿度代码
[aht10_example.c](aht10_example.c)
## 文件操作相关代码
[drv_filesystem.c](drv_filesystem.c)
## mqtt订阅发布相关代码
[mqtt_test.c](mqtt_test.c)
# 作业
- [x] 将今天的任务完成并将学习笔记上传至RT-Thread论坛，笔记标题命名为：【RSOC26】+ xxx
- [x] 使用AHT10软件包采集温湿度并上传到emqx（如果你有其他MQTT服务器也可上传到对应服务器上）；
- [x] 梳理文件系统启动流程，熟悉shell命令使用。能够将font分区给挂载上。在温湿度上传上传云端的同时，将数据同步放在文件系统处，文件名为：Data.txt；
	文件内容： Temp：XX ; Humi：XX ; Count： 1（自上电起所采集的数据次数）
	                  Temp：XX ; Humi：XX ; Count： 2（自上电起所采集的数据次数）
	                  Temp：XX ; Humi：XX ; Count： 3（自上电起所采集的数据次数）
- [x] 利用云端给开发板发送指令然后实现小灯翻转；
- [x] 熟悉其他软件包