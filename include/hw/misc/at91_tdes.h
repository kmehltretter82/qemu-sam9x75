/*
 * Microchip AT91 Triple Data Encryption Standard accelerator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_TDES_H
#define HW_MISC_AT91_TDES_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_TDES "at91-tdes"
OBJECT_DECLARE_SIMPLE_TYPE(AT91TDESState, AT91_TDES)

struct AT91TDESState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pclk;
    QEMUTimer *processing_timer;
    qemu_irq irq;
    qemu_irq tx_request;
    qemu_irq rx_request;

    uint32_t mr;
    uint32_t version;
    uint32_t imr;
    uint32_t isr;
    uint32_t key[6];
    uint32_t iv[2];
    uint32_t xtea_rounds;
    uint32_t wpmr;
    uint32_t wpsr;

    uint8_t input[8];
    uint8_t output[8];
    uint8_t input_valid;
    uint8_t key_written;
    uint8_t dma_input_pos;
    uint8_t dma_output_pos;
    uint8_t output_size;

    bool busy;
    bool locked;
    bool output_pending;
    bool private_key_write_once;
    bool reports_read;
    bool tx_request_level;
    bool rx_request_level;
};

#endif /* HW_MISC_AT91_TDES_H */
