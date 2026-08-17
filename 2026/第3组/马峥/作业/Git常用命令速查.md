# Git 常用命令速查

## 黄金三步（日常提交推送）

```bash
git add .            # 1. 暂存所有改动（可改成 git add 具体文件名）
git commit -m "说明"  # 2. 提交（说明写清楚这次改了什么）
git push origin <分支名>   # 3. 推送到云端（origin=远程仓库）
```

> 记住：git 的所有命令都要以 `git` 开头。
> `git add` / `git commit` / `git pull` / `git push` / `git checkout` 都是。
> 漏了或拼错会报 `command not found` 或 `is not a git command`。

## 主要分支操作

| 命令 | 作用 |
|---|---|
| `git branch` | 列出所有分支，当前分支前有 `*` |
| `git branch -a` | 查看本地+云端所有分支 |
| `git checkout <分支>` | 切换到某分支 |
| `git checkout -b <新分支>` | 新建分支并切换过去 |
| `git push origin <分支>` | 把当前提交推到云端某分支 |
| `git push -u origin <分支>` | 同上，并记住对应关系，以后直接 `git push` |
| `git pull` | 拉取云端最新（先拉再推，避免冲突） |

## 查看状态

| 命令 | 作用 |
|---|---|
| `git status` | 看当前改动/暂存情况 |
| `git log --oneline` | 看提交历史（一行一条） |
| `git diff` | 看未暂存的改动内容 |
| `git remote -v` | 查看远程仓库地址 |

## 撤销/后悔药

| 命令 | 作用 |
|---|---|
| `git restore --staged <文件>` | 把已暂存的文件撤出（取消 add） |
| `git restore <文件>` | 丢弃某文件未提交的改动（小心！会丢内容） |
| `git reset --soft HEAD~1` | 撤销最近一次 commit，保留改动 |

## 常见坑（都是实际遇到过的）

1. **空文件夹推不上去**：git 不跟踪空目录。放进占位文件 `.gitkeep` 或先放真实文件。
2. **`git push` 显示 `Everything up-to-date`**：说明没 commit 成功，`git commit` 前一定要 `git add`。
3. **Windows 换行符警告（LF/CRLF）**：在仓库根目录放 `.gitattributes`（内容 `* text=auto`）即可消除，仓库永远存 LF。
4. **私有仓库 push 报 401/认证失败**：Gitee 私人令牌过期了，重新去 Gitee「设置→私人令牌」生成。
