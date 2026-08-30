/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __AHT10_DATA_H__
#define __AHT10_DATA_H__

/**
 * @brief 通知其他应用模块已经完成一次温湿度采集。
 *
 * @param temperature_x10 温度扩大 10 倍后的整数。
 * @param humidity_x10 湿度扩大 10 倍后的整数。
 */
void aht10_data_notify(int temperature_x10, int humidity_x10);

#endif /* __AHT10_DATA_H__ */
