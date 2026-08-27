/*
 * Microchip AT91 FLEXCOM USART
 *
 * This models the USART personality integrated in each SAM9X7 FLEXCOM.
 * The byte-oriented backend is intentionally kept separate from the FLEXCOM
 * wrapper: selecting another FLEXCOM personality gates this device without
 * destroying its register state.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "chardev/char-serial.h"
#include "hw/char/at91_usart.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

enum {
    US_CR       = 0x00,
    US_MR       = 0x04,
    US_IER      = 0x08,
    US_IDR      = 0x0c,
    US_IMR      = 0x10,
    US_CSR      = 0x14,
    US_RHR      = 0x18,
    US_THR      = 0x1c,
    US_BRGR     = 0x20,
    US_RTOR     = 0x24,
    US_TTGR     = 0x28,
    US_FIDI     = 0x40,
    US_NER      = 0x44,
    US_IF       = 0x4c,
    US_MAN      = 0x50,
    US_LINMR    = 0x54,
    US_LINIR    = 0x58,
    US_LINBRR   = 0x5c,
    US_LONMR    = 0x60,
    US_LONPR    = 0x64,
    US_LONDL    = 0x68,
    US_LONL2HDR = 0x6c,
    US_LONBL    = 0x70,
    US_LONB1TX  = 0x74,
    US_LONB1RX  = 0x78,
    US_LONPRIO  = 0x7c,
    US_IDTTX    = 0x80,
    US_IDTRX    = 0x84,
    US_ICDIFF   = 0x88,
    US_CMPR     = 0x90,
    US_FMR      = 0xa0,
    US_FLR      = 0xa4,
    US_FIER     = 0xa8,
    US_FIDR     = 0xac,
    US_FIMR     = 0xb0,
    US_FESR     = 0xb4,
    US_WPMR     = 0xe4,
    US_WPSR     = 0xe8,
    US_NAME     = 0xf0,
    US_VERSION  = 0xfc,
};

#define US_MMIO_SIZE            0x200

#define US_CR_RSTRX             BIT(2)
#define US_CR_RSTTX             BIT(3)
#define US_CR_RXEN              BIT(4)
#define US_CR_RXDIS             BIT(5)
#define US_CR_TXEN              BIT(6)
#define US_CR_TXDIS             BIT(7)
#define US_CR_RSTSTA            BIT(8)
#define US_CR_STTBRK            BIT(9)
#define US_CR_STPBRK            BIT(10)
#define US_CR_STTTO             BIT(11)
#define US_CR_RSTIT             BIT(13)
#define US_CR_RSTNACK           BIT(14)
#define US_CR_RETTO             BIT(15)
#define US_CR_RTSEN             BIT(18)
#define US_CR_RTSDIS            BIT(19)
#define US_CR_TXFCLR            BIT(24)
#define US_CR_RXFCLR            BIT(25)
#define US_CR_TXFLCLR           BIT(26)
#define US_CR_REQCLR            BIT(28)
#define US_CR_FIFOEN            BIT(30)
#define US_CR_FIFODIS           BIT(31)

#define US_MR_MODE_MASK         0xf7ffffff
#define US_MR_RESET             0xc0000000
#define US_MR_USART_MODE_MASK   0xf
#define US_MR_USART_MODE_NORMAL 0
#define US_MR_USART_MODE_RS485  1
#define US_MR_USART_MODE_HWHS   2
#define US_MR_USART_MODE_LON    9
#define US_MR_USART_MODE_LIN_H  10
#define US_MR_USART_MODE_LIN_C  11
#define US_MR_USCLKS_SHIFT      4
#define US_MR_USCLKS_MASK       (3U << US_MR_USCLKS_SHIFT)
#define US_MR_CHRL_SHIFT        6
#define US_MR_CHRL_MASK         (3U << US_MR_CHRL_SHIFT)
#define US_MR_SYNC              BIT(8)
#define US_MR_PAR_SHIFT         9
#define US_MR_PAR_MASK          (7U << US_MR_PAR_SHIFT)
#define US_MR_NBSTOP_SHIFT      12
#define US_MR_NBSTOP_MASK       (3U << US_MR_NBSTOP_SHIFT)
#define US_MR_CHMODE_SHIFT      14
#define US_MR_CHMODE_MASK       (3U << US_MR_CHMODE_SHIFT)
#define US_MR_CHMODE_AUTO       (1U << US_MR_CHMODE_SHIFT)
#define US_MR_CHMODE_LOCAL      (2U << US_MR_CHMODE_SHIFT)
#define US_MR_CHMODE_REMOTE     (3U << US_MR_CHMODE_SHIFT)
#define US_MR_MODE9             BIT(17)
#define US_MR_OVER              BIT(19)
#define US_MR_MAN               BIT(29)

#define US_INT_RXRDY            BIT(0)
#define US_INT_TXRDY            BIT(1)
#define US_INT_RXBRK            BIT(2)
#define US_INT_OVRE             BIT(5)
#define US_INT_FRAME            BIT(6)
#define US_INT_PARE             BIT(7)
#define US_INT_TIMEOUT          BIT(8)
#define US_INT_TXEMPTY          BIT(9)
#define US_INT_ITER             BIT(10)
#define US_INT_NACK             BIT(13)
#define US_INT_CTSIC            BIT(19)
#define US_INT_CMP              BIT(22)
#define US_STATUS_CTS           BIT(23)
#define US_INT_MANE             BIT(24)
#define US_INT_DEFAULT_MASK     0x014827e7
#define US_INT_LIN_MASK         0xfe00e1c0
#define US_INT_LON_MASK         0x1f0004c0
#define US_INT_VALID_MASK       (US_INT_DEFAULT_MASK | US_INT_LIN_MASK | \
                                 US_INT_LON_MASK)
#define US_STATUS_DERIVED       (US_INT_RXRDY | US_INT_TXRDY | \
                                 US_INT_TXEMPTY | US_STATUS_CTS)

#define US_BRGR_MASK            0x0007ffff
#define US_RTOR_MASK            0x0001ffff
#define US_TTGR_MASK            0x000000ff
#define US_FIDI_RESET           0x00000174
#define US_FIDI_MASK            0x0000ffff
#define US_IF_MASK              0x000000ff
#define US_MAN_RESET            0xb0011004
#define US_MAN_MASK             0xf30f130f
#define US_LINMR_MASK           0x0003ffff
#define US_LINIR_MASK           0x000000ff
#define US_LINBRR_MASK          0x0007ffff
#define US_LONMR_MASK           0x00ff003f
#define US_LONPR_MASK           0x00003fff
#define US_LONDL_MASK           0x000000ff
#define US_LONL2HDR_MASK        0x000000ff
#define US_LONBL_MASK           0x0000003f
#define US_LONB1_MASK           0x00ffffff
#define US_LONPRIO_MASK         0x00007f7f
#define US_IDT_MASK             0x00ffffff
#define US_ICDIFF_MASK          0x0000000f
#define US_CMPR_MASK            0x01ff71ff
#define US_CMPR_MODE_SHIFT      12
#define US_CMPR_MODE_MASK       (3U << US_CMPR_MODE_SHIFT)
#define US_FMR_MASK             0x3f3f3fb3
#define US_FMR_TXRDYM_MASK      0x3
#define US_FMR_RXRDYM_SHIFT     4
#define US_FMR_FRTSC            BIT(7)
#define US_FMR_TXFTHRES_SHIFT   8
#define US_FMR_RXFTHRES_SHIFT   16
#define US_FMR_RXFTHRES2_SHIFT  24

#define US_FIFO_INT_TXFEF       BIT(0)
#define US_FIFO_INT_TXFFF       BIT(1)
#define US_FIFO_INT_TXFTHF      BIT(2)
#define US_FIFO_INT_RXFEF       BIT(3)
#define US_FIFO_INT_RXFFF       BIT(4)
#define US_FIFO_INT_RXFTHF      BIT(5)
#define US_FIFO_INT_TXFPTEF     BIT(6)
#define US_FIFO_INT_RXFPTEF     BIT(7)
#define US_FIFO_STATUS_TXFLOCK  BIT(8)
#define US_FIFO_INT_RXFTHF2     BIT(9)
#define US_FIFO_INT_MASK        0x000002ff
#define US_FIFO_EVENT_CLEAR     US_FIFO_INT_MASK

#define US_WPMR_WPEN           BIT(0)
#define US_WPMR_WPITEN         BIT(1)
#define US_WPMR_WPCREN         BIT(2)
#define US_WPMR_MASK            0x7
#define US_WPMR_KEY_MASK        0xffffff00
#define US_WPMR_KEY             0x55534100

#define US_MODEM_POLL_NS        (NANOSECONDS_PER_SECOND / 100)

static void at91_usart_update(AT91USARTState *s);
static void at91_usart_start_tx(AT91USARTState *s);
static void at91_usart_schedule_timeout(AT91USARTState *s);
static bool at91_usart_refresh_tiocm(AT91USARTState *s, bool latch_change);

static uint32_t at91_usart_expand_write(hwaddr offset, uint64_t value,
                                        unsigned int size)
{
    return (uint32_t)value << ((offset & 3) * 8);
}

static uint32_t at91_usart_merge_write(uint32_t old, hwaddr offset,
                                       uint64_t value, unsigned int size)
{
    unsigned int shift = (offset & 3) * 8;
    uint64_t mask = MAKE_64BIT_MASK(shift, size * 8);

    return (old & ~mask) | (((uint64_t)value << shift) & mask);
}

static unsigned int at91_usart_ready_count(unsigned int mode)
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

static uint32_t at91_usart_interrupt_valid_mask(AT91USARTState *s)
{
    switch (s->mode & US_MR_USART_MODE_MASK) {
    case US_MR_USART_MODE_LIN_H:
    case US_MR_USART_MODE_LIN_C:
        return US_INT_LIN_MASK;
    case US_MR_USART_MODE_LON:
        return US_INT_LON_MASK;
    default:
        return US_INT_DEFAULT_MASK;
    }
}

static bool at91_usart_multiple_data(AT91USARTState *s)
{
    unsigned int mode = s->mode & US_MR_USART_MODE_MASK;

    return s->fifo_enabled && !(s->mode & (US_MR_MODE9 | US_MR_MAN)) &&
           mode != US_MR_USART_MODE_LIN_H &&
           mode != US_MR_USART_MODE_LIN_C && mode != US_MR_USART_MODE_LON;
}

static Clock *at91_usart_baud_clock(AT91USARTState *s)
{
    unsigned int source = extract32(s->mode, US_MR_USCLKS_SHIFT, 2);

    return source == 2 ? s->gclk : s->pclk;
}

static unsigned int at91_usart_clock_divisor(AT91USARTState *s)
{
    return extract32(s->mode, US_MR_USCLKS_SHIFT, 2) == 1 ? 8 : 1;
}

static bool at91_usart_clocked(AT91USARTState *s)
{
    unsigned int source = extract32(s->mode, US_MR_USCLKS_SHIFT, 2);
    Clock *clock = at91_usart_baud_clock(s);

    return s->flexcom_enabled && source != 3 && clock_get_hz(s->pclk) &&
           clock_get_hz(clock) && extract32(s->baud_generator, 0, 16);
}

static uint64_t at91_usart_bit_cycles(AT91USARTState *s)
{
    uint64_t divisor_eighths;
    unsigned int oversampling;

    divisor_eighths = extract32(s->baud_generator, 0, 16) * 8ULL +
                      extract32(s->baud_generator, 16, 3);
    oversampling = (s->mode & US_MR_SYNC) ? 1 :
                   (s->mode & US_MR_OVER) ? 8 : 16;

    return DIV_ROUND_UP(divisor_eighths * oversampling *
                        at91_usart_clock_divisor(s), 8);
}

static unsigned int at91_usart_character_bits(AT91USARTState *s)
{
    unsigned int data_bits;
    unsigned int parity;
    unsigned int stop;

    data_bits = s->mode & US_MR_MODE9 ? 9 :
                5 + extract32(s->mode, US_MR_CHRL_SHIFT, 2);
    parity = extract32(s->mode, US_MR_PAR_SHIFT, 3);
    stop = extract32(s->mode, US_MR_NBSTOP_SHIFT, 2) ? 2 : 1;

    return 1 + data_bits + (parity == 4 || parity == 5 ? 0 : 1) + stop;
}

static uint64_t at91_usart_character_ns(AT91USARTState *s)
{
    uint64_t cycles = at91_usart_bit_cycles(s) *
                      (at91_usart_character_bits(s) +
                       s->transmitter_timeguard);

    return MAX(clock_ticks_to_ns(at91_usart_baud_clock(s), cycles), 1);
}

static uint64_t at91_usart_rx_character_ns(AT91USARTState *s)
{
    uint64_t cycles = at91_usart_bit_cycles(s) *
                      at91_usart_character_bits(s);

    return MAX(clock_ticks_to_ns(at91_usart_baud_clock(s), cycles), 1);
}

static void at91_usart_update_parameters(AT91USARTState *s)
{
    QEMUSerialSetParams params;
    uint64_t divisor_eighths;
    uint64_t denominator;
    unsigned int parity;

    if (!at91_usart_clocked(s)) {
        return;
    }

    divisor_eighths = extract32(s->baud_generator, 0, 16) * 8ULL +
                      extract32(s->baud_generator, 16, 3);
    denominator = divisor_eighths *
                  ((s->mode & US_MR_SYNC) ? 1 :
                   (s->mode & US_MR_OVER) ? 8 : 16) *
                  at91_usart_clock_divisor(s);
    params.speed = (clock_get_hz(at91_usart_baud_clock(s)) * 8ULL) /
                   denominator;
    params.data_bits = s->mode & US_MR_MODE9 ? 9 :
                       5 + extract32(s->mode, US_MR_CHRL_SHIFT, 2);
    params.stop_bits = extract32(s->mode, US_MR_NBSTOP_SHIFT, 2) ? 2 : 1;
    parity = extract32(s->mode, US_MR_PAR_SHIFT, 3);
    params.parity = parity == 0 ? 'E' : parity == 1 ? 'O' : 'N';
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_PARAMS, &params);
}

static void at91_usart_set_request(qemu_irq irq, bool *old_level,
                                   bool new_level)
{
    if (*old_level != new_level) {
        *old_level = new_level;
        qemu_set_irq(irq, new_level);
    }
}

static void at91_usart_refresh_status(AT91USARTState *s)
{
    unsigned int tx_limit = s->fifo_enabled ? AT91_USART_FIFO_SIZE : 1;
    unsigned int tx_ready = s->fifo_enabled ?
        at91_usart_ready_count(s->fifo_mode & US_FMR_TXRDYM_MASK) : 1;
    unsigned int rx_ready = s->fifo_enabled ?
        at91_usart_ready_count(s->fifo_mode >> US_FMR_RXRDYM_SHIFT) : 1;

    s->status &= ~US_STATUS_DERIVED;
    if (s->receiver_enabled && s->rx_count >= rx_ready) {
        s->status |= US_INT_RXRDY;
    }
    if (s->transmitter_enabled && !s->tx_fifo_locked &&
        tx_limit - s->tx_count >= tx_ready) {
        s->status |= US_INT_TXRDY;
    }
    if (s->transmitter_enabled && !s->tx_count && !s->tx_shift_valid &&
        !s->tx_break) {
        s->status |= US_INT_TXEMPTY;
    }
    if (s->cts_level) {
        s->status |= US_STATUS_CTS;
    }
    if (s->tx_fifo_locked) {
        s->fifo_event_status |= US_FIFO_STATUS_TXFLOCK;
    } else {
        s->fifo_event_status &= ~US_FIFO_STATUS_TXFLOCK;
    }
}

static bool at91_usart_fifo_controls_rts(AT91USARTState *s)
{
    return s->fifo_enabled && (s->fifo_mode & US_FMR_FRTSC) &&
           (s->mode & US_MR_USART_MODE_MASK) == US_MR_USART_MODE_HWHS;
}

static void at91_usart_refresh_fifo_rts(AT91USARTState *s)
{
    unsigned int high;
    unsigned int low;

    if (!at91_usart_fifo_controls_rts(s)) {
        return;
    }

    high = extract32(s->fifo_mode, US_FMR_RXFTHRES_SHIFT, 6);
    low = extract32(s->fifo_mode, US_FMR_RXFTHRES2_SHIFT, 6);

    /*
     * RXFTHRES and RXFTHRES2 form a hysteresis window.  RXFTHRES wins
     * if the guest programs an invalid overlapping pair, matching the
     * documented qualification that RXFTHRES2 only controls RTS while
     * RXFTHRES is not reached.
     */
    if (s->rx_count >= high) {
        s->fifo_rts_level = true;
    } else if (s->rx_count <= low) {
        s->fifo_rts_level = false;
    }
}

