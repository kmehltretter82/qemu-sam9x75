/*
 * Microchip AT91 True Random Number Generator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/misc/at91_trng.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/guest-random.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TRNG_CR                 0x00
#define TRNG_MR                 0x04
#define TRNG_PKBCR              0x08
#define TRNG_IER                0x10
#define TRNG_IDR                0x14
#define TRNG_IMR                0x18
#define TRNG_ISR                0x1c
#define TRNG_ODATA              0x50
#define TRNG_WPMR               0xe4
#define TRNG_WPSR               0xe8
#define TRNG_MMIO_SIZE          0x100

#define TRNG_CR_KEY_MASK        0xffffff00
#define TRNG_CR_KEY             0x524e4700
#define TRNG_CR_ENABLE          BIT(0)

#define TRNG_MR_DIFF            BIT(7)
#define TRNG_MR_HALFR           BIT(0)
#define TRNG_MR_MASK            (TRNG_MR_DIFF | TRNG_MR_HALFR)

#define TRNG_PKBCR_KEY_MASK     0xffff0000
#define TRNG_PKBCR_KEY          0x524b0000

#define TRNG_INT_DATRDY         BIT(0)
#define TRNG_INT_SECE           BIT(1)
#define TRNG_INT_EOTPKB         BIT(2)
#define TRNG_INT_MASK           (TRNG_INT_DATRDY | TRNG_INT_SECE | \
                                 TRNG_INT_EOTPKB)

#define TRNG_WPMR_KEY_MASK      0xffffff00
#define TRNG_WPMR_KEY           0x524e4700
#define TRNG_WPMR_WPEN          BIT(0)
#define TRNG_WPMR_WPITEN        BIT(1)
#define TRNG_WPMR_WPCREN        BIT(2)
#define TRNG_WPMR_FIRSTE        BIT(4)
#define TRNG_WPMR_MASK          (TRNG_WPMR_WPEN | TRNG_WPMR_WPITEN | \
                                 TRNG_WPMR_WPCREN | TRNG_WPMR_FIRSTE)

#define TRNG_WPSR_WPVS          BIT(0)
#define TRNG_WPSR_SWE           BIT(3)
#define TRNG_WPSR_WPVSRC_SHIFT  8
#define TRNG_WPSR_SWETYP_SHIFT  24
#define TRNG_WPSR_ECLASS        BIT(31)

#define TRNG_SWE_READ_WO        0
#define TRNG_SWE_WRITE_RO       1
#define TRNG_SWE_UNDEF_RW       2
#define TRNG_SWE_TRNG_DIS       3

static void at91_trng_update_irq(AT91TRNGState *s)
{
    qemu_set_irq(s->irq, !!(s->isr & s->imr & TRNG_INT_MASK));
}

static void at91_trng_schedule(AT91TRNGState *s)
{
    uint64_t cycles;
    uint64_t duration;

    if (!s->enabled || (s->isr & TRNG_INT_DATRDY) ||
        timer_pending(s->generation_timer) || !clock_get_hz(s->pclk)) {
        return;
    }
    cycles = s->mr & TRNG_MR_HALFR ? 168 : 84;
    duration = MAX(clock_ticks_to_ns(s->pclk, cycles), 1);
    timer_mod_ns(s->generation_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration);
}

static void at91_trng_raise_swe(AT91TRNGState *s, hwaddr offset,
                                unsigned int type, bool error)
{
    bool first_only = s->wpmr & TRNG_WPMR_FIRSTE;

    if (!first_only || !(s->wpsr & TRNG_WPSR_SWE)) {
        s->wpsr &= ~(TRNG_WPSR_ECLASS |
                      (0xfU << TRNG_WPSR_SWETYP_SHIFT));
        s->wpsr |= TRNG_WPSR_SWE |
                   (type << TRNG_WPSR_SWETYP_SHIFT);
        if (!(s->wpsr & TRNG_WPSR_WPVS)) {
            s->wpsr &= ~(0xffffU << TRNG_WPSR_WPVSRC_SHIFT);
            s->wpsr |= (offset & 0xffff) << TRNG_WPSR_WPVSRC_SHIFT;
        }
        if (error) {
            s->wpsr |= TRNG_WPSR_ECLASS;
        }
    }
    s->isr |= TRNG_INT_SECE;
    at91_trng_update_irq(s);
}

static bool at91_trng_write_protected(AT91TRNGState *s, hwaddr offset,
                                      uint32_t enable)
{
    bool first_only = s->wpmr & TRNG_WPMR_FIRSTE;

    if (!(s->wpmr & enable)) {
        return false;
    }
    if (!first_only || !(s->wpsr & TRNG_WPSR_WPVS)) {
        s->wpsr &= ~(0xffffU << TRNG_WPSR_WPVSRC_SHIFT);
        s->wpsr |= TRNG_WPSR_WPVS |
                   ((offset & 0xffff) << TRNG_WPSR_WPVSRC_SHIFT);
    }
    s->isr |= TRNG_INT_SECE;
    at91_trng_update_irq(s);
    return true;
}

static void at91_trng_generate(void *opaque)
{
    AT91TRNGState *s = opaque;
    uint32_t value;
    unsigned int attempts = 0;

    if (!s->enabled || !clock_get_hz(s->pclk)) {
        return;
    }
    do {
        qemu_guest_getrandom_nofail(&value, sizeof(value));
    } while ((s->mr & TRNG_MR_DIFF) && s->previous_valid &&
             value == s->odata && ++attempts < 16);
    if ((s->mr & TRNG_MR_DIFF) && s->previous_valid && value == s->odata) {
        value ^= 1;
    }
    s->odata = value;
    s->data_valid = true;
    s->previous_valid = true;
    s->isr |= TRNG_INT_DATRDY;
    at91_trng_update_irq(s);
}

static uint64_t at91_trng_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91TRNGState *s = AT91_TRNG(opaque);
    uint32_t value;

    switch (offset) {
    case TRNG_MR:
        return s->mr;
    case TRNG_IMR:
        return s->imr;
    case TRNG_ISR:
        value = s->isr;
        s->isr = 0;
        at91_trng_update_irq(s);
        at91_trng_schedule(s);
        return value;
    case TRNG_ODATA:
        if (!s->enabled || !s->data_valid) {
            at91_trng_raise_swe(s, offset, TRNG_SWE_TRNG_DIS, true);
            return 0;
        }
        if (!(s->isr & TRNG_INT_DATRDY)) {
            s->data_valid = false;
        }
        return s->odata;
    case TRNG_WPMR:
        return s->wpmr;
    case TRNG_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case TRNG_CR:
    case TRNG_PKBCR:
    case TRNG_IER:
    case TRNG_IDR:
        if (s->enabled) {
            at91_trng_raise_swe(s, offset, TRNG_SWE_READ_WO, false);
        }
        return 0;
    default:
        at91_trng_raise_swe(s, offset, TRNG_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TRNG ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_trng_set_enabled(AT91TRNGState *s, bool enabled)
{
    if (s->enabled == enabled) {
        return;
    }
    s->enabled = enabled;
    if (enabled) {
        at91_trng_schedule(s);
    } else {
        timer_del(s->generation_timer);
        s->isr &= ~TRNG_INT_DATRDY;
        s->data_valid = false;
        at91_trng_update_irq(s);
    }
}

static void at91_trng_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91TRNGState *s = AT91_TRNG(opaque);
    uint32_t val = value;

    if (offset == TRNG_MR &&
        at91_trng_write_protected(s, offset, TRNG_WPMR_WPEN)) {
        return;
    }
    if ((offset == TRNG_IER || offset == TRNG_IDR) &&
        at91_trng_write_protected(s, offset, TRNG_WPMR_WPITEN)) {
        return;
    }
    if ((offset == TRNG_CR || offset == TRNG_PKBCR) &&
        at91_trng_write_protected(s, offset, TRNG_WPMR_WPCREN)) {
        return;
    }

    switch (offset) {
    case TRNG_CR:
        if ((val & TRNG_CR_KEY_MASK) == TRNG_CR_KEY) {
            at91_trng_set_enabled(s, val & TRNG_CR_ENABLE);
        }
        break;
    case TRNG_MR:
        s->mr = val & TRNG_MR_MASK;
        if (timer_pending(s->generation_timer)) {
            timer_del(s->generation_timer);
            at91_trng_schedule(s);
        }
        break;
    case TRNG_PKBCR:
        if ((val & TRNG_PKBCR_KEY_MASK) == TRNG_PKBCR_KEY) {
            qemu_log_mask(LOG_UNIMP,
                          TYPE_AT91_TRNG ": private-key bus transfer is "
                          "not implemented\n");
            s->isr |= TRNG_INT_EOTPKB;
            at91_trng_update_irq(s);
        }
        break;
    case TRNG_IER:
        s->imr |= val & TRNG_INT_MASK;
        at91_trng_update_irq(s);
        break;
    case TRNG_IDR:
        s->imr &= ~(val & TRNG_INT_MASK);
        at91_trng_update_irq(s);
        break;
    case TRNG_WPMR:
        if ((val & TRNG_WPMR_KEY_MASK) == TRNG_WPMR_KEY) {
            s->wpmr = val & TRNG_WPMR_MASK;
        }
        break;
    case TRNG_IMR:
    case TRNG_ISR:
    case TRNG_ODATA:
    case TRNG_WPSR:
        if (s->enabled) {
            at91_trng_raise_swe(s, offset, TRNG_SWE_WRITE_RO, false);
        }
        break;
    default:
        at91_trng_raise_swe(s, offset, TRNG_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TRNG ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_trng_ops = {
    .read = at91_trng_read,
    .write = at91_trng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_trng_clock_changed(void *opaque, ClockEvent event)
{
    AT91TRNGState *s = opaque;

    timer_del(s->generation_timer);
    at91_trng_schedule(s);
}

static void at91_trng_reset(DeviceState *dev)
{
    AT91TRNGState *s = AT91_TRNG(dev);

    timer_del(s->generation_timer);
    s->mr = 0;
    s->imr = 0;
    s->isr = 0;
    s->odata = 0;
    s->wpmr = 0;
    s->wpsr = 0;
    s->enabled = false;
    s->data_valid = false;
    s->previous_valid = false;
    at91_trng_update_irq(s);
}

static void at91_trng_init(Object *obj)
{
    AT91TRNGState *s = AT91_TRNG(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &at91_trng_ops, s,
                          TYPE_AT91_TRNG, TRNG_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_trng_clock_changed,
                                 s, ClockUpdate);
    s->generation_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       at91_trng_generate, s);
}

static void at91_trng_finalize(Object *obj)
{
    AT91TRNGState *s = AT91_TRNG(obj);

    timer_free(s->generation_timer);
}

static void at91_trng_realize(DeviceState *dev, Error **errp)
{
    AT91TRNGState *s = AT91_TRNG(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_TRNG ": pclk must be connected");
    }
}

static int at91_trng_post_load(void *opaque, int version_id)
{
    AT91TRNGState *s = opaque;

    s->mr &= TRNG_MR_MASK;
    s->imr &= TRNG_INT_MASK;
    s->isr &= TRNG_INT_MASK;
    s->wpmr &= TRNG_WPMR_MASK;
    at91_trng_update_irq(s);
    at91_trng_schedule(s);
    return 0;
}

static const VMStateDescription vmstate_at91_trng = {
    .name = TYPE_AT91_TRNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_trng_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91TRNGState),
        VMSTATE_UINT32(imr, AT91TRNGState),
        VMSTATE_UINT32(isr, AT91TRNGState),
        VMSTATE_UINT32(odata, AT91TRNGState),
        VMSTATE_UINT32(wpmr, AT91TRNGState),
        VMSTATE_UINT32(wpsr, AT91TRNGState),
        VMSTATE_BOOL(enabled, AT91TRNGState),
        VMSTATE_BOOL(data_valid, AT91TRNGState),
        VMSTATE_BOOL(previous_valid, AT91TRNGState),
        VMSTATE_TIMER_PTR(generation_timer, AT91TRNGState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_trng_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 True Random Number Generator";
    dc->realize = at91_trng_realize;
    dc->vmsd = &vmstate_at91_trng;
    device_class_set_legacy_reset(dc, at91_trng_reset);
}

static const TypeInfo at91_trng_info = {
    .name = TYPE_AT91_TRNG,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91TRNGState),
    .instance_init = at91_trng_init,
    .instance_finalize = at91_trng_finalize,
    .class_init = at91_trng_class_init,
};

static void at91_trng_register_types(void)
{
    type_register_static(&at91_trng_info);
}

type_init(at91_trng_register_types)
