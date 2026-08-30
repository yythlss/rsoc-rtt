# SCons 学习笔记

> 

---

## 一、什么是构建工具？

**一句话理解**：构建工具就是帮你把一堆 `.c` 源文件"打包"成一个可以运行的程序的工具。

你可以把它想象成一个"自动化工厂"：
- 你告诉它：这些源文件需要编译，它们之间有依赖关系
- 它帮你完成：调用编译器、链接器，最终生成可执行文件

常见的构建工具有：
| 工具 | 特点 |
|------|------|
| **GNU Make** | 最流行，但语法混乱，Windows 上不友好 |
| **CMake** | 跨平台，生成 Makefile 或 IDE 工程 |
| **SCons** | 用 Python 写脚本，语法清晰，RT-Thread 采用 |

---

## 二、SCons 是什么？

**SCons = 用 Python 语法来控制的构建系统**

与 Make 用 Makefile 不同，SCons 使用两种脚本文件：

| 文件 | 角色 | 数量 |
|------|------|------|
| **SConstruct** | 构建的"入口"，相当于程序的主函数 | 一个项目只有 **1 个** |
| **SConscript** | 分布在各个子目录中的"子脚本"，负责各自目录下的源码构建 | 每个源码子目录通常有 **1 个** |

> 通俗理解：SConstruct 是"总指挥"，SConscript 是各个部门的"小组长"。

### RT-Thread 项目的典型文件结构

```
bsp_xxx/
 ├── SConstruct        ← 构建入口（总指挥）
 ├── rtconfig.py       ← 编译器配置文件（用哪个编译器、路径在哪）
 ├── Kconfig           ← menuconfig 配置入口
 ├── rt-thread/
 │    └── src/
 │         ├── SConscript  ← 子脚本（小组长）
 │         ├── Kconfig
 │         └── *.c
 └── 其他子目录/
      └── SConscript  ← 每个有源码的子目录都有一个
```

### RT-Thread 构建系统的组成

```
RT-Thread 构建系统
 ├── Kconfig          → 配置项（哪些功能开/关）
 ├── rtconfig.py      → 编译器配置（工具链、路径等）
 └── SCons
      ├── SConstruct  → 入口脚本
      └── SConscript  → 子脚本
           ├── SCons 标准函数
           ├── RT-Thread 自定义函数
           └── Python 函数（随意使用！）
```

---

## 三、SCons 基本命令速查

在 Env 工具命令行中，进入 BSP 目录后使用以下命令：

### 3.1 编译相关

| 命令 | 作用 | 说明 |
|------|------|------|
| `scons` | 编译工程 | 默认使用 ARM GCC；支持**增量编译**（只编译改过的文件） |
| `scons -s` | 静默编译 | 不打印具体的内部编译命令 |
| `scons -jN` | 多线程编译 | N = 线程数，一般 CPU 核心数 × 2，如 4 核用 `scons -j8` |
| `scons -c` | 清除编译产物 | 删除所有临时文件和目标文件（类似 `make clean`） |
| `scons --verbose` | 显示详细编译参数 | 可以看到完整的编译器调用命令 |

### 3.2 生成 IDE 工程

| 命令 | 作用 |
|------|------|
| `scons --target=mdk5` | 生成 Keil MDK5 工程（project.uvprojx） |
| `scons --target=mdk4` | 生成 Keil MDK4 工程 |
| `scons --target=iar` | 生成 IAR 工程（project.eww） |
| `scons --target=cmake` | 生成 CMake 脚本 |
| `scons --target=makefile` | 生成 Makefile |
| `scons --target=vs2012` | 生成 Visual Studio 2012 工程 |

> **注意**：生成 IDE 工程前，BSP 目录需要有一个工程模板文件，SCons 会根据模板 + 源码配置生成完整工程。

### 3.3 切换编译器

如果不用默认的 ARM GCC，需要手动指定编译器：

```bash
# 使用 Keil MDK
set RTT_CC=keil
set RTT_EXEC_PATH=C:/Keilv5

# 使用 GCC（如树莓派 Pico）
set RTT_CC=gcc
set RTT_EXEC_PATH=D:\software\RaspberryPi-Pico\gcc\2020-q4-major\bin
```

### 3.4 其他实用命令

