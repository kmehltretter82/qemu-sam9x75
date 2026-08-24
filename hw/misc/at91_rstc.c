/*
 * Microchip SAM9X7 reset controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
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

    s->srcmp = false;
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

static void at91_rstc_user_event(AT91RSTCState *s)
{
    s->ursts = true;
    at91_rstc_update_irq(s);

    if (s->mode & RSTC_MR_URSTEN) {
        s->pending_reset_type = RSTC_TYPE_USER;
        at91_rstc_assert_external(s);
        qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
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
        if (s->mode & RSTC_MR_URSTASYNC) {
            at91_rstc_user_event(s);
        } else {
            timer_mod(s->sample_timer,
                      qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                      at91_rstc_slow_clock_delay(s, 1));
        }
    } else if (!old_level && s->nrst_level) {
        timer_del(s->sample_timer);
    }
}

static void at91_rstc_set_wdt_reset(void *opaque, int n, int level)
{
    AT91RSTCState *s = AT91_RSTC(opaque);

    if (level) {
        s->pending_reset_type = RSTC_TYPE_WATCHDOG;
        at91_rstc_assert_external(s);
    } else if (s->pending_reset_type == RSTC_TYPE_WATCHDOG) {
        /* A non-reset QEMU watchdog policy ends the physical pulse. */
        s->pending_reset_type = RSTC_TYPE_NONE;
        timer_del(s->external_timer);
        s->nrst_out_level = true;
        qemu_set_irq(s->nrst_out, 1);
    }
}

static uint32_t at91_rstc_status(AT91RSTCState *s)
{
    uint32_t value = s->reset_type << RSTC_SR_RSTTYP_SHIFT;

    if (s->ursts) {
        value |= RSTC_SR_URSTS;
    }
    if (s->nrst_level) {
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
        if ((value & RSTC_KEY_MASK) != RSTC_KEY || s->srcmp) {
            return;
        }
        command = value & RSTC_CR_MASK;
        if (!command) {
            return;
        }

        s->srcmp = true;
        at91_rstc_assert_external(s);
        if (command & RSTC_CR_PROCRST) {
            s->pending_reset_type = RSTC_TYPE_SOFTWARE;
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
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

static void at91_rstc_reset(DeviceState *dev)
{
    AT91RSTCState *s = AT91_RSTC(dev);
    bool warm_reset = s->pending_reset_type != RSTC_TYPE_NONE;
    uint32_t saved_mode = s->mode;

    timer_del(s->external_timer);
    timer_del(s->sample_timer);

    if (warm_reset) {
        s->reset_type = s->pending_reset_type;
        s->mode = saved_mode;
        s->ursts = s->reset_type == RSTC_TYPE_USER;
    } else {
        s->reset_type = RSTC_TYPE_GENERAL;
        s->mode = RSTC_MR_URSTEN;
        s->ursts = true;
        s->nrst_level = true;
    }
    s->pending_reset_type = RSTC_TYPE_NONE;
    s->srcmp = false;
    s->nrst_out_level = true;
    qemu_set_irq(s->nrst_out, 1);
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
    qdev_init_gpio_in_named(dev, at91_rstc_set_wdt_reset, "wdt-reset", 1);
    qdev_init_gpio_out_named(dev, &s->nrst_out, "nrst-out", 1);
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
}

static void at91_rstc_finalize(Object *obj)
{
    AT91RSTCState *s = AT91_RSTC(obj);

    timer_free(s->external_timer);
    timer_free(s->sample_timer);
}

static int at91_rstc_post_load(void *opaque, int version_id)
{
    AT91RSTCState *s = AT91_RSTC(opaque);

    qemu_set_irq(s->nrst_out, s->nrst_out_level);
    at91_rstc_update_irq(s);
    return 0;
}

static const VMStateDescription at91_rstc_vmstate = {
    .name = TYPE_AT91_RSTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_rstc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(slck, AT91RSTCState),
        VMSTATE_TIMER_PTR(external_timer, AT91RSTCState),
        VMSTATE_TIMER_PTR(sample_timer, AT91RSTCState),
        VMSTATE_UINT32(mode, AT91RSTCState),
        VMSTATE_UINT8(reset_type, AT91RSTCState),
        VMSTATE_UINT8(pending_reset_type, AT91RSTCState),
        VMSTATE_BOOL(ursts, AT91RSTCState),
        VMSTATE_BOOL(srcmp, AT91RSTCState),
        VMSTATE_BOOL(nrst_level, AT91RSTCState),
        VMSTATE_BOOL(nrst_out_level, AT91RSTCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_rstc_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91RSTCState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
};

static void at91_rstc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 reset controller";
    dc->realize = at91_rstc_realize;
    dc->vmsd = &at91_rstc_vmstate;
    device_class_set_props(dc, at91_rstc_properties);
    device_class_set_legacy_reset(dc, at91_rstc_reset);
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
