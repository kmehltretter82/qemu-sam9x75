/*
 * Microchip AT91 Secure Hash Algorithm accelerator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_SHA_H
#define HW_MISC_AT91_SHA_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_SHA "at91-sha"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SHAState, AT91_SHA)

#define AT91_SHA_MAX_BLOCK_SIZE 128
#define AT91_SHA_MAX_WORDS      16

struct AT91SHAState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pclk;
    qemu_irq irq;
    qemu_irq tx_request;
    QEMUTimer *processing_timer;

    uint32_t mr;
    uint32_t imr;
    uint32_t isr;
    uint32_t msr;
    uint32_t bcr;
    uint32_t wpmr;
    uint32_t wpsr;
    uint32_t version;

    uint8_t block[AT91_SHA_MAX_BLOCK_SIZE];
    uint8_t queued_block[AT91_SHA_MAX_BLOCK_SIZE];
    uint32_t output[AT91_SHA_MAX_WORDS];
    uint32_t ir0[AT91_SHA_MAX_WORDS];
    uint32_t ir1[AT91_SHA_MAX_WORDS];
    uint32_t check[AT91_SHA_MAX_WORDS];
    uint64_t hash[8];

    uint32_t input_words;
    uint32_t input_bytes;
    uint32_t expected_bytes;
    uint32_t queued_input_words;
    uint32_t queued_input_bytes;
    uint32_t queued_expected_bytes;
    uint32_t check_words;
    uint8_t write_target;
    uint8_t processing_stage;

    bool first_pending;
    bool busy;
    bool current_auto_final;
    bool locked;
    bool output_valid;
    bool awaiting_check;
    bool tx_request_level;
};

#endif /* HW_MISC_AT91_SHA_H */
