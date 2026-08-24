/*
 * Microchip SAM9X60/SAM9X7 watchdog timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/watchdog/at91_wdt.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "system/watchdog.h"

#define WDT_CR                  0x00
#define WDT_MR                  0x04
#define WDT_VR                  0x08
#define WDT_WLR                 0x0c
#define WDT_ILR                 0x10
#define WDT_IER                 0x14
#define WDT_IDR                 0x18
#define WDT_ISR                 0x1c
#define WDT_IMR                 0x20
#define WDT_MMIO_SIZE           0x24

#define WDT_CR_WDRSTT           BIT(0)
#define WDT_CR_LOCKMR           BIT(4)
#define WDT_CR_KEY_MASK         0xff000000
#define WDT_CR_KEY              0xa5000000

#define WDT_MR_PERIODRST        BIT(4)
#define WDT_MR_RPTHRST          BIT(5)
#define WDT_MR_WDDIS            BIT(12)
#define WDT_MR_WDIDLEHLT        BIT(28)
#define WDT_MR_WDDBGHLT         BIT(29)
#define WDT_MR_MASK             (WDT_MR_PERIODRST | WDT_MR_RPTHRST | \
                                 WDT_MR_WDDIS | WDT_MR_WDIDLEHLT | \
                                 WDT_MR_WDDBGHLT)

#define WDT_WLR_PERIOD_MASK     0x00000fff
#define WDT_WLR_RPTH_MASK       0x0fff0000
#define WDT_WLR_MASK            (WDT_WLR_PERIOD_MASK | WDT_WLR_RPTH_MASK)
#define WDT_ILR_MASK            0x00000fff

#define WDT_INT_PER             BIT(0)
#define WDT_INT_RPTH            BIT(1)
#define WDT_INT_LVL             BIT(2)
#define WDT_INT_MASK            (WDT_INT_PER | WDT_INT_RPTH | WDT_INT_LVL)

static uint32_t at91_wdt_period(AT91WDTState *s)
{
    return s->window & WDT_WLR_PERIOD_MASK;
}

static uint32_t at91_wdt_rpth(AT91WDTState *s)
{
    return extract32(s->window, 16, 12);
}

static void at91_wdt_update_irq(AT91WDTState *s)
{
    qemu_set_irq(s->irq, !!(s->isr & s->imr));
}

static void at91_wdt_set_timer_period(AT91WDTState *s,
                                      ptimer_state *timer)
{
    if (clock_get_hz(s->slck)) {
        ptimer_set_period_from_clock(timer, s->slck, 128);
    } else {
        /* Keep ptimer state valid while the slow clock is stopped. */
        ptimer_set_period(timer, 1);
    }
}

static void at91_wdt_restart_level(AT91WDTState *s)
{
    uint32_t period = at91_wdt_period(s);
    uint32_t level = s->level & WDT_ILR_MASK;

    ptimer_transaction_begin(s->level_timer);
    ptimer_stop(s->level_timer);
    at91_wdt_set_timer_period(s, s->level_timer);

    s->level_running = s->running && level < period;
    if (s->level_running) {
        /* Fire when the visible down-counter first equals LVLTH. */
        ptimer_set_limit(s->level_timer, period - level, 1);
        if (!s->clock_suspended) {
            ptimer_run(s->level_timer, 1);
        }
    }
    ptimer_transaction_commit(s->level_timer);
}

static void at91_wdt_restart(AT91WDTState *s)
{
    uint32_t period = at91_wdt_period(s);

    s->running = !(s->mode & WDT_MR_WDDIS);
    s->clock_suspended = !clock_get_hz(s->slck);

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    at91_wdt_set_timer_period(s, s->timer);

    /* PERIOD=0 still represents one slow-clock/128 interval. */
    ptimer_set_limit(s->timer, period + 1, 1);
    if (s->running && !s->clock_suspended) {
        ptimer_run(s->timer, 0);
    }
    ptimer_transaction_commit(s->timer);

    at91_wdt_restart_level(s);
}

static void at91_wdt_raise_event(AT91WDTState *s, uint32_t event,
                                 bool generate_reset)
{
    s->isr |= event;
    at91_wdt_update_irq(s);

    if (generate_reset) {
        /*
         * The physical output is consumed by the reset controller.  Use
         * QEMU's watchdog policy until that controller can preserve the
         * SAM9X7 reset cause and processor/peripheral reset distinction.
         */
        qemu_set_irq(s->reset_out, 1);
        watchdog_perform_action();
        if (get_watchdog_action() != WATCHDOG_ACTION_RESET) {
            qemu_set_irq(s->reset_out, 0);
        }
    }
}

