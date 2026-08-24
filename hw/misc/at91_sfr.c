/*
 * Microchip SAM9X7 special function registers
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/at91_sfr.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SFR_CCFG_EBICSA           0x004
#define SFR_OHCIICR               0x010
#define SFR_OHCIISR               0x014
#define SFR_UTMIHSTRIM            0x034
#define SFR_UTMIFSTRIM            0x038
#define SFR_UTMISWAP              0x03c
#define SFR_LS                    0x0a0
#define SFR_CAL1                  0x0b4
#define SFR_WPMR                  0x0e4
#define SFR_PUFCTL                0x200
#define SFR_PUFDIS                0x208
#define SFR_PUFRUCR0              0x20c
#define SFR_PUFRUCR1              0x210
#define SFR_PUFWORUCR0            0x214
#define SFR_PUFWORUCR1            0x218
#define SFR_FLEXRAMS_CLKG_DIS     0x220
#define SFR_ISS_CFG               0x240
#define SFR_TSU_CFG               0x250
#define SFR_REMAP_MP_DDR          0x260
#define SFR_MMIO_SIZE             0x264

#define SFR_WPMR_WPEN             BIT(0)
#define SFR_WPMR_KEY_MASK         0xffffff00
#define SFR_WPMR_KEY              0x53465200

#define SFR_CCFG_EBICSA_MASK      0x03100306
#define SFR_OHCIICR_MASK          0x00000717
#define SFR_OHCIICR_ARIE          BIT(4)
#define SFR_UTMIHSTRIM_MASK       0x00077700
#define SFR_UTMIFSTRIM_MASK       0x77770000
#define SFR_LS_MASK               0x00013fff
#define SFR_CAL1_MASK             0x000001ff
#define SFR_PUFCTL_WRITE_MASK     0x0000007f
#define SFR_PUFCTL_PUFDIS         BIT(4)
#define SFR_PUFCTL_STATUS_MASK    (BIT(9) | BIT(8))
#define SFR_PUFDIS_MASK           0x00001fff

static bool at91_sfr_write_protected(AT91SFRState *s)
{
    return s->wpmr & SFR_WPMR_WPEN;
}

static void at91_sfr_update_irq(AT91SFRState *s)
{
    qemu_set_irq(s->irq, (s->ohciicr & SFR_OHCIICR_ARIE) && s->ohciisr);
}

static void at91_sfr_resume(void *opaque, int n, int level)
{
    AT91SFRState *s = AT91_SFR(opaque);

    s->ohciisr = deposit32(s->ohciisr, n, 1, level);
    at91_sfr_update_irq(s);
}

static uint64_t at91_sfr_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91SFRState *s = AT91_SFR(opaque);

    switch (offset) {
    case SFR_CCFG_EBICSA:
        return s->ccfg_ebicsa;
    case SFR_OHCIICR:
        return s->ohciicr;
    case SFR_OHCIISR:
        return s->ohciisr;
    case SFR_UTMIHSTRIM:
        return s->utmihstrim;
    case SFR_UTMIFSTRIM:
        return s->utmifstrim;
    case SFR_UTMISWAP:
        return s->utmiswap;
    case SFR_LS:
        return s->ls;
    case SFR_CAL1:
        return s->cal1;
    case SFR_WPMR:
        return s->wpmr;
    case SFR_PUFCTL:
        return s->pufctl;
    case SFR_PUFDIS:
        return s->pufdis;
    case SFR_PUFRUCR0:
        return s->pufrucr[0];
    case SFR_PUFRUCR1:
        return s->pufrucr[1];
    case SFR_PUFWORUCR0:
        return s->pufworucr[0];
    case SFR_PUFWORUCR1:
        return s->pufworucr[1];
    case SFR_FLEXRAMS_CLKG_DIS:
        return s->flexrams_clkg_dis;
    case SFR_ISS_CFG:
        return s->iss_cfg;
    case SFR_TSU_CFG:
        return s->tsu_cfg;
    case SFR_REMAP_MP_DDR:
        return s->remap_mp_ddr;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AT91_SFR ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static uint32_t at91_sfr_preserve_reserved(uint32_t old, uint32_t value,
                                           uint32_t mask)
{
    return (old & ~mask) | (value & mask);
}

static void at91_sfr_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91SFRState *s = AT91_SFR(opaque);
    uint32_t val = value;

    if (offset == SFR_WPMR) {
        if ((val & SFR_WPMR_KEY_MASK) == SFR_WPMR_KEY) {
            s->wpmr = val & SFR_WPMR_WPEN;
        }
        return;
    }

    if (at91_sfr_write_protected(s)) {
        return;
    }

    switch (offset) {
    case SFR_CCFG_EBICSA:
        s->ccfg_ebicsa = val & SFR_CCFG_EBICSA_MASK;
        break;
    case SFR_OHCIICR:
        s->ohciicr = val & SFR_OHCIICR_MASK;
        at91_sfr_update_irq(s);
        break;
    case SFR_OHCIISR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SFR ": write to read-only OHCIISR\n");
        break;
    case SFR_UTMIHSTRIM:
        s->utmihstrim = at91_sfr_preserve_reserved(s->utmihstrim, val,
                                                   SFR_UTMIHSTRIM_MASK);
        break;
    case SFR_UTMIFSTRIM:
        s->utmifstrim = at91_sfr_preserve_reserved(s->utmifstrim, val,
                                                   SFR_UTMIFSTRIM_MASK);
        break;
    case SFR_UTMISWAP:
        s->utmiswap = val & 0x7;
        break;
    case SFR_LS:
        s->ls = val & SFR_LS_MASK;
        break;
    case SFR_CAL1:
        s->cal1 = val & SFR_CAL1_MASK;
        break;
    case SFR_PUFCTL:
        s->pufctl = (s->pufctl & SFR_PUFCTL_STATUS_MASK) |
                    (val & SFR_PUFCTL_WRITE_MASK) |
                    (s->pufctl & SFR_PUFCTL_PUFDIS);
        break;
    case SFR_PUFDIS:
        s->pufdis |= val & SFR_PUFDIS_MASK;
        break;
    case SFR_PUFRUCR0:
        s->pufrucr[0] = val;
        break;
    case SFR_PUFRUCR1:
        s->pufrucr[1] = val;
        break;
    case SFR_PUFWORUCR0:
        s->pufworucr[0] |= val;
        break;
    case SFR_PUFWORUCR1:
        s->pufworucr[1] |= val;
        break;
    case SFR_FLEXRAMS_CLKG_DIS:
        s->flexrams_clkg_dis = val & 0x3;
        break;
    case SFR_ISS_CFG:
        s->iss_cfg = val & 0x1;
        break;
    case SFR_TSU_CFG:
        s->tsu_cfg = val & 0xff;
        break;
    case SFR_REMAP_MP_DDR:
        s->remap_mp_ddr = val & 0x3fff;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AT91_SFR ": write to reserved offset 0x%"
                      HWADDR_PRIx " value 0x%08" PRIx32 "\n",
                      offset, val);
        break;
    }
}

static const MemoryRegionOps at91_sfr_ops = {
    .read = at91_sfr_read,
    .write = at91_sfr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_sfr_reset(DeviceState *dev)
{
    AT91SFRState *s = AT91_SFR(dev);

    s->ccfg_ebicsa = 0x00000300;
    s->ohciicr = 0;
    s->ohciisr = 0;
    s->utmihstrim = 0x00044433;
    s->utmifstrim = 0x00430211;
    s->utmiswap = 0;
    s->ls = 0;
    s->cal1 = 0x00000084;
    s->wpmr = 0;
    s->pufctl = 0x00000148;
    s->pufdis = 0;
    memset(s->pufrucr, 0, sizeof(s->pufrucr));
    memset(s->pufworucr, 0, sizeof(s->pufworucr));
    s->flexrams_clkg_dis = 0;
    s->iss_cfg = 0;
    s->tsu_cfg = 0x00000043;
    s->remap_mp_ddr = 0;
    at91_sfr_update_irq(s);
}

static void at91_sfr_init(Object *obj)
{
    AT91SFRState *s = AT91_SFR(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_sfr_ops, s,
                          TYPE_AT91_SFR, SFR_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(DEVICE(obj), at91_sfr_resume, "resume", 3);
}

static int at91_sfr_post_load(void *opaque, int version_id)
{
    AT91SFRState *s = AT91_SFR(opaque);

    at91_sfr_update_irq(s);
    return 0;
}

static const VMStateDescription at91_sfr_vmstate = {
    .name = TYPE_AT91_SFR,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_sfr_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ccfg_ebicsa, AT91SFRState),
        VMSTATE_UINT32(ohciicr, AT91SFRState),
        VMSTATE_UINT32(ohciisr, AT91SFRState),
        VMSTATE_UINT32(utmihstrim, AT91SFRState),
        VMSTATE_UINT32(utmifstrim, AT91SFRState),
        VMSTATE_UINT32(utmiswap, AT91SFRState),
        VMSTATE_UINT32(ls, AT91SFRState),
        VMSTATE_UINT32(cal1, AT91SFRState),
        VMSTATE_UINT32(wpmr, AT91SFRState),
        VMSTATE_UINT32(pufctl, AT91SFRState),
        VMSTATE_UINT32(pufdis, AT91SFRState),
        VMSTATE_UINT32_ARRAY(pufrucr, AT91SFRState, 2),
        VMSTATE_UINT32_ARRAY(pufworucr, AT91SFRState, 2),
        VMSTATE_UINT32(flexrams_clkg_dis, AT91SFRState),
        VMSTATE_UINT32(iss_cfg, AT91SFRState),
        VMSTATE_UINT32(tsu_cfg, AT91SFRState),
        VMSTATE_UINT32(remap_mp_ddr, AT91SFRState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_sfr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 special function registers";
    dc->vmsd = &at91_sfr_vmstate;
    device_class_set_legacy_reset(dc, at91_sfr_reset);
}

static const TypeInfo at91_sfr_info = {
    .name = TYPE_AT91_SFR,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SFRState),
    .instance_init = at91_sfr_init,
    .class_init = at91_sfr_class_init,
};

static void at91_sfr_register_types(void)
{
    type_register_static(&at91_sfr_info);
}

type_init(at91_sfr_register_types)