static void at91_usart_set_cts_level(AT91USARTState *s, bool level,
                                     bool latch_change)
{
    if (s->cts_level == level) {
        return;
    }

    s->cts_level = level;
    if (latch_change) {
        s->status |= US_INT_CTSIC;
    }
    if (!level) {
        at91_usart_start_tx(s);
    } else {
        at91_usart_update(s);
    }
}

static void at91_usart_tiocm_unavailable(AT91USARTState *s,
                                         bool latch_change)
{
    s->tiocm_supported = false;
    s->tiocm_set_failed = false;
    s->tiocm_rts_valid = false;
    timer_del(s->modem_status_poll);
    at91_usart_set_cts_level(s, s->cts_gpio_level, latch_change);
}

static bool at91_usart_refresh_tiocm(AT91USARTState *s, bool latch_change)
{
    int flags = 0;

    if (qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_GET_TIOCM,
                          &flags) < 0) {
        at91_usart_tiocm_unavailable(s, latch_change);
        return false;
    }

    if (!s->tiocm_supported) {
        s->tiocm_set_failed = false;
        s->tiocm_rts_valid = false;
    }
    s->tiocm_supported = true;

    /* CTS and RTS are active-low pins, unlike the asserted TIOCM bits. */
    at91_usart_set_cts_level(s, !(flags & CHR_TIOCM_CTS), latch_change);
    return true;
}

