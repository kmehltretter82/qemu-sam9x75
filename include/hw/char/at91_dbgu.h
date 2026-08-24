/*
 * Microchip AT91 Debug Unit (DBGU)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_CHAR_AT91_DBGU_H
#define HW_CHAR_AT91_DBGU_H

#include "chardev/char-fe.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_DBGU "at91-dbgu"
OBJECT_DECLARE_SIMPLE_TYPE(AT91DBGUState, AT91_DBGU)

struct AT91DBGUState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharFrontend chr;
    qemu_irq irq;

    uint32_t mode;
    uint32_t interrupt_mask;
    uint32_t status;
    uint32_t receive_holding;
    uint32_t baud_generator;
    uint32_t force_ntrst;

    uint32_t chip_id;
    uint32_t extension_id;

    bool receiver_enabled;
    bool transmitter_enabled;
};

#endif /* HW_CHAR_AT91_DBGU_H */
