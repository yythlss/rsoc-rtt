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

#include <stm32f4xx_hal.h>

/*************************引脚定义**************************/
#define LED_Port        GPIOF
#define LED1_Pin        GPIO_PIN_11
#define LED2_Pin        GPIO_PIN_12
//使用按键btn_down
#define Key_Port        GPIOC
#define Key_Pin         GPIO_PIN_1

/********************* 全局静态线程定义 *********************/
// 静态线程2栈大小、TCB控制块定义
#define TH_LED2_STACK_SIZE    2048
rt_uint8_t th_led2_stack[TH_LED2_STACK_SIZE];
struct rt_thread th_led2_tcb;

/********************* 线程句柄定义 *********************/
rt_thread_t th_led1_handle = RT_NULL;
rt_thread_t th_monitor_handle = RT_NULL;

/********************* 计数变量 *********************/
static rt_uint32_t led1_cnt = 0;
static rt_uint32_t led2_cnt = 0;
static rt_uint8_t key_flag = 0;
static rt_uint8_t task_flag = 0;

void gpio_init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // 使能GPIO时钟
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    // 初始化PF11、PF12为推挽输出（LED）
    GPIO_InitStruct.Pin = LED1_Pin | LED2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(LED_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(LED_Port, LED1_Pin | LED2_Pin, GPIO_PIN_RESET);

    // 初始化PC1为上拉输入（按键）
    GPIO_InitStruct.Pin = Key_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(Key_Port, &GPIO_InitStruct);
}
INIT_BOARD_EXPORT(gpio_init);

// LED 翻转
void led_toggle(uint16_t GPIO_Pin)
{
    HAL_GPIO_TogglePin(LED_Port, GPIO_Pin);
}

rt_bool_t key_scan(void)
{
    if ((HAL_GPIO_ReadPin(Key_Port, Key_Pin) == GPIO_PIN_RESET))
    {
        rt_thread_mdelay(20);
        if(HAL_GPIO_ReadPin(Key_Port, Key_Pin) == GPIO_PIN_RESET)
            return RT_TRUE;
    }

    return RT_FALSE;
}

void th_led1_entry(void *parameter)
{
    while(1)
    {
        if(task_flag) {
            rt_kprintf("Suspend th_led1 success!\n");
            rt_thread_suspend(th_led1_handle);
            rt_schedule();
        }

        led_toggle(LED1_Pin);
        led1_cnt++;
        rt_kprintf("[LED1] Count: %d, SysTick: %d\n", led1_cnt, rt_tick_get());
        rt_thread_mdelay(500);
    }
}


void th_led2_entry(void *parameter)
{
    while(1)
    {
        led_toggle(LED2_Pin);
        led2_cnt++;
        rt_kprintf("[LED2] Count: %d\n", led2_cnt);
        rt_thread_mdelay(500);
    }
}

void th_monitor_entry(void *parameter)
{
    while(1)
    {
        // 按键单次触发：切换LED1线程挂起/恢复状态
        if(key_scan())
        {
            if (key_flag)
            {
                task_flag = 0;
                rt_thread_resume(th_led1_handle);
                rt_kprintf("Resume th_led1 success!\n");
            }
            else
                task_flag = 1;

            key_flag = !key_flag;
        }

        // 打印线程运行状态与剩余栈空间
        rt_kprintf("\n========== Thread Status Info ==========\n");
        rt_kprintf("[LED1 Thread] State: %d\n", th_led1_handle->stat);

        // 静态线程直接读取TCB
        rt_kprintf("[LED2 Thread] State: %d\n", th_led2_tcb.stat);
        rt_kprintf("========================================\n\n");

        rt_thread_mdelay(2000);
    }
}

void thread_test_init(void)
{
    /***** 动态创建 LED1 线程 *****/
    th_led1_handle = rt_thread_create("th_led1",th_led1_entry,RT_NULL,2048,16,5);
    if(th_led1_handle != RT_NULL)
    {
        rt_thread_startup(th_led1_handle);
        rt_kprintf("[Init] th_led1 create success!\n");
    }

    /***** 静态初始化 LED2 线程 *****/
    rt_thread_init(&th_led2_tcb,"th_led2",th_led2_entry,RT_NULL,th_led2_stack,TH_LED2_STACK_SIZE,16,5);
    rt_thread_startup(&th_led2_tcb);
    rt_kprintf("[Init] th_led2 static init success!\n");

    /***** 动态创建监控线程 *****/
    th_monitor_handle = rt_thread_create("th_monitor",
                                         th_monitor_entry,
                                         RT_NULL,
                                         2048,
                                         10,
                                         5);
    if(th_monitor_handle != RT_NULL)
    {
        rt_thread_startup(th_monitor_handle);
        rt_kprintf("[Init] th_monitor create success!\n");
    }
}
INIT_APP_EXPORT(thread_test_init);

void thread_dump_cmd(void)
{
    rt_kprintf("\n===== Custom Thread Dump Info =====\n");
    rt_kprintf("\nLED1 Count: %d\n", led1_cnt);
    rt_kprintf("LED2 Count: %d\n", led2_cnt);
    rt_kprintf("LED1 Suspend State: %s\n", th_led1_handle->stat);
}
// 导出控制台命令
MSH_CMD_EXPORT(thread_dump_cmd, dump custom thread info);

int main(void)
{
    while (1)
    {
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}
