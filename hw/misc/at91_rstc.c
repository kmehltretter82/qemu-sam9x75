/*
 * Microchip SAM9X7 reset controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/resettable.h"
#include "hw/misc/at91_rstc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/runstate.h"

#define RSTC_CR                 0x00
#define RSTC_SR                 0x04
#define RSTC_MR                 0x08
#define RSTC_MMIO_SIZE          0x0c

#define RSTC_CR_PROCRST         BIT(0)
#define RSTC_CR_EXTRST          BIT(3)
#define RSTC_CR_MASK            (RSTC_CR_PROCRST | RSTC_CR_EXTRST)
#define RSTC_KEY_MASK           0xff000000
#define RSTC_KEY                0xa5000000

#define RSTC_SR_URSTS           BIT(0)
#define RSTC_SR_RSTTYP_SHIFT    8
#define RSTC_SR_RSTTYP_MASK     (7U << RSTC_SR_RSTTYP_SHIFT)
#define RSTC_SR_NRSTL           BIT(16)
#define RSTC_SR_SRCMP           BIT(17)

#define RSTC_MR_URSTEN          BIT(0)
#define RSTC_MR_SCKSW           BIT(1)
#define RSTC_MR_URSTASYNC       BIT(2)
#define RSTC_MR_URSTIEN         BIT(4)
#define RSTC_MR_ERSTL_MASK      (0xfU << 8)
#define RSTC_MR_ENGCLR          BIT(20)
#define RSTC_MR_MASK            (RSTC_MR_URSTEN | RSTC_MR_SCKSW | \
                                 RSTC_MR_URSTASYNC | RSTC_MR_URSTIEN | \
                                 RSTC_MR_ERSTL_MASK | RSTC_MR_ENGCLR)

#define RSTC_TYPE_NONE          UINT8_MAX
#define RSTC_TYPE_GENERAL       0
#define RSTC_TYPE_BACKUP        1
#define RSTC_TYPE_WATCHDOG      2
#define RSTC_TYPE_SOFTWARE      3
#define RSTC_TYPE_USER          4

#define RSTC_INTERNAL_RESET_CYCLES 3
#define RSTC_USER_SAMPLE_CYCLES 2
#define RSTC_USER_RELEASE_CYCLES 6

bool at91_rstc_gpbr_clear_enabled(const AT91RSTCState *s)
{
    return s && (s->mode & RSTC_MR_ENGCLR);
}

bool at91_rstc_take_warm_reset_request(AT91RSTCState *s)
{
    bool pending = s && s->warm_reset_pending;

    if (s) {
        s->warm_reset_pending = false;
    }
    return pending;
}

static void at91_rstc_update_irq(AT91RSTCState *s)
{
    bool asserted = s->ursts && !(s->mode & RSTC_MR_URSTEN) &&
                    (s->mode & RSTC_MR_URSTIEN);

    qemu_set_irq(s->irq, asserted);
}

static int64_t at91_rstc_slow_clock_delay(AT91RSTCState *s,
                                          uint64_t cycles)
{
    uint64_t hz = clock_get_hz(s->slck);

    if (!hz) {
        return 1;
    }
    return DIV_ROUND_UP(cycles * NANOSECONDS_PER_SECOND, hz);
}

static void at91_rstc_external_done(void *opaque)
{
    AT91RSTCState *s = AT91_RSTC(opaque);

    s->nrst_out_level = true;
    qemu_set_irq(s->nrst_out, 1);
}

static void at91_rstc_assert_external(AT91RSTCState *s)
{
    unsigned int erstl = extract32(s->mode, 8, 4);
    uint64_t cycles = 1ULL << (erstl + 1);

    s->nrst_out_level = false;
    qemu_set_irq(s->nrst_out, 0);
    timer_mod(s->external_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              at91_rstc_slow_clock_delay(s, cycles));
}

static void at91_rstc_assert_core_reset(AT91RSTCState *s)
{
    s->reset_request_level = true;
    qemu_set_irq(s->reset_request, 1);
}

static void at91_rstc_request_reset(AT91RSTCState *s)
{
    at91_rstc_assert_core_reset(s);
    s->warm_reset_pending = true;
    qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
}

static bool at91_rstc_continue_user_reset(AT91RSTCState *s)
{
    if (s->nrst_level || s->power_reset_level ||
        !(s->mode & RSTC_MR_URSTEN)) {
        return false;
    }

    s->pending_reset_type = RSTC_TYPE_USER;
    s->user_reset_active = true;
    at91_rstc_assert_external(s);
    return true;
}

static void at91_rstc_reset_done(void *opaque)
{
    AT91RSTCState *s = AT91_RSTC(opaque);
    uint8_t reset_type = s->pending_reset_type;
    bool reset_processor = reset_type == RSTC_TYPE_WATCHDOG ||
                           (reset_type == RSTC_TYPE_SOFTWARE &&
                            s->software_procrst);

    s->srcmp = false;
    s->software_procrst = false;
    s->warm_reset_pending = false;
    if (reset_processor) {
        if (!at91_rstc_continue_user_reset(s)) {
            /* RSTTYP changes only when Processor reset is released. */
            s->reset_type = reset_type;
            s->pending_reset_type = RSTC_TYPE_NONE;
            s->reset_request_level = false;
            qemu_set_irq(s->reset_request, 0);
        }
    }
}

