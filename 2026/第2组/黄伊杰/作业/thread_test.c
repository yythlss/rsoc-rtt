/*
 * 文件: thread_test.c
 * 平台: 星火1号开发板 (STM32F407ZGT6) + RT-Thread
 * 功能: DAY2 多线程实战编程
 *   - th_led1: 动态创建，翻转 LED_R (PF12)
 *   - th_led2: 静态初始化，翻转 LED_B (PF11)
 *   - th_monitor: 动态创建，监控线程状态 + 按键控制挂起/恢复
 */

 #include <rtthread.h>
 #include <rtdevice.h>
 #include <board.h>
 
 /*============================================================================
  *  硬件引脚定义（对照星火1号原理图 & 用户手册）
  *============================================================================*/
 /* RGB LED: 共阳极，低电平点亮，高电平熄灭 */
 #define PIN_LED_R       GET_PIN(F, 12)    /* PF12: LED_R */
 #define PIN_LED_B       GET_PIN(F, 11)    /* PF11: LED_B */
 
 /* 板载按键: 按下为低电平，松开为高电平 */
 #define PIN_KEY_LEFT    GET_PIN(C, 0)     /* PC0: KEY0 (LEFT)  */
 #define PIN_KEY_DOWN    GET_PIN(C, 1)     /* PC1: KEY1 (DOWN)  */
 #define PIN_KEY_RIGHT   GET_PIN(C, 4)     /* PC4: KEY2 (RIGHT) */
 #define PIN_KEY_UP      GET_PIN(C, 5)     /* PC5: WK_UP (UP)   */
 
 /*============================================================================
  *  线程参数定义
  *============================================================================*/
 #define THREAD_PRIORITY     16      /* 工作线程优先级 */
 #define THREAD_TIMESLICE    5       /* 时间片 5 ticks */
 #define THREAD_STACK_SIZE   1024    /* 栈大小 1024 Bytes */
 
 #define MONITOR_PRIORITY    10      /* 监控线程优先级（高于工作线程） */
 #define MONITOR_STACK_SIZE  1024    /* 监控线程栈大小 */
 
 /*============================================================================
  *  线程句柄与静态资源
  *============================================================================*/
 static rt_thread_t tid_led1 = RT_NULL;      /* th_led1 动态线程句柄 */
 static rt_thread_t tid_monitor = RT_NULL;   /* th_monitor 动态线程句柄 */
 
 /* th_led2 静态线程：全局 TCB 与栈 */
 ALIGN(RT_ALIGN_SIZE)
 static char th_led2_stack[THREAD_STACK_SIZE];
 static struct rt_thread th_led2_tcb;
 
 /* 按键控制标志：0 = th_led1 运行中，1 = th_led1 已挂起 */
 static volatile rt_uint8_t led1_suspended = 0;
 
 /*============================================================================
  *  工作线程 1 入口（动态创建 th_led1）
  *  功能：循环翻转 LED_R，打印计数与 rt_tick_get()
  *============================================================================*/
 static void th_led1_entry(void *parameter)
 {
     rt_uint32_t count = 0;
 
     while (1)
     {
         /* 翻转 LED_R（PF12）：低电平亮，高电平灭 */
         rt_pin_write(PIN_LED_R, (count % 2 == 0) ? PIN_LOW : PIN_HIGH);
 
         /* 打印计数与当前系统 tick */
         rt_kprintf("[th_led1] count = %d, tick = %d\n", count, rt_tick_get());
 
         count++;
 
         /* 延时 500ms，让出 CPU */
         rt_thread_mdelay(500);
     }
 }
 
 /*============================================================================
  *  工作线程 2 入口（静态初始化 th_led2）
  *  功能：循环翻转 LED_B，打印计数
  *============================================================================*/
 static void th_led2_entry(void *parameter)
 {
     rt_uint32_t count = 0;
 
     while (1)
     {
         /* 翻转 LED_B（PF11）：低电平亮，高电平灭 */
         rt_pin_write(PIN_LED_B, (count % 2 == 0) ? PIN_LOW : PIN_HIGH);
 
         /* 打印计数 */
         rt_kprintf("[th_led2] count = %d\n", count);
 
         count++;
 
         /* 延时 500ms，让出 CPU */
         rt_thread_mdelay(500);
     }
 }
 
 /*============================================================================
  *  按键中断回调函数
  *  功能：单次触发挂起/恢复 th_led1
  *  注意：中断回调中不能调用 rt_thread_suspend/resume，
  *        因此仅设置标志，由 th_monitor 线程执行实际操作
  *============================================================================*/
 static void key_irq_callback(void *args)
 {
     /* 翻转挂起标志 */
     led1_suspended = !led1_suspended;
     rt_kprintf("[KEY IRQ] led1_suspended flag = %d\n", led1_suspended);
 }
 
 /*============================================================================
  *  监控与控制线程入口（动态创建 th_monitor）
  *  功能：
  *    1. 周期打印 th_led1 与 th_led2 的运行状态与剩余栈空间
  *    2. 根据按键标志执行挂起/恢复 th_led1
  *============================================================================*/
 static void th_monitor_entry(void *parameter)
 {
     while (1)
     {
         /* ---------- 按键控制：挂起 / 恢复 th_led1 ---------- */
         if (led1_suspended && tid_led1 != RT_NULL)
         {
             /* 仅在线程处于就绪/运行态时才能挂起 */
             if (tid_led1->stat == RT_THREAD_READY ||
                 tid_led1->stat == RT_THREAD_RUNNING)
             {
                 rt_thread_suspend(tid_led1);
                 rt_schedule();
                 rt_kprintf("[th_monitor] th_led1 SUSPENDED\n");
             }
         }
         else if (!led1_suspended && tid_led1 != RT_NULL)
         {
             /* 仅在线程处于挂起态时才能恢复 */
             if (tid_led1->stat == RT_THREAD_SUSPEND)
             {
                 rt_thread_resume(tid_led1);
                 rt_schedule();
                 rt_kprintf("[th_monitor] th_led1 RESUMED\n");
             }
         }
 
         /* ---------- 打印线程状态与剩余栈空间 ---------- */
         if (tid_led1 != RT_NULL)
         {
             rt_uint32_t free_stack1 = (rt_uint32_t)tid_led1->stack_addr +
                                       tid_led1->stack_size -
                                       (rt_uint32_t)tid_led1->sp;
             rt_kprintf("[th_monitor] th_led1: stat=0x%02x, stack free=%d/%d bytes\n",
                        tid_led1->stat, free_stack1, tid_led1->stack_size);
         }
 
         {
             rt_uint32_t free_stack2 = (rt_uint32_t)th_led2_tcb.stack_addr +
                                       th_led2_tcb.stack_size -
                                       (rt_uint32_t)th_led2_tcb.sp;
             rt_kprintf("[th_monitor] th_led2: stat=0x%02x, stack free=%d/%d bytes\n",
                        th_led2_tcb.stat, free_stack2, th_led2_tcb.stack_size);
         }
 
         rt_kprintf("-------------------------------------------\n");
 
         /* 周期延时 2000ms */
         rt_thread_mdelay(2000);
     }
 }
 
 /*============================================================================
  *  MSH 调试命令：thread_dump
  *  功能：手动查看所有自定义线程的状态信息
  *============================================================================*/
 static void thread_dump(void)
 {
     rt_kprintf("====== Thread Dump ======\n");
 
     if (tid_led1 != RT_NULL)
     {
         rt_uint32_t free = (rt_uint32_t)tid_led1->stack_addr +
                            tid_led1->stack_size - (rt_uint32_t)tid_led1->sp;
         rt_kprintf("  th_led1   : prio=%d, stat=0x%02x, stack_free=%d/%d\n",
                    tid_led1->current_priority, tid_led1->stat,
                    free, tid_led1->stack_size);
     }
     else
     {
         rt_kprintf("  th_led1   : NOT CREATED\n");
     }
 
     {
         rt_uint32_t free = (rt_uint32_t)th_led2_tcb.stack_addr +
                            th_led2_tcb.stack_size - (rt_uint32_t)th_led2_tcb.sp;
         rt_kprintf("  th_led2   : prio=%d, stat=0x%02x, stack_free=%d/%d\n",
                    th_led2_tcb.current_priority, th_led2_tcb.stat,
                    free, th_led2_tcb.stack_size);
     }
 
     if (tid_monitor != RT_NULL)
     {
         rt_uint32_t free = (rt_uint32_t)tid_monitor->stack_addr +
                            tid_monitor->stack_size - (rt_uint32_t)tid_monitor->sp;
         rt_kprintf("  th_monitor: prio=%d, stat=0x%02x, stack_free=%d/%d\n",
                    tid_monitor->current_priority, tid_monitor->stat,
                    free, tid_monitor->stack_size);
     }
 
     rt_kprintf("  led1_suspended = %d\n", led1_suspended);
     rt_kprintf("=========================\n");
 }
 MSH_CMD_EXPORT(thread_dump, dump custom thread status);
 
 /*============================================================================
  *  模块初始化入口（INIT_APP_EXPORT 自动加载，无需在 main.c 中调用）
  *============================================================================*/
 static int thread_task_init(void)
 {
     /* ---------- 1. 初始化 GPIO ---------- */
     /* LED 引脚设为输出模式，默认熄灭（高电平） */
     rt_pin_mode(PIN_LED_R, PIN_MODE_OUTPUT);
     rt_pin_mode(PIN_LED_B, PIN_MODE_OUTPUT);
     rt_pin_write(PIN_LED_R, PIN_HIGH);
     rt_pin_write(PIN_LED_B, PIN_HIGH);
 
     /* 按键引脚设为输入上拉模式 */
     rt_pin_mode(PIN_KEY_LEFT, PIN_MODE_INPUT_PULLUP);
 
     /* ---------- 2. 配置按键中断（下降沿触发） ---------- */
     rt_pin_attach_irq(PIN_KEY_LEFT, PIN_IRQ_MODE_FALLING,
                       key_irq_callback, (void *)PIN_KEY_LEFT);
     rt_pin_irq_enable(PIN_KEY_LEFT, PIN_IRQ_ENABLE);
 
     /* ---------- 3. 创建工作线程 1：th_led1（动态创建） ---------- */
     tid_led1 = rt_thread_create("th_led1",
                                 th_led1_entry, RT_NULL,
                                 THREAD_STACK_SIZE,
                                 THREAD_PRIORITY,
                                 THREAD_TIMESLICE);
     if (tid_led1 != RT_NULL)
     {
         rt_thread_startup(tid_led1);
         rt_kprintf("[init] th_led1 created & started\n");
     }
     else
     {
         rt_kprintf("[init] ERROR: th_led1 create failed!\n");
     }
 
     /* ---------- 4. 创建工作线程 2：th_led2（静态初始化） ---------- */
     rt_thread_init(&th_led2_tcb,
                    "th_led2",
                    th_led2_entry, RT_NULL,
                    th_led2_stack, sizeof(th_led2_stack),
                    THREAD_PRIORITY,
                    THREAD_TIMESLICE);
     rt_thread_startup(&th_led2_tcb);
     rt_kprintf("[init] th_led2 initialized & started\n");
 
     /* ---------- 5. 创建监控线程：th_monitor（动态创建） ---------- */
     tid_monitor = rt_thread_create("th_monitor",
                                    th_monitor_entry, RT_NULL,
                                    MONITOR_STACK_SIZE,
                                    MONITOR_PRIORITY,
                                    THREAD_TIMESLICE);
     if (tid_monitor != RT_NULL)
     {
         rt_thread_startup(tid_monitor);
         rt_kprintf("[init] th_monitor created & started\n");
     }
     else
     {
         rt_kprintf("[init] ERROR: th_monitor create failed!\n");
     }
 
     return 0;
 }
 /* 使用 INIT_APP_EXPORT 自动加载，禁止在 main.c 中硬编码调用 */
 INIT_APP_EXPORT(thread_task_init);
 