/*
 * Microchip SAM9X7 Analog-to-Digital Converter
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/adc/at91_adc.h"
#include "hw/core/qdev-clock.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"

#define ADC_CR                  0x00
#define ADC_MR                  0x04
#define ADC_SEQR1               0x08
#define ADC_CHER                0x10
#define ADC_CHDR                0x14
#define ADC_CHSR                0x18
#define ADC_LCDR                0x20
#define ADC_IER                 0x24
#define ADC_IDR                 0x28
#define ADC_IMR                 0x2c
#define ADC_ISR                 0x30
#define ADC_LCTMR               0x34
#define ADC_LCCWR               0x38
#define ADC_OVER                0x3c
#define ADC_EMR                 0x40
#define ADC_CWR                 0x44
#define ADC_CCR                 0x4c
#define ADC_CDR_FIRST           0x50
#define ADC_CDR_LAST            0x6c
#define ADC_ACR                 0x94
#define ADC_PDR                 0xa0
#define ADC_TSMR                0xb0
#define ADC_XPOSR               0xb4
#define ADC_YPOSR               0xb8
#define ADC_PRESSR              0xbc
#define ADC_TRGR                0xc0
#define ADC_CVR                 0xd4
#define ADC_CECR                0xd8
#define ADC_TSCVR               0xdc
#define ADC_WPMR                0xe4
#define ADC_WPSR                0xe8
#define ADC_MMIO_SIZE           0x100

#define ADC_CR_SWRST            BIT(0)
#define ADC_CR_START            BIT(1)
#define ADC_CR_CMPRST           BIT(4)

#define ADC_MR_RESET            0x20000000
#define ADC_MR_PRESCAL_SHIFT    8
#define ADC_MR_PRESCAL_MASK     (0xffU << ADC_MR_PRESCAL_SHIFT)
#define ADC_MR_STARTUP_SHIFT    16
#define ADC_MR_STARTUP_MASK     (0xfU << ADC_MR_STARTUP_SHIFT)
#define ADC_MR_TRACKTIM_SHIFT   24
#define ADC_MR_TRACKTIM_MASK    (0xfU << ADC_MR_TRACKTIM_SHIFT)
#define ADC_MR_USEQ             BIT(31)
#define ADC_MR_MASK             0xff8fff6e

#define ADC_CHANNEL_MASK        0x000000ff

#define ADC_INT_EOC_MASK        ADC_CHANNEL_MASK
#define ADC_INT_LCCHG           BIT(19)
#define ADC_INT_XRDY            BIT(20)
#define ADC_INT_YRDY            BIT(21)
#define ADC_INT_PRDY            BIT(22)
#define ADC_INT_DRDY            BIT(24)
#define ADC_INT_GOVRE           BIT(25)
#define ADC_INT_COMPE           BIT(26)
#define ADC_INT_PEN             BIT(29)
#define ADC_INT_NOPEN           BIT(30)
#define ADC_INT_PENS            BIT(31)
#define ADC_INT_MASK            0x677800ff
#define ADC_ISR_MASK            0xe77800ff
#define ADC_ISR_READ_CLEAR      (ADC_INT_LCCHG | ADC_INT_XRDY | \
                                 ADC_INT_YRDY | ADC_INT_PRDY | \
                                 ADC_INT_GOVRE | ADC_INT_COMPE | \
                                 ADC_INT_PEN | ADC_INT_NOPEN)

#define ADC_LCTMR_MASK          0x00000031
#define ADC_WINDOW_12_MASK      0x0fff0fff

#define ADC_EMR_CMPMODE_MASK    0x00000003
#define ADC_EMR_CMPTYPE         BIT(2)
#define ADC_EMR_CMPSEL_SHIFT    4
#define ADC_EMR_CMPALL          BIT(9)
#define ADC_EMR_CMPFILTER_SHIFT 12
#define ADC_EMR_OSR_SHIFT       16
#define ADC_EMR_OSR_MASK        (7U << ADC_EMR_OSR_SHIFT)
#define ADC_EMR_ASTE            BIT(20)
#define ADC_EMR_SRCCLK          BIT(21)
#define ADC_EMR_TRACKX4         BIT(22)
#define ADC_EMR_TAG             BIT(24)
#define ADC_EMR_MASK            0x377732f7

#define ADC_CCR_MASK            0x00ff0000
#define ADC_ACR_FIXED           BIT(12)
#define ADC_ACR_RESET           0x00001200
#define ADC_ACR_WRITABLE_MASK   0x00000303
#define ADC_PDR_MASK            0x000000ff
#define ADC_TSMR_MASK           0xf14f0f33
#define ADC_POSITION_MASK       0x0fff0fff
#define ADC_TRGR_MODE_MASK      0x00000007
#define ADC_TRGR_PERIOD_SHIFT   16
#define ADC_TRGR_MASK           0xffff0007
#define ADC_TRGR_PERIODIC       5
#define ADC_TRGR_CONTINUOUS     6
#define ADC_CECR_MASK           0x000000ff

#define ADC_WPMR_WPEN           BIT(0)
#define ADC_WPMR_WPITEN         BIT(1)
#define ADC_WPMR_WPCTEN         BIT(2)
#define ADC_WPMR_ENABLE_MASK    0x00000007
#define ADC_WPMR_KEY_MASK       0xffffff00
#define ADC_WPMR_KEY            0x41444300
#define ADC_WPSR_WPVS           BIT(0)

#define ADC_NATIVE_BITS         12
#define ADC_NATIVE_LEVELS       (1U << ADC_NATIVE_BITS)
#define ADC_CONVERSION_CYCLES   20
#define ADC_DEFAULT_VREF_UV     3300000
#define ADC_NUM_TRIGGERS        5

static void at91_adc_update_outputs(AT91ADCState *s);
static void at91_adc_clock_sync(AT91ADCState *s);
static void at91_adc_start_sequence(AT91ADCState *s);

static Clock *at91_adc_selected_clock(AT91ADCState *s)
{
    return (s->emr & ADC_EMR_SRCCLK) ? s->gclk : s->pclk;
}

static bool at91_adc_clock_ready(AT91ADCState *s)
{
    return clock_get_hz(s->pclk) &&
           clock_get_hz(at91_adc_selected_clock(s));
}

static unsigned int at91_adc_clock_divider(AT91ADCState *s)
{
    return 2 * (extract32(s->mr, ADC_MR_PRESCAL_SHIFT, 8) + 1);
}

static void at91_adc_set_timer_period(AT91ADCState *s, ptimer_state *timer)
{
    Clock *clk = at91_adc_selected_clock(s);

    if (clock_get_hz(clk)) {
        ptimer_set_period_from_clock(timer, clk,
                                     at91_adc_clock_divider(s));
    } else {
        /* A stopped source clock must still leave valid ptimer state. */
        ptimer_set_period(timer, 1);
    }
}

