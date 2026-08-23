#ifndef DAY5_STORAGE_H
#define DAY5_STORAGE_H

#include <rtthread.h>

int day5_storage_init(void);
int day5_storage_append(float temp, float humi, rt_uint32_t count);

#endif