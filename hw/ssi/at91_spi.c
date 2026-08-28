/*
 * Microchip AT91 FLEXCOM SPI
 *
 * This models the SPI personality integrated in SAM9X7 FLEXCOM0 through
 * FLEXCOM5.  The controller provides host transfers on a QEMU SSI bus, the
 * SAM9X7 16-entry FIFOs, interrupt and DMA request generation, chip-select
 * timing, local loopback, comparison and register write protection.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/ssi/at91_spi.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

enum {
    SPI_CR      = 0x00,
    SPI_MR      = 0x04,
    SPI_RDR     = 0x08,
    SPI_TDR     = 0x0c,
    SPI_SR      = 0x10,
    SPI_IER     = 0x14,
    SPI_IDR     = 0x18,
    SPI_IMR     = 0x1c,
    SPI_CSR0    = 0x30,
    SPI_CSR1    = 0x34,
    SPI_CSR2    = 0x38,
    SPI_CSR3    = 0x3c,
    SPI_FMR     = 0x40,
    SPI_FLR     = 0x44,
    SPI_CMPR    = 0x48,
    SPI_CRCR    = 0x4c,
    SPI_TPMR    = 0x50,
    SPI_TPHR    = 0x54,
    SPI_WPMR    = 0xe4,
    SPI_WPSR    = 0xe8,
};

#define SPI_MMIO_SIZE           0x200

#define SPI_CR_SPIEN            BIT(0)
#define SPI_CR_SPIDIS           BIT(1)
#define SPI_CR_SWRST            BIT(7)
#define SPI_CR_REQCLR           BIT(12)
#define SPI_CR_TXFCLR           BIT(16)
#define SPI_CR_RXFCLR           BIT(17)
#define SPI_CR_LASTXFER         BIT(24)
#define SPI_CR_FIFOEN           BIT(30)
#define SPI_CR_FIFODIS          BIT(31)

#define SPI_MR_MSTR             BIT(0)
#define SPI_MR_PS               BIT(1)
#define SPI_MR_PCSDEC           BIT(2)
#define SPI_MR_BRSRCCLK         BIT(3)
#define SPI_MR_WDRBT            BIT(5)
#define SPI_MR_LLB              BIT(7)
#define SPI_MR_CMPMODE          BIT(12)
#define SPI_MR_CSIE             BIT(14)
#define SPI_MR_PCS_SHIFT        16
#define SPI_MR_DLYBCS_SHIFT     24
#define SPI_MR_MASK             0xff0ff1ff

#define SPI_TDR_DATA_MASK       0x0000ffff
#define SPI_TDR_PCS_SHIFT       16
#define SPI_TDR_LASTXFER        BIT(24)

#define SPI_INT_RDRF            BIT(0)
#define SPI_INT_TDRE            BIT(1)
#define SPI_INT_MODF            BIT(2)
#define SPI_INT_OVRES           BIT(3)
#define SPI_INT_NSSR            BIT(8)
#define SPI_INT_TXEMPTY         BIT(9)
#define SPI_INT_UNDES           BIT(10)
#define SPI_INT_CMP             BIT(11)
#define SPI_INT_SFERR           BIT(12)
#define SPI_INT_CRCERR          BIT(13)
#define SPI_STATUS_SPIENS       BIT(16)
#define SPI_INT_TXFEF           BIT(24)
#define SPI_INT_TXFFF           BIT(25)
#define SPI_INT_TXFTHF          BIT(26)
#define SPI_INT_RXFEF           BIT(27)
#define SPI_INT_RXFFF           BIT(28)
#define SPI_INT_RXFTHF          BIT(29)
#define SPI_INT_TXFPTEF         BIT(30)
#define SPI_INT_RXFPTEF         BIT(31)
#define SPI_INT_MASK            0xff003f0f
#define SPI_STATUS_MASK         0xff013f0f
#define SPI_STATUS_DERIVED      (SPI_INT_RDRF | SPI_INT_TDRE | \
                                 SPI_INT_TXEMPTY | SPI_STATUS_SPIENS)
#define SPI_STATUS_CLEAR_READ   (SPI_INT_MODF | SPI_INT_OVRES | \
                                 SPI_INT_NSSR | SPI_INT_UNDES | \
                                 SPI_INT_CMP | SPI_INT_SFERR | \
                                 SPI_INT_CRCERR | SPI_INT_TXFEF | \
                                 SPI_INT_TXFFF | SPI_INT_TXFTHF | \
                                 SPI_INT_RXFEF | SPI_INT_RXFFF | \
                                 SPI_INT_RXFTHF)
#define SPI_STATUS_FIFO_MASK    0xff000000

#define SPI_CSR_CPOL            BIT(0)
#define SPI_CSR_NCPHA           BIT(1)
#define SPI_CSR_CSNAAT          BIT(2)
#define SPI_CSR_CSAAT           BIT(3)
#define SPI_CSR_BITS_SHIFT      4
#define SPI_CSR_SCBR_SHIFT      8
#define SPI_CSR_DLYBS_SHIFT     16
#define SPI_CSR_DLYBCT_SHIFT    24

#define SPI_FMR_TXRDYM_MASK     0x3
#define SPI_FMR_RXRDYM_SHIFT    4
#define SPI_FMR_TXFTHRES_SHIFT  16
#define SPI_FMR_RXFTHRES_SHIFT  24
#define SPI_FMR_MASK            0x3f3f0033
#define SPI_CRCR_MASK           0x0ff100ff
#define SPI_TPMR_MASK           0x0000000f
#define SPI_TPHR_MASK           0x0000007f

/*
 * SPI_VERSION at +0xfc.  The SAM9X7 data sheet marks the word reserved,
 * but the Linux spi-atmel driver reads it to choose the FIFO/XDMAC path
 * over the legacy PDC window.  Measured read-only on a SAM9X75 Curiosity
 * with FLEXCOM4 runtime-active: 0x00000410, stable, no abort; a 16-bit read
 * returned 0x0410 and an 8-bit read 0x10.
 */
