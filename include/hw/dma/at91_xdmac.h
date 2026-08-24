/*
 * Microchip AT91 Extensible DMA Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_DMA_AT91_XDMAC_H
#define HW_DMA_AT91_XDMAC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "system/memory.h"

#define TYPE_AT91_XDMAC "at91-xdmac"
OBJECT_DECLARE_SIMPLE_TYPE(AT91XDMACState, AT91_XDMAC)

#define AT91_XDMAC_NUM_CHANNELS 16
#define AT91_XDMAC_NUM_REQUESTS 51

typedef struct AT91XDMACChannel {
    uint32_t cim;
    uint32_t cis;
    uint32_t csa;
    uint32_t cda;
    uint32_t cnda;
    uint32_t cndc;
    uint32_t cubc;
    uint32_t cbc;
    uint32_t cc;
    uint32_t cds_msp;
    uint32_t csus;
    uint32_t cdus;
    uint32_t initial_ublen;
    uint32_t request_remaining;
    bool enabled;
    bool descriptor_mode;
    bool needs_fetch;
    bool initd;
    bool error_stalled;
} AT91XDMACChannel;

struct AT91XDMACState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    qemu_irq irq;
    Clock *pclk;
    QEMUBH *bh;

    uint32_t gcfg;
    uint32_t gwac;
    uint32_t gim;
    uint32_t grs;
    uint32_t gws;
    uint32_t sw_requests;
    uint64_t request_level;
    AT91XDMACChannel channel[AT91_XDMAC_NUM_CHANNELS];
};

#endif /* HW_DMA_AT91_XDMAC_H */
