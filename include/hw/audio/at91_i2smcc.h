/*
 * Microchip AT91 I2S Multi-Channel Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_AUDIO_AT91_I2SMCC_H
#define HW_AUDIO_AT91_I2SMCC_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_I2SMCC "at91-i2smcc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91I2SMCCState, AT91_I2SMCC)

#define AT91_I2SMCC_MAX_CHANNELS 8

struct AT91I2SMCCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pclk;
    Clock *gclk;
    qemu_irq irq;
    qemu_irq tx_request;
    qemu_irq rx_request;
    QEMUTimer *word_timer;

    uint32_t mra;
    uint32_t mrb;
    uint32_t sr;
    uint32_t imra;
    uint32_t isra;
    uint32_t imrb;
    uint32_t isrb;
    uint32_t rhr;
    uint32_t thr;
    uint32_t wpmr;
    uint32_t wpsr;
    uint32_t version;

    uint32_t tx_holding[AT91_I2SMCC_MAX_CHANNELS];
    uint32_t tx_previous[AT91_I2SMCC_MAX_CHANNELS];
    uint32_t rx_holding[AT91_I2SMCC_MAX_CHANNELS];
    bool tx_valid[AT91_I2SMCC_MAX_CHANNELS];
    bool rx_valid[AT91_I2SMCC_MAX_CHANNELS];

    uint8_t tx_write_channel;
    uint8_t rx_read_channel;
    uint8_t stream_channel;
    uint8_t tx_discard_mask;
    uint8_t tx_right_underrun_mask;
    bool clocks_enabled;
    bool tx_enabled;
    bool rx_enabled;
    bool tx_request_level;
    bool rx_request_level;
};

#endif /* HW_AUDIO_AT91_I2SMCC_H */
