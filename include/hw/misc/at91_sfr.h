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

#define AT91_SFR_GPIO_EBI_CS          "ebi-cs"
#define AT91_SFR_GPIO_NFD0_ON_D16     "nfd0-on-d16"

#define AT91_SFR_EBI_CS1              0
#define AT91_SFR_EBI_CS2              1
#define AT91_SFR_NUM_EBI_CS           2

struct AT91SFRState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq ebi_cs[AT91_SFR_NUM_EBI_CS];
    qemu_irq nfd0_on_d16;

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

    bool direct_linux_boot;
};

#endif /* HW_MISC_AT91_SFR_H */
