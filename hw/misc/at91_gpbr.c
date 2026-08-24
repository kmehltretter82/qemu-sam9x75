/*
 * Microchip AT91 general-purpose backup registers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/misc/at91_gpbr.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define GPBR_MR                 0x00
#define GPBR_FCLR               0x04
#define GPBR_BASE               0x08
#define GPBR_STRIDE             0x04
#define GPBR_LAST               (GPBR_BASE + \
                                 (AT91_GPBR_NUM_REGISTERS - 1) * GPBR_STRIDE)
#define GPBR_MMIO_SIZE          0x48

#define GPBR_MR_WP_MASK         0x000000ff
#define GPBR_MR_RP_MASK         0x00ff0000
#define GPBR_MR_MASK            (GPBR_MR_WP_MASK | GPBR_MR_RP_MASK)
#define GPBR_FCLR_ENABLE        BIT(0)

#define GPBR_SYSC_OFFSET        0x60

static bool at91_gpbr_register_offset(hwaddr offset)
{
    return offset >= GPBR_BASE && offset <= GPBR_LAST &&
           !(offset & (GPBR_STRIDE - 1));
}

static unsigned int at91_gpbr_register_index(hwaddr offset)
{
    return (offset - GPBR_BASE) / GPBR_STRIDE;
}

static uint64_t at91_gpbr_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91GPBRState *s = AT91_GPBR(opaque);
    unsigned int index;

    switch (offset) {
    case GPBR_MR:
        return s->mode;
    case GPBR_FCLR:
        return s->full_clear;
    default:
        if (at91_gpbr_register_offset(offset)) {
            index = at91_gpbr_register_index(offset);
            if (s->mode & BIT(index + 16)) {
                return 0;
            }
            return s->registers[index];
        }

        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_GPBR ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_gpbr_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91GPBRState *s = AT91_GPBR(opaque);
    unsigned int index;

    switch (offset) {
    case GPBR_MR:
        if (at91_sysc_write_protected(s->sysc,
                                      GPBR_SYSC_OFFSET + offset,
                                      false, true) || s->mode_written) {
            return;
        }
        s->mode = value & GPBR_MR_MASK;
        s->mode_written = true;
        break;
    case GPBR_FCLR:
        if (!at91_sysc_write_protected(s->sysc,
                                       GPBR_SYSC_OFFSET + offset,
                                       false, true)) {
            s->full_clear = value & GPBR_FCLR_ENABLE;
        }
        break;
    default:
        if (at91_gpbr_register_offset(offset)) {
            index = at91_gpbr_register_index(offset);
            if (at91_sysc_write_protected(s->sysc,
                                          GPBR_SYSC_OFFSET + offset,
                                          false, true) ||
                s->tamper_event_level || (s->mode & BIT(index))) {
                return;
            }
            s->registers[index] = value;
            return;
        }

        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_GPBR ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_gpbr_ops = {
    .read = at91_gpbr_read,
    .write = at91_gpbr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_gpbr_power_reset(AT91GPBRState *s)
{
    s->mode = 0;
    s->full_clear = 0;
    memset(s->registers, 0, sizeof(s->registers));
    s->mode_written = false;
}

static void at91_gpbr_set_vddbu_reset(void *opaque, int n, int level)
{
    AT91GPBRState *s = AT91_GPBR(opaque);

    if (level) {
        at91_gpbr_power_reset(s);
        s->tamper_event_level = false;
    }
}

static void at91_gpbr_set_tamper_event(void *opaque, int n, int level)
{
    AT91GPBRState *s = AT91_GPBR(opaque);
    unsigned int count;

    if (level && !s->tamper_event_level &&
        at91_rstc_gpbr_clear_enabled(s->rstc)) {
        count = (s->full_clear & GPBR_FCLR_ENABLE) ?
                AT91_GPBR_NUM_REGISTERS : 4;
        memset(s->registers, 0, count * sizeof(s->registers[0]));
    }
    s->tamper_event_level = !!level;
}

static void at91_gpbr_reset(DeviceState *dev)
{
    AT91GPBRState *s = AT91_GPBR(dev);

    /* The backup registers survive processor and peripheral resets. */
    if (!s->initialized) {
        at91_gpbr_power_reset(s);
        s->initialized = true;
    }
    s->tamper_event_level = false;
}

static void at91_gpbr_init(Object *obj)
{
    AT91GPBRState *s = AT91_GPBR(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_gpbr_ops, s,
                          TYPE_AT91_GPBR, GPBR_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_in_named(dev, at91_gpbr_set_vddbu_reset,
                            "vddbu-reset", 1);
    qdev_init_gpio_in_named(dev, at91_gpbr_set_tamper_event,
                            "tamper-event", 1);
}

static void at91_gpbr_realize(DeviceState *dev, Error **errp)
{
    AT91GPBRState *s = AT91_GPBR(dev);

    if (!s->sysc) {
        error_setg(errp, TYPE_AT91_GPBR ": sysc link must be connected");
        return;
    }
    if (!s->rstc) {
        error_setg(errp, TYPE_AT91_GPBR ": rstc link must be connected");
    }
}

static const VMStateDescription at91_gpbr_vmstate = {
    .name = TYPE_AT91_GPBR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mode, AT91GPBRState),
        VMSTATE_UINT32(full_clear, AT91GPBRState),
        VMSTATE_UINT32_ARRAY(registers, AT91GPBRState,
                             AT91_GPBR_NUM_REGISTERS),
        VMSTATE_BOOL(mode_written, AT91GPBRState),
        VMSTATE_BOOL(initialized, AT91GPBRState),
        VMSTATE_BOOL(tamper_event_level, AT91GPBRState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_gpbr_properties[] = {
    DEFINE_PROP_LINK("sysc", AT91GPBRState, sysc, TYPE_AT91_SYSCWP,
                     AT91SYSCWPState *),
    DEFINE_PROP_LINK("rstc", AT91GPBRState, rstc, TYPE_AT91_RSTC,
                     AT91RSTCState *),
};

static void at91_gpbr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 general-purpose backup registers";
    dc->realize = at91_gpbr_realize;
    dc->vmsd = &at91_gpbr_vmstate;
    device_class_set_props(dc, at91_gpbr_properties);
    device_class_set_legacy_reset(dc, at91_gpbr_reset);
}

static const TypeInfo at91_gpbr_info = {
    .name = TYPE_AT91_GPBR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91GPBRState),
    .instance_init = at91_gpbr_init,
    .class_init = at91_gpbr_class_init,
};

static void at91_gpbr_register_types(void)
{
    type_register_static(&at91_gpbr_info);
}

type_init(at91_gpbr_register_types)
