/*
 * Microchip AT91 SDHCI wrapper
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SD_AT91_SDHCI_H
#define HW_SD_AT91_SDHCI_H

#include "hw/core/clock.h"
#include "hw/sd/sdhci.h"
#include "qom/object.h"

#define TYPE_AT91_SDHCI "at91-sdhci"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SDHCIState, AT91_SDHCI)

struct AT91SDHCIState {
    SysBusDevice parent_obj;

    MemoryRegion container;
    MemoryRegion caps_iomem;
    MemoryRegion preset_iomem;
    MemoryRegion vendor_iomem;
    BusState *bus;

    SDHCIState sdhci;
    Clock *hclock;
    Clock *gclock;

    uint64_t capareg;
    uint64_t maxcurr;
    uint16_t pvr[3];
    uint8_t mc1r;
    uint32_t acr;
    uint32_t cc2r;
    uint32_t cacr;
    uint32_t dbgr;
    uint32_t calcr;
};

#endif /* HW_SD_AT91_SDHCI_H */