static void at91_wdt_period_tick(void *opaque)
{
    AT91WDTState *s = AT91_WDT(opaque);

    /* The periodic ptimer has already reloaded the watchdog counter. */
    at91_wdt_restart_level(s);
    at91_wdt_raise_event(s, WDT_INT_PER,
                         s->mode & WDT_MR_PERIODRST);
}

static void at91_wdt_level_tick(void *opaque)
{
    AT91WDTState *s = AT91_WDT(opaque);

    s->level_running = false;
    at91_wdt_raise_event(s, WDT_INT_LVL, false);
}

static uint32_t at91_wdt_value(AT91WDTState *s)
{
    uint64_t count = ptimer_get_count(s->timer);

    /* The ptimer includes the terminal interval; the register does not. */
    return count ? count - 1 : 0;
}

static bool at91_wdt_in_cr_guard(AT91WDTState *s)
{
    return s->cr_guard_deadline >= 0 &&
           qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) < s->cr_guard_deadline;
}

static void at91_wdt_start_cr_guard(AT91WDTState *s)
{
    uint64_t hz = clock_get_hz(s->slck);
    int64_t delay;

    if (!hz) {
        s->cr_guard_deadline = INT64_MAX;
        return;
    }

    delay = DIV_ROUND_UP(3 * NANOSECONDS_PER_SECOND, hz);
    s->cr_guard_deadline = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay;
}

static void at91_wdt_early_period(AT91WDTState *s)
{
    at91_wdt_restart(s);
    at91_wdt_raise_event(s, WDT_INT_PER,
                         s->mode & WDT_MR_PERIODRST);
}

