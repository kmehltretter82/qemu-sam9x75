/*
 * Microchip AT91 Debug Unit (DBGU)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_AT91_DBGU_H
#define HW_CHAR_AT91_DBGU_H

#include "chardev/char-fe.h"
#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_DBGU "at91-dbgu"
OBJECT_DECLARE_SIMPLE_TYPE(AT91DBGUState, AT91_DBGU)

struct AT91DBGUState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharFrontend chr;
    Clock *pclk;
    Clock *gclk;
    qemu_irq irq;
    qemu_irq tx_request;
    qemu_irq rx_request;
    QEMUTimer *timeout_timer;

    uint32_t mode;
    uint32_t interrupt_mask;
    uint32_t status;
    uint32_t receive_holding;
    uint32_t baud_generator;
    uint32_t receiver_timeout;
    uint32_t force_ntrst;
    uint32_t write_protection;

    uint32_t chip_id;
    uint32_t extension_id;

    bool receiver_enabled;
    bool transmitter_enabled;
    bool timeout_running;
    bool timeout_waiting;
    bool tx_request_level;
    bool rx_request_level;
};

#endif /* HW_CHAR_AT91_DBGU_H */
