/*
 * Microchip AT91 shutdown controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/at91_shdwc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/runstate.h"

#define SHDWC_CR                0x00
#define SHDWC_MR                0x04
#define SHDWC_SR                0x08
#define SHDWC_WUIR              0x0c
#define SHDWC_MMIO_SIZE         0x10

#define SHDWC_CR_SHDW           BIT(0)
#define SHDWC_CR_KEY_MASK       0xff000000
#define SHDWC_CR_KEY            0xa5000000

#define SHDWC_MR_RTTWKEN        BIT(16)
#define SHDWC_MR_RTCWKEN        BIT(17)
#define SHDWC_MR_WKUPDBC_MASK   (7U << 24)
#define SHDWC_MR_MASK           (SHDWC_MR_RTTWKEN | SHDWC_MR_RTCWKEN | \
                                 SHDWC_MR_WKUPDBC_MASK)

#define SHDWC_SR_WKUPS          BIT(0)
#define SHDWC_SR_RTTWK          BIT(4)
#define SHDWC_SR_RTCWK          BIT(5)
#define SHDWC_SR_WKUPIS0        BIT(16)

#define SHDWC_WUIR_WKUPEN0      BIT(0)
#define SHDWC_WUIR_WKUPT0       BIT(16)
#define SHDWC_WUIR_MASK         (SHDWC_WUIR_WKUPEN0 | \
                                 SHDWC_WUIR_WKUPT0)

#define SHDWC_SYSC_OFFSET       0x10

enum {
    SHDWC_WAKE_WKUP,
    SHDWC_WAKE_RTC,
    SHDWC_WAKE_RTT,
};

static int64_t at91_shdwc_cycles_ns(const AT91SHDWCState *s,
                                    uint64_t cycles)
{
    uint64_t hz = clock_get_hz(s->slck);

    if (!hz) {
        return 1;
    }
    return DIV_ROUND_UP(cycles * NANOSECONDS_PER_SECOND, hz);
}

static uint32_t at91_shdwc_debounce_cycles(const AT91SHDWCState *s)
{
    static const uint32_t cycles[] = { 1, 3, 32, 512, 4096, 32768 };
    unsigned int selector = extract32(s->mode, 24, 3);

    return cycles[MIN(selector, ARRAY_SIZE(cycles) - 1)];
}

static bool at91_shdwc_wkup_active(const AT91SHDWCState *s)
{
    bool active_high = s->wakeup_inputs & SHDWC_WUIR_WKUPT0;

    return s->wkup_level == active_high;
}

static void at91_shdwc_update_wake_timer(AT91SHDWCState *s)
{
    int64_t earliest = INT64_MAX;
    unsigned int i;

    for (i = 0; i < AT91_SHDWC_NUM_WAKE_SOURCES; i++) {
        if (s->wake_deadline[i] >= 0) {
            earliest = MIN(earliest, s->wake_deadline[i]);
        }
    }

    if (earliest == INT64_MAX) {
        timer_del(s->wake_timer);
    } else {
        timer_mod_ns(s->wake_timer, earliest);
    }
}

static void at91_shdwc_cancel_wake_sources(AT91SHDWCState *s)
{
    unsigned int i;

    for (i = 0; i < AT91_SHDWC_NUM_WAKE_SOURCES; i++) {
        s->wake_deadline[i] = -1;
    }
    timer_del(s->wake_timer);
}

static void at91_shdwc_wake(AT91SHDWCState *s)
{
    s->backup_mode = false;
    s->shdn_level = true;
    qemu_set_irq(s->shdn, 1);
    at91_shdwc_cancel_wake_sources(s);
}

static void at91_shdwc_wake_tick(void *opaque)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    bool wake = false;

    if (s->wake_deadline[SHDWC_WAKE_WKUP] >= 0 &&
        s->wake_deadline[SHDWC_WAKE_WKUP] <= now) {
        s->wake_deadline[SHDWC_WAKE_WKUP] = -1;
        if (s->backup_mode &&
            (s->wakeup_inputs & SHDWC_WUIR_WKUPEN0) &&
            at91_shdwc_wkup_active(s)) {
            s->status |= SHDWC_SR_WKUPS | SHDWC_SR_WKUPIS0;
            wake = true;
        }
    }
    if (s->wake_deadline[SHDWC_WAKE_RTC] >= 0 &&
        s->wake_deadline[SHDWC_WAKE_RTC] <= now) {
        s->wake_deadline[SHDWC_WAKE_RTC] = -1;
        if (s->backup_mode && (s->mode & SHDWC_MR_RTCWKEN) &&
            s->rtc_alarm_level) {
            s->status |= SHDWC_SR_RTCWK;
            wake = true;
        }
    }
    if (s->wake_deadline[SHDWC_WAKE_RTT] >= 0 &&
        s->wake_deadline[SHDWC_WAKE_RTT] <= now) {
        s->wake_deadline[SHDWC_WAKE_RTT] = -1;
        if (s->backup_mode && (s->mode & SHDWC_MR_RTTWKEN) &&
            s->rtt_alarm_level) {
            s->status |= SHDWC_SR_RTTWK;
            wake = true;
        }
    }

    if (wake) {
        at91_shdwc_wake(s);
    } else {
        at91_shdwc_update_wake_timer(s);
    }
}

static void at91_shdwc_schedule_wake(AT91SHDWCState *s,
                                     unsigned int source, uint64_t cycles)
{
    s->wake_deadline[source] =
        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        at91_shdwc_cycles_ns(s, cycles);
    at91_shdwc_update_wake_timer(s);
}

static void at91_shdwc_shutdown_tick(void *opaque)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);

    s->backup_mode = true;
    s->shdn_level = false;
    qemu_set_irq(s->shdn, 0);
    if (s->request_system_shutdown) {
        qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void at91_shdwc_set_wkup(void *opaque, int n, int level)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);
    bool old_level = s->wkup_level;

    s->wkup_level = !!level;
    if (s->wake_deadline[SHDWC_WAKE_WKUP] >= 0 &&
        !at91_shdwc_wkup_active(s)) {
        s->wake_deadline[SHDWC_WAKE_WKUP] = -1;
        at91_shdwc_update_wake_timer(s);
    }
    if (s->backup_mode && old_level != s->wkup_level &&
        (s->wakeup_inputs & SHDWC_WUIR_WKUPEN0) &&
        at91_shdwc_wkup_active(s)) {
        at91_shdwc_schedule_wake(s, SHDWC_WAKE_WKUP,
                                 at91_shdwc_debounce_cycles(s));
    }
}

static void at91_shdwc_set_rtc_alarm(void *opaque, int n, int level)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);
    bool old_level = s->rtc_alarm_level;

    s->rtc_alarm_level = !!level;
    if (!level && s->wake_deadline[SHDWC_WAKE_RTC] >= 0) {
        s->wake_deadline[SHDWC_WAKE_RTC] = -1;
        at91_shdwc_update_wake_timer(s);
    }
    if (s->backup_mode && !old_level && level &&
        (s->mode & SHDWC_MR_RTCWKEN)) {
        at91_shdwc_schedule_wake(s, SHDWC_WAKE_RTC, 1);
    }
}

static void at91_shdwc_set_rtt_alarm(void *opaque, int n, int level)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);
    bool old_level = s->rtt_alarm_level;

    s->rtt_alarm_level = !!level;
    if (!level && s->wake_deadline[SHDWC_WAKE_RTT] >= 0) {
        s->wake_deadline[SHDWC_WAKE_RTT] = -1;
        at91_shdwc_update_wake_timer(s);
    }
    if (s->backup_mode && !old_level && level &&
        (s->mode & SHDWC_MR_RTTWKEN)) {
        at91_shdwc_schedule_wake(s, SHDWC_WAKE_RTT, 1);
    }
}

static uint64_t at91_shdwc_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);
    uint32_t value;

    switch (offset) {
    case SHDWC_CR:
        return 0;
    case SHDWC_MR:
        return s->mode;
    case SHDWC_SR:
        value = s->status;
        s->status = 0;
        return value;
    case SHDWC_WUIR:
        return s->wakeup_inputs;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SHDWC ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_shdwc_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);

    switch (offset) {
    case SHDWC_CR:
        if ((value & SHDWC_CR_KEY_MASK) == SHDWC_CR_KEY &&
            (value & SHDWC_CR_SHDW) &&
            !timer_pending(s->shutdown_timer) && !s->backup_mode) {
            timer_mod_ns(s->shutdown_timer,
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                at91_shdwc_cycles_ns(s, 2));
        }
        break;
    case SHDWC_MR:
        if (at91_sysc_write_protected(s->sysc,
                                      SHDWC_SYSC_OFFSET + offset,
                                      false, true)) {
            return;
        }
        s->mode = value & SHDWC_MR_MASK;
        if (!(s->mode & SHDWC_MR_RTCWKEN)) {
            s->wake_deadline[SHDWC_WAKE_RTC] = -1;
        }
        if (!(s->mode & SHDWC_MR_RTTWKEN)) {
            s->wake_deadline[SHDWC_WAKE_RTT] = -1;
        }
        at91_shdwc_update_wake_timer(s);
        break;
    case SHDWC_WUIR:
        if (at91_sysc_write_protected(s->sysc,
                                      SHDWC_SYSC_OFFSET + offset,
                                      false, true)) {
            return;
        }
        s->wakeup_inputs = value & SHDWC_WUIR_MASK;
        if (!(s->wakeup_inputs & SHDWC_WUIR_WKUPEN0) ||
            !at91_shdwc_wkup_active(s)) {
            s->wake_deadline[SHDWC_WAKE_WKUP] = -1;
            at91_shdwc_update_wake_timer(s);
        }
        break;
    case SHDWC_SR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SHDWC ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SHDWC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_shdwc_ops = {
    .read = at91_shdwc_read,
    .write = at91_shdwc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_shdwc_power_reset(AT91SHDWCState *s)
{
    s->mode = 0;
    s->status = 0;
    s->wakeup_inputs = 0;
}

static void at91_shdwc_set_vddbu_reset(void *opaque, int n, int level)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);

    if (level) {
        timer_del(s->shutdown_timer);
        at91_shdwc_cancel_wake_sources(s);
        at91_shdwc_power_reset(s);
        s->backup_mode = false;
        s->shdn_level = true;
        qemu_set_irq(s->shdn, 1);
    }
}

static void at91_shdwc_reset(DeviceState *dev)
{
    AT91SHDWCState *s = AT91_SHDWC(dev);

    timer_del(s->shutdown_timer);
    at91_shdwc_cancel_wake_sources(s);
    if (!s->initialized) {
        at91_shdwc_power_reset(s);
        s->initialized = true;
    }
    s->backup_mode = false;
    s->shdn_level = true;
    qemu_set_irq(s->shdn, 1);
}

static void at91_shdwc_init(Object *obj)
{
    AT91SHDWCState *s = AT91_SHDWC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_shdwc_ops, s,
                          TYPE_AT91_SHDWC, SHDWC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_out_named(dev, &s->shdn, "shdn", 1);
    qdev_init_gpio_in_named(dev, at91_shdwc_set_wkup, "wkup", 1);
    qdev_init_gpio_in_named(dev, at91_shdwc_set_rtc_alarm,
                            "rtc-alarm", 1);
    qdev_init_gpio_in_named(dev, at91_shdwc_set_rtt_alarm,
                            "rtt-alarm", 1);
    qdev_init_gpio_in_named(dev, at91_shdwc_set_vddbu_reset,
                            "vddbu-reset", 1);
    s->slck = qdev_init_clock_in(dev, "slck", NULL, s, ClockUpdate);

    s->shutdown_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     at91_shdwc_shutdown_tick, s);
    s->wake_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 at91_shdwc_wake_tick, s);
}

static void at91_shdwc_realize(DeviceState *dev, Error **errp)
{
    AT91SHDWCState *s = AT91_SHDWC(dev);

    if (!clock_has_source(s->slck)) {
        error_setg(errp, TYPE_AT91_SHDWC ": slck clock must be connected");
        return;
    }
    if (!s->sysc) {
        error_setg(errp, TYPE_AT91_SHDWC ": sysc link must be connected");
        return;
    }
}

static void at91_shdwc_finalize(Object *obj)
{
    AT91SHDWCState *s = AT91_SHDWC(obj);

    timer_free(s->shutdown_timer);
    timer_free(s->wake_timer);
}

static int at91_shdwc_pre_save(void *opaque)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    unsigned int i;

    s->shutdown_remaining = timer_pending(s->shutdown_timer) ?
        MAX(timer_expire_time_ns(s->shutdown_timer) - now, 0) : -1;
    for (i = 0; i < AT91_SHDWC_NUM_WAKE_SOURCES; i++) {
        s->wake_remaining[i] = s->wake_deadline[i] >= 0 ?
            MAX(s->wake_deadline[i] - now, 0) : -1;
    }
    return 0;
}

static int at91_shdwc_post_load(void *opaque, int version_id)
{
    AT91SHDWCState *s = AT91_SHDWC(opaque);
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    unsigned int i;

    if (s->shutdown_remaining >= 0) {
        timer_mod_ns(s->shutdown_timer, now + s->shutdown_remaining);
    } else {
        timer_del(s->shutdown_timer);
    }
    for (i = 0; i < AT91_SHDWC_NUM_WAKE_SOURCES; i++) {
        s->wake_deadline[i] = s->wake_remaining[i] >= 0 ?
            now + s->wake_remaining[i] : -1;
    }
    at91_shdwc_update_wake_timer(s);
    qemu_set_irq(s->shdn, s->shdn_level);
    return 0;
}

static const VMStateDescription at91_shdwc_vmstate = {
    .name = TYPE_AT91_SHDWC,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = at91_shdwc_pre_save,
    .post_load = at91_shdwc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(slck, AT91SHDWCState),
        VMSTATE_UINT32(mode, AT91SHDWCState),
        VMSTATE_UINT32(status, AT91SHDWCState),
        VMSTATE_UINT32(wakeup_inputs, AT91SHDWCState),
        VMSTATE_INT64(shutdown_remaining, AT91SHDWCState),
        VMSTATE_INT64_ARRAY(wake_remaining, AT91SHDWCState,
                            AT91_SHDWC_NUM_WAKE_SOURCES),
        VMSTATE_BOOL(initialized, AT91SHDWCState),
        VMSTATE_BOOL(backup_mode, AT91SHDWCState),
        VMSTATE_BOOL(shdn_level, AT91SHDWCState),
        VMSTATE_BOOL(wkup_level, AT91SHDWCState),
        VMSTATE_BOOL(rtc_alarm_level, AT91SHDWCState),
        VMSTATE_BOOL(rtt_alarm_level, AT91SHDWCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_shdwc_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91SHDWCState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
    DEFINE_PROP_BOOL("request-system-shutdown", AT91SHDWCState,
                     request_system_shutdown, true),
};

static void at91_shdwc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 shutdown controller";
    dc->realize = at91_shdwc_realize;
    dc->vmsd = &at91_shdwc_vmstate;
    device_class_set_props(dc, at91_shdwc_properties);
    device_class_set_legacy_reset(dc, at91_shdwc_reset);
}

static const TypeInfo at91_shdwc_info = {
    .name = TYPE_AT91_SHDWC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SHDWCState),
    .instance_init = at91_shdwc_init,
    .instance_finalize = at91_shdwc_finalize,
    .class_init = at91_shdwc_class_init,
};

static void at91_shdwc_register_types(void)
{
    type_register_static(&at91_shdwc_info);
}

type_init(at91_shdwc_register_types)
