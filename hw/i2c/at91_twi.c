/*
 * Microchip AT91 two-wire interface controller
 *
 * This is the TWI implementation integrated in the SAM9X7 FLEXCOM block.
 * It models immediate I2C transfers, the PIO-facing holding registers,
 * alternative-command byte counts, FIFO-width accesses, interrupts, and
 * the three hardware write-protection domains.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/at91_twi.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"
#include "trace.h"

#define TWI_MMIO_SIZE           0x200

#define TWI_CR                  0x00
#define TWI_MMR                 0x04
#define TWI_SMR                 0x08
#define TWI_IADR                0x0c
#define TWI_CWGR                0x10
#define TWI_SR                  0x20
#define TWI_IER                 0x24
#define TWI_IDR                 0x28
#define TWI_IMR                 0x2c
#define TWI_RHR                 0x30
#define TWI_THR                 0x34
#define TWI_SMBTR               0x38
#define TWI_HSR                 0x3c
#define TWI_ACR                 0x40
#define TWI_FILTR               0x44
#define TWI_HSCWGR              0x48
#define TWI_FMR                 0x50
#define TWI_FLR                 0x54
#define TWI_FSR                 0x60
#define TWI_FIER                0x64
#define TWI_FIDR                0x68
#define TWI_FIMR                0x6c
#define TWI_WPMR                0xe4
#define TWI_WPSR                0xe8
#define TWI_VER                 0xfc

#define TWI_CR_START            BIT(0)
#define TWI_CR_STOP             BIT(1)
#define TWI_CR_MSEN             BIT(2)
#define TWI_CR_MSDIS            BIT(3)
#define TWI_CR_SVEN             BIT(4)
#define TWI_CR_SVDIS            BIT(5)
#define TWI_CR_QUICK            BIT(6)
#define TWI_CR_SWRST            BIT(7)
#define TWI_CR_HSEN             BIT(8)
#define TWI_CR_HSDIS            BIT(9)
#define TWI_CR_SMBEN            BIT(10)
#define TWI_CR_SMBDIS           BIT(11)
#define TWI_CR_PECEN            BIT(12)
#define TWI_CR_PECDIS           BIT(13)
#define TWI_CR_PECRQ            BIT(14)
#define TWI_CR_CLEAR            BIT(15)
#define TWI_CR_ACMEN            BIT(16)
#define TWI_CR_ACMDIS           BIT(17)
#define TWI_CR_SCLRBD           BIT(18)
#define TWI_CR_SCLRBE           BIT(19)
#define TWI_CR_THRCLR           BIT(24)
#define TWI_CR_RXFCLR           BIT(25)
#define TWI_CR_LOCKCLR          BIT(26)
#define TWI_CR_FIFOEN           BIT(28)
#define TWI_CR_FIFODIS          BIT(29)

#define TWI_MMR_IADRSZ_SHIFT    8
#define TWI_MMR_IADRSZ_MASK     (3U << TWI_MMR_IADRSZ_SHIFT)
#define TWI_MMR_MREAD           BIT(12)
#define TWI_MMR_DADR_SHIFT      16
#define TWI_MMR_DADR_MASK       (0x7fU << TWI_MMR_DADR_SHIFT)
#define TWI_MMR_NOAP            BIT(24)
#define TWI_MMR_MASK            0x017f7300

#define TWI_SMR_MASK            0x007f7ffd
#define TWI_IADR_MASK           0x00ffffff
#define TWI_CWGR_MASK           0x7f17ffff

#define TWI_SR_TXCOMP           BIT(0)
#define TWI_SR_RXRDY            BIT(1)
#define TWI_SR_TXRDY            BIT(2)
#define TWI_SR_SVREAD           BIT(3)
#define TWI_SR_GACC             BIT(5)
#define TWI_SR_OVRE             BIT(6)
#define TWI_SR_UNRE             BIT(7)
#define TWI_SR_NACK             BIT(8)
#define TWI_SR_ARBLST           BIT(9)
#define TWI_SR_EOSACC           BIT(11)
#define TWI_SR_MCACK            BIT(16)
#define TWI_SR_TOUT             BIT(18)
#define TWI_SR_PECERR           BIT(19)
#define TWI_SR_SMBDAM           BIT(20)
#define TWI_SR_SMBHHM           BIT(21)
#define TWI_SR_LOCK             BIT(23)
#define TWI_SR_SCL              BIT(24)
#define TWI_SR_SDA              BIT(25)
#define TWI_SR_REPSTART         BIT(26)
#define TWI_SR_RESET            0x03000009
#define TWI_SR_CLEAR_ON_READ    (TWI_SR_REPSTART | TWI_SR_SMBHHM | \
                                 TWI_SR_SMBDAM | TWI_SR_PECERR | \
                                 TWI_SR_TOUT | TWI_SR_MCACK | \
                                 TWI_SR_EOSACC | TWI_SR_ARBLST | \
                                 TWI_SR_NACK | TWI_SR_UNRE | \
                                 TWI_SR_OVRE | TWI_SR_GACC)

#define TWI_INT_MASK            (TWI_SR_SMBHHM | TWI_SR_SMBDAM | \
                                 TWI_SR_PECERR | TWI_SR_TOUT | \
                                 TWI_SR_MCACK | BIT(15) | BIT(14) | \
                                 BIT(13) | BIT(12) | TWI_SR_EOSACC | \
                                 BIT(10) | TWI_SR_ARBLST | TWI_SR_NACK | \
                                 TWI_SR_UNRE | TWI_SR_OVRE | TWI_SR_GACC | \
                                 BIT(4) | TWI_SR_TXRDY | TWI_SR_RXRDY | \
                                 TWI_SR_TXCOMP)

#define TWI_SMBTR_MASK          0xffffff0f
#define TWI_HSR_MASK            0x000000ff
#define TWI_ACR_DATAL_MASK      0x000000ff
#define TWI_ACR_DIR             BIT(8)
#define TWI_ACR_MASK            0x03ff03ff
#define TWI_FILTR_MASK          0x00000703
#define TWI_HSCWGR_MASK         0x0007ffff
#define TWI_FMR_MASK            0x3f3f0033
#define TWI_FIFO_INT_MASK       0x000000ff
#define TWI_FSR_TXFEF           BIT(0)
#define TWI_FSR_TXFFF           BIT(1)
#define TWI_FSR_TXFTHF          BIT(2)
#define TWI_FSR_RXFEF           BIT(3)
#define TWI_FSR_RXFFF           BIT(4)
#define TWI_FSR_RXFTHF          BIT(5)
#define TWI_FSR_TXFPTEF         BIT(6)
#define TWI_FSR_RXFPTEF         BIT(7)
#define TWI_FSR_CLEAR_ON_READ   (TWI_FSR_TXFEF | TWI_FSR_TXFFF | \
                                 TWI_FSR_TXFTHF | TWI_FSR_RXFEF | \
                                 TWI_FSR_RXFFF | TWI_FSR_RXFTHF)

#define TWI_WPMR_WPEN          BIT(0)
#define TWI_WPMR_WPITEN        BIT(1)
#define TWI_WPMR_WPCREN        BIT(2)
#define TWI_WPMR_MASK          0x00000007
#define TWI_WPMR_KEY_MASK      0xffffff00
#define TWI_WPMR_KEY           0x54574900

static void at91_twi_update_irq(AT91TWIState *s)
{
    bool level = s->flexcom_enabled &&
                 ((s->status & s->imr) ||
                  (s->fifo_enabled && (s->fsr & s->fimr)));

    qemu_set_irq(s->irq, level);
}

static void at91_twi_end_bus(AT91TWIState *s, bool nack)
{
    if (s->bus_active && i2c_bus_busy(s->bus)) {
        if (nack && s->read_transfer) {
            i2c_nack(s->bus);
        }
        i2c_end_transfer(s->bus);
    }

    s->bus_active = false;
    s->read_transfer = false;
    s->stop_pending = false;
    s->remaining = 0;
    s->status &= ~TWI_SR_RXRDY;
    s->status |= TWI_SR_TXCOMP | TWI_SR_TXRDY;
}

static void at91_twi_nack(AT91TWIState *s)
{
    if (!(s->mmr & TWI_MMR_NOAP)) {
        at91_twi_end_bus(s, false);
    }

    s->status &= ~TWI_SR_RXRDY;
    s->status |= TWI_SR_NACK | TWI_SR_TXCOMP | TWI_SR_TXRDY;
    if (s->alt_enabled || s->fifo_enabled) {
        s->status |= TWI_SR_LOCK;
    }
    at91_twi_update_irq(s);
}

static uint8_t at91_twi_address(AT91TWIState *s)
{
    return extract32(s->mmr, TWI_MMR_DADR_SHIFT, 7);
}

static unsigned int at91_twi_internal_address_size(AT91TWIState *s)
{
    return extract32(s->mmr, TWI_MMR_IADRSZ_SHIFT, 2);
}

static bool at91_twi_send_internal_address(AT91TWIState *s)
{
    unsigned int size = at91_twi_internal_address_size(s);
    uint8_t address = at91_twi_address(s);
    int i;

    if (!size) {
        return true;
    }

    if (i2c_start_send(s->bus, address)) {
        at91_twi_nack(s);
        return false;
    }
    s->bus_active = true;

    for (i = size - 1; i >= 0; i--) {
        uint8_t byte = extract32(s->iadr, i * 8, 8);

        trace_at91_twi_byte(DEVICE(s)->canonical_path, "internal", byte);
        if (i2c_send(s->bus, byte)) {
            at91_twi_nack(s);
            return false;
        }
    }

    return true;
}

static void at91_twi_preload_receive(AT91TWIState *s)
{
    if (!s->bus_active || !s->read_transfer) {
        return;
    }

    s->rhr = i2c_recv(s->bus);
    s->status |= TWI_SR_RXRDY;
    trace_at91_twi_byte(DEVICE(s)->canonical_path, "receive", s->rhr);
    at91_twi_update_irq(s);
}

static void at91_twi_start_read(AT91TWIState *s, bool stop)
{
    uint8_t address = at91_twi_address(s);

    if (!s->master_enabled) {
        return;
    }

    if (s->bus_active) {
        at91_twi_end_bus(s, false);
    }

    s->status &= ~(TWI_SR_TXCOMP | TWI_SR_RXRDY | TWI_SR_NACK |
                   TWI_SR_LOCK);
    s->read_transfer = true;
    s->stop_pending = stop;
    s->remaining = s->alt_enabled ? s->acr & TWI_ACR_DATAL_MASK : 0;

    trace_at91_twi_start(DEVICE(s)->canonical_path, address, true,
                         s->iadr, at91_twi_internal_address_size(s));

    if (!at91_twi_send_internal_address(s)) {
        return;
    }

    if (i2c_start_recv(s->bus, address)) {
        at91_twi_nack(s);
        return;
    }

    if (at91_twi_internal_address_size(s)) {
        s->status |= TWI_SR_REPSTART;
    }
    s->bus_active = true;

    if (s->alt_enabled && !s->remaining) {
        at91_twi_end_bus(s, true);
        at91_twi_update_irq(s);
        return;
    }

    at91_twi_preload_receive(s);
}

static void at91_twi_start_write(AT91TWIState *s)
{
    uint8_t address = at91_twi_address(s);

    if (!s->master_enabled) {
        return;
    }

    s->status &= ~(TWI_SR_TXCOMP | TWI_SR_NACK | TWI_SR_LOCK);
    s->status |= TWI_SR_TXRDY;
    s->read_transfer = false;
    s->stop_pending = false;
    s->remaining = s->alt_enabled ? s->acr & TWI_ACR_DATAL_MASK : 0;

    trace_at91_twi_start(DEVICE(s)->canonical_path, address, false,
                         s->iadr, at91_twi_internal_address_size(s));

    if (!at91_twi_send_internal_address(s)) {
        return;
    }

    if (!at91_twi_internal_address_size(s)) {
        if (i2c_start_send(s->bus, address)) {
            at91_twi_nack(s);
            return;
        }
        s->bus_active = true;
    }
}

static void at91_twi_quick(AT91TWIState *s)
{
    uint8_t address = at91_twi_address(s);
    bool read = s->alt_enabled ? !!(s->acr & TWI_ACR_DIR) :
                                !!(s->mmr & TWI_MMR_MREAD);

    if (!s->master_enabled) {
        return;
    }

    trace_at91_twi_start(DEVICE(s)->canonical_path, address, read, 0, 0);
    if (i2c_start_transfer(s->bus, address, read)) {
        at91_twi_nack(s);
        return;
    }

    s->bus_active = true;
    s->read_transfer = read;
    at91_twi_end_bus(s, false);
    at91_twi_update_irq(s);
}

static uint8_t at91_twi_read_byte(AT91TWIState *s)
{
    uint8_t value = s->rhr;

    if (!(s->status & TWI_SR_RXRDY)) {
        if (s->fifo_enabled) {
            s->fsr |= TWI_FSR_RXFPTEF;
        }
        at91_twi_update_irq(s);
        return value;
    }

    s->status &= ~TWI_SR_RXRDY;

    if (s->bus_active && s->read_transfer) {
        if (s->alt_enabled && s->remaining) {
            s->remaining--;
        }

        if (s->stop_pending || (s->alt_enabled && !s->remaining)) {
            at91_twi_end_bus(s, true);
        } else {
            at91_twi_preload_receive(s);
        }
    }

    at91_twi_update_irq(s);
    return value;
}

static void at91_twi_write_byte(AT91TWIState *s, uint8_t value)
{
    if (!s->master_enabled ||
        (s->alt_enabled && (s->acr & TWI_ACR_DIR))) {
        return;
    }

    if (!s->bus_active) {
        at91_twi_start_write(s);
        if (!s->bus_active) {
            return;
        }
    }

    s->status &= ~(TWI_SR_TXCOMP | TWI_SR_TXRDY);
    trace_at91_twi_byte(DEVICE(s)->canonical_path, "transmit", value);

    if (i2c_send(s->bus, value)) {
        at91_twi_nack(s);
        return;
    }

    s->status |= TWI_SR_TXRDY;
    if (s->alt_enabled && s->remaining) {
        s->remaining--;
        if (!s->remaining) {
            at91_twi_end_bus(s, false);
        }
    }
    at91_twi_update_irq(s);
}

static void at91_twi_reset_registers(AT91TWIState *s)
{
    if (s->bus && i2c_bus_busy(s->bus)) {
        i2c_end_transfer(s->bus);
    }

    s->mmr = 0;
    s->smr = 0;
    s->iadr = 0;
    s->cwgr = 0;
    s->status = TWI_SR_RESET;
    s->imr = 0;
    s->rhr = 0;
    s->smbtr = 0;
    s->hsr = 0;
    s->acr = 0;
    s->filtr = 0;
    s->hscwgr = 0;
    s->fmr = 0;
    s->fsr = 0;
    s->fimr = 0;
    s->wpmr = 0;
    s->wpsr = 0;
    s->remaining = 0;
    s->master_enabled = false;
    s->slave_enabled = false;
    s->high_speed_enabled = false;
    s->smbus_enabled = false;
    s->pec_enabled = false;
    s->alt_enabled = false;
    s->fifo_enabled = false;
    s->bus_active = false;
    s->read_transfer = false;
    s->stop_pending = false;
    at91_twi_update_irq(s);
}

static void at91_twi_protection_violation(AT91TWIState *s, hwaddr reg)
{
    /* WPVSRC reports the offset in the complete FLEXCOM register bank. */
    s->wpsr = ((0x600 + reg) << 8) | 1;
}

