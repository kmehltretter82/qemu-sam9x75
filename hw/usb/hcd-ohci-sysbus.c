/*
 * QEMU USB OHCI Emulation
 * Copyright (c) 2006 Openedhand Ltd.
 * Copyright (c) 2010 CodeSourcery
 * Copyright (c) 2024 Red Hat, Inc.
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
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/usb/usb.h"
#include "migration/vmstate.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "trace.h"
#include "hcd-ohci.h"

static bool ohci_sysbus_vmstate_needed(void *opaque)
{
    OHCISysBusState *s = SYSBUS_OHCI(opaque);

    return s->migrate_state;
}

static const VMStateDescription vmstate_ohci_sysbus = {
    .name = "ohci-sysbus",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ohci_sysbus_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ohci, OHCISysBusState, 1, vmstate_ohci_state,
                       OHCIState),
        VMSTATE_END_OF_LIST()
    },
};


static void ohci_sysbus_realize(DeviceState *dev, Error **errp)
{
    OHCISysBusState *s = SYSBUS_OHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    Error *err = NULL;

    if (object_dynamic_cast(OBJECT(dev), TYPE_AT91_UHPHS_OHCI) &&
        s->num_ports != 3) {
        error_setg(errp, "%s requires exactly 3 ports",
                   TYPE_AT91_UHPHS_OHCI);
        return;
    }

    usb_ohci_init(&s->ohci, dev, s->num_ports, s->dma_offset,
                  s->masterbus, s->firstport,
                  &address_space_memory, ohci_sysbus_die, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }
    sysbus_init_irq(sbd, &s->ohci.irq);
    sysbus_init_mmio(sbd, &s->ohci.mem);
}

static void ohci_sysbus_reset(DeviceState *dev)
{
    OHCISysBusState *s = SYSBUS_OHCI(dev);
    OHCIState *ohci = &s->ohci;

    ohci_hard_reset(ohci);
}

static const Property ohci_sysbus_properties[] = {
    DEFINE_PROP_STRING("masterbus", OHCISysBusState, masterbus),
    DEFINE_PROP_UINT32("num-ports", OHCISysBusState, num_ports, 3),
    DEFINE_PROP_UINT32("firstport", OHCISysBusState, firstport, 0),
    DEFINE_PROP_UINT64("dma-offset", OHCISysBusState, dma_offset, 0),
    DEFINE_PROP_BOOL("migrate-state", OHCISysBusState, migrate_state, false),
    DEFINE_PROP_BOOL("x-migrate-async-state", OHCISysBusState,
                     ohci.migrate_async_state, false),
    DEFINE_PROP_BOOL("pxa-extensions", OHCISysBusState,
                     ohci.pxa_extensions, false),
};

static void ohci_sysbus_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ohci_sysbus_realize;
    dc->vmsd = &vmstate_ohci_sysbus;
    set_bit(DEVICE_CATEGORY_USB, dc->categories);
    dc->desc = "OHCI USB Controller";
    device_class_set_props(dc, ohci_sysbus_properties);
    device_class_set_legacy_reset(dc, ohci_sysbus_reset);
}

static void at91_uhphs_ohci_init(Object *obj)
{
    OHCISysBusState *s = SYSBUS_OHCI(obj);
    OHCIState *ohci = &s->ohci;

    /* SAM9X7 Series Data Sheet, UHPFS register reset values. */
    s->migrate_state = true;
    ohci->migrate_async_state = true;
    ohci->custom_reset_values = true;
    ohci->reset_intr = 0;
    ohci->reset_fsmps = 0;
    ohci->reset_rhdesc_a = 0x0a001203;
    ohci->reset_port_ctrl = 0x00000100;
}

static const TypeInfo ohci_sysbus_types[] = {
    {
        .name          = TYPE_SYSBUS_OHCI,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(OHCISysBusState),
        .class_init    = ohci_sysbus_class_init,
    },
    {
        .name          = TYPE_AT91_UHPHS_OHCI,
        .parent        = TYPE_SYSBUS_OHCI,
        .instance_init = at91_uhphs_ohci_init,
    },
};

DEFINE_TYPES(ohci_sysbus_types);
