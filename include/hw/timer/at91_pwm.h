/*
 * Microchip SAM9X7 PWM controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_AT91_PWM_H
#define HW_TIMER_AT91_PWM_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_PWM "at91-pwm"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PWMState, AT91_PWM)

#define AT91_PWM_NUM_CHANNELS 4

typedef struct AT91PWMChannel {
    AT91PWMState *owner;
    QEMUTimer *timer;
    uint32_t cmr;
    uint32_t cdty;
    uint32_t cprd;
    uint32_t cupd;
    bool enabled;
    bool update_pending;
    bool disable_pending;
    bool output;
    /* Position in the complete cycle, including the center-aligned return. */
    uint64_t phase;
    /* Input clock cycles and fractional input cycle before the next tick. */
    uint32_t subticks;
    uint64_t fraction;
    int64_t last_ns;
} AT91PWMChannel;

struct AT91PWMState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq output[AT91_PWM_NUM_CHANNELS];
    Clock *pclk;
    /* Retained while gated, so fractional progress survives clock restart. */
    uint64_t pclk_period;
    uint32_t mr;
    uint32_t imr;
    uint32_t isr;
    AT91PWMChannel channel[AT91_PWM_NUM_CHANNELS];
};

#endif /* HW_TIMER_AT91_PWM_H */