static bool at91_usart_modem_poll_needed(AT91USARTState *s)
{
    return s->tiocm_supported && s->flexcom_enabled &&
           ((s->interrupt_mask & US_INT_CTSIC) ||
            ((s->mode & US_MR_USART_MODE_MASK) == US_MR_USART_MODE_HWHS &&
             s->transmitter_enabled));
}

static void at91_usart_schedule_modem_poll(AT91USARTState *s)
{
    if (!at91_usart_modem_poll_needed(s)) {
        timer_del(s->modem_status_poll);
    } else if (!timer_pending(s->modem_status_poll)) {
        timer_mod_ns(s->modem_status_poll,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     US_MODEM_POLL_NS);
    }
}

static void at91_usart_drive_tiocm_rts(AT91USARTState *s, bool level)
{
    int flags = 0;

    if (!s->tiocm_supported || s->tiocm_set_failed ||
        (s->tiocm_rts_valid && s->tiocm_rts_level == level)) {
        return;
    }
    if (qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_GET_TIOCM,
                          &flags) < 0) {
        at91_usart_tiocm_unavailable(s, true);
        return;
    }

    flags &= ~CHR_TIOCM_RTS;
    if (!level) {
        flags |= CHR_TIOCM_RTS;
    }
    if (qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_TIOCM,
                          &flags) < 0) {
        /* Input modem status may still be usable on a read-only backend. */
        s->tiocm_set_failed = true;
        s->tiocm_rts_valid = false;
        return;
    }

    s->tiocm_rts_level = level;
    s->tiocm_rts_valid = true;
}

static void at91_usart_modem_status_poll(void *opaque)
{
    AT91USARTState *s = opaque;

    at91_usart_refresh_tiocm(s, true);
    at91_usart_update(s);
}

static void at91_usart_update(AT91USARTState *s)
{
    bool active;
    bool tx;
    bool rx;
    bool gpio_rts_level;
    bool rts_level;

    at91_usart_refresh_status(s);
    at91_usart_refresh_fifo_rts(s);
    active = at91_usart_clocked(s) &&
             (s->mode & US_MR_CHMODE_MASK) != US_MR_CHMODE_REMOTE;
    tx = active && s->transmitter_enabled &&
         (s->status & US_INT_TXRDY);
    rx = active && s->receiver_enabled &&
         (s->status & US_INT_RXRDY);
    at91_usart_set_request(s->tx_request, &s->tx_request_level, tx);
    at91_usart_set_request(s->rx_request, &s->rx_request_level, rx);

    qemu_set_irq(s->irq, s->flexcom_enabled &&
                 ((s->status & s->interrupt_mask) ||
                  (s->fifo_enabled &&
                   (s->fifo_event_status & s->fifo_interrupt_mask))));

    switch (s->mode & US_MR_USART_MODE_MASK) {
    case US_MR_USART_MODE_RS485:
        /* In RS485 mode RTS is active while the transmitter is not empty. */
        rts_level = !(s->status & US_INT_TXEMPTY);
        break;
    case US_MR_USART_MODE_HWHS:
        rts_level = at91_usart_fifo_controls_rts(s) ?
                    s->fifo_rts_level : s->rts_enabled;
        break;
    default:
        rts_level = !s->rts_enabled;
        break;
    }
    gpio_rts_level = s->flexcom_enabled && rts_level;
    qemu_set_irq(s->rts, gpio_rts_level);
    /* A disconnected USART must not tell an external peer to transmit. */
    at91_usart_drive_tiocm_rts(s,
                               s->flexcom_enabled ? rts_level : true);
    at91_usart_schedule_modem_poll(s);
}

static void at91_usart_update_tx_events(AT91USARTState *s,
                                        unsigned int old_count)
{
    unsigned int threshold = extract32(s->fifo_mode,
                                       US_FMR_TXFTHRES_SHIFT, 6);

    if (!s->fifo_enabled) {
        return;
    }
    if (s->tx_count == AT91_USART_FIFO_SIZE &&
        old_count != AT91_USART_FIFO_SIZE) {
        s->fifo_event_status |= US_FIFO_INT_TXFFF;
    }
    if (!s->tx_count && old_count) {
        s->fifo_event_status |= US_FIFO_INT_TXFEF;
    }
    if (old_count > threshold && s->tx_count <= threshold) {
        s->fifo_event_status |= US_FIFO_INT_TXFTHF;
    }
}