static bool at91_twi_write_protected(AT91TWIState *s, hwaddr reg)
{
    switch (reg) {
    case TWI_CR:
        return s->wpmr & TWI_WPMR_WPCREN;
    case TWI_IER:
    case TWI_IDR:
    case TWI_FIER:
    case TWI_FIDR:
        return s->wpmr & TWI_WPMR_WPITEN;
    case TWI_SMR:
    case TWI_CWGR:
    case TWI_SMBTR:
    case TWI_HSCWGR:
    case TWI_FMR:
        return s->wpmr & TWI_WPMR_WPEN;
    default:
        return false;
    }
}

static uint32_t at91_twi_access_mask(hwaddr offset, unsigned int size)
{
    unsigned int shift = (offset & 3) * 8;
    uint32_t mask = size == 4 ? UINT32_MAX : (1U << (size * 8)) - 1;

    return mask << shift;
}

static uint32_t at91_twi_expand_write(hwaddr offset, uint64_t value,
                                      unsigned int size)
{
    unsigned int shift = (offset & 3) * 8;

    return (value << shift) & at91_twi_access_mask(offset, size);
}

static uint32_t at91_twi_merge_write(uint32_t old, hwaddr offset,
                                     uint64_t value, unsigned int size)
{
    uint32_t mask = at91_twi_access_mask(offset, size);

    return (old & ~mask) | at91_twi_expand_write(offset, value, size);
}

