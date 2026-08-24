/*
 * Microchip AT91 Audio Class D Amplifier
 *
 * This models the register interface, sample timing, interrupt, and DMA
 * handshake of the SAMA5D2/SAM9X7 CLASSD.  Converted samples are discarded
 * because the board's PWM power-stage output is not connected to a QEMU
 * audio codec.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/audio/at91_classd.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define CLASSD_CR                  0x00
#define CLASSD_MR                  0x04
#define CLASSD_INTPMR              0x08
#define CLASSD_INTSR               0x0c
#define CLASSD_THR                 0x10
#define CLASSD_IER                 0x14
#define CLASSD_IDR                 0x18
#define CLASSD_IMR                 0x1c
#define CLASSD_ISR                 0x20
#define CLASSD_WPMR                0xe4
#define CLASSD_MMIO_SIZE           0x100

#define CLASSD_CR_SWRST            BIT(0)

#define CLASSD_MR_LEN              BIT(0)
#define CLASSD_MR_LMUTE            BIT(1)
#define CLASSD_MR_REN              BIT(4)
#define CLASSD_MR_RMUTE            BIT(5)
#define CLASSD_MR_PWMTYP           BIT(8)
#define CLASSD_MR_NON_OVERLAP      BIT(16)
#define CLASSD_MR_NOVRVAL_MASK     (3U << 20)
#define CLASSD_MR_ENABLE_MASK      (CLASSD_MR_LEN | CLASSD_MR_REN)
#define CLASSD_MR_MASK             (CLASSD_MR_ENABLE_MASK | \
                                    CLASSD_MR_LMUTE | CLASSD_MR_RMUTE | \
                                    CLASSD_MR_PWMTYP | \
                                    CLASSD_MR_NON_OVERLAP | \
                                    CLASSD_MR_NOVRVAL_MASK)
#define CLASSD_MR_RESET            0x00010022

#define CLASSD_INTPMR_DSPCLKFREQ   BIT(16)
#define CLASSD_INTPMR_FRAME_MASK   (7U << 20)
#define CLASSD_INTPMR_MASK         0x7f7d7f7f
#define CLASSD_INTPMR_RESET        0x00304e4e

#define CLASSD_INTSR_CFGERR        BIT(0)
#define CLASSD_INT_DATRDY          BIT(0)

#define CLASSD_WPMR_WPEN           BIT(0)
#define CLASSD_WPMR_KEY_MASK       0xffffff00
#define CLASSD_WPMR_KEY            0x434c4400

#define CLASSD_GCLK_12M288         98304000ULL
#define CLASSD_GCLK_11M2896        90316800ULL
#define CLASSD_GCLK_TOLERANCE_PPM  1

static const unsigned int at91_classd_sample_hz[] = {
    8000, 16000, 32000, 48000, 96000, 22050, 44100, 88200,
};

static bool at91_classd_config_error(const AT91CLASSDState *s)
{
    unsigned int frame = extract32(s->intpmr, 20, 3);
    bool family_44k = s->intpmr & CLASSD_INTPMR_DSPCLKFREQ;
    uint64_t expected = family_44k ? CLASSD_GCLK_11M2896 :
                                     CLASSD_GCLK_12M288;
    uint64_t actual = clock_get_hz(s->gclk);
    uint64_t difference;

    if (!actual) {
        return false;
    }
    if (family_44k != (frame >= 5)) {
        return true;
    }

    difference = actual > expected ? actual - expected : expected - actual;
    return difference > MAX(1ULL, expected *
                            CLASSD_GCLK_TOLERANCE_PPM / 1000000);
}

static bool at91_classd_should_convert(const AT91CLASSDState *s)
{
    return (s->mr & CLASSD_MR_ENABLE_MASK) && clock_get_hz(s->gclk) &&
           !at91_classd_config_error(s);
}

static uint64_t at91_classd_sample_period_ns(const AT91CLASSDState *s)
{
    unsigned int frame = extract32(s->intpmr, 20, 3);

    return DIV_ROUND_UP(NANOSECONDS_PER_SECOND,
                        at91_classd_sample_hz[frame]);
}

static void at91_classd_update_irq(AT91CLASSDState *s)
{
    qemu_set_irq(s->irq, (s->isr & s->imr) && clock_get_hz(s->pclk));
}

static void at91_classd_update_request(AT91CLASSDState *s)
{
    bool level = s->conversion_active && !s->data_valid &&
                 clock_get_hz(s->pclk);

    if (level != s->tx_request_level) {
        s->tx_request_level = level;
        qemu_set_irq(s->tx_request, level);
    }
}

static void at91_classd_schedule(AT91CLASSDState *s)
{
    if (s->conversion_active && !timer_pending(s->sample_timer)) {
        timer_mod(s->sample_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  at91_classd_sample_period_ns(s));
    }
}

static void at91_classd_refresh(AT91CLASSDState *s)
{
    bool active = at91_classd_should_convert(s);

    if (active && !s->conversion_active && !s->data_valid) {
        s->isr |= CLASSD_INT_DATRDY;
    }
    s->conversion_active = active;
    if (!active) {
        timer_del(s->sample_timer);
    }
    at91_classd_update_irq(s);
    at91_classd_update_request(s);
    at91_classd_schedule(s);
}

static void at91_classd_sample_done(void *opaque)
{
    AT91CLASSDState *s = opaque;

    if (!s->conversion_active) {
        return;
    }

    s->data_valid = false;
    s->isr |= CLASSD_INT_DATRDY;
    at91_classd_update_irq(s);
    at91_classd_update_request(s);
    at91_classd_schedule(s);
}

static void at91_classd_reset_registers(AT91CLASSDState *s)
{
    timer_del(s->sample_timer);
    s->mr = CLASSD_MR_RESET;
    s->intpmr = CLASSD_INTPMR_RESET;
    s->thr = 0;
    s->imr = 0;
    s->isr = 0;
    s->wpmr = 0;
    s->data_valid = false;
    s->conversion_active = false;
    at91_classd_update_irq(s);
    at91_classd_update_request(s);
}

static uint64_t at91_classd_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    AT91CLASSDState *s = AT91_CLASSD(opaque);
    uint32_t value;

    switch (offset) {
    case CLASSD_MR:
        return s->mr;
    case CLASSD_INTPMR:
        return s->intpmr;
    case CLASSD_INTSR:
        return at91_classd_config_error(s) ? CLASSD_INTSR_CFGERR : 0;
    case CLASSD_THR:
        return s->thr;
    case CLASSD_IMR:
        return s->imr;
    case CLASSD_ISR:
        value = s->isr;
        s->isr &= ~CLASSD_INT_DATRDY;
        at91_classd_update_irq(s);
        return value;
    case CLASSD_WPMR:
        return s->wpmr;
    case CLASSD_CR:
    case CLASSD_IER:
    case CLASSD_IDR:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_CLASSD ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_classd_write_thr(AT91CLASSDState *s, uint32_t value,
                                  unsigned int size)
{
    if (size == 1) {
        s->thr = deposit32(s->thr, 0, 8, value);
    } else if (size == 2) {
        s->thr = deposit32(s->thr, 0, 16, value);
    } else {
        s->thr = value;
    }
    s->data_valid = true;
    at91_classd_update_request(s);
}

static void at91_classd_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    AT91CLASSDState *s = AT91_CLASSD(opaque);
    uint32_t val = value;

    switch (offset) {
    case CLASSD_CR:
        if (val & CLASSD_CR_SWRST) {
            at91_classd_reset_registers(s);
        }
        break;
    case CLASSD_MR:
        if (!(s->wpmr & CLASSD_WPMR_WPEN)) {
            s->mr = val & CLASSD_MR_MASK;
            at91_classd_refresh(s);
        }
        break;
    case CLASSD_INTPMR:
        if (!(s->wpmr & CLASSD_WPMR_WPEN)) {
            s->intpmr = val & CLASSD_INTPMR_MASK;
            timer_del(s->sample_timer);
            at91_classd_refresh(s);
        }
        break;
    case CLASSD_THR:
        at91_classd_write_thr(s, val, size);
        break;
    case CLASSD_IER:
        s->imr |= val & CLASSD_INT_DATRDY;
        at91_classd_update_irq(s);
        break;
    case CLASSD_IDR:
        s->imr &= ~(val & CLASSD_INT_DATRDY);
        at91_classd_update_irq(s);
        break;
    case CLASSD_WPMR:
        if ((val & CLASSD_WPMR_KEY_MASK) == CLASSD_WPMR_KEY) {
            s->wpmr = val & CLASSD_WPMR_WPEN;
        }
        break;
    case CLASSD_INTSR:
    case CLASSD_IMR:
    case CLASSD_ISR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_CLASSD ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_CLASSD ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_classd_ops = {
    .read = at91_classd_read,
    .write = at91_classd_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_classd_clock_changed(void *opaque, ClockEvent event)
{
    AT91CLASSDState *s = opaque;

    timer_del(s->sample_timer);
    at91_classd_refresh(s);
}

static void at91_classd_reset(DeviceState *dev)
{
    at91_classd_reset_registers(AT91_CLASSD(dev));
}

static void at91_classd_init(Object *obj)
{
    AT91CLASSDState *s = AT91_CLASSD(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &at91_classd_ops, s,
                          TYPE_AT91_CLASSD, CLASSD_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_classd_clock_changed,
                                 s, ClockUpdate);
    s->gclk = qdev_init_clock_in(dev, "gclk", at91_classd_clock_changed,
                                 s, ClockUpdate);
    s->sample_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   at91_classd_sample_done, s);
}

static void at91_classd_finalize(Object *obj)
{
    AT91CLASSDState *s = AT91_CLASSD(obj);

    timer_free(s->sample_timer);
}

static void at91_classd_realize(DeviceState *dev, Error **errp)
{
    AT91CLASSDState *s = AT91_CLASSD(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_CLASSD ": pclk must be connected");
        return;
    }
    if (!clock_has_source(s->gclk)) {
        error_setg(errp, TYPE_AT91_CLASSD ": gclk must be connected");
    }
}

static int at91_classd_post_load(void *opaque, int version_id)
{
    AT91CLASSDState *s = opaque;

    s->mr &= CLASSD_MR_MASK;
    s->intpmr &= CLASSD_INTPMR_MASK;
    s->imr &= CLASSD_INT_DATRDY;
    s->isr &= CLASSD_INT_DATRDY;
    s->wpmr &= CLASSD_WPMR_WPEN;
    qemu_set_irq(s->tx_request, 0);
    s->tx_request_level = false;
    at91_classd_refresh(s);
    return 0;
}

static const VMStateDescription vmstate_at91_classd = {
    .name = TYPE_AT91_CLASSD,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_classd_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91CLASSDState),
        VMSTATE_UINT32(intpmr, AT91CLASSDState),
        VMSTATE_UINT32(thr, AT91CLASSDState),
        VMSTATE_UINT32(imr, AT91CLASSDState),
        VMSTATE_UINT32(isr, AT91CLASSDState),
        VMSTATE_UINT32(wpmr, AT91CLASSDState),
        VMSTATE_BOOL(data_valid, AT91CLASSDState),
        VMSTATE_BOOL(conversion_active, AT91CLASSDState),
        VMSTATE_TIMER_PTR(sample_timer, AT91CLASSDState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_classd_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Audio Class D Amplifier";
    dc->realize = at91_classd_realize;
    dc->vmsd = &vmstate_at91_classd;
    device_class_set_legacy_reset(dc, at91_classd_reset);
}

static const TypeInfo at91_classd_info = {
    .name = TYPE_AT91_CLASSD,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91CLASSDState),
    .instance_init = at91_classd_init,
    .instance_finalize = at91_classd_finalize,
    .class_init = at91_classd_class_init,
};

static void at91_classd_register_types(void)
{
    type_register_static(&at91_classd_info);
}

type_init(at91_classd_register_types)