static void at91_adc_set_line(qemu_irq irq, bool *old_level, bool new_level)
{
    if (*old_level != new_level) {
        *old_level = new_level;
        qemu_set_irq(irq, new_level);
    }
}

static void at91_adc_update_outputs(AT91ADCState *s)
{
    bool accessible = clock_get_hz(s->pclk) != 0;

    at91_adc_set_line(s->irq, &s->irq_level,
                      accessible && (s->isr & s->imr & ADC_INT_MASK));
    at91_adc_set_line(s->rx_request, &s->rx_request_level,
                      accessible && (s->isr & ADC_INT_DRDY));
}

static unsigned int at91_adc_osr(AT91ADCState *s)
{
    /* Encodings 5..7 are reserved; keep their timing bounded. */
    return MIN(extract32(s->emr, ADC_EMR_OSR_SHIFT, 3), 4U);
}

static unsigned int at91_adc_sample_count(AT91ADCState *s)
{
    return 1U << (2 * at91_adc_osr(s));
}

static unsigned int at91_adc_startup_cycles(AT91ADCState *s)
{
    static const uint16_t cycles[16] = {
        0, 8, 16, 24, 64, 80, 96, 112,
        512, 576, 640, 704, 768, 832, 896, 960,
    };

    return cycles[extract32(s->mr, ADC_MR_STARTUP_SHIFT, 4)];
}

static unsigned int at91_adc_tracking_cycles(AT91ADCState *s)
{
    unsigned int track = extract32(s->mr, ADC_MR_TRACKTIM_SHIFT, 4);

    if (s->emr & ADC_EMR_TRACKX4) {
        if (track <= 1) {
            return 6;
        }
        if (track == 2) {
            return 7;
        }
        return 4 * (track + 1) - 6;
    }
    if (track <= 10) {
        return 6;
    }
    if (track <= 12) {
        return 7;
    }
    return track - 5;
}

static unsigned int at91_adc_conversion_cycles(AT91ADCState *s)
{
    unsigned int cycles = ADC_CONVERSION_CYCLES - 6 +
                          at91_adc_tracking_cycles(s);

    if (s->sequence_index == 0 &&
        (!((s->emr & ADC_EMR_ASTE) && !(s->mr & ADC_MR_USEQ)) ||
         s->sequence_repeats_remaining == at91_adc_sample_count(s)) &&
        (s->mr & BIT(5))) {
        cycles += at91_adc_startup_cycles(s);
    }
    return MAX(cycles, 1U);
}

