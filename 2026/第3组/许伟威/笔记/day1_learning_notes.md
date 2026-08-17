## 【RSOC26】第一天学习笔记
记录人：许伟威
日期：`2026.08.17`

---

## 一、环境搭建

### 1.1 软件安装

| 软件 | 版本/说明 | 用途 |
|------|-----------|------|
| RT-Thread Studio | 2.3.0+（最新 2.3.6） | 一站式 IDE，集成工具链/Env |
| RT-Thread 标准版源码 | 4.0.3（默认）或 4.1.1 | 内核源码 |
| STM32F4 芯片支持包 | 通过 SDK Manager 下载 | 芯片 BSP |
| Git | 最新版 | 版本控制 |
| TortoiseGit | 可选 | 图形化 Git 辅助工具 |
| Typora | + 破解补丁 | Markdown 编辑器 |
| STLink 驱动/升级工具 | STLinkUpgrade | 板卡调试下载 |
| 串口调试助手 | 可选（正点原子等） | 串口终端 |

### 1.2 SDK Manager 配置

1. 打开 RT-Thread Studio → **SDK Manager**
2. 厂商过滤选择 **STM32**
3. 下载 **RT-Thread Source Code 4.0.3**（或 4.1.1）
4. 下载 **STM32F4 Chip Support Package**

![SDK Manager 配置](./figures/sdk_manager.png)

### 1.3 硬件连接

- 星火1号 Type-C 接 **DEBUG 口** → 按蓝色电源键
- 设备管理器确认出现 **STLink Debug**（通用串行总线设备）
- 若 STLink 版本过旧 → 用 `STLinkUpgrade` 升级固件
- 串口波特率：**115200**

![设备管理器中的 STLink](./figures/stlink_device.png)

---

## 二、工程创建与验证

### 2.1 QEMU 仿真工程（第一个任务）

1. `文件 → 新建 → RT-Thread 项目 → 基于开发板 → QEMU VExpress-A9`
2. 编译 → 调试/下载 → 终端输出 `hello rt-thread`

```c
/* 终端输出 */
hello rt-thread
```

### 2.2 星火1号实体板工程（第三个任务）

| 配置项 | 值 |
|--------|-----|
| 基于 | 芯片 |
| 厂商 | STM32 |
| 系列 | F4 |
| 芯片 | STM32F407VG |
| 内核版本 | 4.0.3（统一） |
| 控制台串口 | 默认 UART |
| 调试器 | STLink |

在 `main.c` 第一行添加：

```c
rt_kprintf("Hello SOC 2026!\n");
```

验证步骤：编译 → 下载 → 串口终端（115200）→ 看到 RT-Thread Logo + `Hello SOC 2026!`。

- 输入 `Tab` 查看 FinSH 命令列表
- 输入 `ps` 查看线程

![串口输出 RT-Thread Logo](./figures/uart_logo.png)

---

## 三、工程目录结构

| 目录 | 内容 |
|------|------|
| `applications/main.c` | main 线程入口 |
| `drivers/` | 底层驱动（适配 RT-Thread 驱动框架） |
| `rt-thread/components/` | 组件（FinSH、文件系统、网络、libc） |
| `rt-thread/components/drivers/` | 驱动框架三层：IO管理层 → 设备驱动框架 → 设备驱动层 |
| `rt-thread/libcpu/` | CPU 架构相关（上下文切换汇编） |
| `rt-thread/src/` | 内核源码（线程/调度/IPC/内存/时钟） |

---

## 四、启动流程

```
上电 → 启动汇编 reset_handler → system_init → entry/$Sub$$main
→ rtthread_startup() → 关全局中断 → rt_hw_board_init()
→ rt_show_version()（打印Logo）→ 调度器初始化 → 创建main线程 → 启动调度 → main()
```

### 关键函数说明

- **`reset_handler`**：上电后第一条执行的汇编，初始化栈指针
- **`rt_hw_board_init()`**：板级初始化（时钟、外设）
- **`rt_show_version()`**：打印 RT-Thread 版本 Logo
- **`rt_thread_create`**：创建 `main` 线程
- **`rt_system_scheduler_start()`**：启动调度器

---

## 五、Markdown 语法

### 5.1 常用语法速查