static void at91_usart_update_rx_events(AT91USARTState *s,
                                        unsigned int old_count)
{
    unsigned int threshold = extract32(s->fifo_mode,
                                       US_FMR_RXFTHRES_SHIFT, 6);
    unsigned int threshold2 = extract32(s->fifo_mode,
                                        US_FMR_RXFTHRES2_SHIFT, 6);

    if (!s->fifo_enabled) {
        return;
    }
    if (s->rx_count == AT91_USART_FIFO_SIZE &&
        old_count != AT91_USART_FIFO_SIZE) {
        s->fifo_event_status |= US_FIFO_INT_RXFFF;
    }
    if (!s->rx_count && old_count) {
        s->fifo_event_status |= US_FIFO_INT_RXFEF;
    }
    if (old_count < threshold && s->rx_count >= threshold) {
        s->fifo_event_status |= US_FIFO_INT_RXFTHF;
    }
    if (old_count > threshold2 && s->rx_count <= threshold2) {
        s->fifo_event_status |= US_FIFO_INT_RXFTHF2;
    }
}

static void at91_usart_schedule_tx(AT91USARTState *s)
{
    timer_del(s->tx_timer);
    if (s->tx_shift_valid && at91_usart_clocked(s)) {
        timer_mod_ns(s->tx_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     at91_usart_character_ns(s));
    }
}

static void at91_usart_start_tx(AT91USARTState *s)
{
    unsigned int old_count;

    if (s->tx_shift_valid || !s->tx_count || !s->transmitter_enabled ||
        !at91_usart_clocked(s) || s->tx_break ||
        ((s->mode & US_MR_USART_MODE_MASK) == US_MR_USART_MODE_HWHS &&
         s->cts_level)) {
        at91_usart_update(s);
        return;
    }

    old_count = s->tx_count;
    s->tx_shift = s->tx_fifo[s->tx_head];
    s->tx_head = (s->tx_head + 1) % AT91_USART_FIFO_SIZE;
    s->tx_count--;
    s->tx_shift_valid = true;
    at91_usart_update_tx_events(s, old_count);
    at91_usart_schedule_tx(s);
    at91_usart_update(s);
}

static void at91_usart_push_rx(AT91USARTState *s, uint16_t value)
{
    unsigned int limit = s->fifo_enabled ? AT91_USART_FIFO_SIZE : 1;
    unsigned int old_count = s->rx_count;
    unsigned int index;
    unsigned int compare_mode;
    uint16_t val1;
    uint16_t val2;
    bool match;

    if (!s->receiver_enabled || !s->flexcom_enabled) {
        return;
    }

    val1 = extract32(s->comparison, 0, 9);
    val2 = extract32(s->comparison, 16, 9);
    match = value >= val1 && value <= val2;
    compare_mode = extract32(s->comparison, US_CMPR_MODE_SHIFT, 2);
    if (match) {
        s->status |= US_INT_CMP;
        if (compare_mode == 1) {
            s->comparison_started = true;
        }
    }
    if ((compare_mode == 1 && !s->comparison_started) ||
        (compare_mode == 2 && !match)) {
        at91_usart_update(s);
        return;
    }

    if (s->rx_count == limit) {
        s->status |= US_INT_OVRE;
    } else {
        index = (s->rx_head + s->rx_count) % AT91_USART_FIFO_SIZE;
        s->rx_fifo[index] = value & (s->mode & US_MR_MODE9 ? 0x1ff : 0xff);
        s->rx_count++;
        at91_usart_update_rx_events(s, old_count);
    }

    if (s->receiver_timeout) {
        s->timeout_waiting = false;
        s->timeout_running = true;
        at91_usart_schedule_timeout(s);
    }
    at91_usart_update(s);
}

static uint16_t at91_usart_pop_rx(AT91USARTState *s)
{
    unsigned int old_count = s->rx_count;
    uint16_t value = 0;

    if (!s->rx_count) {
        if (s->fifo_enabled) {
            s->fifo_event_status |= US_FIFO_INT_RXFPTEF;
        }
    } else {
        value = s->rx_fifo[s->rx_head];
        s->rx_head = (s->rx_head + 1) % AT91_USART_FIFO_SIZE;
        s->rx_count--;
        at91_usart_update_rx_events(s, old_count);
    }

    at91_usart_update(s);
    qemu_chr_fe_accept_input(&s->chr);
    return value;
}

static void at91_usart_receive_value(AT91USARTState *s, uint16_t value)
{
    uint8_t ch = value;

    if ((s->mode & US_MR_CHMODE_MASK) == US_MR_CHMODE_REMOTE) {
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
        return;
    }

    at91_usart_push_rx(s, value);
    if ((s->mode & US_MR_CHMODE_MASK) == US_MR_CHMODE_AUTO &&
        s->transmitter_enabled) {
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
    }
}

static void at91_usart_tx_complete(void *opaque)
{
    AT91USARTState *s = opaque;
    uint16_t value = s->tx_shift;
    uint8_t ch = value;

    if (!s->tx_shift_valid) {
        return;
    }

    s->tx_shift_valid = false;
    if ((s->mode & US_MR_CHMODE_MASK) == US_MR_CHMODE_LOCAL) {
        at91_usart_receive_value(s, value);
    } else if ((s->mode & US_MR_CHMODE_MASK) != US_MR_CHMODE_REMOTE) {
        qemu_chr_fe_write_all(&s->chr, &ch, 1);
    }
    if ((s->mode & US_MR_USART_MODE_MASK) == US_MR_USART_MODE_HWHS) {
        at91_usart_refresh_tiocm(s, true);
    }
    at91_usart_start_tx(s);
}

static void at91_usart_schedule_timeout(AT91USARTState *s)
{
    uint64_t cycles;

    timer_del(s->timeout_timer);
    if (!s->timeout_running || s->timeout_waiting ||
        !s->receiver_enabled || !s->receiver_timeout ||
        !at91_usart_clocked(s)) {
        return;
    }

    cycles = at91_usart_bit_cycles(s) * s->receiver_timeout;
    timer_mod_ns(s->timeout_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                 MAX(clock_ticks_to_ns(at91_usart_baud_clock(s), cycles),
                     1));
}

static void at91_usart_timeout(void *opaque)
{
    AT91USARTState *s = opaque;

    s->timeout_running = false;
    s->status |= US_INT_TIMEOUT;
    at91_usart_update(s);
}

static int at91_usart_can_receive(void *opaque)
{
    AT91USARTState *s = opaque;
    unsigned int limit = s->fifo_enabled ? AT91_USART_FIFO_SIZE : 1;

    if (!at91_usart_clocked(s)) {
        return 0;
    }
    if (timer_pending(s->rx_spacing_timer)) {
        return 0;
    }
    if ((s->mode & US_MR_CHMODE_MASK) == US_MR_CHMODE_REMOTE) {
        return 1;
    }
    return s->receiver_enabled ? MIN(1U, limit - s->rx_count) : 0;
}

static void at91_usart_receive(void *opaque, const uint8_t *buf, int size)
{
    AT91USARTState *s = opaque;
    int i;

    g_assert(size <= 1);
    for (i = 0; i < size; i++) {
        at91_usart_receive_value(s, buf[i]);
    }
    if (size) {
        timer_mod_ns(s->rx_spacing_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     at91_usart_rx_character_ns(s));
    }
}

static void at91_usart_rx_spacing_elapsed(void *opaque)
{
    AT91USARTState *s = opaque;

    qemu_chr_fe_accept_input(&s->chr);
}

static void at91_usart_event(void *opaque, QEMUChrEvent event)
{
    AT91USARTState *s = opaque;

    if (event == CHR_EVENT_BREAK && s->flexcom_enabled &&
        s->receiver_enabled) {
        s->status |= US_INT_RXBRK;
        at91_usart_update(s);
    }
}

