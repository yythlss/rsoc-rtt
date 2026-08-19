  #include <rtthread.h>
  #include <rtdevice.h>
  #include <rthw.h>
  #include <board.h>
  #include <finsh.h>

  /* 星火一号硬件引脚 */
  #define LED_R_PIN              GET_PIN(F, 12)
  #define LED_G_PIN              GET_PIN(F, 11)
  #define KEY_PIN                GET_PIN(C, 1)

  #define WORKER_STACK_SIZE      1024
  #define WORKER_PRIORITY        16
  #define WORKER_TIMESLICE       5

  #define MONITOR_STACK_SIZE     1024
  #define MONITOR_PRIORITY       10
  #define MONITOR_TIMESLICE      5

  static rt_thread_t th_led1_handle = RT_NULL;
  static rt_thread_t th_monitor_handle = RT_NULL;

  /* th_led2 使用静态 TCB 和静态栈 */
  static struct rt_thread th_led2_tcb;

  ALIGN(RT_ALIGN_SIZE)
  static rt_uint8_t th_led2_stack[WORKER_STACK_SIZE];

  /* 按键和 th_led1 控制标志 */
  static volatile rt_bool_t key_event = RT_FALSE;
  static volatile rt_bool_t led1_pause_request = RT_FALSE;
  static volatile rt_bool_t led1_is_paused = RT_FALSE;
  static volatile rt_tick_t last_key_tick = 0;

  static const char *thread_state_name(rt_uint8_t stat)
  {
      switch (stat & RT_THREAD_STAT_MASK)
      {
      case RT_THREAD_INIT:
          return "init";
      case RT_THREAD_READY:
          return "ready";
      case RT_THREAD_SUSPEND:
          return "suspend";
      case RT_THREAD_RUNNING:
          return "running";
      case RT_THREAD_CLOSE:
          return "close";
      default:
          return "unknown";
      }
  }

  /* Cortex-M 栈向低地址增长，'#' 表示尚未使用的栈空间 */
  static rt_size_t thread_unused_stack(rt_thread_t thread)
  {
      rt_uint8_t *ptr;
      rt_uint8_t *end;

      if (thread == RT_NULL || thread->stack_addr == RT_NULL)
      {
          return 0;
      }

      ptr = (rt_uint8_t *)thread->stack_addr;
      end = ptr + thread->stack_size;

      while (ptr < end && *ptr == '#')
      {
          ptr++;
      }

      return (rt_size_t)(ptr -
              (rt_uint8_t *)thread->stack_addr);
  }

  static void print_thread_info(const char *name,
                                rt_thread_t thread)
  {
      if (thread == RT_NULL)
      {
          rt_kprintf("[monitor] %s does not exist\n", name);
          return;
      }

      rt_kprintf(
          "[monitor] %s: state=%s, unused_stack=%u/%u bytes\n",
          name,
          thread_state_name(thread->stat),
          (unsigned int)thread_unused_stack(thread),
          (unsigned int)thread->stack_size);
  }

  /* PC1 按键下降沿中断，200 ms 软件消抖 */
  static void key_irq_callback(void *args)
  {
      rt_tick_t now = rt_tick_get();

      if ((rt_tick_t)(now - last_key_tick) >=
          RT_TICK_PER_SECOND / 5)
      {
          last_key_tick = now;
          key_event = RT_TRUE;
      }
  }

  static void th_led1_entry(void *parameter)
  {
      rt_uint32_t count = 0;
      rt_base_t level = PIN_HIGH;

      rt_pin_mode(LED_R_PIN, PIN_MODE_OUTPUT);
      rt_pin_write(LED_R_PIN, level);

      while (1)
      {
          /*
           * RT-Thread 4.1.1 要求线程只能挂起自身。
           * 监控线程提出请求，由 th_led1 自己调用 suspend。
           */
          if (led1_pause_request)
          {
              led1_pause_request = RT_FALSE;
              led1_is_paused = RT_TRUE;

              rt_kprintf("[th_led1] suspended by key\n");

              rt_thread_suspend(rt_thread_self());
              rt_schedule();

              led1_is_paused = RT_FALSE;
              rt_kprintf("[th_led1] resumed by key\n");
          }

          level = (level == PIN_HIGH) ?
                  PIN_LOW : PIN_HIGH;
          rt_pin_write(LED_R_PIN, level);

          count++;
          rt_kprintf("[th_led1] count=%u, tick=%u\n",
                     (unsigned int)count,
                     (unsigned int)rt_tick_get());

          rt_thread_mdelay(1000);
      }
  }

  static void th_led2_entry(void *parameter)
  {
      rt_uint32_t count = 0;
      rt_base_t level = PIN_HIGH;

      rt_pin_mode(LED_G_PIN, PIN_MODE_OUTPUT);
      rt_pin_write(LED_G_PIN, level);

      while (1)
      {
          level = (level == PIN_HIGH) ?
                  PIN_LOW : PIN_HIGH;
          rt_pin_write(LED_G_PIN, level);

          count++;
          rt_kprintf("[th_led2] count=%u\n",
                     (unsigned int)count);

          rt_thread_mdelay(1500);
      }
  }

  static void process_key_event(void)
  {
      rt_base_t level;
      rt_bool_t event;

      /* 防止读取和清除标志时被按键中断打断 */
      level = rt_hw_interrupt_disable();
      event = key_event;
      key_event = RT_FALSE;
      rt_hw_interrupt_enable(level);

      if (!event)
      {
          return;
      }

      if (led1_is_paused)
      {
          if (rt_thread_resume(th_led1_handle) == RT_EOK)
          {
              rt_kprintf(
                  "[monitor] resume th_led1 requested\n");
          }
      }
      else if (!led1_pause_request)
      {
          led1_pause_request = RT_TRUE;
          rt_kprintf(
              "[monitor] suspend th_led1 requested\n");
      }
  }

  static void th_monitor_entry(void *parameter)
  {
      while (1)
      {
          rt_thread_mdelay(2000);

          process_key_event();

          rt_kprintf("\n[monitor] tick=%u, control=%s\n",
                     (unsigned int)rt_tick_get(),
                     led1_is_paused ?
                     "paused" : "running");

          print_thread_info("th_led1", th_led1_handle);
          print_thread_info("th_led2", &th_led2_tcb);
          rt_kprintf("\n");
      }
  }

  /* msh 中输入 thread_dump 可随时查看线程 */
  static void thread_dump(void)
  {
      rt_kprintf("\n[thread_dump]\n");

      print_thread_info("th_led1", th_led1_handle);
      print_thread_info("th_led2", &th_led2_tcb);
      print_thread_info("th_monitor", th_monitor_handle);

      rt_kprintf("[control] th_led1=%s\n",
                 led1_is_paused ? "paused" : "running");
  }

  MSH_CMD_EXPORT(thread_dump,
                 show study thread information);

  static int thread_study_init(void)
  {
      rt_err_t result;

      th_led1_handle = rt_thread_create(
          "th_led1",
          th_led1_entry,
          RT_NULL,
          WORKER_STACK_SIZE,
          WORKER_PRIORITY,
          WORKER_TIMESLICE);

      if (th_led1_handle == RT_NULL)
      {
          rt_kprintf(
              "[thread_study] create th_led1 failed\n");
          return -RT_ENOMEM;
      }

      result = rt_thread_init(
          &th_led2_tcb,
          "th_led2",
          th_led2_entry,
          RT_NULL,
          th_led2_stack,
          sizeof(th_led2_stack),
          WORKER_PRIORITY,
          WORKER_TIMESLICE);

      if (result != RT_EOK)
      {
          rt_kprintf(
              "[thread_study] init th_led2 failed\n");
          return result;
      }

      th_monitor_handle = rt_thread_create(
          "th_monitor",
          th_monitor_entry,
          RT_NULL,
          MONITOR_STACK_SIZE,
          MONITOR_PRIORITY,
          MONITOR_TIMESLICE);

      if (th_monitor_handle == RT_NULL)
      {
          rt_kprintf(
              "[thread_study] create th_monitor failed\n");
          return -RT_ENOMEM;
      }

      /* KEY_DOWN：PC1，上拉输入、下降沿中断 */
      rt_pin_mode(KEY_PIN, PIN_MODE_INPUT_PULLUP);

      result = rt_pin_attach_irq(
          KEY_PIN,
          PIN_IRQ_MODE_FALLING,
          key_irq_callback,
          RT_NULL);

      if (result != RT_EOK)
      {
          rt_kprintf(
              "[thread_study] attach key IRQ failed: %d\n",
              result);
          return result;
      }

      result = rt_pin_irq_enable(
          KEY_PIN,
          PIN_IRQ_ENABLE);

      if (result != RT_EOK)
      {
          rt_kprintf(
              "[thread_study] enable key IRQ failed: %d\n",
              result);
          return result;
      }

      rt_thread_startup(th_led1_handle);
      rt_thread_startup(&th_led2_tcb);
      rt_thread_startup(th_monitor_handle);

      rt_kprintf(
          "[thread_study] all threads started\n");

      return RT_EOK;
  }

  INIT_APP_EXPORT(thread_study_init);