static void at91_adc_stop_conversion(AT91ADCState *s)
{
    if (s->conversion_timer) {
        ptimer_transaction_begin(s->conversion_timer);
        ptimer_stop(s->conversion_timer);
        ptimer_transaction_commit(s->conversion_timer);
    }
    s->conversion_active = false;
    s->conversion_suspended = false;
    s->sequence_length = 0;
    s->sequence_index = 0;
    s->sequence_repeats_remaining = 0;
}

static void at91_adc_stop_trigger(AT91ADCState *s)
{
    if (s->trigger_timer) {
        ptimer_transaction_begin(s->trigger_timer);
        ptimer_stop(s->trigger_timer);
        ptimer_transaction_commit(s->trigger_timer);
    }
    s->trigger_suspended = false;
}

static void at91_adc_schedule_conversion_locked(AT91ADCState *s)
{
    ptimer_stop(s->conversion_timer);
    at91_adc_set_timer_period(s, s->conversion_timer);
    ptimer_set_limit(s->conversion_timer,
                     at91_adc_conversion_cycles(s), 1);
    s->conversion_suspended = !at91_adc_clock_ready(s);
    if (!s->conversion_suspended) {
        ptimer_run(s->conversion_timer, 1);
    }
}

static void at91_adc_schedule_conversion(AT91ADCState *s)
{
    ptimer_transaction_begin(s->conversion_timer);
    at91_adc_schedule_conversion_locked(s);
    ptimer_transaction_commit(s->conversion_timer);
}

static void at91_adc_configure_trigger_timer(AT91ADCState *s)
{
    uint32_t mode = s->trgr & ADC_TRGR_MODE_MASK;
    uint32_t period = extract32(s->trgr, ADC_TRGR_PERIOD_SHIFT, 16) + 1;

    ptimer_transaction_begin(s->trigger_timer);
    ptimer_stop(s->trigger_timer);
    at91_adc_set_timer_period(s, s->trigger_timer);
    s->trigger_suspended = false;
    if (mode == ADC_TRGR_PERIODIC) {
        ptimer_set_limit(s->trigger_timer, period, 1);
        s->trigger_suspended = !at91_adc_clock_ready(s);
        if (!s->trigger_suspended) {
            ptimer_run(s->trigger_timer, 0);
        }
    }
    ptimer_transaction_commit(s->trigger_timer);

    if (mode == ADC_TRGR_CONTINUOUS && !s->conversion_active) {
        at91_adc_start_sequence(s);
    }
}

static unsigned int at91_adc_build_sequence(AT91ADCState *s)
{
    unsigned int length = 0;
    unsigned int i;

    if (s->mr & ADC_MR_USEQ) {
        for (i = 0; i < AT91_ADC_NUM_CHANNELS; i++) {
            unsigned int channel;

            if (!(s->chsr & BIT(i))) {
                continue;
            }
            channel = extract32(s->seqr1, i * 4, 4);
            if (channel < AT91_ADC_NUM_CHANNELS) {
                s->sequence[length++] = channel;
            }
        }
    } else {
        for (i = 0; i < AT91_ADC_NUM_CHANNELS; i++) {
            if (s->chsr & BIT(i)) {
                s->sequence[length++] = i;
            }
        }
    }
    return length;
}

static bool at91_adc_prepare_sequence(AT91ADCState *s)
{
    if (s->conversion_active) {
        return false;
    }

    s->sequence_length = at91_adc_build_sequence(s);
    if (!s->sequence_length) {
        return false;
    }
    s->sequence_index = 0;
    s->current_channel = s->sequence[0];
    s->sequence_repeats_remaining = 1;
    if ((s->emr & ADC_EMR_ASTE) && !(s->mr & ADC_MR_USEQ)) {
        s->sequence_repeats_remaining = at91_adc_sample_count(s);
    }
    s->conversion_active = true;
    return true;
}

static void at91_adc_start_sequence(AT91ADCState *s)
{
    if (!at91_adc_prepare_sequence(s)) {
        return;
    }
    at91_adc_schedule_conversion(s);
}

static uint32_t at91_adc_native_sample(AT91ADCState *s,
                                       unsigned int channel)
{
    uint64_t value;

    if (!s->vref) {
        return 0;
    }
    value = (uint64_t)s->adci[channel] * ADC_NATIVE_LEVELS / s->vref;
    return MIN(value, ADC_NATIVE_LEVELS - 1);
}

static bool at91_adc_compare(uint32_t value, uint32_t window,
                             unsigned int mode)
{
    uint32_t low = extract32(window, 0, 16);
    uint32_t high = extract32(window, 16, 16);

    switch (mode) {
    case 0:
        return value < low;
    case 1:
        return value > high;
    case 2:
        return value >= low && value <= high;
    case 3:
        return value < low || value > high;
    default:
        g_assert_not_reached();
    }
}

