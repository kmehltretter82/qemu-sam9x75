/*
 * Microchip SAM9X7 USB Device Port High Speed controller
 *
 * Copyright (c) 2026 Karl Mehltretter
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/usb/at91-udphs.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* Global register bank. */
#define UDPHS_CTRL                  0x000
#define UDPHS_FNUM                  0x004
#define UDPHS_IEN                   0x010
#define UDPHS_INTSTA                0x014
#define UDPHS_CLRINT                0x018
#define UDPHS_EPTRST                0x01c
#define UDPHS_TST                   0x0e0

#define UDPHS_CTRL_DEV_ADDR_MASK    0x0000007f
#define UDPHS_CTRL_FADDR_EN         BIT(7)
#define UDPHS_CTRL_EN               BIT(8)
#define UDPHS_CTRL_DETACH           BIT(9)
#define UDPHS_CTRL_REWAKEUP         BIT(10)
#define UDPHS_CTRL_MASK             0x00000fff
#define UDPHS_CTRL_RESET            UDPHS_CTRL_DETACH

#define UDPHS_IEN_MASK              0x7e007ffe
#define UDPHS_IEN_RESET             BIT(4)
#define UDPHS_GLOBAL_EVENT_MASK     0x000000fe
#define UDPHS_INT_HIGH_SPEED        BIT(0)
#define UDPHS_INT_ENDRESET          BIT(4)
#define UDPHS_EPT_INT_SHIFT         8
#define UDPHS_DMA_INT_SHIFT         25
#define UDPHS_EPTRST_MASK           0x7f
#define UDPHS_TST_MASK              0x3f
#define UDPHS_TST_SPEED_MASK        0x3
#define UDPHS_TST_FORCE_HIGH        0x2
#define UDPHS_TST_FORCE_FULL        0x3

/* Endpoint register bank. */
#define UDPHS_EPT_BASE              0x100
#define UDPHS_EPT_STRIDE            0x20
#define UDPHS_EPT_CFG               0x00
#define UDPHS_EPT_CTLENB            0x04
#define UDPHS_EPT_CTLDIS            0x08
#define UDPHS_EPT_CTL               0x0c
#define UDPHS_EPT_SETSTA            0x14
#define UDPHS_EPT_CLRSTA            0x18
#define UDPHS_EPT_STA               0x1c

#define UDPHS_EPT_CFG_SIZE_MASK     0x7
#define UDPHS_EPT_CFG_DIR_IN        BIT(3)
#define UDPHS_EPT_CFG_TYPE_SHIFT    4
#define UDPHS_EPT_CFG_TYPE_MASK     (0x3 << UDPHS_EPT_CFG_TYPE_SHIFT)
#define UDPHS_EPT_CFG_BANK_SHIFT    6
#define UDPHS_EPT_CFG_BANK_MASK     (0x3 << UDPHS_EPT_CFG_BANK_SHIFT)
#define UDPHS_EPT_CFG_MASK          0x800003ff
#define UDPHS_EPT_CFG_WRITABLE      0x000003ff
#define UDPHS_EPT_CFG_MAPPED        BIT(31)

#define UDPHS_EPT_CTL_ENABLE        BIT(0)
#define UDPHS_EPT_CTL_AUTO_VALID    BIT(1)
#define UDPHS_EPT_CTL_MASK          0x8004ffdb
#define UDPHS_EPT_CTL_TXRDY_IE      BIT(11)
#define UDPHS_EPT_CTL_BUSY_IE       BIT(18)
#define UDPHS_EPT_IRQ_STATUS_MASK   ((0x0000ff00 | BIT(31)) & ~BIT(11))

#define UDPHS_EPT_STA_FORCE_STALL   BIT(5)
#define UDPHS_EPT_STA_TOGGLE_MASK   (0x3 << 6)
#define UDPHS_EPT_STA_OVERFLOW      BIT(8)
#define UDPHS_EPT_STA_RXRDY         BIT(9)
#define UDPHS_EPT_STA_TX_COMPLETE   BIT(10)
#define UDPHS_EPT_STA_TXRDY         BIT(11)
#define UDPHS_EPT_STA_RX_SETUP      BIT(12)
#define UDPHS_EPT_STA_STALL_SENT    BIT(13)
#define UDPHS_EPT_STA_NAK_IN        BIT(14)
#define UDPHS_EPT_STA_NAK_OUT       BIT(15)
#define UDPHS_EPT_STA_CURBANK_MASK  (0x3 << 16)
#define UDPHS_EPT_STA_BUSY_MASK     (0x3 << 18)
#define UDPHS_EPT_STA_COUNT_MASK    (0x7ff << 20)
#define UDPHS_EPT_STA_SHORT         BIT(31)
#define UDPHS_EPT_STA_DYNAMIC_MASK  \
    (UDPHS_EPT_STA_RXRDY | UDPHS_EPT_STA_TXRDY | \
     UDPHS_EPT_STA_CURBANK_MASK | UDPHS_EPT_STA_BUSY_MASK | \
     UDPHS_EPT_STA_COUNT_MASK | UDPHS_EPT_STA_SHORT)
#define UDPHS_EPT_STA_RESET         BIT(6)
#define UDPHS_EPT_SETSTA_MASK       0x00000a20
#define UDPHS_EPT_CLRSTA_MASK       0x0000f660

/* Embedded DMA register bank; channel 1 begins at 0x310. */
#define UDPHS_DMA_BASE              0x310
#define UDPHS_DMA_STRIDE            0x10
#define UDPHS_DMA_NXTDSC            0x00
#define UDPHS_DMA_ADDRESS           0x04
#define UDPHS_DMA_CONTROL           0x08
#define UDPHS_DMA_STATUS            0x0c
#define UDPHS_DMA_CONTROL_MASK      0xffff00ff
#define UDPHS_DMA_CH_ENABLE         BIT(0)
#define UDPHS_DMA_LINK              BIT(1)
#define UDPHS_DMA_END_TR_ENABLE     BIT(2)
#define UDPHS_DMA_END_BUF_ENABLE    BIT(3)
#define UDPHS_DMA_CH_ACTIVE         BIT(1)
#define UDPHS_DMA_END_TR_STATUS     BIT(4)
#define UDPHS_DMA_END_BUF_STATUS    BIT(5)
#define UDPHS_DMA_DESC_LOAD_STATUS  BIT(6)
#define UDPHS_DMA_COUNT_MASK        0xffff0000U
#define UDPHS_DMA_IRQ_MASK          0x70
#define UDPHS_DMA_STATUS_MASK       0xffff0073
#define UDPHS_DMA_STATUS_RTC_MASK   0x70

#define UDPHS_SPEED_UNKNOWN         (-1)

static const uint8_t at91_udphs_physical_banks[AT91_UDPHS_NUM_ENDPOINTS] = {
    1, 2, 2, 3, 3, 3, 3,
};

static const uint16_t at91_udphs_physical_size[AT91_UDPHS_NUM_ENDPOINTS] = {
    64, 1024, 1024, 1024, 1024, 1024, 1024,
};

static unsigned at91_udphs_ep_size(const AT91UDPHSEndpoint *ep)
{
    return 8U << (ep->cfg & UDPHS_EPT_CFG_SIZE_MASK);
}

static unsigned at91_udphs_ep_banks(const AT91UDPHSEndpoint *ep)
{
    return (ep->cfg & UDPHS_EPT_CFG_BANK_MASK) >> UDPHS_EPT_CFG_BANK_SHIFT;
}

static bool at91_udphs_ep_is_in(const AT91UDPHSEndpoint *ep)
{
    return ep->cfg & UDPHS_EPT_CFG_DIR_IN;
}

static unsigned at91_udphs_ep_type(const AT91UDPHSEndpoint *ep)
{
    return (ep->cfg & UDPHS_EPT_CFG_TYPE_MASK) >>
           UDPHS_EPT_CFG_TYPE_SHIFT;
}

static bool at91_udphs_ep_is_control(const AT91UDPHSEndpoint *ep)
{
    return at91_udphs_ep_type(ep) == USB_ENDPOINT_XFER_CONTROL;
}

static bool at91_udphs_transfer_is_in(const AT91UDPHSEndpoint *ep)
{
    return at91_udphs_ep_is_control(ep) ?
           ep->control_dir_in : at91_udphs_ep_is_in(ep);
}

static bool at91_udphs_cfg_valid(unsigned index, uint32_t cfg);
static bool at91_udphs_cfg_operational(unsigned index, uint32_t cfg);

static bool at91_udphs_clocked(const AT91UDPHSState *s)
{
    return clock_is_enabled(s->pclk) && clock_is_enabled(s->utmi);
}

static unsigned at91_udphs_ready_banks(const AT91UDPHSEndpoint *ep)
{
    unsigned count = 0;
    unsigned banks = at91_udphs_ep_banks(ep);
    unsigned i;

    for (i = 0; i < banks; i++) {
        if (ep->bank[i].state != AT91_UDPHS_BANK_FREE) {
            count++;
        }
    }
    return count;
}

