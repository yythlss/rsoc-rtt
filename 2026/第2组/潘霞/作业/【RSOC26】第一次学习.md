# DAY1 学习记录

# 一、Markdown 基本语法学习

今天学习了 Markdown 的常用写法，包括标题、代码块、表格、行内代码、加粗和斜体等内容，后续可以用 Typora 更方便地整理课程笔记和实验过程。

## 1. 标题语法

```
# 一级标题
## 二级标题
### 三级标题
```

## 2. 常用格式

`行内代码`
**加粗**
***斜体***

# 二、Git 与 Gitee 基础操作

今天学习了 Git 和 Gitee 的基础使用，主要包括用户信息配置、远程仓库连接、文件提交、分支创建和远程推送。

## 1. 基本配置与连接

```
git config --global user.name "用户名"      # 配置提交时显示的用户名
git config --global user.email "邮箱"       # 配置提交时显示的邮箱
git config --list                           # 查看当前 Git 配置
ssh -T git@gitee.com                        # 测试 SSH 是否能正常连接 Gitee
git remote -v                               # 查看当前远程仓库地址
git remote set-url origin 仓库地址          # 修改当前仓库的远程地址
```

## 2. 仓库同步与提交

```
git init                    # 初始化当前目录为 Git 仓库
git fetch origin master     # 同步远程 master 分支信息到本地
git add file/path           # 添加文件到暂存区
git commit -m "message"     # 提交暂存区内容并填写提交说明
git pull                    # 拉取远程内容到本地
git push origin master      # 将本地内容推送到远程仓库
```

## 3. 分支操作

```
git checkout -b test       # 基于当前分支创建并切换到 test 分支
```

# 三、RT-Thread 工程与调试

今天还学习了 RT-Thread Studio 中工程创建、编译、下载和调试的基本流程。

## 工程创建与调试

- 检查 ST-Link 驱动是否正常
- 基于 RT-Thread 4.1.1 创建 STM32F407ZG 工程
- 编译工程并生成可执行文件
- 将程序下载到开发板
- 使用 ST-Link 进行断点调试

工程调试成功后，程序可以停在 `main()` 函数中，说明工程已经正常下载并运行。

# 四、RT-Thread 启动流程

通过官网提供的启动流程图，了解了 RT-Thread 从启动文件到用户 `main()` 函数的大致执行过程。

- `startup_xx.S` 先执行
- 进入编译器相关入口函数
- 调用 `rtthread_startup()`
- 初始化硬件、定时器和调度器
- 创建主线程、定时器线程和空闲线程
- 启动系统调度器
- 最后进入 `main()` 函数