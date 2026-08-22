# Git 基础命令笔记

## 一、工作区 / 暂存区 / 远端 的关系

Git 把代码分成三层：

| 区域                            | 说明                                                         |
| ------------------------------- | ------------------------------------------------------------ |
| **工作区**（Working Directory） | 你当前在磁盘上看到、正在编辑的文件                           |
| **暂存区**（Stage / Index）     | 本地的 git 管理库，存放"准备提交"的快照；加入暂存区后这些文件可以得到一个保护 |
| **远端仓库**（Remote）          | 服务器上的仓库（如 GitHub / Gitee）                          |

数据流向：

```
工作区  ──add──▶  暂存区  ──commit──▶  本地仓库  ──push──▶  远端
工作区  ◀─pull──  远端（pull = fetch + merge）
```

记忆口诀：

- `git pull`：**远端 → 本地**（把服务器最新内容拉到本地）
- `git push`：**本地 → 远端**（把本地提交同步到服务器）

---

## 二、核心命令清单

### 1. `git init`

**初始化当前目录为 git 仓库。**

```bash
git init
```

执行后当前目录会出现一个 `.git` 隐藏文件夹，git 开始对这个目录进行版本管理。

---

### 2. `git pull origin master`

**拉取远端 master 分支的最新内容到本地。**

```bash
git pull origin master
```

- `origin`：远端仓库的默认别名
- `master`：要拉取的分支名
- 通常在开始干活前先 `pull` 一次，避免和远端产生冲突

---

### 3. `git add <file/path>`

**把指定文件 / 文件夹加入 git 暂存区。**

```bash
git add readme.md          # 添加单个文件
git add src/               # 添加整个文件夹
git add .                  # 添加当前目录下所有改动
```

> **补充：git 暂存区代表你本地的 git 管理库，将指定的文件/文件夹加入暂存库后，这些文件可以得到一个保护。**

---

### 4. `git commit -m "message"`

**把暂存区的内容提交到本地仓库，本次提交日志为 `message`。**

```bash
git commit -m "feat: 新增串口驱动"
```

- `-m` 后面跟提交说明
- 一条好的 commit message 应说明"这次改了什么、为什么改"

> ⚠️ 课程中写的是 `git commit "message"`，实际用法需要加 `-m` 参数：`git commit -m "message"`。

---

### 5. `git push origin master`

**把本地 master 分支的最新内容推送到远端。**

```bash
git push origin master
```

提交完 `commit` 之后，还需要 `push` 才能让远端（GitHub/Gitee）看到你的改动。

---

## 三、典型工作流程（本地 ↔ 远端同步）

```bash
# 第一次：初始化 + 连接远端
git init
git remote add origin <仓库地址>

# 日常开发循环
git pull origin master          # 1. 先拉取远端最新代码
# …… 编辑文件 ……
git add .                       # 2. 加入暂存区
git commit -m "本次改动说明"     # 3. 提交到本地仓库
git push origin master          # 4. 推送到远端
```

---

## 四、远端地址与分支操作

### 6. `git remote set-url origin <url>`

**设置 / 修改当前 git 仓库的远端地址。**

```bash
git remote set-url origin git@gitee.com:kurisaW/git_-proj.git
```

- `origin` 是远端别名，`<url>` 换成自己的仓库地址
- 常见场景：仓库地址改了、从 HTTPS 换成 SSH、或初始化后第一次绑定远端
- 括号里的"根据自己的修改"= URL 要换成你自己的 gitee/github 地址

> 💡 第一次绑定远端用的是 `git remote add origin <url>`；后续要改地址才用 `set-url`。

---

### 7. `git fetch origin master`

**把远端 master 分支的内容同步到本地（只更新远端跟踪分支，不动工作区）。**

```bash
git fetch origin master
```

- `fetch` 和 `pull` 的区别：
  - `git fetch`：**只下载**远端最新内容到本地的 `origin/master` 引用，**不合并**到当前分支，工作区不变
  - `git pull` = `git fetch` + `git merge`，会直接把改动合并进当前分支
- 想先看看远端有什么变化、再决定要不要合并时，用 `fetch` 更安全

---

### 8. `git remote -v`

