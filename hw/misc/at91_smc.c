/*
 * Microchip AT91 static memory controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/misc/at91_smc.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SMC_OCMS            0x80
#define SMC_KEY1            0x84
#define SMC_KEY2            0x88
#define SMC_SRIER           0x90
#define SMC_WPMR            0xe4
#define SMC_WPSR            0xe8
#define SMC_MMIO_SIZE       0x100

#define SMC_SETUP_MASK      0x3f3f3f3f
#define SMC_PULSE_MASK      0x7f7f7f7f
#define SMC_CYCLE_MASK      0x01ff01ff
#define SMC_MODE_MASK       0x311f3133
#define SMC_WPMR_KEY        0x534d4300
#define SMC_WPMR_KEY_MASK   0xffffff00

#define SMC_WPSR_WPVS       BIT(0)
#define SMC_WPSR_SEQE       BIT(2)
#define SMC_WPSR_SWE        BIT(3)
#define SMC_WPSR_SRC_MASK   0x00ffff00
#define SMC_WPSR_TYPE_MASK  0x03000000
#define SMC_WPSR_TYPE_WO    (1U << 24)
#define SMC_WPSR_TYPE_UNDEF (2U << 24)

static void at91_smc_software_error(AT91SMCState *s, hwaddr offset,
                                    uint32_t type)
{
    s->wpsr &= ~(SMC_WPSR_SRC_MASK | SMC_WPSR_TYPE_MASK);
    s->wpsr |= SMC_WPSR_SWE | type | ((offset & 0xffff) << 8);
}

static bool at91_smc_write_protected(AT91SMCState *s, hwaddr offset)
{
    if (!(s->wpmr & BIT(0))) {
        return false;
    }

    if (!(s->wpsr & SMC_WPSR_WPVS)) {
        s->wpsr |= SMC_WPSR_WPVS | ((offset & 0xffff) << 8);
    }
    return true;
}

static uint64_t at91_smc_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91SMCState *s = AT91_SMC(opaque);
    unsigned int cs;
    unsigned int reg;
    uint32_t value;

    if (offset < AT91_SMC_NUM_CS * 0x10) {
        cs = offset / 0x10;
        reg = (offset & 0xf) / 4;
        switch (reg) {
        case 0:
            return s->setup[cs];
        case 1:
            return s->pulse[cs];
        case 2:
            return s->cycle[cs];
        case 3:
            return s->mode[cs];
        default:
            g_assert_not_reached();
        }
    }

    switch (offset) {
    case SMC_OCMS:
        return s->ocms;
    case SMC_KEY1:
    case SMC_KEY2:
    case SMC_SRIER:
        at91_smc_software_error(s, offset, 0);
        return 0;
    case SMC_WPMR:
        return s->wpmr & BIT(0);
    case SMC_WPSR:
        value = s->wpsr;
        s->wpsr &= ~(SMC_WPSR_TYPE_MASK | SMC_WPSR_SWE | SMC_WPSR_SEQE);
        return value;
    default:
        at91_smc_software_error(s, offset, SMC_WPSR_TYPE_UNDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SMC ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_smc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91SMCState *s = AT91_SMC(opaque);
    unsigned int cs;
    unsigned int reg;

    if (offset < AT91_SMC_NUM_CS * 0x10) {
        if (at91_smc_write_protected(s, offset)) {
            return;
        }
        cs = offset / 0x10;
        reg = (offset & 0xf) / 4;
        switch (reg) {
        case 0:
            s->setup[cs] = value & SMC_SETUP_MASK;
            return;
        case 1:
            s->pulse[cs] = value & SMC_PULSE_MASK;
            return;
        case 2:
            s->cycle[cs] = value & SMC_CYCLE_MASK;
            return;
        case 3:
            s->mode[cs] = value & SMC_MODE_MASK;
            return;
        default:
            g_assert_not_reached();
        }
    }

    switch (offset) {
    case SMC_OCMS:
        if (!at91_smc_write_protected(s, offset)) {
            s->ocms = value & 0x00070101;
        }
        break;
    case SMC_KEY1:
        if (!at91_smc_write_protected(s, offset)) {
            s->key1 = value;
        }
        break;
    case SMC_KEY2:
        if (!at91_smc_write_protected(s, offset)) {
            s->key2 = value;
        }
        break;
    case SMC_SRIER:
        if (!at91_smc_write_protected(s, offset)) {
            s->srier = value & BIT(0);
        }
        break;
    case SMC_WPMR:
        if ((value & SMC_WPMR_KEY_MASK) == SMC_WPMR_KEY) {
            s->wpmr = value & BIT(0);
        }
        break;
    case SMC_WPSR:
        at91_smc_software_error(s, offset, SMC_WPSR_TYPE_WO);
        break;
    default:
        at91_smc_software_error(s, offset, SMC_WPSR_TYPE_UNDEF);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SMC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_smc_ops = {
    .read = at91_smc_read,
    .write = at91_smc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_smc_reset(DeviceState *dev)
{
    AT91SMCState *s = AT91_SMC(dev);
    unsigned int i;

    for (i = 0; i < AT91_SMC_NUM_CS; i++) {
        s->setup[i] = 0x01010101;
        s->pulse[i] = 0x01010101;
        s->cycle[i] = 0x00030003;
        s->mode[i] = 0x10001000;
    }
    s->ocms = 0;
    s->key1 = 0;
    s->key2 = 0;
    s->srier = 0;
    s->wpmr = 0;
    s->wpsr = 0;
}

static const VMStateDescription at91_smc_vmstate = {
    .name = TYPE_AT91_SMC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(setup, AT91SMCState, AT91_SMC_NUM_CS),
        VMSTATE_UINT32_ARRAY(pulse, AT91SMCState, AT91_SMC_NUM_CS),
        VMSTATE_UINT32_ARRAY(cycle, AT91SMCState, AT91_SMC_NUM_CS),
        VMSTATE_UINT32_ARRAY(mode, AT91SMCState, AT91_SMC_NUM_CS),
        VMSTATE_UINT32(ocms, AT91SMCState),
        VMSTATE_UINT32(key1, AT91SMCState),
        VMSTATE_UINT32(key2, AT91SMCState),
        VMSTATE_UINT32(srier, AT91SMCState),
        VMSTATE_UINT32(wpmr, AT91SMCState),
        VMSTATE_UINT32(wpsr, AT91SMCState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_smc_init(Object *obj)
{
    AT91SMCState *s = AT91_SMC(obj);

    memory_region_init_io(&s->mmio, obj, &at91_smc_ops, s,
                          TYPE_AT91_SMC, SMC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void at91_smc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 static memory controller";
    dc->vmsd = &at91_smc_vmstate;
    device_class_set_legacy_reset(dc, at91_smc_reset);
}

static const TypeInfo at91_smc_info = {
    .name = TYPE_AT91_SMC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SMCState),
    .instance_init = at91_smc_init,
    .class_init = at91_smc_class_init,
};

static void at91_smc_register_types(void)
{
    type_register_static(&at91_smc_info);
}

type_init(at91_smc_register_types)
