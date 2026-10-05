/*
 * Microchip SAM9X7 PWM controller
 *
 * SAM9X7 uses the original Atmel PWM register layout with 32-bit counters.
 * DS60001813E chapter 70 still describes 16-bit counters; SAM9X75 hardware
 * measurements and Linux's sam9x60-pwm match data establish the wider size.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/timer/at91_pwm.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"

#define PWM_MR                  0x00
#define PWM_ENA                 0x04
#define PWM_DIS                 0x08
#define PWM_SR                  0x0c
#define PWM_IER                 0x10
#define PWM_IDR                 0x14
#define PWM_IMR                 0x18
#define PWM_ISR                 0x1c
#define PWM_CHANNEL_BASE        0x200
#define PWM_CHANNEL_SIZE        0x20
#define PWM_CMR                 0x00
#define PWM_CDTY                0x04
#define PWM_CPRD                0x08
#define PWM_CCNT                0x0c
#define PWM_CUPD                0x10
#define PWM_MMIO_SIZE           0x300

#define PWM_CHANNEL_MASK        0x0f
#define PWM_MR_MASK             0x0fff0fff
#define PWM_CMR_CPRE            0x0f
#define PWM_CMR_CALG            BIT(8)
#define PWM_CMR_CPOL            BIT(9)
#define PWM_CMR_CPD             BIT(10)
#define PWM_CMR_MASK            0x70f

static void at91_pwm_schedule(AT91PWMChannel *ch);

static unsigned int at91_pwm_index(const AT91PWMChannel *ch)
{
    return ch - ch->owner->channel;
}

static uint32_t at91_pwm_divider(const AT91PWMChannel *ch)
{
    unsigned int pres = ch->cmr & PWM_CMR_CPRE;
    unsigned int div = 1;

    if (pres == 11 || pres == 12) {
        uint32_t mr = ch->owner->mr >> (pres == 11 ? 0 : 16);

        div = mr & 0xff;
        pres = (mr >> 8) & 0xf;
    }
    return pres <= 10 ? div << pres : 0;
}

static uint64_t at91_pwm_length(const AT91PWMChannel *ch)
{
    /* Avoid a zero-length timer for an unconfigured channel. */
    return (uint64_t)MAX(ch->cprd, 1) << !!(ch->cmr & PWM_CMR_CALG);
}

static bool at91_pwm_level(const AT91PWMChannel *ch)
{
    bool active = ch->cdty < ch->cprd && ch->phase >= ch->cdty;

    if ((ch->cmr & PWM_CMR_CALG) && ch->cdty) {
        active &= ch->phase < at91_pwm_length(ch) - ch->cdty;
    }
    return active ^ !!(ch->cmr & PWM_CMR_CPOL);
}

static void at91_pwm_drive(AT91PWMChannel *ch, bool level)
{
    if (ch->output != level) {
        ch->output = level;
        qemu_set_irq(ch->owner->output[at91_pwm_index(ch)], level);
    }
}

static void at91_pwm_update_irq(AT91PWMState *s)
{
    qemu_set_irq(s->irq, (s->isr & s->imr) != 0);
}

/* Advance whole channel ticks, processing a pending update before disabling. */
static void at91_pwm_advance(AT91PWMChannel *ch, uint64_t ticks)
{
    AT91PWMState *s = ch->owner;
    uint64_t length = at91_pwm_length(ch);
    uint64_t remaining = length - ch->phase;

    if (ticks >= remaining) {
        ticks -= remaining;
        ch->phase = 0;
        s->isr |= BIT(at91_pwm_index(ch));
        if (ch->update_pending) {
            if (ch->cmr & PWM_CMR_CPD) {
                ch->cprd = ch->cupd;
            } else {
                ch->cdty = ch->cupd;
            }
            ch->update_pending = false;
            length = at91_pwm_length(ch);
        }
        if (ch->disable_pending) {
            at91_pwm_drive(ch, at91_pwm_level(ch));
            ch->enabled = false;
            ch->disable_pending = false;
            ch->subticks = 0;
            ch->fraction = 0;
            return;
        }
        /* Subsequent complete periods have no further buffered operation. */
        ticks %= length;
    }
    ch->phase += ticks;
    at91_pwm_drive(ch, at91_pwm_level(ch));
}

