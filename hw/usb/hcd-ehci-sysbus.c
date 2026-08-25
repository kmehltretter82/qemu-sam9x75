/*
 * QEMU USB EHCI Emulation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/usb/hcd-ehci.h"
#include "migration/vmstate.h"

static const VMStateDescription vmstate_ehci_sysbus = {
    .name        = "ehci-sysbus",
    .version_id  = 2,
    .minimum_version_id  = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ehci, EHCISysBusState, 2, vmstate_ehci, EHCIState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property ehci_sysbus_properties[] = {
    DEFINE_EHCI_COMMON_PROPERTIES(EHCISysBusState),
    DEFINE_PROP_BOOL("companion-enable", EHCISysBusState, ehci.companion_enable,
                     false),
};

static void usb_ehci_sysbus_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(dev);
    EHCIState *s = &i->ehci;

    if (object_dynamic_cast(OBJECT(dev), TYPE_AT91_UHPHS_EHCI)) {
        AT91UHPHSEHCIState *at91 = AT91_UHPHS_EHCI(dev);

        if (!clock_has_source(at91->pclk) ||
            !clock_has_source(at91->utmi)) {
            error_setg(errp, "%s: pclk and utmi clocks must be connected",
                       TYPE_AT91_UHPHS_EHCI);
            return;
        }
    }

    usb_ehci_realize(s, dev, errp);
    if (*errp) {
        return;
    }
    sysbus_init_irq(d, &s->irq);
    ehci_clock_update(s);
}

static void usb_ehci_sysbus_reset(DeviceState *dev)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    EHCISysBusState *i = SYS_BUS_EHCI(d);
    EHCIState *s = &i->ehci;

    ehci_reset(s);
}

static void ehci_sysbus_init(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_GET_CLASS(obj);
    EHCIState *s = &i->ehci;

    s->capsbase = sec->capsbase;
    s->opregbase = sec->opregbase;
    s->portscbase = sec->portscbase;
    s->portnr = sec->portnr;
    s->as = &address_space_memory;

    usb_ehci_init(s, DEVICE(obj));
    sysbus_init_mmio(d, &s->mem);
}

static void ehci_sysbus_finalize(Object *obj)
{
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    EHCIState *s = &i->ehci;

    usb_ehci_finalize(s);
}

static void ehci_sysbus_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(klass);

    sec->portscbase = 0x44;
    sec->portnr = EHCI_PORTS;

    dc->realize = usb_ehci_sysbus_realize;
    dc->vmsd = &vmstate_ehci_sysbus;
    device_class_set_props(dc, ehci_sysbus_properties);
    device_class_set_legacy_reset(dc, usb_ehci_sysbus_reset);
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_platform_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x20;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_exynos4210_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_aw_h3_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

#define AT91_UHPHS_INSNREG06_AHB_ERR (1U << 31)

static uint64_t ehci_at91_uhphs_vendor_read(void *opaque, hwaddr addr,
                                            unsigned int size)
{
    AT91UHPHSEHCIState *s = opaque;

    switch (addr) {
    case 0x0:
        return s->insnreg06;
    case 0x4:
        return s->insnreg07;
    default:
        g_assert_not_reached();
    }
}

static void ehci_at91_uhphs_vendor_write(void *opaque, hwaddr addr,
                                         uint64_t value, unsigned int size)
{
    AT91UHPHSEHCIState *s = opaque;

    switch (addr) {
    case 0x0:
        /* AHB_ERR is cleared by writing zero; all other fields are RO. */
        if (!(value & AT91_UHPHS_INSNREG06_AHB_ERR)) {
            s->insnreg06 &= ~AT91_UHPHS_INSNREG06_AHB_ERR;
        }
        break;
    case 0x4:
        /* AHB_ADDR is read-only. */
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps ehci_at91_uhphs_vendor_ops = {
    .read = ehci_at91_uhphs_vendor_read,
    .write = ehci_at91_uhphs_vendor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

void at91_uhphs_ehci_record_dma_error(AT91UHPHSEHCIState *s,
                                      uint64_t addr)
{
    s->insnreg06 |= AT91_UHPHS_INSNREG06_AHB_ERR;
    s->insnreg07 = (uint32_t)addr;
}

static void ehci_at91_uhphs_dma_error(void *opaque, uint64_t addr)
{
    at91_uhphs_ehci_record_dma_error(opaque, addr);
}

static void ehci_at91_uhphs_update_port_a(AT91UHPHSEHCIState *s)
{
    EHCIState *ehci = &s->parent_obj.ehci;

    /* UDPHS owns Port A's shared UTMI transceiver and connector. */
    ehci_set_port_available(ehci, 0, !s->device_mode);
}

static void ehci_at91_uhphs_device_mode(void *opaque, int n, int level)
{
    AT91UHPHSEHCIState *s = opaque;

    s->device_mode = level;
    ehci_at91_uhphs_update_port_a(s);
}

static void ehci_at91_uhphs_reset(void *opaque)
{
    AT91UHPHSEHCIState *s = opaque;

    s->insnreg06 = 0;
    s->insnreg07 = 0;
    ehci_at91_uhphs_update_port_a(s);
}

static bool ehci_at91_uhphs_clocked(void *opaque)
{
    AT91UHPHSEHCIState *s = opaque;

    return s->legacy_clock_bypass ||
           (clock_is_enabled(s->pclk) && clock_is_enabled(s->utmi));
}

static void ehci_at91_uhphs_clock_changed(void *opaque, ClockEvent event)
{
    AT91UHPHSEHCIState *s = opaque;

    ehci_clock_update(&s->parent_obj.ehci);
}

static int ehci_at91_uhphs_pre_load(void *opaque)
{
    AT91UHPHSEHCIState *s = opaque;

    s->parent_obj.ehci.clock_post_load_pending = true;
    return 0;
}

static int ehci_at91_uhphs_post_load(void *opaque, int version_id)
{
    AT91UHPHSEHCIState *s = opaque;

    if (version_id < 2) {
        s->legacy_clock_bypass = true;
    }
    return 0;
}

static const VMStateDescription vmstate_ehci_at91_uhphs = {
    .name = "at91-uhphs-ehci",
    .version_id = 2,
    .minimum_version_id = 1,
    .pre_load = ehci_at91_uhphs_pre_load,
    .post_load = ehci_at91_uhphs_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj.ehci, AT91UHPHSEHCIState, 1,
                       vmstate_ehci, EHCIState),
        VMSTATE_CLOCK_V(pclk, AT91UHPHSEHCIState, 2),
        VMSTATE_CLOCK_V(utmi, AT91UHPHSEHCIState, 2),
        VMSTATE_BOOL_V(legacy_clock_bypass, AT91UHPHSEHCIState, 2),
        VMSTATE_UINT32(insnreg06, AT91UHPHSEHCIState),
        VMSTATE_UINT32(insnreg07, AT91UHPHSEHCIState),
        VMSTATE_END_OF_LIST()
    },
};