#define SPI_VERSION             0xfc
#define SPI_VERSION_SAM9X7      0x00000410

#define SPI_WPMR_WPEN           BIT(0)
#define SPI_WPMR_WPITEN         BIT(1)
#define SPI_WPMR_WPCREN         BIT(2)
#define SPI_WPMR_MASK           0x7
#define SPI_WPMR_KEY_MASK       0xffffff00
#define SPI_WPMR_KEY            0x53504900

static void at91_spi_update(AT91SPIState *s);
static void at91_spi_start_next(AT91SPIState *s);

static uint32_t at91_spi_expand_write(hwaddr offset, uint64_t value,
                                      unsigned int size)
{
    return (uint32_t)value << ((offset & 3) * 8);
}

static uint32_t at91_spi_merge_write(uint32_t old, hwaddr offset,
                                     uint64_t value, unsigned int size)
{
    unsigned int shift = (offset & 3) * 8;
    uint64_t mask = MAKE_64BIT_MASK(shift, size * 8);

    return (old & ~mask) | (((uint64_t)value << shift) & mask);
}

static unsigned int at91_spi_ready_count(unsigned int mode)
{
    switch (mode & 3) {
    case 1:
        return 2;
    case 2:
        return 4;
    default:
        return 1;
    }
}

static uint8_t at91_spi_entry_pcs(uint32_t entry)
{
    return extract32(entry, SPI_TDR_PCS_SHIFT, 4);
}

static unsigned int at91_spi_cs_index(AT91SPIState *s, uint8_t pcs)
{
    uint32_t asserted;

    if (s->mode & SPI_MR_PCSDEC) {
        return (pcs & 0xf) >> 2;
    }

    asserted = ~pcs & 0xf;
    return asserted ? ctz32(asserted) : 0;
}

static uint32_t at91_spi_entry_csr(AT91SPIState *s, uint32_t entry)
{
    return s->chip_select[at91_spi_cs_index(s,
                                            at91_spi_entry_pcs(entry))];
}

static unsigned int at91_spi_entry_bits(AT91SPIState *s, uint32_t entry)
{
    unsigned int bits = extract32(at91_spi_entry_csr(s, entry),
                                  SPI_CSR_BITS_SHIFT, 4);

    return bits <= 8 ? bits + 8 : 8;
}

static uint32_t at91_spi_entry_data_mask(AT91SPIState *s, uint32_t entry)
{
    return MAKE_64BIT_MASK(0, at91_spi_entry_bits(s, entry));
}

static Clock *at91_spi_baud_clock(AT91SPIState *s)
{
    return s->mode & SPI_MR_BRSRCCLK ? s->gclk : s->pclk;
}

static bool at91_spi_entry_clocked(AT91SPIState *s, uint32_t entry)
{
    uint32_t csr = at91_spi_entry_csr(s, entry);

    return s->flexcom_enabled && s->spi_enabled &&
           (s->mode & SPI_MR_MSTR) && clock_get_hz(s->pclk) &&
           clock_get_hz(at91_spi_baud_clock(s)) &&
           extract32(csr, SPI_CSR_SCBR_SHIFT, 8);
}

static uint64_t at91_spi_cycles_to_ns(AT91SPIState *s, uint64_t cycles)
{
    return MAX(clock_ticks_to_ns(at91_spi_baud_clock(s), cycles), 1);
}

static void at91_spi_set_request(qemu_irq irq, bool *old_level,
                                 bool new_level)
{
    if (*old_level != new_level) {
        *old_level = new_level;
        qemu_set_irq(irq, new_level);
    }
}

static void at91_spi_refresh_status(AT91SPIState *s)
{
    unsigned int tx_limit = s->fifo_enabled ? AT91_SPI_FIFO_SIZE : 1;
    unsigned int tx_ready = s->fifo_enabled ?
        at91_spi_ready_count(s->fifo_mode & SPI_FMR_TXRDYM_MASK) : 1;
    unsigned int rx_ready = s->fifo_enabled ?
        at91_spi_ready_count(s->fifo_mode >> SPI_FMR_RXRDYM_SHIFT) : 1;

    s->status &= ~SPI_STATUS_DERIVED;
    if (s->spi_enabled) {
        s->status |= SPI_STATUS_SPIENS;
        if (tx_limit - s->tx_count >= tx_ready) {
            s->status |= SPI_INT_TDRE;
        }
        if (s->rx_count >= rx_ready) {
            s->status |= SPI_INT_RDRF;
        }
        if (!s->tx_count && !s->tx_shift_valid &&
            s->phase == AT91_SPI_PHASE_IDLE) {
            s->status |= SPI_INT_TXEMPTY;
        }
    }
}