static int at91_udphs_find_bank(const AT91UDPHSEndpoint *ep,
                                unsigned start, uint8_t state)
{
    unsigned banks = at91_udphs_ep_banks(ep);
    unsigned i;

    for (i = 0; i < banks; i++) {
        unsigned bank = (start + i) % banks;

        if (ep->bank[bank].state == state) {
            return bank;
        }
    }
    return -1;
}

static void at91_udphs_refresh_endpoint(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned banks = at91_udphs_ep_banks(ep);
    unsigned ready;
    unsigned current;
    unsigned length = 0;
    unsigned packet_length = 0;
    bool is_in;
    int bank;

    ep->sta &= ~UDPHS_EPT_STA_DYNAMIC_MASK;
    if (!(ep->cfg & UDPHS_EPT_CFG_MAPPED) || !banks) {
        return;
    }

    ready = at91_udphs_ready_banks(ep);
    current = ep->cpu_bank % banks;
    is_in = at91_udphs_transfer_is_in(ep);
    ep->sta |= current << 16;
    ep->sta |= MIN(ready, 3U) << 18;

    if (ep->bank[current].state == AT91_UDPHS_BANK_READY_IN) {
        ep->sta |= UDPHS_EPT_STA_TXRDY;
    }
    bank = at91_udphs_find_bank(ep, current,
                                AT91_UDPHS_BANK_READY_OUT);
    if (bank >= 0) {
        ep->cpu_bank = bank;
        current = bank;
        ep->sta &= ~UDPHS_EPT_STA_CURBANK_MASK;
        ep->sta |= current << 16;
        ep->sta |= UDPHS_EPT_STA_RXRDY;
        packet_length = ep->bank[current].length;
        length = packet_length - MIN(packet_length,
                                     ep->bank[current].written);
    } else if (at91_udphs_ep_is_control(ep) &&
               (ep->sta & UDPHS_EPT_STA_RX_SETUP)) {
        length = 8 - MIN(ep->bank[0].written, 8);
    } else if (is_in) {
        length = ep->bank[current].written;
    }

    ep->sta |= MIN(length, 0x7ffU) << 20;
    if (!is_in && bank >= 0 &&
        packet_length < at91_udphs_ep_size(ep)) {
        ep->sta |= UDPHS_EPT_STA_SHORT;
    }

    if (at91_udphs_ep_is_control(ep) && ep->control_dir_in) {
        ep->sta &= ~UDPHS_EPT_STA_CURBANK_MASK;
        ep->sta |= BIT(16);
    }
}

static bool at91_udphs_endpoint_pending(const AT91UDPHSEndpoint *ep)
{
    uint32_t pending;
    unsigned banks;
    unsigned ready;

    if (!(ep->ctl & UDPHS_EPT_CTL_ENABLE)) {
        return false;
    }

    pending = ep->sta & ep->ctl & UDPHS_EPT_IRQ_STATUS_MASK;

    /* TXRDY is an interrupt-on-FIFO-available source (active low). */
    if ((ep->ctl & UDPHS_EPT_CTL_TXRDY_IE) &&
        !(ep->sta & UDPHS_EPT_STA_TXRDY)) {
        pending |= UDPHS_EPT_CTL_TXRDY_IE;
    }
    if (ep->ctl & UDPHS_EPT_CTL_BUSY_IE) {
        banks = at91_udphs_ep_banks(ep);
        ready = at91_udphs_ready_banks(ep);
        if (banks && (at91_udphs_transfer_is_in(ep) ?
                      ready == 0 : ready == banks)) {
            pending |= UDPHS_EPT_CTL_BUSY_IE;
        }
    }
    return pending != 0;
}

static bool at91_udphs_dma_pending(const AT91UDPHSDMAChannel *dma)
{
    return dma->irq_pending != 0;
}

static uint32_t at91_udphs_intsta(AT91UDPHSState *s)
{
    uint32_t value = s->events & UDPHS_GLOBAL_EVENT_MASK;
    unsigned i;

    if (s->negotiated_speed == USB_SPEED_HIGH) {
        value |= UDPHS_INT_HIGH_SPEED;
    }
    for (i = 0; i < AT91_UDPHS_NUM_ENDPOINTS; i++) {
        at91_udphs_refresh_endpoint(s, i);
        if ((s->ien & BIT(UDPHS_EPT_INT_SHIFT + i)) &&
            at91_udphs_endpoint_pending(&s->endpoint[i])) {
            value |= BIT(UDPHS_EPT_INT_SHIFT + i);
        }
    }
    for (i = 0; i < AT91_UDPHS_NUM_DMA_CHANNELS; i++) {
        if ((s->ien & BIT(UDPHS_DMA_INT_SHIFT + i)) &&
            at91_udphs_dma_pending(&s->dma[i])) {
            value |= BIT(UDPHS_DMA_INT_SHIFT + i);
        }
    }
    return value;
}

static void at91_udphs_update_irq(AT91UDPHSState *s)
{
    qemu_set_irq(s->irq, clock_is_enabled(s->pclk) &&
                 !!(at91_udphs_intsta(s) & s->ien));
}

static void at91_udphs_reset_endpoint(AT91UDPHSState *s, unsigned index,
                                      bool clear_configuration)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    uint32_t cfg = clear_configuration ? 0 : ep->cfg;
    uint32_t ctl = clear_configuration ?
                   (index == 0 ? UDPHS_EPT_CTL_ENABLE : 0) : ep->ctl;

    memset(ep, 0, sizeof(*ep));
    ep->cfg = cfg;
    ep->ctl = ctl;
    ep->sta = UDPHS_EPT_STA_RESET;
}

static void at91_udphs_sync_usb(AT91UDPHSState *s);

static void at91_udphs_reset_bus(AT91UDPHSState *s, int speed)
{
    unsigned i;

    s->ctrl &= UDPHS_CTRL_EN | UDPHS_CTRL_DETACH;
    s->fnum = 0;
    s->ien = UDPHS_IEN_RESET;
    s->events = UDPHS_INT_ENDRESET;
    s->negotiated_speed = speed;
    for (i = 0; i < AT91_UDPHS_NUM_ENDPOINTS; i++) {
        at91_udphs_reset_endpoint(s, i, true);
    }
    memset(s->dma, 0, sizeof(s->dma));
    at91_udphs_sync_usb(s);
    at91_udphs_update_irq(s);
}

static void at91_udphs_sync_usb_endpoint(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSGadgetState *bridge = s->gadget;
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    USBDevice *dev;
    unsigned size;
    unsigned type;
    int pid;

    if (!bridge) {
        return;
    }
    dev = USB_DEVICE(bridge);
    size = at91_udphs_cfg_operational(index, ep->cfg) ?
           at91_udphs_ep_size(ep) : 0;
    type = at91_udphs_ep_type(ep);

    if (index == 0) {
        usb_ep_set_type(dev, USB_TOKEN_IN, 0, USB_ENDPOINT_XFER_CONTROL);
        usb_ep_set_max_packet_size(dev, USB_TOKEN_IN, 0, size);
        usb_ep_set_halted(dev, USB_TOKEN_IN, 0,
                          ep->sta & UDPHS_EPT_STA_FORCE_STALL);
        return;
    }

    for (pid = USB_TOKEN_OUT; pid != 0; pid =
             (pid == USB_TOKEN_OUT ? USB_TOKEN_IN : 0)) {
        bool selected = type == USB_ENDPOINT_XFER_CONTROL ||
                        ((pid == USB_TOKEN_IN) == at91_udphs_ep_is_in(ep));

        usb_ep_set_type(dev, pid, index,
                        selected && size ? type : USB_ENDPOINT_XFER_INVALID);
        usb_ep_set_max_packet_size(dev, pid, index,
                                   selected ? size : 0);
        usb_ep_set_halted(dev, pid, index,
                          selected && (ep->sta &
                                       UDPHS_EPT_STA_FORCE_STALL));
    }
}

static void at91_udphs_sync_usb(AT91UDPHSState *s)
{
    unsigned i;

    for (i = 0; i < AT91_UDPHS_NUM_ENDPOINTS; i++) {
        at91_udphs_sync_usb_endpoint(s, i);
    }
    if (s->gadget) {
        USBDevice *dev = USB_DEVICE(s->gadget);

        dev->addr = (s->ctrl & UDPHS_CTRL_FADDR_EN) ?
                    (s->ctrl & UDPHS_CTRL_DEV_ADDR_MASK) : 0;
    }
}

static void at91_udphs_set_speedmask(AT91UDPHSState *s)
{
    USBDevice *dev;

    if (!s->gadget) {
        return;
    }
    dev = USB_DEVICE(s->gadget);
    switch (s->tst & UDPHS_TST_SPEED_MASK) {
    case UDPHS_TST_FORCE_HIGH:
        dev->speedmask = USB_SPEED_MASK_HIGH;
        break;
    case UDPHS_TST_FORCE_FULL:
        dev->speedmask = USB_SPEED_MASK_FULL;
        break;
    default:
        dev->speedmask = USB_SPEED_MASK_FULL | USB_SPEED_MASK_HIGH;
        break;
    }
}

