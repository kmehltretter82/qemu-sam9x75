/*
 * Microchip AT91 FLEXCOM wrapper
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/misc/at91_flexcom.h"
#include "migration/vmstate.h"
#include "qemu/module.h"

#define FLEXCOM_MMIO_SIZE   0x200
#define FLEX_MR             0x00
#define FLEX_RHR            0x10
#define FLEX_THR            0x20

static void at91_flexcom_update_mode(AT91FlexcomState *s)
{
    qemu_set_irq(s->twi_enabled, s->mr == AT91_FLEXCOM_MODE_TWI);
}

static uint64_t at91_flexcom_read(void *opaque, hwaddr offset,
                                  unsigned int size)
{
    AT91FlexcomState *s = AT91_FLEXCOM(opaque);
    hwaddr reg = offset & ~3;

    switch (reg) {
    case FLEX_MR:
        return s->mr >> ((offset & 3) * 8);
    case FLEX_RHR:
        /* The protocol-specific RHR is the canonical interface in QEMU. */
        return 0;
    default:
        return 0;
    }
}

static void at91_flexcom_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned int size)
{
    AT91FlexcomState *s = AT91_FLEXCOM(opaque);
    hwaddr reg = offset & ~3;

    switch (reg) {
    case FLEX_MR:
        if (offset == FLEX_MR) {
            s->mr = value & 0x3;
        }
        at91_flexcom_update_mode(s);
        break;
    case FLEX_THR:
        /* The protocol-specific THR is the canonical interface in QEMU. */
        break;
    default:
        break;
    }
}

static const MemoryRegionOps at91_flexcom_ops = {
    .read = at91_flexcom_read,
    .write = at91_flexcom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_flexcom_reset(DeviceState *dev)
{
    AT91FlexcomState *s = AT91_FLEXCOM(dev);

    /* FLEXCOM comes up in USART mode on SAM9X7. */
    s->mr = AT91_FLEXCOM_MODE_USART;
    at91_flexcom_update_mode(s);
}

static int at91_flexcom_post_load(void *opaque, int version_id)
{
    at91_flexcom_update_mode(AT91_FLEXCOM(opaque));
    return 0;
}

static const VMStateDescription at91_flexcom_vmstate = {
    .name = TYPE_AT91_FLEXCOM,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_flexcom_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91FlexcomState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_flexcom_init(Object *obj)
{
    AT91FlexcomState *s = AT91_FLEXCOM(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_flexcom_ops, s,
                          TYPE_AT91_FLEXCOM, FLEXCOM_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_out_named(dev, &s->twi_enabled, "twi-enabled", 1);
}

static void at91_flexcom_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 FLEXCOM wrapper";
    dc->vmsd = &at91_flexcom_vmstate;
    device_class_set_legacy_reset(dc, at91_flexcom_reset);
}

static const TypeInfo at91_flexcom_info = {
    .name = TYPE_AT91_FLEXCOM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91FlexcomState),
    .instance_init = at91_flexcom_init,
    .class_init = at91_flexcom_class_init,
};

static void at91_flexcom_register_types(void)
{
    type_register_static(&at91_flexcom_info);
}

type_init(at91_flexcom_register_types)