static void at91_spi_update(AT91SPIState *s)
{
    bool active;

    at91_spi_refresh_status(s);
    active = s->flexcom_enabled && s->spi_enabled &&
             clock_get_hz(s->pclk);
    at91_spi_set_request(s->tx_request, &s->tx_request_level,
                         active && (s->status & SPI_INT_TDRE));
    at91_spi_set_request(s->rx_request, &s->rx_request_level,
                         active && (s->status & SPI_INT_RDRF));
    qemu_set_irq(s->irq, s->flexcom_enabled &&
                 (s->status & s->interrupt_mask));
}

static void at91_spi_update_tx_events(AT91SPIState *s,
                                      unsigned int old_count)
{
    unsigned int threshold = extract32(s->fifo_mode,
                                       SPI_FMR_TXFTHRES_SHIFT, 6);

    if (!s->fifo_enabled) {
        return;
    }
    if (s->tx_count == AT91_SPI_FIFO_SIZE &&
        old_count != AT91_SPI_FIFO_SIZE) {
        s->status |= SPI_INT_TXFFF;
    }
    if (!s->tx_count && old_count) {
        s->status |= SPI_INT_TXFEF;
    }
    if (old_count > threshold && s->tx_count <= threshold) {
        s->status |= SPI_INT_TXFTHF;
    }
}

static void at91_spi_update_rx_events(AT91SPIState *s,
                                      unsigned int old_count)
{
    unsigned int threshold = extract32(s->fifo_mode,
                                       SPI_FMR_RXFTHRES_SHIFT, 6);

    if (!s->fifo_enabled) {
        return;
    }
    if (s->rx_count == AT91_SPI_FIFO_SIZE &&
        old_count != AT91_SPI_FIFO_SIZE) {
        s->status |= SPI_INT_RXFFF;
    }
    if (!s->rx_count && old_count) {
        s->status |= SPI_INT_RXFEF;
    }
    if (old_count < threshold && s->rx_count >= threshold) {
        s->status |= SPI_INT_RXFTHF;
    }
}

static uint8_t at91_spi_direct_pcs(AT91SPIState *s, uint8_t pcs)
{
    if (s->mode & SPI_MR_PCSDEC) {
        return pcs & 0xf;
    }
    if ((pcs & 0xf) == 0xf) {
        return 0xf;
    }
    return 0xf & ~BIT(at91_spi_cs_index(s, pcs));
}

static void at91_spi_drive_cs(AT91SPIState *s)
{
    uint8_t levels = 0xf;
    unsigned int i;

    if (s->flexcom_enabled && s->spi_enabled && s->cs_active) {
        levels = at91_spi_direct_pcs(s, s->current_pcs);
    }
    if (s->mode & SPI_MR_CSIE) {
        levels ^= 0xf;
    }
    for (i = 0; i < s->num_cs; i++) {
        qemu_set_irq(s->cs[i], !!(levels & BIT(i)));
    }
}

static void at91_spi_deassert_cs(AT91SPIState *s, bool track_gap)
{
    uint64_t cycles;

    if (!s->cs_active) {
        return;
    }
    s->cs_active = false;
    at91_spi_drive_cs(s);
    if (!track_gap || !clock_get_hz(at91_spi_baud_clock(s))) {
        s->cs_gap_until_ns = 0;
        return;
    }

    cycles = MAX(extract32(s->mode, SPI_MR_DLYBCS_SHIFT, 8), 6);
    s->cs_gap_until_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                         at91_spi_cycles_to_ns(s, cycles);
}

static void at91_spi_assert_cs(AT91SPIState *s)
{
    s->current_pcs = at91_spi_entry_pcs(s->tx_shift);
    s->cs_active = at91_spi_direct_pcs(s, s->current_pcs) != 0xf ||
                   (s->mode & SPI_MR_PCSDEC);
    at91_spi_drive_cs(s);
}

static void at91_spi_schedule_cycles(AT91SPIState *s, uint64_t cycles)
{
    timer_del(s->transfer_timer);
    if (s->tx_shift_valid && at91_spi_entry_clocked(s, s->tx_shift)) {
        timer_mod_ns(s->transfer_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     at91_spi_cycles_to_ns(s, cycles));
    }
}

static void at91_spi_begin_transfer(AT91SPIState *s)
{
    uint32_t csr = at91_spi_entry_csr(s, s->tx_shift);
    uint64_t scbr = extract32(csr, SPI_CSR_SCBR_SHIFT, 8);

    s->phase = AT91_SPI_PHASE_TRANSFER;
    at91_spi_schedule_cycles(s, scbr * at91_spi_entry_bits(s, s->tx_shift));
    at91_spi_update(s);
}

static void at91_spi_begin_dlybs(AT91SPIState *s)
{
    uint32_t csr = at91_spi_entry_csr(s, s->tx_shift);
    uint64_t cycles = extract32(csr, SPI_CSR_DLYBS_SHIFT, 8);

    if (!cycles) {
        cycles = DIV_ROUND_UP(extract32(csr, SPI_CSR_SCBR_SHIFT, 8), 2);
    }
    s->phase = AT91_SPI_PHASE_DLYBS;
    at91_spi_schedule_cycles(s, MAX(cycles, 1));
    at91_spi_update(s);
}

static bool at91_spi_can_start(AT91SPIState *s, uint32_t entry)
{
    bool rx_blocked;

    /*
     * WDRBT waits for unread receive data before starting a transfer.  With
     * the FIFOs enabled the receive side is the FIFO: the Linux driver fills
     * the transmit FIFO, waits for RXFTHF at the data count and only then
     * reads, so a transfer may start while the receive FIFO has room and is
     * blocked only when it is full.
     */
    if (s->fifo_enabled) {
        rx_blocked = s->rx_count >= AT91_SPI_FIFO_SIZE;
    } else {
        rx_blocked = s->rx_count != 0;
    }
    return at91_spi_entry_clocked(s, entry) &&
           (!(s->mode & SPI_MR_WDRBT) || !rx_blocked);
}