static void at91_pwm_sync_channel(AT91PWMChannel *ch, int64_t now)
{
    AT91PWMState *s = ch->owner;
    uint32_t divider = at91_pwm_divider(ch);
    uint64_t elapsed, lo, hi, old_lo, ticks;

    /* qtest restores its virtual clock separately after incoming migration. */
    if (now < ch->last_ns) {
        return;
    }
    elapsed = now - ch->last_ns;
    ch->last_ns = now;
    if (!ch->enabled || !divider || !clock_get(s->pclk) || !elapsed) {
        return;
    }

    /* Divide (elapsed * 2^32 + fraction) by the input clock's exact period. */
    lo = elapsed << 32;
    hi = elapsed >> 32;
    old_lo = lo;
    lo += ch->fraction;
    hi += lo < old_lo;
    ch->fraction = divu128(&lo, &hi, s->pclk_period);
    ticks = lo / divider;
    ch->subticks += lo % divider;
    ticks += ch->subticks / divider;
    ch->subticks %= divider;
    at91_pwm_advance(ch, ticks);
}

static void at91_pwm_sync(AT91PWMState *s)
{
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    unsigned int i;

    for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
        at91_pwm_sync_channel(&s->channel[i], now);
    }
    at91_pwm_update_irq(s);
}

static void at91_pwm_schedule(AT91PWMChannel *ch)
{
    AT91PWMState *s = ch->owner;
    uint32_t divider = at91_pwm_divider(ch);
    uint64_t length = at91_pwm_length(ch);
    uint64_t ticks = UINT64_MAX;
    uint64_t lo, hi, cycles, ns;
    int64_t now = MAX(qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL), ch->last_ns);

    timer_del(ch->timer);
    if (!ch->enabled || !divider || !clock_get(s->pclk)) {
        return;
    }
    if (ch->update_pending || ch->disable_pending ||
        ((s->imr & ~s->isr) & BIT(at91_pwm_index(ch)))) {
        ticks = length - ch->phase;
    }
    if (ch->cdty && ch->cdty < ch->cprd) {
        uint64_t edge;

        if (ch->phase < ch->cdty) {
            edge = ch->cdty - ch->phase;
        } else if (!(ch->cmr & PWM_CMR_CALG)) {
            edge = length - ch->phase;
        } else if (ch->phase < length - ch->cdty) {
            edge = length - ch->cdty - ch->phase;
        } else {
            edge = length - ch->phase + ch->cdty;
        }
        ticks = MIN(ticks, edge);
    }
    /* Constant outputs with no outstanding operation can advance lazily. */
    if (ticks == UINT64_MAX) {
        return;
    }

    cycles = ticks * divider - ch->subticks;
    mulu64(&lo, &hi, cycles, s->pclk_period);
    if (lo < ch->fraction) {
        hi--;
    }
    lo -= ch->fraction;
    if (hi >> 31) {
        ns = INT64_MAX;
    } else {
        ns = (hi << 32) | (lo >> 32);
        ns += (uint32_t)lo != 0; /* Do not fire before the hardware edge. */
    }
    ns = MIN(MAX(ns, 1), (uint64_t)(INT64_MAX - now));
    timer_mod(ch->timer, now + ns);
}

static void at91_pwm_schedule_all(AT91PWMState *s)
{
    unsigned int i;

    for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
        at91_pwm_schedule(&s->channel[i]);
    }
}

static void at91_pwm_expire(void *opaque)
{
    AT91PWMChannel *ch = opaque;

    at91_pwm_sync_channel(ch, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    at91_pwm_update_irq(ch->owner);
    at91_pwm_schedule(ch);
}

static uint64_t at91_pwm_read(void *opaque, hwaddr offset, unsigned int size)
{
    AT91PWMState *s = AT91_PWM(opaque);
    uint32_t value = 0;
    unsigned int i;

    at91_pwm_sync(s);
    if (offset >= PWM_CHANNEL_BASE &&
        offset < PWM_CHANNEL_BASE + PWM_CHANNEL_SIZE * AT91_PWM_NUM_CHANNELS) {
        AT91PWMChannel *ch = &s->channel[(offset - PWM_CHANNEL_BASE) /
                                       PWM_CHANNEL_SIZE];

        switch (offset % PWM_CHANNEL_SIZE) {
        case PWM_CMR:
            value = ch->cmr;
            break;
        case PWM_CDTY:
            value = ch->cdty;
            break;
        case PWM_CPRD:
            value = ch->cprd;
            break;
        case PWM_CCNT:
            value = ch->phase;
            if ((ch->cmr & PWM_CMR_CALG) && ch->phase > ch->cprd) {
                value = at91_pwm_length(ch) - ch->phase;
            }
            break;
        case PWM_CUPD:
            /* Write-only; zero also matches the captured SAM9X75 reads. */
            break;
        default:
            break;
        }
    } else {
        switch (offset) {
        case PWM_MR:
            value = s->mr;
            break;
        case PWM_SR:
            for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
                value |= s->channel[i].enabled << i;
            }
            break;
        case PWM_IMR:
            value = s->imr;
            break;
        case PWM_ISR:
            value = s->isr;
            s->isr = 0;
            at91_pwm_update_irq(s);
            break;
        default:
            break;
        }
    }
    at91_pwm_schedule_all(s);
    return value;
}