static bool at91_udphs_link_enabled(const AT91UDPHSState *s)
{
    return s->gadget && clock_is_enabled(s->utmi) &&
           (s->ctrl & UDPHS_CTRL_EN) && !(s->ctrl & UDPHS_CTRL_DETACH);
}

static void at91_udphs_update_outputs(AT91UDPHSState *s)
{
    USBDevice *dev = s->gadget ? USB_DEVICE(s->gadget) : NULL;
    bool attach = at91_udphs_link_enabled(s);

    qemu_set_irq(s->device_mode, !!(s->ctrl & UDPHS_CTRL_EN));
    qemu_set_irq(s->vbus, s->gadget != NULL);

    if (!dev || attach == dev->attached) {
        return;
    }
    if (attach) {
        Error *local_err = NULL;

        at91_udphs_set_speedmask(s);
        usb_device_attach(dev, &local_err);
        if (local_err) {
            error_report_err(local_err);
        }
    } else {
        usb_device_detach(dev);
    }
}

static void at91_udphs_clock_changed(void *opaque, ClockEvent event)
{
    AT91UDPHSState *s = opaque;

    at91_udphs_update_outputs(s);
    at91_udphs_update_irq(s);
}

static bool at91_udphs_cfg_valid(unsigned index, uint32_t cfg)
{
    unsigned banks = (cfg & UDPHS_EPT_CFG_BANK_MASK) >>
                     UDPHS_EPT_CFG_BANK_SHIFT;
    unsigned size = 8U << (cfg & UDPHS_EPT_CFG_SIZE_MASK);

    return banks != 0 && banks <= at91_udphs_physical_banks[index] &&
           size <= at91_udphs_physical_size[index];
}

/*
 * MAPD only reports whether the requested DPRAM resources fit.  The packet
 * engine separately rejects endpoint modes that the silicon cannot execute.
 */
static bool at91_udphs_cfg_operational(unsigned index, uint32_t cfg)
{
    unsigned type = (cfg & UDPHS_EPT_CFG_TYPE_MASK) >>
                    UDPHS_EPT_CFG_TYPE_SHIFT;
    unsigned size = 8U << (cfg & UDPHS_EPT_CFG_SIZE_MASK);
    unsigned banks = (cfg & UDPHS_EPT_CFG_BANK_MASK) >>
                     UDPHS_EPT_CFG_BANK_SHIFT;

    if (!(cfg & UDPHS_EPT_CFG_MAPPED) ||
        !at91_udphs_cfg_valid(index, cfg)) {
        return false;
    }
    if (type == USB_ENDPOINT_XFER_CONTROL) {
        return banks == 1 && size <= 64;
    }
    if (index == 0) {
        return false;
    }
    if (type == USB_ENDPOINT_XFER_ISOC && banks < 2) {
        return false;
    }
    if (type == USB_ENDPOINT_XFER_BULK && size > 512) {
        return false;
    }
    return size != 1024 || type == USB_ENDPOINT_XFER_ISOC;
}

static void at91_udphs_write_cfg(AT91UDPHSState *s, unsigned index,
                                 uint32_t value)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    uint32_t cfg = value & UDPHS_EPT_CFG_WRITABLE;

    if (at91_udphs_cfg_valid(index, cfg)) {
        cfg |= UDPHS_EPT_CFG_MAPPED;
    }
    if (cfg != ep->cfg) {
        at91_udphs_reset_endpoint(s, index, false);
        ep->cfg = cfg;
    }
    at91_udphs_sync_usb_endpoint(s, index);
    at91_udphs_update_irq(s);
}

static void at91_udphs_free_bank(AT91UDPHSEndpoint *ep, unsigned bank)
{
    ep->bank[bank].state = AT91_UDPHS_BANK_FREE;
    ep->bank[bank].length = 0;
    ep->bank[bank].written = 0;
}

static void at91_udphs_kill_bank(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned banks = at91_udphs_ep_banks(ep);
    unsigned start;
    int bank = -1;
    unsigned i;

    if (!banks) {
        return;
    }
    start = ep->cpu_bank % banks;
    if (ep->bank[start].state == AT91_UDPHS_BANK_FREE) {
        start = (start + banks - 1) % banks;
    }
    /* RXRDY_TXKL kills the newest packet, not the USB-facing oldest one. */
    for (i = 0; i < banks; i++) {
        unsigned candidate = (start + banks - i) % banks;

        if (ep->bank[candidate].state == AT91_UDPHS_BANK_READY_IN) {
            bank = candidate;
            break;
        }
    }

    if (bank >= 0) {
        at91_udphs_free_bank(ep, bank);
        ep->cpu_bank = bank;
    }
    at91_udphs_refresh_endpoint(s, index);
}

static void at91_udphs_mark_tx_ready(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned banks = at91_udphs_ep_banks(ep);
    int next;

    if (!(ep->cfg & UDPHS_EPT_CFG_MAPPED) || !banks ||
        ep->bank[ep->cpu_bank].state != AT91_UDPHS_BANK_FREE) {
        return;
    }

    ep->bank[ep->cpu_bank].length = ep->bank[ep->cpu_bank].written;
    ep->bank[ep->cpu_bank].state = AT91_UDPHS_BANK_READY_IN;
    next = at91_udphs_find_bank(ep, ep->cpu_bank + 1,
                                AT91_UDPHS_BANK_FREE);
    if (next >= 0) {
        ep->cpu_bank = next;
    }
    at91_udphs_refresh_endpoint(s, index);
}

static void at91_udphs_clear_rx_ready(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned banks = at91_udphs_ep_banks(ep);
    unsigned old = ep->cpu_bank;
    int next;

    if (!banks || ep->bank[old].state != AT91_UDPHS_BANK_READY_OUT) {
        return;
    }
    at91_udphs_free_bank(ep, old);
    next = at91_udphs_find_bank(ep, old + 1, AT91_UDPHS_BANK_READY_OUT);
    ep->cpu_bank = next >= 0 ? next : (old + 1) % banks;
    at91_udphs_refresh_endpoint(s, index);
}

static uint64_t at91_udphs_fifo_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    AT91UDPHSState *s = opaque;
    bool update_irq = false;
    uint64_t value = 0;
    unsigned byte;

    if (!clock_is_enabled(s->pclk)) {
        return 0;
    }

    for (byte = 0; byte < size; byte++) {
        hwaddr address = offset + byte;
        unsigned index = address >> 16;
        unsigned within = address & 0xffff;
        AT91UDPHSEndpoint *ep;
        unsigned packet_size;
        unsigned bank;
        unsigned position;

        if (index >= AT91_UDPHS_NUM_ENDPOINTS) {
            continue;
        }
        ep = &s->endpoint[index];
        if (!(ep->cfg & UDPHS_EPT_CFG_MAPPED)) {
            continue;
        }
        packet_size = at91_udphs_ep_size(ep);
        bank = ep->cpu_bank % at91_udphs_ep_banks(ep);
        position = within % packet_size;
        value |= (uint64_t)ep->bank[bank].data[position] << (byte * 8);

        if (ep->bank[bank].state == AT91_UDPHS_BANK_READY_OUT &&
            position < ep->bank[bank].length) {
            ep->bank[bank].written = MIN(ep->bank[bank].written + 1,
                                         ep->bank[bank].length);
            update_irq = true;
            if (!at91_udphs_ep_is_control(ep) &&
                (ep->ctl & UDPHS_EPT_CTL_AUTO_VALID) &&
                ep->bank[bank].written >= ep->bank[bank].length) {
                at91_udphs_clear_rx_ready(s, index);
            }
        } else if (at91_udphs_ep_is_control(ep) &&
                   (ep->sta & UDPHS_EPT_STA_RX_SETUP) && position < 8) {
            ep->bank[0].written = MIN(ep->bank[0].written + 1, 8);
            at91_udphs_refresh_endpoint(s, index);
            update_irq = true;
        }
    }
    if (update_irq) {
        at91_udphs_update_irq(s);
    }
    return value;
}

