#include <rtthread.h>
#include <stdio.h>
#include <rtdevice.h>
#include <board.h>
#include "aht10.h"
#define DBG_TAG "ath10_entery"
#define DBG_LVL         DBG_LOG
#include <rtdbg.h>
#include "ath10_msg.h"

struct ath10_msg_t
{
    float  temp;
    float humid;
};

extern rt_mailbox_t mqtt_mailbox;



void ath10_entery(void *params) {
    ath10_msg_t *p_msg;   // 改为指针
    aht10_device_t ath10 = aht10_init("i2c3");
    if(NULL == ath10)
    {
        LOG_E("i2c3 not found\r\n");
        return;
    }
    for(;;) {
        p_msg = rt_malloc(sizeof(ath10_msg_t));
        if (p_msg == NULL) {
            LOG_E("malloc failed");
            rt_thread_mdelay(1000);
            continue;
        }
        p_msg->temp = aht10_read_temperature(ath10);
        p_msg->humid = aht10_read_humidity(ath10);
        if (rt_mb_send(mqtt_mailbox, (rt_ubase_t)p_msg) != RT_EOK) {
            LOG_E("mailbox full, drop");
            rt_free(p_msg);
        }
        rt_thread_mdelay(1000);
    }
}
int ath10_init(void)
{
    rt_thread_t ath10_thread = rt_thread_create("AHT10",
                                                ath10_entery,
                                                NULL,
                                                1024,
                                                20,
                                                5);
    if(NULL == ath10_thread)
    {
        return RT_ERROR;
    }

    rt_thread_startup(ath10_thread);
    return RT_EOK;
}

MSH_CMD_EXPORT( ath10_init,ath10_init);
