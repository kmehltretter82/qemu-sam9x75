/*
 * Microchip SAM9X7 multi-port DDR-SDRAM controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_MPDDRC_H
#define HW_MISC_AT91_MPDDRC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_MPDDRC "at91-mpddrc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91MPDDRCState, AT91_MPDDRC)

#define AT91_MPDDRC_NUM_REGS (0xec / sizeof(uint32_t))

struct AT91MPDDRCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t regs[AT91_MPDDRC_NUM_REGS];
    bool key_written[2];
};

#endif /* HW_MISC_AT91_MPDDRC_H */