static void at91_adc_update_comparison(AT91ADCState *s,
                                       unsigned int channel,
                                       uint32_t value)
{
    unsigned int selected = extract32(s->emr, ADC_EMR_CMPSEL_SHIFT, 4);
    unsigned int needed = extract32(s->emr, ADC_EMR_CMPFILTER_SHIFT, 2) + 1;
    bool selected_channel = (s->emr & ADC_EMR_CMPALL) ||
                            selected == channel;
    bool match = selected_channel &&
                 at91_adc_compare(value, s->cwr,
                                  s->emr & ADC_EMR_CMPMODE_MASK);

    if (!selected_channel) {
        return;
    }
    if (s->emr & ADC_EMR_CMPTYPE) {
        if (match) {
            s->comparison_storage = true;
        }
        return;
    }
    if (match) {
        s->compare_count = MIN((unsigned int)s->compare_count + 1, needed);
        if (s->compare_count >= needed) {
            s->isr |= ADC_INT_COMPE;
        }
    } else {
        s->compare_count = 0;
    }
}

static void at91_adc_store_result(AT91ADCState *s, unsigned int channel,
                                  uint32_t result)
{
    unsigned int osr = at91_adc_osr(s);

    at91_adc_update_comparison(s, channel, result);

    if (s->isr & BIT(channel)) {
        s->over |= BIT(channel);
    }
    s->cdr[channel] = result & 0xffff;
    s->isr |= BIT(channel);

    if (channel == AT91_ADC_NUM_CHANNELS - 1 &&
        at91_adc_compare(result, s->lccwr,
                         extract32(s->lctmr, 4, 2))) {
        s->isr |= ADC_INT_LCCHG;
    }

    if (!(s->emr & ADC_EMR_CMPTYPE) || s->comparison_storage) {
        if (s->isr & ADC_INT_DRDY) {
            s->isr |= ADC_INT_GOVRE;
        }
        if (osr || !(s->emr & ADC_EMR_TAG)) {
            s->lcdr = result & 0xffff;
        } else {
            s->lcdr = (result & 0x0fff) | (channel << 12);
        }
        if (osr && (s->emr & ADC_EMR_TAG)) {
            s->lcdr |= channel << 24;
        }
        s->isr |= ADC_INT_DRDY;
    }
    at91_adc_update_outputs(s);
}

static void at91_adc_complete_channel(AT91ADCState *s,
                                      unsigned int channel)
{
    unsigned int osr = at91_adc_osr(s);
    unsigned int samples = 1U << (2 * osr);
    uint32_t native = at91_adc_native_sample(s, channel);
    uint32_t result;

    if (osr) {
        if (!s->sample_count[channel] && (s->isr & BIT(channel))) {
            s->over |= BIT(channel);
        }
        s->accumulator[channel] += native;
        s->sample_count[channel]++;
        s->cdr[channel] = native;
        if (s->sample_count[channel] < samples) {
            return;
        }
        result = s->accumulator[channel] >> osr;
        s->accumulator[channel] = 0;
        s->sample_count[channel] = 0;
    } else {
        result = native << osr;
    }
    at91_adc_store_result(s, channel, result);
}

static void at91_adc_conversion_tick(void *opaque)
{
    AT91ADCState *s = opaque;

    if (!s->conversion_active) {
        return;
    }
    at91_adc_complete_channel(s, s->current_channel);
    s->sequence_index++;
    if (s->sequence_index < s->sequence_length) {
        s->current_channel = s->sequence[s->sequence_index];
        at91_adc_schedule_conversion_locked(s);
        return;
    }
    if (s->sequence_repeats_remaining > 1) {
        s->sequence_repeats_remaining--;
        s->sequence_index = 0;
        s->current_channel = s->sequence[0];
        at91_adc_schedule_conversion_locked(s);
        return;
    }

    s->conversion_active = false;
    s->conversion_suspended = false;
    if ((s->trgr & ADC_TRGR_MODE_MASK) == ADC_TRGR_CONTINUOUS &&
        at91_adc_prepare_sequence(s)) {
        at91_adc_schedule_conversion_locked(s);
    }
}

static void at91_adc_trigger_tick(void *opaque)
{
    AT91ADCState *s = opaque;

    if ((s->trgr & ADC_TRGR_MODE_MASK) == ADC_TRGR_PERIODIC) {
        at91_adc_start_sequence(s);
    }
}

static void at91_adc_set_analog_input(void *opaque, int n, int level)
{
    AT91ADCState *s = opaque;

    s->adci[n] = MAX(level, 0);
}