static void at91_pwm_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91PWMState *s = AT91_PWM(opaque);
    unsigned int i;

    at91_pwm_sync(s);
    if (offset >= PWM_CHANNEL_BASE &&
        offset < PWM_CHANNEL_BASE + PWM_CHANNEL_SIZE * AT91_PWM_NUM_CHANNELS) {
        AT91PWMChannel *ch = &s->channel[(offset - PWM_CHANNEL_BASE) /
                                       PWM_CHANNEL_SIZE];

        switch (offset % PWM_CHANNEL_SIZE) {
        case PWM_CMR:
            value &= PWM_CMR_MASK;
            if (ch->enabled) {
                /* Polarity and alignment can change only while stopped. */
                value = (value & ~(PWM_CMR_CPOL | PWM_CMR_CALG)) |
                        (ch->cmr & (PWM_CMR_CPOL | PWM_CMR_CALG));
            } else if ((ch->cmr ^ value) & PWM_CMR_CPOL) {
                at91_pwm_drive(ch, value & PWM_CMR_CPOL);
            }
            if ((ch->cmr ^ value) & PWM_CMR_CPRE) {
                ch->subticks = 0;
                ch->fraction = 0;
            }
            ch->cmr = value;
            break;
        case PWM_CDTY:
            if (!ch->enabled) {
                ch->cdty = value;
            }
            break;
        case PWM_CPRD:
            if (!ch->enabled) {
                ch->cprd = value;
                ch->phase = 0;
            }
            break;
        case PWM_CUPD:
            ch->cupd = value;
            ch->update_pending = true;
            break;
        default:
            break;
        }
    } else {
        switch (offset) {
        case PWM_MR:
            s->mr = value & PWM_MR_MASK;
            for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
                /* Reprogramming shared dividers starts their new division. */
                if ((s->channel[i].cmr & PWM_CMR_CPRE) >= 11) {
                    s->channel[i].subticks = 0;
                    s->channel[i].fraction = 0;
                }
            }
            break;
        case PWM_ENA:
            for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
                AT91PWMChannel *ch = &s->channel[i];

                if ((value & BIT(i)) && !ch->enabled) {
                    ch->enabled = true;
                    ch->disable_pending = false;
                    ch->phase = 0;
                    ch->subticks = 0;
                    ch->fraction = 0;
                    ch->last_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
                    at91_pwm_drive(ch, at91_pwm_level(ch));
                }
            }
            break;
        case PWM_DIS:
            for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
                if ((value & BIT(i)) && s->channel[i].enabled) {
                    s->channel[i].disable_pending = true;
                }
            }
            break;
        case PWM_IER:
            s->imr |= value & PWM_CHANNEL_MASK;
            break;
        case PWM_IDR:
            s->imr &= ~value;
            break;
        default:
            break;
        }
    }
    at91_pwm_update_irq(s);
    at91_pwm_schedule_all(s);
}

static const MemoryRegionOps at91_pwm_ops = {
    .read = at91_pwm_read,
    .write = at91_pwm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_pwm_clock_changed(void *opaque, ClockEvent event)
{
    AT91PWMState *s = AT91_PWM(opaque);
    uint64_t period = clock_get(s->pclk);
    unsigned int i;

    if (!s->channel[0].timer) {
        return;
    }
    if (event == ClockPreUpdate) {
        at91_pwm_sync(s);
        return;
    }
    if (period) {
        for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
            AT91PWMChannel *ch = &s->channel[i];

            if (s->pclk_period) {
                uint64_t lo, hi;

                mulu64(&lo, &hi, ch->fraction, period);
                divu128(&lo, &hi, s->pclk_period);
                ch->fraction = lo;
            }
        }
        s->pclk_period = period;
    }
    for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
        s->channel[i].last_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    }
    at91_pwm_schedule_all(s);
}

static void at91_pwm_reset(DeviceState *dev)
{
    AT91PWMState *s = AT91_PWM(dev);
    unsigned int i;

    s->mr = 0;
    s->imr = 0;
    s->isr = 0;
    s->pclk_period = clock_get(s->pclk);
    for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
        AT91PWMChannel *ch = &s->channel[i];

        timer_del(ch->timer);
        ch->cmr = 0;
        ch->cdty = 0;
        ch->cprd = 0;
        ch->cupd = 0;
        ch->phase = 0;
        ch->subticks = 0;
        ch->fraction = 0;
        ch->enabled = false;
        ch->disable_pending = false;
        ch->update_pending = false;
        ch->output = false;
        ch->last_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        qemu_set_irq(s->output[i], 0);
    }
    at91_pwm_update_irq(s);
}

