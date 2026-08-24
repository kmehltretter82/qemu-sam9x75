/*
 * Microchip AT91 octal/quad serial peripheral interface
 *
 * This is the QSPI revision used by SAM9X7.  It has separate interrupt and
 * synchronization status registers and a system-bus serial-memory window.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/irq.h"
#include "hw/ssi/at91_ospi.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "trace.h"

#define OSPI_REG_SIZE          0x100
#define OSPI_MEMORY_SIZE       0x20000000

#define OSPI_CR                0x00
#define OSPI_MR                0x04
#define OSPI_RDR               0x08
#define OSPI_TDR               0x0c
#define OSPI_ISR               0x10
#define OSPI_IER               0x14
#define OSPI_IDR               0x18
#define OSPI_IMR               0x1c
#define OSPI_SCR               0x20
#define OSPI_SR                0x24
#define OSPI_IAR               0x30
#define OSPI_WICR              0x34
#define OSPI_IFR               0x38
#define OSPI_RICR              0x3c
#define OSPI_SMR               0x40
#define OSPI_SKR               0x44
#define OSPI_REFRESH           0x50
#define OSPI_WRACNT            0x54
#define OSPI_TOUT              0x64
#define OSPI_WPMR              0xe4
#define OSPI_WPSR              0xe8

#define CR_QSPIEN              BIT(0)
#define CR_QSPIDIS             BIT(1)
#define CR_SRFRSH              BIT(5)
#define CR_SWRST               BIT(7)
#define CR_UPDCFG              BIT(8)
#define CR_STTFR               BIT(9)
#define CR_RTOUT               BIT(10)
#define CR_LASTXFER            BIT(24)

#define MR_SMM                 BIT(0)

#define ISR_RDRF               BIT(0)
#define ISR_TDRE               BIT(1)
#define ISR_TXEMPTY            BIT(2)
#define ISR_OVRES              BIT(3)
#define ISR_CSR                BIT(8)
#define ISR_INSTRE             BIT(10)
#define ISR_LWRA               BIT(11)
#define ISR_QITF               BIT(12)
#define ISR_QITR               BIT(13)
#define ISR_CSFA               BIT(14)
#define ISR_CSRA               BIT(15)
#define ISR_RFRSHD             BIT(16)
#define ISR_TOUT               BIT(17)
#define ISR_MASK               0x0003fd0f
#define ISR_READ_CLEAR_MASK    (ISR_CSR | ISR_INSTRE | ISR_QITF | ISR_QITR | \
                                ISR_CSFA | ISR_CSRA | ISR_RFRSHD | ISR_TOUT)

#define SR_SYNCBSY             BIT(0)
#define SR_QSPIENS             BIT(1)
#define SR_CSS                 BIT(2)
#define SR_RBUSY               BIT(3)
#define SR_HIDLE               BIT(4)

#define IFR_WIDTH_MASK         MAKE_64BIT_MASK(0, 4)
#define IFR_INSTEN             BIT(4)
#define IFR_ADDREN             BIT(5)
#define IFR_OPTEN              BIT(6)
#define IFR_DATAEN             BIT(7)
#define IFR_OPTL_SHIFT         8
#define IFR_ADDRL_SHIFT        10
#define IFR_TFRTYP             BIT(12)
#define IFR_DDREN              BIT(15)
#define IFR_NBDUM_SHIFT        16
#define IFR_NBDUM_MASK         MAKE_64BIT_MASK(IFR_NBDUM_SHIFT, 6)
#define IFR_APBTFRTYP          BIT(24)
#define IFR_PROTTYP_SHIFT      28
#define IFR_PROTTYP_MASK       MAKE_64BIT_MASK(IFR_PROTTYP_SHIFT, 3)

#define SMR_MASK               0x7
#define SMR_SCRKL              BIT(2)

#define WPMR_WPEN             BIT(0)
#define WPMR_WPITEN           BIT(1)
#define WPMR_WPCREN           BIT(2)
#define WPMR_ENABLE_MASK      0x7
#define WPMR_KEY              0x51535000

static void at91_ospi_update_irq(AT91OSPIState *s)
{
    qemu_set_irq(s->irq, !!(s->isr & s->imr));
}

static void at91_ospi_set_cs(AT91OSPIState *s, bool asserted)
{
    if (s->cs_asserted == asserted) {
        return;
    }

    s->cs_asserted = asserted;
    qemu_set_irq(s->cs, asserted ? 0 : 1);
    s->isr |= asserted ? ISR_CSFA : (ISR_CSR | ISR_CSRA);
    at91_ospi_update_irq(s);
}

static void at91_ospi_end_transfer(AT91OSPIState *s)
{
    if (s->transfer_active || s->cs_asserted) {
        s->isr |= ISR_INSTRE;
        at91_ospi_set_cs(s, false);
    }
    s->transfer_active = false;
    s->transfer_count = 0;
    at91_ospi_update_irq(s);
}

static unsigned int at91_ospi_address_width(uint32_t ifr)
{
    switch (ifr & IFR_WIDTH_MASK) {
    case 3: /* dual I/O */
    case 5: /* dual command */
        return 2;
    case 4: /* quad I/O */
    case 6: /* quad command */
        return 4;
    case 8: /* octal I/O */
    case 9: /* octal command */
        return 8;
    default:
        return 1;
    }
}