static void at91_usart_write_thr(AT91USARTState *s, uint16_t value)
{
    unsigned int limit = s->fifo_enabled ? AT91_USART_FIFO_SIZE : 1;
    unsigned int old_count = s->tx_count;
    unsigned int index;

    if (!s->transmitter_enabled || s->tx_fifo_locked ||
        (s->mode & US_MR_CHMODE_MASK) == US_MR_CHMODE_REMOTE) {
        return;
    }
    if (s->tx_count == limit) {
        if (s->fifo_enabled) {
            s->fifo_event_status |= US_FIFO_INT_TXFPTEF;
            s->tx_fifo_locked = true;
        }
        at91_usart_update(s);
        return;
    }

    index = (s->tx_head + s->tx_count) % AT91_USART_FIFO_SIZE;
    s->tx_fifo[index] = value & (s->mode & US_MR_MODE9 ? 0x1ff : 0xff);
    s->tx_count++;
    at91_usart_update_tx_events(s, old_count);
    at91_usart_start_tx(s);
}

static uint32_t at91_usart_raw_read(AT91USARTState *s, hwaddr reg)
{
    uint32_t value;

    switch (reg) {
    case US_MR:
        return s->mode;
    case US_IMR:
        return s->interrupt_mask & at91_usart_interrupt_valid_mask(s);
    case US_CSR:
        at91_usart_refresh_tiocm(s, true);
        at91_usart_refresh_status(s);
        value = s->status;
        s->status &= ~US_INT_CTSIC;
        at91_usart_update(s);
        return value;
    case US_BRGR:
        return s->baud_generator;
    case US_RTOR:
        return s->receiver_timeout;
    case US_TTGR:
        return s->transmitter_timeguard;
    case US_FIDI:
        return s->fidi;
    case US_NER:
        value = s->number_errors;
        s->number_errors = 0;
        return value;
    case US_IF:
        return s->irda_filter;
    case US_MAN:
        return s->manchester;
    case US_LINMR:
        return s->lin_mode;
    case US_LINIR:
        return s->lin_identifier;
    case US_LINBRR:
        return s->lin_baud;
    case US_LONMR:
        return s->lon_mode;
    case US_LONPR:
        return s->lon_preamble;
    case US_LONDL:
        return s->lon_data_length;
    case US_LONL2HDR:
        return s->lon_l2_header;
    case US_LONBL:
        return s->lon_backlog;
    case US_LONB1TX:
        return s->lon_beta1_tx;
    case US_LONB1RX:
        return s->lon_beta1_rx;
    case US_LONPRIO:
        return s->lon_priority;
    case US_IDTTX:
        return s->lon_idt_tx;
    case US_IDTRX:
        return s->lon_idt_rx;
    case US_ICDIFF:
        return s->ic_diff;
    case US_CMPR:
        return s->comparison;
    case US_FMR:
        return s->fifo_enabled ? s->fifo_mode : 0;
    case US_FLR:
        return s->fifo_enabled ? s->tx_count | (s->rx_count << 16) : 0;
    case US_FIMR:
        return s->fifo_enabled ? s->fifo_interrupt_mask : 0;
    case US_FESR:
        return s->fifo_enabled ? s->fifo_event_status : 0;
    case US_WPMR:
        return s->write_protection;
    case US_WPSR:
        value = s->write_protection_status;
        s->write_protection_status = 0;
        return value;
    case US_CR:
    case US_IER:
    case US_IDR:
    case US_THR:
    case US_FIER:
    case US_FIDR:
    case US_NAME:
    case US_VERSION:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_USART, reg);
        return 0;
    }
}

static uint64_t at91_usart_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    AT91USARTState *s = opaque;
    hwaddr reg = offset & ~3;
    uint32_t value = 0;
    unsigned int i;

    if (!s->flexcom_enabled) {
        return 0;
    }
    if (reg == US_RHR) {
        if (at91_usart_multiple_data(s)) {
            for (i = 0; i < size; i++) {
                value |= (uint32_t)at91_usart_pop_rx(s) << (i * 8);
            }
            return value;
        }
        return at91_usart_pop_rx(s);
    }

    value = at91_usart_raw_read(s, reg);
    return value >> ((offset & 3) * 8);
}

static void at91_usart_reset_receiver(AT91USARTState *s)
{
    s->rx_head = 0;
    s->rx_count = 0;
    s->receiver_enabled = false;
    s->timeout_running = false;
    s->timeout_waiting = false;
    timer_del(s->rx_spacing_timer);
    timer_del(s->timeout_timer);
    s->status &= ~(US_INT_RXRDY | US_INT_OVRE | US_INT_FRAME |
                   US_INT_PARE);
}

static void at91_usart_reset_transmitter(AT91USARTState *s)
{
    s->tx_head = 0;
    s->tx_count = 0;
    s->tx_shift = 0;
    s->tx_shift_valid = false;
    s->transmitter_enabled = false;
    s->tx_fifo_locked = false;
    s->tx_break = false;
    timer_del(s->tx_timer);
}

static void at91_usart_write_control(AT91USARTState *s, uint32_t value)
{
    int break_enable;
    unsigned int usart_mode = s->mode & US_MR_USART_MODE_MASK;

    at91_usart_refresh_tiocm(s, true);

    if (value & US_CR_RSTRX) {
        at91_usart_reset_receiver(s);
    }
    if (value & US_CR_RSTTX) {
        at91_usart_reset_transmitter(s);
    }
    if (value & US_CR_RXDIS) {
        s->receiver_enabled = false;
        s->timeout_running = false;
        timer_del(s->rx_spacing_timer);
        timer_del(s->timeout_timer);
    } else if (value & US_CR_RXEN) {
        s->receiver_enabled = true;
        qemu_chr_fe_accept_input(&s->chr);
    }
    if (value & US_CR_TXDIS) {
        s->transmitter_enabled = false;
    } else if (value & US_CR_TXEN) {
        s->transmitter_enabled = true;
    }
    if (value & US_CR_RSTSTA) {
        uint32_t clear = US_INT_OVRE | US_INT_FRAME | US_INT_PARE;

        if (usart_mode == US_MR_USART_MODE_LIN_H ||
            usart_mode == US_MR_USART_MODE_LIN_C) {
            clear |= 0xfe00e000;
        } else if (usart_mode == US_MR_USART_MODE_LON) {
            clear |= US_INT_LON_MASK;
        } else {
            clear |= US_INT_RXBRK | US_INT_CMP | US_INT_MANE;
        }
        s->status &= ~clear;
        s->fifo_event_status &= ~US_FIFO_EVENT_CLEAR;
    }
    if (value & US_CR_RSTNACK) {
        s->status &= ~US_INT_NACK;
    }
    if (value & US_CR_RSTIT) {
        s->status &= ~US_INT_ITER;
    }
    if (value & US_CR_STTTO) {
        s->status &= ~US_INT_TIMEOUT;
        s->timeout_running = false;
        s->timeout_waiting = true;
        timer_del(s->timeout_timer);
    }
    if (value & US_CR_RETTO) {
        s->status &= ~US_INT_TIMEOUT;
        s->timeout_running = true;
        s->timeout_waiting = false;
        at91_usart_schedule_timeout(s);
    }
    if (value & US_CR_STTBRK) {
        s->tx_break = true;
        break_enable = 1;
        qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK,
                          &break_enable);
    }
    if (value & US_CR_STPBRK) {
        s->tx_break = false;
        break_enable = 0;
        qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK,
                          &break_enable);
    }
    if (usart_mode == US_MR_USART_MODE_NORMAL ||
        usart_mode == US_MR_USART_MODE_HWHS) {
        if (value & US_CR_RTSEN) {
            s->rts_enabled = true;
        }
        if (value & US_CR_RTSDIS) {
            s->rts_enabled = false;
        }
    }
    if (value & US_CR_REQCLR) {
        s->comparison_started = false;
    }
    if (value & US_CR_TXFLCLR) {
        s->tx_fifo_locked = false;
    }
    if (value & US_CR_RXFCLR) {
        unsigned int old_count = s->rx_count;

        s->rx_head = 0;
        s->rx_count = 0;
        at91_usart_update_rx_events(s, old_count);
    }
    if (value & US_CR_TXFCLR) {
        unsigned int old_count = s->tx_count;

        s->tx_head = 0;
        s->tx_count = 0;
        at91_usart_update_tx_events(s, old_count);
    }
    if (value & US_CR_FIFOEN) {
        s->fifo_enabled = true;
    }
    if (value & US_CR_FIFODIS) {
        s->fifo_enabled = false;
        s->rx_head = 0;
        s->rx_count = 0;
        s->tx_head = 0;
        s->tx_count = 0;
        s->tx_fifo_locked = false;
    }

    at91_usart_start_tx(s);
    at91_usart_schedule_timeout(s);
    at91_usart_update(s);
}

