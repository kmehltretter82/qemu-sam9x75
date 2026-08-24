/*
 * Microchip AT91 SMC-connected raw NAND flash
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_BLOCK_AT91_NAND_H
#define HW_BLOCK_AT91_NAND_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_NAND "at91-nand"
OBJECT_DECLARE_SIMPLE_TYPE(AT91NANDState, AT91_NAND)

#define AT91_NAND_GPIO_NCE "nce"

#define AT91_NAND_PAGE_SIZE          4096
#define AT91_NAND_OOB_SIZE           256
#define AT91_NAND_PAGE_TOTAL_SIZE    (AT91_NAND_PAGE_SIZE + \
                                      AT91_NAND_OOB_SIZE)
#define AT91_NAND_PAGES_PER_BLOCK    64
#define AT91_NAND_NUM_BLOCKS         2048
#define AT91_NAND_NUM_PAGES          (AT91_NAND_PAGES_PER_BLOCK * \
                                      AT91_NAND_NUM_BLOCKS)
#define AT91_NAND_DATA_SIZE          ((uint64_t)AT91_NAND_PAGE_SIZE * \
                                      AT91_NAND_NUM_PAGES)
#define AT91_NAND_RAW_SIZE           ((uint64_t)AT91_NAND_PAGE_TOTAL_SIZE * \
                                      AT91_NAND_NUM_PAGES)

struct AT91NANDState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    BlockBackend *blk;
    uint8_t **sparse_pages;
    bool raw_backend;
    bool selected;

    uint8_t command;
    uint8_t previous_command;
    uint8_t status;
    uint8_t address[5];
    uint8_t address_len;
    uint8_t feature_address;
    uint8_t features[256][4];
    uint32_t current_page;
    uint32_t data_pos;
    uint32_t data_len;
    uint32_t program_column;
    uint32_t program_pos;
    uint8_t data[AT91_NAND_PAGE_TOTAL_SIZE * 3];
    uint8_t program[AT91_NAND_PAGE_TOTAL_SIZE];
};

#endif /* HW_BLOCK_AT91_NAND_H */
