/*
 * Microchip AT91 Advanced Interrupt Controller (AIC5)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_INTC_AT91_AIC_H
#define HW_INTC_AT91_AIC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_AIC5 "at91-aic5"
OBJECT_DECLARE_SIMPLE_TYPE(AT91AIC5State, AT91_AIC5)

#define AT91_AIC5_NUM_SOURCES       128
#define AT91_AIC5_PRIORITY_LEVELS   8

struct AT91AIC5State {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq fiq;

    uint32_t source_mode[AT91_AIC5_NUM_SOURCES];
    uint32_t source_vector[AT91_AIC5_NUM_SOURCES];
    uint32_t input_level[AT91_AIC5_NUM_SOURCES / 32];
    uint32_t edge_pending[AT91_AIC5_NUM_SOURCES / 32];
    uint32_t enabled[AT91_AIC5_NUM_SOURCES / 32];
    uint32_t fast_forcing[AT91_AIC5_NUM_SOURCES / 32];
    uint32_t source_index_return[AT91_AIC5_NUM_SOURCES / 32];

    uint8_t active_source[AT91_AIC5_PRIORITY_LEVELS];
    uint8_t active_priority[AT91_AIC5_PRIORITY_LEVELS];
    uint8_t stack_depth;
    uint8_t selected_source;
    int16_t protected_source;

    uint32_t spurious_vector;
    uint32_t debug_control;
    uint32_t write_protection_mode;
    uint32_t write_protection_status;
};

#endif /* HW_INTC_AT91_AIC_H */