static void at91_spi_start_next(AT91SPIState *s)
{
    uint32_t entry;
    uint8_t pcs;
    unsigned int old_count;
    int64_t now;

    if (s->phase != AT91_SPI_PHASE_IDLE || s->tx_shift_valid ||
        !s->tx_count) {
        at91_spi_update(s);
        return;
    }

    entry = s->tx_fifo[s->tx_head];
    if (!at91_spi_can_start(s, entry)) {
        at91_spi_update(s);
        return;
    }

    old_count = s->tx_count;
    s->tx_shift = entry;
    s->tx_head = (s->tx_head + 1) % AT91_SPI_FIFO_SIZE;
    s->tx_count--;
    s->tx_shift_valid = true;
    at91_spi_update_tx_events(s, old_count);

    pcs = at91_spi_entry_pcs(entry);
    if (s->cs_active && s->current_pcs != pcs) {
        at91_spi_deassert_cs(s, true);
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    if (!s->cs_active && now < s->cs_gap_until_ns) {
        s->phase = AT91_SPI_PHASE_DLYBCS;
        timer_mod_ns(s->transfer_timer, s->cs_gap_until_ns);
    } else if (!s->cs_active) {
        at91_spi_assert_cs(s);
        at91_spi_begin_dlybs(s);
    } else {
        at91_spi_begin_transfer(s);
    }
    at91_spi_update(s);
}

static void at91_spi_push_rx(AT91SPIState *s, uint32_t value, uint8_t pcs)
{
    unsigned int limit = s->fifo_enabled ? AT91_SPI_FIFO_SIZE : 1;
    unsigned int old_count = s->rx_count;
    unsigned int index;
    uint16_t val1 = extract32(s->comparison, 0, 16);
    uint16_t val2 = extract32(s->comparison, 16, 16);
    bool match = value >= val1 && value <= val2;

    if (match) {
        s->status |= SPI_INT_CMP;
        if (s->mode & SPI_MR_CMPMODE) {
            s->comparison_started = true;
        }
    }
    if ((s->mode & SPI_MR_CMPMODE) && !s->comparison_started) {
        at91_spi_update(s);
        return;
    }

    value = (value & SPI_TDR_DATA_MASK) | ((uint32_t)pcs << 16);
    if (s->rx_count == limit) {
        s->status |= SPI_INT_OVRES;
        if (!s->fifo_enabled) {
            s->rx_fifo[s->rx_head] = value;
        }
    } else {
        index = (s->rx_head + s->rx_count) % AT91_SPI_FIFO_SIZE;
        s->rx_fifo[index] = value;
        s->rx_count++;
        at91_spi_update_rx_events(s, old_count);
    }
    at91_spi_update(s);
}

static uint32_t at91_spi_pop_rx(AT91SPIState *s)
{
    unsigned int old_count = s->rx_count;
    uint32_t value = 0;

    if (!s->rx_count) {
        if (s->fifo_enabled) {
            s->status |= SPI_INT_RXFPTEF;
        }
    } else {
        value = s->rx_fifo[s->rx_head];
        s->rx_head = (s->rx_head + 1) % AT91_SPI_FIFO_SIZE;
        s->rx_count--;
        at91_spi_update_rx_events(s, old_count);
    }
    at91_spi_start_next(s);
    at91_spi_update(s);
    return value;
}

static void at91_spi_transfer_complete(AT91SPIState *s)
{
    uint32_t csr = at91_spi_entry_csr(s, s->tx_shift);
    uint32_t tx = s->tx_shift & at91_spi_entry_data_mask(s, s->tx_shift);
    uint32_t rx;
    uint64_t delay;

    rx = s->mode & SPI_MR_LLB ? tx : ssi_transfer(s->bus, tx);
    rx &= at91_spi_entry_data_mask(s, s->tx_shift);
    at91_spi_push_rx(s, rx, at91_spi_entry_pcs(s->tx_shift));

    delay = extract32(csr, SPI_CSR_DLYBCT_SHIFT, 8) * 32ULL;
    s->phase = AT91_SPI_PHASE_DLYBCT;
    if (delay) {
        at91_spi_schedule_cycles(s, delay);
    }
    at91_spi_update(s);
}

static void at91_spi_finish_dlybct(AT91SPIState *s)
{
    uint32_t csr = at91_spi_entry_csr(s, s->tx_shift);
    uint8_t pcs = at91_spi_entry_pcs(s->tx_shift);
    bool next_ready = s->tx_count &&
        at91_spi_can_start(s, s->tx_fifo[s->tx_head]);
    bool same_next = next_ready &&
        at91_spi_entry_pcs(s->tx_fifo[s->tx_head]) == pcs;
    bool deassert;

    deassert = s->tx_shift & SPI_TDR_LASTXFER;
    if (!deassert && !(csr & SPI_CSR_CSAAT)) {
        deassert = !same_next || (csr & SPI_CSR_CSNAAT);
    } else if (!deassert && same_next) {
        deassert = false;
    } else if (!deassert && next_ready) {
        deassert = true;
    }

    s->tx_shift_valid = false;
    s->phase = AT91_SPI_PHASE_IDLE;
    if (deassert || s->disable_pending) {
        at91_spi_deassert_cs(s, !s->disable_pending);
    }
    if (s->disable_pending) {
        s->disable_pending = false;
        s->spi_enabled = false;
    }
    at91_spi_update(s);
    at91_spi_start_next(s);
}

static void at91_spi_timer(void *opaque)
{
    AT91SPIState *s = opaque;

    if (!s->tx_shift_valid) {
        s->phase = AT91_SPI_PHASE_IDLE;
        at91_spi_update(s);
        return;
    }

    switch ((AT91SPIPhase)s->phase) {
    case AT91_SPI_PHASE_DLYBCS:
        at91_spi_assert_cs(s);
        at91_spi_begin_dlybs(s);
        break;
    case AT91_SPI_PHASE_DLYBS:
        at91_spi_begin_transfer(s);
        break;
    case AT91_SPI_PHASE_TRANSFER:
        at91_spi_transfer_complete(s);
        if (!extract32(at91_spi_entry_csr(s, s->tx_shift),
                       SPI_CSR_DLYBCT_SHIFT, 8)) {
            at91_spi_finish_dlybct(s);
        }
        break;
    case AT91_SPI_PHASE_DLYBCT:
        at91_spi_finish_dlybct(s);
        break;
    case AT91_SPI_PHASE_IDLE:
        break;
    default:
        g_assert_not_reached();
    }
}

static void at91_spi_write_tdr_entry(AT91SPIState *s, uint32_t value,
                                     uint8_t pcs, bool last)
{
    unsigned int limit = s->fifo_enabled ? AT91_SPI_FIFO_SIZE : 1;
    unsigned int old_count = s->tx_count;
    unsigned int index;
    uint32_t entry = (value & SPI_TDR_DATA_MASK) |
                     ((uint32_t)(pcs & 0xf) << SPI_TDR_PCS_SHIFT);

    if (!s->spi_enabled) {
        return;
    }
    if (last) {
        entry |= SPI_TDR_LASTXFER;
    }
    entry &= at91_spi_entry_data_mask(s, entry) |
             (0xfU << SPI_TDR_PCS_SHIFT) | SPI_TDR_LASTXFER;
    if (s->tx_count == limit) {
        if (s->fifo_enabled) {
            s->status |= SPI_INT_TXFPTEF;
        }
        at91_spi_update(s);
        return;
    }

    index = (s->tx_head + s->tx_count) % AT91_SPI_FIFO_SIZE;
    s->tx_fifo[index] = entry;
    s->tx_count++;
    at91_spi_update_tx_events(s, old_count);
    at91_spi_start_next(s);
}

static void at91_spi_write_tdr(AT91SPIState *s, uint64_t value,
                               unsigned int size)
{
    uint8_t pcs = s->mode & SPI_MR_PS ?
        extract32(value, SPI_TDR_PCS_SHIFT, 4) :
        extract32(s->mode, SPI_MR_PCS_SHIFT, 4);
    bool last = (s->mode & SPI_MR_PS) && (value & SPI_TDR_LASTXFER);
    unsigned int count = s->fifo_enabled && !(s->mode & SPI_MR_PS) &&
                         size == 4 ? 2 : 1;
    unsigned int free = (s->fifo_enabled ? AT91_SPI_FIFO_SIZE : 1) -
                        s->tx_count;
    unsigned int i;

    if (count > free) {
        if (s->fifo_enabled) {
            s->status |= SPI_INT_TXFPTEF;
        }
        at91_spi_update(s);
        return;
    }
    for (i = 0; i < count; i++) {
        at91_spi_write_tdr_entry(s, value >> (i * 16), pcs,
                                 last && i == count - 1);
    }
}

static uint64_t at91_spi_read_rdr(AT91SPIState *s, unsigned int size)
{
    bool multiple = s->fifo_enabled && !(s->mode & SPI_MR_MSTR) &&
                    !(s->mode & SPI_MR_PS);
    unsigned int bits = extract32(s->chip_select[0],
                                  SPI_CSR_BITS_SHIFT, 4);
    unsigned int count = 1;
    unsigned int stride = 16;
    uint64_t value = 0;
    unsigned int i;

    if (!multiple) {
        return at91_spi_pop_rx(s);
    }

    if (!bits) {
        count = size;
        stride = 8;
    } else if (size == 4) {
        count = 2;
    }
    for (i = 0; i < count; i++) {
        value |= (uint64_t)(at91_spi_pop_rx(s) &
                            (stride == 8 ? 0xff : 0xffff)) << (i * stride);
    }
    return value;
}

static void at91_spi_mark_last(AT91SPIState *s)
{
    unsigned int index;

    if (s->tx_count) {
        index = (s->tx_head + s->tx_count - 1) % AT91_SPI_FIFO_SIZE;
        s->tx_fifo[index] |= SPI_TDR_LASTXFER;
    } else if (s->tx_shift_valid) {
        s->tx_shift |= SPI_TDR_LASTXFER;
    } else {
        at91_spi_deassert_cs(s, true);
    }
}

static void at91_spi_clear_tx_fifo(AT91SPIState *s)
{
    unsigned int old_count = s->tx_count;

    s->tx_head = 0;
    s->tx_count = 0;
    at91_spi_update_tx_events(s, old_count);
}

static void at91_spi_clear_rx_fifo(AT91SPIState *s)
{
    unsigned int old_count = s->rx_count;

    s->rx_head = 0;
    s->rx_count = 0;
    at91_spi_update_rx_events(s, old_count);
}

static void at91_spi_common_reset(AT91SPIState *s)
{
    timer_del(s->transfer_timer);
    at91_spi_deassert_cs(s, false);
    s->mode = 0;
    s->interrupt_mask = 0;
    s->status = 0;
    memset(s->chip_select, 0, sizeof(s->chip_select));
    s->fifo_mode = 0;
    s->comparison = 0;
    s->crc = 0;
    s->two_pin_mode = 0;
    s->two_pin_header = 0;
    s->write_protection = 0;
    s->write_protection_status = 0;
    memset(s->rx_fifo, 0, sizeof(s->rx_fifo));
    memset(s->tx_fifo, 0, sizeof(s->tx_fifo));
    s->tx_shift = 0;
    s->cs_gap_until_ns = 0;
    s->rx_head = 0;
    s->rx_count = 0;
    s->tx_head = 0;
    s->tx_count = 0;
    s->current_pcs = 0xf;
    s->phase = AT91_SPI_PHASE_IDLE;
    s->spi_enabled = false;
    s->fifo_enabled = false;
    s->tx_shift_valid = false;
    s->disable_pending = false;
    s->comparison_started = false;
    s->cs_active = false;
    s->tx_request_level = false;
    s->rx_request_level = false;
    qemu_set_irq(s->tx_request, 0);
    qemu_set_irq(s->rx_request, 0);
    at91_spi_drive_cs(s);
    at91_spi_update(s);
}

static void at91_spi_write_control(AT91SPIState *s, uint32_t value)
{
    if (value & SPI_CR_SWRST) {
        at91_spi_common_reset(s);
        return;
    }
    if (!s->spi_enabled && (value & SPI_CR_FIFOEN)) {
        s->fifo_enabled = true;
    }
    if (!s->spi_enabled && (value & SPI_CR_FIFODIS)) {
        s->fifo_enabled = false;
        at91_spi_clear_tx_fifo(s);
        at91_spi_clear_rx_fifo(s);
        s->status &= ~SPI_STATUS_FIFO_MASK;
    }
    if (value & SPI_CR_REQCLR) {
        s->comparison_started = false;
    }
    if (value & SPI_CR_TXFCLR) {
        at91_spi_clear_tx_fifo(s);
    }
    if (value & SPI_CR_RXFCLR) {
        at91_spi_clear_rx_fifo(s);
    }
    if (value & SPI_CR_LASTXFER) {
        at91_spi_mark_last(s);
    }
    if (value & SPI_CR_SPIEN) {
        s->spi_enabled = true;
        s->disable_pending = false;
        at91_spi_drive_cs(s);
        at91_spi_start_next(s);
    }
    if (value & SPI_CR_SPIDIS) {
        if (s->tx_shift_valid) {
            s->disable_pending = true;
        } else {
            s->spi_enabled = false;
            s->disable_pending = false;
            at91_spi_deassert_cs(s, false);
        }
    }
    at91_spi_update(s);
}

static bool at91_spi_write_protected(AT91SPIState *s, hwaddr reg)
{
    if ((s->write_protection & SPI_WPMR_WPCREN) && reg == SPI_CR) {
        return true;
    }
    if ((s->write_protection & SPI_WPMR_WPITEN) &&
        (reg == SPI_IER || reg == SPI_IDR)) {
        return true;
    }
    if (!(s->write_protection & SPI_WPMR_WPEN)) {
        return false;
    }

    return reg == SPI_MR ||
           (reg >= SPI_CSR0 && reg <= SPI_CSR3) ||
           reg == SPI_CMPR || reg == SPI_CRCR || reg == SPI_TPMR;
}

static void at91_spi_protection_violation(AT91SPIState *s, hwaddr reg)
{
    /* WPVSRC is eight bits; SPI personality offsets are reported modulo 256. */
    s->write_protection_status = ((reg & 0xff) << 8) | 1;
}

static uint32_t at91_spi_raw_read(AT91SPIState *s, hwaddr reg)
{
    uint32_t value;

    switch (reg) {
    case SPI_MR:
        return s->mode;
    case SPI_SR:
        at91_spi_refresh_status(s);
        value = s->status & SPI_STATUS_MASK;
        s->status &= ~SPI_STATUS_CLEAR_READ;
        at91_spi_update(s);
        return value;
    case SPI_IMR:
        return s->interrupt_mask;
    case SPI_CSR0:
    case SPI_CSR1:
    case SPI_CSR2:
    case SPI_CSR3:
        return s->chip_select[(reg - SPI_CSR0) / 4];
    case SPI_FMR:
        return s->fifo_enabled ? s->fifo_mode : 0;
    case SPI_FLR:
        return s->fifo_enabled ? s->tx_count | (s->rx_count << 16) : 0;
    case SPI_CMPR:
        return s->comparison;
    case SPI_CRCR:
        return s->crc;
    case SPI_TPMR:
        return s->two_pin_mode;
    case SPI_TPHR:
        return s->two_pin_header;
    case SPI_WPMR:
        return s->write_protection;
    case SPI_WPSR:
        value = s->write_protection_status;
        s->write_protection_status = 0;
        return value;
    case SPI_CR:
    case SPI_IER:
    case SPI_IDR:
    case SPI_TDR:
        return 0;
    case SPI_VERSION:
        return SPI_VERSION_SAM9X7;
    default:
        /* Other reserved words read as zero on the SAM9X7 model. */
        return 0;
    }
}

static uint64_t at91_spi_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91SPIState *s = opaque;
    hwaddr reg = offset & ~3;
    uint64_t value;

    if (!s->flexcom_enabled) {
        return 0;
    }
    if (reg == SPI_RDR) {
        return at91_spi_read_rdr(s, size);
    }

    value = at91_spi_raw_read(s, reg);
    return value >> ((offset & 3) * 8);
}