static uint32_t at91_twi_raw_read(AT91TWIState *s, hwaddr reg)
{
    uint32_t value;

    switch (reg) {
    case TWI_MMR:
        return s->mmr;
    case TWI_SMR:
        return s->smr;
    case TWI_IADR:
        return s->iadr;
    case TWI_CWGR:
        return s->cwgr;
    case TWI_SR:
        value = s->status;
        s->status &= ~TWI_SR_CLEAR_ON_READ;
        at91_twi_update_irq(s);
        return value;
    case TWI_IMR:
        return s->imr;
    case TWI_SMBTR:
        return s->smbtr;
    case TWI_HSR:
        return s->hsr;
    case TWI_ACR:
        return s->acr;
    case TWI_FILTR:
        return s->filtr;
    case TWI_HSCWGR:
        return s->hscwgr;
    case TWI_FMR:
        return s->fifo_enabled ? s->fmr : 0;
    case TWI_FLR:
        return s->fifo_enabled && (s->status & TWI_SR_RXRDY) ? BIT(16) : 0;
    case TWI_FSR:
        if (!s->fifo_enabled) {
            return 0;
        }
        value = s->fsr;
        s->fsr &= ~TWI_FSR_CLEAR_ON_READ;
        at91_twi_update_irq(s);
        return value;
    case TWI_FIMR:
        return s->fifo_enabled ? s->fimr : 0;
    case TWI_WPMR:
        return s->wpmr;
    case TWI_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case TWI_CR:
    case TWI_IER:
    case TWI_IDR:
    case TWI_RHR:
    case TWI_THR:
    case TWI_FIER:
    case TWI_FIDR:
    case TWI_VER:
    default:
        return 0;
    }
}

