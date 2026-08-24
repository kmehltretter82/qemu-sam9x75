/*
 * Microchip AT91 True Random Number Generator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_TRNG_H
#define HW_MISC_AT91_TRNG_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_TRNG "at91-trng"
OBJECT_DECLARE_SIMPLE_TYPE(AT91TRNGState, AT91_TRNG)

struct AT91TRNGState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pclk;
    qemu_irq irq;
    QEMUTimer *generation_timer;

    uint32_t mr;
    uint32_t imr;
    uint32_t isr;
    uint32_t odata;
    uint32_t wpmr;
    uint32_t wpsr;

    bool enabled;
    bool data_valid;
    bool previous_valid;
};

#endif /* HW_MISC_AT91_TRNG_H */
