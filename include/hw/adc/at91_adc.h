/*
 * Microchip SAM9X7 Analog-to-Digital Converter
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ADC_AT91_ADC_H
#define HW_ADC_AT91_ADC_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_ADC "at91-adc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91ADCState, AT91_ADC)

#define AT91_ADC_NUM_CHANNELS 8

struct AT91ADCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pclk;
    Clock *gclk;
    qemu_irq irq;
    qemu_irq rx_request;
    ptimer_state *conversion_timer;
    ptimer_state *trigger_timer;

    uint32_t mr;
    uint32_t seqr1;
    uint32_t chsr;
    uint32_t lcdr;
    uint32_t imr;
    uint32_t isr;
    uint32_t lctmr;
    uint32_t lccwr;
    uint32_t over;
    uint32_t emr;
    uint32_t cwr;
    uint32_t ccr;
    uint32_t cdr[AT91_ADC_NUM_CHANNELS];
    uint32_t acr;
    uint32_t pdr;
    uint32_t tsmr;
    uint32_t xposr;
    uint32_t yposr;
    uint32_t pressr;
    uint32_t trgr;
    uint32_t cvr;
    uint32_t cecr;
    uint32_t tscvr;
    uint32_t wpmr;
    uint32_t wpsr;

    /* Analog pin and reference voltages, in microvolts. */
    uint32_t adci[AT91_ADC_NUM_CHANNELS];
    uint32_t vref;
    uint64_t accumulator[AT91_ADC_NUM_CHANNELS];
    uint16_t sample_count[AT91_ADC_NUM_CHANNELS];
    uint8_t sequence[AT91_ADC_NUM_CHANNELS];
    uint8_t sequence_length;
    uint8_t sequence_index;
    uint8_t current_channel;
    uint8_t compare_count;
    uint16_t sequence_repeats_remaining;

    bool conversion_active;
    bool comparison_storage;
    bool conversion_suspended;
    bool trigger_suspended;
    uint8_t trigger_levels;
    bool irq_level;
    bool rx_request_level;
};

#endif /* HW_ADC_AT91_ADC_H */
