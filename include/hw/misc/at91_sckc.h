/*
 * Microchip SAM9X7 slow clock controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_SCKC_H
#define HW_MISC_AT91_SCKC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "hw/misc/at91_sysc.h"
#include "qom/object.h"

#define TYPE_AT91_SCKC "at91-sckc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SCKCState, AT91_SCKC)

struct AT91SCKCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    Clock *slow_rc;
    Clock *slow_xtal;
    Clock *md_slck;
    Clock *td_slck;
    AT91SYSCWPState *sysc;
    uint32_t cr;
};

#endif /* HW_MISC_AT91_SCKC_H */