static uint64_t at91_wdt_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91WDTState *s = AT91_WDT(opaque);
    uint32_t value;

    switch (offset) {
    case WDT_CR:
    case WDT_IER:
    case WDT_IDR:
        return 0;
    case WDT_MR:
        return s->mode;
    case WDT_VR:
        return at91_wdt_value(s);
    case WDT_WLR:
        return s->window;
    case WDT_ILR:
        return s->level;
    case WDT_ISR:
        value = s->isr;
        s->isr = 0;
        at91_wdt_update_irq(s);
        return value;
    case WDT_IMR:
        return s->imr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_WDT ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_wdt_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91WDTState *s = AT91_WDT(opaque);
    bool sequence_failure;
    uint32_t current;
    uint32_t rpth;

    switch (offset) {
    case WDT_CR:
        if ((value & WDT_CR_KEY_MASK) != WDT_CR_KEY) {
            return;
        }
        if (at91_sysc_write_protected(s->sysc, WDT_CR, false, false)) {
            return;
        }

        sequence_failure = at91_wdt_in_cr_guard(s);
        if (value & WDT_CR_LOCKMR) {
            s->locked = true;
        }
        if (value & WDT_CR_WDRSTT) {
            current = at91_wdt_value(s);
            rpth = at91_wdt_rpth(s);
            if (rpth && (rpth > at91_wdt_period(s) ||
                         current > at91_wdt_period(s) - rpth)) {
                at91_wdt_raise_event(s, WDT_INT_RPTH,
                                     s->mode & WDT_MR_RPTHRST);
            }
            at91_wdt_restart(s);
            at91_wdt_start_cr_guard(s);
        }
        if (sequence_failure) {
            at91_wdt_early_period(s);
        }
        break;
    case WDT_MR:
        if (s->locked) {
            return;
        }
        if (at91_sysc_write_protected(s->sysc, WDT_MR, false, false)) {
            return;
        }
        sequence_failure = at91_wdt_in_cr_guard(s);
        s->mode = value & WDT_MR_MASK;
        at91_wdt_restart(s);
        if (sequence_failure) {
            at91_wdt_early_period(s);
        }
        break;
    case WDT_WLR:
        if (!s->locked) {
            s->window = value & WDT_WLR_MASK;
            at91_wdt_restart(s);
        }
        break;
    case WDT_ILR:
        if (!s->locked) {
            s->level = value & WDT_ILR_MASK;
            at91_wdt_restart_level(s);
        }
        break;
    case WDT_IER:
        s->imr |= value & WDT_INT_MASK;
        at91_wdt_update_irq(s);
        break;
    case WDT_IDR:
        s->imr &= ~(value & WDT_INT_MASK);
        at91_wdt_update_irq(s);
        break;
    case WDT_VR:
    case WDT_ISR:
    case WDT_IMR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_WDT ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_WDT ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_wdt_ops = {
    .read = at91_wdt_read,
    .write = at91_wdt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_wdt_clock_changed(void *opaque, ClockEvent event)
{
    AT91WDTState *s = AT91_WDT(opaque);
    bool was_suspended;

    if (!s->timer) {
        return;
    }

    was_suspended = s->clock_suspended;
    s->clock_suspended = !clock_get_hz(s->slck);

    ptimer_transaction_begin(s->timer);
    if (s->clock_suspended) {
        if (s->running && !was_suspended) {
            ptimer_stop(s->timer);
        }
    } else {
        at91_wdt_set_timer_period(s, s->timer);
        if (s->running && was_suspended) {
            ptimer_run(s->timer, 0);
        }
    }
    ptimer_transaction_commit(s->timer);

    ptimer_transaction_begin(s->level_timer);
    if (s->clock_suspended) {
        if (s->level_running && !was_suspended) {
            ptimer_stop(s->level_timer);
        }
    } else {
        at91_wdt_set_timer_period(s, s->level_timer);
        if (s->level_running && was_suspended) {
            ptimer_run(s->level_timer, 1);
        }
    }
    ptimer_transaction_commit(s->level_timer);
}

static void at91_wdt_reset(DeviceState *dev)
{
    AT91WDTState *s = AT91_WDT(dev);

    s->mode = WDT_MR_PERIODRST | WDT_MR_RPTHRST;
    s->window = WDT_WLR_PERIOD_MASK;
    s->level = WDT_ILR_MASK;
    s->imr = 0;
    s->isr = 0;
    s->locked = false;
    s->cr_guard_deadline = -1;
    qemu_set_irq(s->reset_out, 0);
    at91_wdt_update_irq(s);
    at91_wdt_restart(s);
}

static void at91_wdt_init(Object *obj)
{
    AT91WDTState *s = AT91_WDT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_wdt_ops, s,
                          TYPE_AT91_WDT, WDT_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(s), &s->reset_out, "reset", 1);
    s->slck = qdev_init_clock_in(DEVICE(s), "slck",
                                 at91_wdt_clock_changed, s, ClockUpdate);
}

static void at91_wdt_realize(DeviceState *dev, Error **errp)
{
    AT91WDTState *s = AT91_WDT(dev);
    uint8_t policy = PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                     PTIMER_POLICY_NO_COUNTER_ROUND_DOWN;

    if (!clock_has_source(s->slck)) {
        error_setg(errp, TYPE_AT91_WDT ": slck clock must be connected");
        return;
    }

    s->timer = ptimer_init(at91_wdt_period_tick, s, policy);
    s->level_timer = ptimer_init(at91_wdt_level_tick, s, policy);

    ptimer_transaction_begin(s->timer);
    at91_wdt_set_timer_period(s, s->timer);
    ptimer_transaction_commit(s->timer);
    ptimer_transaction_begin(s->level_timer);
    at91_wdt_set_timer_period(s, s->level_timer);
    ptimer_transaction_commit(s->level_timer);
}

static void at91_wdt_finalize(Object *obj)
{
    AT91WDTState *s = AT91_WDT(obj);

    if (s->timer) {
        ptimer_free(s->timer);
    }
    if (s->level_timer) {
        ptimer_free(s->level_timer);
    }
}

static int at91_wdt_post_load(void *opaque, int version_id)
{
    AT91WDTState *s = AT91_WDT(opaque);

    at91_wdt_update_irq(s);
    return 0;
}

static const VMStateDescription at91_wdt_vmstate = {
    .name = TYPE_AT91_WDT,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_wdt_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(slck, AT91WDTState),
        VMSTATE_PTIMER(timer, AT91WDTState),
        VMSTATE_PTIMER(level_timer, AT91WDTState),
        VMSTATE_UINT32(mode, AT91WDTState),
        VMSTATE_UINT32(window, AT91WDTState),
        VMSTATE_UINT32(level, AT91WDTState),
        VMSTATE_UINT32(imr, AT91WDTState),
        VMSTATE_UINT32(isr, AT91WDTState),
        VMSTATE_INT64(cr_guard_deadline, AT91WDTState),
        VMSTATE_BOOL(locked, AT91WDTState),
        VMSTATE_BOOL(running, AT91WDTState),
        VMSTATE_BOOL(level_running, AT91WDTState),
        VMSTATE_BOOL(clock_suspended, AT91WDTState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_wdt_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91WDTState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
};

static void at91_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 watchdog timer";
    dc->realize = at91_wdt_realize;
    dc->vmsd = &at91_wdt_vmstate;
    device_class_set_props(dc, at91_wdt_properties);
    device_class_set_legacy_reset(dc, at91_wdt_reset);
    set_bit(DEVICE_CATEGORY_WATCHDOG, dc->categories);
}

static const TypeInfo at91_wdt_info = {
    .name = TYPE_AT91_WDT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91WDTState),
    .instance_init = at91_wdt_init,
    .instance_finalize = at91_wdt_finalize,
    .class_init = at91_wdt_class_init,
};

static void at91_wdt_register_types(void)
{
    type_register_static(&at91_wdt_info);
}

type_init(at91_wdt_register_types)
