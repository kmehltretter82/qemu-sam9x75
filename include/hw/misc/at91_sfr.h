/*
 * Microchip SAM9X7 special function registers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_SFR_H
#define HW_MISC_AT91_SFR_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_SFR "at91-sfr"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SFRState, AT91_SFR)

struct AT91SFRState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    uint32_t ccfg_ebicsa;
    uint32_t ohciicr;
    uint32_t ohciisr;
    uint32_t utmihstrim;
    uint32_t utmifstrim;
    uint32_t utmiswap;
    uint32_t ls;
    uint32_t cal1;
    uint32_t wpmr;
    uint32_t pufctl;
    uint32_t pufdis;
    uint32_t pufrucr[2];
    uint32_t pufworucr[2];
    uint32_t flexrams_clkg_dis;
    uint32_t iss_cfg;
    uint32_t tsu_cfg;
    uint32_t remap_mp_ddr;
};

#endif /* HW_MISC_AT91_SFR_H */