static void at91_spi_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91SPIState *s = opaque;
    hwaddr reg = offset & ~3;
    uint32_t bits = at91_spi_expand_write(offset, value, size);
    uint32_t merged;

    if (!s->flexcom_enabled) {
        return;
    }
    if (at91_spi_write_protected(s, reg)) {
        at91_spi_protection_violation(s, reg);
        return;
    }

    switch (reg) {
    case SPI_CR:
        at91_spi_write_control(s, bits);
        break;
    case SPI_MR:
        s->mode = at91_spi_merge_write(s->mode, offset, value, size) &
                  SPI_MR_MASK;
        at91_spi_drive_cs(s);
        at91_spi_start_next(s);
        at91_spi_update(s);
        break;
    case SPI_IER:
        s->interrupt_mask |= bits & SPI_INT_MASK;
        at91_spi_update(s);
        break;
    case SPI_IDR:
        s->interrupt_mask &= ~(bits & SPI_INT_MASK);
        at91_spi_update(s);
        break;
    case SPI_TDR:
        at91_spi_write_tdr(s, value, size);
        break;
    case SPI_CSR0:
    case SPI_CSR1:
    case SPI_CSR2:
    case SPI_CSR3:
        s->chip_select[(reg - SPI_CSR0) / 4] = at91_spi_merge_write(
            s->chip_select[(reg - SPI_CSR0) / 4], offset, value, size);
        at91_spi_start_next(s);
        break;
    case SPI_FMR:
        if (s->fifo_enabled) {
            s->fifo_mode = at91_spi_merge_write(s->fifo_mode, offset,
                                                 value, size) & SPI_FMR_MASK;
            at91_spi_update(s);
        }
        break;
    case SPI_CMPR:
        s->comparison = at91_spi_merge_write(s->comparison, offset,
                                              value, size);
        s->comparison_started = false;
        break;
    case SPI_CRCR:
        s->crc = at91_spi_merge_write(s->crc, offset, value, size) &
                 SPI_CRCR_MASK;
        break;
    case SPI_TPMR:
        s->two_pin_mode = at91_spi_merge_write(s->two_pin_mode, offset,
                                                value, size) & SPI_TPMR_MASK;
        break;
    case SPI_WPMR:
        merged = at91_spi_merge_write(s->write_protection, offset,
                                       value, size);
        if ((merged & SPI_WPMR_KEY_MASK) == SPI_WPMR_KEY) {
            s->write_protection = merged & SPI_WPMR_MASK;
        }
        break;
    case SPI_RDR:
    case SPI_SR:
    case SPI_IMR:
    case SPI_FLR:
    case SPI_TPHR:
    case SPI_WPSR:
    case SPI_VERSION:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_SPI, reg);
        break;
    }
}

