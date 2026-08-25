/*
 * Microchip SAM9X7 multi-port DDR-SDRAM controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/misc/at91_mpddrc.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define MPDDRC_MR                 0x00
#define MPDDRC_RTR                0x04
#define MPDDRC_CR                 0x08
#define MPDDRC_TPR0               0x0c
#define MPDDRC_TPR1               0x10
#define MPDDRC_TPR2               0x14
#define MPDDRC_LPR                0x1c
#define MPDDRC_MD                 0x20
#define MPDDRC_DDR3_CAL           0x2c
#define MPDDRC_DDR3_TIM_CAL       0x30
#define MPDDRC_IO_CALIBR          0x34
#define MPDDRC_OCMS               0x38
#define MPDDRC_OCMS_KEY1          0x3c
#define MPDDRC_OCMS_KEY2          0x40
#define MPDDRC_CONF_ARBITER       0x44
#define MPDDRC_TIMEOUT            0x48
#define MPDDRC_REQ_PORT_0123      0x4c
#define MPDDRC_REQ_PORT_456       0x50
#define MPDDRC_BDW_PORT_0123      0x54
#define MPDDRC_BDW_PORT_456       0x58
#define MPDDRC_RD_DATA_PATH       0x5c
#define MPDDRC_MCFGR              0x60
#define MPDDRC_MADDR0             0x64
#define MPDDRC_MADDR6             0x7c
#define MPDDRC_MINFO0             0x84
#define MPDDRC_MINFO6             0x9c
#define MPDDRC_IER                0xc0
#define MPDDRC_IDR                0xc4
#define MPDDRC_IMR                0xc8
#define MPDDRC_ISR                0xcc
#define MPDDRC_SAFETY             0xd0
#define MPDDRC_WPMR               0xe4
#define MPDDRC_WPSR               0xe8
#define MPDDRC_MMIO_SIZE          0xec

#define MPDDRC_WPMR_WPEN          BIT(0)
#define MPDDRC_WPMR_WPITEN        BIT(1)
#define MPDDRC_WPMR_FIRSTE        BIT(4)
#define MPDDRC_WPMR_MASK          (MPDDRC_WPMR_WPEN | \
                                   MPDDRC_WPMR_WPITEN | \
                                   MPDDRC_WPMR_FIRSTE)
#define MPDDRC_WPMR_KEY_MASK      0xffffff00
#define MPDDRC_WPMR_KEY           0x44445200

#define MPDDRC_IRQ_MASK           0x3
#define MPDDRC_ISR_SEC            BIT(0)

#define MPDDRC_WPSR_WPVS          BIT(0)
#define MPDDRC_WPSR_SWE           BIT(3)
#define MPDDRC_WPSR_SWETYP_SHIFT  24
#define MPDDRC_WPSR_ECLASS        BIT(31)

enum {
    MPDDRC_SWE_READ_WO,
    MPDDRC_SWE_WRITE_RO,
    MPDDRC_SWE_UNDEF_RW,
    MPDDRC_SWE_WRITE_AFTER_INIT,
};

static uint32_t *at91_mpddrc_reg(AT91MPDDRCState *s, hwaddr offset)
{
    return &s->regs[offset >> 2];
}

static void at91_mpddrc_update_irq(AT91MPDDRCState *s)
{
    qemu_set_irq(s->irq, (*at91_mpddrc_reg(s, MPDDRC_IMR) &
                          *at91_mpddrc_reg(s, MPDDRC_ISR)) != 0);
}

static bool at91_mpddrc_first_error_latched(AT91MPDDRCState *s)
{
    return (*at91_mpddrc_reg(s, MPDDRC_WPMR) & MPDDRC_WPMR_FIRSTE) &&
           *at91_mpddrc_reg(s, MPDDRC_WPSR);
}

static void at91_mpddrc_security_event(AT91MPDDRCState *s)
{
    *at91_mpddrc_reg(s, MPDDRC_ISR) |= MPDDRC_ISR_SEC;
    at91_mpddrc_update_irq(s);
}

static void at91_mpddrc_report_wp(AT91MPDDRCState *s, hwaddr offset)
{
    uint32_t *wpsr = at91_mpddrc_reg(s, MPDDRC_WPSR);

    if (!at91_mpddrc_first_error_latched(s)) {
        *wpsr = (*wpsr & 0xff0000fe) | MPDDRC_WPSR_WPVS |
                ((offset & 0xffff) << 8);
    } else {
        *wpsr |= MPDDRC_WPSR_WPVS;
    }
    at91_mpddrc_security_event(s);
}

static void at91_mpddrc_report_swe(AT91MPDDRCState *s, hwaddr offset,
                                   unsigned int type)
{
    uint32_t *wpsr = at91_mpddrc_reg(s, MPDDRC_WPSR);

    if (!at91_mpddrc_first_error_latched(s)) {
        uint32_t wpvs = *wpsr & (MPDDRC_WPSR_WPVS | 0x00ffff00);

        *wpsr = wpvs | MPDDRC_WPSR_SWE |
                (type << MPDDRC_WPSR_SWETYP_SHIFT);
        if (!(wpvs & MPDDRC_WPSR_WPVS)) {
            *wpsr |= (offset & 0xffff) << 8;
        }
        if (type == MPDDRC_SWE_WRITE_AFTER_INIT) {
            *wpsr |= MPDDRC_WPSR_ECLASS;
        }
    } else {
        *wpsr |= MPDDRC_WPSR_SWE;
    }
    at91_mpddrc_security_event(s);
}

static bool at91_mpddrc_general_protected(hwaddr offset)
{
    switch (offset) {
    case MPDDRC_MR:
    case MPDDRC_RTR:
    case MPDDRC_CR:
    case MPDDRC_TPR0:
    case MPDDRC_TPR1:
    case MPDDRC_MD:
    case MPDDRC_OCMS:
    case MPDDRC_OCMS_KEY1:
    case MPDDRC_OCMS_KEY2:
        return true;
    default:
        return false;
    }
}

static bool at91_mpddrc_write_protected(AT91MPDDRCState *s,
                                        hwaddr offset)
{
    uint32_t wpmr = *at91_mpddrc_reg(s, MPDDRC_WPMR);
    bool protected = (at91_mpddrc_general_protected(offset) &&
                      (wpmr & MPDDRC_WPMR_WPEN)) ||
                     ((offset == MPDDRC_IER || offset == MPDDRC_IDR) &&
                      (wpmr & MPDDRC_WPMR_WPITEN));

    if (protected) {
        at91_mpddrc_report_wp(s, offset);
    }
    return protected;
}

static bool at91_mpddrc_is_config(hwaddr offset)
{
    switch (offset) {
    case MPDDRC_CR:
    case MPDDRC_TPR0:
    case MPDDRC_TPR1:
    case MPDDRC_TPR2:
    case MPDDRC_MD:
    case MPDDRC_DDR3_CAL:
    case MPDDRC_DDR3_TIM_CAL:
    case MPDDRC_IO_CALIBR:
    case MPDDRC_OCMS:
    case MPDDRC_OCMS_KEY1:
    case MPDDRC_OCMS_KEY2:
        return true;
    default:
        return false;
    }
}

static uint32_t at91_mpddrc_write_mask(hwaddr offset)
{
    switch (offset) {
    case MPDDRC_MR:
        return 0x00000007;
    case MPDDRC_RTR:
        return 0x00000fff;
    case MPDDRC_CR:
        return 0x1cf1f3ff;
    case MPDDRC_TPR0:
        return 0xf7ffffff;
    case MPDDRC_TPR1:
        return 0x0fffff7f;
    case MPDDRC_TPR2:
        return 0x00ff7fff;
    case MPDDRC_LPR:
        return 0x0133f007;
    case MPDDRC_MD:
        return 0x00000017;
    case MPDDRC_DDR3_CAL:
        return 0x0000ffff;
    case MPDDRC_DDR3_TIM_CAL:
        return 0x000000ff;
    case MPDDRC_IO_CALIBR:
        return 0x00007fff;
    case MPDDRC_OCMS:
        return 0x00000011;
    case MPDDRC_CONF_ARBITER:
        return 0x7f7f7f0f;
    case MPDDRC_TIMEOUT:
        return 0x0fffffff;
    case MPDDRC_REQ_PORT_456:
        return 0x00ffffff;
    case MPDDRC_RD_DATA_PATH:
        return 0x00000003;
    case MPDDRC_MCFGR:
        return 0x00003f13;
    case MPDDRC_SAFETY:
        return 0x1fffffff;
    default:
        return UINT32_MAX;
    }
}

static bool at91_mpddrc_normal_register(hwaddr offset)
{
    if (offset <= MPDDRC_TPR2 && offset != 0x18) {
        return true;
    }
    if (offset == MPDDRC_LPR || offset == MPDDRC_MD ||
        (offset >= MPDDRC_DDR3_CAL && offset <= MPDDRC_REQ_PORT_456) ||
        offset == MPDDRC_RD_DATA_PATH || offset == MPDDRC_MCFGR ||
        (offset >= MPDDRC_MADDR0 && offset <= MPDDRC_MADDR6) ||
        offset == MPDDRC_SAFETY) {
        return true;
    }
    return false;
}

static uint64_t at91_mpddrc_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    AT91MPDDRCState *s = AT91_MPDDRC(opaque);
    uint32_t value;

    if (at91_mpddrc_normal_register(offset)) {
        value = *at91_mpddrc_reg(s, offset);
        if (offset == MPDDRC_LPR && (value & 0x3) == 1) {
            value |= BIT(25);
        }
        return value;
    }

    if (offset == MPDDRC_BDW_PORT_0123 ||
        offset == MPDDRC_BDW_PORT_456 ||
        (offset >= MPDDRC_MINFO0 && offset <= MPDDRC_MINFO6) ||
        offset == MPDDRC_IMR) {
        return *at91_mpddrc_reg(s, offset);
    }

    switch (offset) {
    case MPDDRC_ISR:
        value = *at91_mpddrc_reg(s, offset);
        *at91_mpddrc_reg(s, offset) = 0;
        at91_mpddrc_update_irq(s);
        return value;
    case MPDDRC_WPMR:
        return *at91_mpddrc_reg(s, offset);
    case MPDDRC_WPSR:
        value = *at91_mpddrc_reg(s, offset);
        *at91_mpddrc_reg(s, offset) = 0;
        return value;
    case MPDDRC_OCMS_KEY1:
    case MPDDRC_OCMS_KEY2:
    case MPDDRC_IER:
    case MPDDRC_IDR:
        at91_mpddrc_report_swe(s, offset, MPDDRC_SWE_READ_WO);
        return 0;
    default:
        at91_mpddrc_report_swe(s, offset, MPDDRC_SWE_UNDEF_RW);
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AT91_MPDDRC ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_mpddrc_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    AT91MPDDRCState *s = AT91_MPDDRC(opaque);
    uint32_t val = value;
    uint32_t *reg;

    if (offset == MPDDRC_WPMR) {
        if ((val & MPDDRC_WPMR_KEY_MASK) == MPDDRC_WPMR_KEY) {
            *at91_mpddrc_reg(s, offset) = val & MPDDRC_WPMR_MASK;
        }
        return;
    }

    if (at91_mpddrc_write_protected(s, offset)) {
        return;
    }

    if (offset == MPDDRC_IER) {
        *at91_mpddrc_reg(s, MPDDRC_IMR) |= val & MPDDRC_IRQ_MASK;
        at91_mpddrc_update_irq(s);
        return;
    }
    if (offset == MPDDRC_IDR) {
        *at91_mpddrc_reg(s, MPDDRC_IMR) &= ~(val & MPDDRC_IRQ_MASK);
        at91_mpddrc_update_irq(s);
        return;
    }

    if (offset == MPDDRC_OCMS_KEY1 || offset == MPDDRC_OCMS_KEY2) {
        unsigned int key = (offset - MPDDRC_OCMS_KEY1) / 4;

        if (!s->key_written[key]) {
            *at91_mpddrc_reg(s, offset) = val;
            s->key_written[key] = true;
        }
        return;
    }

    if (at91_mpddrc_normal_register(offset)) {
        if (at91_mpddrc_is_config(offset) &&
            (*at91_mpddrc_reg(s, MPDDRC_RTR) & 0xfff)) {
            at91_mpddrc_report_swe(s, offset,
                                   MPDDRC_SWE_WRITE_AFTER_INIT);
        }

        reg = at91_mpddrc_reg(s, offset);
        if (offset == MPDDRC_IO_CALIBR) {
            *reg = (*reg & 0x00ff0000) |
                   (val & at91_mpddrc_write_mask(offset));
        } else if (offset == MPDDRC_LPR) {
            *reg = val & at91_mpddrc_write_mask(offset);
        } else if (offset == MPDDRC_MCFGR && (val & BIT(1))) {
            memset(at91_mpddrc_reg(s, MPDDRC_MINFO0), 0,
                   MPDDRC_MINFO6 - MPDDRC_MINFO0 + 4);
            *reg = val & (at91_mpddrc_write_mask(offset) & ~BIT(1));
        } else {
            *reg = val & at91_mpddrc_write_mask(offset);
        }
        return;
    }

    if (offset == MPDDRC_BDW_PORT_0123 ||
        offset == MPDDRC_BDW_PORT_456 ||
        (offset >= MPDDRC_MINFO0 && offset <= MPDDRC_MINFO6) ||
        offset == MPDDRC_IMR || offset == MPDDRC_ISR ||
        offset == MPDDRC_WPSR) {
        at91_mpddrc_report_swe(s, offset, MPDDRC_SWE_WRITE_RO);
        return;
    }

    at91_mpddrc_report_swe(s, offset, MPDDRC_SWE_UNDEF_RW);
    qemu_log_mask(LOG_UNIMP,
                  TYPE_AT91_MPDDRC ": write to reserved offset 0x%"
                  HWADDR_PRIx " value 0x%08" PRIx32 "\n", offset, val);
}

static const MemoryRegionOps at91_mpddrc_ops = {
    .read = at91_mpddrc_read,
    .write = at91_mpddrc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_mpddrc_reset(DeviceState *dev)
{
    AT91MPDDRCState *s = AT91_MPDDRC(dev);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->key_written, 0, sizeof(s->key_written));

    *at91_mpddrc_reg(s, MPDDRC_CR) = 0x00207024;
    *at91_mpddrc_reg(s, MPDDRC_TPR0) = 0x20227225;
    *at91_mpddrc_reg(s, MPDDRC_TPR1) = 0x03c80808;
    *at91_mpddrc_reg(s, MPDDRC_TPR2) = 0x00042062;
    *at91_mpddrc_reg(s, MPDDRC_LPR) = 0x00010000;
    *at91_mpddrc_reg(s, MPDDRC_MD) = 0x00000013;
    *at91_mpddrc_reg(s, MPDDRC_DDR3_TIM_CAL) = 0x00000006;
    *at91_mpddrc_reg(s, MPDDRC_IO_CALIBR) = 0x00870000;
    at91_mpddrc_update_irq(s);
}

static void at91_mpddrc_init(Object *obj)
{
    AT91MPDDRCState *s = AT91_MPDDRC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_mpddrc_ops, s,
                          TYPE_AT91_MPDDRC, MPDDRC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
}

static int at91_mpddrc_post_load(void *opaque, int version_id)
{
    AT91MPDDRCState *s = AT91_MPDDRC(opaque);

    at91_mpddrc_update_irq(s);
    return 0;
}

static const VMStateDescription at91_mpddrc_vmstate = {
    .name = TYPE_AT91_MPDDRC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_mpddrc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AT91MPDDRCState,
                             AT91_MPDDRC_NUM_REGS),
        VMSTATE_BOOL_ARRAY(key_written, AT91MPDDRCState, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_mpddrc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 multi-port DDR-SDRAM controller";
    dc->vmsd = &at91_mpddrc_vmstate;
    device_class_set_legacy_reset(dc, at91_mpddrc_reset);
}

static const TypeInfo at91_mpddrc_info = {
    .name = TYPE_AT91_MPDDRC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91MPDDRCState),
    .instance_init = at91_mpddrc_init,
    .class_init = at91_mpddrc_class_init,
};

static void at91_mpddrc_register_types(void)
{
    type_register_static(&at91_mpddrc_info);
}

type_init(at91_mpddrc_register_types)
