/*
 * Microchip AT91 watchdog timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_WATCHDOG_AT91_WDT_H
#define HW_WATCHDOG_AT91_WDT_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "hw/misc/at91_sysc.h"
#include "qom/object.h"

#define TYPE_AT91_WDT "at91-wdt"
OBJECT_DECLARE_SIMPLE_TYPE(AT91WDTState, AT91_WDT)

struct AT91WDTState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    Clock *slck;
    ptimer_state *timer;
    ptimer_state *level_timer;
    qemu_irq reset_out;
    AT91SYSCWPState *sysc;

    uint32_t mode;
    uint32_t window;
    uint32_t level;
    uint32_t imr;
    uint32_t isr;
    int64_t cr_guard_deadline;
    bool locked;
    bool running;
    bool level_running;
    bool clock_suspended;
};

#endif /* HW_WATCHDOG_AT91_WDT_H */
