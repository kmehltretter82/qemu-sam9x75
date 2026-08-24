/*
 * Microchip SAM9X7 SD/MMC host controller
 *
 * The standard register bank is provided by QEMU's generic SDHCI core.
 * This wrapper adds the Microchip register window and writable capability
 * registers used by the Linux sdhci-of-at91 driver.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/sd/at91_sdhci.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "sdhci-internal.h"

#define AT91_SDHCI_MMIO_SIZE       0x300

#define SDMMC_CA0R                 0x040
#define SDMMC_CA1R                 0x044
#define SDMMC_MCCAR                0x048
#define SDMMC_CAPS_SIZE            0x00c

#define SDMMC_PVR0                 0x060
#define SDMMC_PVR_LAST             0x064
#define SDMMC_PRESET_SIZE          0x010

#define SDMMC_APSR                 0x200
#define SDMMC_MC1R                 0x204
#define SDMMC_MC2R                 0x205
#define SDMMC_ACR                  0x208
#define SDMMC_CC2R                 0x20c
#define SDMMC_CACR                 0x230
#define SDMMC_DBGR                 0x234
#define SDMMC_CALCR                0x240
#define SDMMC_VENDOR_BASE          SDMMC_APSR
#define SDMMC_VENDOR_SIZE          0x044

#define SDMMC_CA0R_RESET           0x27e832b2
#define SDMMC_CA1R_RESET           0x00010070
#define SDMMC_HCVR_RESET           0x1802
#define SDMMC_APSR_RESET           0x0000000f

#define SDMMC_CA0R_WRITE_MASK      0xf700ffbf
#define SDMMC_CA1R_WRITE_MASK      0x00ff0077
#define SDMMC_MCCAR_WRITE_MASK     0x00ffffff
#define SDMMC_PVR_WRITE_MASK       0x07ff

#define SDMMC_MC1R_MASK            0x000000bb
#define SDMMC_MC1R_FCD             BIT(7)
#define SDMMC_ACR_MASK             0x00000003
#define SDMMC_CC2R_MASK            0x00000001
#define SDMMC_CACR_CAPWREN         BIT(0)
#define SDMMC_CACR_KEY_MASK        0x0000ff00
#define SDMMC_CACR_KEY             0x00004600
#define SDMMC_DBGR_MASK            0x00000001
#define SDMMC_CALCR_MASK           0x0000001f
#define SDMMC_CALCR_EN             BIT(0)

static uint32_t at91_sdhci_lane_mask(hwaddr offset, unsigned int size)
{
    unsigned int shift = (offset & 3) * 8;

    return size == 4 ? UINT32_MAX :
           (uint32_t)MAKE_64BIT_MASK(shift, size * 8);
}

static uint32_t at91_sdhci_extract(uint32_t value, hwaddr offset,
                                   unsigned int size)
{
    return extract32(value, (offset & 3) * 8, size * 8);
}

static uint32_t at91_sdhci_merge(uint32_t old, hwaddr offset,
                                 uint64_t value, unsigned int size)
{
    unsigned int shift = (offset & 3) * 8;
    uint32_t lane_mask = at91_sdhci_lane_mask(offset, size);

    return (old & ~lane_mask) | (((uint32_t)value << shift) & lane_mask);
}

static uint32_t at91_sdhci_caps_reg(AT91SDHCIState *s, hwaddr offset)
{
    switch (offset & ~3) {
    case 0:
        return s->capareg;
    case 4:
        return s->capareg >> 32;
    case 8:
        return s->maxcurr;
    default:
        g_assert_not_reached();
    }
}

static uint64_t at91_sdhci_caps_read(void *opaque, hwaddr offset,
                                     unsigned int size)
{
    AT91SDHCIState *s = opaque;

    return at91_sdhci_extract(at91_sdhci_caps_reg(s, offset), offset, size);
}

static void at91_sdhci_caps_write(void *opaque, hwaddr offset,
                                  uint64_t value, unsigned int size)
{
    AT91SDHCIState *s = opaque;
    hwaddr reg_offset = offset & ~3;
    uint32_t old = at91_sdhci_caps_reg(s, offset);
    uint32_t merged = at91_sdhci_merge(old, offset, value, size);
    uint32_t write_mask;
    uint32_t result;

    if (!(s->cacr & SDMMC_CACR_CAPWREN)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SDHCI ": write to locked capability "
                      "register 0x%03" HWADDR_PRIx "\n",
                      SDMMC_CA0R + offset);
        return;
    }

    switch (reg_offset) {
    case 0:
        write_mask = SDMMC_CA0R_WRITE_MASK;
        result = (old & ~write_mask) | (merged & write_mask);
        s->capareg = (s->capareg & 0xffffffff00000000ULL) | result;
        break;
    case 4:
        write_mask = SDMMC_CA1R_WRITE_MASK;
        result = (old & ~write_mask) | (merged & write_mask);
        s->capareg = (s->capareg & 0x00000000ffffffffULL) |
                     ((uint64_t)result << 32);
        break;
    case 8:
        write_mask = SDMMC_MCCAR_WRITE_MASK;
        result = (old & ~write_mask) | (merged & write_mask);
        s->maxcurr = (s->maxcurr & 0xffffffff00000000ULL) | result;
        break;
    default:
        g_assert_not_reached();
    }

    s->sdhci.capareg = s->capareg;
    s->sdhci.maxcurr = s->maxcurr;
}

static bool at91_sdhci_caps_accepts(void *opaque, hwaddr offset,
                                    unsigned int size, bool is_write,
                                    MemTxAttrs attrs)
{
    return offset + size <= SDMMC_CAPS_SIZE &&
           (offset & (size - 1)) == 0 &&
           (offset & ~3) == ((offset + size - 1) & ~3);
}

static const MemoryRegionOps at91_sdhci_caps_ops = {
    .read = at91_sdhci_caps_read,
    .write = at91_sdhci_caps_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = at91_sdhci_caps_accepts,
    },
};

static uint64_t at91_sdhci_preset_read(void *opaque, hwaddr offset,
                                       unsigned int size)
{
    AT91SDHCIState *s = opaque;

    if (offset <= SDMMC_PVR_LAST - SDMMC_PVR0) {
        return s->pvr[offset / 2];
    }

    /* SAM9X7 implements only PVR0-PVR2; the remaining slots are reserved. */
    return 0;
}

