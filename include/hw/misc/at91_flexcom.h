/*
 * Microchip AT91 FLEXCOM wrapper
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_FLEXCOM_H
#define HW_MISC_AT91_FLEXCOM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_FLEXCOM "at91-flexcom"
OBJECT_DECLARE_SIMPLE_TYPE(AT91FlexcomState, AT91_FLEXCOM)

#define AT91_FLEXCOM_MODE_NONE   0
#define AT91_FLEXCOM_MODE_USART  1
#define AT91_FLEXCOM_MODE_SPI    2
#define AT91_FLEXCOM_MODE_TWI    3

struct AT91FlexcomState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq twi_enabled;
    uint32_t mr;
};

#endif /* HW_MISC_AT91_FLEXCOM_H */
