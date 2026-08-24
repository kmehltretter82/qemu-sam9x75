/*
 * Microchip AT91 64-bit Periodic Interval Timer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/timer/at91_pit64b.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "trace.h"

#define PIT64B_CR       0x00
#define PIT64B_MR       0x04
#define PIT64B_LSB_PR   0x08
#define PIT64B_MSB_PR   0x0c
#define PIT64B_IER      0x10
#define PIT64B_IDR      0x14
#define PIT64B_IMR      0x18
#define PIT64B_ISR      0x1c
#define PIT64B_TLSBR    0x20
#define PIT64B_TMSBR    0x24
#define PIT64B_WPMR     0xe4
#define PIT64B_WPSR     0xe8

#define PIT64B_MMIO_SIZE        0x100

#define PIT64B_CR_START         BIT(0)
#define PIT64B_CR_SWRST         BIT(8)

#define PIT64B_MR_CONT          BIT(0)
#define PIT64B_MR_SGCLK         BIT(3)
#define PIT64B_MR_SMOD          BIT(4)
#define PIT64B_MR_PRES_MASK     0x00000f00
#define PIT64B_MR_MASK          (PIT64B_MR_CONT | PIT64B_MR_SGCLK | \
                                 PIT64B_MR_SMOD | PIT64B_MR_PRES_MASK)

#define PIT64B_INT_PERIOD       BIT(0)
#define PIT64B_INT_OVRE         BIT(1)
#define PIT64B_INT_SECE         BIT(4)
#define PIT64B_INT_MASK         (PIT64B_INT_PERIOD | PIT64B_INT_OVRE | \
                                 PIT64B_INT_SECE)

#define PIT64B_WPMR_WPEN        BIT(0)
#define PIT64B_WPMR_WPITEN      BIT(1)
#define PIT64B_WPMR_WPCREN      BIT(2)
#define PIT64B_WPMR_FIRSTE      BIT(4)
#define PIT64B_WPMR_ENABLE_MASK 0x00000017
#define PIT64B_WPMR_KEY_MASK    0xffffff00
#define PIT64B_WPMR_KEY         0x50495400

#define PIT64B_WPSR_WPVS        BIT(0)
#define PIT64B_WPSR_SEQE        BIT(2)

static void at91_pit64b_update_irq(AT91PIT64BState *s)
{
    qemu_set_irq(s->irq, !!(s->isr & s->imr));
}

static uint64_t at91_pit64b_period(AT91PIT64BState *s)
{
    uint64_t period = ((uint64_t)s->msb_period << 32) | s->lsb_period;

    /* A zero period represents the full 64-bit counter range. */
    return period ? period : UINT64_MAX;
}

static Clock *at91_pit64b_selected_clock(AT91PIT64BState *s)
{
    return (s->mode & PIT64B_MR_SGCLK) ? s->gclk : s->pclk;
}

static unsigned int at91_pit64b_clock_divider(AT91PIT64BState *s)
{
    return extract32(s->mode, 8, 4) + 1;
}

static void at91_pit64b_set_clock_period(AT91PIT64BState *s)
{
    Clock *clk = at91_pit64b_selected_clock(s);

    if (clock_get_hz(clk)) {
        ptimer_set_period_from_clock(s->timer, clk,
                                     at91_pit64b_clock_divider(s));
    } else {
        /* Keep a valid ptimer period while the selected clock is gated. */
        ptimer_set_period(s->timer, 1);
    }
}

static void at91_pit64b_start(AT91PIT64BState *s)
{
    Clock *clk = at91_pit64b_selected_clock(s);

    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    at91_pit64b_set_clock_period(s);
    ptimer_set_limit(s->timer, at91_pit64b_period(s), 1);
    s->running = true;
    s->clock_suspended = !clock_get_hz(clk);
    if (!s->clock_suspended) {
        ptimer_run(s->timer, !(s->mode & PIT64B_MR_CONT));
    }
    ptimer_transaction_commit(s->timer);
}

static void at91_pit64b_stop_and_clear(AT91PIT64BState *s)
{
    ptimer_transaction_begin(s->timer);
    ptimer_stop(s->timer);
    ptimer_set_limit(s->timer, UINT64_MAX, 1);
    ptimer_transaction_commit(s->timer);

    s->mode = 0;
    s->lsb_period = 0;
    s->msb_period = 0;
    s->imr = 0;
    s->isr = 0;
    s->latched_msb = 0;
    s->running = false;
    s->clock_suspended = false;
    at91_pit64b_update_irq(s);
}

