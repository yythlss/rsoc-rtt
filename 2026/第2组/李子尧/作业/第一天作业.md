# RSOC 2026 第一天作业

> 日期：2026-08-17
> 学员：李子尧
> 内容：RT-Thread Studio 基础使用、STM32F407 工程流程、Markdown 与 Git/Gitee 协作实践

---

## 一、Day1 任务目标与完成内容

第一天的作业主要围绕两个方向展开：一是熟悉 RT-Thread 的基本开发环境和 STM32 工程开发流程，二是掌握后续两周都会持续使用的 Markdown 与 Git/Gitee 协作方式。

当天需要完成的主要任务如下：

| 任务 | 主要内容 |
|---|---|
| 开发环境准备 | 安装并熟悉 RT-Thread Studio |
| SDK 配置 | 准备 RT-Thread 源码及 STM32F4 相关支持包 |
| 工程实践 | 学习创建 STM32F407 RT-Thread 工程 |
| 编译与下载 | 理解 Build、Download、Run、Debug 的基本流程 |
| Markdown | 使用 Markdown 编写学习笔记与作业 |
| Git | 掌握 clone、add、commit、push、branch 等操作 |
| Gitee 协作 | Fork 官方仓库并建立个人开发仓库 |
| PR | 按要求完成初始化模板与每日内容的分离提交 |

Day1 的重点并不仅仅是完成一个 Hello World，而是把后续开发所需的基本工作流建立起来：

```text
课程学习
   ↓
本地实践
   ↓
整理 Markdown
   ↓
Git 提交
   ↓
Push 到个人 Fork
   ↓
Pull Request
```

---

## 二、RT-Thread Studio 开发环境准备

RT-Thread Studio 将 RT-Thread 工程创建、代码编辑、编译、下载以及调试等功能集成在同一个开发环境中。

当天首先熟悉了 Studio 中几个比较重要的部分：

```text
RT-Thread Studio
│
├── RT-Thread Source Code
├── MCU / Chip Support Package
├── Toolchain
├── Project
├── Build
├── Download
└── Debug
```

其中 SDK Manager 用来管理 RT-Thread 开发所需的 SDK、芯片支持包和相关工具。

针对 STM32F407 的开发，需要保证至少具备：

- RT-Thread Studio；
- 课程指定版本的 RT-Thread 源码；
- STM32F4 芯片支持包；
- 对应编译工具链；
- ST-Link 驱动和调试环境。

这一步让我对嵌入式 IDE 和普通代码编辑器的区别更加明确：Studio 不只是负责写代码，还承担了工程配置、编译工具链调用以及调试器连接等工作。

---

## 三、STM32F407 RT-Thread 工程创建流程

课程中以 STM32F407 为例演示 RT-Thread 工程的创建。

整个过程可以整理为：

```text
新建 RT-Thread Project
        ↓
选择芯片 / 开发板
        ↓
选择 STM32F407
        ↓
选择 RT-Thread 版本
        ↓
配置工程参数
        ↓
生成工程
        ↓
修改 applications/main.c
        ↓
Build
        ↓
Download
        ↓
Run / Debug
```

在创建工程时，不能只关注 MCU 型号，还需要同时确认：

- RT-Thread 版本；
- 芯片型号；
- BSP；
- Toolchain；
- Debugger；
- Console / UART 配置。

串口调试时课程采用：

```text
115200 baud
```

后续很多 RT-Thread 的运行信息都可以通过串口终端观察，包括启动日志、调试打印和 MSH 命令。

---

## 四、理解 Build、Download、Run 和 Debug

第一天课程中一个比较基础但容易混淆的问题，是 Build、Download、Run、Debug 实际上属于不同阶段。

### 1. Build

Build 是将源代码转换为目标芯片可以运行的程序。

大体过程：

```text
.c / .h
   ↓
预处理
   ↓
编译
   ↓
汇编
   ↓
链接
   ↓
ELF / BIN
```

其中：

- `.elf` 通常包含代码、符号以及调试信息；
- `.bin` 更接近最终写入 Flash 的纯二进制数据。

### 2. Download

Download 是把编译生成的程序写入目标 MCU。

```text
PC
 ↓
RT-Thread Studio
 ↓
ST-Link
 ↓
STM32F407
```

### 3. Run

下载完成以后，MCU 正常执行程序属于 Run。

### 4. Debug

Debug 则是在调试器控制下运行程序，可以进行：

- 设置断点；
- 单步执行；
- 查看变量；
- 查看寄存器；
- 分析函数调用过程。

因此可以简单总结为：

| 阶段 | 作用 |
|---|---|
| Build | 把源码变成程序 |
| Download | 把程序写入 MCU |
| Run | MCU 正常运行程序 |
| Debug | 在调试器控制下分析程序 |

