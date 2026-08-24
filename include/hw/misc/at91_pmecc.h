/*
 * Microchip AT91 programmable multibit ECC controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_PMECC_H
#define HW_MISC_AT91_PMECC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_PMECC "at91-pmecc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PMECCState, AT91_PMECC)

struct AT91PMECCState {
    SysBusDevice parent_obj;

    MemoryRegion pmecc_mmio;
    MemoryRegion errloc_mmio;
    qemu_irq irq;
    Clock *pclk;

    uint32_t cfg;
    uint32_t sarea;
    uint32_t saddr;
    uint32_t eaddr;
    uint32_t imr;
    uint32_t isr;
    bool enabled;

    uint32_t errloc_cfg;
    uint32_t errloc_prim;
    uint32_t errloc_len;
    uint32_t errloc_imr;
    uint32_t errloc_isr;
    uint32_t sigma[25];
};

#endif /* HW_MISC_AT91_PMECC_H */