static bool at91_usart_write_protected(AT91USARTState *s, hwaddr reg)
{
    if ((s->write_protection & US_WPMR_WPCREN) && reg == US_CR) {
        return true;
    }
    if ((s->write_protection & US_WPMR_WPITEN) &&
        (reg == US_IER || reg == US_IDR)) {
        return true;
    }
    if (!(s->write_protection & US_WPMR_WPEN)) {
        return false;
    }

    switch (reg) {
    case US_MR:
    case US_BRGR:
    case US_RTOR:
    case US_TTGR:
    case US_FIDI:
    case US_IF:
    case US_MAN:
    case US_LONMR:
    case US_LONB1TX:
    case US_LONB1RX:
    case US_LONPRIO:
    case US_IDTTX:
    case US_IDTRX:
    case US_ICDIFF:
    case US_CMPR:
        return true;
    default:
        return false;
    }
}

static void at91_usart_protection_violation(AT91USARTState *s, hwaddr reg)
{
    s->write_protection_status = ((reg + 0x200) << 8) | 1;
}

static void at91_usart_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    AT91USARTState *s = opaque;
    hwaddr reg = offset & ~3;
    uint32_t bits = at91_usart_expand_write(offset, value, size);
    uint32_t merged;
    unsigned int i;

    if (!s->flexcom_enabled) {
        return;
    }
    if (at91_usart_write_protected(s, reg)) {
        at91_usart_protection_violation(s, reg);
        return;
    }

    switch (reg) {
    case US_CR:
        at91_usart_write_control(s, bits);
        break;
    case US_MR:
        s->mode = at91_usart_merge_write(s->mode, offset, value, size) &
                  US_MR_MODE_MASK;
        at91_usart_update_parameters(s);
        at91_usart_refresh_tiocm(s, true);
        at91_usart_start_tx(s);
        at91_usart_schedule_timeout(s);
        at91_usart_update(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case US_IER:
        s->interrupt_mask |= bits & at91_usart_interrupt_valid_mask(s);
        at91_usart_update(s);
        break;
    case US_IDR:
        s->interrupt_mask &= ~(bits & at91_usart_interrupt_valid_mask(s));
        at91_usart_update(s);
        break;
    case US_THR:
        if ((s->mode & US_MR_USART_MODE_MASK) == US_MR_USART_MODE_HWHS) {
            at91_usart_refresh_tiocm(s, true);
        }
        if (at91_usart_multiple_data(s)) {
            for (i = 0; i < size; i++) {
                at91_usart_write_thr(s, value >> (i * 8));
            }
        } else {
            at91_usart_write_thr(s, value);
        }
        break;
    case US_BRGR:
        s->baud_generator = at91_usart_merge_write(s->baud_generator,
                                                    offset, value, size) &
                            US_BRGR_MASK;
        s->lin_baud = s->baud_generator;
        at91_usart_update_parameters(s);
        at91_usart_start_tx(s);
        at91_usart_schedule_tx(s);
        at91_usart_schedule_timeout(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case US_RTOR:
        s->receiver_timeout = at91_usart_merge_write(s->receiver_timeout,
                                                      offset, value, size) &
                              US_RTOR_MASK;
        if (!s->receiver_timeout) {
            s->status &= ~US_INT_TIMEOUT;
            s->timeout_running = false;
            s->timeout_waiting = false;
            timer_del(s->timeout_timer);
        } else if (!s->timeout_waiting) {
            s->timeout_running = true;
            at91_usart_schedule_timeout(s);
        }
        at91_usart_update(s);
        break;
    case US_TTGR:
        s->transmitter_timeguard = at91_usart_merge_write(
            s->transmitter_timeguard, offset, value, size) & US_TTGR_MASK;
        at91_usart_schedule_tx(s);
        break;
    case US_FIDI:
        s->fidi = at91_usart_merge_write(s->fidi, offset, value, size) &
                  US_FIDI_MASK;
        break;
    case US_IF:
        s->irda_filter = at91_usart_merge_write(s->irda_filter, offset,
                                                 value, size) & US_IF_MASK;
        break;
    case US_MAN:
        s->manchester = at91_usart_merge_write(s->manchester, offset,
                                                value, size) & US_MAN_MASK;
        break;
    case US_LINMR:
        s->lin_mode = at91_usart_merge_write(s->lin_mode, offset, value,
                                              size) & US_LINMR_MASK;
        break;
    case US_LINIR:
        s->lin_identifier = at91_usart_merge_write(s->lin_identifier,
                                                    offset, value, size) &
                            US_LINIR_MASK;
        break;
    case US_LONMR:
        s->lon_mode = at91_usart_merge_write(s->lon_mode, offset, value,
                                              size) & US_LONMR_MASK;
        break;
    case US_LONPR:
        s->lon_preamble = at91_usart_merge_write(s->lon_preamble, offset,
                                                  value, size) & US_LONPR_MASK;
        break;
    case US_LONDL:
        s->lon_data_length = at91_usart_merge_write(s->lon_data_length,
                                                     offset, value, size) &
                             US_LONDL_MASK;
        break;
    case US_LONL2HDR:
        s->lon_l2_header = at91_usart_merge_write(s->lon_l2_header, offset,
                                                   value, size) &
                           US_LONL2HDR_MASK;
        break;
    case US_LONB1TX:
        s->lon_beta1_tx = at91_usart_merge_write(s->lon_beta1_tx, offset,
                                                  value, size) & US_LONB1_MASK;
        break;
    case US_LONB1RX:
        s->lon_beta1_rx = at91_usart_merge_write(s->lon_beta1_rx, offset,
                                                  value, size) & US_LONB1_MASK;
        break;
    case US_LONPRIO:
        s->lon_priority = at91_usart_merge_write(s->lon_priority, offset,
                                                  value, size) &
                          US_LONPRIO_MASK;
        break;
    case US_IDTTX:
        s->lon_idt_tx = at91_usart_merge_write(s->lon_idt_tx, offset,
                                                value, size) & US_IDT_MASK;
        break;
    case US_IDTRX:
        s->lon_idt_rx = at91_usart_merge_write(s->lon_idt_rx, offset,
                                                value, size) & US_IDT_MASK;
        break;
    case US_ICDIFF:
        s->ic_diff = at91_usart_merge_write(s->ic_diff, offset, value,
                                             size) & US_ICDIFF_MASK;
        break;
    case US_CMPR:
        s->comparison = at91_usart_merge_write(s->comparison, offset,
                                                value, size) & US_CMPR_MASK;
        s->comparison_started = false;
        break;
    case US_FMR:
        s->fifo_mode = at91_usart_merge_write(s->fifo_mode, offset,
                                               value, size) & US_FMR_MASK;
        at91_usart_update(s);
        break;
    case US_FIER:
        s->fifo_interrupt_mask |= bits & US_FIFO_INT_MASK;
        at91_usart_update(s);
        break;
    case US_FIDR:
        s->fifo_interrupt_mask &= ~(bits & US_FIFO_INT_MASK);
        at91_usart_update(s);
        break;
    case US_WPMR:
        merged = at91_usart_merge_write(s->write_protection, offset,
                                         value, size);
        if ((merged & US_WPMR_KEY_MASK) == US_WPMR_KEY) {
            s->write_protection = merged & US_WPMR_MASK;
        }
        break;
    case US_IMR:
    case US_CSR:
    case US_RHR:
    case US_NER:
    case US_LINBRR:
    case US_LONBL:
    case US_FLR:
    case US_FIMR:
    case US_FESR:
    case US_WPSR:
    case US_NAME:
    case US_VERSION:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_USART, reg);
        break;
    }
}

