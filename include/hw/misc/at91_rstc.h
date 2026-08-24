/*
 * Microchip AT91 reset controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_RSTC_H
#define HW_MISC_AT91_RSTC_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/misc/at91_sysc.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_RSTC "at91-rstc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91RSTCState, AT91_RSTC)

struct AT91RSTCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq nrst_out;
    Clock *slck;
    AT91SYSCWPState *sysc;
    QEMUTimer *external_timer;
    QEMUTimer *sample_timer;

    uint32_t mode;
    uint8_t reset_type;
    uint8_t pending_reset_type;
    bool ursts;
    bool srcmp;
    bool nrst_level;
    bool nrst_out_level;
};

#endif /* HW_MISC_AT91_RSTC_H */
