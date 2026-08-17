# 2026 RT-Thread 大学生夏令营 Day1 学习笔记

---

## 一、工程文件树要点

| 目录/模块 | 作用 |
| :-- | :-- |
| `application/main.c` | 程序主入口（如 hello 打印） |
| `drivers/` | 底层驱动适配（LCD/串口/音频等） |
| `components/` | 核心组件：C++、文件系统 DFS、finish 终端等 |
| `libc` | C 库兼容层（armcc/gcc/clang） |
| `src/` | 内核：时钟、设备管理、idle 线程、IPC（进程间通信） |
| `drivers` 框架 | 三层：IO 设备管理 → 驱动框架（串口/SPI/I2C/GPIO）→ 厂商驱动实现 |

- **finish 组件**：`component/finish`，可查看系统资源（内存、线程状态）；`ps` 查看线程，`tab` 补全命令；`idle` 线程是 RTOS 最小运行实体。

---

## 二、Markdown 语法（笔记/作业统一格式）

- 标题：`#` 一级、`##` 二级、`###` 三级……（井号数量 = 层级）。
- 代码块：三个反引号 ``` 包裹，起始处标注语言（如 ```c）实现高亮。
- 行内代码/高亮：一对反引号 `code`。
- 加粗：`**文字**`；斜体：`*文字*`。
- 表格：用 `|` 分隔列，第二行用 `|---|` 分隔表头与内容。
- 插入图片（语法示例，非本文实际引用）：`![描述](图片路径)`；建议把剪贴板图片保存到工程下 `figures/` 等相对路径目录，避免迁移丢图。
- 学习要求：每日用 Markdown 整理笔记 → 提交到个人 Git 仓库 + 发布到社区论坛。

---

## 三、Git 基础操作与 PR 贡献流程

### 3.1 全局设置

第一次使用 Git 时，先配置用户名和邮箱，这些信息会记录到每一次提交中：

```bash
git config --global user.name "<your-name>"
git config --global user.email "<your-email>"
```

### 3.2 基础工作流（本地 → 远端）

从零开始创建并推送一个仓库的最小流程：

```bash
mkdir my_project
cd my_project
git init                    # 初始化当前目录为 Git 仓库
touch README.md
git add README.md           # 加入暂存区（受 git 保护）
git commit -m "first commit"  # 提交，附说明
git remote add origin <git-remote-url>  # 关联远端仓库
git push -u origin master   # 本地推送到远端
```

> **说明**
>
> - `git add <file/path>`：将指定文件或目录加入暂存区（stage/index），加入后文件即进入 Git 管理、受版本控制保护。
> - `git commit -m "<message>"`：把暂存区的改动生成一次提交，并填写提交日志。
> - `git push`：本地 → 远端（推送）；`git pull`：远端 → 本地（拉取）。

提交成功后的终端输出示例：

```text
[master 8de35a] first commit
 1 file changed, 3 insertions(+)
 create mode 100644 README.md
```

如果当前工作区没有待提交内容，执行 `git commit` 会提示：

```text
On branch master
Your branch is up to date with 'origin/master'.

nothing to commit, working tree clean
```

需要临时保存当前修改（稍后恢复）时可用：

```bash
git stash
```

输出示例：

```text
Saved working directory and index state WIP on master: <commit-id> Initial commit
```

### 3.3 远程仓库管理

| 命令 | 作用 |
| :-- | :-- |
| `git remote add origin <url>` | 关联本地仓库到远端 |
| `git remote set-url origin <url>` | 修改当前仓库的远端地址（如仓库迁移） |
| `git remote -v` | 查看远端仓库的 fetch / push 地址 |
| `git fetch origin master` | 同步远端分支到本地（不自动合并） |

修改远端地址示例：

```bash
git remote set-url origin <新的仓库地址>
```

> 注意：`<新的仓库地址>` 请替换为你自己的实际仓库地址，例如 `git@example.com:<your-name>/<repo-name>.git`。

查看远端地址：

```bash
git remote -v
```

输出示例：

```text
origin  <git-remote-url> (fetch)
origin  <git-remote-url> (push)
```

同步远端分支：

```bash
git fetch origin master
```

输出示例：