**查看当前仓库的远端地址（远端分支情况）。**

```bash
git remote -v
```

- `-v` 即 `--verbose`，会列出抓取（fetch）和推送（push）两行地址

- 输出示例：

  ```
  origin  git@gitee.com:kurisaW/git_-proj.git (fetch)
  origin  git@gitee.com:kurisaW/git_-proj.git (push)
  ```

- 用来确认远端地址有没有配对、是不是指向正确的仓库

---

### 9. `git checkout -b <分支名>`

**基于当前分支，切出一个新分支并立即切换过去。**

```bash
git checkout -b test       # 基于当前分支创建 test 分支，并切到 test
```

- 等价于 `git branch test` + `git checkout test` 两步合一
- 新分支会继承当前分支的全部提交历史
- 新分支上做的提交不会影响 master，适合在新分支上开发新功能 / 修 bug，稳定后再合并回 master

> 📌 新版 Git 也推荐用 `git switch -c test` 来创建并切换分支，语义更清晰。

---

## 五、补充：常见疑问

**Q：绑定 / 修改远端地址前，要先删仓库吗？**
A：不需要。`git remote set-url origin <新地址>` 会直接覆盖原有地址，原仓库历史和本地文件都不受影响。只有当本地 `origin` 别名本身不存在时，才需要用 `git remote add origin <url>` 新增。

---

## 六、冲突是怎么产生的（为什么会出现冲突）

**核心原因：两个分支各自修改了同一个文件的同一行（或相邻几行），git 无法自动判断该保留哪一侧，于是报 CONFLICT 让你人工决定。**

git 自动合并的原理是"三方合并"：找到两个分支的公共祖先，对比两边的改动。只要两边改的不是同一处，git 都能自动合上；一旦撞车，就产生冲突。

### fork 场景的产生过程

```text
时间线：

  A ──── B ──── C            upstream/master（上游仓库，别人在 B、C 里改了 readme.md 第 10 行）
    \
     └──── D                 你的 fork 分支（你在 D 里也改了 readme.md 第 10 行）
            \
             ↓ 执行 git merge upstream/master
         CONFLICT！
         同一行被两条时间线分别修改，git 无法自动合并，需要人工选择
```

### 常见的三种冲突场景

| 场景                 | 说明                                                         |
| -------------------- | ------------------------------------------------------------ |
| **fork 同步上游**    | 你的 fork 和上游仓库都改了同一处，同步上游（merge/rebase）时冲突 |
| **多人协作同一分支** | 你和同事改了同一文件的同一行，后 push / pull 的人遇到冲突    |
| **分支合并**         | feature 分支和 master 分支各自改了同一行，合并（merge/rebase）时冲突 |

---

## 七、解决冲突的完整流程（fork 场景）

### 第 1 步：fork repo，同步上游

fork 出来的仓库**不会自动跟随上游**，需要手动同步：先绑定上游地址，再拉取上游最新内容。

```bash
git remote add upstream <上游仓库地址>   # 只需绑定一次
git remote -v                            # 确认：origin = 自己的 fork，upstream = 上游
git fetch upstream master                # 把上游最新内容下载到本地（此时还不会冲突）
```

> 💡 Gitee / GitHub 网页端也有 "Sync fork / 同步上游" 按钮，命令行做法就是上面这几步。

### 第 2 步：切到产生冲突的分支，merge / rebase 同步上游

```bash
git checkout <产生冲突的分支>      # 切到自己要更新的分支
git merge upstream/master          # 把上游最新修改合并进当前分支
# 或者：git rebase upstream/master  # 变基方式，提交历史更线性
```

如果两边改了同一处，终端会提示 `CONFLICT (content): Merge conflict in <文件>`，**此时 git 停下来让你解决冲突**。

### 第 3 步：手动解决冲突（选择旧时间线还是新时间线的修改）

打开冲突文件，会看到这样的标记：

```text
<<<<<<< HEAD
这一侧是你当前分支的修改（你自己这条时间线）
=======
这一侧是 upstream/master 的修改（上游那条时间线）
>>>>>>> upstream/master
```

**自行决定保留哪条时间线的修改**：