static void at91_adc_set_trigger(void *opaque, int n, int level)
{
    AT91ADCState *s = opaque;
    uint8_t mask = BIT(n);
    bool old_level = s->trigger_levels & mask;
    bool new_level = !!level;
    unsigned int selected = extract32(s->mr, 1, 3);
    unsigned int mode = s->trgr & ADC_TRGR_MODE_MASK;
    bool fire = false;

    if (new_level) {
        s->trigger_levels |= mask;
    } else {
        s->trigger_levels &= ~mask;
    }
    if (n != selected || !at91_adc_clock_ready(s)) {
        return;
    }
    if (mode == 1) {
        fire = !old_level && new_level;
    } else if (mode == 2) {
        fire = old_level && !new_level;
    } else if (mode == 3) {
        fire = old_level != new_level;
    }
    if (fire) {
        at91_adc_start_sequence(s);
    }
}

static void at91_adc_clock_sync(AT91ADCState *s)
{
    bool ready;
    bool periodic;

    if (!s->conversion_timer || !s->trigger_timer) {
        at91_adc_update_outputs(s);
        return;
    }
    ready = at91_adc_clock_ready(s);

    ptimer_transaction_begin(s->conversion_timer);
    if (s->conversion_active && !ready && !s->conversion_suspended) {
        ptimer_stop(s->conversion_timer);
        s->conversion_suspended = true;
    }
    at91_adc_set_timer_period(s, s->conversion_timer);
    if (s->conversion_active && ready && s->conversion_suspended) {
        ptimer_run(s->conversion_timer, 1);
        s->conversion_suspended = false;
    }
    ptimer_transaction_commit(s->conversion_timer);

    periodic = (s->trgr & ADC_TRGR_MODE_MASK) == ADC_TRGR_PERIODIC;
    ptimer_transaction_begin(s->trigger_timer);
    if (periodic && !ready && !s->trigger_suspended) {
        ptimer_stop(s->trigger_timer);
        s->trigger_suspended = true;
    }
    at91_adc_set_timer_period(s, s->trigger_timer);
    if (periodic && ready && s->trigger_suspended) {
        ptimer_run(s->trigger_timer, 0);
        s->trigger_suspended = false;
    }
    ptimer_transaction_commit(s->trigger_timer);
    at91_adc_update_outputs(s);
}

static void at91_adc_clock_changed(void *opaque, ClockEvent event)
{
    at91_adc_clock_sync(opaque);
}

static void at91_adc_record_wpviolation(AT91ADCState *s, hwaddr offset)
{
    s->wpsr = ADC_WPSR_WPVS | ((offset & 0xffff) << 8);
}

static bool at91_adc_write_protected(AT91ADCState *s, hwaddr offset)
{
    bool protected = false;

    if (offset == ADC_CR) {
        protected = s->wpmr & ADC_WPMR_WPCTEN;
    } else if (offset == ADC_IER || offset == ADC_IDR) {
        protected = s->wpmr & ADC_WPMR_WPITEN;
    } else if (offset == ADC_MR || offset == ADC_SEQR1 ||
               offset == ADC_CHER || offset == ADC_CHDR ||
               offset == ADC_LCTMR || offset == ADC_LCCWR ||
               offset == ADC_EMR || offset == ADC_CWR ||
               offset == ADC_CCR || offset == ADC_ACR ||
               offset == ADC_PDR || offset == ADC_TSMR ||
               offset == ADC_TRGR || offset == ADC_CVR ||
               offset == ADC_CECR || offset == ADC_TSCVR) {
        protected = s->wpmr & ADC_WPMR_WPEN;
    }

    if (protected) {
        at91_adc_record_wpviolation(s, offset);
    }
    return protected;
}

static uint64_t at91_adc_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91ADCState *s = opaque;
    uint32_t value = 0;
    unsigned int channel;

    if (offset >= ADC_CDR_FIRST && offset <= ADC_CDR_LAST) {
        channel = (offset - ADC_CDR_FIRST) / 4;
        value = s->cdr[channel];
        s->isr &= ~BIT(channel);
        at91_adc_update_outputs(s);
        return value;
    }

    switch (offset) {
    case ADC_MR:
        value = s->mr;
        break;
    case ADC_SEQR1:
        value = s->seqr1;
        break;
    case ADC_CHSR:
        value = s->chsr;
        break;
    case ADC_LCDR:
        value = s->lcdr;
        s->isr &= ~ADC_INT_DRDY;
        at91_adc_update_outputs(s);
        break;
    case ADC_IMR:
        value = s->imr;
        break;
    case ADC_ISR:
        value = s->isr;
        s->isr &= ~ADC_ISR_READ_CLEAR;
        s->compare_count = 0;
        at91_adc_update_outputs(s);
        break;
    case ADC_LCTMR:
        value = s->lctmr;
        break;
    case ADC_LCCWR:
        value = s->lccwr;
        break;
    case ADC_OVER:
        value = s->over;
        s->over = 0;
        break;
    case ADC_EMR:
        value = s->emr;
        break;
    case ADC_CWR:
        value = s->cwr;
        break;
    case ADC_CCR:
        value = s->ccr;
        break;
    case ADC_ACR:
        value = s->acr;
        break;
    case ADC_PDR:
        value = s->pdr;
        break;
    case ADC_TSMR:
        value = s->tsmr;
        break;
    case ADC_XPOSR:
        value = s->xposr;
        break;
    case ADC_YPOSR:
        value = s->yposr;
        break;
    case ADC_PRESSR:
        value = s->pressr;
        break;
    case ADC_TRGR:
        value = s->trgr;
        break;
    case ADC_CVR:
        value = s->cvr;
        break;
    case ADC_CECR:
        value = s->cecr;
        break;
    case ADC_TSCVR:
        value = s->tscvr;
        break;
    case ADC_WPMR:
        value = s->wpmr;
        break;
    case ADC_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        break;
    default:
        /* Reserved and write-only locations are decoded and read as zero. */
        break;
    }
    return size == 2 ? value & 0xffff : value;
}

