/*
 * Microchip AT91 Advanced Encryption Standard accelerator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_AES_H
#define HW_MISC_AT91_AES_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_AES "at91-aes"
OBJECT_DECLARE_SIMPLE_TYPE(AT91AESState, AT91_AES)

struct AT91AESState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pclk;
    qemu_irq irq;
    qemu_irq tx_request;
    qemu_irq rx_request;

    uint32_t mr;
    uint32_t version;
    uint32_t imr;
    uint32_t isr;
    uint32_t key[8];
    uint32_t iv[4];
    uint32_t aadlen;
    uint32_t clen;
    uint32_t ghash[4];
    uint32_t tag[4];
    uint32_t gcmh[4];
    uint32_t emr;
    uint32_t bcnt;
    uint32_t tweak[4];
    uint32_t alpha[4];
    uint32_t wpmr;
    uint32_t wpsr;
    uint32_t gcm_aad_done;
    uint32_t gcm_text_done;

    uint8_t input[16];
    uint8_t output[16];
    uint8_t gcm_counter[16];
    uint8_t gcm_j0[16];
    uint16_t input_valid;
    uint8_t key_written;
    uint8_t dma_input_pos;
    uint8_t dma_output_pos;
    uint8_t output_size;

    bool mr_key_seen;
    bool key_complete;
    bool input_ready;
    bool output_pending;
    bool gcm_counter_valid;
    bool tx_request_level;
    bool rx_request_level;
};

#endif /* HW_MISC_AT91_AES_H */