static void at91_rstc_user_release_done(void *opaque)
{
    AT91RSTCState *s = AT91_RSTC(opaque);

    if (!s->user_reset_active || !s->nrst_level ||
        s->power_reset_level ||
        s->pending_reset_type != RSTC_TYPE_USER) {
        return;
    }

    s->user_reset_active = false;
    s->reset_type = RSTC_TYPE_USER;
    s->pending_reset_type = RSTC_TYPE_NONE;
    s->reset_request_level = false;
    s->warm_reset_pending = false;
    qemu_set_irq(s->reset_request, 0);
}

static void at91_rstc_user_event(AT91RSTCState *s)
{
    if (s->mode & RSTC_MR_URSTEN) {
        if (s->power_reset_level || s->user_reset_active ||
            s->pending_reset_type != RSTC_TYPE_NONE) {
            return;
        }
        s->pending_reset_type = RSTC_TYPE_USER;
        s->user_reset_active = true;
        at91_rstc_assert_external(s);
        at91_rstc_request_reset(s);
    }
}

static void at91_rstc_sample_nrst(void *opaque)
{
    AT91RSTCState *s = AT91_RSTC(opaque);

    if (!s->nrst_level) {
        at91_rstc_user_event(s);
    }
}

static void at91_rstc_set_nrst(void *opaque, int n, int level)
{
    AT91RSTCState *s = AT91_RSTC(opaque);
    bool old_level = s->nrst_level;

    s->nrst_level = !!level;
    if (old_level && !s->nrst_level) {
        /* URSTS records the raw high-to-low transition. */
        s->ursts = true;
        at91_rstc_update_irq(s);
        if (s->user_reset_active) {
            timer_del(s->user_timer);
            return;
        }
        if (s->mode & RSTC_MR_URSTASYNC) {
            at91_rstc_user_event(s);
        } else {
            timer_mod(s->sample_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      at91_rstc_slow_clock_delay(
                          s, RSTC_USER_SAMPLE_CYCLES));
        }
    } else if (!old_level && s->nrst_level) {
        timer_del(s->sample_timer);
        if (!s->power_reset_level &&
            s->pending_reset_type == RSTC_TYPE_BACKUP) {
            at91_rstc_request_reset(s);
        } else if (s->user_reset_active &&
                   s->pending_reset_type == RSTC_TYPE_USER) {
            timer_mod(s->user_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      at91_rstc_slow_clock_delay(
                          s, RSTC_USER_RELEASE_CYCLES));
        }
    }
}

