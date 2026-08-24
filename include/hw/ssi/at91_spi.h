/*
 * Microchip AT91 FLEXCOM SPI
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SSI_AT91_SPI_H
#define HW_SSI_AT91_SPI_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_SPI "at91-spi"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SPIState, AT91_SPI)

#define AT91_SPI_FIFO_SIZE 16
#define AT91_SPI_NUM_CS     4

typedef enum AT91SPIPhase {
    AT91_SPI_PHASE_IDLE,
    AT91_SPI_PHASE_DLYBCS,
    AT91_SPI_PHASE_DLYBS,
    AT91_SPI_PHASE_TRANSFER,
    AT91_SPI_PHASE_DLYBCT,
} AT91SPIPhase;

struct AT91SPIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    Clock *pclk;
    Clock *gclk;
    SSIBus *bus;
    qemu_irq irq;
    qemu_irq tx_request;
    qemu_irq rx_request;
    qemu_irq cs[AT91_SPI_NUM_CS];
    QEMUTimer *transfer_timer;

    uint32_t mode;
    uint32_t interrupt_mask;
    uint32_t status;
    uint32_t chip_select[AT91_SPI_NUM_CS];
    uint32_t fifo_mode;
    uint32_t comparison;
    uint32_t crc;
    uint32_t two_pin_mode;
    uint32_t two_pin_header;
    uint32_t write_protection;
    uint32_t write_protection_status;

    uint32_t rx_fifo[AT91_SPI_FIFO_SIZE];
    uint32_t tx_fifo[AT91_SPI_FIFO_SIZE];
    uint32_t tx_shift;
    int64_t cs_gap_until_ns;
    uint8_t rx_head;
    uint8_t rx_count;
    uint8_t tx_head;
    uint8_t tx_count;
    uint8_t current_pcs;
    uint8_t phase;

    bool flexcom_enabled;
    bool spi_enabled;
    bool fifo_enabled;
    bool tx_shift_valid;
    bool disable_pending;
    bool comparison_started;
    bool cs_active;
    bool tx_request_level;
    bool rx_request_level;
};

uint64_t at91_spi_flexcom_read(AT91SPIState *s, unsigned int size);
void at91_spi_flexcom_write(AT91SPIState *s, uint64_t value,
                            unsigned int size);

#endif /* HW_SSI_AT91_SPI_H */