static uint64_t at91_twi_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91TWIState *s = AT91_TWI(opaque);
    hwaddr reg = offset & ~3;
    uint32_t value = 0;
    unsigned int i;

    if (!s->flexcom_enabled) {
        return 0;
    }

    if (reg == TWI_RHR) {
        if (!s->fifo_enabled) {
            return at91_twi_read_byte(s);
        }
        for (i = 0; i < size; i++) {
            value |= (uint32_t)at91_twi_read_byte(s) << (i * 8);
        }
        return value;
    }

    value = at91_twi_raw_read(s, reg);
    return value >> ((offset & 3) * 8);
}

static void at91_twi_write_control(AT91TWIState *s, uint32_t value)
{
    if (value & TWI_CR_SWRST) {
        at91_twi_reset_registers(s);
        return;
    }

    if (value & TWI_CR_FIFOEN) {
        s->fifo_enabled = true;
    }
    if (value & TWI_CR_FIFODIS) {
        s->fifo_enabled = false;
        s->fsr = 0;
        s->fimr = 0;
    }
    if (value & TWI_CR_ACMEN) {
        s->alt_enabled = true;
    }
    if (value & TWI_CR_ACMDIS) {
        s->alt_enabled = false;
    }
    if (value & TWI_CR_HSEN) {
        s->high_speed_enabled = true;
    }
    if (value & TWI_CR_HSDIS) {
        s->high_speed_enabled = false;
    }
    if (value & TWI_CR_SMBEN) {
        s->smbus_enabled = true;
    }
    if (value & TWI_CR_SMBDIS) {
        s->smbus_enabled = false;
    }
    if (value & TWI_CR_PECEN) {
        s->pec_enabled = true;
    }
    if (value & TWI_CR_PECDIS) {
        s->pec_enabled = false;
    }
    if (value & TWI_CR_MSEN) {
        s->master_enabled = true;
        s->status |= TWI_SR_TXRDY;
    }
    if (value & TWI_CR_MSDIS) {
        at91_twi_end_bus(s, false);
        s->master_enabled = false;
        s->status &= ~TWI_SR_TXRDY;
    }
    if (value & TWI_CR_SVEN) {
        s->slave_enabled = true;
    }
    if (value & TWI_CR_SVDIS) {
        s->slave_enabled = false;
    }
    if (value & TWI_CR_LOCKCLR) {
        s->status &= ~TWI_SR_LOCK;
    }
    if (value & TWI_CR_THRCLR) {
        s->status |= TWI_SR_TXRDY | TWI_SR_TXCOMP;
    }
    if (value & TWI_CR_RXFCLR) {
        s->status &= ~TWI_SR_RXRDY;
    }
    if (value & TWI_CR_CLEAR) {
        at91_twi_end_bus(s, false);
    }

    if (value & TWI_CR_QUICK) {
        at91_twi_quick(s);
    } else if (value & TWI_CR_START) {
        bool read = s->alt_enabled ? !!(s->acr & TWI_ACR_DIR) :
                                    !!(s->mmr & TWI_MMR_MREAD);

        if (read) {
            at91_twi_start_read(s, value & TWI_CR_STOP);
        }
    } else if (value & TWI_CR_STOP) {
        if (s->bus_active && s->read_transfer) {
            s->stop_pending = true;
        } else {
            at91_twi_end_bus(s, false);
        }
    }

    if (value & TWI_CR_PECRQ) {
        /* PEC generation is represented by the enable state only. */
    }
    if (value & (TWI_CR_SCLRBE | TWI_CR_SCLRBD)) {
        /* Pad rise-boost timing has no externally observable QEMU state. */
    }
    at91_twi_update_irq(s);
}

