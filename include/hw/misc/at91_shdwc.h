/*
 * Microchip AT91 shutdown controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_SHDWC_H
#define HW_MISC_AT91_SHDWC_H

#include "hw/core/clock.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/misc/at91_sysc.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_SHDWC "at91-shdwc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91SHDWCState, AT91_SHDWC)

#define AT91_SHDWC_NUM_WAKE_SOURCES 3

struct AT91SHDWCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    Clock *slck;
    AT91SYSCWPState *sysc;
    qemu_irq shdn;
    QEMUTimer *shutdown_timer;
    QEMUTimer *wake_timer;

    uint32_t mode;
    uint32_t status;
    uint32_t wakeup_inputs;
    int64_t shutdown_remaining;
    int64_t wake_remaining[AT91_SHDWC_NUM_WAKE_SOURCES];
    int64_t wake_deadline[AT91_SHDWC_NUM_WAKE_SOURCES];
    bool initialized;
    bool backup_mode;
    bool shdn_level;
    bool wkup_level;
    bool rtc_alarm_level;
    bool rtt_alarm_level;
    bool request_system_shutdown;
};

#endif /* HW_MISC_AT91_SHDWC_H */
