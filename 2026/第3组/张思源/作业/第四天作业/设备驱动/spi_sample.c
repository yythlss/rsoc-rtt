#include <rtthread.h>
#include <rtdevice.h>

#include "drv_spi.h"
#include "drv_gpio.h"

static int spi_attach(void)
{
    return rt_hw_spi_device_attach("spi2", "spi20", GET_PIN(B, 12));
}
INIT_DEVICE_EXPORT(spi_attach);

static int spi_transfer_one_data(void)
{
    rt_err_t ret = RT_EOK;
    struct rt_spi_device * spi20 = (struct rt_spi_device *)rt_device_find("spi20");

    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = 1 * 1000 * 1000;   /* 1M */
    rt_spi_configure(spi20, &cfg);

    rt_uint8_t sendBuff = 0xDA;
    rt_uint8_t recvBuff = 0xF1;
    ret = rt_spi_transfer(spi20, &sendBuff, &recvBuff, 1);
    rt_kprintf("ret = %d\n", ret);       // return actual num

    return ret;
}
MSH_CMD_EXPORT(spi_transfer_one_data, spi_transfer_one_data);

static int spi_send_one_data(void)
{
    rt_err_t ret = RT_EOK;
    struct rt_spi_device * spi20 = (struct rt_spi_device *)rt_device_find("spi20");

    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = 1 * 1000 * 1000;   /* 1M */
    rt_spi_configure(spi20, &cfg);

    rt_uint8_t sendBuff = 0x1A;
    ret = rt_spi_send(spi20, &sendBuff, 1);
    rt_kprintf("ret = %d\n", ret);       // return actual num

    return ret;
}
MSH_CMD_EXPORT(spi_send_one_data, spi_send_one_data);

static int spi_recv_one_data(void)
{
    rt_err_t ret = RT_EOK;
    struct rt_spi_device * spi20 = (struct rt_spi_device *)rt_device_find("spi20");

    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = 1 * 1000 * 1000;   /* 1M */
    rt_spi_configure(spi20, &cfg);

    rt_uint8_t recvBuff = 0x1A;
    ret = rt_spi_recv(spi20, &recvBuff, 1);     // return actual num
    rt_kprintf("ret = %d\n", ret);

    return ret;
}
MSH_CMD_EXPORT(spi_recv_one_data, spi_recv_one_data);

static int spi_send_then_send_data(void)
{
    rt_err_t ret = RT_EOK;
    struct rt_spi_device * spi20 = (struct rt_spi_device *)rt_device_find("spi20");

    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = 1 * 1000 * 1000;   /* 1M */
    rt_spi_configure(spi20, &cfg);

    rt_uint8_t sendBuff1[2] = {0x1A, 0x99};
    rt_uint8_t sendBuff2[2] = {0x12, 0x39};
    ret = rt_spi_send_then_send(spi20, &sendBuff1, 2, &sendBuff2, 2);
    rt_kprintf("ret = %d\n", ret);  // RT_EOK means success

    return ret;
}
MSH_CMD_EXPORT(spi_send_then_send_data, spi_send_then_send_data);

static int spi_send_then_recv_data(void)
{
    rt_err_t ret = RT_EOK;
    struct rt_spi_device * spi20 = (struct rt_spi_device *)rt_device_find("spi20");

    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = 1 * 1000 * 1000;   /* 1M */
    rt_spi_configure(spi20, &cfg);

    rt_uint8_t sendBuff1[2] = {0x1A, 0x99};
    rt_uint8_t sendBuff2[2] = {0x12, 0x39};
    ret = rt_spi_send_then_recv(spi20, &sendBuff1, 2, &sendBuff2, 2);
    rt_kprintf("ret = %d\n", ret);  // RT_EOK means success

    return ret;
}
MSH_CMD_EXPORT(spi_send_then_recv_data, spi_send_then_recv_data);

static int spi_sample1(void)
{
    rt_err_t ret = RT_EOK;
    struct rt_spi_device * spi20 = (struct rt_spi_device *)rt_device_find("spi20");

    struct rt_spi_configuration cfg;
    cfg.data_width = 8;
    cfg.mode = RT_SPI_MASTER | RT_SPI_MODE_0 | RT_SPI_MSB;
    cfg.max_hz = 1 * 1000 * 1000;   /* 1M */
    rt_spi_configure(spi20, &cfg);

    rt_uint8_t sendBuff[4] = {0x01, 0x02, 0x03, 0x04};

    // first time
    rt_spi_send_then_send(spi20, &sendBuff[0], 2, &sendBuff[1], 2);
    rt_thread_mdelay(1000);

    // two time
    rt_spi_send(spi20, &sendBuff[0], 2);
    rt_spi_send(spi20, &sendBuff[2], 2);
    rt_thread_mdelay(1000);

    // two time
    rt_spi_transfer(spi20, &sendBuff, RT_NULL, 4);

    return RT_EOK;
}
MSH_CMD_EXPORT(spi_sample1, spi_sample1);