static void at91_sdhci_preset_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned int size)
{
    AT91SDHCIState *s = opaque;

    if (offset <= SDMMC_PVR_LAST - SDMMC_PVR0) {
        s->pvr[offset / 2] = value & SDMMC_PVR_WRITE_MASK;
    }
    /* Reserved preset slots are read-zero/write-ignore on SAM9X7. */
}

static bool at91_sdhci_preset_accepts(void *opaque, hwaddr offset,
                                      unsigned int size, bool is_write,
                                      MemTxAttrs attrs)
{
    return size == 2 && !(offset & 1) &&
           offset + size <= SDMMC_PRESET_SIZE;
}

static const MemoryRegionOps at91_sdhci_preset_ops = {
    .read = at91_sdhci_preset_read,
    .write = at91_sdhci_preset_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
        .accepts = at91_sdhci_preset_accepts,
    },
};

static uint32_t at91_sdhci_vendor_reg(AT91SDHCIState *s, hwaddr offset)
{
    switch (offset & ~3) {
    case SDMMC_APSR - SDMMC_VENDOR_BASE:
        return SDMMC_APSR_RESET;
    case SDMMC_MC1R - SDMMC_VENDOR_BASE:
        return s->mc1r;
    case SDMMC_ACR - SDMMC_VENDOR_BASE:
        return s->acr;
    case SDMMC_CC2R - SDMMC_VENDOR_BASE:
        return s->cc2r;
    case SDMMC_CACR - SDMMC_VENDOR_BASE:
        return s->cacr;
    case SDMMC_DBGR - SDMMC_VENDOR_BASE:
        return s->dbgr;
    case SDMMC_CALCR - SDMMC_VENDOR_BASE:
        return s->calcr;
    default:
        return 0;
    }
}

static uint64_t at91_sdhci_vendor_read(void *opaque, hwaddr offset,
                                       unsigned int size)
{
    AT91SDHCIState *s = opaque;
    hwaddr reg_offset = offset & ~3;

    switch (reg_offset + SDMMC_VENDOR_BASE) {
    case SDMMC_APSR:
    case SDMMC_MC1R:
    case SDMMC_ACR:
    case SDMMC_CC2R:
    case SDMMC_CACR:
    case SDMMC_DBGR:
    case SDMMC_CALCR:
        return at91_sdhci_extract(at91_sdhci_vendor_reg(s, offset),
                                  offset, size);
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AT91_SDHCI ": read from reserved vendor "
                      "offset 0x%03" HWADDR_PRIx "\n",
                      offset + SDMMC_VENDOR_BASE);
        return 0;
    }
}

