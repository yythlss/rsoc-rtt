/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-27     SummerGift   add spi flash port file
 */

#include <rtthread.h>
#include "spi_flash.h"
#include "spi_flash_sfud.h"
#include "drv_spi.h"

#if defined(BSP_USING_SPI_FLASH)

int rt_hw_spi_flash_init(void)
{
    int i;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    rt_hw_spi_device_attach("spi2", "spi20", GPIOB, GPIO_PIN_12);

    /* RSOC: W25Q64 shares SPI2 bus with RW007, probe may fail intermittently.
     * Retry several times before giving up. */
    for (i = 0; i < 5; i++)
    {
        if (RT_NULL != rt_sfud_flash_probe("W25Q64", "spi20"))
        {
            return RT_EOK;
        }
        rt_thread_mdelay(50);
    }

    return -RT_ERROR;
}
INIT_COMPONENT_EXPORT(rt_hw_spi_flash_init);
#endif