static void at91_udphs_fifo_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned size)
{
    AT91UDPHSState *s = opaque;
    bool update_irq = false;
    unsigned byte;

    if (!clock_is_enabled(s->pclk)) {
        return;
    }

    for (byte = 0; byte < size; byte++) {
        hwaddr address = offset + byte;
        unsigned index = address >> 16;
        unsigned within = address & 0xffff;
        AT91UDPHSEndpoint *ep;
        AT91UDPHSBank *fifo;
        unsigned packet_size;
        unsigned bank;
        unsigned position;

        if (index >= AT91_UDPHS_NUM_ENDPOINTS) {
            continue;
        }
        ep = &s->endpoint[index];
        if (!(ep->cfg & UDPHS_EPT_CFG_MAPPED)) {
            continue;
        }
        packet_size = at91_udphs_ep_size(ep);
        bank = ep->cpu_bank % at91_udphs_ep_banks(ep);
        position = within % packet_size;
        fifo = &ep->bank[bank];
        if (fifo->state != AT91_UDPHS_BANK_FREE) {
            continue;
        }

        fifo->data[position] = value >> (byte * 8);
        fifo->written = MIN(fifo->written + 1, packet_size);
        fifo->length = fifo->written;
        update_irq = true;

        if (!at91_udphs_ep_is_control(ep) &&
            (ep->ctl & UDPHS_EPT_CTL_AUTO_VALID) &&
            at91_udphs_transfer_is_in(ep) &&
            bank == ep->cpu_bank &&
            fifo->written == packet_size) {
            at91_udphs_mark_tx_ready(s, index);
            update_irq = true;
        }
    }
    if (update_irq) {
        at91_udphs_update_irq(s);
    }
}

static const MemoryRegionOps at91_udphs_fifo_ops = {
    .read = at91_udphs_fifo_read,
    .write = at91_udphs_fifo_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static uint32_t at91_udphs_endpoint_read(AT91UDPHSState *s,
                                         unsigned index, hwaddr reg)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];

    switch (reg) {
    case UDPHS_EPT_CFG:
        return ep->cfg & UDPHS_EPT_CFG_MASK;
    case UDPHS_EPT_CTLENB:
    case UDPHS_EPT_CTLDIS:
    case UDPHS_EPT_SETSTA:
    case UDPHS_EPT_CLRSTA:
        return 0;
    case UDPHS_EPT_CTL:
        return ep->ctl & UDPHS_EPT_CTL_MASK;
    case UDPHS_EPT_STA:
        at91_udphs_refresh_endpoint(s, index);
        return ep->sta;
    default:
        return 0;
    }
}

static void at91_udphs_endpoint_write(AT91UDPHSState *s, unsigned index,
                                      hwaddr reg, uint32_t value)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    uint32_t mask;

    switch (reg) {
    case UDPHS_EPT_CFG:
        at91_udphs_write_cfg(s, index, value);
        return;
    case UDPHS_EPT_CTLENB:
        mask = value & UDPHS_EPT_CTL_MASK;
        if (!(ep->cfg & UDPHS_EPT_CFG_MAPPED)) {
            mask &= ~UDPHS_EPT_CTL_ENABLE;
        }
        ep->ctl |= mask;
        break;
    case UDPHS_EPT_CTLDIS:
        mask = value & UDPHS_EPT_CTL_MASK;
        ep->ctl &= ~mask;
        if (mask & UDPHS_EPT_CTL_ENABLE) {
            at91_udphs_reset_endpoint(s, index, false);
            ep = &s->endpoint[index];
        }
        break;
    case UDPHS_EPT_SETSTA:
        value &= UDPHS_EPT_SETSTA_MASK;
        if (value & UDPHS_EPT_STA_FORCE_STALL) {
            ep->sta |= UDPHS_EPT_STA_FORCE_STALL;
        }
        if (value & UDPHS_EPT_STA_RXRDY) {
            at91_udphs_kill_bank(s, index);
        }
        if (value & UDPHS_EPT_STA_TXRDY) {
            at91_udphs_mark_tx_ready(s, index);
        }
        break;
    case UDPHS_EPT_CLRSTA:
        value &= UDPHS_EPT_CLRSTA_MASK;
        if (value & UDPHS_EPT_STA_FORCE_STALL) {
            ep->sta &= ~UDPHS_EPT_STA_FORCE_STALL;
        }
        if (value & BIT(6)) {
            ep->sta &= ~UDPHS_EPT_STA_TOGGLE_MASK;
        }
        if (value & UDPHS_EPT_STA_RXRDY) {
            at91_udphs_clear_rx_ready(s, index);
        }
        ep->sta &= ~(value & (UDPHS_EPT_STA_TX_COMPLETE |
                              UDPHS_EPT_STA_RX_SETUP |
                              UDPHS_EPT_STA_STALL_SENT |
                              UDPHS_EPT_STA_NAK_IN |
                              UDPHS_EPT_STA_NAK_OUT));
        if ((value & UDPHS_EPT_STA_RX_SETUP) &&
            at91_udphs_ep_is_control(ep)) {
            ep->bank[0].length = 0;
            ep->bank[0].written = 0;
        }
        break;
    case UDPHS_EPT_CTL:
    case UDPHS_EPT_STA:
    default:
        return;
    }

    at91_udphs_refresh_endpoint(s, index);
    at91_udphs_sync_usb_endpoint(s, index);
    at91_udphs_update_irq(s);
}

static bool at91_udphs_dma_enabled(const AT91UDPHSDMAChannel *dma)
{
    return dma->status & UDPHS_DMA_CH_ENABLE;
}

static void at91_udphs_dma_set_count(AT91UDPHSDMAChannel *dma)
{
    uint32_t count = dma->remaining == 0x10000 ? 0 : dma->remaining;

    dma->status &= ~UDPHS_DMA_COUNT_MASK;
    dma->status |= (count & 0xffff) << 16;
}

static void at91_udphs_dma_set_events(AT91UDPHSDMAChannel *dma,
                                      uint32_t events)
{
    events &= UDPHS_DMA_STATUS_RTC_MASK;
    dma->status |= events;
    dma->irq_pending |= events & dma->control & UDPHS_DMA_IRQ_MASK;
}

static void at91_udphs_dma_error(AT91UDPHSState *s, unsigned index,
                                 hwaddr address, const char *operation)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index];

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: DMA channel %u %s failed at 0x%" HWADDR_PRIx "\n",
                  TYPE_AT91_UDPHS, index + 1, operation, address);
    dma->status &= ~(UDPHS_DMA_CH_ENABLE | UDPHS_DMA_CH_ACTIVE);
    /* There is no architectural bus-error flag; terminate safely. */
    at91_udphs_dma_set_events(dma, UDPHS_DMA_END_TR_STATUS);
    dma->pending_zlp = false;
}

static bool at91_udphs_dma_memory_read(AT91UDPHSState *s, unsigned index,
                                       hwaddr address, void *buffer,
                                       size_t length)
{
    MemTxResult result;

    result = address_space_read(&s->dma_as, address,
                                MEMTXATTRS_UNSPECIFIED, buffer, length);
    if (result != MEMTX_OK) {
        at91_udphs_dma_error(s, index, address, "read");
        return false;
    }
    return true;
}

static bool at91_udphs_dma_memory_write(AT91UDPHSState *s, unsigned index,
                                        hwaddr address, const void *buffer,
                                        size_t length)
{
    MemTxResult result;

    result = address_space_write(&s->dma_as, address,
                                 MEMTXATTRS_UNSPECIFIED, buffer, length);
    if (result != MEMTX_OK) {
        at91_udphs_dma_error(s, index, address, "write");
        return false;
    }
    return true;
}

static bool at91_udphs_dma_load_descriptor(AT91UDPHSState *s,
                                           unsigned index)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index];
    hwaddr descriptor = dma->next_descriptor;
    uint8_t data[12];
    uint32_t control;

    if (descriptor & 0xf) {
        at91_udphs_dma_error(s, index, descriptor,
                             "unaligned descriptor load");
        return false;
    }
    if (!at91_udphs_dma_memory_read(s, index, descriptor,
                                    data, sizeof(data))) {
        return false;
    }

    dma->next_descriptor = ldl_le_p(data);
    dma->address = ldl_le_p(data + 4);
    control = ldl_le_p(data + 8) & UDPHS_DMA_CONTROL_MASK;
    dma->control = control & ~UDPHS_DMA_CH_ENABLE;
    dma->remaining = control >> 16;
    if (!dma->remaining) {
        dma->remaining = 0x10000;
    }
    dma->pending_zlp = false;
    dma->status &= ~(UDPHS_DMA_CH_ENABLE | UDPHS_DMA_CH_ACTIVE |
                     UDPHS_DMA_COUNT_MASK);
    if (control & UDPHS_DMA_CH_ENABLE) {
        dma->status |= UDPHS_DMA_CH_ENABLE;
    }
    at91_udphs_dma_set_count(dma);
    at91_udphs_dma_set_events(dma, UDPHS_DMA_DESC_LOAD_STATUS);
    return true;
}

static bool at91_udphs_dma_complete(AT91UDPHSState *s, unsigned index,
                                    uint32_t events)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index];

    at91_udphs_dma_set_events(dma, events);
    dma->status &= ~(UDPHS_DMA_CH_ENABLE | UDPHS_DMA_CH_ACTIVE);
    if (!(dma->control & UDPHS_DMA_LINK)) {
        return false;
    }
    if (!at91_udphs_dma_load_descriptor(s, index)) {
        return false;
    }
    return at91_udphs_dma_enabled(dma);
}

