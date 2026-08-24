/*
 * Microchip AT91 64-bit Periodic Interval Timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_AT91_PIT64B_H
#define HW_TIMER_AT91_PIT64B_H

#include "hw/core/clock.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_PIT64B "at91-pit64b"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PIT64BState, AT91_PIT64B)

struct AT91PIT64BState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    Clock *pclk;
    Clock *gclk;
    ptimer_state *timer;

    uint32_t mode;
    uint32_t lsb_period;
    uint32_t msb_period;
    uint32_t imr;
    uint32_t isr;
    uint32_t wpmr;
    uint32_t wpsr;
    uint32_t latched_msb;
    uint64_t counter_base;
    uint64_t counter_period;
    int64_t counter_base_ns;
    bool running;
    bool clock_suspended;
    bool counter_epoch;
};

#endif /* HW_TIMER_AT91_PIT64B_H */