static void at91_ospi_send_instruction(AT91OSPIState *s, uint32_t code)
{
    unsigned int protocol = (s->ifr & IFR_PROTTYP_MASK) >>
                            IFR_PROTTYP_SHIFT;

    if (!(s->ifr & IFR_INSTEN)) {
        return;
    }

    /* OctaFlash and HyperFlash use the 16-bit instruction field. */
    if (protocol == 2 || protocol == 3) {
        ssi_transfer(s->spi, extract32(code, 8, 8));
    }
    ssi_transfer(s->spi, extract32(code, 0, 8));
}

static void at91_ospi_send_address(AT91OSPIState *s, uint32_t addr)
{
    int i;
    unsigned int bytes;

    if (!(s->ifr & IFR_ADDREN)) {
        return;
    }

    bytes = extract32(s->ifr, IFR_ADDRL_SHIFT, 2) + 1;
    for (i = bytes - 1; i >= 0; i--) {
        ssi_transfer(s->spi, extract32(addr, i * 8, 8));
    }
}

static void at91_ospi_send_option_and_dummy(AT91OSPIState *s, uint32_t code)
{
    unsigned int dummy_cycles;
    unsigned int dummy_bytes;
    unsigned int i;

    if (s->ifr & IFR_OPTEN) {
        /* SSI represents even sub-byte option phases as one transfer. */
        ssi_transfer(s->spi, extract32(code, 16, 8));
    }

    dummy_cycles = (s->ifr & IFR_NBDUM_MASK) >> IFR_NBDUM_SHIFT;
    dummy_bytes = DIV_ROUND_UP(dummy_cycles *
                              at91_ospi_address_width(s->ifr) *
                              ((s->ifr & IFR_DDREN) ? 2 : 1), 8);
    for (i = 0; i < dummy_bytes; i++) {
        ssi_transfer(s->spi, 0);
    }
}

static void at91_ospi_begin_transfer(AT91OSPIState *s, bool write,
                                     uint32_t addr)
{
    uint32_t code = write ? s->wicr : s->ricr;

    if (s->transfer_active) {
        at91_ospi_end_transfer(s);
    }

    trace_at91_ospi_transfer(write, code, addr, s->ifr);

    at91_ospi_set_cs(s, true);
    at91_ospi_send_instruction(s, code);
    at91_ospi_send_address(s, addr);
    at91_ospi_send_option_and_dummy(s, code);

    s->transfer_active = true;
    s->transfer_write = write;
    s->transfer_count = 0;
    s->next_addr = addr;
    at91_ospi_update_irq(s);
}

static void at91_ospi_soft_reset(AT91OSPIState *s)
{
    at91_ospi_end_transfer(s);
    s->mr = 0;
    s->isr = 0;
    s->imr = 0;
    s->scr = 0;
    s->iar = 0;
    s->wicr = 0;
    s->ifr = 0;
    s->ricr = 0;
    s->smr = 0;
    s->skr = 0;
    s->refresh = 0;
    s->wracnt = 0;
    s->tout = 0;
    s->wpmr = 0;
    s->wpsr = 0;
    s->rdr = 0;
    s->enabled = false;
    s->transfer_active = false;
    s->transfer_write = false;
    s->next_addr = 0;
    s->transfer_count = 0;
    qemu_set_irq(s->cs, 1);
    s->cs_asserted = false;
    at91_ospi_update_irq(s);
}