static void at91_adc_reset_registers(AT91ADCState *s, bool hardware)
{
    at91_adc_stop_conversion(s);
    at91_adc_stop_trigger(s);
    s->mr = ADC_MR_RESET;
    s->seqr1 = 0;
    s->chsr = 0;
    s->lcdr = 0;
    s->imr = 0;
    s->isr = 0;
    s->lctmr = 0;
    s->lccwr = 0;
    s->over = 0;
    s->emr = 0;
    s->cwr = 0;
    s->ccr = 0;
    memset(s->cdr, 0, sizeof(s->cdr));
    s->acr = ADC_ACR_RESET;
    s->pdr = 0;
    s->tsmr = 0;
    s->xposr = 0;
    s->yposr = 0;
    s->pressr = 0;
    s->trgr = 0;
    s->cvr = 0;
    s->cecr = 0;
    s->tscvr = 0;
    memset(s->accumulator, 0, sizeof(s->accumulator));
    memset(s->sample_count, 0, sizeof(s->sample_count));
    s->compare_count = 0;
    s->comparison_storage = false;
    s->wpsr = 0;
    if (hardware) {
        s->wpmr = 0;
    }
    at91_adc_update_outputs(s);
}

static void at91_adc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91ADCState *s = opaque;
    uint32_t val = value;

    if (at91_adc_write_protected(s, offset)) {
        return;
    }

    switch (offset) {
    case ADC_CR:
        val &= 0x17;
        if (val & ADC_CR_SWRST) {
            at91_adc_reset_registers(s, false);
            return;
        }
        if (val & ADC_CR_CMPRST) {
            s->comparison_storage = false;
            s->compare_count = 0;
        }
        if ((val & ADC_CR_START) &&
            !(s->trgr & ADC_TRGR_MODE_MASK)) {
            at91_adc_start_sequence(s);
        }
        break;
    case ADC_MR:
        s->mr = val & ADC_MR_MASK;
        at91_adc_clock_sync(s);
        break;
    case ADC_SEQR1:
        s->seqr1 = val;
        break;
    case ADC_CHER:
        s->chsr |= val & ADC_CHANNEL_MASK;
        if ((s->trgr & ADC_TRGR_MODE_MASK) == ADC_TRGR_CONTINUOUS) {
            at91_adc_start_sequence(s);
        }
        break;
    case ADC_CHDR:
        val &= ADC_CHANNEL_MASK;
        s->chsr &= ~val;
        s->isr &= ~val;
        at91_adc_update_outputs(s);
        break;
    case ADC_IER:
        s->imr |= val & ADC_INT_MASK;
        at91_adc_update_outputs(s);
        break;
    case ADC_IDR:
        s->imr &= ~(val & ADC_INT_MASK);
        at91_adc_update_outputs(s);
        break;
    case ADC_LCTMR:
        s->lctmr = val & ADC_LCTMR_MASK;
        break;
    case ADC_LCCWR:
        s->lccwr = val & ADC_WINDOW_12_MASK;
        break;
    case ADC_EMR:
        s->emr = val & ADC_EMR_MASK;
        at91_adc_clock_sync(s);
        break;
    case ADC_CWR:
        s->cwr = val;
        break;
    case ADC_CCR:
        s->ccr = val & ADC_CCR_MASK;
        break;
    case ADC_ACR:
        s->acr = ADC_ACR_FIXED | (val & ADC_ACR_WRITABLE_MASK);
        break;
    case ADC_PDR:
        s->pdr = val & ADC_PDR_MASK;
        break;
    case ADC_TSMR:
        s->tsmr = val & ADC_TSMR_MASK;
        break;
    case ADC_TRGR:
        s->trgr = val & ADC_TRGR_MASK;
        at91_adc_configure_trigger_timer(s);
        break;
    case ADC_CVR:
        s->cvr = val;
        break;
    case ADC_CECR:
        s->cecr = val & ADC_CECR_MASK;
        break;
    case ADC_TSCVR:
        s->tscvr = val;
        break;
    case ADC_WPMR:
        if ((val & ADC_WPMR_KEY_MASK) == ADC_WPMR_KEY) {
            s->wpmr = val & ADC_WPMR_ENABLE_MASK;
        }
        break;
    default:
        /* Writes to reserved, read-only, and result registers are ignored. */
        break;
    }
}