| 命令 | 作用 |
|------|------|
| `scons --dist` | 导出精简项目框架到 dist 目录，去掉无关 BSP 和 libcpu |
| `scons --dist-ide` | 导出可被 RT-Thread Studio 导入的工程 |
| `scons --buildlib=xxx` | 将指定 Group 编译为静态库（xxx 为 Group 名） |

---

## 四、SCons 函数详解

### 4.1 SCons 原生标准函数

这些是 SCons 自带的函数，任何 SCons 项目都能用。

#### `Import(vars)` — 导入变量

从其他脚本中导入变量（变量名是字符串）。

```python
Import('rtconfig')   # 导入名为 rtconfig 的变量
Import('env')        # 导入环境变量
```

#### `Export(vars)` — 导出变量

把当前脚本中的变量"共享"给其他脚本使用。

```python
Export('rtconfig')   # 把 rtconfig 变量导出，供子脚本 Import
```

> **Import/Export 的关系**：父脚本 `Export` 导出变量 → 子脚本 `Import` 导入变量，实现脚本之间的数据传递。

#### `SConscript(scripts, ...)` — 调用子脚本

读取并执行另一个 SConscript 文件，实现分层构建。

```python
SConscript('src/SConscript')                    # 调用子目录的脚本
SConscript('drivers/SConscript', variant_dir='build/drivers')  # 指定输出目录
```

| 参数 | 说明 |
|------|------|
| `scripts` | 要导入的 SConscript 文件路径 |
| `exports` | 传递给子脚本的变量（可选） |
| `variant_dir` | 编译产物的输出路径（可选） |
| `duplicate` | 是否拷贝源文件到 variant_dir（可选） |

#### `Glob(pattern)` — 文件匹配

返回匹配模式的所有文件列表，常用于收集 `.c` 文件。

```python
src = Glob('*.c')           # 获取当前目录下所有 .c 文件
src = Glob('*.c') + Glob('*.cpp')  # 同时获取 .c 和 .cpp
```

#### `Split(str)` — 分割字符串为列表

把一段文本按空格/换行分割成列表。

```python
src = Split('''
shell.c
msh.c
cmd.c
''')
# 等价于：src = ['shell.c', 'msh.c', 'cmd.c']
```

#### `IsDefined(depend)` — 判断宏是否定义

```python
if IsDefined('RT_USING_FINSH'):
    # 如果定义了 RT_USING_FINSH 宏，执行这里的代码
```

---

### 4.2 RT-Thread 自定义函数

这些是 RT-Thread 在 SCons 基础上扩展的函数，**需要先 `from building import *` 才能使用**。

#### `DefineGroup(name, src, depend, **parameters)` — 定义一个构建组（最核心！）

这是 RT-Thread 中**最重要的函数**，用来定义一组源文件是否参与编译。

```python
from building import *

cwd = GetCurrentDir()
src = Glob('*.c')
CPPPATH = [cwd]

group = DefineGroup('MyDriver', src, depend=['RT_USING_MY_DRIVER'], CPPPATH=CPPPATH)
Return('group')
```

| 参数 | 说明 | 是否必填 |
|------|------|----------|
| `name` | Group 的名字（对应 IDE 中的分组名） | 必填 |
| `src` | 包含的源文件列表 | 必填（可以为空列表 `[]`） |
| `depend` | 依赖的宏定义，只有 rtconfig.h 中定义了这些宏，该组才会被编译 | 必填（可以为 `['']`） |
| `CPPPATH` | 头文件搜索路径 | 可选 |
| `CCFLAGS` | C/C++ 通用编译参数 | 可选 |
| `CFLAGS` | C 文件独有编译参数 | 可选 |
| `CXXFLAGS` | C++ 文件独有编译参数 | 可选 |
| `CPPDEFINES` | 全局宏定义 | 可选 |
| `LIBRARY` | 是否构建为静态库 | 可选 |

> **通俗理解**：`DefineGroup` 就像一个"开关"——你在 menuconfig 里打开了某个功能（定义了某个宏），对应的 Group 才会被编译；关掉就不编译。

#### `GetCurrentDir()` — 获取当前路径

```python
cwd = GetCurrentDir()  # 返回当前 SConscript 所在的目录路径
```

#### `GetDepend(depend)` — 条件判断宏

```python
if GetDepend('MSH_USING_BUILT_IN_COMMANDS'):
    src += ['cmd.c']    # 只有启用 MSH 内建命令时，才编译 cmd.c
```