static void at91_twi_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91TWIState *s = AT91_TWI(opaque);
    hwaddr reg = offset & ~3;
    uint32_t bits = at91_twi_expand_write(offset, value, size);
    uint32_t merged;
    unsigned int i;

    if (!s->flexcom_enabled) {
        return;
    }

    if (at91_twi_write_protected(s, reg)) {
        at91_twi_protection_violation(s, reg);
        return;
    }

    switch (reg) {
    case TWI_CR:
        at91_twi_write_control(s, bits);
        break;
    case TWI_MMR:
        merged = at91_twi_merge_write(s->mmr, offset, value, size);
        s->mmr = merged & TWI_MMR_MASK;
        break;
    case TWI_SMR:
        merged = at91_twi_merge_write(s->smr, offset, value, size);
        s->smr = merged & TWI_SMR_MASK;
        break;
    case TWI_IADR:
        merged = at91_twi_merge_write(s->iadr, offset, value, size);
        s->iadr = merged & TWI_IADR_MASK;
        break;
    case TWI_CWGR:
        merged = at91_twi_merge_write(s->cwgr, offset, value, size);
        s->cwgr = merged & TWI_CWGR_MASK;
        break;
    case TWI_IER:
        s->imr |= bits & TWI_INT_MASK;
        at91_twi_update_irq(s);
        break;
    case TWI_IDR:
        s->imr &= ~(bits & TWI_INT_MASK);
        at91_twi_update_irq(s);
        break;
    case TWI_THR:
        if (!s->fifo_enabled) {
            at91_twi_write_byte(s, value);
            break;
        }
        for (i = 0; i < size; i++) {
            at91_twi_write_byte(s, value >> (i * 8));
        }
        break;
    case TWI_SMBTR:
        merged = at91_twi_merge_write(s->smbtr, offset, value, size);
        s->smbtr = merged & TWI_SMBTR_MASK;
        break;
    case TWI_HSR:
        merged = at91_twi_merge_write(s->hsr, offset, value, size);
        s->hsr = merged & TWI_HSR_MASK;
        break;
    case TWI_ACR:
        merged = at91_twi_merge_write(s->acr, offset, value, size);
        s->acr = merged & TWI_ACR_MASK;
        break;
    case TWI_FILTR:
        merged = at91_twi_merge_write(s->filtr, offset, value, size);
        s->filtr = merged & TWI_FILTR_MASK;
        break;
    case TWI_HSCWGR:
        merged = at91_twi_merge_write(s->hscwgr, offset, value, size);
        s->hscwgr = merged & TWI_HSCWGR_MASK;
        break;
    case TWI_FMR:
        merged = at91_twi_merge_write(s->fmr, offset, value, size);
        s->fmr = merged & TWI_FMR_MASK;
        break;
    case TWI_FIER:
        s->fimr |= bits & TWI_FIFO_INT_MASK;
        at91_twi_update_irq(s);
        break;
    case TWI_FIDR:
        s->fimr &= ~(bits & TWI_FIFO_INT_MASK);
        at91_twi_update_irq(s);
        break;
    case TWI_WPMR:
        merged = at91_twi_merge_write(s->wpmr, offset, value, size);
        if ((merged & TWI_WPMR_KEY_MASK) == TWI_WPMR_KEY) {
            s->wpmr = merged & TWI_WPMR_MASK;
        }
        break;
    case TWI_SR:
    case TWI_IMR:
    case TWI_RHR:
    case TWI_FLR:
    case TWI_FSR:
    case TWI_FIMR:
    case TWI_WPSR:
    case TWI_VER:
    default:
        break;
    }
}