- 想保留自己的旧改动 → 删掉 `=======` 下侧的内容和三个标记行
- 想采用上游的新改动 → 删掉 `<<<<<<< HEAD` 与 `=======` 之间的内容和标记行
- 两边都要 → 手动把两侧内容拼成想要的最终样子，删掉所有标记行

### 第 4 步：标记已解决并提交、推送

```bash
git add <冲突文件>          # 告诉 git 冲突已解决
git commit -m "解决冲突"    # merge 场景也可直接用默认的合并提交信息
git push origin <分支>      # 推回自己的 fork 远端
```

### 辅助命令速记

| 命令                | 作用                           |
| ------------------- | ------------------------------ |
| `git status`        | 查看哪些文件处于冲突状态       |
| `git merge --abort` | 放弃本次合并，回到合并前的状态 |
| `git mergetool`     | 调用可视化工具解决冲突         |

> 📌 `git merge` 会生成一个"合并提交"，冲突一次性解决；`git rebase` 会把你的提交逐个"搬"到上游最新之上，冲突可能要在每个提交处分别解决。课堂场景建议先用 `merge`，更直观。

---

## 八、推送第一次 PR 后：下一次推送与 PR 创建的完整流程

> 场景：已提交过第一次 PR，要在 fork 仓库继续提交下一次内容（day2 分支名自行而定）

```text
① 浏览器打开 fork 仓库，点"同步上游"（Sync fork），让 fork 的 master 跟上原仓库最新代码
        ↓
② 本地打开 git bash，依次执行：
   git switch master        # 切回到主分支
   git pull origin master   # 从远端拉主分支到本地
                            # （前提：①已完成，即 origin 的 master 已是最新上游代码）
        ↓
③ git checkout -b day2      # 基于最新 master 切出新分支 day2
④ 修改 / 新增 / 删除对应文档或内容
⑤ git add .                 # 将当前目录下所有文件修改加入本地 git 管理库
⑥ git commit -m "第二次提交信息"
⑦ git push origin day2      # 将最新修改同步到远端 day2 分支
        ↓
⑧ 到 day2 分支下创建 PR
```

### 关键点说明

- `git switch master` 是切分支的新写法，等价于 `git checkout master`
- 第 ② 步 `git pull origin master` **必须在第 ① 步网页端"同步上游"完成后执行**，这样才能保证本地 master 和上游最新代码一致
- 第 ③ 步基于"最新 master"切出新分支，目的是**避免旧代码和冲突带进新 PR**，让这次 PR 只包含本次改动
- 每次新内容都走"**新分支 + 新 PR**"（day2、day3…），而不是继续往旧分支提交
- 课堂提示（李诗）：**审查人员是固定的，创建 PR 时没法选择小组长作为审查人**

---

## 九、速查表

| 命令                              | 作用                           | 方向              |
| --------------------------------- | ------------------------------ | ----------------- |
| `git init`                        | 初始化仓库                     | —                 |
| `git pull origin master`          | 拉取远端 master（fetch+merge） | 远端 → 本地       |
| `git add <path>`                  | 加入暂存区                     | 工作区 → 暂存区   |
| `git commit -m "msg"`             | 提交到本地仓库                 | 暂存区 → 本地仓库 |
| `git push origin master`          | 推送到远端                     | 本地 → 远端       |
| `git remote set-url origin <url>` | 修改远端地址                   | —                 |
| `git fetch origin master`         | 同步远端到本地引用（不合并）   | 远端 → 本地引用   |
| `git remote -v`                   | 查看远端地址                   | —                 |
| `git checkout -b <分支名>`        | 新建并切换分支                 | —                 |
| `git switch <分支名>`             | 切换分支（新版语法）           | —                 |
| `git remote add upstream <url>`   | 绑定上游仓库地址（fork 后）    | —                 |
| `git merge upstream/master`       | 合并上游修改，可能触发冲突     | 上游 → 当前分支   |
| `git rebase upstream/master`      | 变基同步上游，历史线性         | 上游 → 当前分支   |
| `git status`                      | 查看冲突文件                   | —                 |
| `git merge --abort`               | 放弃本次合并                   | —                 |

|      |      |      |
| ---- | ---- | ---- |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |
|      |      |      |