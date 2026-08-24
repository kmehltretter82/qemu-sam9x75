/*
 * Microchip AT91 boot sequence controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/misc/at91_bsc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define BSC_CR                  0x00
#define BSC_MMIO_SIZE           0x04

#define BSC_CR_BOOT_MASK        0x00000007
#define BSC_CR_WPKEY_MASK       0xffff0000
#define BSC_CR_WPKEY            0x66830000

static uint64_t at91_bsc_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91BSCState *s = AT91_BSC(opaque);

    if (offset == BSC_CR) {
        /* WPKEY is write-only and always reads as zero. */
        return s->boot_sequence;
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  TYPE_AT91_BSC ": read from bad offset 0x%"
                  HWADDR_PRIx "\n", offset);
    return 0;
}

static void at91_bsc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91BSCState *s = AT91_BSC(opaque);

    if (offset != BSC_CR) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_BSC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return;
    }

    if ((value & BSC_CR_WPKEY_MASK) != BSC_CR_WPKEY) {
        return;
    }

    s->boot_sequence = value & BSC_CR_BOOT_MASK;
}

static const MemoryRegionOps at91_bsc_ops = {
    .read = at91_bsc_read,
    .write = at91_bsc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_bsc_power_reset(AT91BSCState *s)
{
    s->boot_sequence = s->factory_boot_sequence;
}

static void at91_bsc_set_vddbu_reset(void *opaque, int n, int level)
{
    AT91BSCState *s = AT91_BSC(opaque);

    if (level) {
        at91_bsc_power_reset(s);
    }
}

static void at91_bsc_init(Object *obj)
{
    AT91BSCState *s = AT91_BSC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_bsc_ops, s,
                          TYPE_AT91_BSC, BSC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_in_named(dev, at91_bsc_set_vddbu_reset,
                            "vddbu-reset", 1);
}

static void at91_bsc_realize(DeviceState *dev, Error **errp)
{
    AT91BSCState *s = AT91_BSC(dev);

    if (s->factory_boot_sequence & ~BSC_CR_BOOT_MASK) {
        error_setg(errp, TYPE_AT91_BSC
                   ": factory-boot-sequence must be between 0 and 7");
        return;
    }

    at91_bsc_power_reset(s);
}

static const VMStateDescription at91_bsc_vmstate = {
    .name = TYPE_AT91_BSC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(boot_sequence, AT91BSCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_bsc_properties[] = {
    DEFINE_PROP_UINT8("factory-boot-sequence", AT91BSCState,
                      factory_boot_sequence, 0),
};

static void at91_bsc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 boot sequence controller";
    dc->realize = at91_bsc_realize;
    dc->vmsd = &at91_bsc_vmstate;
    device_class_set_props(dc, at91_bsc_properties);
}

static const TypeInfo at91_bsc_info = {
    .name = TYPE_AT91_BSC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91BSCState),
    .instance_init = at91_bsc_init,
    .class_init = at91_bsc_class_init,
};

static void at91_bsc_register_types(void)
{
    type_register_static(&at91_bsc_info);
}

type_init(at91_bsc_register_types)
