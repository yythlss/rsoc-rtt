# 星火1号多线程实战 - 使用说明

## 一、硬件配置

根据星火1号开发板原理图和用户手册：

| 外设 | 引脚 | 说明 |
|------|------|------|
| LED_R (红色) | PF12 | 共阳LED，低电平点亮 |
| LED_B (蓝色) | PF11 | 共阳LED，低电平点亮 |
| KEY0 (按键) | PC0 | 按下为低电平，外部上拉 |

## 二、RT-Thread Studio 工程配置步骤

### 2.1 创建工程

1. 打开 RT-Thread Studio
2. 点击 **文件** → **新建** → **RT-Thread 项目**
3. 选择 **基于芯片** → 搜索 **STM32F407ZGT6**
4. 或选择 **基于开发板** → 搜索 **STM32F407-RT-SPARK** (星火1号)
5. 输入项目名称，点击 **完成**

### 2.2 添加源文件

1. 在工程资源管理器中，找到 `applications` 文件夹
2. 将 `thread_test.c` 复制到 `applications` 目录下
3. 右键点击项目 → **刷新**

### 2.3 配置 RT-Thread 组件

打开 **RT-Thread Settings** (rtconfig.h)，确保以下配置已启用：

```
√ 启用 PIN 设备驱动
√ 启用 FinSH/MSH 组件
√ 启用自动初始化 (AUTO_INIT)
√ 启用线程间通信 (IPC) - 信号量
```

### 2.4 编译与下载

1. 点击 **项目** → **构建项目** (或 Ctrl+B)
2. 连接星火1号开发板的 ST-Link USB 口到电脑
3. 点击 **运行** → **调试** (或 F11) 下载固件

## 三、运行效果

### 3.1 预期现象

1. **LED_R (红色)**: 以 500ms 间隔闪烁
2. **LED_B (蓝色)**: 以 500ms 间隔闪烁
3. **串口输出**: 通过 ST-Link 虚拟串口 (115200 8N1) 查看日志

### 3.2 串口日志示例

```
[thread_test] ===== Multi-thread Test Start =====
[thread_test] Board: Spark No.1 (STM32F407ZGT6)
[thread_test] th_led1 started (dynamic, prio=16, slice=5, stack=1024)
[thread_test] th_led2 started (static,  prio=16, slice=5, stack=1024)
[thread_test] th_monitor started (dynamic, prio=10, period=2000ms)
[thread_test] Press KEY0 to suspend/resume th_led1
[thread_test] Type 'thread_dump' in msh to check threads
[thread_test] ===== Multi-thread Test Ready =====

[th_led1] count=1    sys_tick=12
[th_led2] count=1
[th_led1] count=2    sys_tick=512
[th_led2] count=2
[monitor] th_led1  stat=SUSPEND  prio=16 free stack 856/1024 bytes
[monitor] th_led2  stat=SUSPEND  prio=16 free stack 872/1024 bytes
------------------------------------------------
```

### 3.3 按键功能

- **第1次按下 KEY0**: 挂起 th_led1 (红色LED停止闪烁，串口停止打印 count)
- **第2次按下 KEY0**: 恢复 th_led1 (红色LED继续闪烁)
- 依此类推，奇数次挂起，偶数次恢复
- 按键事件由 KEY0 下降沿中断释放信号量通知 monitor 线程，
  monitor 每 2 秒周期唤醒时处理，**最多延迟一个周期(2s)** 生效
- 挂起后 monitor 周期报告中 `th_led1 stat=SUSPEND`，且剩余栈不再变化

### 3.4 MSH 命令

在串口终端中输入 `thread_dump` 查看线程状态：

```
msh > thread_dump
=============== thread_dump ===============
[dump] th_led1     stat=SUSPEND  prio=16 free stack 856/1024 bytes
[dump] th_led2     stat=READY    prio=16 free stack 872/1024 bytes
[dump] th_monitor  stat=RUNNING  prio=10 free stack 512/1024 bytes
th_led1 paused: YES
============================================
```

## 四、线程设计说明

### 4.1 线程参数对比

