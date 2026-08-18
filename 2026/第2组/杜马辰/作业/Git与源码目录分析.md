# Git/Gitee 提交与 RT-Thread 源码目录分析

## 1. Git 基本信息配置

查看 Git 版本：

```bash
git --version
```

设置提交用户名：

```bash
git config --global user.name "填写 Gitee 用户名"
```

设置提交邮箱：

```bash
git config --global user.email "填写 Gitee 邮箱"
```

查看 Git 配置：

```bash
git config --global --list
```

## 2. 配置 Gitee SSH 登录

生成 SSH 密钥：

```bash
ssh-keygen -t ed25519 -C "填写 Gitee 邮箱"
```

查看公钥：

```bash
cat ~/.ssh/id_ed25519.pub
```

将公钥复制到 Gitee：

```text
Gitee → 设置 → SSH 公钥 → 添加公钥
```

测试 SSH 连接：

```bash
ssh -T git@gitee.com
```

## 3. 获取作业仓库并创建分支

克隆 RSOC-RTT 仓库：

```bash
cd /Users/gatesbil/Documents/Project/stm32
git clone https://gitee.com/rtthread/rsoc-rtt.git
cd rsoc-rtt
```

查看远程仓库：

```bash
git remote -v
```

查看本地和远程分支：

```bash
git branch
git branch -a
```

同步主分支：

```bash
git switch master
git pull origin master
```

创建个人作业分支：

```bash
git switch -c day1
```

分支用于保存个人作业修改，避免直接修改官方 `master` 分支。

## 4. 添加作业文件

创建个人作业目录：

```bash
mkdir -p 2026/第1组/姓名/作业
```

查看当前修改：

```bash
git status
```

添加作业文件：

```bash
git add 2026/第1组/姓名/作业/
```

检查暂存区内容：

```bash
git diff --cached
```

提交作业：

```bash
git commit -m "完成 STM32F407 Spark-1 RT-Thread 第一天作业"
```

查看提交记录：

```bash
git log --oneline --decorate -5
```

## 5. 推送分支并创建 Pull Request

第一次推送个人分支：

```bash
git push -u origin day1
```

以后继续提交时：

```bash
git add .
git commit -m "补充作业记录"
git push origin day1
```

在 Gitee 页面创建 Pull Request：

```text
源仓库：个人 fork 仓库
源分支：day1
目标仓库：RT-Thread/rsoc-rtt
目标分支：master
```

Pull Request 用于请求仓库维护者检查并合并作业内容。官方仓库没有个人写权限时，应先 Fork 官方仓库，再把个人分支推送到自己的远程仓库。

## 6. Git 常用命令

查看工作区状态：

```bash
git status
```

查看未暂存修改：

```bash
git diff
```

查看提交记录：

```bash
git log --oneline
```

查看远程地址：

```bash
git remote -v
```

撤销暂存：

```bash
git restore --staged 文件名
```

撤销工作区未提交修改：

```bash
git restore 文件名
```

`Everything up-to-date` 表示当前分支没有新的提交需要推送。空目录不会被 Git 跟踪，目录中至少需要存在一个文件。

## 7. RT-Thread 工程目录结构

以官方 Spark-1 SDK 为例：

```text
sdk-bsp-stm32f407-spark/
├── rt-thread/
├── libraries/
├── projects/
├── tools/
└── docs/
```

目录含义：

```text
rt-thread/：RT-Thread 内核和组件源码
libraries/：STM32 HAL、CMSIS 和芯片驱动库
projects/：基于 Spark-1 的示例工程
tools/：SCons 构建和工程配置工具
docs/：SDK 使用说明
```

## 8. RT-Thread 内核目录

```text
rt-thread/
├── src/
├── include/
├── components/
├── libcpu/
├── tools/
├── packages/
└── SConstruct
```

目录和文件作用：

```text
src/：线程、调度器、时钟、IPC、内存管理等内核实现
include/：RT-Thread 对外提供的头文件
components/：FinSH、文件系统、网络协议栈等可选组件
libcpu/：不同 CPU 架构的上下文切换和底层移植代码
tools/：工程构建、Kconfig 和 SCons 相关脚本
packages/：通过配置启用的外部软件包
SConstruct：SCons 构建入口
```

STM32F407 使用 Cortex-M4 内核，因此底层 CPU 相关代码位于：

```text
libcpu/arm/cortex-m4/
```

## 9. BSP 目录结构

Spark-1 BSP 目录：

```text
bsp/stm32/stm32f407-rt-spark/
├── board/
├── applications/
├── drivers/
├── libraries/
├── rtconfig.h
├── rtconfig.py
├── SConstruct
└── SConscript
```

目录和文件作用：

```text
board/：开发板启动、时钟、引脚和板级初始化
applications/：用户应用程序，例如 applications/main.c
drivers/：串口、GPIO、SPI 等外设驱动
libraries/：芯片 HAL 和 CMSIS 相关文件
rtconfig.h：RT-Thread 功能宏配置
rtconfig.py：SCons 编译器和工程配置
SConstruct：工程构建入口
SConscript：收集当前目录下需要编译的源文件
```

BSP 负责把 RT-Thread 内核移植到具体芯片和开发板。不同开发板可以有不同的 BSP，但它们共用 RT-Thread 内核、组件和构建工具。

## 10. FinSH 和 msh

RT-Thread 的命令行功能由 FinSH 组件提供。启用以下配置后，可以通过串口使用 `msh`：

```text
RT_USING_FINSH
FINSH_USING_MSH
```

常用命令：

```text
help：查看命令帮助
list_thread：查看线程
free：查看内存使用情况
list_timer：查看定时器
```

应用程序中使用 `rt_kprintf` 输出普通信息，使用 `LOG_D` 输出 Debug 日志。RT-Thread 启动后，串口出现 `msh >` 即表示 FinSH 命令行已经运行。

## 11. SCons 构建关系

```text
SConstruct
    ↓
读取 rtconfig.py、rtconfig.h 和 Kconfig 配置
    ↓
调用 tools/building.py
    ↓
收集 BSP、内核、驱动和组件源文件
    ↓
调用 arm-none-eabi-gcc
    ↓
生成 ELF、BIN 和 MAP 文件
```

进入 Spark-1 示例工程并编译：

```bash
source /Users/gatesbil/Documents/Project/stm32/rtthread-mac-env.sh
spark_project 01_kernel
scons -j4
```

只有被工程配置启用的 `RT_USING_*` 组件才会被加入编译。例如启用 FinSH 后，工程才会编译命令行相关源码并提供 `msh` 命令。

## 12. 作业提交内容

```text
1. 完成 Git 用户信息和 Gitee SSH 配置。
2. 创建个人分支并提交 STM32F407 Spark-1 作业文件。
3. 说明 git add、git commit、git push 和 Pull Request 的使用流程。
4. 说明 RT-Thread、BSP、components、libcpu 和 tools 目录的作用。
5. 说明 STM32F407 的 Cortex-M4 底层代码位置。
6. 说明 FinSH、msh 和 SCons 构建流程。
```

