/*
 * Microchip AT91 octal/quad serial peripheral interface
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SSI_AT91_OSPI_H
#define HW_SSI_AT91_OSPI_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_AT91_OSPI "at91-ospi"
OBJECT_DECLARE_SIMPLE_TYPE(AT91OSPIState, AT91_OSPI)

struct AT91OSPIState {
    SysBusDevice parent_obj;

    MemoryRegion regs_mmio;
    MemoryRegion memory_mmio;
    qemu_irq irq;
    qemu_irq cs;
    Clock *pclk;
    Clock *gclk;
    SSIBus *spi;

    uint32_t mr;
    uint32_t isr;
    uint32_t imr;
    uint32_t scr;
    uint32_t iar;
    uint32_t wicr;
    uint32_t ifr;
    uint32_t ricr;
    uint32_t smr;
    uint32_t skr;
    uint32_t refresh;
    uint32_t wracnt;
    uint32_t tout;
    uint32_t wpmr;
    uint32_t wpsr;
    uint16_t rdr;

    bool enabled;
    bool cs_asserted;
    bool transfer_active;
    bool transfer_write;
    uint32_t next_addr;
    uint32_t transfer_count;
};

#endif /* HW_SSI_AT91_OSPI_H */
