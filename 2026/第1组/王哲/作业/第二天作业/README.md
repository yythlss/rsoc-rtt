# 【DAY2】RT-Thread 启动流程、内核基础与多线程编程

- **目标平台**：STM32F407 星火号（`stm32f407-rtspark`）
- **提交仓库**：`https://gitee.com/rtthread/rsoc-rtt`
- **作业交付物**：理论总结与实验报告（`README.md`）+ 可编译源码（`*.c`）通过 Gitee PR 提交。

## Part 1：理论知识梳理文档（写入作业目录 `README.md`）

### 1. STM32F407 启动流程与 RT-Thread 引导机制

- **启动路径**：详细画出/写出从上电复位 `Reset_Handler` 到进入 `main()` 的完整调用链。
- **自动初始化原理**：解释 `INIT_BOARD_EXPORT` 与 `INIT_APP_EXPORT` 的底层实现原理。

### 2. 内核基础与对象模型

- **对象容器与控制块**：简述 RT-Thread 面向对象的设计思想，线程控制块（`struct rt_thread`）中关键成员（如 `stat`、`current_priority`、`init_tick`、`sp`）的作用。
- **内存分配方式**：对比静态对象与动态对象的区别（分配位置、生命周期、释放机制），分析为什么某些高可靠场景强制使用静态线程。

### 3. 线程状态机与调度算法

- **五态模型**：绘制或描述 RT-Thread 线程状态（初始态、就绪态、运行态、挂起态、关闭态）及触发状态转换的典型 API。
- **调度机制剖析**：
  - **抢占式调度**
  - **时间片轮转**

## Part 2：多线程实战编程（C 语言实现）

### 任务说明

1. **工作线程 1（动态创建 `th_led1`）**：
   - 优先级 `16`，时间片 `5 ticks`，栈 `1024 Bytes`。
   - 循环翻转板载 LED_R，每次执行打印计数与 `rt_tick_get()`。
2. **工作线程 2（静态初始化 `th_led2`）**：
   - 优先级 `16`，时间片 `5 ticks`，静态定义全局栈与 TCB。
   - 循环翻转板载 LED_B，每次执行打印计数。
3. **监控与控制线程（动态创建 `th_monitor`）**：
   - 优先级 `10`（高于工作线程），周期延时 `rt_thread_mdelay(2000)`。
   - 唤醒后获取并打印 `th_led1` 与 `th_led2` 的运行状态与剩余栈空间大小。
   - 结合板载按键检测（或按键中断）：单次触发时调用 `rt_thread_suspend()` 挂起 `th_led1`；再次触发时调用 `rt_thread_resume()` 恢复。
4. **系统集成与调试命令**：
   - 使用 `INIT_APP_EXPORT()` 自动加载测试模块，禁止在 `main.c` 中硬编码调用。
   - 使用 `MSH_CMD_EXPORT()` 导出自定义查看命令（如 `thread_dump`）。

## Part 3：Gitee PR 提交与规范

### 1. 提交目录规范

```
2026/<组别>/<姓名>/第二天作业
├── README.md             # 包含：Part 1 理论整理 + Part 2 实验结果分析与串口截图
└── thread_study.c        # 作业源码（含动/静态线程、自动初始化、MSH导出）
```

### 2. PR 提交流程与规范

1. Fork 官方仓库 [https://gitee.com/rtthread/rsoc-rtt](https://gitee.com/rtthread/rsoc-rtt) 到个人账号。
2. 基于 `master` 新建分支：`day2-姓名拼音`。
3. 编写代码、文档并进行本地 `scons` 编译验证与星火1号烧录测试。
4. 提交 Commit。
5. 向主仓库提交 Pull Request：
   - **PR 标题**：`[作业提交] 内核与多线程培训 - <姓名>`
   - **PR 内容**：附带理论文档简述、硬件运行现象照片及 FinSH 串口日志输出。
