/*
 * Microchip AT91 programmable multibit ECC controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/irq.h"
#include "hw/misc/at91_pmecc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PMECC_CFG          0x00
#define PMECC_SAREA        0x04
#define PMECC_SADDR        0x08
#define PMECC_EADDR        0x0c
#define PMECC_RESERVED_CLK 0x10
#define PMECC_CTRL         0x14
#define PMECC_SR           0x18
#define PMECC_IER          0x1c
#define PMECC_IDR          0x20
#define PMECC_IMR          0x24
#define PMECC_ISR          0x28
#define PMECC_BANK_COUNT   8
#define PMECC_BANK_STRIDE  0x40
#define PMECC_ECC_FIRST    0x40
#define PMECC_ECC_SIZE     0x2c
#define PMECC_REM_FIRST    0x240
#define PMECC_REM_SIZE     0x30
#define PMECC_MMIO_SIZE    0x600

#define PMECC_CFG_MASK     0x00111317
#define PMECC_CTRL_RST     BIT(0)
#define PMECC_CTRL_DATA    BIT(1)
#define PMECC_CTRL_USER    BIT(2)
#define PMECC_CTRL_ENABLE  BIT(4)
#define PMECC_CTRL_DISABLE BIT(5)
#define PMECC_SR_BUSY      BIT(0)
#define PMECC_SR_ENABLE    BIT(4)

#define PMERRLOC_CFG       0x00
#define PMERRLOC_PRIM      0x04
#define PMERRLOC_LEN       0x08
#define PMERRLOC_DIS       0x0c
#define PMERRLOC_SR        0x10
#define PMERRLOC_IER       0x14
#define PMERRLOC_IDR       0x18
#define PMERRLOC_IMR       0x1c
#define PMERRLOC_ISR       0x20
#define PMERRLOC_SIGMA0    0x28
#define PMERRLOC_SIGMA_LAST 0x88
#define PMERRLOC_EL_FIRST  0x8c
#define PMERRLOC_MMIO_SIZE 0x100

#define PMERRLOC_DONE      BIT(0)

static void at91_pmecc_update_irq(AT91PMECCState *s)
{
    qemu_set_irq(s->irq, (s->isr & s->imr) ||
                         (s->errloc_isr & s->errloc_imr));
}

static bool at91_pmecc_in_banks(hwaddr offset, hwaddr first,
                                hwaddr bank_size)
{
    hwaddr bank_offset;

    if (offset < first) {
        return false;
    }

    bank_offset = offset - first;
    return bank_offset / PMECC_BANK_STRIDE < PMECC_BANK_COUNT &&
           bank_offset % PMECC_BANK_STRIDE < bank_size;
}

static uint64_t at91_pmecc_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    switch (offset) {
    case PMECC_CFG:
        return s->cfg;
    case PMECC_SAREA:
        return s->sarea;
    case PMECC_SADDR:
        return s->saddr;
    case PMECC_EADDR:
        return s->eaddr;
    case PMECC_RESERVED_CLK:
        /* SAM9X7 controls the PMECC clock in the PMC, not this register. */
        return 0;
    case PMECC_CTRL:
        return 0;
    case PMECC_SR:
        return s->enabled ? PMECC_SR_ENABLE : 0;
    case PMECC_IER:
    case PMECC_IDR:
        return 0;
    case PMECC_IMR:
        return s->imr;
    case PMECC_ISR:
        return s->isr;
    default:
        if (at91_pmecc_in_banks(offset, PMECC_ECC_FIRST,
                                PMECC_ECC_SIZE)) {
            /* An erased data stream generates erased ECC bytes. */
            return MAKE_64BIT_MASK(0, size * 8);
        }
        if (at91_pmecc_in_banks(offset, PMECC_REM_FIRST,
                                PMECC_REM_SIZE)) {
            return 0;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_pmecc_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    switch (offset) {
    case PMECC_CFG:
        s->cfg = value & PMECC_CFG_MASK;
        break;
    case PMECC_SAREA:
        s->sarea = value & 0x1ff;
        break;
    case PMECC_SADDR:
        s->saddr = value & 0x1ff;
        break;
    case PMECC_EADDR:
        s->eaddr = value & 0x1ff;
        break;
    case PMECC_RESERVED_CLK:
        /* Kept for compatibility with the inherited Atmel PMECC driver. */
        break;
    case PMECC_CTRL:
        if (value & PMECC_CTRL_RST) {
            s->isr = 0;
        }
        if (value & PMECC_CTRL_DISABLE) {
            s->enabled = false;
        }
        if (value & PMECC_CTRL_ENABLE) {
            s->enabled = true;
        }
        /* DATA and USER select the monitored stream; completion is instant. */
        if (value & (PMECC_CTRL_DATA | PMECC_CTRL_USER)) {
            s->isr = 0;
        }
        at91_pmecc_update_irq(s);
        break;
    case PMECC_IER:
        s->imr |= value & BIT(0);
        at91_pmecc_update_irq(s);
        break;
    case PMECC_IDR:
        s->imr &= ~(value & BIT(0));
        at91_pmecc_update_irq(s);
        break;
    case PMECC_SR:
    case PMECC_IMR:
    case PMECC_ISR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static uint64_t at91_pmerrloc_read(void *opaque, hwaddr offset,
                                   unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    switch (offset) {
    case PMERRLOC_CFG:
        return s->errloc_cfg;
    case PMERRLOC_PRIM:
        return s->errloc_prim;
    case PMERRLOC_LEN:
        return s->errloc_len;
    case PMERRLOC_DIS:
    case PMERRLOC_IER:
    case PMERRLOC_IDR:
        return 0;
    case PMERRLOC_SR:
        return 0;
    case PMERRLOC_IMR:
        return s->errloc_imr;
    case PMERRLOC_ISR:
        return s->errloc_isr;
    default:
        if (offset >= PMERRLOC_SIGMA0 && offset <= PMERRLOC_SIGMA_LAST &&
            !(offset & 3)) {
            return s->sigma[(offset - PMERRLOC_SIGMA0) / 4];
        }
        if (offset >= PMERRLOC_EL_FIRST && offset < PMERRLOC_MMIO_SIZE) {
            return 0;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": errloc read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_pmerrloc_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    switch (offset) {
    case PMERRLOC_CFG:
        s->errloc_cfg = value & 0x001f0001;
        s->errloc_isr = 0;
        break;
    case PMERRLOC_PRIM:
        s->errloc_prim = value & 0x3fff;
        break;
    case PMERRLOC_LEN:
        s->errloc_len = value & 0x3fff;
        s->errloc_isr = PMERRLOC_DONE;
        at91_pmecc_update_irq(s);
        break;
    case PMERRLOC_DIS:
        s->errloc_isr = 0;
        at91_pmecc_update_irq(s);
        break;
    case PMERRLOC_IER:
        s->errloc_imr |= value & PMERRLOC_DONE;
        at91_pmecc_update_irq(s);
        break;
    case PMERRLOC_IDR:
        s->errloc_imr &= ~(value & PMERRLOC_DONE);
        at91_pmecc_update_irq(s);
        break;
    default:
        if (offset >= PMERRLOC_SIGMA0 && offset <= PMERRLOC_SIGMA_LAST &&
            !(offset & 3)) {
            s->sigma[(offset - PMERRLOC_SIGMA0) / 4] = value & 0x3fff;
            break;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": errloc write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_pmecc_ops = {
    .read = at91_pmecc_read,
    .write = at91_pmecc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps at91_pmerrloc_ops = {
    .read = at91_pmerrloc_read,
    .write = at91_pmerrloc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_pmecc_reset(DeviceState *dev)
{
    AT91PMECCState *s = AT91_PMECC(dev);

    s->cfg = 0;
    s->sarea = 0;
    s->saddr = 0;
    s->eaddr = 0;
    s->imr = 0;
    s->isr = 0;
    s->enabled = false;
    s->errloc_cfg = 0;
    s->errloc_prim = 0;
    s->errloc_len = 0;
    s->errloc_imr = 0;
    s->errloc_isr = 0;
    memset(s->sigma, 0, sizeof(s->sigma));
    at91_pmecc_update_irq(s);
}

static int at91_pmecc_post_load(void *opaque, int version_id)
{
    at91_pmecc_update_irq(AT91_PMECC(opaque));
    return 0;
}

static const VMStateDescription at91_pmecc_vmstate = {
    .name = TYPE_AT91_PMECC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_pmecc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, AT91PMECCState),
        VMSTATE_UINT32(cfg, AT91PMECCState),
        VMSTATE_UINT32(sarea, AT91PMECCState),
        VMSTATE_UINT32(saddr, AT91PMECCState),
        VMSTATE_UINT32(eaddr, AT91PMECCState),
        VMSTATE_UINT32(imr, AT91PMECCState),
        VMSTATE_UINT32(isr, AT91PMECCState),
        VMSTATE_BOOL(enabled, AT91PMECCState),
        VMSTATE_UINT32(errloc_cfg, AT91PMECCState),
        VMSTATE_UINT32(errloc_prim, AT91PMECCState),
        VMSTATE_UINT32(errloc_len, AT91PMECCState),
        VMSTATE_UINT32(errloc_imr, AT91PMECCState),
        VMSTATE_UINT32(errloc_isr, AT91PMECCState),
        VMSTATE_UINT32_ARRAY(sigma, AT91PMECCState, 25),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_pmecc_init(Object *obj)
{
    AT91PMECCState *s = AT91_PMECC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->pmecc_mmio, obj, &at91_pmecc_ops, s,
                          TYPE_AT91_PMECC, PMECC_MMIO_SIZE);
    memory_region_init_io(&s->errloc_mmio, obj, &at91_pmerrloc_ops, s,
                          TYPE_AT91_PMECC ".errloc", PMERRLOC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->pmecc_mmio);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->errloc_mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    s->pclk = qdev_init_clock_in(dev, "pclk", NULL, NULL, 0);
}

static void at91_pmecc_realize(DeviceState *dev, Error **errp)
{
    AT91PMECCState *s = AT91_PMECC(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_PMECC ": pclk must be connected");
    }
}

static void at91_pmecc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 programmable multibit ECC controller";
    dc->realize = at91_pmecc_realize;
    dc->vmsd = &at91_pmecc_vmstate;
    device_class_set_legacy_reset(dc, at91_pmecc_reset);
}

static const TypeInfo at91_pmecc_info = {
    .name = TYPE_AT91_PMECC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91PMECCState),
    .instance_init = at91_pmecc_init,
    .class_init = at91_pmecc_class_init,
};

static void at91_pmecc_register_types(void)
{
    type_register_static(&at91_pmecc_info);
}

type_init(at91_pmecc_register_types)
