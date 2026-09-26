/*
 * Microchip SAM9X7 XLCDC display controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_DISPLAY_AT91_XLCDC_H
#define HW_DISPLAY_AT91_XLCDC_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_XLCDC "at91-xlcdc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91XLCDCState, AT91_XLCDC)

#define AT91_XLCDC_MMIO_SIZE    0x4000
#define AT91_XLCDC_NREGS        (AT91_XLCDC_MMIO_SIZE / 4)

struct AT91XLCDCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    qemu_irq irq;
    QEMUTimer *frame_timer;

    uint32_t regs[AT91_XLCDC_NREGS];
    uint32_t sr;
    uint32_t imr;
    uint32_t isr;

    /* frames fetched, and a checksum of the last base-layer frame */
    uint64_t frames;
    uint32_t last_sum;
};

#endif
