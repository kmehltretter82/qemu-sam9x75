/*
 * Microchip SAM9X7 bus matrix
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/at91_matrix.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define MATRIX_MCFG(n)          (0x000 + (n) * 4)
#define MATRIX_SCFG(n)          (0x040 + (n) * 4)
#define MATRIX_PRAS(n)          (0x080 + (n) * 8)
#define MATRIX_PRBS(n)          (0x084 + (n) * 8)
#define MATRIX_MRCR             0x100
#define MATRIX_MEIER            0x150
#define MATRIX_MEIDR            0x154
#define MATRIX_MEIMR            0x158
#define MATRIX_MESR             0x15c
#define MATRIX_MEAR(n)          (0x160 + (n) * 4)
#define MATRIX_WPMR             0x1e4
#define MATRIX_WPSR             0x1e8
#define MATRIX_MMIO_SIZE        0x200

#define MATRIX_HOST_MASK        MAKE_64BIT_MASK(0, AT91_MATRIX_NUM_HOSTS)
#define MATRIX_CPU_HOST_MASK    (BIT(12) | BIT(13))
#define MATRIX_MCFG_MASK        0x00000007
#define MATRIX_SCFG_MASK        0x003f01ff
#define MATRIX_PRAS_MASK        0x77777777
#define MATRIX_PRBS_MASK        0x00777777

#define MATRIX_WPMR_WPEN        BIT(0)
#define MATRIX_WPMR_CFGFRZ      BIT(7)
#define MATRIX_WPMR_KEY_MASK    0xffffff00
#define MATRIX_WPMR_KEY         0x4d415400
#define MATRIX_WPMR_VALUE_MASK  (MATRIX_WPMR_WPEN | MATRIX_WPMR_CFGFRZ)

static const uint32_t matrix_pras_reset[AT91_MATRIX_NUM_CLIENTS] = {
    0x00000777, 0x00077777, 0x00007700, 0x00070000,
    0x00000077, 0x00077777, 0x00077000, 0x00007000,
    0x00077000, 0x00077000, 0x00077070, 0x00000000,
};

static const uint32_t matrix_prbs_reset[AT91_MATRIX_NUM_CLIENTS] = {
    0x00000000, 0x00110000, 0x00010000, 0x00100000,
    0x00000000, 0x00110000, 0x00110000, 0x00000000,
    0x00100000, 0x00100000, 0x00110000, 0x00110000,
};

static void at91_matrix_update_outputs(AT91MatrixState *s)
{
    qemu_set_irq(s->irq, !!(s->meimr & s->mesr));
    qemu_set_irq(s->cpu_remap, !!(s->mrcr & MATRIX_CPU_HOST_MASK));
}

static bool at91_matrix_write_blocked(AT91MatrixState *s, hwaddr offset)
{
    if (!(s->wpmr & MATRIX_WPMR_VALUE_MASK)) {
        return false;
    }

    s->wpsr = ((offset & 0xffff) << 8) | 1;
    return true;
}

static uint64_t at91_matrix_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    AT91MatrixState *s = AT91_MATRIX(opaque);
    unsigned int index;
    uint32_t value;

    if (offset <= MATRIX_MCFG(AT91_MATRIX_NUM_HOSTS - 1)) {
        return s->mcfg[offset / 4];
    }
    if (offset >= MATRIX_SCFG(0) &&
        offset <= MATRIX_SCFG(AT91_MATRIX_NUM_CLIENTS - 1)) {
        return s->scfg[(offset - MATRIX_SCFG(0)) / 4];
    }
    if (offset >= MATRIX_PRAS(0) &&
        offset <= MATRIX_PRBS(AT91_MATRIX_NUM_CLIENTS - 1)) {
        index = (offset - MATRIX_PRAS(0)) / 8;
        return (offset & 4) ? s->prbs[index] : s->pras[index];
    }
    if (offset >= MATRIX_MEAR(0) &&
        offset <= MATRIX_MEAR(AT91_MATRIX_NUM_HOSTS - 1)) {
        return s->mear[(offset - MATRIX_MEAR(0)) / 4];
    }

    switch (offset) {
    case MATRIX_MRCR:
        return s->mrcr;
    case MATRIX_MEIER:
    case MATRIX_MEIDR:
        return 0;
    case MATRIX_MEIMR:
        return s->meimr;
    case MATRIX_MESR:
        value = s->mesr;
        s->mesr = 0;
        at91_matrix_update_outputs(s);
        return value;
    case MATRIX_WPMR:
        return s->wpmr;
    case MATRIX_WPSR:
        return s->wpsr;
    default:
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AT91_MATRIX ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_matrix_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    AT91MatrixState *s = AT91_MATRIX(opaque);
    uint32_t val = value;
    unsigned int index;

    if (offset == MATRIX_WPMR) {
        if ((val & MATRIX_WPMR_KEY_MASK) == MATRIX_WPMR_KEY) {
            s->wpmr = (s->wpmr & MATRIX_WPMR_CFGFRZ) |
                      (val & MATRIX_WPMR_VALUE_MASK);
            s->wpsr = 0;
        }
        return;
    }

    if (offset <= MATRIX_MCFG(AT91_MATRIX_NUM_HOSTS - 1)) {
        if (!at91_matrix_write_blocked(s, offset)) {
            s->mcfg[offset / 4] = val & MATRIX_MCFG_MASK;
        }
        return;
    }
    if (offset >= MATRIX_SCFG(0) &&
        offset <= MATRIX_SCFG(AT91_MATRIX_NUM_CLIENTS - 1)) {
        if (!at91_matrix_write_blocked(s, offset)) {
            s->scfg[(offset - MATRIX_SCFG(0)) / 4] =
                val & MATRIX_SCFG_MASK;
        }
        return;
    }
    if (offset >= MATRIX_PRAS(0) &&
        offset <= MATRIX_PRBS(AT91_MATRIX_NUM_CLIENTS - 1)) {
        if (at91_matrix_write_blocked(s, offset)) {
            return;
        }
        index = (offset - MATRIX_PRAS(0)) / 8;
        if (offset & 4) {
            s->prbs[index] = val & MATRIX_PRBS_MASK;
        } else {
            s->pras[index] = val & MATRIX_PRAS_MASK;
        }
        return;
    }

    switch (offset) {
    case MATRIX_MRCR:
        if (!at91_matrix_write_blocked(s, offset)) {
            s->mrcr = val & MATRIX_HOST_MASK;
            at91_matrix_update_outputs(s);
        }
        break;
    case MATRIX_MEIER:
        if (!at91_matrix_write_blocked(s, offset)) {
            s->meimr |= val & MATRIX_HOST_MASK;
            at91_matrix_update_outputs(s);
        }
        break;
    case MATRIX_MEIDR:
        if (!at91_matrix_write_blocked(s, offset)) {
            s->meimr &= ~(val & MATRIX_HOST_MASK);
            at91_matrix_update_outputs(s);
        }
        break;
    case MATRIX_MEIMR:
    case MATRIX_MESR:
    case MATRIX_WPSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_MATRIX ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        if (offset >= MATRIX_MEAR(0) &&
            offset <= MATRIX_MEAR(AT91_MATRIX_NUM_HOSTS - 1)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          TYPE_AT91_MATRIX ": write to read-only offset "
                          "0x%" HWADDR_PRIx "\n", offset);
        } else {
            qemu_log_mask(LOG_UNIMP,
                          TYPE_AT91_MATRIX ": write to reserved offset 0x%"
                          HWADDR_PRIx " value 0x%08" PRIx32 "\n",
                          offset, val);
        }
        break;
    }
}

static const MemoryRegionOps at91_matrix_ops = {
    .read = at91_matrix_read,
    .write = at91_matrix_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_matrix_reset(DeviceState *dev)
{
    AT91MatrixState *s = AT91_MATRIX(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->mcfg); i++) {
        s->mcfg[i] = 0x00000004;
    }
    for (i = 0; i < ARRAY_SIZE(s->scfg); i++) {
        s->scfg[i] = 0x000001ff;
    }
    memcpy(s->pras, matrix_pras_reset, sizeof(s->pras));
    memcpy(s->prbs, matrix_prbs_reset, sizeof(s->prbs));
    s->mrcr = 0;
    s->meimr = 0;
    s->mesr = 0;
    memset(s->mear, 0, sizeof(s->mear));
    s->wpmr = 0;
    s->wpsr = 0;
    at91_matrix_update_outputs(s);
}

static void at91_matrix_init(Object *obj)
{
    AT91MatrixState *s = AT91_MATRIX(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_matrix_ops, s,
                          TYPE_AT91_MATRIX, MATRIX_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), &s->cpu_remap, "cpu-remap", 1);
}

static int at91_matrix_post_load(void *opaque, int version_id)
{
    AT91MatrixState *s = AT91_MATRIX(opaque);

    at91_matrix_update_outputs(s);
    return 0;
}

static const VMStateDescription at91_matrix_vmstate = {
    .name = TYPE_AT91_MATRIX,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_matrix_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(mcfg, AT91MatrixState, AT91_MATRIX_NUM_HOSTS),
        VMSTATE_UINT32_ARRAY(scfg, AT91MatrixState, AT91_MATRIX_NUM_CLIENTS),
        VMSTATE_UINT32_ARRAY(pras, AT91MatrixState, AT91_MATRIX_NUM_CLIENTS),
        VMSTATE_UINT32_ARRAY(prbs, AT91MatrixState, AT91_MATRIX_NUM_CLIENTS),
        VMSTATE_UINT32(mrcr, AT91MatrixState),
        VMSTATE_UINT32(meimr, AT91MatrixState),
        VMSTATE_UINT32(mesr, AT91MatrixState),
        VMSTATE_UINT32_ARRAY(mear, AT91MatrixState, AT91_MATRIX_NUM_HOSTS),
        VMSTATE_UINT32(wpmr, AT91MatrixState),
        VMSTATE_UINT32(wpsr, AT91MatrixState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_matrix_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 bus matrix";
    dc->vmsd = &at91_matrix_vmstate;
    device_class_set_legacy_reset(dc, at91_matrix_reset);
}

static const TypeInfo at91_matrix_info = {
    .name = TYPE_AT91_MATRIX,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91MatrixState),
    .instance_init = at91_matrix_init,
    .class_init = at91_matrix_class_init,
};

static void at91_matrix_register_types(void)
{
    type_register_static(&at91_matrix_info);
}

type_init(at91_matrix_register_types)
