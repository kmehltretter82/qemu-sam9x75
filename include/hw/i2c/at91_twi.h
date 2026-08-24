/*
 * Microchip AT91 two-wire interface controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_I2C_AT91_TWI_H
#define HW_I2C_AT91_TWI_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_TWI "at91-twi"
OBJECT_DECLARE_SIMPLE_TYPE(AT91TWIState, AT91_TWI)

#define AT91_TWI_FIFO_SIZE 16

struct AT91TWIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq tx_request;
    qemu_irq rx_request;
    I2CBus *bus;
    Clock *pclk;
    Clock *gclk;
    QEMUTimer *transfer_timer;
    char *bus_name;

    uint32_t mmr;
    uint32_t smr;
    uint32_t iadr;
    uint32_t cwgr;
    uint32_t status;
    uint32_t imr;
    uint32_t rhr;
    uint32_t smbtr;
    uint32_t hsr;
    uint32_t acr;
    uint32_t filtr;
    uint32_t hscwgr;
    uint32_t fmr;
    uint32_t fsr;
    uint32_t fimr;
    uint32_t wpmr;
    uint32_t wpsr;

    uint8_t rx_fifo[AT91_TWI_FIFO_SIZE];
    uint8_t tx_fifo[AT91_TWI_FIFO_SIZE];
    uint8_t tx_shift;
    uint8_t rx_head;
    uint8_t rx_count;
    uint8_t tx_head;
    uint8_t tx_count;

    uint16_t remaining;
    bool flexcom_enabled;
    bool master_enabled;
    bool slave_enabled;
    bool high_speed_enabled;
    bool smbus_enabled;
    bool pec_enabled;
    bool alt_enabled;
    bool fifo_enabled;
    bool bus_active;
    bool read_transfer;
    bool stop_pending;
    bool tx_shift_valid;
    bool rx_shift_pending;
    bool tx_request_level;
    bool rx_request_level;
};

I2CBus *at91_twi_get_bus(AT91TWIState *s);
uint64_t at91_twi_flexcom_read(AT91TWIState *s, unsigned int size);
void at91_twi_flexcom_write(AT91TWIState *s, uint64_t value,
                            unsigned int size);

#endif /* HW_I2C_AT91_TWI_H */
