/*
 * Microchip AT91 periodic interval timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_AT91_PIT_H
#define HW_TIMER_AT91_PIT_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "hw/misc/at91_sysc.h"
#include "qom/object.h"

#define TYPE_AT91_PIT "at91-pit"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PITState, AT91_PIT)

struct AT91PITState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    Clock *mck;
    ptimer_state *timer;
    AT91SYSCWPState *sysc;

    uint32_t mode;
    uint16_t picnt;
    bool pits;
    bool running;
    bool clock_suspended;
};

#endif /* HW_TIMER_AT91_PIT_H */