static const MemoryRegionOps at91_twi_ops = {
    .read = at91_twi_read,
    .write = at91_twi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_twi_set_flexcom_enabled(void *opaque, int n, int level)
{
    AT91TWIState *s = AT91_TWI(opaque);

    if (s->flexcom_enabled && !level) {
        at91_twi_end_bus(s, false);
    }
    s->flexcom_enabled = level;
    at91_twi_update_irq(s);
}

static void at91_twi_reset(DeviceState *dev)
{
    at91_twi_reset_registers(AT91_TWI(dev));
}

static int at91_twi_post_load(void *opaque, int version_id)
{
    at91_twi_update_irq(AT91_TWI(opaque));
    return 0;
}

static const VMStateDescription at91_twi_vmstate = {
    .name = TYPE_AT91_TWI,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_twi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mmr, AT91TWIState),
        VMSTATE_UINT32(smr, AT91TWIState),
        VMSTATE_UINT32(iadr, AT91TWIState),
        VMSTATE_UINT32(cwgr, AT91TWIState),
        VMSTATE_UINT32(status, AT91TWIState),
        VMSTATE_UINT32(imr, AT91TWIState),
        VMSTATE_UINT32(rhr, AT91TWIState),
        VMSTATE_UINT32(smbtr, AT91TWIState),
        VMSTATE_UINT32(hsr, AT91TWIState),
        VMSTATE_UINT32(acr, AT91TWIState),
        VMSTATE_UINT32(filtr, AT91TWIState),
        VMSTATE_UINT32(hscwgr, AT91TWIState),
        VMSTATE_UINT32(fmr, AT91TWIState),
        VMSTATE_UINT32(fsr, AT91TWIState),
        VMSTATE_UINT32(fimr, AT91TWIState),
        VMSTATE_UINT32(wpmr, AT91TWIState),
        VMSTATE_UINT32(wpsr, AT91TWIState),
        VMSTATE_UINT16(remaining, AT91TWIState),
        VMSTATE_BOOL(flexcom_enabled, AT91TWIState),
        VMSTATE_BOOL(master_enabled, AT91TWIState),
        VMSTATE_BOOL(slave_enabled, AT91TWIState),
        VMSTATE_BOOL(high_speed_enabled, AT91TWIState),
        VMSTATE_BOOL(smbus_enabled, AT91TWIState),
        VMSTATE_BOOL(pec_enabled, AT91TWIState),
        VMSTATE_BOOL(alt_enabled, AT91TWIState),
        VMSTATE_BOOL(fifo_enabled, AT91TWIState),
        VMSTATE_BOOL(bus_active, AT91TWIState),
        VMSTATE_BOOL(read_transfer, AT91TWIState),
        VMSTATE_BOOL(stop_pending, AT91TWIState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_twi_realize(DeviceState *dev, Error **errp)
{
    AT91TWIState *s = AT91_TWI(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_TWI ": pclk must be connected");
        return;
    }

    s->bus = i2c_init_bus(dev, s->bus_name ? s->bus_name : "i2c");
}

static const Property at91_twi_properties[] = {
    DEFINE_PROP_STRING("bus-name", AT91TWIState, bus_name),
};

static void at91_twi_init(Object *obj)
{
    AT91TWIState *s = AT91_TWI(obj);
    DeviceState *dev = DEVICE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_twi_ops, s,
                          TYPE_AT91_TWI, TWI_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in_named(dev, at91_twi_set_flexcom_enabled,
                            "flexcom-enabled", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", NULL, NULL, 0);
}

I2CBus *at91_twi_get_bus(AT91TWIState *s)
{
    return s->bus;
}

static void at91_twi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 FLEXCOM two-wire interface";
    dc->realize = at91_twi_realize;
    dc->vmsd = &at91_twi_vmstate;
    device_class_set_props(dc, at91_twi_properties);
    device_class_set_legacy_reset(dc, at91_twi_reset);
}

static const TypeInfo at91_twi_info = {
    .name = TYPE_AT91_TWI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91TWIState),
    .instance_init = at91_twi_init,
    .class_init = at91_twi_class_init,
};

static void at91_twi_register_types(void)
{
    type_register_static(&at91_twi_info);
}

type_init(at91_twi_register_types)
