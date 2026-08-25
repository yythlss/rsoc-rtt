/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-08-25     wangqi       the first version
 */
//框架核心实现
#include "test_dev.h"
#include <stdio.h>
#include <string.h>
#include <rtthread.h>

/* 全局设备链表头 */
static struct test_dev *dev_list = NULL;

int test_dev_register(struct test_dev *dev)
{
    if (dev == NULL || dev->name[0] == '\0')
        return -1;

    /* 检查是否重复注册 */
    if (test_dev_find(dev->name) != NULL) {
        rt_kprintf("[test_dev] '%s' already registered\n", dev->name);
        return -1;
    }

    dev->ref_count = 0;
    dev->next = dev_list;
    dev_list = dev;

    rt_kprintf("[test_dev] registered: %s\n", dev->name);
    return 0;
}

struct test_dev *test_dev_find(const char *name)
{
    struct test_dev *p = dev_list;

    if (name == NULL)
        return NULL;

    while (p != NULL) {
        if (strncmp(p->name, name, TEST_DEV_NAME_MAX) == 0)
            return p;
        p = p->next;
    }
    return NULL;
}

int test_dev_open(struct test_dev *dev, int oflag)
{
    if (dev == NULL) return -1;

    if (dev->ops && dev->ops->open) {
        int ret = dev->ops->open(dev, oflag);
        if (ret != 0) return ret;
    }

    dev->flag = (uint8_t)oflag;
    dev->ref_count++;
    return 0;
}

int test_dev_close(struct test_dev *dev)
{
    if (dev == NULL || dev->ref_count == 0) return -1;

    if (dev->ops && dev->ops->close) {
        int ret = dev->ops->close(dev);
        if (ret != 0) return ret;
    }

    dev->ref_count--;
    return 0;
}

int test_dev_read(struct test_dev *dev, void *buf, uint32_t size)
{
    if (dev == NULL || buf == NULL) return -1;

    if (dev->ops && dev->ops->read)
        return dev->ops->read(dev, buf, size);

    return -1;
}

int test_dev_write(struct test_dev *dev, const void *buf, uint32_t size)
{
    if (dev == NULL || buf == NULL) return -1;

    if (dev->ops && dev->ops->write)
        return dev->ops->write(dev, buf, size);

    return -1;
}

int test_dev_control(struct test_dev *dev, int cmd, void *arg)
{
    if (dev == NULL) return -1;

    if (dev->ops && dev->ops->control)
        return dev->ops->control(dev, cmd, arg);

    return -1;
}

void test_dev_list(void)
{
    struct test_dev *p = dev_list;

    rt_kprintf("=== Registered Devices ===\n");
    while (p != NULL) {
        rt_kprintf("  -> %s (ref=%d)\n", p->name, p->ref_count);
        p = p->next;
    }
    rt_kprintf("==========================\n");
}
