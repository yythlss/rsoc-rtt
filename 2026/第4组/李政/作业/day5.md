# day5作业

## 1.连接通过mqtt协议，连接阿里云，上传数据
![image-20260822161942457](fig/image-20260822161942457.png)

![image-20260822162051077](fig/image-20260822162051077.png)

![image-20260822162004793](fig/image-20260822162004793.png)

![image-20260822162225879](fig/image-20260822162225879.png)

## 2.挂载文件系统存储数据

```bash
格式化：mkfs -t elm filesystem

挂载：mount filesystem /fal elm

检查存储数据：cat /fal/Data.txt
```

![image-20260822230149160](fig/image-20260822230149160.png)

## 3代码见附件

aht_mqtt_demo.c；data_storage.c