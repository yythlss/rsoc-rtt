/*
 * 温湿度数据文件存储模块
 *
 * 将采集结果追加写入 /fal/Data.txt，超过容量上限时丢弃最旧行。
 */
#ifndef DATA_STORAGE_H
#define DATA_STORAGE_H

#include <rtthread.h>

rt_err_t dataStorageSessionStart(void);
rt_err_t dataStorageAppendSample(float temp, float humi);
rt_uint32_t dataStorageGetSampleCount(void);

#endif /* DATA_STORAGE_H */