| 语法 | 写法 | 效果 |
|------|------|------|
| 标题 | `# 一级标题` | 大标题 |
| 加粗 | `**粗体**` | **粗体** |
| 行内代码 | `` `code` `` | `code` |
| 代码块 | ` ``` ` 包裹 | 多行代码 |
| 表格 | `|` 分隔 | 如上表 |
| 图片 | `![描述](路径)` | 见下图 |
| 链接 | `[文字](url)` | [链接](https://www.rt-thread.org/) |
| 列表 | `-` 或 `1.` | 无序/有序 |

### 5.2 Typora 图像设置

在 Typora 中：`偏好设置 → 图像 → 复制到指定路径 ./figures`，使所有图片统一保存到 `./figures/` 目录。

![Typora 图像设置](./figures/typora_image_setting.png)

---

## 六、Git/Gitee 操作

### 6.1 账户与密钥配置

```bash
git config --global user.name "你的名字（英文）"
git config --global user.email "你的Gitee注册邮箱"

ssh-keygen -t rsa -C "你的邮箱"        # 一路回车
cat ~/.ssh/id_rsa.pub                   # 复制公钥
```

> 将公钥粘贴到 Gitee：右上角头像 → 设置 → SSH 公钥 → 粘贴 → 确定

```bash
ssh -T git@gitee.com                    # 看到 successfully 即成功
```

> ⚠️ **注意**：笔记截图时给公钥打码，**不要上传私钥**。

### 6.2 常用命令

| 命令 | 作用 |
|------|------|
| `git init` | 初始化仓库 |
| `git clone <url>` | 克隆远程仓库 |
| `git pull origin master` | 拉取最新代码 |
| `git add <file>` / `git add .` | 添加到暂存区 |
| `git commit -m "message"` | 提交到本地仓库 |
| `git push origin <branch>` | 推送到远程分支 |
| `git checkout -b <branch>` | 新建并切换分支 |
| `git switch <branch>` | 切换分支 |
| `git remote set-url origin <url>` | 修改远程地址 |
| `git remote -v` | 查看远程信息 |

---

## 七、PR 提交流程

### 7.1 完整流程

1. 在 Gitee 网页 **Fork** 上游夏令营仓库
2. `git clone` 自己的 Fork 到本地
3. `git checkout -b day1`（新建分支，**不要用 master 直接提交**）
4. 在对应组别目录下新建个人目录，放入笔记和作业
5. `git add .` → `git commit -m "docs: init day1 template"` → `git push origin day1`
6. Gitee 网页 → 新建 Pull Request → 源分支 `day1` → 目标分支 `master`（上游仓库）
7. 审查人员选择：组员选小组长，小组长选导师
8. 审查通过后合并（建议勾选"合并后删除提交分支"）
9. 合并后切回 master → `git pull` 同步最新

### 7.2 目录结构规范

```
夏令营仓库/
├── group1/                    # 第一组
│   ├── 示例张三/              # 示例（参考用）
│   │   ├── notes/
│   │   └── homework/
│   ├── 你的名字/              # 自己建，改成自己的名字
│   │   ├── notes/day1_learning_notes.md
│   │   └── homework/
│   └── ...
├── group2/
├── group3/
└── group4/
```

### 7.3 冲突解决

冲突原因：Fork 时主仓库已更新，分支基于旧版本。

```bash
# Fork 页面点「同步」
git switch master
git pull
git switch day1
git merge master      # 手动解决冲突
git push origin day1  # 重新 push
```

---

## 八、常见问题与避坑

| 坑点 | 解决方案 |
|------|----------|
| Studio 找不到 SDK Manager | 重新安装或下载 4.1.1 版本源码 |
| STLink 无法识别 | 升级固件（STLinkUpgrade）或重装驱动 |
| 串口终端无输出 | 确认端口号 + 波特率 115200 + 板子已通电 |
| 公钥泄露风险 | 笔记截图给公钥打码，不要上传私钥 |
| PR 冲突 | 先同步上游 → pull 最新 → merge → 解决冲突 → 重新 push |
| 分支名重复 | 每次用不同名（day1、day1-v2、day2...） |
| 直接提交到 master | 必须新建分支再提 PR，master 受保护 |

