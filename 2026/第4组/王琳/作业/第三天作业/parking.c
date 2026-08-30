/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-22     d0ubleU0x00       the first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <stdlib.h>

#define CAR_NUM 5
#define PARKING_SPACES 3
#define PARKING_TIMEOUT (2*RT_TICK_PER_SECOND)

static rt_sem_t parking_sem=RT_NULL;
static rt_thread_t car_threads[CAR_NUM]={0};  //线程句柄数组，变量类型本质上是指向线程控制块TCB结构体的指针
static rt_bool_t parking_running=RT_FALSE;
static rt_uint8_t activate_car_count=0;   //当前活跃车辆数，用于自动清理
static rt_uint8_t used_spaces=0;

static void clean_parking_resources(void){
    int i;
    for (i=0;i<CAR_NUM;i++){
//        if(car_threads[i]!=RT_NULL){
//            rt_thread_delete(car_threads[i]); 线程已经自动释放
            car_threads[i]=RT_NULL;
//        }
    }

    if(parking_sem!=RT_NULL){
        rt_sem_delete(parking_sem);
        parking_sem=RT_NULL;
    }

    parking_running=RT_FALSE;
    activate_car_count=0;
    used_spaces=0;
    rt_kprintf("parking resources cleaned up.\n");
}

static void car_thread_entry(void *parameter){  //内核只负责调度和运行线程，但它不关心的业务数据是什么。
    //实际上常用void*传递复杂的结构体指针，入口函数内将其强转回原类型
    //void*的本质再议
    int car_id=(int)(rt_int32_t)parameter;
    rt_err_t result;

    rt_enter_critical();
    activate_car_count++;
    rt_exit_critical();

    //1. 申请车位 超时两秒
    result=rt_sem_take(parking_sem,PARKING_TIMEOUT);
    if (result==RT_EOK){
        rt_enter_critical();
        used_spaces++;
        rt_kprintf("car %d entered, used spaces: %d\n",car_id, used_spaces);
        rt_exit_critical();

        rt_thread_mdelay(rand()%2000 + 1000);  //模拟停车时间1-3秒

        rt_enter_critical();
        used_spaces--;
        rt_kprintf("car %d left, used spaces: %d\n",car_id, used_spaces);
        rt_exit_critical();
        rt_sem_release(parking_sem);

    }else if(result == RT_ETIMEOUT){
        rt_kprintf("car %d waiting for a space (timeout)\n", car_id);

        result=rt_sem_take(parking_sem,PARKING_TIMEOUT);  //失败后重试一次
        if(result==RT_EOK){
            rt_enter_critical();
            used_spaces++;
            rt_kprintf("car %d enter (after timeout), used spaces:%d\n", car_id ,used_spaces);
            rt_exit_critical();
            used_spaces--;
            rt_kprintf("car %d left (after timeout),used space: %d \n ",car_id,used_spaces);

            rt_exit_critical();

            rt_sem_release(parking_sem);
        }else{
            rt_kprintf("car %d semaphore take error: %d\n",car_id, result);
        }


    }
    rt_enter_critical();
    activate_car_count--;
    if(activate_car_count==0){
        //最后一个线程退出，清理资源
        rt_exit_critical();
        clean_parking_resources();
    }else{
        rt_exit_critical();
    }

}


//MSH命令入口
static int parking_demo(int argc, char** argv){
    int i;
    char thread_name[8];

    if(parking_running){
        rt_kprintf("parking demo is already running. wait for completion.\n");
        return -RT_EFULL;
    }

    parking_sem=rt_sem_create("park_sem",PARKING_SPACES,RT_IPC_FLAG_FIFO);
    if(parking_sem==RT_NULL){
        rt_kprintf("Failed to create semaphore.\n");
        return -RT_ERROR;
    }

    /* 创建5个车辆线程 */
    for (i = 0; i < CAR_NUM; i++) {
        rt_sprintf(thread_name, "car%d", i + 1);
        car_threads[i] = rt_thread_create(thread_name,
                                          car_thread_entry,
                                          (void *)(rt_int32_t)(i + 1),
                                          1024,
                                          25,
                                          10);
        if (car_threads[i] == RT_NULL) {
            rt_kprintf("Failed to create thread %s\n", thread_name);
            /* 失败时清理已创建资源 */
            clean_parking_resources();
            return -RT_ERROR;
        }
        rt_thread_startup(car_threads[i]); //将线程[i]专为就绪态
    }

    parking_running = RT_TRUE;
    rt_kprintf("Parking demo started with %d cars.\n", CAR_NUM);
    return RT_EOK;
}
MSH_CMD_EXPORT(parking_demo, Parking lot demo with counting semaphore);