static void at91_ospi_write_violation(AT91OSPIState *s, hwaddr offset)
{
    s->wpsr = (extract32(offset, 0, 8) << 8) | BIT(0);
}

static bool at91_ospi_register_protected(AT91OSPIState *s, hwaddr offset)
{
    if ((s->wpmr & WPMR_WPCREN) && offset == OSPI_CR) {
        return true;
    }
    if ((s->wpmr & WPMR_WPITEN) &&
        (offset == OSPI_IER || offset == OSPI_IDR)) {
        return true;
    }
    if (!(s->wpmr & WPMR_WPEN)) {
        return false;
    }

    switch (offset) {
    case OSPI_MR:
    case OSPI_SCR:
    case OSPI_IAR:
    case OSPI_WICR:
    case OSPI_IFR:
    case OSPI_RICR:
    case OSPI_SMR:
    case OSPI_SKR:
    case OSPI_REFRESH:
    case OSPI_WRACNT:
    case OSPI_TOUT:
        return true;
    default:
        return false;
    }
}

static uint64_t at91_ospi_reg_read(void *opaque, hwaddr offset,
                                   unsigned int size)
{
    AT91OSPIState *s = AT91_OSPI(opaque);
    uint32_t value;

    switch (offset) {
    case OSPI_CR:
    case OSPI_TDR:
    case OSPI_IER:
    case OSPI_IDR:
    case OSPI_SKR:
        return 0;
    case OSPI_MR:
        return s->mr;
    case OSPI_RDR:
        value = s->rdr;
        s->isr &= ~ISR_RDRF;
        at91_ospi_update_irq(s);
        return value;
    case OSPI_ISR:
        value = s->isr;
        trace_at91_ospi_status_read(value,
            (s->enabled ? SR_QSPIENS : 0) |
            ((s->enabled && !s->cs_asserted) ? SR_CSS | SR_HIDLE : 0));
        s->isr &= ~ISR_READ_CLEAR_MASK;
        at91_ospi_update_irq(s);
        return value;
    case OSPI_IMR:
        return s->imr;
    case OSPI_SCR:
        return s->scr;
    case OSPI_SR:
        value = 0;
        if (s->enabled) {
            value |= SR_QSPIENS;
            if (!s->cs_asserted) {
                value |= SR_CSS | SR_HIDLE;
            }
        }
        trace_at91_ospi_status_read(s->isr, value);
        return value;
    case OSPI_IAR:
        return s->iar;
    case OSPI_WICR:
        return s->wicr;
    case OSPI_IFR:
        return s->ifr;
    case OSPI_RICR:
        return s->ricr;
    case OSPI_SMR:
        return s->smr;
    case OSPI_REFRESH:
        return s->refresh;
    case OSPI_WRACNT:
        return s->wracnt;
    case OSPI_TOUT:
        return s->tout;
    case OSPI_WPMR:
        return s->wpmr & WPMR_ENABLE_MASK;
    case OSPI_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_OSPI ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_ospi_reg_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned int size)
{
    AT91OSPIState *s = AT91_OSPI(opaque);
    uint32_t v = value;

    trace_at91_ospi_reg_write(offset, v);

    if (at91_ospi_register_protected(s, offset)) {
        at91_ospi_write_violation(s, offset);
        return;
    }

    switch (offset) {
    case OSPI_CR:
        if (v & CR_SWRST) {
            at91_ospi_soft_reset(s);
            break;
        }
        if (v & CR_QSPIDIS) {
            at91_ospi_end_transfer(s);
            s->enabled = false;
        } else if (v & CR_QSPIEN) {
            s->enabled = true;
            /* Enabling starts the OSPI analog-block refresh sequence. */
            s->isr |= ISR_TDRE | ISR_TXEMPTY | ISR_RFRSHD;
        }
        if (v & CR_SRFRSH) {
            s->isr |= ISR_RFRSHD;
        }
        if (v & CR_RTOUT) {
            s->isr &= ~ISR_TOUT;
        }
        if (v & CR_STTFR) {
            bool write = !(s->ifr & IFR_APBTFRTYP);

            at91_ospi_begin_transfer(s, write, s->iar);
            if (!(s->ifr & IFR_DATAEN)) {
                at91_ospi_end_transfer(s);
            }
        }
        if (v & CR_LASTXFER) {
            at91_ospi_end_transfer(s);
        }
        /* UPDCFG is instantaneous in the functional model. */
        at91_ospi_update_irq(s);
        break;
    case OSPI_MR:
        s->mr = v & 0xffff2fbd;
        break;
    case OSPI_TDR:
        if (!s->transfer_active) {
            at91_ospi_begin_transfer(s, true, s->iar);
        }
        s->rdr = ssi_transfer(s->spi, v & 0xffff);
        s->isr |= ISR_RDRF | ISR_TDRE | ISR_TXEMPTY;
        at91_ospi_update_irq(s);
        break;
    case OSPI_IER:
        s->imr |= v & ISR_MASK;
        at91_ospi_update_irq(s);
        break;
    case OSPI_IDR:
        s->imr &= ~(v & ISR_MASK);
        at91_ospi_update_irq(s);
        break;
    case OSPI_SCR:
        s->scr = v & 0x00ffff03;
        break;
    case OSPI_IAR:
        s->iar = v;
        break;
    case OSPI_WICR:
        s->wicr = v & 0x00ffffff;
        break;
    case OSPI_IFR:
        s->ifr = v & 0x7fffdfff;
        break;
    case OSPI_RICR:
        s->ricr = v & 0x00ffffff;
        break;
    case OSPI_SMR:
        if (s->smr & SMR_SCRKL) {
            s->smr = (s->smr & SMR_SCRKL) | (v & ~SMR_SCRKL & SMR_MASK);
        } else {
            s->smr = v & SMR_MASK;
        }
        break;
    case OSPI_SKR:
        if (!(s->smr & SMR_SCRKL)) {
            s->skr = v;
        }
        break;
    case OSPI_REFRESH:
        s->refresh = v;
        break;
    case OSPI_WRACNT:
        s->wracnt = v;
        break;
    case OSPI_TOUT:
        s->tout = v & 0xffff;
        break;
    case OSPI_WPMR:
        if ((v & 0xffffff00) == WPMR_KEY) {
            s->wpmr = v & WPMR_ENABLE_MASK;
        }
        break;
    case OSPI_RDR:
    case OSPI_ISR:
    case OSPI_IMR:
    case OSPI_SR:
    case OSPI_WPSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_OSPI ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_OSPI ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_ospi_reg_ops = {
    .read = at91_ospi_reg_read,
    .write = at91_ospi_reg_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static uint64_t at91_ospi_memory_read(void *opaque, hwaddr offset,
                                      unsigned int size)
{
    AT91OSPIState *s = AT91_OSPI(opaque);
    uint64_t value = 0;
    unsigned int i;

    if (!s->enabled || !(s->mr & MR_SMM) || !(s->ifr & IFR_DATAEN)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_OSPI ": memory read while controller or "
                      "serial-memory transfer is disabled\n");
        return MAKE_64BIT_MASK(0, size * 8);
    }

    if (!s->transfer_active || s->transfer_write ||
        ((s->ifr & IFR_ADDREN) && s->next_addr != offset)) {
        at91_ospi_begin_transfer(s, false, offset);
    }

    for (i = 0; i < size; i++) {
        value |= (uint64_t)(ssi_transfer(s->spi, 0) & 0xff) << (8 * i);
    }
    s->next_addr = offset + size;
    s->transfer_count += size;
    trace_at91_ospi_memory_read(offset, size, value);
    return value;
}

static void at91_ospi_memory_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned int size)
{
    AT91OSPIState *s = AT91_OSPI(opaque);
    unsigned int i;

    if (!s->enabled || !(s->mr & MR_SMM) || !(s->ifr & IFR_DATAEN)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_OSPI ": memory write while controller or "
                      "serial-memory transfer is disabled\n");
        return;
    }

    if (!s->transfer_active || !s->transfer_write ||
        ((s->ifr & IFR_ADDREN) && s->next_addr != offset)) {
        at91_ospi_begin_transfer(s, true, offset);
    }

    for (i = 0; i < size; i++) {
        ssi_transfer(s->spi, extract64(value, 8 * i, 8));
    }
    s->next_addr = offset + size;
    s->transfer_count += size;
    trace_at91_ospi_memory_write(offset, size, value);
    if (!s->wracnt || s->transfer_count >= s->wracnt) {
        s->isr |= ISR_LWRA;
        at91_ospi_update_irq(s);
    }
}

static const MemoryRegionOps at91_ospi_memory_ops = {
    .read = at91_ospi_memory_read,
    .write = at91_ospi_memory_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static void at91_ospi_reset_hold(Object *obj, ResetType type)
{
    at91_ospi_soft_reset(AT91_OSPI(obj));
}

static int at91_ospi_post_load(void *opaque, int version_id)
{
    AT91OSPIState *s = opaque;

    qemu_set_irq(s->cs, s->cs_asserted ? 0 : 1);
    at91_ospi_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_at91_ospi = {
    .name = TYPE_AT91_OSPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_ospi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91OSPIState),
        VMSTATE_UINT32(isr, AT91OSPIState),
        VMSTATE_UINT32(imr, AT91OSPIState),
        VMSTATE_UINT32(scr, AT91OSPIState),
        VMSTATE_UINT32(iar, AT91OSPIState),
        VMSTATE_UINT32(wicr, AT91OSPIState),
        VMSTATE_UINT32(ifr, AT91OSPIState),
        VMSTATE_UINT32(ricr, AT91OSPIState),
        VMSTATE_UINT32(smr, AT91OSPIState),
        VMSTATE_UINT32(skr, AT91OSPIState),
        VMSTATE_UINT32(refresh, AT91OSPIState),
        VMSTATE_UINT32(wracnt, AT91OSPIState),
        VMSTATE_UINT32(tout, AT91OSPIState),
        VMSTATE_UINT32(wpmr, AT91OSPIState),
        VMSTATE_UINT32(wpsr, AT91OSPIState),
        VMSTATE_UINT16(rdr, AT91OSPIState),
        VMSTATE_BOOL(enabled, AT91OSPIState),
        VMSTATE_BOOL(cs_asserted, AT91OSPIState),
        VMSTATE_BOOL(transfer_active, AT91OSPIState),
        VMSTATE_BOOL(transfer_write, AT91OSPIState),
        VMSTATE_UINT32(next_addr, AT91OSPIState),
        VMSTATE_UINT32(transfer_count, AT91OSPIState),
        VMSTATE_END_OF_LIST(),
    },
};

static void at91_ospi_init(Object *obj)
{
    AT91OSPIState *s = AT91_OSPI(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->regs_mmio, obj, &at91_ospi_reg_ops, s,
                          TYPE_AT91_OSPI ".registers", OSPI_REG_SIZE);
    sysbus_init_mmio(sbd, &s->regs_mmio);
    memory_region_init_io(&s->memory_mmio, obj, &at91_ospi_memory_ops, s,
                          TYPE_AT91_OSPI ".memory", OSPI_MEMORY_SIZE);
    sysbus_init_mmio(sbd, &s->memory_mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(dev, &s->cs, "cs", 1);

    s->pclk = qdev_init_clock_in(dev, "pclk", NULL, NULL, 0);
    s->gclk = qdev_init_clock_in(dev, "gclk", NULL, NULL, 0);
    s->spi = ssi_create_bus(dev, "spi");
}

static void at91_ospi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "Microchip AT91 SAM9X7 OSPI controller";
    dc->vmsd = &vmstate_at91_ospi;
    rc->phases.hold = at91_ospi_reset_hold;
}

static const TypeInfo at91_ospi_info = {
    .name = TYPE_AT91_OSPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91OSPIState),
    .instance_init = at91_ospi_init,
    .class_init = at91_ospi_class_init,
};

static void at91_ospi_register_types(void)
{
    type_register_static(&at91_ospi_info);
}

type_init(at91_ospi_register_types)