static void at91_pwm_init(Object *obj)
{
    AT91PWMState *s = AT91_PWM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_pwm_ops, s,
                          TYPE_AT91_PWM, PWM_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(s), s->output, "pwm",
                             AT91_PWM_NUM_CHANNELS);
    s->pclk = qdev_init_clock_in(DEVICE(s), "pclk", at91_pwm_clock_changed,
                                 s, ClockPreUpdate | ClockUpdate);
}

static void at91_pwm_realize(DeviceState *dev, Error **errp)
{
    AT91PWMState *s = AT91_PWM(dev);
    unsigned int i;

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_PWM ": pclk clock must be connected");
        return;
    }
    for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
        AT91PWMChannel *ch = &s->channel[i];

        ch->owner = s;
        ch->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, at91_pwm_expire, ch);
    }
}

static void at91_pwm_finalize(Object *obj)
{
    AT91PWMState *s = AT91_PWM(obj);
    unsigned int i;

    for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
        if (s->channel[i].timer) {
            timer_free(s->channel[i].timer);
        }
    }
}

static int at91_pwm_pre_save(void *opaque)
{
    at91_pwm_sync(AT91_PWM(opaque));
    return 0;
}

static int at91_pwm_post_load(void *opaque, int version_id)
{
    AT91PWMState *s = AT91_PWM(opaque);
    unsigned int i;

    if ((s->mr & ~PWM_MR_MASK) ||
        ((s->imr | s->isr) & ~PWM_CHANNEL_MASK)) {
        return -EINVAL;
    }
    for (i = 0; i < AT91_PWM_NUM_CHANNELS; i++) {
        AT91PWMChannel *ch = &s->channel[i];
        uint32_t divider = at91_pwm_divider(ch);

        if ((ch->cmr & ~PWM_CMR_MASK) || ch->phase >= at91_pwm_length(ch) ||
            (divider && ch->subticks >= divider) ||
            (s->pclk_period && ch->fraction >= s->pclk_period) ||
            (!ch->enabled && ch->disable_pending)) {
            return -EINVAL;
        }
        qemu_set_irq(s->output[i], ch->output);
    }
    at91_pwm_update_irq(s);
    at91_pwm_schedule_all(s);
    return 0;
}

static const VMStateDescription at91_pwm_channel_vmstate = {
    .name = TYPE_AT91_PWM "/channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cmr, AT91PWMChannel),
        VMSTATE_UINT32(cdty, AT91PWMChannel),
        VMSTATE_UINT32(cprd, AT91PWMChannel),
        VMSTATE_UINT32(cupd, AT91PWMChannel),
        VMSTATE_BOOL(enabled, AT91PWMChannel),
        VMSTATE_BOOL(update_pending, AT91PWMChannel),
        VMSTATE_BOOL(disable_pending, AT91PWMChannel),
        VMSTATE_BOOL(output, AT91PWMChannel),
        VMSTATE_UINT64(phase, AT91PWMChannel),
        VMSTATE_UINT32(subticks, AT91PWMChannel),
        VMSTATE_UINT64(fraction, AT91PWMChannel),
        VMSTATE_INT64(last_ns, AT91PWMChannel),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription at91_pwm_vmstate = {
    .name = TYPE_AT91_PWM,
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = at91_pwm_pre_save,
    .post_load = at91_pwm_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, AT91PWMState),
        VMSTATE_UINT64(pclk_period, AT91PWMState),
        VMSTATE_UINT32(mr, AT91PWMState),
        VMSTATE_UINT32(imr, AT91PWMState),
        VMSTATE_UINT32(isr, AT91PWMState),
        VMSTATE_STRUCT_ARRAY(channel, AT91PWMState, AT91_PWM_NUM_CHANNELS,
                              0, at91_pwm_channel_vmstate, AT91PWMChannel),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_pwm_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 32-bit PWM controller";
    dc->realize = at91_pwm_realize;
    dc->vmsd = &at91_pwm_vmstate;
    device_class_set_legacy_reset(dc, at91_pwm_reset);
}

static const TypeInfo at91_pwm_types[] = {
    {
        .name = TYPE_AT91_PWM,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AT91PWMState),
        .instance_init = at91_pwm_init,
        .instance_finalize = at91_pwm_finalize,
        .class_init = at91_pwm_class_init,
    },
};

DEFINE_TYPES(at91_pwm_types)
