/*
 * Microchip SAM9X7 slow clock controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/at91_sckc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SCKC_CR                  0x00
#define SCKC_MMIO_SIZE           0x04

#define SCKC_CR_OSC32EN          BIT(1)
#define SCKC_CR_OSC32BYP         BIT(2)
#define SCKC_CR_TD_OSCSEL        BIT(24)
#define SCKC_CR_WRITE_MASK       (SCKC_CR_OSC32EN | SCKC_CR_OSC32BYP | \
                                  SCKC_CR_TD_OSCSEL)

static void at91_sckc_update_clocks(AT91SCKCState *s)
{
    uint64_t rc_hz = clock_get_hz(s->slow_rc);
    uint64_t td_hz = rc_hz;

    if (s->cr & SCKC_CR_TD_OSCSEL) {
        if (s->cr & (SCKC_CR_OSC32EN | SCKC_CR_OSC32BYP)) {
            td_hz = clock_get_hz(s->slow_xtal);
        } else {
            td_hz = 0;
        }
    }

    clock_update_hz(s->md_slck, rc_hz);
    clock_update_hz(s->td_slck, td_hz);
}

static void at91_sckc_clock_changed(void *opaque, ClockEvent event)
{
    at91_sckc_update_clocks(AT91_SCKC(opaque));
}

static uint64_t at91_sckc_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91SCKCState *s = AT91_SCKC(opaque);

    if (offset == SCKC_CR) {
        return s->cr;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_AT91_SCKC ": read from bad offset 0x%"
                  HWADDR_PRIx "\n", offset);
    return 0;
}

static void at91_sckc_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91SCKCState *s = AT91_SCKC(opaque);

    if (offset != SCKC_CR) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SCKC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    if (at91_sysc_write_protected(s->sysc, 0x50, false, true)) {
        return;
    }

    /* Bit zero is tied high on SAM9X7; only documented controls change. */
    s->cr = (s->cr & ~SCKC_CR_WRITE_MASK) |
            (value & SCKC_CR_WRITE_MASK);
    at91_sckc_update_clocks(s);
}

static const MemoryRegionOps at91_sckc_ops = {
    .read = at91_sckc_read,
    .write = at91_sckc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_sckc_reset(DeviceState *dev)
{
    AT91SCKCState *s = AT91_SCKC(dev);

    s->cr = 0x00000001;
    at91_sckc_update_clocks(s);
}

static void at91_sckc_init(Object *obj)
{
    AT91SCKCState *s = AT91_SCKC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_sckc_ops, s,
                          TYPE_AT91_SCKC, SCKC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->slow_rc = qdev_init_clock_in(dev, "slow-rc",
                                    at91_sckc_clock_changed, s, ClockUpdate);
    s->slow_xtal = qdev_init_clock_in(dev, "slow-xtal",
                                      at91_sckc_clock_changed, s,
                                      ClockUpdate);
    s->md_slck = qdev_init_clock_out(dev, "md-slck");
    s->td_slck = qdev_init_clock_out(dev, "td-slck");
}

static void at91_sckc_realize(DeviceState *dev, Error **errp)
{
    AT91SCKCState *s = AT91_SCKC(dev);

    if (!clock_has_source(s->slow_rc) || !clock_has_source(s->slow_xtal)) {
        error_setg(errp, TYPE_AT91_SCKC
                   ": slow-rc and slow-xtal clocks must be connected");
        return;
    }
    if (!s->sysc) {
        error_setg(errp, TYPE_AT91_SCKC ": sysc link must be connected");
        return;
    }
    at91_sckc_update_clocks(s);
}

static int at91_sckc_post_load(void *opaque, int version_id)
{
    at91_sckc_update_clocks(AT91_SCKC(opaque));
    return 0;
}

static const VMStateDescription at91_sckc_vmstate = {
    .name = TYPE_AT91_SCKC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_sckc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(slow_rc, AT91SCKCState),
        VMSTATE_CLOCK(slow_xtal, AT91SCKCState),
        VMSTATE_CLOCK(md_slck, AT91SCKCState),
        VMSTATE_CLOCK(td_slck, AT91SCKCState),
        VMSTATE_UINT32(cr, AT91SCKCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_sckc_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91SCKCState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
};

static void at91_sckc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 slow clock controller";
    dc->realize = at91_sckc_realize;
    dc->vmsd = &at91_sckc_vmstate;
    device_class_set_legacy_reset(dc, at91_sckc_reset);
    device_class_set_props(dc, at91_sckc_properties);
}

static const TypeInfo at91_sckc_info = {
    .name = TYPE_AT91_SCKC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SCKCState),
    .instance_init = at91_sckc_init,
    .class_init = at91_sckc_class_init,
};

static void at91_sckc_register_types(void)
{
    type_register_static(&at91_sckc_info);
}

type_init(at91_sckc_register_types)