static void at91_sdhci_force_card_detect(AT91SDHCIState *s)
{
    if (s->mc1r & SDMMC_MC1R_FCD) {
        s->sdhci.prnsts |= 0x01f70000;
    }
}

static void at91_sdhci_vendor_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned int size)
{
    AT91SDHCIState *s = opaque;
    hwaddr reg_offset = offset & ~3;
    uint32_t old = at91_sdhci_vendor_reg(s, offset);
    uint32_t merged = at91_sdhci_merge(old, offset, value, size);

    switch (reg_offset + SDMMC_VENDOR_BASE) {
    case SDMMC_APSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SDHCI ": write to read-only APSR\n");
        break;
    case SDMMC_MC1R:
        /* MC2R occupies byte one and contains self-clearing commands. */
        s->mc1r = merged & SDMMC_MC1R_MASK;
        at91_sdhci_force_card_detect(s);
        break;
    case SDMMC_ACR:
        s->acr = merged & SDMMC_ACR_MASK;
        break;
    case SDMMC_CC2R:
        s->cc2r = merged & SDMMC_CC2R_MASK;
        break;
    case SDMMC_CACR:
        if ((merged & SDMMC_CACR_KEY_MASK) == SDMMC_CACR_KEY) {
            s->cacr = merged & SDMMC_CACR_CAPWREN;
        }
        break;
    case SDMMC_DBGR:
        s->dbgr = merged & SDMMC_DBGR_MASK;
        break;
    case SDMMC_CALCR:
        /* Calibration completes immediately; EN therefore self-clears. */
        s->calcr = (merged & SDMMC_CALCR_MASK) & ~SDMMC_CALCR_EN;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AT91_SDHCI ": write to reserved vendor "
                      "offset 0x%03" HWADDR_PRIx " value 0x%08" PRIx64
                      "\n", offset + SDMMC_VENDOR_BASE, value);
        break;
    }
}

static bool at91_sdhci_vendor_accepts(void *opaque, hwaddr offset,
                                      unsigned int size, bool is_write,
                                      MemTxAttrs attrs)
{
    return offset + size <= SDMMC_VENDOR_SIZE &&
           (offset & (size - 1)) == 0 &&
           (offset & ~3) == ((offset + size - 1) & ~3);
}

static const MemoryRegionOps at91_sdhci_vendor_ops = {
    .read = at91_sdhci_vendor_read,
    .write = at91_sdhci_vendor_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = at91_sdhci_vendor_accepts,
    },
};

static void at91_sdhci_reset(DeviceState *dev)
{
    AT91SDHCIState *s = AT91_SDHCI(dev);

    device_cold_reset(DEVICE(&s->sdhci));

    s->capareg = ((uint64_t)SDMMC_CA1R_RESET << 32) |
                 SDMMC_CA0R_RESET;
    s->maxcurr = 0;
    memset(s->pvr, 0, sizeof(s->pvr));
    s->mc1r = 0;
    s->acr = 0;
    s->cc2r = 0;
    s->cacr = 0;
    s->dbgr = 0;
    s->calcr = 0;

    s->sdhci.capareg = s->capareg;
    s->sdhci.maxcurr = s->maxcurr;
    s->sdhci.version = SDMMC_HCVR_RESET;
}

