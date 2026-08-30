# RTT-学习日志-Day1

## markdown语法学习

### 代码插入

```
#include <stdio.h>

int main()
{
return 0;
}
```

### 表格插入相关



| ---  | ---  |
| ---- | ---- |
|      |      |

### 其他语法

**加粗语法**

`高亮语法`

图像插入![image-20260817100636809](figures/image-20260817100636809.png)





## 关于RTT-Stdio

### 任务一rtt断点调试

![image-20260817102123446](figures/image-20260817102123446.png)

## 星火一号

### 星火一号工程创建

#### 端口确认



![image-20260817102559963](figures/image-20260817102559963.png)

#### 新建项目



![image-20260817103806695](figures/image-20260817103806695.png)

![image-20260817104345415](figures/image-20260817104345415.png)

#### 任务三    rt_kprintf()

![image-20260817104204439](figures/image-20260817104204439.png)

![image-20260817104221356](figures/image-20260817104221356.png)

![image-20260817104255704](figures/image-20260817104255704.png)

# GIT的使用

## 创建仓库

![image-20260817193850149](figures/image-20260817193850149.png)

·![image-20260817195708890](figures/image-20260817195708890.png)

## git clone



![image-20260817200541589](figures/image-20260817200541589.png)





![image-20260817200633114](figures/image-20260817200633114.png)



## GIT 初始化相关

### 邮箱用户相关

user 和邮箱 我的电脑之前已经配置好，故这部分跳过



### 远程密钥相关

#### 输入指令

```$ ssh-keygen -t rsa -b 4096 -C "1394546446@qq.com"
$ ssh-keygen -t rsa -b 4096 -C "1394546446@qq.com"
```



![image-20260817201509787](figures/image-20260817201509787.png) 

#### 查看密钥

```
cat /c/Users/13945/.ssh/id_rsa.pub
```

![image-20260817231925741](figures/image-20260817231925741.png)

#### 设置中将公钥配置进入gitee

![image-20260817231901642](figures/image-20260817231901642.png)

#### 远程测试ssh gitee

```
ssh -T git@gitee.com
```



![image-20260817231955977](figures/image-20260817231955977.png)

#### git 上传指令

```
git init			  //初始化仓库

git add 文件名			//添加和提交

git commit -m "提交信息"

git push
```





![image-20260817220655304](figures/image-20260817220655304.png)

## GIT pullrequest

git原始命令行老久不用生疏了，我直接使用我之前使用的sourcetree了。

将gitee先将仓库进行fork

拉取到本地然后使用source tree同步

![image-20260817232207720](figures/image-20260817232207720.png)

然后可以看见提交状态和相关信息

![image-20260817232228576](figures/image-20260817232228576.png)