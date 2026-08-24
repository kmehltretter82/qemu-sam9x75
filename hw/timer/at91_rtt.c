/*
 * Microchip AT91 real-time timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/timer/at91_rtt.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define RTT_MR                  0x00
#define RTT_AR                  0x04
#define RTT_VR                  0x08
#define RTT_SR                  0x0c
#define RTT_MODR                0x10
#define RTT_TSR                 0x14
#define RTT_MMIO_SIZE           0x20

#define RTT_MR_RTPRES_MASK      0x0000ffff
#define RTT_MR_ALMIEN           BIT(16)
#define RTT_MR_RTTINCIEN        BIT(17)
#define RTT_MR_RTTRST           BIT(18)
#define RTT_MR_RTTDIS           BIT(20)
#define RTT_MR_INC2AEN          BIT(21)
#define RTT_MR_RTC1HZ           BIT(24)
#define RTT_MR_WRITE_MASK       (RTT_MR_RTPRES_MASK | RTT_MR_ALMIEN | \
                                 RTT_MR_RTTINCIEN | RTT_MR_RTTDIS | \
                                 RTT_MR_INC2AEN | RTT_MR_RTC1HZ)

#define RTT_SR_ALMS             BIT(0)
#define RTT_SR_RTTINC           BIT(1)
#define RTT_SR_RTTINC2          BIT(2)

#define RTT_MODR_SELINC2_MASK   0x7

#define RTT_TSR_TSTAMP_MASK     0x7fffffff
#define RTT_TSR_TS_OVF          BIT(31)

#define RTT_RESET_PRESCALER     0x8000
#define RTT_SYSC_OFFSET         0x20

static uint32_t at91_rtt_prescaler(const AT91RTTState *s)
{
    uint32_t prescaler = s->mr & RTT_MR_RTPRES_MASK;

    return prescaler ? prescaler : 0x10000;
}

static void at91_rtt_update_irq(AT91RTTState *s)
{
    bool level = ((s->sr & RTT_SR_ALMS) && (s->mr & RTT_MR_ALMIEN)) ||
                 ((s->sr & RTT_SR_RTTINC) &&
                  (s->mr & RTT_MR_RTTINCIEN)) ||
                 ((s->sr & RTT_SR_RTTINC2) &&
                  (s->mr & RTT_MR_INC2AEN));

    qemu_set_irq(s->irq, level);
}

static void at91_rtt_increment_counter(AT91RTTState *s)
{
    uint32_t selector;
    uint32_t modulo;

    s->vr++;
    if (s->vr == s->ar) {
        s->sr |= RTT_SR_ALMS;
    }

    selector = s->modr & RTT_MODR_SELINC2_MASK;
    if (!selector) {
        return;
    }

    modulo = 1U << (selector + 5);
    if (!(s->vr & (modulo - 1))) {
        if (s->sr & RTT_SR_RTTINC2) {
            s->tsr |= RTT_TSR_TS_OVF;
        } else {
            s->tsr &= ~RTT_TSR_TS_OVF;
        }
        s->tsr = (s->tsr & RTT_TSR_TS_OVF) |
                 (s->vr & RTT_TSR_TSTAMP_MASK);
        s->sr |= RTT_SR_RTTINC2;
    }
}

static void at91_rtt_prescaler_tick(void *opaque)
{
    AT91RTTState *s = AT91_RTT(opaque);

    s->sr |= RTT_SR_RTTINC;
    if (!(s->mr & RTT_MR_RTC1HZ)) {
        at91_rtt_increment_counter(s);
    }

    /* A programmed prescaler value is loaded at the next roll-over. */
    ptimer_set_limit(s->prescaler_timer, at91_rtt_prescaler(s), 0);
    at91_rtt_update_irq(s);
}

static void at91_rtt_rtc_tick(void *opaque)
{
    AT91RTTState *s = AT91_RTT(opaque);

    at91_rtt_increment_counter(s);
    at91_rtt_update_irq(s);
}

static void at91_rtt_update_prescaler_timer(AT91RTTState *s, bool restart)
{
    ptimer_transaction_begin(s->prescaler_timer);
    if (clock_get_hz(s->slck)) {
        ptimer_set_period_from_clock(s->prescaler_timer, s->slck, 1);
    } else {
        ptimer_set_period(s->prescaler_timer, 1);
    }
    ptimer_set_limit(s->prescaler_timer, at91_rtt_prescaler(s), restart);

    if ((s->mr & RTT_MR_RTTDIS) || s->clock_suspended) {
        ptimer_stop(s->prescaler_timer);
    } else {
        ptimer_run(s->prescaler_timer, 0);
    }
    ptimer_transaction_commit(s->prescaler_timer);
}

static void at91_rtt_update_rtc_timer(AT91RTTState *s, bool restart)
{
    ptimer_transaction_begin(s->rtc_timer);
    ptimer_set_freq(s->rtc_timer, 1);
    ptimer_set_limit(s->rtc_timer, 1, restart);

    if ((s->mr & (RTT_MR_RTTDIS | RTT_MR_RTC1HZ)) == RTT_MR_RTC1HZ) {
        ptimer_run(s->rtc_timer, 0);
    } else {
        ptimer_stop(s->rtc_timer);
    }
    ptimer_transaction_commit(s->rtc_timer);
}