static const MemoryRegionOps at91_usart_ops = {
    .read = at91_usart_read,
    .write = at91_usart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

uint64_t at91_usart_flexcom_read(AT91USARTState *s, unsigned int size)
{
    return at91_usart_read(s, US_RHR, size);
}

void at91_usart_flexcom_write(AT91USARTState *s, uint64_t value,
                              unsigned int size)
{
    at91_usart_write(s, US_THR, value, size);
}

static void at91_usart_set_enabled(void *opaque, int n, int level)
{
    AT91USARTState *s = opaque;

    s->flexcom_enabled = level;
    if (!level) {
        timer_del(s->tx_timer);
        timer_del(s->rx_spacing_timer);
        timer_del(s->timeout_timer);
    } else {
        at91_usart_refresh_tiocm(s, true);
        at91_usart_start_tx(s);
        if (!timer_pending(s->tx_timer)) {
            at91_usart_schedule_tx(s);
        }
        if (!timer_pending(s->timeout_timer)) {
            at91_usart_schedule_timeout(s);
        }
        qemu_chr_fe_accept_input(&s->chr);
    }
    at91_usart_update(s);
}

static void at91_usart_set_cts(void *opaque, int n, int level)
{
    AT91USARTState *s = opaque;

    s->cts_gpio_level = !!level;
    if (!s->tiocm_supported) {
        at91_usart_set_cts_level(s, s->cts_gpio_level, true);
    }
}

static void at91_usart_clock_changed(void *opaque, ClockEvent event)
{
    AT91USARTState *s = opaque;

    if (!at91_usart_clocked(s)) {
        timer_del(s->tx_timer);
        timer_del(s->rx_spacing_timer);
        timer_del(s->timeout_timer);
    } else {
        at91_usart_start_tx(s);
        at91_usart_schedule_tx(s);
        at91_usart_schedule_timeout(s);
        at91_usart_update_parameters(s);
        qemu_chr_fe_accept_input(&s->chr);
    }
    at91_usart_update(s);
}

static void at91_usart_reset(DeviceState *dev)
{
    AT91USARTState *s = AT91_USART(dev);

    timer_del(s->tx_timer);
    timer_del(s->rx_spacing_timer);
    timer_del(s->timeout_timer);
    timer_del(s->modem_status_poll);
    s->mode = US_MR_RESET;
    s->interrupt_mask = 0;
    s->status = 0;
    s->baud_generator = 0;
    s->receiver_timeout = 0;
    s->transmitter_timeguard = 0;
    s->fidi = US_FIDI_RESET;
    s->number_errors = 0;
    s->irda_filter = 0;
    s->manchester = US_MAN_RESET;
    s->lin_mode = 0;
    s->lin_identifier = 0;
    s->lin_baud = 0;
    s->lon_mode = 0;
    s->lon_preamble = 0;
    s->lon_data_length = 0;
    s->lon_l2_header = 0;
    s->lon_backlog = 0;
    s->lon_beta1_tx = 0;
    s->lon_beta1_rx = 0;
    s->lon_priority = 0;
    s->lon_idt_tx = 0;
    s->lon_idt_rx = 0;
    s->ic_diff = 0;
    s->comparison = 0;
    s->fifo_mode = 0;
    s->fifo_interrupt_mask = 0;
    s->fifo_event_status = 0;
    s->write_protection = 0;
    s->write_protection_status = 0;
    memset(s->rx_fifo, 0, sizeof(s->rx_fifo));
    memset(s->tx_fifo, 0, sizeof(s->tx_fifo));
    s->tx_shift = 0;
    s->rx_head = 0;
    s->rx_count = 0;
    s->tx_head = 0;
    s->tx_count = 0;
    s->receiver_enabled = false;
    s->transmitter_enabled = false;
    s->fifo_enabled = false;
    s->tx_shift_valid = false;
    s->timeout_running = false;
    s->timeout_waiting = false;
    s->comparison_started = false;
    s->tx_fifo_locked = false;
    s->tx_break = false;
    s->rts_enabled = false;
    s->fifo_rts_level = false;
    s->tx_request_level = false;
    s->rx_request_level = false;
    s->tiocm_supported = false;
    s->tiocm_set_failed = false;
    s->tiocm_rts_valid = false;
    qemu_set_irq(s->tx_request, 0);
    qemu_set_irq(s->rx_request, 0);
    at91_usart_refresh_tiocm(s, false);
    at91_usart_update(s);
}

static int at91_usart_post_load(void *opaque, int version_id)
{
    AT91USARTState *s = opaque;

    if (version_id < 2) {
        /* Version 1 had no independent FRTSC hysteresis latch. */
        s->fifo_rts_level = s->rts_enabled;
    }
    if (version_id < 3) {
        /* Older streams used effective CTS as the named GPIO level. */
        s->cts_gpio_level = s->cts_level;
    }
    if (version_id < 4) {
        timer_del(s->rx_spacing_timer);
    }

    s->mode &= US_MR_MODE_MASK;
    s->interrupt_mask &= US_INT_VALID_MASK;
    s->baud_generator &= US_BRGR_MASK;
    s->receiver_timeout &= US_RTOR_MASK;
    s->transmitter_timeguard &= US_TTGR_MASK;
    s->fidi &= US_FIDI_MASK;
    s->fifo_mode &= US_FMR_MASK;
    s->fifo_interrupt_mask &= US_FIFO_INT_MASK;
    s->fifo_event_status &= US_FIFO_INT_MASK | US_FIFO_STATUS_TXFLOCK;
    s->write_protection &= US_WPMR_MASK;
    s->rx_head %= AT91_USART_FIFO_SIZE;
    s->tx_head %= AT91_USART_FIFO_SIZE;
    s->rx_count = MIN(s->rx_count, AT91_USART_FIFO_SIZE);
    s->tx_count = MIN(s->tx_count, AT91_USART_FIFO_SIZE);
    s->tx_request_level = false;
    s->rx_request_level = false;
    timer_del(s->modem_status_poll);
    s->tiocm_supported = false;
    s->tiocm_set_failed = false;
    s->tiocm_rts_valid = false;
    at91_usart_refresh_tiocm(s, false);
    at91_usart_update(s);
    if (!s->tx_shift_valid) {
        timer_del(s->tx_timer);
    } else if (!timer_pending(s->tx_timer) && at91_usart_clocked(s)) {
        at91_usart_schedule_tx(s);
    }
    if (!s->timeout_running || s->timeout_waiting ||
        !s->receiver_enabled || !s->receiver_timeout) {
        timer_del(s->timeout_timer);
    } else if (!timer_pending(s->timeout_timer) && at91_usart_clocked(s)) {
        at91_usart_schedule_timeout(s);
    }
    if (!s->receiver_enabled || !at91_usart_clocked(s)) {
        timer_del(s->rx_spacing_timer);
    }
    at91_usart_update_parameters(s);
    return 0;
}

static const VMStateDescription at91_usart_vmstate = {
    .name = TYPE_AT91_USART,
    .version_id = 4,
    .minimum_version_id = 1,
    .post_load = at91_usart_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mode, AT91USARTState),
        VMSTATE_UINT32(interrupt_mask, AT91USARTState),
        VMSTATE_UINT32(status, AT91USARTState),
        VMSTATE_UINT32(baud_generator, AT91USARTState),
        VMSTATE_UINT32(receiver_timeout, AT91USARTState),
        VMSTATE_UINT32(transmitter_timeguard, AT91USARTState),
        VMSTATE_UINT32(fidi, AT91USARTState),
        VMSTATE_UINT32(number_errors, AT91USARTState),
        VMSTATE_UINT32(irda_filter, AT91USARTState),
        VMSTATE_UINT32(manchester, AT91USARTState),
        VMSTATE_UINT32(lin_mode, AT91USARTState),
        VMSTATE_UINT32(lin_identifier, AT91USARTState),
        VMSTATE_UINT32(lin_baud, AT91USARTState),
        VMSTATE_UINT32(lon_mode, AT91USARTState),
        VMSTATE_UINT32(lon_preamble, AT91USARTState),
        VMSTATE_UINT32(lon_data_length, AT91USARTState),
        VMSTATE_UINT32(lon_l2_header, AT91USARTState),
        VMSTATE_UINT32(lon_backlog, AT91USARTState),
        VMSTATE_UINT32(lon_beta1_tx, AT91USARTState),
        VMSTATE_UINT32(lon_beta1_rx, AT91USARTState),
        VMSTATE_UINT32(lon_priority, AT91USARTState),
        VMSTATE_UINT32(lon_idt_tx, AT91USARTState),
        VMSTATE_UINT32(lon_idt_rx, AT91USARTState),
        VMSTATE_UINT32(ic_diff, AT91USARTState),
        VMSTATE_UINT32(comparison, AT91USARTState),
        VMSTATE_UINT32(fifo_mode, AT91USARTState),
        VMSTATE_UINT32(fifo_interrupt_mask, AT91USARTState),
        VMSTATE_UINT32(fifo_event_status, AT91USARTState),
        VMSTATE_UINT32(write_protection, AT91USARTState),
        VMSTATE_UINT32(write_protection_status, AT91USARTState),
        VMSTATE_UINT16_ARRAY(rx_fifo, AT91USARTState, AT91_USART_FIFO_SIZE),
        VMSTATE_UINT16_ARRAY(tx_fifo, AT91USARTState, AT91_USART_FIFO_SIZE),
        VMSTATE_UINT16(tx_shift, AT91USARTState),
        VMSTATE_UINT8(rx_head, AT91USARTState),
        VMSTATE_UINT8(rx_count, AT91USARTState),
        VMSTATE_UINT8(tx_head, AT91USARTState),
        VMSTATE_UINT8(tx_count, AT91USARTState),
        VMSTATE_BOOL(flexcom_enabled, AT91USARTState),
        VMSTATE_BOOL(receiver_enabled, AT91USARTState),
        VMSTATE_BOOL(transmitter_enabled, AT91USARTState),
        VMSTATE_BOOL(fifo_enabled, AT91USARTState),
        VMSTATE_BOOL(tx_shift_valid, AT91USARTState),
        VMSTATE_BOOL(timeout_running, AT91USARTState),
        VMSTATE_BOOL(timeout_waiting, AT91USARTState),
        VMSTATE_BOOL(comparison_started, AT91USARTState),
        VMSTATE_BOOL(tx_fifo_locked, AT91USARTState),
        VMSTATE_BOOL(tx_break, AT91USARTState),
        VMSTATE_BOOL(cts_level, AT91USARTState),
        VMSTATE_BOOL_V(cts_gpio_level, AT91USARTState, 3),
        VMSTATE_BOOL(rts_enabled, AT91USARTState),
        VMSTATE_BOOL_V(fifo_rts_level, AT91USARTState, 2),
        VMSTATE_CLOCK(pclk, AT91USARTState),
        VMSTATE_CLOCK(gclk, AT91USARTState),
        VMSTATE_TIMER_PTR(tx_timer, AT91USARTState),
        VMSTATE_TIMER_PTR_V(rx_spacing_timer, AT91USARTState, 4),
        VMSTATE_TIMER_PTR(timeout_timer, AT91USARTState),
        VMSTATE_END_OF_LIST()
    },
};