static bool at91_adc_accepts(void *opaque, hwaddr offset,
                             unsigned int size, bool is_write,
                             MemTxAttrs attrs)
{
    if (size == 4 && !(offset & 3)) {
        return true;
    }
    return !is_write && size == 2 && offset == ADC_LCDR;
}

static const MemoryRegionOps at91_adc_ops = {
    .read = at91_adc_read,
    .write = at91_adc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = at91_adc_accepts,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 4,
    },
};

static void at91_adc_reset(DeviceState *dev)
{
    at91_adc_reset_registers(AT91_ADC(dev), true);
}

static void at91_adc_init(Object *obj)
{
    AT91ADCState *s = AT91_ADC(obj);
    DeviceState *dev = DEVICE(obj);
    unsigned int i;

    memory_region_init_io(&s->iomem, obj, &at91_adc_ops, s,
                          TYPE_AT91_ADC, ADC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    qdev_init_gpio_in_named(dev, at91_adc_set_analog_input,
                            "analog-input", AT91_ADC_NUM_CHANNELS);
    qdev_init_gpio_in_named(dev, at91_adc_set_trigger,
                            "trigger", ADC_NUM_TRIGGERS);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_adc_clock_changed,
                                 s, ClockUpdate);
    s->gclk = qdev_init_clock_in(dev, "gclk", at91_adc_clock_changed,
                                 s, ClockUpdate);

    for (i = 0; i < AT91_ADC_NUM_CHANNELS; i++) {
        object_property_add_uint32_ptr(obj, "adci[*]", &s->adci[i],
                                       OBJ_PROP_FLAG_READWRITE);
    }
    object_property_add_uint32_ptr(obj, "vref", &s->vref,
                                   OBJ_PROP_FLAG_READWRITE);
    s->vref = ADC_DEFAULT_VREF_UV;
}

static void at91_adc_realize(DeviceState *dev, Error **errp)
{
    AT91ADCState *s = AT91_ADC(dev);
    uint8_t policy = PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
                     PTIMER_POLICY_NO_COUNTER_ROUND_DOWN;

    if (!clock_has_source(s->pclk) || !clock_has_source(s->gclk)) {
        error_setg(errp, TYPE_AT91_ADC
                   ": pclk and gclk clocks must be connected");
        return;
    }
    s->conversion_timer = ptimer_init(at91_adc_conversion_tick, s, policy);
    s->trigger_timer = ptimer_init(at91_adc_trigger_tick, s, policy);

    ptimer_transaction_begin(s->conversion_timer);
    at91_adc_set_timer_period(s, s->conversion_timer);
    ptimer_transaction_commit(s->conversion_timer);
    ptimer_transaction_begin(s->trigger_timer);
    at91_adc_set_timer_period(s, s->trigger_timer);
    ptimer_transaction_commit(s->trigger_timer);
}

static void at91_adc_finalize(Object *obj)
{
    AT91ADCState *s = AT91_ADC(obj);

    if (s->conversion_timer) {
        ptimer_free(s->conversion_timer);
    }
    if (s->trigger_timer) {
        ptimer_free(s->trigger_timer);
    }
}

static int at91_adc_post_load(void *opaque, int version_id)
{
    AT91ADCState *s = opaque;
    unsigned int i;

    if (s->sequence_length > AT91_ADC_NUM_CHANNELS ||
        s->sequence_index > s->sequence_length ||
        s->current_channel >= AT91_ADC_NUM_CHANNELS ||
        s->sequence_repeats_remaining > 256 ||
        (s->conversion_active &&
         (!s->sequence_length || s->sequence_index >= s->sequence_length ||
          !s->sequence_repeats_remaining ||
          s->current_channel != s->sequence[s->sequence_index]))) {
        return -EINVAL;
    }
    for (i = 0; i < AT91_ADC_NUM_CHANNELS; i++) {
        if ((i < s->sequence_length &&
             s->sequence[i] >= AT91_ADC_NUM_CHANNELS) ||
            s->sample_count[i] >= 256) {
            return -EINVAL;
        }
    }

    s->mr &= ADC_MR_MASK;
    s->chsr &= ADC_CHANNEL_MASK;
    s->imr &= ADC_INT_MASK;
    s->isr &= ADC_ISR_MASK;
    s->over &= ADC_CHANNEL_MASK;
    s->emr &= ADC_EMR_MASK;
    s->ccr &= ADC_CCR_MASK;
    s->pdr &= ADC_PDR_MASK;
    s->tsmr &= ADC_TSMR_MASK;
    s->trgr &= ADC_TRGR_MASK;
    s->cecr &= ADC_CECR_MASK;
    s->wpmr &= ADC_WPMR_ENABLE_MASK;
    qemu_set_irq(s->irq, 0);
    qemu_set_irq(s->rx_request, 0);
    s->irq_level = false;
    s->rx_request_level = false;
    at91_adc_clock_sync(s);
    return 0;
}

