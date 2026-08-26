/*
 * Microchip SAM9X7 USB Device Port High Speed controller
 *
 * Copyright (c) 2026 Karl Mehltretter
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_USB_AT91_UDPHS_H
#define HW_USB_AT91_UDPHS_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "hw/usb/usb.h"
#include "qom/object.h"
#include "qemu/timer.h"
#include "system/memory.h"

#define TYPE_AT91_UDPHS "at91-udphs"
OBJECT_DECLARE_SIMPLE_TYPE(AT91UDPHSState, AT91_UDPHS)

#define TYPE_AT91_UDPHS_GADGET "at91-udphs-gadget"
OBJECT_DECLARE_SIMPLE_TYPE(AT91UDPHSGadgetState, AT91_UDPHS_GADGET)

#define AT91_UDPHS_NUM_ENDPOINTS       7
#define AT91_UDPHS_NUM_DMA_CHANNELS    6
#define AT91_UDPHS_MAX_BANKS           3
#define AT91_UDPHS_MAX_PACKET_SIZE     1024
#define AT91_UDPHS_FIFO_MMIO_SIZE      0x100000
#define AT91_UDPHS_REG_MMIO_SIZE       0x400
#define AT91_UDPHS_GADGET_TRANSFERS    (AT91_UDPHS_NUM_ENDPOINTS * 2)

typedef enum AT91UDPHSBankState {
    AT91_UDPHS_BANK_FREE,
    AT91_UDPHS_BANK_READY_IN,
    AT91_UDPHS_BANK_READY_OUT,
} AT91UDPHSBankState;

typedef struct AT91UDPHSBank {
    uint8_t data[AT91_UDPHS_MAX_PACKET_SIZE];
    uint16_t length;
    uint16_t written;
    uint8_t state;
} AT91UDPHSBank;

typedef struct AT91UDPHSEndpoint {
    uint32_t cfg;
    uint32_t ctl;
    uint32_t sta;
    uint8_t cpu_bank;
    uint8_t usb_bank;
    bool control_dir_in;
    bool status_out_nak;
    uint16_t control_length;
    uint16_t control_transferred;
    AT91UDPHSBank bank[AT91_UDPHS_MAX_BANKS];
} AT91UDPHSEndpoint;

typedef struct AT91UDPHSDMAChannel {
    uint32_t next_descriptor;
    uint32_t address;
    uint32_t control;
    uint32_t status;
    /* 0 in the architectural field means 64 KiB, so retain 17 bits. */
    uint32_t remaining;
    uint8_t irq_pending;
    bool pending_zlp;
} AT91UDPHSDMAChannel;

typedef struct AT91UDPHSGadgetTransfer {
    bool active;
    uint8_t pid;
    uint64_t id;
    uint32_t size;
    uint32_t actual_length;
} AT91UDPHSGadgetTransfer;

struct AT91UDPHSState {
    SysBusDevice parent_obj;

    MemoryRegion fifo_mmio;
    MemoryRegion regs_mmio;
    MemoryRegion *dma_mr;
    AddressSpace dma_as;
    bool dma_as_initialized;
    QEMUBH *dma_bh;

    qemu_irq irq;
    qemu_irq device_mode;
    qemu_irq vbus;
    Clock *pclk;
    Clock *utmi;

    AT91UDPHSGadgetState *gadget;

    uint32_t ctrl;
    uint32_t fnum;
    uint32_t ien;
    uint32_t events;
    uint32_t tst;
    int32_t negotiated_speed;

    AT91UDPHSEndpoint endpoint[AT91_UDPHS_NUM_ENDPOINTS];
    AT91UDPHSDMAChannel dma[AT91_UDPHS_NUM_DMA_CHANNELS];
    uint32_t dma_generation;
    uint8_t dma_pending;
    uint8_t dma_servicing;
};

struct AT91UDPHSGadgetState {
    USBDevice parent_obj;

    AT91UDPHSState *udphs;
    /* Separate OUT and IN state is required for nonzero control endpoints. */
    AT91UDPHSGadgetTransfer transfer[AT91_UDPHS_GADGET_TRANSFERS];
    QEMUTimer *retry_timer;
    uint8_t abort_ioerror;
    uint8_t abort_nodev;
    uint8_t servicing;
};

/* Raw token entry point used by the USBDevice bridge and unit tests. */
void at91_udphs_handle_token(AT91UDPHSState *s, USBPacket *packet);

#endif /* HW_USB_AT91_UDPHS_H */
