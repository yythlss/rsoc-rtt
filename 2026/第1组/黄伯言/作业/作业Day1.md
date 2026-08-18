# 作业 · Day1

> 提交人：黄伯言（第1组）｜日期：2026-08-17

## 一、开发环境搭建

- [x]  安装 RT-Thread Studio
- [x]  替换 `app.asar` 完成破解
- [x]  SDK Manager 下载 **RT-Thread 4.1.0** 源码 + STM32F4 支持包
- [x]  确认 ST-Link 驱动已安装

## 二、Git 环境配置（已完成）

## 三、STM32F407 实操

### 3.1 工程信息

- **工程名**：`
- **RT-Thread 内核版本**：4.1.0（构建时间 `build Aug 17 2026 11:23:12`）
- **板级芯片**：STM32F407
- **IDE**：RT-Thread Studio
- **工程结构**：`applications/main.c` + `CubeMX Settings` + `drivers` + `libraries` + `linkscripts` + `rt-thread [4.1.0]` + `rtconfig.h`

### 3.2 串口配置

| 参数   | 值          |
| ---- | ---------- |
| 串行端口 | **COM9**   |
| 波特率  | **115200** |
| 数据位  | 8          |
| 校验位  | None       |
| 停止位  | 1          |
| 流控   | None       |
| 编码   | UTF-8      |

### 3.3 关键代码（`applications/main.c`）

```
/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-17     RT-Thread    first version
 */

#include <rtthread.h>

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

int main(void)
{
    rt_kprintf("hello2026\n");

    return RT_EOK;
}

```

### 3.4 终端运行结果

**启动 banner** 

```····
 \ | /
- RT -     Thread Operating System
 / | \     4.1.1 build Aug 17 2026 10:42:05
 2006 - 2022 Copyright by RT-Thread team
hello2026
```

**`msh />free` 内存查询**

```
msh >free
total    : 126168
used     : 4876
maximum  : 7076
available: 121292
```

```**`msh /&gt;free` 内存查询**
msh >list_thread
thread   pri  status      sp     stack size max used left tick  error
-------- ---  ------- ---------- ----------  ------  ---------- ---
tshell    20  running 0x000000c8 0x00001000    14%   0x00000006 OK
tidle0    31  ready   0x0000005c 0x00000100    64%   0x0000000a OK
timer      4  suspend 0x0000007c 0x00000200    24%   0x00000009 OK
```

```
msh >list_sem
semaphor v   suspend thread
-------- --- --------------
shrx     000 0
```



`msh` 交互正常，`help` 命令可列出 `list_thread` / `list_sem` / `list_event` / `list_mutex` / `list_mailbox` / `list_msgqueue` / `list_mempool` / `list_timer` 等 RT-Thread shell 工具。

### 3.5 完成项

- [x]  创建 STM32F407 Hello World 工程（标准版内核）
- [x]  编译生成 `.elf` / `.bin`，ST-Link 下载
- [x]  终端查看启动日志（`hello RSOC2026!` + RT-Thread 4.1.1 banner）
- [x]  断点调试可用（RT-Thread Studio 集成）
- [x]  msh 交互正常（`free` / `help` / `list_*` 等命令验证通过）

## 四、PR 提交说明

- 本次提交为「当天作业 / 笔记提交」PR，对上游仓库发起，指定**小组长**为**易莹莹**
