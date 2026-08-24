/*
 * Microchip AT91 FLEXCOM wrapper
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_FLEXCOM_H
#define HW_MISC_AT91_FLEXCOM_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

typedef struct AT91TWIState AT91TWIState;
typedef struct AT91USARTState AT91USARTState;
typedef struct AT91SPIState AT91SPIState;

#define TYPE_AT91_FLEXCOM "at91-flexcom"
OBJECT_DECLARE_SIMPLE_TYPE(AT91FlexcomState, AT91_FLEXCOM)

#define AT91_FLEXCOM_MODE_NONE   0
#define AT91_FLEXCOM_MODE_USART  1
#define AT91_FLEXCOM_MODE_SPI    2
#define AT91_FLEXCOM_MODE_TWI    3

struct AT91FlexcomState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq usart_enabled;
    qemu_irq spi_enabled;
    qemu_irq twi_enabled;
    qemu_irq tx_request;
    qemu_irq rx_request;
    AT91USARTState *usart;
    AT91SPIState *spi;
    AT91TWIState *twi;
    uint32_t mr;
    uint32_t thr;
    bool usart_irq_level;
    bool spi_irq_level;
    bool twi_irq_level;
    bool usart_tx_request_level;
    bool usart_rx_request_level;
    bool spi_tx_request_level;
    bool spi_rx_request_level;
    bool twi_tx_request_level;
    bool twi_rx_request_level;
};

void at91_flexcom_set_children(AT91FlexcomState *s, AT91USARTState *usart,
                               AT91SPIState *spi, AT91TWIState *twi);

#endif /* HW_MISC_AT91_FLEXCOM_H */
