/*
 * Microchip AT91 PIO3 parallel I/O controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_GPIO_AT91_PIO_H
#define HW_GPIO_AT91_PIO_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_PIO "at91-pio"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PIOState, AT91_PIO)

#define AT91_PIO_NUM_PINS 32

typedef struct AT91PIOFilterTimer {
    AT91PIOState *pio;
    QEMUTimer *timer;
    unsigned int pin;
} AT91PIOFilterTimer;

struct AT91PIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq output[AT91_PIO_NUM_PINS];
    Clock *pclk;
    Clock *slck;

    /* Product/package-dependent reset configuration. */
    uint32_t valid_mask;
    uint32_t reset_pio_mask;
    uint32_t reset_pullup_mask;
    uint32_t reset_pulldown_mask;

    uint32_t pio_status;
    uint32_t output_status;
    uint32_t filter_status;
    uint32_t output_data;
    uint32_t imr;
    uint32_t isr;
    uint32_t multidrive_status;
    uint32_t pullup_enable;
    uint32_t abcdsr[2];
    uint32_t slow_filter_status;
    uint32_t slow_clock_divider;
    uint32_t pulldown_enable;
    uint32_t output_write_status;
    uint32_t additional_mode;
    uint32_t edge_mode;
    uint32_t rise_high_mode;
    uint32_t wpmr;
    uint32_t wpsr;
    uint32_t schmitt;
    uint32_t slew_rate;
    uint32_t driver;

    /* Pin electrical state and the clocked/filter-visible state. */
    uint32_t external_level;
    uint32_t external_mask;
    uint32_t raw_level;
    uint32_t sampled_level;
    uint32_t old_output_level;
    uint32_t old_output_connected;

    uint32_t filter_pending;
    int64_t filter_deadline[AT91_PIO_NUM_PINS];
    AT91PIOFilterTimer filter[AT91_PIO_NUM_PINS];
};

#endif /* HW_GPIO_AT91_PIO_H */
