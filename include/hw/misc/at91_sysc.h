/*
 * Microchip AT91 system-controller write protection
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_SYSC_H
#define HW_MISC_AT91_SYSC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_SYSCWP "at91-sysc-write-protection"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SYSCWPState, AT91_SYSCWP)

struct AT91SYSCWPState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    uint32_t wpmr;
    uint32_t wpsr;
};

/* Return true when the requested class is protected, recording if required. */
bool at91_sysc_write_protected(AT91SYSCWPState *s, hwaddr offset,
                               bool interrupt_register, bool report);

#endif /* HW_MISC_AT91_SYSC_H */
