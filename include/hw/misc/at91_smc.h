/*
 * Microchip AT91 static memory controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_SMC_H
#define HW_MISC_AT91_SMC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_SMC "at91-smc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SMCState, AT91_SMC)

#define AT91_SMC_NUM_CS 3

struct AT91SMCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t setup[AT91_SMC_NUM_CS];
    uint32_t pulse[AT91_SMC_NUM_CS];
    uint32_t cycle[AT91_SMC_NUM_CS];
    uint32_t mode[AT91_SMC_NUM_CS];
    uint32_t ocms;
    uint32_t key1;
    uint32_t key2;
    uint32_t srier;
    uint32_t wpmr;
    uint32_t wpsr;
};

#endif /* HW_MISC_AT91_SMC_H */
