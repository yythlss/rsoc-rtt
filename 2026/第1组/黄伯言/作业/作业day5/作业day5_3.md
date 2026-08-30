在前面的代码的基础上，完善 MQTT 订阅回调函数

修改项目一的 `mqtt_sub_callback` 函数，解析 JSON 指令并控制 LED。

```
#include <rtdevice.h>
#define LED_PIN GET_PIN(F, 9) // 改成你板子上LED对应的引脚
static void mqtt_sub_callback(mqtt_client_t client, message_data_t *msg)
{
 char *payload = (char *)msg->message->payload;
 cJSON *root = cJSON_Parse(payload);
 if (root == RT_NULL) return;
 ```
cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
if (cmd != RT_NULL && strcmp(cmd->valuestring, "led_toggle") == 0)
{
    rt_pin_write(LED_PIN, !rt_pin_read(LED_PIN));
    rt_kprintf("LED toggled by cloud command!\n");
}
cJSON_Delete(root);
}
```



**同时需要做的操作**：

1. 在 `main.c` 或 `board.c` 中初始化 LED 引脚为输出模式：`rt_pin_mode(LED_PIN, PIN_MODE_OUTPUT);`
