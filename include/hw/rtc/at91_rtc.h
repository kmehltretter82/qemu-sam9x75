/*
 * Microchip AT91 real-time clock
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_RTC_AT91_RTC_H
#define HW_RTC_AT91_RTC_H

#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/misc/at91_sysc.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_AT91_RTC "at91-rtc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91RTCState, AT91_RTC)

#define AT91_RTC_NUM_TAMPER_INPUTS 8
#define AT91_RTC_NUM_OUTPUTS       2

struct AT91RTCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq alarm_out;
    qemu_irq rtc_out[AT91_RTC_NUM_OUTPUTS];
    qemu_irq tamper_out;
    AT91SYSCWPState *sysc;

    QEMUTimer *tick_timer;
    QEMUTimer *wave_timer[AT91_RTC_NUM_OUTPUTS];
    QEMUTimer *tamper_timer;

    uint32_t cr;
    uint32_t mr;
    uint32_t timalr;
    uint32_t calalr;
    uint32_t sr;
    uint32_t imr;
    uint32_t ver;
    uint32_t tmr;
    uint32_t tdpr;

    uint32_t year;
    uint32_t month;
    uint32_t mday;
    uint32_t wday;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
    uint32_t utc_time;

    uint32_t timestamp_time[2];
    uint32_t timestamp_date[2];
    uint32_t timestamp_source[2];
    uint32_t tamper_count;
    int64_t tick_deadline;
    int64_t tamper_deadline[AT91_RTC_NUM_TAMPER_INPUTS];
    int64_t tick_remaining;
    int64_t wave_remaining[AT91_RTC_NUM_OUTPUTS];
    int64_t tamper_remaining[AT91_RTC_NUM_TAMPER_INPUTS];

    uint32_t correction_count;
    uint8_t tamper_levels;
    uint8_t tamper_latched;
    bool wave_level[AT91_RTC_NUM_OUTPUTS];
    bool tamper_locked;
    bool update_acknowledged;
    bool update_ready;
    bool time_initialized;
};

#endif /* HW_RTC_AT91_RTC_H */
