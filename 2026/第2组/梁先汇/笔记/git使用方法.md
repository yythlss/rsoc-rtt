# Git 使用方法

## 〇、初次使用前的配置（用户名 / 邮箱 / SSH 密钥）

### 1. 配置提交用户名和邮箱

git 每次提交都会记录作者信息，先配置用户名和邮箱：

```bash
git config --global user.name "你的用户名"
git config --global user.email "你的邮箱"
```

查看是否配置成功（`--global` 只看全局配置）：

```bash
git config --list
```

本机执行效果（节选，只需关注最后的 `user.name` 和 `user.email` 两行即可，其余为系统默认配置）：

```text
$ git config --list
diff.astextplain.textconv=astextplain
filter.lfs.clean=git-lfs clean -- %f
filter.lfs.smudge=git-lfs smudge -- %f
filter.lfs.process=git-lfs filter-process
filter.lfs.required=true
http.sslbackend=schannel
core.autocrlf=true
core.fscache=true
core.symlinks=false
pull.rebase=false
credential.helper=manager
credential.https://dev.azure.com.usehttppath=true
init.defaultbranch=master
user.email=15512667+liquoricehy@user.noreply.gitee.com   <-- 已生效
user.name=LiquoriceHy                                     <-- 已生效
...
```

### 2. 配置 SSH 密钥

使用 SSH 方式推送代码到 gitee，无需每次输入账号密码。

**（1）生成密钥**（一路回车即可，`-C` 后写自己的邮箱作备注）：

```bash
ssh-keygen -t ed25519 -C "你的邮箱"
```

**（2）查看公钥**：密钥默认生成在 `~/.ssh/` 目录下，私钥为 `id_ed25519`（不要外泄），公钥为 `id_ed25519.pub`：

```bash
cat ~/.ssh/id_ed25519.pub
```

**（3）添加到 gitee**：复制公钥全部内容，打开 gitee -> 设置 -> SSH 公钥 -> 粘贴保存

**（4）测试连接**：

```bash
ssh -T git@gitee.com
```

出现 `Hi xxx! You've successfully authenticated...` 即配置成功。

## 一、常用命令

| # | 命令 | 作用 |
| - | ---- | ---- |
| 1 | `git init` | 初始化当前目录为 git 仓库 |
| 2 | `git pull origin master` | 拉取远端 master 分支 |
| 3 | `git add <file/path>` | 添加 file/path 到 git 暂存区 |
| 4 | `git commit -m "message"` | 提交，本次 git 提交日志为 "message" |
| 5 | `git push origin master` | 将本地 git 分支最新内容同步/推送到远端 |
| 6 | `git remote set-url origin git@gitee.com:xxx/xxx.git` | 设置当前 git 仓库的 URL（按自己的仓库修改） |
| 7 | `git fetch origin master` | 同步远端分支到本地 |
| 8 | `git remote -v` | 查看仓库远端分支情况 |
| 9 | `git checkout -b test` | 基于当前分支切出一个名为 test 的新分支 |

> 注：提交必须带 `-m` 参数指定提交信息，即 `git commit -m "message"`。

## 二、pull 与 push 的方向

| 命令 | 方向 |
| ---- | ---- |
| `git pull` | 远端 -> 本地 |
| `git push` | 本地 -> 远端 |

## 三、暂存区的理解

git 暂存区代表你**本地的 git 管理库**。将指定的文件/文件夹 `git add` 加入暂存库后，这些文件可以得到一个保护（可恢复、可追踪改动）。

文件流转过程：工作区 --`git add`--> 暂存区 --`git commit -m`--> 本地仓库 --`git push origin 分支`--> 远端仓库（gitee）；`git pull` / `git fetch` 则把远端最新内容同步回本地。

## 四、如何解决 git 冲突

1. **fork repo**：浏览器上同步上游（fork 仓库页面的同步按钮）
2. **切到产生冲突的分支**，执行 `git merge` / `git rebase`，将最新远端的修改同步到产生冲突的分支；此时会让你解决冲突，**自行选择旧的时间线还是新的时间线的修改**

## 五、第一次 PR 后，如何进行下一次代码推送和 PR 创建

以 day2 作业为例，完整流程：

1. 浏览器 fork 仓库**同步上游**
2. 本地仓库打开 git bash，依次执行：
   - `git switch master`：切回到主分支
   - `git pull origin master`：从远端拉取主分支到本地（确保第一步完成，即 origin/master 是同步的最新上游仓库）
3. `git checkout -b day2`：基于 master 切出 day2 新分支
4. 修改/新增/删除对应的文档内容
5. `git add .`：将当前目录下的所有文件修改提交到本地 git 管理库
6. `git commit -m "第二次提交信息"`
7. `git push origin day2`：将最新修改同步到远端 day2 分支
8. 到 gitee 的 day2 分支下创建 PR