static const VMStateDescription vmstate_at91_adc = {
    .name = TYPE_AT91_ADC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_adc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, AT91ADCState),
        VMSTATE_CLOCK(gclk, AT91ADCState),
        VMSTATE_PTIMER(conversion_timer, AT91ADCState),
        VMSTATE_PTIMER(trigger_timer, AT91ADCState),
        VMSTATE_UINT32(mr, AT91ADCState),
        VMSTATE_UINT32(seqr1, AT91ADCState),
        VMSTATE_UINT32(chsr, AT91ADCState),
        VMSTATE_UINT32(lcdr, AT91ADCState),
        VMSTATE_UINT32(imr, AT91ADCState),
        VMSTATE_UINT32(isr, AT91ADCState),
        VMSTATE_UINT32(lctmr, AT91ADCState),
        VMSTATE_UINT32(lccwr, AT91ADCState),
        VMSTATE_UINT32(over, AT91ADCState),
        VMSTATE_UINT32(emr, AT91ADCState),
        VMSTATE_UINT32(cwr, AT91ADCState),
        VMSTATE_UINT32(ccr, AT91ADCState),
        VMSTATE_UINT32_ARRAY(cdr, AT91ADCState, AT91_ADC_NUM_CHANNELS),
        VMSTATE_UINT32(acr, AT91ADCState),
        VMSTATE_UINT32(pdr, AT91ADCState),
        VMSTATE_UINT32(tsmr, AT91ADCState),
        VMSTATE_UINT32(xposr, AT91ADCState),
        VMSTATE_UINT32(yposr, AT91ADCState),
        VMSTATE_UINT32(pressr, AT91ADCState),
        VMSTATE_UINT32(trgr, AT91ADCState),
        VMSTATE_UINT32(cvr, AT91ADCState),
        VMSTATE_UINT32(cecr, AT91ADCState),
        VMSTATE_UINT32(tscvr, AT91ADCState),
        VMSTATE_UINT32(wpmr, AT91ADCState),
        VMSTATE_UINT32(wpsr, AT91ADCState),
        VMSTATE_UINT32_ARRAY(adci, AT91ADCState, AT91_ADC_NUM_CHANNELS),
        VMSTATE_UINT32(vref, AT91ADCState),
        VMSTATE_UINT64_ARRAY(accumulator, AT91ADCState,
                             AT91_ADC_NUM_CHANNELS),
        VMSTATE_UINT16_ARRAY(sample_count, AT91ADCState,
                             AT91_ADC_NUM_CHANNELS),
        VMSTATE_UINT8_ARRAY(sequence, AT91ADCState, AT91_ADC_NUM_CHANNELS),
        VMSTATE_UINT8(sequence_length, AT91ADCState),
        VMSTATE_UINT8(sequence_index, AT91ADCState),
        VMSTATE_UINT8(current_channel, AT91ADCState),
        VMSTATE_UINT8(compare_count, AT91ADCState),
        VMSTATE_UINT16(sequence_repeats_remaining, AT91ADCState),
        VMSTATE_BOOL(conversion_active, AT91ADCState),
        VMSTATE_BOOL(comparison_storage, AT91ADCState),
        VMSTATE_BOOL(conversion_suspended, AT91ADCState),
        VMSTATE_BOOL(trigger_suspended, AT91ADCState),
        VMSTATE_UINT8(trigger_levels, AT91ADCState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_adc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 Analog-to-Digital Converter";
    dc->realize = at91_adc_realize;
    dc->vmsd = &vmstate_at91_adc;
    device_class_set_legacy_reset(dc, at91_adc_reset);
}

static const TypeInfo at91_adc_info = {
    .name = TYPE_AT91_ADC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91ADCState),
    .instance_init = at91_adc_init,
    .instance_finalize = at91_adc_finalize,
    .class_init = at91_adc_class_init,
};

static void at91_adc_register_types(void)
{
    type_register_static(&at91_adc_info);
}

type_init(at91_adc_register_types)