static void at91_rstc_set_power_reset(void *opaque, int n, int level)
{
    AT91RSTCState *s = AT91_RSTC(opaque);
    bool asserted = !!level;

    if (s->power_reset_level == asserted) {
        return;
    }

    s->power_reset_level = asserted;
    if (asserted) {
        timer_del(s->reset_timer);
        timer_del(s->user_timer);
        s->srcmp = false;
        s->software_procrst = false;
        s->user_reset_active = false;
        s->ursts = true;
        s->pending_reset_type = RSTC_TYPE_BACKUP;
        at91_rstc_update_irq(s);
        at91_rstc_assert_external(s);
    } else if (s->nrst_level &&
               s->pending_reset_type == RSTC_TYPE_BACKUP &&
               !resettable_is_in_reset(OBJECT(s))) {
        at91_rstc_request_reset(s);
    }
}

static void at91_rstc_set_wdt_reset(void *opaque, int n, int level)
{
    AT91RSTCState *s = AT91_RSTC(opaque);

    if (level) {
        /* Backup and User reset have priority over Watchdog reset. */
        if (s->pending_reset_type == RSTC_TYPE_BACKUP ||
            s->pending_reset_type == RSTC_TYPE_USER) {
            return;
        }
        timer_del(s->reset_timer);
        timer_del(s->user_timer);
        s->srcmp = false;
        s->software_procrst = false;
        s->user_reset_active = false;
        s->pending_reset_type = RSTC_TYPE_WATCHDOG;
        at91_rstc_assert_external(s);
        at91_rstc_assert_core_reset(s);
        s->warm_reset_pending = true;
        timer_mod(s->reset_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  at91_rstc_slow_clock_delay(
                      s, RSTC_INTERNAL_RESET_CYCLES));
    } else if (s->pending_reset_type == RSTC_TYPE_WATCHDOG &&
               !resettable_is_in_reset(OBJECT(s))) {
        /* A non-reset QEMU watchdog policy ends the physical pulse. */
        s->pending_reset_type = RSTC_TYPE_NONE;
        s->warm_reset_pending = false;
        timer_del(s->reset_timer);
        timer_del(s->external_timer);
        s->nrst_out_level = true;
        s->reset_request_level = false;
        qemu_set_irq(s->nrst_out, 1);
        qemu_set_irq(s->reset_request, 0);
    }
}

static uint32_t at91_rstc_status(AT91RSTCState *s)
{
    uint8_t reset_type = s->reset_type;
    uint32_t value;

    if (reset_type == RSTC_TYPE_GENERAL &&
        s->general_reset_reports_backup) {
        reset_type = RSTC_TYPE_BACKUP;
    }
    value = reset_type << RSTC_SR_RSTTYP_SHIFT;

    if (s->ursts) {
        value |= RSTC_SR_URSTS;
    }
    if (s->nrst_level && !s->power_reset_level) {
        value |= RSTC_SR_NRSTL;
    }
    if (s->srcmp) {
        value |= RSTC_SR_SRCMP;
    }
    return value;
}

