/*
 * Microchip AT91 system-controller write protection
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/at91_sysc.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SYSC_WPMR               0x00
#define SYSC_WPSR               0x04
#define SYSC_MMIO_SIZE          0x08

#define SYSC_WPMR_WPEN          BIT(0)
#define SYSC_WPMR_WPITEN        BIT(1)
#define SYSC_WPMR_MASK          (SYSC_WPMR_WPEN | SYSC_WPMR_WPITEN)
#define SYSC_WPMR_KEY_MASK      0xffffff00
#define SYSC_WPMR_KEY           0x53594300

#define SYSC_WPSR_WPVS          BIT(0)

bool at91_sysc_write_protected(AT91SYSCWPState *s, hwaddr offset,
                               bool interrupt_register, bool report)
{
    uint32_t enable = interrupt_register ? SYSC_WPMR_WPITEN :
                                           SYSC_WPMR_WPEN;

    if (!s || !(s->wpmr & enable)) {
        return false;
    }

    if (report) {
        s->wpsr = SYSC_WPSR_WPVS | ((offset & 0xff) << 8);
    }
    return true;
}

static uint64_t at91_sysc_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91SYSCWPState *s = AT91_SYSCWP(opaque);
    uint32_t value;

    switch (offset) {
    case SYSC_WPMR:
        return s->wpmr;
    case SYSC_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SYSCWP ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_sysc_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91SYSCWPState *s = AT91_SYSCWP(opaque);

    switch (offset) {
    case SYSC_WPMR:
        if ((value & SYSC_WPMR_KEY_MASK) == SYSC_WPMR_KEY) {
            s->wpmr = value & SYSC_WPMR_MASK;
        }
        break;
    case SYSC_WPSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SYSCWP ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SYSCWP ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_sysc_ops = {
    .read = at91_sysc_read,
    .write = at91_sysc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_sysc_reset(DeviceState *dev)
{
    AT91SYSCWPState *s = AT91_SYSCWP(dev);

    s->wpmr = 0;
    s->wpsr = 0;
}

static void at91_sysc_init(Object *obj)
{
    AT91SYSCWPState *s = AT91_SYSCWP(obj);

    memory_region_init_io(&s->mmio, obj, &at91_sysc_ops, s,
                          TYPE_AT91_SYSCWP, SYSC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static const VMStateDescription at91_sysc_vmstate = {
    .name = TYPE_AT91_SYSCWP,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(wpmr, AT91SYSCWPState),
        VMSTATE_UINT32(wpsr, AT91SYSCWPState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_sysc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 system-controller write protection";
    dc->vmsd = &at91_sysc_vmstate;
    device_class_set_legacy_reset(dc, at91_sysc_reset);
}

static const TypeInfo at91_sysc_info = {
    .name = TYPE_AT91_SYSCWP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SYSCWPState),
    .instance_init = at91_sysc_init,
    .class_init = at91_sysc_class_init,
};

static void at91_sysc_register_types(void)
{
    type_register_static(&at91_sysc_info);
}

type_init(at91_sysc_register_types)