---

## 五、Markdown 文档实践

RSOC 要求每天使用 Markdown 提交学习笔记和作业，因此 Markdown 也是第一天必须掌握的工具。

实际整理文档时主要使用以下语法。

### 标题层级

```markdown
# 一级标题
## 二级标题
### 三级标题
```

### 加粗与行内代码

```markdown
**重点内容**

`git status`
```

### 代码块

````markdown
```bash
git status
git add .
git commit -m "message"
```
````

### 表格

```markdown
| 项目 | 说明 |
|---|---|
| Build | 编译 |
| Download | 下载 |
```

### 图片路径

图片如果需要放入 Markdown，最好统一放入相对目录，例如：

```text
figures/
```

引用形式：

```markdown
![说明](./figures/example.png)
```

相比 Windows 本地绝对路径：

```text
D:\xxx\xxx.png
```

相对路径更适合 Git 仓库，因为整个工程移动或者上传 Gitee 后路径仍然能够保持有效。

---

## 六、Git 本地版本管理实践

第一天开始正式使用 Git 管理自己的学习内容。

Git 最重要的并不是记住命令，而是先理解修改究竟保存在哪里。

```text
Working Tree
     │
     │ git add
     ▼
Staging Area
     │
     │ git commit
     ▼
Local Repository
     │
     │ git push
     ▼
Remote Repository
```

### 查看当前状态

```bash
git status
```

可以查看：

- 当前所在分支；
- 修改了哪些文件；
- 哪些文件尚未暂存；
- 哪些文件已经进入暂存区。

### 加入暂存区

```bash
git add <文件>
```

或者：

```bash
git add .
```

`git add` 并不是提交代码，而是在选择：

> 下一次 Commit 准备包含哪些修改。

### 创建 Commit

```bash
git commit -m "提交说明"
```

Commit 之后会在**本地仓库**形成一次版本记录。

### Push

```bash
git push origin <分支名>
```

这一步才会真正把本地 Commit 上传到 Gitee。

所以：

```text
git add
≠
git commit
≠
git push
```

三者属于完全不同的阶段。

---

## 七、Fork 仓库与远程仓库关系

这次作业并不是直接修改 RT-Thread 官方仓库，而是采用开源项目中常见的 Fork 工作流。

我的仓库关系可以理解为：

```text
RT-Thread 官方仓库
        │
        │ Fork
        ▼
我的 Gitee 仓库
        │
        │ Clone
        ▼
我的本地电脑
```

### Fork

Fork 发生在 Gitee 平台：

```text
官方远程仓库
       ↓
个人远程仓库
```

### Clone

Clone 则是：

```text
个人远程仓库
       ↓
本地电脑
```

对应命令：

```bash
git clone <仓库地址>
```

这两个概念一开始比较容易混淆，但实际数据方向完全不同。

---

## 八、origin 与 upstream 的配置

Fork 之后，本地实际上需要区分两个远程仓库。

### origin

`origin` 对应自己的 Fork。

本次项目中可以理解为：

```text
origin
   ↓
niconiconilzy/rsoc-rtt
```

平时自己的修改主要 Push 到这里。

### upstream

`upstream` 对应 RT-Thread 官方原始仓库。

```text
upstream
   ↓
rtthread/rsoc-rtt
```

添加方式：

```bash
git remote add upstream <官方仓库地址>
```

查看当前配置：

```bash
git remote -v
```

最终关系：

```text
             upstream
       RT-Thread/rsoc-rtt
               ▲
               │
               │ Pull Request
               │
             origin
      niconiconilzy/rsoc-rtt
               ▲
               │
               │ git push
               │
             Local
```

因此自己通常：

```text
Push → origin
```

而不是直接：

```text
Push → upstream
```

官方内容发生更新时，可以：

```bash
git fetch upstream
```

获取上游最新状态。

---

## 九、Day1 两次 Pull Request 的拆分

第一天作业要求至少完成两次 PR。

这让我第一次实际理解了：

> 一次 PR 应该尽量只解决一个明确的问题。

### 第一次 PR：初始化模板

第一次 PR 只负责建立个人目录以及基础模板。

内容主要属于：

```text
2026/
└── 第2组/
    └── 李子尧/
        ├── 作业/
        └── 笔记/
```

第一次提交不能把完整 Day1 作业和笔记一起塞进去，否则就失去了“初始化模板”这一独立提交的意义。

### 第二次 PR：Day1 内容

第二次 PR 再提交：

```text
第一天作业.md
+
第一天笔记.md
```

因此整个思路为：

```text
PR 1
初始化个人模板
     ↓
官方审核

PR 2
提交 Day1 作业与笔记
     ↓
官方审核
```

