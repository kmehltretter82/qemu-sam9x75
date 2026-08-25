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
    qemu_irq reset_request;
    Clock *slck;
    AT91SYSCWPState *sysc;
    QEMUTimer *external_timer;
    QEMUTimer *sample_timer;

    bool general_reset_reports_backup;
    uint32_t mode;
    uint8_t reset_type;
    uint8_t pending_reset_type;
    bool ursts;
    bool srcmp;
    bool nrst_level;
    bool power_reset_level;
    bool nrst_out_level;
    bool reset_request_level;
};

bool at91_rstc_gpbr_clear_enabled(const AT91RSTCState *s);
bool at91_rstc_watchdog_reset_pending(const AT91RSTCState *s);

#endif /* HW_MISC_AT91_RSTC_H */
