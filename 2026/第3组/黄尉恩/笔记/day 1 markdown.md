# Git 基础常用命令
## 1. git init：初始化当前目录为 git 仓库
```bash
git init
```

作用：把当前本地文件夹初始化为 Git 本地仓库。
使用场景：从零开始，把普通文件夹变成 git 仓库。
注意：不要在已经 clone 下来的仓库里再执行git init，会重复初始化，引发异常。

## 2. git pull origin master：拉取远程 master 分支

作用：拉取远程origin仓库的master分支代码，并且自动合并到本地当前分支。
origin：远程仓库的默认别名。
master：远程主分支。
场景：远程仓库更新了，把最新代码下载合并到本地。
风险：本地有修改时，pull 可能产生代码冲突，冲突需要手动解决。

## 3.git add file/path：添加 file/path 到 git 暂存区

```bash
git add 文件名

#添加整个文件夹

git add ./文件夹名

#添加全部改动文件

git add .
```

作用：将文件 / 文件夹加入暂存区 (stage)。
暂存区理解：一个临时缓冲区，告诉 git：这些文件改动，我准备提交。
补充笔记原话：git 暂存区代表你本地的 git 管理库，文件加入暂存区后，改动会被 git 追踪保护，但还没有真正生成本地版本快照。
⚠️add 之后改动还没真正提交，必须配合git commit。

## 4. git commit -m "message"：本次 git 提交日志为 "message"

```bash
git commit -m "feat: day1 黄尉恩笔记作业"
```

作用：把暂存区里面的改动，生成本地版本快照，保存到本地 git 仓库。
-m：后面双引号里面填写本次提交说明 (message)，写清楚本次做了什么修改。
❗ 不要漏写 -m，直接写git commit "xxx"会报错。

## 5.git push origin master：将本地 git 分支最新内容同步 / 推送到远端

```bash
git push origin master
```

作用：把本地master分支的提交记录，推送到远程仓库 origin 的 master 分支。
流程顺序：add → commit → push。
我们不 push 到 master，push 到自己新建的开发分支。

## 6.git remote set-url origin git@gitee.com:kurisaW/git_-proj.git

```bash
git remote set-url origin git@gitee.com:kurisaW/git_-proj.git
```

作用：修改当前本地仓库关联的远程仓库地址。
使用场景：
clone 错仓库，需要更换远程地址；
https 地址切换为 ssh 地址；
地址需要改成你自己仓库的 SSH/HTTPS 链接。

## 7.git fetch origin master：同步远端分支到本地

```bash
git fetch origin master
```

作用：把远程origin/master的更新下载到本地，但是不会自动合并到你的本地分支。
和git pull区别：
fetch：只下载，不合并，安全，看完再手动合并。
pull = fetch + merge，下载并且直接自动合并，容易冲突。

## 8.git remote -v：查看仓库远端分支情况

```bash
git remote -v
```

作用：查看当前本地仓库关联的远程仓库地址，显示 fetch（拉取）和 push（推送）地址。
排查问题：不知道自己本地绑的是哪个远程仓库，执行这条命令核对。

## 9.git checkout -b test：基于当前分支切出一个名为 test 的新分支

```bash
git checkout -b test
```

作用：‑b代表新建分支；基于当前所在分支，创建名为test的新分支，并且立刻切换到这个新分支。

# 完整流程

## 1、clone 你 fork 后的个人仓库到本地

```bash
git clone git@gitee.com:你的ID/RSOC‑RTT.git
cd RSOC‑RTT
```

## 2、基于 master 创建自己的作业分支

```bash
git checkout -b day1黄尉恩
```

## 3、把笔记文件放到目录 2026/第3组/黄尉恩/

## 4、添加文件到暂存区

```bash
git add 2026/第3组/黄尉恩/
```

## 5、本地提交

```bash
git commit -m "feat: day1 黄尉恩笔记与作业"
```

## 6、推送到自己 Gitee 远程的这个分支

```bash
git push origin day1黄尉恩
```

## 7、在网页 Gitee 打开自己仓库，创建 Pull Request 提交给官方仓库。

禁止直接在 master 分支修改文件，所有作业在自己新建分支完成。

1