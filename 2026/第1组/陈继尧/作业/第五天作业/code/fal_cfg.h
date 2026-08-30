#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#include <rtconfig.h>
#include <board.h>

#define W25Q64_FLASH_DEV_NAME "W25Q64"

extern struct fal_flash_dev w25q64;

#define FAL_FLASH_DEV_TABLE \
{                           \
    &w25q64,                 \
}

#ifdef FAL_PART_HAS_TABLE_CFG
#define FAL_PART_TABLE                                                     \
{                                                                          \
    {FAL_PART_MAGIC_WORD, "font", W25Q64_FLASH_DEV_NAME, 5 * 1024 * 1024, \
     3 * 1024 * 1024, 0},                                                  \
}
#endif

#endif