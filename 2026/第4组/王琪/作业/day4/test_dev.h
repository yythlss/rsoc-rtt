/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     wangqi       the first version
 */
#ifndef APPLICATIONS_TEST_DEV_H_
#define APPLICATIONS_TEST_DEV_H_

#include <stdint.h>
#include <stddef.h>

#define TEST_DEV_NAME_MAX   16

/* 设备打开标志 */
#define TEST_DEV_OFLAG_RDONLY  0x01
#define TEST_DEV_OFLAG_WRONLY  0x02
#define TEST_DEV_OFLAG_RDWR    0x03

/* 前向声明 */
struct test_dev;

/* 设备操作接口（类似 rt_device_ops） */
struct test_dev_ops {
    int (*init)   (struct test_dev *dev);
    int (*open)   (struct test_dev *dev, int oflag);
    int (*close)  (struct test_dev *dev);
    int (*read)   (struct test_dev *dev, void *buf, uint32_t size);
    int (*write)  (struct test_dev *dev, const void *buf, uint32_t size);
    int (*control)(struct test_dev *dev, int cmd, void *arg);
};

/* 设备对象（类似 rt_device） */
struct test_dev {
    char    name[TEST_DEV_NAME_MAX];  /* 设备名称 */
    uint8_t flag;                      /* 打开标志 */
    uint8_t ref_count;                 /* 引用计数 */

    const struct test_dev_ops *ops;    /* 操作函数表 */
    void *user_data;                   /* 驱动私有数据 */

    struct test_dev *next;             /* 链表指针 */
};

/* 框架对外 API */
int              test_dev_register(struct test_dev *dev);
struct test_dev *test_dev_find(const char *name);

int test_dev_open  (struct test_dev *dev, int oflag);
int test_dev_close (struct test_dev *dev);
int test_dev_read  (struct test_dev *dev, void *buf, uint32_t size);
int test_dev_write (struct test_dev *dev, const void *buf, uint32_t size);
int test_dev_control(struct test_dev *dev, int cmd, void *arg);

void test_dev_list(void);

#endif /* APPLICATIONS_TEST_DEV_H_ */