static uint64_t at91_rstc_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91RSTCState *s = AT91_RSTC(opaque);
    uint32_t value;

    switch (offset) {
    case RSTC_CR:
        return 0;
    case RSTC_SR:
        value = at91_rstc_status(s);
        s->ursts = false;
        at91_rstc_update_irq(s);
        return value;
    case RSTC_MR:
        return s->mode;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RSTC ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_rstc_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91RSTCState *s = AT91_RSTC(opaque);
    uint32_t command;

    switch (offset) {
    case RSTC_CR:
        if ((value & RSTC_KEY_MASK) != RSTC_KEY || s->srcmp ||
            s->pending_reset_type != RSTC_TYPE_NONE) {
            return;
        }
        command = value & RSTC_CR_MASK;
        if (!command) {
            return;
        }

        s->srcmp = true;
        s->software_procrst = command & RSTC_CR_PROCRST;
        timer_mod(s->reset_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  at91_rstc_slow_clock_delay(s,
                                              RSTC_INTERNAL_RESET_CYCLES));
        if (command & RSTC_CR_EXTRST) {
            at91_rstc_assert_external(s);
        }
        if (command & RSTC_CR_PROCRST) {
            s->pending_reset_type = RSTC_TYPE_SOFTWARE;
            at91_rstc_request_reset(s);
        }
        break;
    case RSTC_MR:
        if ((value & RSTC_KEY_MASK) != RSTC_KEY) {
            return;
        }
        if (at91_sysc_write_protected(s->sysc, RSTC_MR, false, true)) {
            return;
        }
        s->mode = value & RSTC_MR_MASK;
        at91_rstc_update_irq(s);
        break;
    case RSTC_SR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RSTC ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RSTC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_rstc_ops = {
    .read = at91_rstc_read,
    .write = at91_rstc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_rstc_reset_hold(Object *obj, ResetType type)
{
    AT91RSTCState *s = AT91_RSTC(obj);
    bool active_reset = (s->pending_reset_type == RSTC_TYPE_SOFTWARE &&
                         s->srcmp && s->software_procrst) ||
                        (s->pending_reset_type == RSTC_TYPE_WATCHDOG &&
                         timer_pending(s->reset_timer)) ||
                        (s->pending_reset_type == RSTC_TYPE_USER &&
                         s->user_reset_active) ||
                        (s->pending_reset_type == RSTC_TYPE_BACKUP &&
                         s->power_reset_level);

    /*
     * RSTC is in the retained domain.  A VDDCORE reset happens at the
     * assertion edge, while this state and its timers run until release.
     */
    if (type != RESET_TYPE_WAKEUP) {
        timer_del(s->external_timer);
        timer_del(s->sample_timer);
        timer_del(s->reset_timer);
        timer_del(s->user_timer);
        s->reset_type = RSTC_TYPE_GENERAL;
        s->mode = RSTC_MR_URSTEN;
        s->ursts = true;
        s->nrst_level = true;
        s->power_reset_level = false;
        s->pending_reset_type = RSTC_TYPE_NONE;
        s->srcmp = false;
        s->software_procrst = false;
        s->user_reset_active = false;
        s->nrst_out_level = true;
        s->reset_request_level = false;
        s->warm_reset_pending = false;
    } else if (!active_reset) {
        if (s->pending_reset_type != RSTC_TYPE_NONE) {
            s->reset_type = s->pending_reset_type;
        }
        timer_del(s->sample_timer);
        timer_del(s->reset_timer);
        timer_del(s->user_timer);
        s->pending_reset_type = RSTC_TYPE_NONE;
        s->srcmp = false;
        s->software_procrst = false;
        s->user_reset_active = false;
        s->reset_request_level = false;
        s->warm_reset_pending = false;
    }

    qemu_set_irq(s->nrst_out, s->nrst_out_level);
    qemu_set_irq(s->reset_request, s->reset_request_level);
    at91_rstc_update_irq(s);
}

static void at91_rstc_init(Object *obj)
{
    AT91RSTCState *s = AT91_RSTC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_rstc_ops, s,
                          TYPE_AT91_RSTC, RSTC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(dev, at91_rstc_set_nrst, "nrst", 1);
    qdev_init_gpio_in_named(dev, at91_rstc_set_power_reset,
                            "power-reset", 1);
    qdev_init_gpio_in_named(dev, at91_rstc_set_wdt_reset, "wdt-reset", 1);
    qdev_init_gpio_out_named(dev, &s->nrst_out, "nrst-out", 1);
    qdev_init_gpio_out_named(dev, &s->reset_request, "reset-request", 1);
    s->slck = qdev_init_clock_in(dev, "slck", NULL, s, ClockUpdate);
    s->pending_reset_type = RSTC_TYPE_NONE;
    s->nrst_level = true;
    s->nrst_out_level = true;
}

static void at91_rstc_realize(DeviceState *dev, Error **errp)
{
    AT91RSTCState *s = AT91_RSTC(dev);

    if (!clock_has_source(s->slck)) {
        error_setg(errp, TYPE_AT91_RSTC ": slck clock must be connected");
        return;
    }
    if (!s->sysc) {
        error_setg(errp, TYPE_AT91_RSTC ": sysc link must be connected");
        return;
    }

    s->external_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                     at91_rstc_external_done, s);
    s->sample_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   at91_rstc_sample_nrst, s);
    s->reset_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  at91_rstc_reset_done, s);
    s->user_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 at91_rstc_user_release_done, s);
}