static uint64_t at91_rtt_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91RTTState *s = AT91_RTT(opaque);
    uint32_t value;

    switch (offset) {
    case RTT_MR:
        return s->mr;
    case RTT_AR:
        return s->ar;
    case RTT_VR:
        return s->vr;
    case RTT_SR:
        value = s->sr;
        s->sr = 0;
        s->tsr &= ~RTT_TSR_TS_OVF;
        at91_rtt_update_irq(s);
        return value;
    case RTT_MODR:
        return s->modr;
    case RTT_TSR:
        return s->tsr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RTT ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_rtt_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91RTTState *s = AT91_RTT(opaque);
    bool old_rtc1hz;
    bool restart;

    switch (offset) {
    case RTT_MR:
        if (at91_sysc_write_protected(s->sysc, RTT_SYSC_OFFSET + RTT_MR,
                                      false, true)) {
            return;
        }

        old_rtc1hz = s->mr & RTT_MR_RTC1HZ;
        restart = value & RTT_MR_RTTRST;
        s->mr = value & RTT_MR_WRITE_MASK;
        if (restart) {
            s->vr = 0;
        }
        at91_rtt_update_prescaler_timer(s, restart);
        at91_rtt_update_rtc_timer(s,
                                  old_rtc1hz != !!(s->mr & RTT_MR_RTC1HZ));
        at91_rtt_update_irq(s);
        break;
    case RTT_AR:
        if (!at91_sysc_write_protected(s->sysc,
                                       RTT_SYSC_OFFSET + RTT_AR,
                                       false, true)) {
            s->ar = value;
        }
        break;
    case RTT_MODR:
        if (!at91_sysc_write_protected(s->sysc,
                                       RTT_SYSC_OFFSET + RTT_MODR,
                                       false, true)) {
            s->modr = value & RTT_MODR_SELINC2_MASK;
        }
        break;
    case RTT_VR:
    case RTT_SR:
    case RTT_TSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RTT ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_RTT ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_rtt_ops = {
    .read = at91_rtt_read,
    .write = at91_rtt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_rtt_clock_changed(void *opaque, ClockEvent event)
{
    AT91RTTState *s = AT91_RTT(opaque);

    if (!s->prescaler_timer) {
        return;
    }

    s->clock_suspended = !clock_get_hz(s->slck);
    at91_rtt_update_prescaler_timer(s, false);
}

static void at91_rtt_reset(DeviceState *dev)
{
    AT91RTTState *s = AT91_RTT(dev);

    s->mr = RTT_RESET_PRESCALER;
    s->ar = UINT32_MAX;
    s->vr = 0;
    s->sr = 0;
    s->modr = 0;
    s->tsr = 0;
    s->clock_suspended = !clock_get_hz(s->slck);

    at91_rtt_update_prescaler_timer(s, true);
    at91_rtt_update_rtc_timer(s, true);
    at91_rtt_update_irq(s);
}

static void at91_rtt_init(Object *obj)
{
    AT91RTTState *s = AT91_RTT(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_rtt_ops, s,
                          TYPE_AT91_RTT, RTT_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    s->slck = qdev_init_clock_in(DEVICE(s), "slck",
                                 at91_rtt_clock_changed, s, ClockUpdate);
}

static void at91_rtt_realize(DeviceState *dev, Error **errp)
{
    AT91RTTState *s = AT91_RTT(dev);
    const uint8_t policy = PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN;

    if (!clock_has_source(s->slck)) {
        error_setg(errp, TYPE_AT91_RTT ": slck clock must be connected");
        return;
    }
    if (!s->sysc) {
        error_setg(errp, TYPE_AT91_RTT ": sysc link must be connected");
        return;
    }

    s->prescaler_timer = ptimer_init(at91_rtt_prescaler_tick, s, policy);
    s->rtc_timer = ptimer_init(at91_rtt_rtc_tick, s, policy);
}

static void at91_rtt_finalize(Object *obj)
{
    AT91RTTState *s = AT91_RTT(obj);

    if (s->prescaler_timer) {
        ptimer_free(s->prescaler_timer);
    }
    if (s->rtc_timer) {
        ptimer_free(s->rtc_timer);
    }
}

static int at91_rtt_post_load(void *opaque, int version_id)
{
    AT91RTTState *s = AT91_RTT(opaque);

    at91_rtt_update_irq(s);
    return 0;
}

static const VMStateDescription at91_rtt_vmstate = {
    .name = TYPE_AT91_RTT,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_rtt_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(slck, AT91RTTState),
        VMSTATE_PTIMER(prescaler_timer, AT91RTTState),
        VMSTATE_PTIMER(rtc_timer, AT91RTTState),
        VMSTATE_UINT32(mr, AT91RTTState),
        VMSTATE_UINT32(ar, AT91RTTState),
        VMSTATE_UINT32(vr, AT91RTTState),
        VMSTATE_UINT32(sr, AT91RTTState),
        VMSTATE_UINT32(modr, AT91RTTState),
        VMSTATE_UINT32(tsr, AT91RTTState),
        VMSTATE_BOOL(clock_suspended, AT91RTTState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_rtt_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91RTTState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
};

static void at91_rtt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 real-time timer";
    dc->realize = at91_rtt_realize;
    dc->vmsd = &at91_rtt_vmstate;
    device_class_set_props(dc, at91_rtt_properties);
    device_class_set_legacy_reset(dc, at91_rtt_reset);
}

static const TypeInfo at91_rtt_info = {
    .name = TYPE_AT91_RTT,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91RTTState),
    .instance_init = at91_rtt_init,
    .instance_finalize = at91_rtt_finalize,
    .class_init = at91_rtt_class_init,
};

static void at91_rtt_register_types(void)
{
    type_register_static(&at91_rtt_info);
}

type_init(at91_rtt_register_types)
