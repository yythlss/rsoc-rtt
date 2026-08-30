# 第4组

## Part 2 多线程实验

### 1. 实现

| 线程 | 方式 | 优先级/时间片/栈 | 功能 |
|---|---|---|---|
| th_led1 | 动态 create | 16/5/1024 | 翻转 LED_R，打印 count+tick |
| th_led2 | 静态 init | 16/5/全局栈+TCB | 翻转 LED_B，打印 count |
| th_monitor | 动态 create | 10/5/1024 | 每2s打印状态+剩余栈；按键挂起/恢复 th_led1 |

`INIT_APP_EXPORT` 自动加载；`MSH_CMD_EXPORT(thread_dump)` 导出命令。

### 2. 运行结果

```
[th_led1] count=177 tick=88569
[th_led2] count=177
[th_led1] count=178 tick=89073
[th_led2] count=178
[th_monitor] tick=90567
  th_led1 : stat=suspend prio=16 剩余栈=780/1024
  th_led2 : stat=suspend prio=16 剩余栈=776/1024
```

- 两工作线程计数同步交替，时间片轮转正常
- monitor 每 2s 准点抢占打印，优先级正常
- 按键可挂起/恢复 th_led1