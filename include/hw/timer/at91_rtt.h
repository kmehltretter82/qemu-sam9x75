/*
 * Microchip AT91 real-time timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_AT91_RTT_H
#define HW_TIMER_AT91_RTT_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "hw/misc/at91_sysc.h"
#include "qom/object.h"

#define TYPE_AT91_RTT "at91-rtt"
OBJECT_DECLARE_SIMPLE_TYPE(AT91RTTState, AT91_RTT)

struct AT91RTTState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    Clock *slck;
    ptimer_state *prescaler_timer;
    ptimer_state *rtc_timer;
    AT91SYSCWPState *sysc;

    uint32_t mr;
    uint32_t ar;
    uint32_t vr;
    uint32_t sr;
    uint32_t modr;
    uint32_t tsr;
    bool clock_suspended;
};

#endif /* HW_TIMER_AT91_RTT_H */