static bool at91_udphs_dma_queue_zlp(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index - 1];
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned banks = at91_udphs_ep_banks(ep);
    int bank;
    int next;

    if (!dma->pending_zlp || !banks) {
        return false;
    }
    bank = at91_udphs_find_bank(ep, ep->cpu_bank,
                                AT91_UDPHS_BANK_FREE);
    if (bank < 0) {
        return false;
    }
    ep->bank[bank].length = 0;
    ep->bank[bank].written = 0;
    ep->bank[bank].state = AT91_UDPHS_BANK_READY_IN;
    next = at91_udphs_find_bank(ep, bank + 1, AT91_UDPHS_BANK_FREE);
    if (next >= 0) {
        ep->cpu_bank = next;
    }
    dma->pending_zlp = false;
    return true;
}

static void at91_udphs_dma_service_in(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index - 1];
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned packet_size = at91_udphs_ep_size(ep);

    at91_udphs_dma_queue_zlp(s, index);
    while (at91_udphs_dma_enabled(dma)) {
        AT91UDPHSBank *fifo;
        unsigned length;
        bool full;
        bool validate;
        bool need_zlp;
        int bank;
        int next;

        bank = at91_udphs_find_bank(ep, ep->cpu_bank,
                                    AT91_UDPHS_BANK_FREE);
        if (bank < 0) {
            break;
        }
        fifo = &ep->bank[bank];
        if (fifo->written) {
            /* Firmware must validate a partially filled PIO bank first. */
            break;
        }
        length = MIN(dma->remaining, packet_size);
        dma->status |= UDPHS_DMA_CH_ACTIVE;
        if (!at91_udphs_dma_memory_read(s, index - 1, dma->address,
                                        fifo->data, length)) {
            break;
        }
        dma->status &= ~UDPHS_DMA_CH_ACTIVE;
        dma->address += length;
        dma->remaining -= length;
        at91_udphs_dma_set_count(dma);

        fifo->length = length;
        fifo->written = length;
        full = length == packet_size;
        validate = (ep->ctl & UDPHS_EPT_CTL_AUTO_VALID) &&
                   (full || ((dma->control & UDPHS_DMA_END_BUF_ENABLE) &&
                             dma->remaining == 0));
        if (validate) {
            fifo->state = AT91_UDPHS_BANK_READY_IN;
            next = at91_udphs_find_bank(ep, bank + 1,
                                        AT91_UDPHS_BANK_FREE);
            if (next >= 0) {
                ep->cpu_bank = next;
            }
        }

        if (dma->remaining == 0) {
            need_zlp = full &&
                       (dma->control & UDPHS_DMA_END_BUF_ENABLE) &&
                       (ep->ctl & UDPHS_EPT_CTL_AUTO_VALID) &&
                       (ep->ctl & UDPHS_EPT_STA_SHORT);
            dma->pending_zlp |= need_zlp;
            if (!at91_udphs_dma_complete(s, index - 1,
                                         UDPHS_DMA_END_BUF_STATUS)) {
                at91_udphs_dma_queue_zlp(s, index);
                break;
            }
        }

        /* Without AUTO_VALID firmware must validate this bank itself. */
        if (!validate) {
            break;
        }
    }
}

static void at91_udphs_dma_service_out(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index - 1];
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned packet_size = at91_udphs_ep_size(ep);

    while (at91_udphs_dma_enabled(dma)) {
        AT91UDPHSBank *fifo;
        unsigned available;
        unsigned length;
        bool packet_done;
        bool short_packet;
        bool end_buffer;
        bool end_transfer;
        bool validate;
        uint32_t events = 0;
        int bank;

        bank = at91_udphs_find_bank(ep, ep->cpu_bank,
                                    AT91_UDPHS_BANK_READY_OUT);
        if (bank < 0) {
            break;
        }
        ep->cpu_bank = bank;
        fifo = &ep->bank[bank];
        available = fifo->length - MIN(fifo->written, fifo->length);
        if (!available) {
            end_transfer = fifo->length < packet_size &&
                           (dma->control & UDPHS_DMA_END_TR_ENABLE);
            validate = ep->ctl & UDPHS_EPT_CTL_AUTO_VALID;

            /* A received ZLP is a real short-packet transfer event. */
            if (validate) {
                at91_udphs_clear_rx_ready(s, index);
            }
            if (end_transfer) {
                if (!at91_udphs_dma_complete(s, index - 1,
                                             UDPHS_DMA_END_TR_STATUS)) {
                    break;
                }
                continue;
            }
            if (!validate) {
                break;
            }
            continue;
        }
        length = MIN(dma->remaining, available);
        dma->status |= UDPHS_DMA_CH_ACTIVE;
        if (!at91_udphs_dma_memory_write(s, index - 1, dma->address,
                                         fifo->data + fifo->written,
                                         length)) {
            break;
        }
        dma->status &= ~UDPHS_DMA_CH_ACTIVE;
        fifo->written += length;
        dma->address += length;
        dma->remaining -= length;
        at91_udphs_dma_set_count(dma);

        packet_done = fifo->written == fifo->length;
        short_packet = fifo->length < packet_size;
        end_buffer = dma->remaining == 0;
        end_transfer = packet_done && short_packet &&
                       (dma->control & UDPHS_DMA_END_TR_ENABLE);
        validate = (ep->ctl & UDPHS_EPT_CTL_AUTO_VALID) &&
                   (packet_done ||
                    (end_buffer &&
                     (dma->control & UDPHS_DMA_END_BUF_ENABLE)));
        if (validate) {
            at91_udphs_clear_rx_ready(s, index);
        }
        if (end_buffer) {
            events |= UDPHS_DMA_END_BUF_STATUS;
        }
        if (end_transfer) {
            events |= UDPHS_DMA_END_TR_STATUS;
        }
        if (events &&
            !at91_udphs_dma_complete(s, index - 1, events)) {
            break;
        }

        /* Firmware owns bank validation when AUTO_VALID is clear. */
        if (!validate && packet_done) {
            break;
        }
    }
}

static void at91_udphs_dma_service(AT91UDPHSState *s, unsigned index)
{
    AT91UDPHSEndpoint *ep;

    if (index == 0 || index >= AT91_UDPHS_NUM_ENDPOINTS) {
        return;
    }
    ep = &s->endpoint[index];
    if (at91_udphs_ep_is_control(ep) ||
        !(ep->cfg & UDPHS_EPT_CFG_MAPPED) ||
        !(ep->ctl & UDPHS_EPT_CTL_ENABLE)) {
        return;
    }
    if (at91_udphs_transfer_is_in(ep)) {
        at91_udphs_dma_service_in(s, index);
    } else {
        at91_udphs_dma_service_out(s, index);
    }
    at91_udphs_refresh_endpoint(s, index);
}

static uint32_t at91_udphs_dma_read(AT91UDPHSState *s, unsigned index,
                                    hwaddr reg)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index];
    uint32_t value;

    switch (reg) {
    case UDPHS_DMA_NXTDSC:
        return dma->next_descriptor;
    case UDPHS_DMA_ADDRESS:
        return dma->address;
    case UDPHS_DMA_CONTROL:
        /* BUFF_LENGTH is write-only. */
        return dma->control & 0xff & ~UDPHS_DMA_CH_ENABLE;
    case UDPHS_DMA_STATUS:
        value = dma->status & UDPHS_DMA_STATUS_MASK;
        dma->status &= ~UDPHS_DMA_STATUS_RTC_MASK;
        dma->irq_pending = 0;
        at91_udphs_update_irq(s);
        return value;
    default:
        return 0;
    }
}

static void at91_udphs_dma_write(AT91UDPHSState *s, unsigned index,
                                 hwaddr reg, uint32_t value)
{
    AT91UDPHSDMAChannel *dma = &s->dma[index];
    uint32_t commands;

    switch (reg) {
    case UDPHS_DMA_NXTDSC:
        if (!at91_udphs_dma_enabled(dma)) {
            dma->next_descriptor = value;
        }
        break;
    case UDPHS_DMA_ADDRESS:
        if (!at91_udphs_dma_enabled(dma)) {
            dma->address = value;
        }
        break;
    case UDPHS_DMA_CONTROL:
        value &= UDPHS_DMA_CONTROL_MASK;
        commands = value & (UDPHS_DMA_CH_ENABLE | UDPHS_DMA_LINK);
        if (!commands) {
            /* Stop-now does not modify the other control fields. */
            dma->control &= ~UDPHS_DMA_LINK;
            dma->status &= ~(UDPHS_DMA_CH_ENABLE |
                             UDPHS_DMA_CH_ACTIVE);
            dma->pending_zlp = false;
            break;
        }

        dma->control = value & ~UDPHS_DMA_CH_ENABLE;
        dma->pending_zlp = false;
        if (value & UDPHS_DMA_CH_ENABLE) {
            dma->status &= ~UDPHS_DMA_STATUS_RTC_MASK;
            dma->irq_pending = 0;
            dma->status |= UDPHS_DMA_CH_ENABLE;
            dma->remaining = value >> 16;
            if (!dma->remaining) {
                dma->remaining = 0x10000;
            }
            at91_udphs_dma_set_count(dma);
        } else {
            dma->status &= ~(UDPHS_DMA_CH_ENABLE |
                             UDPHS_DMA_CH_ACTIVE);
            if (!at91_udphs_dma_load_descriptor(s, index)) {
                break;
            }
        }
        at91_udphs_dma_service(s, index + 1);
        break;
    case UDPHS_DMA_STATUS:
    default:
        return;
    }
    at91_udphs_update_irq(s);
}

