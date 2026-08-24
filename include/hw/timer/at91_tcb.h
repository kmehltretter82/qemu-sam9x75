/*
 * Microchip AT91 Timer Counter Block
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_AT91_TCB_H
#define HW_TIMER_AT91_TCB_H

#include "hw/core/clock.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_TCB "at91-tcb"
OBJECT_DECLARE_SIMPLE_TYPE(AT91TCBState, AT91_TCB)

#define AT91_TCB_NUM_CHANNELS 3

typedef struct AT91TCBChannel {
    AT91TCBState *owner;
    ptimer_state *timer;

    uint32_t cmr;
    uint32_t smmr;
    uint32_t ra;
    uint32_t rb;
    uint32_t rc;
    uint32_t status;
    uint32_t imr;
    uint32_t emr;
    uint32_t ssr;
    bool enabled;
    bool running;
    bool clock_suspended;
} AT91TCBChannel;

struct AT91TCBState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    Clock *pclk;
    Clock *gclk;
    Clock *slck;
    AT91TCBChannel channel[AT91_TCB_NUM_CHANNELS];

    uint32_t bmr;
    uint32_t qimr;
    uint32_t qisr;
    uint32_t wpmr;
};

#endif /* HW_TIMER_AT91_TCB_H */
