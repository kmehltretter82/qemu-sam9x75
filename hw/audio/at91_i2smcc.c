/*
 * Microchip AT91 I2S Multi-Channel Controller
 *
 * This models the single-data-pair, non-FIFO I2SMCC implemented by
 * SAM9X60 and SAM9X7.  An unconnected receive data pin supplies zeroes;
 * internal loopback provides a deterministic full-duplex data path.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/audio/at91_i2smcc.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define I2SMCC_CR                  0x00
#define I2SMCC_MRA                 0x04
#define I2SMCC_MRB                 0x08
#define I2SMCC_SR                  0x0c
#define I2SMCC_IERA                0x10
#define I2SMCC_IDRA                0x14
#define I2SMCC_IMRA                0x18
#define I2SMCC_ISRA                0x1c
#define I2SMCC_IERB                0x20
#define I2SMCC_IDRB                0x24
#define I2SMCC_IMRB                0x28
#define I2SMCC_ISRB                0x2c
#define I2SMCC_RHR                 0x30
#define I2SMCC_THR                 0x34
#define I2SMCC_WPMR                0xe4
#define I2SMCC_WPSR                0xe8
#define I2SMCC_VERSION             0xfc
#define I2SMCC_MMIO_SIZE           0x100

#define I2SMCC_CR_RXEN             BIT(0)
#define I2SMCC_CR_RXDIS            BIT(1)
#define I2SMCC_CR_CKEN             BIT(2)
#define I2SMCC_CR_CKDIS            BIT(3)
#define I2SMCC_CR_TXEN             BIT(4)
#define I2SMCC_CR_TXDIS            BIT(5)
#define I2SMCC_CR_SWRST            BIT(7)
#define I2SMCC_CR_MASK             (I2SMCC_CR_RXEN | I2SMCC_CR_RXDIS | \
                                    I2SMCC_CR_CKEN | I2SMCC_CR_CKDIS | \
                                    I2SMCC_CR_TXEN | I2SMCC_CR_TXDIS | \
                                    I2SMCC_CR_SWRST)

#define I2SMCC_MRA_RXMONO          BIT(8)
#define I2SMCC_MRA_RXLOOP          BIT(9)
#define I2SMCC_MRA_TXMONO          BIT(10)
#define I2SMCC_MRA_TXSAME          BIT(11)
#define I2SMCC_MRA_SRCCLK          BIT(12)
#define I2SMCC_MRA_IWS             BIT(31)
#define I2SMCC_MRA_MASK            0xffffffcf

#define I2SMCC_FORMAT_I2S          0
#define I2SMCC_FORMAT_LJ           1
#define I2SMCC_FORMAT_TDM          2
#define I2SMCC_FORMAT_TDMLJ        3
#define I2SMCC_DATALENGTH_32       0
#define I2SMCC_DATALENGTH_24       1
#define I2SMCC_DATALENGTH_20       2
#define I2SMCC_DATALENGTH_18       3
#define I2SMCC_DATALENGTH_16       4
#define I2SMCC_DATALENGTH_16C      5
#define I2SMCC_DATALENGTH_8        6
#define I2SMCC_DATALENGTH_8C       7

#define I2SMCC_MRB_MASK            (3U << 8)

#define I2SMCC_SR_RXEN             BIT(0)
#define I2SMCC_SR_TXEN             BIT(4)

#define I2SMCC_INT_TXRDY_MASK      0x00000003
#define I2SMCC_INT_TXUNF_MASK      0x00000300
#define I2SMCC_INT_RXRDY_MASK      0x00030000
#define I2SMCC_INT_RXOVF_MASK      0x03000000
#define I2SMCC_INT_A_MASK          (I2SMCC_INT_TXRDY_MASK | \
                                    I2SMCC_INT_TXUNF_MASK | \
                                    I2SMCC_INT_RXRDY_MASK | \
                                    I2SMCC_INT_RXOVF_MASK)
#define I2SMCC_INT_WERR            BIT(0)

#define I2SMCC_WPMR_KEY_MASK       0xffffff00
#define I2SMCC_WPMR_KEY            0x49325300
#define I2SMCC_WPMR_WPCFEN         BIT(0)
#define I2SMCC_WPMR_WPITEN         BIT(1)
#define I2SMCC_WPMR_WPCTEN         BIT(2)
#define I2SMCC_WPMR_MASK           0x00000007
#define I2SMCC_WPSR_WPVS           BIT(0)

static unsigned int at91_i2smcc_format(const AT91I2SMCCState *s)
{
    return extract32(s->mra, 6, 2);
}

static unsigned int at91_i2smcc_data_length(const AT91I2SMCCState *s)
{
    return extract32(s->mra, 1, 3);
}

static bool at91_i2smcc_compact(const AT91I2SMCCState *s)
{
    unsigned int length = at91_i2smcc_data_length(s);

    return length == I2SMCC_DATALENGTH_16C ||
           length == I2SMCC_DATALENGTH_8C;
}

static uint32_t at91_i2smcc_sample_mask(const AT91I2SMCCState *s)
{
    switch (at91_i2smcc_data_length(s)) {
    case I2SMCC_DATALENGTH_24:
        return MAKE_64BIT_MASK(0, 24);
    case I2SMCC_DATALENGTH_20:
        return MAKE_64BIT_MASK(0, 20);
    case I2SMCC_DATALENGTH_18:
        return MAKE_64BIT_MASK(0, 18);
    case I2SMCC_DATALENGTH_16:
        return MAKE_64BIT_MASK(0, 16);
    case I2SMCC_DATALENGTH_8:
        return MAKE_64BIT_MASK(0, 8);
    case I2SMCC_DATALENGTH_8C:
        return MAKE_64BIT_MASK(0, 16);
    case I2SMCC_DATALENGTH_32:
    case I2SMCC_DATALENGTH_16C:
    default:
        return UINT32_MAX;
    }
}

static unsigned int at91_i2smcc_channels(const AT91I2SMCCState *s)
{
    if (at91_i2smcc_format(s) == I2SMCC_FORMAT_TDM ||
        at91_i2smcc_format(s) == I2SMCC_FORMAT_TDMLJ) {
        return extract32(s->mra, 13, 3) + 1;
    }
    return 2;
}

static uint32_t at91_i2smcc_active_mask(const AT91I2SMCCState *s,
                                        bool transmit)
{
    unsigned int channels = at91_i2smcc_channels(s);
    uint32_t mask = MAKE_64BIT_MASK(0, channels);

    if (at91_i2smcc_compact(s)) {
        return BIT(0);
    }
    if (transmit && (s->mra & I2SMCC_MRA_TXMONO)) {
        mask &= 0x55;
    }
    return mask;
}

static uint32_t at91_i2smcc_valid_mask(const bool *valid)
{
    uint32_t mask = 0;
    unsigned int i;

    for (i = 0; i < AT91_I2SMCC_MAX_CHANNELS; i++) {
        if (valid[i]) {
            mask |= BIT(i);
        }
    }
    return mask;
}

static unsigned int at91_i2smcc_last_channel(uint32_t mask)
{
    return 31 - clz32(mask);
}

static void at91_i2smcc_update_ready(AT91I2SMCCState *s)
{
    uint32_t tx_active = at91_i2smcc_active_mask(s, true);
    uint32_t rx_active = at91_i2smcc_active_mask(s, false);
    uint32_t tx_valid = at91_i2smcc_valid_mask(s->tx_valid);
    uint32_t rx_valid = at91_i2smcc_valid_mask(s->rx_valid);
    unsigned int last;

    s->isra &= ~(I2SMCC_INT_TXRDY_MASK | I2SMCC_INT_RXRDY_MASK);

    if (is_power_of_2(tx_active) || at91_i2smcc_compact(s) ||
        (s->mra & I2SMCC_MRA_TXMONO)) {
        if (tx_active & ~tx_valid) {
            s->isra |= BIT(0);
        }
    } else {
        last = at91_i2smcc_last_channel(tx_active);
        if ((tx_active & ~BIT(last)) & ~tx_valid) {
            s->isra |= BIT(0);
        }
        if (!(tx_valid & BIT(last))) {
            s->isra |= BIT(1);
        }
    }

    if (is_power_of_2(rx_active) || at91_i2smcc_compact(s)) {
        if (rx_active & rx_valid) {
            s->isra |= BIT(16);
        }
    } else {
        last = at91_i2smcc_last_channel(rx_active);
        if ((rx_active & ~BIT(last)) & rx_valid) {
            s->isra |= BIT(16);
        }
        if (rx_valid & BIT(last)) {
            s->isra |= BIT(17);
        }
    }
}

static uint8_t at91_i2smcc_next_channel(uint8_t channel, uint32_t mask)
{
    unsigned int i;

    for (i = 1; i <= AT91_I2SMCC_MAX_CHANNELS; i++) {
        unsigned int next = (channel + i) % AT91_I2SMCC_MAX_CHANNELS;

        if (mask & BIT(next)) {
            return next;
        }
    }
    return 0;
}

static uint64_t at91_i2smcc_selected_hz(const AT91I2SMCCState *s)
{
    return clock_get_hz(s->mra & I2SMCC_MRA_SRCCLK ? s->gclk : s->pclk);
}

static uint64_t at91_i2smcc_word_period_ns(const AT91I2SMCCState *s)
{
    unsigned int length = at91_i2smcc_data_length(s);
    unsigned int format = at91_i2smcc_format(s);
    unsigned int divider = extract32(s->mra, 24, 6);
    uint64_t source_hz = at91_i2smcc_selected_hz(s);
    uint64_t cycles;

    if (!source_hz) {
        return 0;
    }

    if (format == I2SMCC_FORMAT_TDM || format == I2SMCC_FORMAT_TDMLJ) {
        cycles = 32;
    } else {
        switch (length) {
        case I2SMCC_DATALENGTH_24:
        case I2SMCC_DATALENGTH_20:
        case I2SMCC_DATALENGTH_18:
            cycles = s->mra & I2SMCC_MRA_IWS ? 24 : 32;
            break;
        case I2SMCC_DATALENGTH_16:
        case I2SMCC_DATALENGTH_16C:
            cycles = 16;
            break;
        case I2SMCC_DATALENGTH_8:
        case I2SMCC_DATALENGTH_8C:
            cycles = 8;
            break;
        case I2SMCC_DATALENGTH_32:
        default:
            cycles = 32;
            break;
        }
    }

    if (divider) {
        cycles *= 2 * divider;
    }
    return MAX(1ULL, DIV_ROUND_UP(cycles * NANOSECONDS_PER_SECOND,
                                  source_hz));
}

static bool at91_i2smcc_clock_active(const AT91I2SMCCState *s)
{
    return s->clocks_enabled && at91_i2smcc_selected_hz(s);
}

static void at91_i2smcc_update_irq(AT91I2SMCCState *s)
{
    bool level = (s->isra & s->imra) || (s->isrb & s->imrb);

    qemu_set_irq(s->irq, level && clock_get_hz(s->pclk));
}

static void at91_i2smcc_update_requests(AT91I2SMCCState *s)
{
    bool active = at91_i2smcc_clock_active(s) && clock_get_hz(s->pclk);
    bool tx_level = active && s->tx_enabled &&
                    (s->isra & I2SMCC_INT_TXRDY_MASK);
    bool rx_level = active && s->rx_enabled &&
                    (s->isra & I2SMCC_INT_RXRDY_MASK);

    if (tx_level != s->tx_request_level) {
        s->tx_request_level = tx_level;
        qemu_set_irq(s->tx_request, tx_level);
    }
    if (rx_level != s->rx_request_level) {
        s->rx_request_level = rx_level;
        qemu_set_irq(s->rx_request, rx_level);
    }
}

static void at91_i2smcc_schedule(AT91I2SMCCState *s)
{
    uint64_t period;

    if (!at91_i2smcc_clock_active(s) ||
        (!s->tx_enabled && !s->rx_enabled) ||
        timer_pending(s->word_timer)) {
        return;
    }

    period = at91_i2smcc_word_period_ns(s);
    if (period) {
        timer_mod(s->word_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  period);
    }
}

static void at91_i2smcc_refresh(AT91I2SMCCState *s)
{
    bool active = at91_i2smcc_clock_active(s);

    s->sr = (active && s->rx_enabled ? I2SMCC_SR_RXEN : 0) |
            (active && s->tx_enabled ? I2SMCC_SR_TXEN : 0);
    if (!active || (!s->tx_enabled && !s->rx_enabled)) {
        timer_del(s->word_timer);
    }
    at91_i2smcc_update_irq(s);
    at91_i2smcc_update_requests(s);
    at91_i2smcc_schedule(s);
}

static void at91_i2smcc_receive(AT91I2SMCCState *s, unsigned int channel,
                                uint32_t sample)
{
    uint32_t active = at91_i2smcc_active_mask(s, false);

    if (!(active & BIT(channel))) {
        return;
    }
    if (s->rx_valid[channel]) {
        s->isra |= BIT(channel & 1 ? 25 : 24);
    }
    s->rx_holding[channel] = sample;
    s->rx_valid[channel] = true;
    at91_i2smcc_update_ready(s);
}

static void at91_i2smcc_word_done(void *opaque)
{
    AT91I2SMCCState *s = opaque;
    unsigned int channels = at91_i2smcc_channels(s);
    unsigned int channel = s->stream_channel % channels;
    uint32_t tx_active = at91_i2smcc_active_mask(s, true);
    bool duplicated = (s->mra & I2SMCC_MRA_TXMONO) ||
                      at91_i2smcc_compact(s);
    uint32_t sample = 0;

    if (s->tx_enabled && (tx_active & BIT(channel))) {
        if (s->tx_valid[channel]) {
            sample = s->tx_holding[channel];
            s->tx_previous[channel] = sample;
            s->tx_valid[channel] = false;
        } else {
            s->isra |= BIT(channel & 1 ? 9 : 8);
            s->tx_discard_mask |= BIT(channel);
            if (duplicated && !(channel & 1) && channel + 1 < channels) {
                s->tx_right_underrun_mask |= BIT(channel + 1);
            }
            if (s->mra & I2SMCC_MRA_TXSAME) {
                sample = s->tx_previous[channel];
            }
        }
        at91_i2smcc_update_ready(s);
    } else if (s->tx_enabled && duplicated && (channel & 1)) {
        if (s->tx_right_underrun_mask & BIT(channel)) {
            s->isra |= BIT(9);
            s->tx_right_underrun_mask &= ~BIT(channel);
        }
        if (s->mra & I2SMCC_MRA_TXMONO) {
            sample = s->tx_previous[channel - 1];
        }
    }

    if (s->rx_enabled) {
        if (!(s->mra & I2SMCC_MRA_RXLOOP)) {
            sample = 0;
        } else if ((s->mra & I2SMCC_MRA_RXMONO) && (channel & 1)) {
            sample = s->rx_holding[channel - 1];
        }
        at91_i2smcc_receive(s, channel, sample);
    }

    s->stream_channel = (channel + 1) % channels;
    at91_i2smcc_update_irq(s);
    at91_i2smcc_update_requests(s);
    at91_i2smcc_schedule(s);
}

static void at91_i2smcc_reset_registers(AT91I2SMCCState *s, bool hardware)
{
    timer_del(s->word_timer);
    s->mra = 0;
    s->mrb = 0;
    s->sr = 0;
    s->imra = 0;
    s->isra = I2SMCC_INT_TXRDY_MASK;
    s->imrb = 0;
    s->isrb = 0;
    s->rhr = 0;
    s->thr = 0;
    memset(s->tx_holding, 0, sizeof(s->tx_holding));
    memset(s->tx_previous, 0, sizeof(s->tx_previous));
    memset(s->rx_holding, 0, sizeof(s->rx_holding));
    memset(s->tx_valid, 0, sizeof(s->tx_valid));
    memset(s->rx_valid, 0, sizeof(s->rx_valid));
    s->tx_write_channel = 0;
    s->rx_read_channel = 0;
    s->stream_channel = 0;
    s->tx_discard_mask = 0;
    s->tx_right_underrun_mask = 0;
    s->clocks_enabled = false;
    s->tx_enabled = false;
    s->rx_enabled = false;
    s->wpsr = 0;
    if (hardware) {
        s->wpmr = 0;
    }
    at91_i2smcc_update_irq(s);
    at91_i2smcc_update_requests(s);
}

static void at91_i2smcc_violation(AT91I2SMCCState *s, hwaddr offset)
{
    s->wpsr = (offset << 8) | I2SMCC_WPSR_WPVS;
    s->isrb |= I2SMCC_INT_WERR;
    at91_i2smcc_update_irq(s);
}

static uint64_t at91_i2smcc_read_rhr(AT91I2SMCCState *s)
{
    uint32_t active = at91_i2smcc_active_mask(s, false);
    unsigned int channel = s->rx_read_channel;

    if ((active & BIT(channel)) && s->rx_valid[channel]) {
        s->rhr = s->rx_holding[channel];
        s->rx_valid[channel] = false;
        s->rx_read_channel = at91_i2smcc_next_channel(channel, active);
        at91_i2smcc_update_ready(s);
        at91_i2smcc_update_irq(s);
        at91_i2smcc_update_requests(s);
    }
    return s->rhr;
}

static uint64_t at91_i2smcc_read(void *opaque, hwaddr offset,
                                 unsigned int size)
{
    AT91I2SMCCState *s = AT91_I2SMCC(opaque);
    uint32_t value;

    switch (offset) {
    case I2SMCC_MRA:
        return s->mra;
    case I2SMCC_MRB:
        return s->mrb;
    case I2SMCC_SR:
        return s->sr;
    case I2SMCC_IMRA:
        return s->imra;
    case I2SMCC_ISRA:
        value = s->isra;
        s->isra &= ~(I2SMCC_INT_TXUNF_MASK | I2SMCC_INT_RXOVF_MASK);
        at91_i2smcc_update_irq(s);
        return value;
    case I2SMCC_IMRB:
        return s->imrb;
    case I2SMCC_ISRB:
        value = s->isrb;
        s->isrb &= ~I2SMCC_INT_WERR;
        at91_i2smcc_update_irq(s);
        return value;
    case I2SMCC_RHR:
        return at91_i2smcc_read_rhr(s);
    case I2SMCC_WPMR:
        return s->wpmr;
    case I2SMCC_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case I2SMCC_VERSION:
        return s->version;
    case I2SMCC_CR:
    case I2SMCC_IERA:
    case I2SMCC_IDRA:
    case I2SMCC_IERB:
    case I2SMCC_IDRB:
    case I2SMCC_THR:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_I2SMCC ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_i2smcc_write_cr(AT91I2SMCCState *s, uint32_t value)
{
    uint32_t rx_active;
    unsigned int i;

    value &= I2SMCC_CR_MASK;
    if (value & I2SMCC_CR_SWRST) {
        at91_i2smcc_reset_registers(s, false);
        return;
    }

    if (value & I2SMCC_CR_CKDIS) {
        s->clocks_enabled = false;
    } else if (value & I2SMCC_CR_CKEN) {
        s->clocks_enabled = true;
    }

    if (value & I2SMCC_CR_TXDIS) {
        memset(s->tx_valid, 0, sizeof(s->tx_valid));
        s->tx_discard_mask = 0;
        s->tx_right_underrun_mask = 0;
        s->tx_enabled = false;
    } else if (value & I2SMCC_CR_TXEN) {
        if (!s->tx_enabled) {
            s->tx_write_channel = 0;
        }
        s->tx_enabled = true;
    }

    rx_active = at91_i2smcc_active_mask(s, false);
    if (value & I2SMCC_CR_RXDIS) {
        for (i = 0; i < AT91_I2SMCC_MAX_CHANNELS; i++) {
            if (rx_active & BIT(i)) {
                s->rx_holding[i] = 0;
                s->rx_valid[i] = true;
            }
        }
        s->rx_enabled = false;
    } else if (value & I2SMCC_CR_RXEN) {
        s->rx_enabled = true;
        s->rx_read_channel = 0;
    }
    at91_i2smcc_update_ready(s);
    at91_i2smcc_refresh(s);
}

static void at91_i2smcc_write_thr(AT91I2SMCCState *s, uint32_t value)
{
    uint32_t active = at91_i2smcc_active_mask(s, true);
    unsigned int channel = s->tx_write_channel;

    s->thr = value;
    if (!(active & BIT(channel)) || s->tx_valid[channel]) {
        return;
    }
    if (s->tx_discard_mask & BIT(channel)) {
        s->tx_discard_mask &= ~BIT(channel);
        s->tx_write_channel = at91_i2smcc_next_channel(channel, active);
        at91_i2smcc_update_ready(s);
        at91_i2smcc_update_irq(s);
        at91_i2smcc_update_requests(s);
        return;
    }
    s->tx_holding[channel] = value & at91_i2smcc_sample_mask(s);
    s->tx_valid[channel] = true;
    s->tx_write_channel = at91_i2smcc_next_channel(channel, active);
    at91_i2smcc_update_ready(s);
    at91_i2smcc_update_irq(s);
    at91_i2smcc_update_requests(s);
}

static void at91_i2smcc_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned int size)
{
    AT91I2SMCCState *s = AT91_I2SMCC(opaque);
    uint32_t val = value;

    switch (offset) {
    case I2SMCC_CR:
        if (s->wpmr & I2SMCC_WPMR_WPCTEN) {
            at91_i2smcc_violation(s, offset);
        } else {
            at91_i2smcc_write_cr(s, val);
        }
        break;
    case I2SMCC_MRA:
    case I2SMCC_MRB:
        if (s->wpmr & I2SMCC_WPMR_WPCFEN) {
            at91_i2smcc_violation(s, offset);
            break;
        }
        if (offset == I2SMCC_MRA) {
            s->mra = val & I2SMCC_MRA_MASK;
            s->tx_write_channel = 0;
            s->rx_read_channel = 0;
            s->stream_channel = 0;
            at91_i2smcc_update_ready(s);
        } else {
            s->mrb = val & I2SMCC_MRB_MASK;
        }
        at91_i2smcc_refresh(s);
        break;
    case I2SMCC_IERA:
    case I2SMCC_IDRA:
    case I2SMCC_IERB:
    case I2SMCC_IDRB:
        if (s->wpmr & I2SMCC_WPMR_WPITEN) {
            at91_i2smcc_violation(s, offset);
            break;
        }
        if (offset == I2SMCC_IERA) {
            s->imra |= val & I2SMCC_INT_A_MASK;
        } else if (offset == I2SMCC_IDRA) {
            s->imra &= ~(val & I2SMCC_INT_A_MASK);
        } else if (offset == I2SMCC_IERB) {
            s->imrb |= val & I2SMCC_INT_WERR;
        } else {
            s->imrb &= ~(val & I2SMCC_INT_WERR);
        }
        at91_i2smcc_update_irq(s);
        break;
    case I2SMCC_THR:
        at91_i2smcc_write_thr(s, val);
        break;
    case I2SMCC_WPMR:
        if ((val & I2SMCC_WPMR_KEY_MASK) == I2SMCC_WPMR_KEY) {
            s->wpmr = val & I2SMCC_WPMR_MASK;
        }
        break;
    case I2SMCC_SR:
    case I2SMCC_IMRA:
    case I2SMCC_ISRA:
    case I2SMCC_IMRB:
    case I2SMCC_ISRB:
    case I2SMCC_RHR:
    case I2SMCC_WPSR:
    case I2SMCC_VERSION:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_I2SMCC ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_I2SMCC ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_i2smcc_ops = {
    .read = at91_i2smcc_read,
    .write = at91_i2smcc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_i2smcc_clock_changed(void *opaque, ClockEvent event)
{
    AT91I2SMCCState *s = opaque;

    timer_del(s->word_timer);
    at91_i2smcc_refresh(s);
}

static void at91_i2smcc_reset(DeviceState *dev)
{
    at91_i2smcc_reset_registers(AT91_I2SMCC(dev), true);
}

static void at91_i2smcc_init(Object *obj)
{
    AT91I2SMCCState *s = AT91_I2SMCC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &at91_i2smcc_ops, s,
                          TYPE_AT91_I2SMCC, I2SMCC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_i2smcc_clock_changed,
                                 s, ClockUpdate);
    s->gclk = qdev_init_clock_in(dev, "gclk", at91_i2smcc_clock_changed,
                                 s, ClockUpdate);
    s->word_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                 at91_i2smcc_word_done, s);
}

static void at91_i2smcc_finalize(Object *obj)
{
    AT91I2SMCCState *s = AT91_I2SMCC(obj);

    timer_free(s->word_timer);
}

static void at91_i2smcc_realize(DeviceState *dev, Error **errp)
{
    AT91I2SMCCState *s = AT91_I2SMCC(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_I2SMCC ": pclk must be connected");
        return;
    }
    if (!clock_has_source(s->gclk)) {
        error_setg(errp, TYPE_AT91_I2SMCC ": gclk must be connected");
    }
}

static int at91_i2smcc_post_load(void *opaque, int version_id)
{
    AT91I2SMCCState *s = opaque;

    s->mra &= I2SMCC_MRA_MASK;
    s->mrb &= I2SMCC_MRB_MASK;
    s->imra &= I2SMCC_INT_A_MASK;
    s->isra &= I2SMCC_INT_A_MASK;
    s->imrb &= I2SMCC_INT_WERR;
    s->isrb &= I2SMCC_INT_WERR;
    s->wpmr &= I2SMCC_WPMR_MASK;
    s->wpsr &= 0xffffff01;
    s->tx_write_channel %= AT91_I2SMCC_MAX_CHANNELS;
    s->rx_read_channel %= AT91_I2SMCC_MAX_CHANNELS;
    s->stream_channel %= AT91_I2SMCC_MAX_CHANNELS;
    s->tx_discard_mask &= at91_i2smcc_active_mask(s, true);
    s->tx_right_underrun_mask &=
        0xaa & MAKE_64BIT_MASK(0, at91_i2smcc_channels(s));
    qemu_set_irq(s->tx_request, 0);
    qemu_set_irq(s->rx_request, 0);
    s->tx_request_level = false;
    s->rx_request_level = false;
    at91_i2smcc_update_ready(s);
    at91_i2smcc_refresh(s);
    return 0;
}

static const VMStateDescription vmstate_at91_i2smcc = {
    .name = TYPE_AT91_I2SMCC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_i2smcc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mra, AT91I2SMCCState),
        VMSTATE_UINT32(mrb, AT91I2SMCCState),
        VMSTATE_UINT32(imra, AT91I2SMCCState),
        VMSTATE_UINT32(isra, AT91I2SMCCState),
        VMSTATE_UINT32(imrb, AT91I2SMCCState),
        VMSTATE_UINT32(isrb, AT91I2SMCCState),
        VMSTATE_UINT32(rhr, AT91I2SMCCState),
        VMSTATE_UINT32(thr, AT91I2SMCCState),
        VMSTATE_UINT32(wpmr, AT91I2SMCCState),
        VMSTATE_UINT32(wpsr, AT91I2SMCCState),
        VMSTATE_UINT32_ARRAY(tx_holding, AT91I2SMCCState,
                             AT91_I2SMCC_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(tx_previous, AT91I2SMCCState,
                             AT91_I2SMCC_MAX_CHANNELS),
        VMSTATE_UINT32_ARRAY(rx_holding, AT91I2SMCCState,
                             AT91_I2SMCC_MAX_CHANNELS),
        VMSTATE_BOOL_ARRAY(tx_valid, AT91I2SMCCState,
                           AT91_I2SMCC_MAX_CHANNELS),
        VMSTATE_BOOL_ARRAY(rx_valid, AT91I2SMCCState,
                           AT91_I2SMCC_MAX_CHANNELS),
        VMSTATE_UINT8(tx_write_channel, AT91I2SMCCState),
        VMSTATE_UINT8(rx_read_channel, AT91I2SMCCState),
        VMSTATE_UINT8(stream_channel, AT91I2SMCCState),
        VMSTATE_UINT8(tx_discard_mask, AT91I2SMCCState),
        VMSTATE_UINT8(tx_right_underrun_mask, AT91I2SMCCState),
        VMSTATE_BOOL(clocks_enabled, AT91I2SMCCState),
        VMSTATE_BOOL(tx_enabled, AT91I2SMCCState),
        VMSTATE_BOOL(rx_enabled, AT91I2SMCCState),
        VMSTATE_TIMER_PTR(word_timer, AT91I2SMCCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_i2smcc_properties[] = {
    DEFINE_PROP_UINT32("version", AT91I2SMCCState, version, 0x100),
};

static void at91_i2smcc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 I2S Multi-Channel Controller";
    dc->realize = at91_i2smcc_realize;
    dc->vmsd = &vmstate_at91_i2smcc;
    device_class_set_props(dc, at91_i2smcc_properties);
    device_class_set_legacy_reset(dc, at91_i2smcc_reset);
}

static const TypeInfo at91_i2smcc_info = {
    .name = TYPE_AT91_I2SMCC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91I2SMCCState),
    .instance_init = at91_i2smcc_init,
    .instance_finalize = at91_i2smcc_finalize,
    .class_init = at91_i2smcc_class_init,
};

static void at91_i2smcc_register_types(void)
{
    type_register_static(&at91_i2smcc_info);
}

type_init(at91_i2smcc_register_types)