static uint64_t at91_udphs_regs_read(void *opaque, hwaddr offset,
                                     unsigned size)
{
    AT91UDPHSState *s = opaque;

    if (!clock_is_enabled(s->pclk)) {
        return 0;
    }

    if (offset >= UDPHS_EPT_BASE &&
        offset < UDPHS_EPT_BASE +
                 AT91_UDPHS_NUM_ENDPOINTS * UDPHS_EPT_STRIDE) {
        unsigned index = (offset - UDPHS_EPT_BASE) / UDPHS_EPT_STRIDE;
        hwaddr reg = (offset - UDPHS_EPT_BASE) % UDPHS_EPT_STRIDE;

        return at91_udphs_endpoint_read(s, index, reg);
    }
    if (offset >= UDPHS_DMA_BASE &&
        offset < UDPHS_DMA_BASE +
                 AT91_UDPHS_NUM_DMA_CHANNELS * UDPHS_DMA_STRIDE) {
        unsigned index = (offset - UDPHS_DMA_BASE) / UDPHS_DMA_STRIDE;
        hwaddr reg = (offset - UDPHS_DMA_BASE) % UDPHS_DMA_STRIDE;

        return at91_udphs_dma_read(s, index, reg);
    }

    switch (offset) {
    case UDPHS_CTRL:
        return s->ctrl & UDPHS_CTRL_MASK;
    case UDPHS_FNUM:
        return s->fnum & 0x80003fff;
    case UDPHS_IEN:
        return s->ien & UDPHS_IEN_MASK;
    case UDPHS_INTSTA:
        return at91_udphs_intsta(s);
    case UDPHS_CLRINT:
    case UDPHS_EPTRST:
        return 0;
    case UDPHS_TST:
        return s->tst & UDPHS_TST_MASK;
    default:
        /* The complete 0x400-byte register aperture is decoded. */
        return 0;
    }
}

static void at91_udphs_disable_reset(AT91UDPHSState *s)
{
    unsigned i;

    s->fnum = 0;
    s->ien = UDPHS_IEN_RESET;
    s->events = 0;
    s->negotiated_speed = UDPHS_SPEED_UNKNOWN;
    for (i = 0; i < AT91_UDPHS_NUM_ENDPOINTS; i++) {
        at91_udphs_reset_endpoint(s, i, true);
    }
    memset(s->dma, 0, sizeof(s->dma));
}

static void at91_udphs_regs_write(void *opaque, hwaddr offset,
                                  uint64_t value64, unsigned size)
{
    AT91UDPHSState *s = opaque;
    uint32_t value = value64;
    unsigned i;

    if (!clock_is_enabled(s->pclk)) {
        return;
    }

    if (offset >= UDPHS_EPT_BASE &&
        offset < UDPHS_EPT_BASE +
                 AT91_UDPHS_NUM_ENDPOINTS * UDPHS_EPT_STRIDE) {
        unsigned index = (offset - UDPHS_EPT_BASE) / UDPHS_EPT_STRIDE;
        hwaddr reg = (offset - UDPHS_EPT_BASE) % UDPHS_EPT_STRIDE;

        at91_udphs_endpoint_write(s, index, reg, value);
        return;
    }
    if (offset >= UDPHS_DMA_BASE &&
        offset < UDPHS_DMA_BASE +
                 AT91_UDPHS_NUM_DMA_CHANNELS * UDPHS_DMA_STRIDE) {
        unsigned index = (offset - UDPHS_DMA_BASE) / UDPHS_DMA_STRIDE;
        hwaddr reg = (offset - UDPHS_DMA_BASE) % UDPHS_DMA_STRIDE;

        at91_udphs_dma_write(s, index, reg, value);
        return;
    }

    switch (offset) {
    case UDPHS_CTRL: {
        uint32_t old = s->ctrl;

        s->ctrl = value & UDPHS_CTRL_MASK;
        if ((old & UDPHS_CTRL_EN) && !(s->ctrl & UDPHS_CTRL_EN)) {
            uint32_t ctrl = s->ctrl;

            at91_udphs_disable_reset(s);
            s->ctrl = ctrl;
        }
        at91_udphs_sync_usb(s);
        at91_udphs_update_outputs(s);
        break;
    }
    case UDPHS_IEN:
        s->ien = value & UDPHS_IEN_MASK;
        break;
    case UDPHS_CLRINT:
        s->events &= ~(value & UDPHS_GLOBAL_EVENT_MASK);
        break;
    case UDPHS_EPTRST:
        value &= UDPHS_EPTRST_MASK;
        for (i = 0; i < AT91_UDPHS_NUM_ENDPOINTS; i++) {
            if (value & BIT(i)) {
                uint32_t toggle = s->endpoint[i].sta &
                                  UDPHS_EPT_STA_TOGGLE_MASK;

                at91_udphs_reset_endpoint(s, i, false);
                s->endpoint[i].sta &= ~UDPHS_EPT_STA_TOGGLE_MASK;
                s->endpoint[i].sta |= toggle;
                at91_udphs_sync_usb_endpoint(s, i);
            }
        }
        break;
    case UDPHS_TST:
        s->tst = value & UDPHS_TST_MASK;
        at91_udphs_set_speedmask(s);
        at91_udphs_update_outputs(s);
        break;
    case UDPHS_FNUM:
    case UDPHS_INTSTA:
    default:
        break;
    }
    at91_udphs_update_irq(s);
}

static const MemoryRegionOps at91_udphs_regs_ops = {
    .read = at91_udphs_regs_read,
    .write = at91_udphs_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_udphs_advance_toggle(AT91UDPHSEndpoint *ep)
{
    ep->sta ^= BIT(6);
}

static void at91_udphs_handle_setup(AT91UDPHSState *s, USBPacket *packet,
                                    unsigned index)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned i;

    if (usb_packet_size(packet) != 8) {
        packet->status = USB_RET_STALL;
        return;
    }

    for (i = 0; i < AT91_UDPHS_MAX_BANKS; i++) {
        at91_udphs_free_bank(ep, i);
    }
    ep->cpu_bank = 0;
    ep->usb_bank = 0;
    ep->sta &= ~(UDPHS_EPT_STA_FORCE_STALL |
                 UDPHS_EPT_STA_TX_COMPLETE |
                 UDPHS_EPT_STA_TXRDY |
                 UDPHS_EPT_STA_RXRDY |
                 UDPHS_EPT_STA_STALL_SENT |
                 UDPHS_EPT_STA_NAK_IN |
                 UDPHS_EPT_STA_NAK_OUT |
                 UDPHS_EPT_STA_SHORT |
                 UDPHS_EPT_STA_TOGGLE_MASK);
    ep->sta |= UDPHS_EPT_STA_RX_SETUP | BIT(6);

    usb_packet_copy(packet, ep->bank[0].data, 8);
    ep->bank[0].length = 8;
    ep->control_dir_in = ep->bank[0].data[0] & USB_DIR_IN;
    ep->control_length = lduw_le_p(&ep->bank[0].data[6]);
    ep->control_transferred = 0;
    ep->status_out_nak = false;
    packet->status = USB_RET_SUCCESS;

    at91_udphs_refresh_endpoint(s, index);
    at91_udphs_sync_usb_endpoint(s, index);
    at91_udphs_update_irq(s);
}

static void at91_udphs_handle_in(AT91UDPHSState *s, USBPacket *packet,
                                 unsigned index)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned banks = at91_udphs_ep_banks(ep);
    size_t host_length = usb_packet_size(packet);
    size_t length;
    int bank;

    if (index) {
        at91_udphs_dma_service(s, index);
    }
    bank = at91_udphs_find_bank(ep, ep->usb_bank,
                                AT91_UDPHS_BANK_READY_IN);
    if (bank < 0) {
        ep->sta |= UDPHS_EPT_STA_NAK_IN;
        packet->status = USB_RET_NAK;
        at91_udphs_update_irq(s);
        return;
    }

    length = ep->bank[bank].length;
    if (length > host_length) {
        usb_packet_copy(packet, ep->bank[bank].data, host_length);
        packet->status = USB_RET_BABBLE;
    } else {
        usb_packet_copy(packet, ep->bank[bank].data, length);
        packet->status = USB_RET_SUCCESS;
    }

    at91_udphs_free_bank(ep, bank);
    ep->usb_bank = (bank + 1) % banks;
    if (ep->bank[ep->cpu_bank % banks].state != AT91_UDPHS_BANK_FREE) {
        ep->cpu_bank = bank;
    }
    ep->sta |= UDPHS_EPT_STA_TX_COMPLETE;
    at91_udphs_advance_toggle(ep);
    if (index) {
        at91_udphs_dma_service(s, index);
    }

    if (at91_udphs_ep_is_control(ep) && ep->control_dir_in &&
        ep->control_length) {
        ep->control_transferred = MIN((unsigned)ep->control_length,
                                      (unsigned)ep->control_transferred +
                                      (unsigned)length);
        if (length < at91_udphs_ep_size(ep) ||
            ep->control_transferred >= ep->control_length) {
            ep->status_out_nak = true;
        }
    }

    at91_udphs_refresh_endpoint(s, index);
    at91_udphs_update_irq(s);
}

