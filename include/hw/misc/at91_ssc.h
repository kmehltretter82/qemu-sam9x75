/*
 * Microchip AT91 Synchronous Serial Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_SSC_H
#define HW_MISC_AT91_SSC_H

#include "hw/core/sysbus.h"
#include "hw/core/clock.h"
#include "hw/core/ptimer.h"
#include "qom/object.h"

#define TYPE_AT91_SSC "at91-ssc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SSCState, AT91_SSC)

struct AT91SSCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    /* XDMAC hardware request lines, level-driven from TXRDY and RXRDY. */
    qemu_irq tx_request;
    qemu_irq rx_request;
    Clock *pclk;
    Clock *gclk;
    /* Shifts one word out over its real duration rather than instantly. */
    ptimer_state *shifter;

    uint32_t cmr;
    uint32_t rcmr;
    uint32_t rfmr;
    uint32_t tcmr;
    uint32_t tfmr;
    uint32_t rhr;
    uint32_t thr;
    uint32_t rshr;
    uint32_t tshr;
    uint32_t rc0r;
    uint32_t rc1r;
    uint32_t status;
    uint32_t imr;
    uint32_t wpmr;
    uint32_t wpsr;

    bool rx_enabled;
    bool tx_enabled;
    bool thr_full;
    bool rhr_full;
    bool tx_request_level;
    bool rx_request_level;
};

#endif /* HW_MISC_AT91_SSC_H */
