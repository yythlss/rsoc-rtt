/* Day5: AHT10 acquisition, Data.txt logging, and a cloud-command entry.
 * Enable RT_USING_I2C and RT_USING_DFS before using the logger command. */
#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>

#define DAY5_LED_PIN       GET_PIN(F, 12)
#define DAY5_FILE_PATH     "/Data.txt"

static rt_uint8_t day5_led_on;

static void day5_set_led(rt_bool_t on)
{
    day5_led_on = on ? 1 : 0;
    /* RT-Spark LED_R is active low. */
    rt_pin_write(DAY5_LED_PIN, day5_led_on ? PIN_LOW : PIN_HIGH);
}

/* This function is the only entry required by an MQTT/HTTP receive callback. */
int day5_handle_cloud_command(const char *command)
{
    if (command == RT_NULL) return -RT_EINVAL;
    if (!rt_strcmp(command, "led:on")) day5_set_led(RT_TRUE);
    else if (!rt_strcmp(command, "led:off")) day5_set_led(RT_FALSE);
    else if (!rt_strcmp(command, "led:toggle")) day5_set_led(!day5_led_on);
    else return -RT_EINVAL;
    rt_kprintf("day5 cloud command accepted: %s\n", command);
    return RT_EOK;
}

#if defined(RT_USING_I2C) && defined(RT_USING_DFS)
#include <drivers/i2c.h>
#include <dfs_posix.h>

static rt_uint32_t day5_sample_count;

#define AHT10_I2C_BUS      "i2c1"
#define AHT10_ADDRESS      0x38

static rt_err_t aht10_read(float *temperature, float *humidity)
{
    struct rt_i2c_bus_device *bus;
    struct rt_i2c_msg msgs[2];
    rt_uint8_t trigger[3] = {0xac, 0x33, 0x00};
    rt_uint8_t data[6];
    rt_uint32_t raw_h, raw_t;

    if (temperature == RT_NULL || humidity == RT_NULL) return -RT_EINVAL;
    bus = (struct rt_i2c_bus_device *)rt_device_find(AHT10_I2C_BUS);
    if (bus == RT_NULL) return -RT_ENOSYS;

    msgs[0].addr = AHT10_ADDRESS; msgs[0].flags = RT_I2C_WR; msgs[0].buf = trigger; msgs[0].len = sizeof(trigger);
    if (rt_i2c_transfer(bus, msgs, 1) != 1) return -RT_ERROR;
    rt_thread_mdelay(80);
    msgs[1].addr = AHT10_ADDRESS; msgs[1].flags = RT_I2C_RD; msgs[1].buf = data; msgs[1].len = sizeof(data);
    if (rt_i2c_transfer(bus, &msgs[1], 1) != 1 || (data[0] & 0x80)) return -RT_ERROR;

    raw_h = ((rt_uint32_t)data[1] << 12) | ((rt_uint32_t)data[2] << 4) | (data[3] >> 4);
    raw_t = ((rt_uint32_t)(data[3] & 0x0f) << 16) | ((rt_uint32_t)data[4] << 8) | data[5];
    *humidity = raw_h * 100.0f / 1048576.0f;
    *temperature = raw_t * 200.0f / 1048576.0f - 50.0f;
    return RT_EOK;
}

static int day5_collect(int argc, char **argv)
{
    float temperature, humidity;
    int fd;
    char line[80];
    int length;

    RT_UNUSED(argc); RT_UNUSED(argv);
    if (aht10_read(&temperature, &humidity) != RT_EOK)
    {
        rt_kprintf("AHT10 read failed: check %s and wiring\n", AHT10_I2C_BUS);
        return -RT_ERROR;
    }
    fd = open(DAY5_FILE_PATH, O_WRONLY | O_CREAT | O_APPEND, 0);
    if (fd < 0) { rt_kprintf("open %s failed: mount DFS first\n", DAY5_FILE_PATH); return -RT_ERROR; }
    day5_sample_count++;
    length = rt_snprintf(line, sizeof(line), "Temp: %.2f; Humi: %.2f; Count: %lu\n",
                         temperature, humidity, (unsigned long)day5_sample_count);
    if (write(fd, line, length) != length) { close(fd); return -RT_ERROR; }
    close(fd);
    rt_kprintf("%s", line);
    return RT_EOK;
}
MSH_CMD_EXPORT(day5_collect, read AHT10 and append to /Data.txt);
#else
static int day5_collect(int argc, char **argv)
{
    RT_UNUSED(argc); RT_UNUSED(argv);
    rt_kprintf("day5_collect needs RT_USING_I2C and RT_USING_DFS. Enable AHT10, DFS/FAL, then rebuild.\n");
    return -RT_ENOSYS;
}
MSH_CMD_EXPORT(day5_collect, configuration required: I2C and DFS);
#endif

static int day5_led(int argc, char **argv)
{
    RT_UNUSED(argc);
    if (argc != 2) { rt_kprintf("usage: day5_led [on|off|toggle]\n"); return -RT_EINVAL; }
    if (!rt_strcmp(argv[1], "on")) return day5_handle_cloud_command("led:on");
    if (!rt_strcmp(argv[1], "off")) return day5_handle_cloud_command("led:off");
    if (!rt_strcmp(argv[1], "toggle")) return day5_handle_cloud_command("led:toggle");
    return -RT_EINVAL;
}
MSH_CMD_EXPORT(day5_led, test cloud command handler: on/off/toggle);

static int day5_init(void)
{
    rt_pin_mode(DAY5_LED_PIN, PIN_MODE_OUTPUT);
    day5_set_led(RT_FALSE);
    rt_kprintf("Day5 ready: use day5_collect and day5_led\n");
    return RT_EOK;
}
INIT_APP_EXPORT(day5_init);