| 线程 | 创建方式 | 优先级 | 时间片 | 栈大小 | 功能 |
|------|----------|--------|--------|--------|------|
| th_led1 | 动态 (rt_thread_create) | 16 | 5 tick | 1024 B | 翻转LED_R，打印count+tick |
| th_led2 | 静态 (rt_thread_init) | 16 | 5 tick | 1024 B | 翻转LED_B，打印count |
| th_monitor | 动态 (rt_thread_create) | 10 | 10 tick | 1024 B | 状态监控+按键控制 |

### 4.2 关键设计点

1. **优先级设置**: th_monitor (10) > th_led1/th_led2 (16)，确保监控线程优先执行
2. **按键中断安全**: ISR 中只做 20ms 消抖和信号量释放，
   线程挂起/恢复在 monitor 线程中执行（ISR 上下文禁止操作线程）
3. **周期与响应**: monitor 用 `rt_thread_mdelay(2000)` 实现周期唤醒，
   唤醒后先打印状态报告，再以非阻塞方式 (`RT_WAITING_NO`) 取出信号量处理按键事件
4. **挂起时机的正确处理**: `rt_thread_suspend()` 只能挂起**就绪态**线程，
   对 mdelay 中（挂起态）的线程调用会返回 `-RT_ERROR`。
   代码在锁调度器 (`rt_enter_critical`) 的前提下等待 th_led1 的周期定时器
   把它置为就绪态（此时它优先级低无法运行），随即挂起必定成功，
   最长等待不超过一个闪烁周期 500ms；恢复则直接 `rt_thread_resume()`
5. **剩余栈测量**: 采用 finsh `list_thread` 同款的 `'#'` 魔术字扫描法
   （内核初始化线程时将栈整体填充为 `'#'`，栈从高地址向低地址增长，
   从栈底开始连续 `'#'` 的长度即空闲区），反映历史最深水位，比读 `sp` 更准确
6. **自动初始化**: 使用 `INIT_APP_EXPORT()` 自动注册，无需修改 main.c

## 五、常见问题

### Q1: LED 不亮
- 检查引脚定义是否正确 (PF11/PF12)
- 确认共阳LED需要低电平点亮
- 使用万用表测量引脚电压

### Q2: 按键无响应
- 检查 KEY0 是否连接到 PC0
- 确认按键按下时为低电平
- 检查 EXTI 中断是否配置为下降沿触发

### Q3: 串口无输出
- 确认 ST-Link 驱动已安装
- 串口参数: 115200 8N1
- 检查 rtconfig.h 中是否启用了 UART 驱动

### Q4: 编译错误
- 确认已添加 `thread_test.c` 到 applications 目录
- 检查 rtconfig.h 中是否启用了 PIN 设备和 FinSH 组件
- 确认 RT-Thread 版本兼容 (推荐 4.x 或 5.x)

## 六、扩展实验

### 6.1 修改 LED 颜色
```c
/* 同时点亮红色和蓝色 = 紫色 */
rt_pin_write(PIN_LED_R, LED_ON);
rt_pin_write(PIN_LED_B, LED_ON);
```

### 6.2 添加更多线程
```c
/* 创建 th_led3 控制外部 LED 矩阵 */
rt_thread_t th_led3 = rt_thread_create("th_led3",
                                        th_led3_entry,
                                        RT_NULL,
                                        1024, 20, 5);
rt_thread_startup(th_led3);
```

### 6.3 使用互斥量保护共享资源
```c
rt_mutex_t mutex = rt_mutex_create("led_mutex", RT_IPC_FLAG_FIFO);
rt_mutex_take(mutex, RT_WAITING_FOREVER);
/* 访问共享资源 */
rt_mutex_release(mutex);
```

## 七、参考文档

- 《RT-Thread 星火1号用户手册 V1.1.0》
- 《星火1号开发板原理图 V1.0》
- 《RT-Thread 内核-线程》编程指南
- RT-Thread 官方文档: https://www.rt-thread.org/document/site/

---

**文档版本**: 1.0  
**更新日期**: 2026-08-18  
**适用开发板**: RT-Thread SPARK (星火1号)  
**适用芯片**: STM32F407ZGT6