static void at91_pit64b_error(AT91PIT64BState *s, hwaddr offset,
                              uint32_t error)
{
    if (!(s->wpmr & PIT64B_WPMR_FIRSTE) || !s->wpsr) {
        s->wpsr = error | ((offset & 0xffff) << 8);
    }
    if (error & PIT64B_WPSR_SEQE) {
        s->isr |= PIT64B_INT_SECE;
        at91_pit64b_update_irq(s);
    }
}

static bool at91_pit64b_write_protected(AT91PIT64BState *s, hwaddr offset)
{
    bool protect = false;

    if (offset == PIT64B_CR) {
        protect = s->wpmr & PIT64B_WPMR_WPCREN;
    } else if (offset == PIT64B_IER || offset == PIT64B_IDR) {
        protect = s->wpmr & PIT64B_WPMR_WPITEN;
    } else if (offset == PIT64B_MR || offset == PIT64B_LSB_PR ||
               offset == PIT64B_MSB_PR) {
        protect = s->wpmr & PIT64B_WPMR_WPEN;
    }

    if (protect) {
        at91_pit64b_error(s, offset, PIT64B_WPSR_WPVS);
    }
    return protect;
}

static uint64_t at91_pit64b_counter(AT91PIT64BState *s)
{
    uint64_t remaining = ptimer_get_count(s->timer);
    uint64_t period = at91_pit64b_period(s);

    /* The hardware is an up-counter; the ptimer backing it counts down. */
    return remaining ? period - remaining : 0;
}