static void at91_rstc_finalize(Object *obj)
{
    AT91RSTCState *s = AT91_RSTC(obj);

    timer_free(s->external_timer);
    timer_free(s->sample_timer);
    timer_free(s->reset_timer);
    timer_free(s->user_timer);
}

static int at91_rstc_post_load(void *opaque, int version_id)
{
    AT91RSTCState *s = AT91_RSTC(opaque);

    if (version_id < 3) {
        s->software_procrst = s->srcmp &&
                              s->pending_reset_type == RSTC_TYPE_SOFTWARE;
        s->user_reset_active = false;
        timer_del(s->reset_timer);
        timer_del(s->user_timer);
        if (s->srcmp) {
            timer_mod(s->reset_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      at91_rstc_slow_clock_delay(
                          s, RSTC_INTERNAL_RESET_CYCLES));
        }
    }

    qemu_set_irq(s->nrst_out, s->nrst_out_level);
    qemu_set_irq(s->reset_request, s->reset_request_level);
    at91_rstc_update_irq(s);
    return 0;
}

static const VMStateDescription at91_rstc_vmstate = {
    .name = TYPE_AT91_RSTC,
    .version_id = 3,
    .minimum_version_id = 1,
    .post_load = at91_rstc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(slck, AT91RSTCState),
        VMSTATE_TIMER_PTR(external_timer, AT91RSTCState),
        VMSTATE_TIMER_PTR(sample_timer, AT91RSTCState),
        VMSTATE_TIMER_PTR_V(reset_timer, AT91RSTCState, 3),
        VMSTATE_TIMER_PTR_V(user_timer, AT91RSTCState, 3),
        VMSTATE_UINT32(mode, AT91RSTCState),
        VMSTATE_UINT8(reset_type, AT91RSTCState),
        VMSTATE_UINT8(pending_reset_type, AT91RSTCState),
        VMSTATE_BOOL(ursts, AT91RSTCState),
        VMSTATE_BOOL(srcmp, AT91RSTCState),
        VMSTATE_BOOL(nrst_level, AT91RSTCState),
        VMSTATE_BOOL(nrst_out_level, AT91RSTCState),
        VMSTATE_BOOL_V(power_reset_level, AT91RSTCState, 2),
        VMSTATE_BOOL_V(reset_request_level, AT91RSTCState, 2),
        VMSTATE_BOOL_V(software_procrst, AT91RSTCState, 3),
        VMSTATE_BOOL_V(user_reset_active, AT91RSTCState, 3),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_rstc_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91RSTCState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
    DEFINE_PROP_BOOL("general-reset-reports-backup", AT91RSTCState,
                     general_reset_reports_backup, false),
};

static void at91_rstc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "Microchip AT91 reset controller";
    dc->realize = at91_rstc_realize;
    dc->vmsd = &at91_rstc_vmstate;
    device_class_set_props(dc, at91_rstc_properties);
    rc->phases.hold = at91_rstc_reset_hold;
}

static const TypeInfo at91_rstc_info = {
    .name = TYPE_AT91_RSTC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91RSTCState),
    .instance_init = at91_rstc_init,
    .instance_finalize = at91_rstc_finalize,
    .class_init = at91_rstc_class_init,
};

static void at91_rstc_register_types(void)
{
    type_register_static(&at91_rstc_info);
}

type_init(at91_rstc_register_types)
