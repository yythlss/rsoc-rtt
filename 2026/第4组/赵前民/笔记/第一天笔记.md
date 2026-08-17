# Day01



## task1：新建一个qemu-a9工程，并运行RT-Thread

​	**1. 创建项目**

<img src="C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817111304917.png" alt="image-20260817111304917" style="zoom: 67%;" />



**2. 编译后，下载，终端中显示运行信息**

![image-20260817111632842](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817111632842.png)



## task2：markdown语法学习

代码文本
```c
#include <stdio.h>

int main(void)
{
    printf("Hello World\n");
    return 0;
}
```



表格

| item1 | item2 | item3 |
| :---: | :---: | :---: |
|   1   |   2   |   3   |



`高亮语法`

**加粗**



## task3:基于RT-Thread 4.1.1版本新建stm32f407zg芯片工程

​	**1. 创建工程**

<img src="C:\Users\loong dragen\桌面\RTT学习\day01.assets\image-20260817115330973.png" alt="image-20260817115330973" style="zoom: 67%;" />

​	**2. 修改main函数代码**

```c
#include <rtthread.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

int main(void)
{
    int count = 1;

    rt_kprintf("Hello World\n");

    while (count++)
    {
//        LOG_D("Hello RT-Thread!");
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}

```



**3. 设置串口后编译，下载，终端中可见如下信息**

![image-20260817120500547](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817120500547.png)



**4. 可通过PS指令查看当前任务详情**

![image-20260817120630039](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817120630039.png)



## task4:git的使用

**1. 下载git和登录gitee后，先配置自己的全局身份，再配置一下公钥，方便后续上传代码**

​	全局身份的命令如下：

```sh
git config --global user.name 'YourName' 
git config --global user.email 'YourEmail'
git config --list	//可查看是否修改成功
```

<img src="C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817200757618.png" alt="image-20260817200757618" style="zoom:80%;" />

生成 RSA 密钥

```shell
ssh-keygen -t rsa
```

获取 RSA 公钥内容，并配置到ssh中

```shell
cat ~/.ssh/id_rsa.pub
```

![image-20260817202523379](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817202523379.png)

**2. 接下来可以去拉取项目了，直接去克隆/下载处复制git命令即可**

![image-20260817203845637](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817203845637.png)





### Q:如何贡献代码在他人仓库？

**1. 先把原作者的仓库fork一份在自己的仓库**

![image-20260817204528180](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817204528180.png)



**2. 然后拉取到自己的文件夹中，并且添加分支**

对应的命令如下

```she
git branch "Name" //创建分支，Name为分支的名字
git checkout "Name"  	//切换到对应分支中
```

![image-20260817204933439](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817204933439.png)



**3. 代码修改后，按如下命令提交**

```she
git add .				//添加修改
git commit -m "xxx"		//xxx是一下描述信息
git push origin Name	//Name是分支的名字
```

![image-20260817210037911](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817210037911.png)



**4. 提交代码**

![image-20260817210452856](C:\Users\loong dragen\桌面\RTT学习\source\day01\image-20260817210452856.png)

最后点击`创建Pull Request`，即可提交