static const MemoryRegionOps at91_spi_ops = {
    .read = at91_spi_read,
    .write = at91_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

uint64_t at91_spi_flexcom_read(AT91SPIState *s, unsigned int size)
{
    return at91_spi_read(s, SPI_RDR, size);
}

void at91_spi_flexcom_write(AT91SPIState *s, uint64_t value,
                            unsigned int size)
{
    at91_spi_write(s, SPI_TDR, value, size);
}

static void at91_spi_set_enabled(void *opaque, int n, int level)
{
    AT91SPIState *s = opaque;

    s->flexcom_enabled = level;
    if (!level) {
        timer_del(s->transfer_timer);
        at91_spi_drive_cs(s);
    } else {
        at91_spi_drive_cs(s);
        if (s->tx_shift_valid && !timer_pending(s->transfer_timer)) {
            at91_spi_schedule_cycles(s, 1);
        } else {
            at91_spi_start_next(s);
        }
    }
    at91_spi_update(s);
}

static void at91_spi_clock_changed(void *opaque, ClockEvent event)
{
    AT91SPIState *s = opaque;

    if (!clock_get_hz(s->pclk) || !clock_get_hz(at91_spi_baud_clock(s))) {
        timer_del(s->transfer_timer);
    } else if (s->tx_shift_valid) {
        at91_spi_schedule_cycles(s, 1);
    } else {
        at91_spi_start_next(s);
    }
    at91_spi_update(s);
}

static void at91_spi_reset(DeviceState *dev)
{
    at91_spi_common_reset(AT91_SPI(dev));
}

static int at91_spi_post_load(void *opaque, int version_id)
{
    AT91SPIState *s = opaque;

    s->mode &= SPI_MR_MASK;
    s->interrupt_mask &= SPI_INT_MASK;
    s->status &= SPI_STATUS_MASK;
    s->fifo_mode &= SPI_FMR_MASK;
    s->crc &= SPI_CRCR_MASK;
    s->two_pin_mode &= SPI_TPMR_MASK;
    s->two_pin_header &= SPI_TPHR_MASK;
    s->write_protection &= SPI_WPMR_MASK;
    s->rx_head %= AT91_SPI_FIFO_SIZE;
    s->tx_head %= AT91_SPI_FIFO_SIZE;
    s->rx_count = MIN(s->rx_count, AT91_SPI_FIFO_SIZE);
    s->tx_count = MIN(s->tx_count, AT91_SPI_FIFO_SIZE);
    if (s->phase > AT91_SPI_PHASE_DLYBCT ||
        (s->phase != AT91_SPI_PHASE_IDLE && !s->tx_shift_valid)) {
        return -EINVAL;
    }
    s->tx_request_level = false;
    s->rx_request_level = false;
    at91_spi_drive_cs(s);
    at91_spi_update(s);
    if (s->tx_shift_valid && !timer_pending(s->transfer_timer) &&
        at91_spi_entry_clocked(s, s->tx_shift)) {
        at91_spi_schedule_cycles(s, 1);
    } else if (!s->tx_shift_valid) {
        timer_del(s->transfer_timer);
        at91_spi_start_next(s);
    }
    return 0;
}

static const VMStateDescription at91_spi_vmstate = {
    .name = TYPE_AT91_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_spi_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mode, AT91SPIState),
        VMSTATE_UINT32(interrupt_mask, AT91SPIState),
        VMSTATE_UINT32(status, AT91SPIState),
        VMSTATE_UINT32_ARRAY(chip_select, AT91SPIState, AT91_SPI_NUM_CS),
        VMSTATE_UINT32(fifo_mode, AT91SPIState),
        VMSTATE_UINT32(comparison, AT91SPIState),
        VMSTATE_UINT32(crc, AT91SPIState),
        VMSTATE_UINT32(two_pin_mode, AT91SPIState),
        VMSTATE_UINT32(two_pin_header, AT91SPIState),
        VMSTATE_UINT32(write_protection, AT91SPIState),
        VMSTATE_UINT32(write_protection_status, AT91SPIState),
        VMSTATE_UINT32_ARRAY(rx_fifo, AT91SPIState, AT91_SPI_FIFO_SIZE),
        VMSTATE_UINT32_ARRAY(tx_fifo, AT91SPIState, AT91_SPI_FIFO_SIZE),
        VMSTATE_UINT32(tx_shift, AT91SPIState),
        VMSTATE_INT64(cs_gap_until_ns, AT91SPIState),
        VMSTATE_UINT8(rx_head, AT91SPIState),
        VMSTATE_UINT8(rx_count, AT91SPIState),
        VMSTATE_UINT8(tx_head, AT91SPIState),
        VMSTATE_UINT8(tx_count, AT91SPIState),
        VMSTATE_UINT8(current_pcs, AT91SPIState),
        VMSTATE_UINT8(phase, AT91SPIState),
        VMSTATE_BOOL(flexcom_enabled, AT91SPIState),
        VMSTATE_BOOL(spi_enabled, AT91SPIState),
        VMSTATE_BOOL(fifo_enabled, AT91SPIState),
        VMSTATE_BOOL(tx_shift_valid, AT91SPIState),
        VMSTATE_BOOL(disable_pending, AT91SPIState),
        VMSTATE_BOOL(comparison_started, AT91SPIState),
        VMSTATE_BOOL(cs_active, AT91SPIState),
        VMSTATE_CLOCK(pclk, AT91SPIState),
        VMSTATE_CLOCK(gclk, AT91SPIState),
        VMSTATE_TIMER_PTR(transfer_timer, AT91SPIState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_spi_realize(DeviceState *dev, Error **errp)
{
    AT91SPIState *s = AT91_SPI(dev);

    if (!s->num_cs || s->num_cs > AT91_SPI_NUM_CS) {
        error_setg(errp, TYPE_AT91_SPI ": num-cs must be between 1 and %u",
                   AT91_SPI_NUM_CS);
        return;
    }
    if (!clock_has_source(s->pclk) || !clock_has_source(s->gclk)) {
        error_setg(errp, TYPE_AT91_SPI ": pclk and gclk must be connected");
        return;
    }

    qdev_init_gpio_out_named(dev, s->cs, "cs", s->num_cs);
}

static void at91_spi_init(Object *obj)
{
    AT91SPIState *s = AT91_SPI(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_spi_ops, s,
                          TYPE_AT91_SPI, SPI_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    qdev_init_gpio_in_named(dev, at91_spi_set_enabled,
                            "flexcom-enabled", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_spi_clock_changed,
                                 s, ClockUpdate);
    s->gclk = qdev_init_clock_in(dev, "gclk", at91_spi_clock_changed,
                                 s, ClockUpdate);
    s->bus = ssi_create_bus(dev, "spi");
    s->transfer_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, at91_spi_timer, s);
}

static void at91_spi_finalize(Object *obj)
{
    AT91SPIState *s = AT91_SPI(obj);

    timer_free(s->transfer_timer);
}

static const Property at91_spi_properties[] = {
    DEFINE_PROP_UINT8("num-cs", AT91SPIState, num_cs, AT91_SPI_NUM_CS),
};

static void at91_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 FLEXCOM SPI";
    dc->realize = at91_spi_realize;
    dc->vmsd = &at91_spi_vmstate;
    device_class_set_props(dc, at91_spi_properties);
    device_class_set_legacy_reset(dc, at91_spi_reset);
}

static const TypeInfo at91_spi_info = {
    .name = TYPE_AT91_SPI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SPIState),
    .instance_init = at91_spi_init,
    .instance_finalize = at91_spi_finalize,
    .class_init = at91_spi_class_init,
};

static void at91_spi_register_types(void)
{
    type_register_static(&at91_spi_info);
}

type_init(at91_spi_register_types)
