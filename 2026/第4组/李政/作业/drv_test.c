#include <rtthread.h>
#include <rtdevice.h>
#include <drv_gpio.h>

#define KEY_UP                      GET_PIN(C, 5)
#define KEY_DOWN                    GET_PIN(C, 1)
#define KEY_LEFT                    GET_PIN(C, 0)
#define KEY_RIGHT                   GET_PIN(C, 4)

static rt_bool_t g_irqKeyEnabled = RT_FALSE;

/************************************************************************
函数名   : keyUpCallback
功能描述:    KEY_UP 中断回调
输入参数:    pArgs -- 回调参数
输出参数:    无
返回值 :    无
************************************************************************/
static void keyUpCallback(void *pArgs)
{
    rt_base_t pinValue = 0;

    RT_UNUSED(pArgs);

    pinValue = rt_pin_read(KEY_UP);
    rt_kprintf("key up! %d\n", (int)pinValue);
}

/************************************************************************
函数名   : keyDownCallback
功能描述:    KEY_DOWN 中断回调
输入参数:    pArgs -- 回调参数
输出参数:    无
返回值 :    无
************************************************************************/
static void keyDownCallback(void *pArgs)
{
    rt_base_t pinValue = 0;

    RT_UNUSED(pArgs);

    pinValue = rt_pin_read(KEY_DOWN);
    rt_kprintf("key down! %d\n", (int)pinValue);
}

/************************************************************************
函数名   : keyLeftCallback
功能描述:    KEY_LEFT 中断回调
输入参数:    pArgs -- 回调参数
输出参数:    无
返回值 :    无
************************************************************************/
static void keyLeftCallback(void *pArgs)
{
    rt_base_t pinValue = 0;

    RT_UNUSED(pArgs);

    pinValue = rt_pin_read(KEY_LEFT);
    rt_kprintf("key left! %d\n", (int)pinValue);
}

/************************************************************************
函数名   : keyRightCallback
功能描述:    KEY_RIGHT 中断回调
输入参数:    pArgs -- 回调参数
输出参数:    无
返回值 :    无
************************************************************************/
static void keyRightCallback(void *pArgs)
{
    rt_base_t pinValue = 0;

    RT_UNUSED(pArgs);

    pinValue = rt_pin_read(KEY_RIGHT);
    rt_kprintf("key right! %d\n", (int)pinValue);
}

/************************************************************************
函数名   : attachKeyIrq
功能描述:    为单个按键配置中断
输入参数:    pin -- 引脚编号
            pCallback -- 中断回调
输出参数:    无
返回值 :    RT_EOK 成功，-RT_ERROR 失败
************************************************************************/
static int attachKeyIrq(rt_base_t pin, void (*pCallback)(void *pArgs))
{
    int result = RT_EOK;
    rt_err_t attachResult = RT_EOK;

    attachResult = rt_pin_attach_irq(pin, PIN_IRQ_MODE_FALLING, pCallback, RT_NULL);
    if (RT_EOK != attachResult)
    {
        rt_kprintf("attach irq failed, pin=%d, err=%d\n", (int)pin, attachResult);
        result = -RT_ERROR;
    }
    else
    {
        attachResult = rt_pin_irq_enable(pin, PIN_IRQ_ENABLE);
        if (RT_EOK != attachResult)
        {
            rt_kprintf("enable irq failed, pin=%d, err=%d\n", (int)pin, attachResult);
            result = -RT_ERROR;
        }
        else
        {
            result = RT_EOK;
        }
    }

    return result;
}

/************************************************************************
函数名   : irqKeyEnable
功能描述:    初始化四个按键并开启外部中断
输入参数:    无
输出参数:    无
返回值 :    0 成功，-1 失败
************************************************************************/
static int irqKeyEnable(void)
{
    int result = 0;

    if (RT_TRUE == g_irqKeyEnabled)
    {
        rt_kprintf("key irq already enabled.\n");
        result = -1;
    }
    else
    {
        rt_pin_mode(KEY_UP, PIN_MODE_INPUT_PULLUP);
        rt_pin_mode(KEY_DOWN, PIN_MODE_INPUT_PULLUP);
        rt_pin_mode(KEY_LEFT, PIN_MODE_INPUT_PULLUP);
        rt_pin_mode(KEY_RIGHT, PIN_MODE_INPUT_PULLUP);

        if (RT_EOK != attachKeyIrq(KEY_UP, keyUpCallback))
        {
            result = -1;
        }
        else if (RT_EOK != attachKeyIrq(KEY_DOWN, keyDownCallback))
        {
            result = -1;
        }
        else if (RT_EOK != attachKeyIrq(KEY_LEFT, keyLeftCallback))
        {
            result = -1;
        }
        else if (RT_EOK != attachKeyIrq(KEY_RIGHT, keyRightCallback))
        {
            result = -1;
        }
        else
        {
            g_irqKeyEnabled = RT_TRUE;
            rt_kprintf("key irq enabled.\n");
            result = 0;
        }
    }

    return result;
}

int irq_key_enable(void)
{
    return irqKeyEnable();
}

MSH_CMD_EXPORT(irq_key_enable, enable key irq);