static int at91_usart_be_change(void *opaque)
{
    AT91USARTState *s = opaque;
    int break_enable = s->tx_break;

    qemu_chr_fe_set_handlers(&s->chr, at91_usart_can_receive,
                             at91_usart_receive, at91_usart_event,
                             at91_usart_be_change, s, NULL, true);
    at91_usart_update_parameters(s);
    qemu_chr_fe_ioctl(&s->chr, CHR_IOCTL_SERIAL_SET_BREAK, &break_enable);

    timer_del(s->modem_status_poll);
    s->tiocm_supported = false;
    s->tiocm_set_failed = false;
    s->tiocm_rts_valid = false;
    at91_usart_refresh_tiocm(s, true);
    at91_usart_update(s);
    return 0;
}

static void at91_usart_realize(DeviceState *dev, Error **errp)
{
    AT91USARTState *s = AT91_USART(dev);

    if (!clock_has_source(s->pclk) || !clock_has_source(s->gclk)) {
        error_setg(errp, TYPE_AT91_USART
                   ": pclk and gclk must be connected");
        return;
    }
    qemu_chr_fe_set_handlers(&s->chr, at91_usart_can_receive,
                             at91_usart_receive, at91_usart_event,
                             at91_usart_be_change, s, NULL, true);
    at91_usart_refresh_tiocm(s, false);
}

static void at91_usart_init(Object *obj)
{
    AT91USARTState *s = AT91_USART(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_usart_ops, s,
                          TYPE_AT91_USART, US_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rts, "rts", 1);
    qdev_init_gpio_in_named(dev, at91_usart_set_enabled,
                            "flexcom-enabled", 1);
    qdev_init_gpio_in_named(dev, at91_usart_set_cts, "cts", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_usart_clock_changed,
                                 s, ClockUpdate);
    s->gclk = qdev_init_clock_in(dev, "gclk", at91_usart_clock_changed,
                                 s, ClockUpdate);
    s->tx_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                               at91_usart_tx_complete, s);
    s->rx_spacing_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       at91_usart_rx_spacing_elapsed, s);
    s->timeout_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    at91_usart_timeout, s);
    s->modem_status_poll = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                        at91_usart_modem_status_poll, s);
}

static void at91_usart_finalize(Object *obj)
{
    AT91USARTState *s = AT91_USART(obj);

    timer_free(s->tx_timer);
    timer_free(s->timeout_timer);
    timer_free(s->modem_status_poll);
}

static const Property at91_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", AT91USARTState, chr),
};

static void at91_usart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 FLEXCOM USART";
    dc->realize = at91_usart_realize;
    dc->vmsd = &at91_usart_vmstate;
    device_class_set_legacy_reset(dc, at91_usart_reset);
    device_class_set_props(dc, at91_usart_properties);
}

static const TypeInfo at91_usart_info = {
    .name = TYPE_AT91_USART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91USARTState),
    .instance_init = at91_usart_init,
    .instance_finalize = at91_usart_finalize,
    .class_init = at91_usart_class_init,
};

static void at91_usart_register_types(void)
{
    type_register_static(&at91_usart_info);
}

type_init(at91_usart_register_types)