static void at91_udphs_handle_out(AT91UDPHSState *s, USBPacket *packet,
                                  unsigned index)
{
    AT91UDPHSEndpoint *ep = &s->endpoint[index];
    unsigned banks = at91_udphs_ep_banks(ep);
    unsigned packet_size = at91_udphs_ep_size(ep);
    size_t host_length = usb_packet_size(packet);
    size_t length = MIN(host_length, (size_t)packet_size);
    int bank;

    /* Figure 72.10 mandates a NAK on the first status-OUT token. */
    if (at91_udphs_ep_is_control(ep) && ep->status_out_nak &&
        host_length == 0) {
        ep->status_out_nak = false;
        ep->sta |= UDPHS_EPT_STA_NAK_OUT;
        packet->status = USB_RET_NAK;
        at91_udphs_update_irq(s);
        return;
    }

    bank = at91_udphs_find_bank(ep, ep->usb_bank,
                                AT91_UDPHS_BANK_FREE);
    if (bank < 0) {
        ep->sta |= UDPHS_EPT_STA_NAK_OUT;
        packet->status = USB_RET_NAK;
        at91_udphs_update_irq(s);
        return;
    }

    usb_packet_copy(packet, ep->bank[bank].data, length);
    ep->bank[bank].length = length;
    ep->bank[bank].written = 0;
    ep->bank[bank].state = AT91_UDPHS_BANK_READY_OUT;
    ep->usb_bank = (bank + 1) % banks;
    at91_udphs_advance_toggle(ep);
    if (index) {
        at91_udphs_dma_service(s, index);
    }

    if (host_length > packet_size) {
        ep->sta |= UDPHS_EPT_STA_OVERFLOW;
        packet->status = USB_RET_BABBLE;
    } else {
        packet->status = USB_RET_SUCCESS;
    }

    if (at91_udphs_ep_is_control(ep) && host_length == 0) {
        ep->control_length = 0;
        ep->control_transferred = 0;
    }
    at91_udphs_refresh_endpoint(s, index);
    at91_udphs_update_irq(s);
}

void at91_udphs_handle_token(AT91UDPHSState *s, USBPacket *packet)
{
    AT91UDPHSEndpoint *ep;
    unsigned index;

    packet->actual_length = 0;
    if (!at91_udphs_link_enabled(s)) {
        packet->status = USB_RET_NODEV;
        return;
    }
    if (!at91_udphs_clocked(s)) {
        packet->status = USB_RET_NAK;
        return;
    }
    if (!packet->ep) {
        packet->status = USB_RET_IOERROR;
        return;
    }

    index = packet->ep->nr;
    if (index >= AT91_UDPHS_NUM_ENDPOINTS) {
        packet->status = USB_RET_STALL;
        return;
    }
    ep = &s->endpoint[index];
    if (!(ep->cfg & UDPHS_EPT_CFG_MAPPED) ||
        !(ep->ctl & UDPHS_EPT_CTL_ENABLE)) {
        packet->status = USB_RET_NAK;
        return;
    }
    if (!at91_udphs_cfg_operational(index, ep->cfg)) {
        packet->status = USB_RET_STALL;
        return;
    }

    if (packet->pid == USB_TOKEN_SETUP) {
        if (!at91_udphs_ep_is_control(ep)) {
            packet->status = USB_RET_STALL;
            return;
        }
        at91_udphs_handle_setup(s, packet, index);
        return;
    }

    if (ep->sta & UDPHS_EPT_STA_FORCE_STALL) {
        ep->sta |= UDPHS_EPT_STA_STALL_SENT;
        packet->status = USB_RET_STALL;
        at91_udphs_update_irq(s);
        return;
    }

    if (!at91_udphs_ep_is_control(ep)) {
        bool token_in = packet->pid == USB_TOKEN_IN;

        if ((packet->pid != USB_TOKEN_IN &&
             packet->pid != USB_TOKEN_OUT) ||
            token_in != at91_udphs_ep_is_in(ep)) {
            packet->status = USB_RET_STALL;
            return;
        }
    }

    switch (packet->pid) {
    case USB_TOKEN_IN:
        at91_udphs_handle_in(s, packet, index);
        break;
    case USB_TOKEN_OUT:
        at91_udphs_handle_out(s, packet, index);
        break;
    default:
        packet->status = USB_RET_IOERROR;
        break;
    }
}

static void at91_udphs_gadget_handle_packet(USBDevice *dev, USBPacket *packet)
{
    AT91UDPHSGadgetState *bridge = AT91_UDPHS_GADGET(dev);

    at91_udphs_handle_token(bridge->udphs, packet);
}

static void at91_udphs_gadget_handle_reset(USBDevice *dev)
{
    AT91UDPHSGadgetState *bridge = AT91_UDPHS_GADGET(dev);

    at91_udphs_reset_bus(bridge->udphs, dev->speed);
    at91_udphs_sync_usb(bridge->udphs);
}

