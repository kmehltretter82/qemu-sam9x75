/*
 * Microchip AT91 Audio Class D Amplifier
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_AUDIO_AT91_CLASSD_H
#define HW_AUDIO_AT91_CLASSD_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_CLASSD "at91-classd"
OBJECT_DECLARE_SIMPLE_TYPE(AT91CLASSDState, AT91_CLASSD)

struct AT91CLASSDState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    Clock *pclk;
    Clock *gclk;
    qemu_irq irq;
    qemu_irq tx_request;
    QEMUTimer *sample_timer;

    uint32_t mr;
    uint32_t intpmr;
    uint32_t thr;
    uint32_t imr;
    uint32_t isr;
    uint32_t wpmr;

    bool data_valid;
    bool conversion_active;
    bool tx_request_level;
};

#endif /* HW_AUDIO_AT91_CLASSD_H */