static void at91_sdhci_realize(DeviceState *dev, Error **errp)
{
    AT91SDHCIState *s = AT91_SDHCI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    SysBusDevice *sdhci_sbd = SYS_BUS_DEVICE(&s->sdhci);

    if (!clock_has_source(s->hclock) || !clock_has_source(s->gclock)) {
        error_setg(errp, TYPE_AT91_SDHCI
                   ": hclock and gclock must be connected");
        return;
    }

    qdev_prop_set_uint8(DEVICE(&s->sdhci), "sd-spec-version", 3);
    qdev_prop_set_uint8(DEVICE(&s->sdhci), "uhs", UHS_I);
    qdev_prop_set_uint64(DEVICE(&s->sdhci), "capareg",
                         ((uint64_t)SDMMC_CA1R_RESET << 32) |
                         SDMMC_CA0R_RESET);
    qdev_prop_set_uint64(DEVICE(&s->sdhci), "maxcurr", 0);

    if (!sysbus_realize(sdhci_sbd, errp)) {
        return;
    }

    memory_region_init(&s->container, OBJECT(s),
                       TYPE_AT91_SDHCI ".container",
                       AT91_SDHCI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->container);
    memory_region_add_subregion(&s->container, 0,
                                sysbus_mmio_get_region(sdhci_sbd, 0));

    memory_region_init_io(&s->caps_iomem, OBJECT(s), &at91_sdhci_caps_ops,
                          s, TYPE_AT91_SDHCI ".capabilities",
                          SDMMC_CAPS_SIZE);
    memory_region_add_subregion_overlap(&s->container, SDMMC_CA0R,
                                        &s->caps_iomem, 1);

    memory_region_init_io(&s->preset_iomem, OBJECT(s),
                          &at91_sdhci_preset_ops, s,
                          TYPE_AT91_SDHCI ".presets", SDMMC_PRESET_SIZE);
    memory_region_add_subregion_overlap(&s->container, SDMMC_PVR0,
                                        &s->preset_iomem, 1);

    memory_region_init_io(&s->vendor_iomem, OBJECT(s),
                          &at91_sdhci_vendor_ops, s,
                          TYPE_AT91_SDHCI ".vendor", SDMMC_VENDOR_SIZE);
    memory_region_add_subregion(&s->container, SDMMC_VENDOR_BASE,
                                &s->vendor_iomem);

    sysbus_pass_irq(sbd, sdhci_sbd);
    s->bus = qdev_get_child_bus(DEVICE(sdhci_sbd), "sd-bus");

    at91_sdhci_reset(dev);
}

static int at91_sdhci_post_load(void *opaque, int version_id)
{
    AT91SDHCIState *s = opaque;

    s->sdhci.capareg = s->capareg;
    s->sdhci.maxcurr = s->maxcurr;
    s->sdhci.version = SDMMC_HCVR_RESET;
    at91_sdhci_force_card_detect(s);
    return 0;
}

static const VMStateDescription at91_sdhci_vmstate = {
    .name = TYPE_AT91_SDHCI,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = at91_sdhci_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(hclock, AT91SDHCIState),
        VMSTATE_CLOCK(gclock, AT91SDHCIState),
        VMSTATE_UINT64(capareg, AT91SDHCIState),
        VMSTATE_UINT64(maxcurr, AT91SDHCIState),
        VMSTATE_UINT16_ARRAY_V(pvr, AT91SDHCIState, 3, 2),
        VMSTATE_UINT8(mc1r, AT91SDHCIState),
        VMSTATE_UINT32(acr, AT91SDHCIState),
        VMSTATE_UINT32(cc2r, AT91SDHCIState),
        VMSTATE_UINT32(cacr, AT91SDHCIState),
        VMSTATE_UINT32(dbgr, AT91SDHCIState),
        VMSTATE_UINT32(calcr, AT91SDHCIState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_sdhci_init(Object *obj)
{
    AT91SDHCIState *s = AT91_SDHCI(obj);
    DeviceState *dev = DEVICE(obj);

    object_initialize_child(obj, "sdhci", &s->sdhci, TYPE_SYSBUS_SDHCI);
    s->hclock = qdev_init_clock_in(dev, "hclock", NULL, s, ClockUpdate);
    s->gclock = qdev_init_clock_in(dev, "gclock", NULL, s, ClockUpdate);
}

static void at91_sdhci_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 SD/MMC host controller";
    dc->realize = at91_sdhci_realize;
    dc->vmsd = &at91_sdhci_vmstate;
    device_class_set_legacy_reset(dc, at91_sdhci_reset);
}

static const TypeInfo at91_sdhci_info = {
    .name = TYPE_AT91_SDHCI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SDHCIState),
    .instance_init = at91_sdhci_init,
    .class_init = at91_sdhci_class_init,
};

static void at91_sdhci_register_types(void)
{
    type_register_static(&at91_sdhci_info);
}

type_init(at91_sdhci_register_types)