static void ehci_at91_uhphs_init(Object *obj)
{
    AT91UHPHSEHCIState *s = AT91_UHPHS_EHCI(obj);
    EHCISysBusState *i = &s->parent_obj;

    i->ehci.dma_error_cb = ehci_at91_uhphs_dma_error;
    i->ehci.dma_error_opaque = s;
    i->ehci.reset_cb = ehci_at91_uhphs_reset;
    i->ehci.reset_opaque = s;
    i->ehci.clocked_cb = ehci_at91_uhphs_clocked;
    i->ehci.clocked_opaque = s;

    s->pclk = qdev_init_clock_in(DEVICE(obj), "pclk",
                                 ehci_at91_uhphs_clock_changed, s,
                                 ClockUpdate);
    s->utmi = qdev_init_clock_in(DEVICE(obj), "utmi",
                                 ehci_at91_uhphs_clock_changed, s,
                                 ClockUpdate);
    qdev_init_gpio_in_named(DEVICE(obj), ehci_at91_uhphs_device_mode,
                            "device-mode", 1);

    /* SAM9X7 Series Data Sheet, UHPHS register reset values. */
    i->ehci.caps[0x08] = 0x26;
    i->ehci.usbcmd_reset = 0x00080b00;

    memory_region_init_io(&s->vendor_mem, obj, &ehci_at91_uhphs_vendor_ops,
                          s, "at91-uhphs-vendor", 8);
    memory_region_add_subregion(&i->ehci.mem, 0xa8, &s->vendor_mem);
}