#### `SrcRemove(src, remove)` — 从构建列表移除文件

```python
SrcRemove(src, ['test_file.c'])  # 把 test_file.c 从编译列表中移除
```

#### `AddDepend(option)` — 添加宏定义

```python
AddDepend('RT_USING_DMA')  # 强制添加一个宏定义
```

#### `GetConfigValue(name)` — 获取宏的值

```python
val = GetConfigValue('RT_NAME_MAX')  # 获取 RT_NAME_MAX 这个宏的值
```

#### `GetVersion()` — 获取 RT-Thread 版本

```python
ver = GetVersion()  # 返回 RT-Thread 的版本号
```

#### `GlobSubDir(sub_dir, ext_name)` — 递归匹配文件

```python
all_c = GlobSubDir('.', '.c')  # 获取当前目录及所有子目录下的 .c 文件
```

#### `BuildPackage(package)` — 按 JSON 定义 Group

```python
group = BuildPackage('package.json')  # 从 JSON 文件读取配置来定义 Group
```

---

### 4.3 Python 函数

SConscript 本质就是 Python 脚本，所以**所有 Python 标准库都能用**：

```python
import os
import shutil

# 例：判断目录是否存在
if os.path.exists('drivers'):
    # 做一些操作...

# 例：复制文件
shutil.copy('template.uvprojx', 'project.uvprojx')
```

> 这是 SCons 相比 Make 的一大优势：遇到复杂逻辑时，可以直接写 Python 代码来处理。

---

## 五、实战示例：一个完整的 SConscript

```python
# 第一步：导入 RT-Thread 构建模块（几乎每个 SConscript 都要写）
from building import *

# 第二步：获取当前目录路径，设置头文件搜索路径
cwd = GetCurrentDir()
CPPPATH = [cwd]

# 第三步：列出需要编译的源文件
src = Split('''
shell.c
msh.c
''')

# 第四步：根据宏定义，条件性地添加源文件
if GetDepend('MSH_USING_BUILT_IN_COMMANDS'):
    src += ['cmd.c']

if GetDepend('DFS_USING_POSIX'):
    src += ['msh_file.c']

# 第五步：定义 Group —— 只有 rtconfig.h 中定义了 RT_USING_FINSH 时才会编译
group = DefineGroup('Finsh', src, depend=['RT_USING_FINSH'], CPPPATH=CPPPATH)

# 第六步：返回构建对象给上级脚本
Return('group')
```

**逐行解读**：
1. `from building import *` → 加载 RT-Thread 的自定义函数
2. `GetCurrentDir()` → 拿到当前目录，作为头文件搜索路径
3. `Split(...)` → 列出"固定要编译"的源文件
4. `GetDepend(...)` → 根据配置开关，决定"额外要编译"哪些文件
5. `DefineGroup(...)` → 把以上信息打包成一个"组"，并设定编译条件
6. `Return('group')` → 把这个组交回给 SConstruct 总脚本

---

## 六、核心概念总结

```
┌─────────────────────────────────────────────────┐
│               RT-Thread 构建流程                  │
├─────────────────────────────────────────────────┤
│                                                 │
│  1. menuconfig 配置 → 生成 rtconfig.h           │
│     （决定哪些功能开/关，即哪些宏被定义）            │
│                                                 │
│  2. rtconfig.py 配置 → 指定编译器和路径            │
│     （决定用什么工具链来编译）                      │
│                                                 │
│  3. scons 命令 → 读取 SConstruct                │
│     → SConstruct 调用各子目录的 SConscript        │
│     → 每个 SConscript 用 DefineGroup 定义组       │
│     → 根据 rtconfig.h 中的宏决定哪些组参与编译     │
│     → 最终调用编译器生成目标文件                    │
│                                                 │
└─────────────────────────────────────────────────┘
```

### 关键要点

- **SConstruct** 是入口，**SConscript** 是干活的
- **DefineGroup** 是核心函数，控制"哪些文件、在什么条件下、被编译"
- **depend 参数**是开关——对应 menuconfig 中的配置项
- SConscript 就是 Python 脚本，可以用任何 Python 语法和库
- `scons` 命令编译，`scons -c` 清除，`scons --target=xxx` 生成 IDE 工程
