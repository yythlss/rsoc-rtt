# Git 常用命令速查表

> 桌面速查参考，按使用场景分组。尖括号 `< >` 中的内容请按需替换，例如 `<url>` = 远程仓库地址，`<branch>` = 分支名，`<file>` = 文件路径。

---

## 一、仓库初始化与克隆

| 命令 | 说明 |
| :-- | :-- |
| `git init` | 在当前目录初始化一个新的 Git 仓库 |
| `git clone <url>` | 克隆远程仓库到本地 |
| `git clone <url> <dir>` | 克隆到指定目录 `<dir>` |
| `git config --global user.name "<name>"` | 设置全局提交用户名 |
| `git config --global user.email "<email>"` | 设置全局提交邮箱 |
| `git config --list` | 查看所有 Git 配置 |

---

## 二、文件暂存与提交

| 命令 | 说明 |
| :-- | :-- |
| `git status` | 查看工作区与暂存区状态 |
| `git add <file>` | 将指定文件加入暂存区 |
| `git add .` | 将所有修改/新增文件加入暂存区（不含删除） |
| `git add -A` / `git add --all` | 将所有改动（含删除）加入暂存区 |
| `git commit -m "<msg>"` | 提交暂存区，并附提交说明 |
| `git commit -am "<msg>"` | 跳过 `git add`，直接提交已跟踪文件的修改 |
| `git commit --amend` | 修改最后一次提交（补充内容或改写说明） |
| `git stash` | 临时保存当前工作区修改 |
| `git stash pop` | 恢复最近一次 stash 并删除该记录 |
| `git stash list` | 查看所有 stash 记录 |

---

## 三、分支管理

| 命令 | 说明 |
| :-- | :-- |
| `git branch` | 列出所有本地分支（当前分支前带 `*`） |
| `git branch <name>` | 基于当前分支创建新分支 |
| `git branch -d <name>` | 删除已合并的分支 |
| `git branch -D <name>` | 强制删除分支（未合并也可用） |
| `git checkout <branch>` | 切换到指定分支 |
| `git checkout -b <branch>` | 创建并切换到新分支 |
| `git switch <branch>` | 切换到指定分支（Git 2.23+ 推荐） |
| `git switch -c <branch>` | 创建并切换到新分支（Git 2.23+ 推荐） |
| `git merge <branch>` | 将 `<branch>` 合并进当前分支 |
| `git rebase <branch>` | 将当前分支的提交变基到 `<branch>` 之上 |

---

## 四、合并与变基

| 命令 | 说明 |
| :-- | :-- |
| `git merge <branch>` | 生成一次合并提交（保留完整历史） |
| `git merge --no-ff <branch>` | 禁用快进合并，强制保留合并提交 |
| `git rebase <branch>` | 将当前分支提交"重放"到目标分支末尾，历史更线性 |
| `git rebase -i <commit>` | 交互式变基，可合并/改写提交 |
| `git rebase --abort` | 中止正在进行的变基 |
| `git rebase --continue` | 解决冲突后继续变基 |
| `git merge --abort` | 中止正在进行的合并 |

> 冲突发生后：手动编辑冲突文件，保留需要的内容，再 `git add <file>` 标记已解决，最后 `git commit`（merge）或 `git rebase --continue`（rebase）。

---

## 五、远程仓库操作

| 命令 | 说明 |
| :-- | :-- |
| `git remote -v` | 查看远程仓库的 fetch / push 地址 |
| `git remote add origin <url>` | 关联本地仓库到远程 `origin` |
| `git remote set-url origin <url>` | 修改远程仓库地址 |
| `git remote rename <old> <new>` | 重命名远程仓库 |
| `git remote rm <name>` | 移除远程仓库 |
| `git fetch` | 拉取所有远端更新到本地（不合并） |
| `git fetch <remote> <branch>` | 同步指定远端分支到本地 |
| `git pull` | 拉取当前分支对应的远端并合并（`fetch` + `merge`） |
| `git pull --rebase` | 以变基方式拉取（fetch + rebase） |
| `git push` | 推送当前分支到对应远端 |
| `git push -u origin <branch>` | 推送并关联远端分支（首次推送推荐） |
| `git push --force` | 强制推送（慎用，会覆盖远端历史） |
| `git push --tags` | 推送本地标签到远端 |

---

## 六、查看状态与日志

| 命令 | 说明 |
| :-- | :-- |
| `git status` | 查看工作区与暂存区状态 |
| `git log` | 查看提交历史（按时间倒序） |
| `git log --oneline` | 每个提交显示一行（简洁） |
| `git log -n <num>` | 只显示最近 `<num>` 条提交 |
| `git log --graph --all` | 图形化展示分支与合并历史 |
| `git log --author="<name>"` | 按作者筛选提交 |
| `git show <commit>` | 查看某次提交的详细改动 |
| `git diff` | 查看工作区与暂存区的差异 |
| `git diff --cached` | 查看暂存区与最新提交的差异 |
| `git diff <branch1> <branch2>` | 比较两个分支的差异 |
| `git blame <file>` | 查看文件每行最后修改的提交与作者 |

---

## 七、撤销与回退

| 命令 | 说明 |
| :-- | :-- |
| `git restore <file>` | 丢弃工作区对 `<file>` 的修改（Git 2.23+） |
| `git restore --staged <file>` | 将 `<file>` 从暂存区移出（取消 `add`） |
| `git checkout -- <file>` | 丢弃工作区对 `<file>` 的修改（旧写法） |
| `git reset <file>` | 取消暂存（保留工作区修改） |
| `git reset --soft <commit>` | 回退到指定提交，保留暂存区与工作区 |
| `git reset --mixed <commit>` | 回退到指定提交，保留工作区（默认） |
| `git reset --hard <commit>` | 彻底回退到指定提交（丢弃所有后续改动，**慎用**） |
| `git revert <commit>` | 生成一次新提交来撤销某次提交（不改写历史，推荐） |
| `git clean -fd` | 删除未跟踪的文件与目录（**慎用**） |

> **reset vs revert**：`reset` 改写历史，适合本地未推送的提交；`revert` 保留历史、新增撤销提交，适合已推送的提交。

---

## 附录：日常典型工作流

```bash
# 1. 克隆并切换到开发分支
git clone <url>
git switch -c feature/my-work

# 2. 开发完成后暂存并提交
git add .
git commit -m "完成某某功能"

# 3. 同步远端最新代码（推荐变基方式）
git fetch origin
git rebase origin/master

# 4. 推送到远端并发起 PR
git push -u origin feature/my-work
```