与一次性把所有修改上传相比，这种方式的优势是：

- PR 目的更加明确；
- Reviewer 更容易审核；
- Git 历史更加清楚；
- 如果某部分存在问题，可以单独修改；
- 更符合真实开源项目的协作方式。

---

## 十、实际遇到的问题与解决过程

Day1 实际操作中，Git 部分遇到了几个比较有代表性的问题。

### 1. Git Bash 无法直接进入带空格的 Windows 路径

我的项目目录位于：

```text
D:\stm32\rtt summer school
```

最开始直接输入：

```bash
cd D:\stm32\rtt summer school
```

Git Bash 提示：

```text
bash: cd: too many arguments
```

原因有两个。

第一，Git Bash 对 Windows 盘符的表示方式不同：

```text
D:\
```

对应：

```text
/d/
```

第二，目录：

```text
rtt summer school
```

中间存在空格，Shell 会把它识别成多个参数。

因此正确写法为：

```bash
cd "/d/stm32/rtt summer school"
```

这里使用双引号把整个路径作为同一个参数。

这个问题让我进一步理解了 Windows CMD / PowerShell 路径与 Bash 路径表示方式之间的差异。

---

### 2. 修改自己的 Fork 后如何贡献到官方仓库

刚开始容易产生一个误解：

> 自己的 Fork 改完以后，是不是直接 Merge 到官方仓库？

实际上普通贡献者通常没有官方仓库的直接写权限。

正确过程是：

```text
官方仓库
   ↓ Fork
个人 Fork
   ↓ Clone
本地修改
   ↓ Commit
个人本地仓库
   ↓ Push
个人 Gitee Fork
   ↓ Pull Request
官方仓库
   ↓ Review
Merge
```

因此自己完成的是：

```text
Commit
Push
Create PR
```

最终是否 Merge，由仓库维护者或 Reviewer 决定。

---

### 3. PR 的 Source 与 Target

创建 Pull Request 时还需要明确：

- Source：修改来自哪里；
- Target：希望合入哪里。

例如当天任务如果要求提交到官方 `day1` 分支，那么应当是：

```text
我的 Fork / 我的分支
          ↓
官方仓库 / day1
```

而不是看到 `master` 就默认选择 `master`。

因此提交 PR 前应该先阅读仓库当天的要求，再确认目标分支。

---

## 十一、Day1 Git 工作流整理

结合第一天实际操作，我把后续比较适合自己的 Git 工作流整理为：

### 开始任务前

```bash
git status
git fetch upstream
```

先确认当前工作区状态，同时获取官方最新内容。

### 创建当天工作分支

```bash
git checkout -b <branch-name>
```

或者：

```bash
git switch -c <branch-name>
```

### 修改完成后检查

```bash
git status
git diff
```

### 选择本次需要提交的文件

```bash
git add <file>
```

相比直接无脑：

```bash
git add .
```

逐个加入需要提交的文件更加安全。

### 查看暂存内容

```bash
git diff --cached
```

### 创建 Commit

```bash
git commit -m "说明本次修改内容"
```

### Push 到个人 Fork

```bash
git push -u origin <branch-name>
```

最后在 Gitee 页面创建 Pull Request。

完整过程：

```text
同步上游
   ↓
创建分支
   ↓
修改文件
   ↓
git status
   ↓
git diff
   ↓
git add
   ↓
git diff --cached
   ↓
git commit
   ↓
git push origin
   ↓
Pull Request
```

---

## 十二、Day1 作业总结

第一天的作业让我完成了两个方面的基础准备。

一方面，熟悉了 RT-Thread Studio 的基本开发流程，并对：

```text
Project
Build
Download
Run
Debug
```

几个阶段有了初步认识，也了解了 STM32F407 工程、ST-Link 和串口终端在整个开发流程中的位置。

另一方面，通过真正操作 Gitee 仓库，开始理解 Git 开源协作中的：

```text
Fork
Clone
Branch
Add
Commit
Push
Pull Request
Review
Merge
```

这些操作之间的关系。

尤其是：

```text
Working Tree
     ↓
Staging Area
     ↓
Local Repository
     ↓
Remote Repository
```

以及：

```text
upstream
官方仓库
   ▲
   │ PR
origin
个人 Fork
   ▲
   │ push
Local
```

这两套关系，是 Day1 Git 学习中最重要的内容。

相比单纯把命令记下来，实际完成 Fork、Clone、修改、Push 和 PR 后，对 Git 中“本地”和“远程”、“个人仓库”和“官方仓库”的区别更加清楚。

后续每日作业可以继续沿用这一工作方式，将每天的内容放在独立提交或独立分支中，保持提交历史清晰，也方便组长和维护者进行审核。