static void at91_udphs_gadget_realize(USBDevice *dev, Error **errp)
{
    AT91UDPHSGadgetState *bridge = AT91_UDPHS_GADGET(dev);
    AT91UDPHSState *s = bridge->udphs;
    Error *local_err = NULL;

    dev->auto_attach = 0;
    dev->speedmask = USB_SPEED_MASK_FULL | USB_SPEED_MASK_HIGH;

    if (!s) {
        error_setg(errp, TYPE_AT91_UDPHS_GADGET
                   ": udphs link is not set");
        return;
    }
    if (s->gadget) {
        error_setg(errp, TYPE_AT91_UDPHS_GADGET
                   ": controller already has a USB bridge");
        return;
    }

    at91_udphs_set_speedmask(s);
    usb_check_attach(dev, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    s->gadget = bridge;
    at91_udphs_sync_usb(s);
    at91_udphs_update_outputs(s);
}

static void at91_udphs_gadget_unrealize(USBDevice *dev)
{
    AT91UDPHSGadgetState *bridge = AT91_UDPHS_GADGET(dev);
    AT91UDPHSState *s = bridge->udphs;

    if (s && s->gadget == bridge) {
        s->gadget = NULL;
        qemu_set_irq(s->vbus, 0);
    }
}

static int at91_udphs_post_load(void *opaque, int version_id)
{
    AT91UDPHSState *s = opaque;
    unsigned i;
    unsigned j;

    s->ctrl &= UDPHS_CTRL_MASK;
    s->fnum &= 0x80003fff;
    s->ien &= UDPHS_IEN_MASK;
    s->events &= UDPHS_GLOBAL_EVENT_MASK;
    s->tst &= UDPHS_TST_MASK;
    if (s->negotiated_speed != UDPHS_SPEED_UNKNOWN &&
        s->negotiated_speed != USB_SPEED_FULL &&
        s->negotiated_speed != USB_SPEED_HIGH) {
        s->negotiated_speed = UDPHS_SPEED_UNKNOWN;
    }

    for (i = 0; i < AT91_UDPHS_NUM_ENDPOINTS; i++) {
        AT91UDPHSEndpoint *ep = &s->endpoint[i];
        unsigned banks;
        unsigned packet_size;

        ep->cfg &= UDPHS_EPT_CFG_MASK;
        if ((ep->cfg & UDPHS_EPT_CFG_MAPPED) &&
            !at91_udphs_cfg_valid(i, ep->cfg)) {
            ep->cfg &= ~UDPHS_EPT_CFG_MAPPED;
        }
        ep->ctl &= UDPHS_EPT_CTL_MASK;
        ep->sta &= 0xffffffe0;
        banks = MAX(at91_udphs_ep_banks(ep), 1U);
        packet_size = (ep->cfg & UDPHS_EPT_CFG_MAPPED) ?
                      at91_udphs_ep_size(ep) :
                      at91_udphs_physical_size[i];
        ep->cpu_bank %= banks;
        ep->usb_bank %= banks;
        ep->control_transferred = MIN(ep->control_transferred,
                                      ep->control_length);
        for (j = 0; j < AT91_UDPHS_MAX_BANKS; j++) {
            AT91UDPHSBank *bank = &ep->bank[j];

            bank->length = MIN(bank->length, packet_size);
            bank->written = MIN(bank->written, packet_size);
            if (bank->state > AT91_UDPHS_BANK_READY_OUT || j >= banks) {
                at91_udphs_free_bank(ep, j);
            }
        }
        at91_udphs_refresh_endpoint(s, i);
    }
    for (i = 0; i < AT91_UDPHS_NUM_DMA_CHANNELS; i++) {
        s->dma[i].control &= UDPHS_DMA_CONTROL_MASK &
                             ~UDPHS_DMA_CH_ENABLE;
        s->dma[i].status &= UDPHS_DMA_STATUS_MASK;
        s->dma[i].irq_pending &= s->dma[i].status &
                                 UDPHS_DMA_STATUS_RTC_MASK;
        if (s->dma[i].remaining > 0x10000) {
            s->dma[i].remaining = 0;
            s->dma[i].status &= ~(UDPHS_DMA_CH_ENABLE |
                                  UDPHS_DMA_CH_ACTIVE);
            s->dma[i].pending_zlp = false;
        }
        if (s->dma[i].remaining) {
            at91_udphs_dma_set_count(&s->dma[i]);
        }
    }

    at91_udphs_set_speedmask(s);
    at91_udphs_sync_usb(s);
    at91_udphs_update_outputs(s);
    at91_udphs_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_at91_udphs_bank = {
    .name = TYPE_AT91_UDPHS "/bank",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT16(length, AT91UDPHSBank),
        VMSTATE_UINT16(written, AT91UDPHSBank),
        VMSTATE_UINT8(state, AT91UDPHSBank),
        VMSTATE_UINT8_ARRAY(data, AT91UDPHSBank,
                            AT91_UDPHS_MAX_PACKET_SIZE),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_at91_udphs_endpoint = {
    .name = TYPE_AT91_UDPHS "/endpoint",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cfg, AT91UDPHSEndpoint),
        VMSTATE_UINT32(ctl, AT91UDPHSEndpoint),
        VMSTATE_UINT32(sta, AT91UDPHSEndpoint),
        VMSTATE_UINT8(cpu_bank, AT91UDPHSEndpoint),
        VMSTATE_UINT8(usb_bank, AT91UDPHSEndpoint),
        VMSTATE_BOOL(control_dir_in, AT91UDPHSEndpoint),
        VMSTATE_BOOL(status_out_nak, AT91UDPHSEndpoint),
        VMSTATE_UINT16(control_length, AT91UDPHSEndpoint),
        VMSTATE_UINT16(control_transferred, AT91UDPHSEndpoint),
        VMSTATE_STRUCT_ARRAY(bank, AT91UDPHSEndpoint,
                             AT91_UDPHS_MAX_BANKS, 1,
                             vmstate_at91_udphs_bank, AT91UDPHSBank),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_at91_udphs_dma = {
    .name = TYPE_AT91_UDPHS "/dma",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(next_descriptor, AT91UDPHSDMAChannel),
        VMSTATE_UINT32(address, AT91UDPHSDMAChannel),
        VMSTATE_UINT32(control, AT91UDPHSDMAChannel),
        VMSTATE_UINT32(status, AT91UDPHSDMAChannel),
        VMSTATE_UINT32(remaining, AT91UDPHSDMAChannel),
        VMSTATE_UINT8(irq_pending, AT91UDPHSDMAChannel),
        VMSTATE_BOOL(pending_zlp, AT91UDPHSDMAChannel),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_at91_udphs = {
    .name = TYPE_AT91_UDPHS,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_udphs_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, AT91UDPHSState),
        VMSTATE_UINT32(fnum, AT91UDPHSState),
        VMSTATE_UINT32(ien, AT91UDPHSState),
        VMSTATE_UINT32(events, AT91UDPHSState),
        VMSTATE_UINT32(tst, AT91UDPHSState),
        VMSTATE_INT32(negotiated_speed, AT91UDPHSState),
        VMSTATE_STRUCT_ARRAY(endpoint, AT91UDPHSState,
                             AT91_UDPHS_NUM_ENDPOINTS, 1,
                             vmstate_at91_udphs_endpoint,
                             AT91UDPHSEndpoint),
        VMSTATE_STRUCT_ARRAY(dma, AT91UDPHSState,
                             AT91_UDPHS_NUM_DMA_CHANNELS, 1,
                             vmstate_at91_udphs_dma,
                             AT91UDPHSDMAChannel),
        VMSTATE_CLOCK(pclk, AT91UDPHSState),
        VMSTATE_CLOCK(utmi, AT91UDPHSState),
        VMSTATE_END_OF_LIST()
    },
};

static int at91_udphs_gadget_post_load(void *opaque, int version_id)
{
    AT91UDPHSGadgetState *bridge = opaque;
    AT91UDPHSState *s = bridge->udphs;

    if (!s || (s->gadget && s->gadget != bridge)) {
        return -EINVAL;
    }
    s->gadget = bridge;
    at91_udphs_set_speedmask(s);
    at91_udphs_sync_usb(s);
    at91_udphs_update_outputs(s);
    return 0;
}

static const VMStateDescription vmstate_at91_udphs_gadget = {
    .name = TYPE_AT91_UDPHS_GADGET,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_udphs_gadget_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(parent_obj, AT91UDPHSGadgetState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_udphs_reset(DeviceState *dev)
{
    AT91UDPHSState *s = AT91_UDPHS(dev);

    at91_udphs_disable_reset(s);
    s->ctrl = UDPHS_CTRL_RESET;
    s->tst = 0;
    s->ien = UDPHS_IEN_RESET;
    s->events = 0;
    s->negotiated_speed = UDPHS_SPEED_UNKNOWN;
    at91_udphs_sync_usb(s);
    at91_udphs_update_outputs(s);
    at91_udphs_update_irq(s);
}

static void at91_udphs_init(Object *obj)
{
    AT91UDPHSState *s = AT91_UDPHS(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->fifo_mmio, obj, &at91_udphs_fifo_ops, s,
                          TYPE_AT91_UDPHS "-fifo",
                          AT91_UDPHS_FIFO_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->fifo_mmio);
    memory_region_init_io(&s->regs_mmio, obj, &at91_udphs_regs_ops, s,
                          TYPE_AT91_UDPHS "-regs",
                          AT91_UDPHS_REG_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->regs_mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->device_mode, "device-mode", 1);
    qdev_init_gpio_out_named(dev, &s->vbus, "vbus", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_udphs_clock_changed,
                                 s, ClockUpdate);
    s->utmi = qdev_init_clock_in(dev, "utmi", at91_udphs_clock_changed,
                                 s, ClockUpdate);
}

static void at91_udphs_realize(DeviceState *dev, Error **errp)
{
    AT91UDPHSState *s = AT91_UDPHS(dev);

    if (!s->dma_mr) {
        error_setg(errp, TYPE_AT91_UDPHS ": dma-memory link is not set");
        return;
    }
    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_UDPHS ": pclk must be connected");
        return;
    }
    if (!clock_has_source(s->utmi)) {
        error_setg(errp, TYPE_AT91_UDPHS ": utmi must be connected");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, TYPE_AT91_UDPHS "-dma");
    s->dma_as_initialized = true;
}

static void at91_udphs_unrealize(DeviceState *dev)
{
    AT91UDPHSState *s = AT91_UDPHS(dev);

    if (s->dma_as_initialized) {
        address_space_destroy(&s->dma_as);
        s->dma_as_initialized = false;
    }
}

static const Property at91_udphs_properties[] = {
    DEFINE_PROP_LINK("dma-memory", AT91UDPHSState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void at91_udphs_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 USB Device Port High Speed controller";
    dc->realize = at91_udphs_realize;
    dc->unrealize = at91_udphs_unrealize;
    dc->vmsd = &vmstate_at91_udphs;
    device_class_set_legacy_reset(dc, at91_udphs_reset);
    device_class_set_props(dc, at91_udphs_properties);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const Property at91_udphs_gadget_properties[] = {
    DEFINE_PROP_LINK("udphs", AT91UDPHSGadgetState, udphs,
                     TYPE_AT91_UDPHS, AT91UDPHSState *),
};

static void at91_udphs_gadget_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 UDPHS USB bus bridge";
    dc->vmsd = &vmstate_at91_udphs_gadget;
    device_class_set_props(dc, at91_udphs_gadget_properties);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    uc->product_desc = "SAM9X7 UDPHS gadget bridge";
    uc->realize = at91_udphs_gadget_realize;
    uc->unrealize = at91_udphs_gadget_unrealize;
    uc->handle_reset = at91_udphs_gadget_handle_reset;
    uc->handle_packet = at91_udphs_gadget_handle_packet;
}

static const TypeInfo at91_udphs_info = {
    .name = TYPE_AT91_UDPHS,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91UDPHSState),
    .instance_init = at91_udphs_init,
    .class_init = at91_udphs_class_init,
};

static const TypeInfo at91_udphs_gadget_info = {
    .name = TYPE_AT91_UDPHS_GADGET,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(AT91UDPHSGadgetState),
    .class_init = at91_udphs_gadget_class_init,
};

static void at91_udphs_register_types(void)
{
    type_register_static(&at91_udphs_info);
    type_register_static(&at91_udphs_gadget_info);
}

type_init(at91_udphs_register_types)
