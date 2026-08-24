/*
 * Microchip AT91 boot sequence controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_BSC_H
#define HW_MISC_AT91_BSC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_BSC "at91-bsc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91BSCState, AT91_BSC)

struct AT91BSCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t boot_sequence;
    uint8_t factory_boot_sequence;
};

#endif /* HW_MISC_AT91_BSC_H */