```text
From <git-remote-host>
 * branch            master     -> FETCH_HEAD
```

### 3.4 分支与协作

| 命令 | 作用 |
| :-- | :-- |
| `git checkout -b <branch>` | 基于当前分支新建并切换到新分支 |
| `git switch <branch>` | 切换到指定分支（Git 2.23+ 推荐） |
| `git pull origin master` | 拉取远程 `master` 分支到本地 |

```bash
git checkout -b test        # 基于当前分支新建并切换到 test 分支
git switch master           # 切换到 master 分支
git fetch origin master     # 同步远端分支到本地
git remote -v               # 查看远端仓库
git remote set-url origin <url>  # 修改远端仓库地址
```

`git checkout -b test` 执行成功后会提示：

```text
Switched to a new branch 'test'
```

### 3.5 提交新分支并创建 PR

在 `test` 分支完成修改后，按以下步骤提交并推送到远端：

```bash
git add README.md
git add ./
git commit -m "second commit"
git push origin test
```

`git push origin test` 的输出示例：

```text
Enumerating objects: 5, done.
Counting objects: 100% (5/5), done.
Delta compression using up to 16 threads
Compressing objects: 100% (3/3), done.
Writing objects: 100% (3/3), 324 bytes | 324.00 KiB/s, done.
Total 3 (delta 0), reused 0 (delta 0), pack-reused 0 (from 0)
remote: Powered by GITEE.COM
remote: Create a pull request for 'test' on Gitee by visiting:
remote:   https://gitee.com/<your-name>/<repo-name>/pull/new/<your-name>:test...<your-name>:master
To <git-remote-url>
 * [new branch]      test -> test
```

推送完成后，在 Gitee 仓库页面创建 Pull Request：

1. **切换到目标分支**：在仓库页面顶部的分支下拉框中选择 `test`。
2. **点击“贡献代码”**：页面会提示“当前分支比 master 领先 1 次提交”。
3. **点击“创建 Pull Request”**：填写标题和描述后提交，等待仓库维护者审查合并。

页面中可看到该分支的提交记录，示例如下：

| 提交人 | 提交信息 | Commit ID | 时间 |
| :-- | :-- | :-- | :-- |
| 你（或贡献者） | second commit | `71eebe9` | 2 分钟前 |

README 预览中可能显示：

```text
这是我的 git 测试仓库
这是我的第二次修改
```

### 3.6 PR（Pull Request）规范流程

1. **Fork** 统一仓库到个人账号，clone 到本地。
2. 在**个人仓库**新建分支，完成当天笔记/作业（建 `笔记/` 与 `作业/` 文件夹，内容 Markdown 格式）。
3. 提交并推送到个人仓库对应分支，发起 **PR** 到统一仓库。
4. **小组长/导师审核** → 审核通过后合并分支。
5. **冲突解决**：在冲突分支执行 `git merge` / `git rebase` 同步最新远端，手动选择保留哪一侧修改后继续提交。

### 3.7 如何解决 Git 冲突

在多人协作中，冲突通常发生在以下场景：

**冲突产生的原因（时间线示意）**

- **t1 时刻**：`master`、`fork1`、`fork2` 都基于同一份代码，状态为 t1。
- **t2 时刻**：
  - `fork1` 修改后合并到 `master`，`master` 由 t1 更新为 t2。
  - `fork2` 仍保留 t1 时刻的内容。

此时 `fork2` 向 `master` 创建 Pull Request，会发现目标分支 `master` 已经更新到 t2，而 `fork2` 自己的提交仍然基于旧的 t1。由于两者的修改基础不同，就会产生冲突。

**解决冲突的步骤**

1. **Fork 仓库后同步上游**：定期将上游（upstream）仓库的最新改动同步到自己的 Fork。
2. 切换到产生冲突的分支，执行 `git merge` 或 `git rebase`，把远端的最新修改同步到当前分支：

   ```bash
   git switch your-branch
   git merge master        # 或 git rebase master
   ```

3. 根据提示手动解决冲突文件，选择保留旧时间线或新时间线的修改。
4. 解决后继续提交并推送：

   ```bash
   git add .
   git commit -m "resolve conflict"
   git push origin your-branch
   ```
