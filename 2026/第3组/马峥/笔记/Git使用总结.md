# Git 使用总结

## 黄金三步（日常提交推送）

```bash
git add .            # 1. 暂存所有改动（可改成 git add 具体文件名）
git commit -m "说明"  # 2. 提交（说明写清楚这次改了什么）
git push origin <分支名>   # 3. 推送到云端（origin=远程仓库）
```

> 注意：git 所有命令都要以 `git` 开头，漏了或拼错会报错。

## 分支操作

| 命令 | 作用 |
|---|---|
| `git branch` | 查看分支，当前分支前有 `*` |
| `git branch -a` | 查看本地 + 云端所有分支 |
| `git checkout <分支>` | 切换分支 |
| `git checkout -b <新分支>` | 新建并切换分支 |
| `git push origin <分支>` | 推送到云端某分支 |
| `git push -u origin <分支>` | 推送并记住对应关系 |
| `git pull` | 拉取云端最新 |

## 查看状态

| 命令 | 作用 |
|---|---|
| `git status` | 查看改动/暂存情况 |
| `git log --oneline` | 查看提交历史 |
| `git remote -v` | 查看远程地址 |

## 常见坑

1. **空文件夹推不上去**：git 不跟踪空目录，需放占位文件（如 `.gitkeep`）或真实文件。
2. **`git push` 显示 `Everything up-to-date`**：说明没有新提交，先 `git add` 再 `git commit`。
3. **`commit` 不是命令**：要写 `git commit`，前面一定要有 `git`。

## 本次实际踩过的坑（2026-08-17）

1. `git chatout` 拼错 → 应为 `git checkout`。
2. `commit -m` 漏了 `git` 前缀 → 报 `command not found`，完整命令是 `git commit -m "..."`。
3. 马峥的 `作业`/`笔记` 文件夹原为空，git 看不到空目录 → 放入 `README.md` 后 `git add .` 才能提交成功。
4. 另一个仓库 geey 因 TLS / 私有仓库认证问题无法 pull/push → 改用 openssl 后端 + Gitee 私人令牌解决。