static void ehci_at91_uhphs_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    sec->portscbase = 0x44;
    sec->portnr = 3;
    dc->vmsd = &vmstate_ehci_at91_uhphs;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_npcm7xx_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    sec->portscbase = 0x44;
    sec->portnr = 1;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_tegra2_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x100;
    sec->opregbase = 0x140;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static void ehci_ppc4xx_init(Object *o)
{
    EHCISysBusState *s = SYS_BUS_EHCI(o);

    s->ehci.companion_enable = true;
}

static void ehci_ppc4xx_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

/*
 * Faraday FUSBH200 USB 2.0 EHCI
 */

/**
 * FUSBH200EHCIRegs:
 * @FUSBH200_REG_EOF_ASTR: EOF/Async. Sleep Timer Register
 * @FUSBH200_REG_BMCSR: Bus Monitor Control/Status Register
 */
enum FUSBH200EHCIRegs {
    FUSBH200_REG_EOF_ASTR = 0x34,
    FUSBH200_REG_BMCSR    = 0x40,
};

static uint64_t fusbh200_ehci_read(void *opaque, hwaddr addr, unsigned size)
{
    EHCIState *s = opaque;
    hwaddr off = s->opregbase + s->portscbase + 4 * s->portnr + addr;

    switch (off) {
    case FUSBH200_REG_EOF_ASTR:
        return 0x00000041;
    case FUSBH200_REG_BMCSR:
        /* High-Speed, VBUS valid, interrupt level-high active */
        return (2 << 9) | (1 << 8) | (1 << 3);
    }

    return 0;
}

static void fusbh200_ehci_write(void *opaque, hwaddr addr, uint64_t val,
                                unsigned size)
{
}

static const MemoryRegionOps fusbh200_ehci_mmio_ops = {
    .read = fusbh200_ehci_read,
    .write = fusbh200_ehci_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void fusbh200_ehci_init(Object *obj)
{
    EHCISysBusState *i = SYS_BUS_EHCI(obj);
    FUSBH200EHCIState *f = FUSBH200_EHCI(obj);
    EHCIState *s = &i->ehci;

    memory_region_init_io(&f->mem_vendor, OBJECT(f), &fusbh200_ehci_mmio_ops, s,
                          "fusbh200", 0x4c);
    memory_region_add_subregion(&s->mem,
                                s->opregbase + s->portscbase + 4 * s->portnr,
                                &f->mem_vendor);
}

static void fusbh200_ehci_class_init(ObjectClass *oc, const void *data)
{
    SysBusEHCIClass *sec = SYS_BUS_EHCI_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    sec->capsbase = 0x0;
    sec->opregbase = 0x10;
    sec->portscbase = 0x20;
    sec->portnr = 1;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
}

static const TypeInfo ehci_sysbus_types[] = {
    {
        .name          = TYPE_SYS_BUS_EHCI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(EHCISysBusState),
        .instance_init = ehci_sysbus_init,
        .instance_finalize = ehci_sysbus_finalize,
        .abstract      = true,
        .class_init    = ehci_sysbus_class_init,
        .class_size    = sizeof(SysBusEHCIClass),
    },
    {
        .name          = TYPE_PLATFORM_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_platform_class_init,
    },
    {
        .name          = TYPE_EXYNOS4210_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_exynos4210_class_init,
    },
    {
        .name          = TYPE_AW_H3_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_aw_h3_class_init,
    },
    {
        .name          = TYPE_AT91_UHPHS_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .instance_size = sizeof(AT91UHPHSEHCIState),
        .instance_init = ehci_at91_uhphs_init,
        .class_init    = ehci_at91_uhphs_class_init,
    },
    {
        .name          = TYPE_NPCM7XX_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_npcm7xx_class_init,
    },
    {
        .name          = TYPE_TEGRA2_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_tegra2_class_init,
    },
    {
        .name          = TYPE_PPC4xx_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .class_init    = ehci_ppc4xx_class_init,
        .instance_init = ehci_ppc4xx_init,
    },
    {
        .name          = TYPE_FUSBH200_EHCI,
        .parent        = TYPE_SYS_BUS_EHCI,
        .instance_size = sizeof(FUSBH200EHCIState),
        .instance_init = fusbh200_ehci_init,
        .class_init    = fusbh200_ehci_class_init,
    },
};

DEFINE_TYPES(ehci_sysbus_types)
