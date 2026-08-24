/*
 * Microchip AT91 periodic interval timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/timer/at91_pit.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PIT_MR                  0x00
#define PIT_SR                  0x04
#define PIT_PIVR                0x08
#define PIT_PIIR                0x0c
#define PIT_MMIO_SIZE           0x10

#define PIT_MR_PIV_MASK         0x000fffff
#define PIT_MR_PITEN            BIT(24)
#define PIT_MR_PITIEN           BIT(25)
#define PIT_MR_MASK             (PIT_MR_PIV_MASK | PIT_MR_PITEN | \
                                 PIT_MR_PITIEN)

#define PIT_COUNTER_RANGE       (1U << 20)
#define PIT_PICNT_MASK          0x0fff

static uint32_t at91_pit_piv(AT91PITState *s)
{
    return s->mode & PIT_MR_PIV_MASK;
}

static void at91_pit_update_irq(AT91PITState *s)
{
    qemu_set_irq(s->irq, s->pits && (s->mode & PIT_MR_PITIEN));
}

static void at91_pit_set_timer_period(AT91PITState *s)
{
    if (clock_get_hz(s->mck)) {
        ptimer_set_period_from_clock(s->timer, s->mck, 16);
    } else {
        ptimer_set_period(s->timer, 1);
    }
}

static uint32_t at91_pit_cpiv(AT91PITState *s)
{
    uint64_t remaining;

    if (!s->running) {
        return 0;
    }

    remaining = ptimer_get_count(s->timer);
    return (at91_pit_piv(s) + 1 - remaining) & PIT_MR_PIV_MASK;
}

static uint64_t at91_pit_ticks_to_match(uint32_t cpiv, uint32_t piv)
{
    if (cpiv <= piv) {
        return piv - cpiv + 1;
    }
    return PIT_COUNTER_RANGE - cpiv + piv + 1;
}

static void at91_pit_start_from_zero(AT91PITState *s)
{
    uint64_t period = at91_pit_piv(s) + 1;

    s->running = true;
    s->clock_suspended = !clock_get_hz(s->mck);
    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    at91_pit_set_timer_period(s);
    ptimer_set_limit(s->timer, period, 1);
    if (!s->clock_suspended) {
        ptimer_run(s->timer, 1);
    }
    ptimer_transaction_commit(s->timer);
}

static uint32_t at91_pit_value(AT91PITState *s)
{
    return ((s->picnt & PIT_PICNT_MASK) << 20) | at91_pit_cpiv(s);
}

static uint64_t at91_pit_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91PITState *s = AT91_PIT(opaque);
    uint32_t value;

    switch (offset) {
    case PIT_MR:
        return s->mode;
    case PIT_SR:
        return s->pits;
    case PIT_PIVR:
        value = at91_pit_value(s);
        s->picnt = 0;
        s->pits = false;
        at91_pit_update_irq(s);
        return value;
    case PIT_PIIR:
        return at91_pit_value(s);
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIT ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_pit_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91PITState *s = AT91_PIT(opaque);
    uint32_t cpiv;
    uint64_t remaining;

    switch (offset) {
    case PIT_MR:
        if (at91_sysc_write_protected(s->sysc, PIT_MR + 0x40,
                                      false, true)) {
            return;
        }

        cpiv = at91_pit_cpiv(s);
        s->mode = value & PIT_MR_MASK;
        at91_pit_update_irq(s);

        if (!s->running) {
            if (s->mode & PIT_MR_PITEN) {
                at91_pit_start_from_zero(s);
            }
            return;
        }

        remaining = at91_pit_ticks_to_match(cpiv, at91_pit_piv(s));
        ptimer_transaction_begin(s->timer);
        ptimer_stop(s->timer);
        at91_pit_set_timer_period(s);
        ptimer_set_limit(s->timer, at91_pit_piv(s) + 1, 0);
        ptimer_set_count(s->timer, remaining);
        if (!s->clock_suspended) {
            ptimer_run(s->timer, 1);
        }
        ptimer_transaction_commit(s->timer);
        break;
    case PIT_SR:
    case PIT_PIVR:
    case PIT_PIIR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIT ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIT ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_pit_ops = {
    .read = at91_pit_read,
    .write = at91_pit_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_pit_tick(void *opaque)
{
    AT91PITState *s = AT91_PIT(opaque);

    s->picnt = (s->picnt + 1) & PIT_PICNT_MASK;
    s->pits = true;
    at91_pit_update_irq(s);

    ptimer_set_limit(s->timer, at91_pit_piv(s) + 1, 1);
    if (s->mode & PIT_MR_PITEN) {
        ptimer_run(s->timer, 1);
    } else {
        s->running = false;
    }
}

static void at91_pit_clock_changed(void *opaque, ClockEvent event)
{
    AT91PITState *s = AT91_PIT(opaque);
    bool was_suspended;

    if (!s->timer) {
        return;
    }

    was_suspended = s->clock_suspended;
    s->clock_suspended = !clock_get_hz(s->mck);

    ptimer_transaction_begin(s->timer);
    if (s->clock_suspended) {
        if (s->running && !was_suspended) {
            ptimer_stop(s->timer);
        }
    } else {
        at91_pit_set_timer_period(s);
        if (s->running && was_suspended) {
            ptimer_run(s->timer, 1);
        }
    }
    ptimer_transaction_commit(s->timer);
}

static void at91_pit_reset(DeviceState *dev)
{
    AT91PITState *s = AT91_PIT(dev);

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    at91_pit_set_timer_period(s);
    ptimer_set_limit(s->timer, PIT_COUNTER_RANGE, 1);
    ptimer_transaction_commit(s->timer);

    s->mode = PIT_MR_PIV_MASK;
    s->picnt = 0;
    s->pits = false;
    s->running = false;
    s->clock_suspended = false;
    at91_pit_update_irq(s);
}

static void at91_pit_init(Object *obj)
{
    AT91PITState *s = AT91_PIT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_pit_ops, s,
                          TYPE_AT91_PIT, PIT_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    s->mck = qdev_init_clock_in(DEVICE(s), "mck",
                                at91_pit_clock_changed, s, ClockUpdate);
}

static void at91_pit_realize(DeviceState *dev, Error **errp)
{
    AT91PITState *s = AT91_PIT(dev);

    if (!clock_has_source(s->mck)) {
        error_setg(errp, TYPE_AT91_PIT ": mck clock must be connected");
        return;
    }
    if (!s->sysc) {
        error_setg(errp, TYPE_AT91_PIT ": sysc link must be connected");
        return;
    }

    s->timer = ptimer_init(at91_pit_tick, s,
                           PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
    ptimer_transaction_begin(s->timer);
    at91_pit_set_timer_period(s);
    ptimer_transaction_commit(s->timer);
}

static void at91_pit_finalize(Object *obj)
{
    AT91PITState *s = AT91_PIT(obj);

    if (s->timer) {
        ptimer_free(s->timer);
    }
}

static int at91_pit_post_load(void *opaque, int version_id)
{
    AT91PITState *s = AT91_PIT(opaque);

    at91_pit_update_irq(s);
    return 0;
}

static const VMStateDescription at91_pit_vmstate = {
    .name = TYPE_AT91_PIT,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_pit_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(mck, AT91PITState),
        VMSTATE_PTIMER(timer, AT91PITState),
        VMSTATE_UINT32(mode, AT91PITState),
        VMSTATE_UINT16(picnt, AT91PITState),
        VMSTATE_BOOL(pits, AT91PITState),
        VMSTATE_BOOL(running, AT91PITState),
        VMSTATE_BOOL(clock_suspended, AT91PITState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_pit_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91PITState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
};

static void at91_pit_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 periodic interval timer";
    dc->realize = at91_pit_realize;
    dc->vmsd = &at91_pit_vmstate;
    device_class_set_props(dc, at91_pit_properties);
    device_class_set_legacy_reset(dc, at91_pit_reset);
}

static const TypeInfo at91_pit_info = {
    .name = TYPE_AT91_PIT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91PITState),
    .instance_init = at91_pit_init,
    .instance_finalize = at91_pit_finalize,
    .class_init = at91_pit_class_init,
};

static void at91_pit_register_types(void)
{
    type_register_static(&at91_pit_info);
}

type_init(at91_pit_register_types)