static uint64_t at91_pit64b_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    AT91PIT64BState *s = AT91_PIT64B(opaque);
    uint64_t counter;
    uint32_t value;

    switch (offset) {
    case PIT64B_CR:
        return 0;
    case PIT64B_MR:
        return s->mode;
    case PIT64B_LSB_PR:
        return s->lsb_period;
    case PIT64B_MSB_PR:
        return s->msb_period;
    case PIT64B_IER:
    case PIT64B_IDR:
        return 0;
    case PIT64B_IMR:
        return s->imr;
    case PIT64B_ISR:
        value = s->isr;
        s->isr = 0;
        at91_pit64b_update_irq(s);
        return value;
    case PIT64B_TLSBR:
        counter = at91_pit64b_counter(s);
        s->latched_msb = counter >> 32;
        return (uint32_t)counter;
    case PIT64B_TMSBR:
        return s->latched_msb;
    case PIT64B_WPMR:
        return s->wpmr;
    case PIT64B_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIT64B ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_pit64b_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    AT91PIT64BState *s = AT91_PIT64B(opaque);

    trace_at91_pit64b_write(offset, value);

    if (offset != PIT64B_WPMR &&
        at91_pit64b_write_protected(s, offset)) {
        return;
    }

    switch (offset) {
    case PIT64B_CR:
        if (value & PIT64B_CR_SWRST) {
            at91_pit64b_stop_and_clear(s);
        } else if (value & PIT64B_CR_START) {
            at91_pit64b_start(s);
        }
        break;
    case PIT64B_MR:
        if (s->running) {
            at91_pit64b_error(s, offset, PIT64B_WPSR_SEQE);
            break;
        }
        s->mode = value & PIT64B_MR_MASK;
        ptimer_transaction_begin(s->timer);
        at91_pit64b_set_clock_period(s);
        ptimer_transaction_commit(s->timer);
        break;
    case PIT64B_LSB_PR:
        if (s->running && !(s->mode & PIT64B_MR_SMOD)) {
            at91_pit64b_error(s, offset, PIT64B_WPSR_SEQE);
            break;
        }
        s->lsb_period = value;
        if (s->running && (s->mode & PIT64B_MR_SMOD)) {
            at91_pit64b_start(s);
        }
        break;
    case PIT64B_MSB_PR:
        if (s->running && !(s->mode & PIT64B_MR_SMOD)) {
            at91_pit64b_error(s, offset, PIT64B_WPSR_SEQE);
            break;
        }
        s->msb_period = value;
        break;
    case PIT64B_IER:
        s->imr |= value & PIT64B_INT_MASK;
        at91_pit64b_update_irq(s);
        break;
    case PIT64B_IDR:
        s->imr &= ~(value & PIT64B_INT_MASK);
        at91_pit64b_update_irq(s);
        break;
    case PIT64B_WPMR:
        if ((value & PIT64B_WPMR_KEY_MASK) == PIT64B_WPMR_KEY) {
            s->wpmr = value & PIT64B_WPMR_ENABLE_MASK;
        }
        break;
    case PIT64B_IMR:
    case PIT64B_ISR:
    case PIT64B_TLSBR:
    case PIT64B_TMSBR:
    case PIT64B_WPSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIT64B ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIT64B ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_pit64b_ops = {
    .read = at91_pit64b_read,
    .write = at91_pit64b_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_pit64b_tick(void *opaque)
{
    AT91PIT64BState *s = AT91_PIT64B(opaque);

    if (s->isr & PIT64B_INT_PERIOD) {
        s->isr |= PIT64B_INT_OVRE;
    }
    s->isr |= PIT64B_INT_PERIOD;
    if (!(s->mode & PIT64B_MR_CONT)) {
        s->running = false;
    }
    at91_pit64b_update_irq(s);
}

static void at91_pit64b_clock_changed(void *opaque, ClockEvent event)
{
    AT91PIT64BState *s = AT91_PIT64B(opaque);
    Clock *clk;

    if (!s->timer) {
        return;
    }

    clk = at91_pit64b_selected_clock(s);
    ptimer_transaction_begin(s->timer);
    if (!clock_get_hz(clk)) {
        if (s->running && !s->clock_suspended) {
            ptimer_stop(s->timer);
            s->clock_suspended = true;
        }
    } else {
        at91_pit64b_set_clock_period(s);
        if (s->running && s->clock_suspended) {
            ptimer_run(s->timer, !(s->mode & PIT64B_MR_CONT));
            s->clock_suspended = false;
        }
    }
    ptimer_transaction_commit(s->timer);
}

static void at91_pit64b_reset(DeviceState *dev)
{
    AT91PIT64BState *s = AT91_PIT64B(dev);

    at91_pit64b_stop_and_clear(s);
    s->wpmr = 0;
    s->wpsr = 0;
}

static void at91_pit64b_init(Object *obj)
{
    AT91PIT64BState *s = AT91_PIT64B(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_pit64b_ops, s,
                          TYPE_AT91_PIT64B, PIT64B_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);

    s->pclk = qdev_init_clock_in(DEVICE(s), "pclk",
                                 at91_pit64b_clock_changed, s, ClockUpdate);
    s->gclk = qdev_init_clock_in(DEVICE(s), "gclk",
                                 at91_pit64b_clock_changed, s, ClockUpdate);
}

static void at91_pit64b_realize(DeviceState *dev, Error **errp)
{
    AT91PIT64BState *s = AT91_PIT64B(dev);

    if (!clock_has_source(s->pclk) || !clock_has_source(s->gclk)) {
        error_setg(errp, TYPE_AT91_PIT64B
                   ": pclk and gclk clocks must be connected");
        return;
    }

    s->timer = ptimer_init(at91_pit64b_tick, s,
                           PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
                           PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                           PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
                           PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
    ptimer_transaction_begin(s->timer);
    at91_pit64b_set_clock_period(s);
    ptimer_transaction_commit(s->timer);
}

static void at91_pit64b_finalize(Object *obj)
{
    AT91PIT64BState *s = AT91_PIT64B(obj);

    if (s->timer) {
        ptimer_free(s->timer);
    }
}

static int at91_pit64b_post_load(void *opaque, int version_id)
{
    AT91PIT64BState *s = AT91_PIT64B(opaque);

    at91_pit64b_update_irq(s);
    return 0;
}

static const VMStateDescription at91_pit64b_vmstate = {
    .name = TYPE_AT91_PIT64B,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_pit64b_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, AT91PIT64BState),
        VMSTATE_CLOCK(gclk, AT91PIT64BState),
        VMSTATE_PTIMER(timer, AT91PIT64BState),
        VMSTATE_UINT32(mode, AT91PIT64BState),
        VMSTATE_UINT32(lsb_period, AT91PIT64BState),
        VMSTATE_UINT32(msb_period, AT91PIT64BState),
        VMSTATE_UINT32(imr, AT91PIT64BState),
        VMSTATE_UINT32(isr, AT91PIT64BState),
        VMSTATE_UINT32(wpmr, AT91PIT64BState),
        VMSTATE_UINT32(wpsr, AT91PIT64BState),
        VMSTATE_UINT32(latched_msb, AT91PIT64BState),
        VMSTATE_BOOL(running, AT91PIT64BState),
        VMSTATE_BOOL(clock_suspended, AT91PIT64BState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_pit64b_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 64-bit Periodic Interval Timer";
    dc->realize = at91_pit64b_realize;
    dc->vmsd = &at91_pit64b_vmstate;
    device_class_set_legacy_reset(dc, at91_pit64b_reset);
}

static const TypeInfo at91_pit64b_info = {
    .name = TYPE_AT91_PIT64B,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91PIT64BState),
    .instance_init = at91_pit64b_init,
    .instance_finalize = at91_pit64b_finalize,
    .class_init = at91_pit64b_class_init,
};

static void at91_pit64b_register_types(void)
{
    type_register_static(&at91_pit64b_info);
}

type_init(at91_pit64b_register_types)
