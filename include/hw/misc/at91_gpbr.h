/*
 * Microchip AT91 general-purpose backup registers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_GPBR_H
#define HW_MISC_AT91_GPBR_H

#include "hw/core/sysbus.h"
#include "hw/misc/at91_rstc.h"
#include "hw/misc/at91_sysc.h"
#include "qom/object.h"

#define TYPE_AT91_GPBR "at91-gpbr"
OBJECT_DECLARE_SIMPLE_TYPE(AT91GPBRState, AT91_GPBR)

#define AT91_GPBR_NUM_REGISTERS 8

struct AT91GPBRState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    AT91SYSCWPState *sysc;
    AT91RSTCState *rstc;

    uint32_t mode;
    uint32_t full_clear;
    uint32_t registers[AT91_GPBR_NUM_REGISTERS];
    bool mode_written;
    bool initialized;
    bool tamper_event_level;
};

#endif /* HW_MISC_AT91_GPBR_H */
