# day2笔记

## 1.env环境

```bash
# 1.生成vscode工程
 scons --target=vsc
#2.vscode打开
code .
#3.vscode编译
scons -j20
#4.打包工程
scons --dist
menuconfig #设置配置
```

![image-20260818223913304](fig/image-20260818223913304.png)

![image-20260818225546819](fig/image-20260818225546819.png)

## 2.RT-Thread 启动流程

![启动流程](fig/rtt_startup.png)

stm32f407汇编文件

![image-20260818230329653](fig/image-20260818230329653.png)

上电或复位后，CPU 最终会执行到 `Reset_Handler`



![image-20260818233336472](fig/image-20260818233336472.png)

![image-20260818233611744](fig/image-20260818233611744.png)

## 3.线程

静态和动态创建

