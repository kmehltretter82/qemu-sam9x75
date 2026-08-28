/*
 * Microchip AT91 Timer Counter Block
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/timer/at91_tcb.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TCB_CCR                 0x00
#define TCB_CMR                 0x04
#define TCB_SMMR                0x08
#define TCB_RAB                 0x0c
#define TCB_CV                  0x10
#define TCB_RA                  0x14
#define TCB_RB                  0x18
#define TCB_RC                  0x1c
#define TCB_SR                  0x20
#define TCB_IER                 0x24
#define TCB_IDR                 0x28
#define TCB_IMR                 0x2c
#define TCB_EMR                 0x30
#define TCB_CSR                 0x34
#define TCB_SSR                 0x38

#define TCB_CHANNEL_STRIDE      0x40
#define TCB_BCR                 0xc0
#define TCB_BMR                 0xc4
#define TCB_QIER                0xc8
#define TCB_QIDR                0xcc
#define TCB_QIMR                0xd0
#define TCB_QISR                0xd4
#define TCB_QSR                 0xdc
#define TCB_WPMR                0xe4
#define TCB_MMIO_SIZE           0x100

#define TCB_CCR_CLKEN           BIT(0)
#define TCB_CCR_CLKDIS          BIT(1)
#define TCB_CCR_SWTRG           BIT(2)

#define TCB_CMR_TCCLKS_MASK     0x7
#define TCB_CMR_CPCSTOP         BIT(6)
#define TCB_CMR_CPCDIS          BIT(7)
#define TCB_CMR_WAVESEL_MASK    (3U << 13)
#define TCB_CMR_WAVESEL_UP_RC   (2U << 13)
#define TCB_CMR_WAVE            BIT(15)
/* Waveform mode: what a compare or a software trigger does to TIOA. */
#define TCB_CMR_ACPA_SHIFT      16
#define TCB_CMR_ACPC_SHIFT      18
#define TCB_CMR_ASWTRG_SHIFT    22
#define TCB_CMR_AEEVT_SHIFT     20
/* Waveform mode external event: source, edge, and whether it triggers. */
#define TCB_CMR_ENETRG          BIT(12)
#define TCB_CMR_EEVT_SHIFT      10
#define TCB_CMR_EEVT_MASK       3
#define TCB_CMR_EEVT_TIOB       0
#define TCB_CMR_EEVTEDG_SHIFT   8
#define TCB_CMR_EEVTEDG_MASK    3
#define TCB_TIOA_EFFECT_NONE    0
#define TCB_TIOA_EFFECT_SET     1
#define TCB_TIOA_EFFECT_CLEAR   2
#define TCB_TIOA_EFFECT_TOGGLE  3
/* Capture mode: which TIOA edge loads RA and RB. */
#define TCB_CMR_LDRA_SHIFT      16
#define TCB_CMR_LDRB_SHIFT      18
#define TCB_CMR_LDR_MASK        3
#define TCB_LDR_NONE            0
#define TCB_LDR_RISING          1
#define TCB_LDR_FALLING         2
#define TCB_LDR_EDGE            3
/* Capture mode external trigger: which pin, and on which edge. */
#define TCB_CMR_ABETRG          BIT(10)
#define TCB_CMR_ETRGEDG_SHIFT   8
#define TCB_CMR_ETRGEDG_MASK    3

#define TCB_EMR_NODIVCLK        BIT(8)
#define TCB_EMR_MASK            (TCB_EMR_NODIVCLK | (3U << 4) | 3U)
#define TCB_SMMR_MASK           0x3

#define TCB_INT_COVFS           BIT(0)
#define TCB_INT_LDRAS           BIT(5)
#define TCB_INT_LDRBS           BIT(6)
#define TCB_INT_ETRGS           BIT(7)
#define TCB_INT_CPAS            BIT(2)
#define TCB_INT_CPBS            BIT(3)
#define TCB_INT_CPCS            BIT(4)
#define TCB_INT_SECE            BIT(10)
#define TCB_INT_MASK            (0xffU | TCB_INT_SECE)
#define TCB_SR_CLKSTA           BIT(16)
#define TCB_SR_MIRROR_MASK      (7U << 16)

#define TCB_QINT_MASK           0xf7

#define TCB_WPMR_WPEN           BIT(0)
#define TCB_WPMR_WPITEN         BIT(1)
#define TCB_WPMR_WPCREN         BIT(2)
#define TCB_WPMR_FIRSTE         BIT(4)
#define TCB_WPMR_ENABLE_MASK    0x17
#define TCB_WPMR_KEY_MASK       0xffffff00
#define TCB_WPMR_KEY            0x54494d00

#define TCB_SSR_WPVS            BIT(0)
#define TCB_SSR_WPVSRC_MASK     0x00ffff00

#define TCB_COUNTER_RANGE       (UINT64_C(1) << 32)

#define AT91_TCB_COMPARE_REQUESTS 3

/*
 * A compare event is a pulse to the XDMAC: the request line follows the
 * latched status bit, so reading the status register releases it.
 */
/* Apply a waveform-mode effect to this channel's TIOA output. */
static void at91_tcb_apply_tioa_effect(AT91TCBChannel *ch, unsigned int shift)
{
    unsigned int effect = (ch->cmr >> shift) & 3;
    AT91TCBState *s = ch->owner;
    unsigned int index = ch - s->channel;
    bool level = ch->tioa_out;

    if (!(ch->cmr & TCB_CMR_WAVE)) {
        return;
    }
    switch (effect) {
    case TCB_TIOA_EFFECT_SET:
        level = true;
        break;
    case TCB_TIOA_EFFECT_CLEAR:
        level = false;
        break;
    case TCB_TIOA_EFFECT_TOGGLE:
        level = !level;
        break;
    default:
        return;
    }
    if (level != ch->tioa_out) {
        ch->tioa_out = level;
        qemu_set_irq(s->tioa[index], level);
    }
}

static void at91_tcb_update_compare_requests(AT91TCBState *s)
{
    static const uint32_t bits[AT91_TCB_COMPARE_REQUESTS] = {
        TCB_INT_CPAS, TCB_INT_CPBS, TCB_INT_CPCS,
    };
    unsigned int i, j;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91TCBChannel *ch = &s->channel[i];

        for (j = 0; j < AT91_TCB_COMPARE_REQUESTS; j++) {
            bool level = !!(ch->status & bits[j]);

            if (level != ch->compare_request_level[j]) {
                ch->compare_request_level[j] = level;
                qemu_set_irq(s->compare_request[i * AT91_TCB_COMPARE_REQUESTS
                                                + j], level);
            }
        }
        /* The capture and trigger requests follow their status bits. */
        bool capture = !!(ch->status & (TCB_INT_LDRAS | TCB_INT_LDRBS));
        bool etrg = !!(ch->status & TCB_INT_ETRGS);

        if (capture != ch->capture_request_level) {
            ch->capture_request_level = capture;
            qemu_set_irq(s->capture_request[i], capture);
        }
        if (etrg != ch->etrg_request_level) {
            ch->etrg_request_level = etrg;
            qemu_set_irq(s->etrg_request[i], etrg);
        }
    }
}

static void at91_tcb_update_irq(AT91TCBState *s)
{
    bool level = s->qisr & s->qimr;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        level |= !!(s->channel[i].status & s->channel[i].imr);
    }
    qemu_set_irq(s->irq, level);
    at91_tcb_update_compare_requests(s);
}

static bool at91_tcb_auto_rc(const AT91TCBChannel *ch)
{
    return (ch->cmr & (TCB_CMR_WAVE | TCB_CMR_WAVESEL_MASK)) ==
           (TCB_CMR_WAVE | TCB_CMR_WAVESEL_UP_RC);
}

/* Counter value at which the period restarts: RC in up-RC mode, else wrap. */
static uint64_t at91_tcb_period_end(const AT91TCBChannel *ch)
{
    if (at91_tcb_auto_rc(ch) && ch->rc) {
        return ch->rc;
    }
    return TCB_COUNTER_RANGE;
}

/*
 * RA and RB are compare registers in waveform mode only; in capture mode
 * they are loaded from TIOA/TIOB edges, which this model does not have.
 */
static bool at91_tcb_compares_active(const AT91TCBChannel *ch)
{
    return ch->cmr & TCB_CMR_WAVE;
}

/* The next counter value at which a status bit is due, after "from". */
static uint64_t at91_tcb_next_boundary(const AT91TCBChannel *ch,
                                       uint64_t from)
{
    uint64_t next = at91_tcb_period_end(ch);

    if (at91_tcb_compares_active(ch)) {
        if (ch->ra > from && ch->ra < next) {
            next = ch->ra;
        }
        if (ch->rb > from && ch->rb < next) {
            next = ch->rb;
        }
    }
    return next;
}

static Clock *at91_tcb_selected_clock(AT91TCBChannel *ch,
                                      unsigned int *divider)
{
    AT91TCBState *s = ch->owner;

    if (ch->emr & TCB_EMR_NODIVCLK) {
        *divider = 1;
        return s->pclk;
    }

    switch (ch->cmr & TCB_CMR_TCCLKS_MASK) {
    case 0:
        *divider = 1;
        return s->gclk;
    case 1:
        *divider = 8;
        return s->pclk;
    case 2:
        *divider = 32;
        return s->pclk;
    case 3:
        *divider = 128;
        return s->pclk;
    case 4:
        *divider = 1;
        return s->slck;
    default:
        /* XC0--XC2 require an externally routed clock. */
        *divider = 1;
        return NULL;
    }
}

static bool at91_tcb_clock_active(AT91TCBChannel *ch)
{
    unsigned int divider;
    Clock *clk = at91_tcb_selected_clock(ch, &divider);

    return clk && clock_get_hz(clk);
}

static void at91_tcb_set_period(AT91TCBChannel *ch)
{
    unsigned int divider;
    Clock *clk = at91_tcb_selected_clock(ch, &divider);

    if (clk && clock_get_hz(clk)) {
        ptimer_set_period_from_clock(ch->timer, clk, divider);
    } else {
        /* Keep the ptimer configuration valid while its source is gated. */
        ptimer_set_period(ch->timer, 1);
    }
}

static uint32_t at91_tcb_counter(AT91TCBChannel *ch)
{
    uint64_t remaining = ptimer_get_count(ch->timer);
    uint64_t span = ch->segment_end - ch->segment_start;

    if (!remaining || remaining > span) {
        return ch->segment_start;
    }
    return ch->segment_start + (span - remaining);
}


/* Arm the segment starting at "counter"; the ptimer transaction is held. */
static void at91_tcb_arm_segment(AT91TCBChannel *ch, uint32_t counter)
{
    uint64_t boundary;

    if (counter >= at91_tcb_period_end(ch)) {
        counter = 0;
    }
    boundary = at91_tcb_next_boundary(ch, counter);
    ch->segment_start = counter;
    ch->segment_end = boundary;

    ptimer_stop(ch->timer);
    at91_tcb_set_period(ch);
    /*
     * One-shot per segment: the tick handler arms the next one, so the
     * channel only stops at a period end that CPCSTOP or CPCDIS selected.
     */
    ptimer_set_limit(ch->timer, boundary - counter, 1);
    ch->clock_suspended = !at91_tcb_clock_active(ch);
    if (ch->running && !ch->clock_suspended) {
        ptimer_run(ch->timer, 1);
    }
}

static void at91_tcb_configure_channel(AT91TCBChannel *ch, uint32_t counter)
{
    ptimer_transaction_begin(ch->timer);
    at91_tcb_arm_segment(ch, counter);
    ptimer_transaction_commit(ch->timer);
}

static void at91_tcb_start_channel(AT91TCBChannel *ch, bool reset)
{
    uint32_t counter = reset ? 0 : at91_tcb_counter(ch);

    ch->enabled = true;
    ch->running = true;
    at91_tcb_configure_channel(ch, counter);
}

static void at91_tcb_stop_channel(AT91TCBChannel *ch)
{
    ptimer_transaction_begin(ch->timer);
    ptimer_stop(ch->timer);
    ptimer_transaction_commit(ch->timer);
    ch->enabled = false;
    ch->running = false;
    ch->clock_suspended = false;
}

static uint32_t at91_tcb_channel_status(const AT91TCBChannel *ch)
{
    return ch->status | (ch->enabled ? TCB_SR_CLKSTA : 0);
}

/*
 * A TIOA edge in capture mode loads RA and RB from the counter, each on
 * the edge its own selector names.  The two are independent: the usual
 * pulse measurement selects a rising edge for RA and a falling edge for
 * RB, but nothing makes RB wait for RA.
 */
static void at91_tcb_capture_edge(AT91TCBChannel *ch, bool rising)
{
    unsigned int ldra = (ch->cmr >> TCB_CMR_LDRA_SHIFT) & TCB_CMR_LDR_MASK;
    unsigned int ldrb = (ch->cmr >> TCB_CMR_LDRB_SHIFT) & TCB_CMR_LDR_MASK;
    uint32_t counter;

    if (ch->cmr & TCB_CMR_WAVE) {
        return;
    }
    counter = at91_tcb_counter(ch);

    if (ldra != TCB_LDR_NONE &&
        (ldra == TCB_LDR_EDGE || (ldra == TCB_LDR_RISING) == rising)) {
        ch->ra = counter;
        ch->status |= TCB_INT_LDRAS;
    }
    if (ldrb != TCB_LDR_NONE &&
        (ldrb == TCB_LDR_EDGE || (ldrb == TCB_LDR_RISING) == rising)) {
        ch->rb = counter;
        ch->status |= TCB_INT_LDRBS;
    }
    at91_tcb_update_irq(ch->owner);
}

/*
 * In capture mode ABETRG picks TIOA or TIOB as the external trigger and
 * ETRGEDG picks its edge.  The trigger restarts the counter and reports
 * itself in ETRGS, which is cleared by reading the status register.
 */
static void at91_tcb_external_trigger(AT91TCBChannel *ch, bool from_tioa,
                                      bool rising)
{
    unsigned int edge = (ch->cmr >> TCB_CMR_ETRGEDG_SHIFT) &
                        TCB_CMR_ETRGEDG_MASK;

    if (ch->cmr & TCB_CMR_WAVE) {
        return;
    }
    if (from_tioa != !!(ch->cmr & TCB_CMR_ABETRG)) {
        return;
    }
    if (edge == TCB_LDR_NONE ||
        !(edge == TCB_LDR_EDGE || (edge == TCB_LDR_RISING) == rising)) {
        return;
    }
    ch->status |= TCB_INT_ETRGS;
    at91_tcb_configure_channel(ch, 0);
}

/*
 * In waveform mode an external event acts on TIOA through AEEVT and, when
 * ENETRG is set, also restarts the counter.  Only TIOB is modeled as an
 * event source; XC0 to XC2 need the block cross-connect, which this model
 * does not have.  A TIOB chosen as the event source is an input, so the
 * channel does not drive it.
 */
static void at91_tcb_external_event(AT91TCBChannel *ch, bool from_tioa,
                                    bool rising)
{
    unsigned int source = (ch->cmr >> TCB_CMR_EEVT_SHIFT) &
                          TCB_CMR_EEVT_MASK;
    unsigned int edge = (ch->cmr >> TCB_CMR_EEVTEDG_SHIFT) &
                        TCB_CMR_EEVTEDG_MASK;

    if (!(ch->cmr & TCB_CMR_WAVE) || from_tioa ||
        source != TCB_CMR_EEVT_TIOB) {
        return;
    }
    if (edge == TCB_LDR_NONE ||
        !(edge == TCB_LDR_EDGE || (edge == TCB_LDR_RISING) == rising)) {
        return;
    }
    at91_tcb_apply_tioa_effect(ch, TCB_CMR_AEEVT_SHIFT);
    if (ch->cmr & TCB_CMR_ENETRG) {
        at91_tcb_configure_channel(ch, 0);
    }
}

static void at91_tcb_pin_input(AT91TCBState *s, unsigned int index,
                               bool from_tioa, int level)
{
    AT91TCBChannel *ch = &s->channel[index];
    bool value = level > 0;
    bool *pin = from_tioa ? &ch->tioa_in : &ch->tiob_in;

    if (value == *pin) {
        return;
    }
    *pin = value;
    if (!ch->enabled || !ch->running) {
        return;
    }
    if (from_tioa) {
        at91_tcb_capture_edge(ch, value);
    }
    at91_tcb_external_trigger(ch, from_tioa, value);
    at91_tcb_external_event(ch, from_tioa, value);
    at91_tcb_update_irq(s);
}

static void at91_tcb_tioa_input(void *opaque, int index, int level)
{
    at91_tcb_pin_input(opaque, index, true, level);
}

static void at91_tcb_tiob_input(void *opaque, int index, int level)
{
    at91_tcb_pin_input(opaque, index, false, level);
}

static void at91_tcb_record_wp_violation(AT91TCBChannel *ch, hwaddr offset)
{
    AT91TCBState *s = ch->owner;

    if (!(s->wpmr & TCB_WPMR_FIRSTE) || !(ch->ssr & TCB_SSR_WPVS)) {
        ch->ssr &= ~TCB_SSR_WPVSRC_MASK;
        ch->ssr |= TCB_SSR_WPVS | ((offset & 0xffff) << 8);
    }
    ch->status |= TCB_INT_SECE;
    at91_tcb_update_irq(s);
}

static bool at91_tcb_write_protected(AT91TCBState *s, AT91TCBChannel *ch,
                                     hwaddr offset)
{
    bool protected = false;
    hwaddr reg = offset < TCB_BCR ? offset % TCB_CHANNEL_STRIDE : offset;

    switch (reg) {
    case TCB_CCR:
    case TCB_BCR:
        protected = s->wpmr & TCB_WPMR_WPCREN;
        break;
    case TCB_IER:
    case TCB_IDR:
    case TCB_QIER:
    case TCB_QIDR:
        protected = s->wpmr & TCB_WPMR_WPITEN;
        break;
    case TCB_CMR:
    case TCB_SMMR:
    case TCB_RA:
    case TCB_RB:
    case TCB_RC:
    case TCB_EMR:
    case TCB_BMR:
        protected = s->wpmr & TCB_WPMR_WPEN;
        break;
    default:
        break;
    }

    if (protected) {
        at91_tcb_record_wp_violation(ch, offset);
    }
    return protected;
}

static uint64_t at91_tcb_channel_read(AT91TCBState *s, unsigned int index,
                                      hwaddr reg)
{
    AT91TCBChannel *ch = &s->channel[index];
    uint32_t value;

    switch (reg) {
    case TCB_CCR:
    case TCB_IER:
    case TCB_IDR:
        return 0;
    case TCB_CMR:
        return ch->cmr;
    case TCB_SMMR:
        return ch->smmr;
    case TCB_RAB:
        return 0;
    case TCB_CV:
        return at91_tcb_counter(ch);
    case TCB_RA:
        return ch->ra;
    case TCB_RB:
        return ch->rb;
    case TCB_RC:
        return ch->rc;
    case TCB_SR:
        value = at91_tcb_channel_status(ch);
        ch->status = 0;
        at91_tcb_update_irq(s);
        return value;
    case TCB_IMR:
        return ch->imr;
    case TCB_EMR:
        return ch->emr;
    case TCB_CSR:
        return at91_tcb_channel_status(ch) & TCB_SR_MIRROR_MASK;
    case TCB_SSR:
        value = ch->ssr;
        ch->ssr = 0;
        return value;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TCB ": read from bad channel offset 0x%"
                      HWADDR_PRIx "\n", reg);
        return 0;
    }
}

static uint64_t at91_tcb_read(void *opaque, hwaddr offset, unsigned int size)
{
    AT91TCBState *s = AT91_TCB(opaque);

    if (offset < TCB_BCR) {
        unsigned int index = offset / TCB_CHANNEL_STRIDE;
        hwaddr reg = offset % TCB_CHANNEL_STRIDE;

        if (index < ARRAY_SIZE(s->channel)) {
            return at91_tcb_channel_read(s, index, reg);
        }
    }

    switch (offset) {
    case TCB_BCR:
    case TCB_QIER:
    case TCB_QIDR:
        return 0;
    case TCB_BMR:
        return s->bmr;
    case TCB_QIMR:
        return s->qimr;
    case TCB_QISR: {
        uint32_t value = s->qisr;

        s->qisr = 0;
        at91_tcb_update_irq(s);
        return value;
    }
    case TCB_QSR:
        return s->qisr;
    case TCB_WPMR:
        return s->wpmr;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TCB ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_tcb_channel_write(AT91TCBState *s, unsigned int index,
                                   hwaddr reg, uint32_t value)
{
    AT91TCBChannel *ch = &s->channel[index];
    hwaddr offset = index * TCB_CHANNEL_STRIDE + reg;
    uint32_t counter;

    if (at91_tcb_write_protected(s, ch, offset)) {
        return;
    }

    switch (reg) {
    case TCB_CCR:
        if (value & TCB_CCR_CLKDIS) {
            at91_tcb_stop_channel(ch);
        } else if (value & TCB_CCR_SWTRG) {
            at91_tcb_start_channel(ch, true);
            at91_tcb_apply_tioa_effect(ch, TCB_CMR_ASWTRG_SHIFT);
        } else if (value & TCB_CCR_CLKEN) {
            at91_tcb_start_channel(ch, false);
        }
        break;
    case TCB_CMR:
        counter = at91_tcb_counter(ch);
        ch->cmr = value;
        at91_tcb_configure_channel(ch, counter);
        break;
    case TCB_SMMR:
        ch->smmr = value & TCB_SMMR_MASK;
        break;
    case TCB_RA:
        counter = at91_tcb_counter(ch);
        ch->ra = value;
        at91_tcb_configure_channel(ch, counter);
        break;
    case TCB_RB:
        counter = at91_tcb_counter(ch);
        ch->rb = value;
        at91_tcb_configure_channel(ch, counter);
        break;
    case TCB_RC:
        counter = at91_tcb_counter(ch);
        ch->rc = value;
        at91_tcb_configure_channel(ch, counter);
        break;
    case TCB_IER:
        ch->imr |= value & TCB_INT_MASK;
        at91_tcb_update_irq(s);
        break;
    case TCB_IDR:
        ch->imr &= ~(value & TCB_INT_MASK);
        at91_tcb_update_irq(s);
        break;
    case TCB_EMR:
        counter = at91_tcb_counter(ch);
        ch->emr = value & TCB_EMR_MASK;
        at91_tcb_configure_channel(ch, counter);
        break;
    case TCB_RAB:
    case TCB_CV:
    case TCB_SR:
    case TCB_IMR:
    case TCB_CSR:
    case TCB_SSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TCB ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TCB ": write to bad channel offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static void at91_tcb_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91TCBState *s = AT91_TCB(opaque);
    AT91TCBChannel *ch = &s->channel[0];
    unsigned int i;

    if (offset < TCB_BCR) {
        unsigned int index = offset / TCB_CHANNEL_STRIDE;
        hwaddr reg = offset % TCB_CHANNEL_STRIDE;

        if (index < ARRAY_SIZE(s->channel)) {
            at91_tcb_channel_write(s, index, reg, value);
            return;
        }
    }

    if (offset != TCB_WPMR &&
        at91_tcb_write_protected(s, ch, offset)) {
        return;
    }

    switch (offset) {
    case TCB_BCR:
        if (value & BIT(0)) {
            for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
                if (s->channel[i].enabled) {
                    at91_tcb_start_channel(&s->channel[i], true);
                }
            }
        }
        break;
    case TCB_BMR:
        s->bmr = value & 0x03f3ff3f;
        break;
    case TCB_QIER:
        s->qimr |= value & TCB_QINT_MASK;
        at91_tcb_update_irq(s);
        break;
    case TCB_QIDR:
        s->qimr &= ~(value & TCB_QINT_MASK);
        at91_tcb_update_irq(s);
        break;
    case TCB_WPMR:
        if ((value & TCB_WPMR_KEY_MASK) == TCB_WPMR_KEY) {
            s->wpmr = value & TCB_WPMR_ENABLE_MASK;
        }
        break;
    case TCB_QIMR:
    case TCB_QISR:
    case TCB_QSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TCB ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TCB ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_tcb_ops = {
    .read = at91_tcb_read,
    .write = at91_tcb_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_tcb_tick(void *opaque)
{
    AT91TCBChannel *ch = opaque;
    uint64_t boundary = ch->segment_end;
    bool period_end = boundary >= at91_tcb_period_end(ch);

    if (at91_tcb_compares_active(ch)) {
        if (ch->ra == boundary) {
            ch->status |= TCB_INT_CPAS;
            at91_tcb_apply_tioa_effect(ch, TCB_CMR_ACPA_SHIFT);
        }
        if (ch->rb == boundary) {
            ch->status |= TCB_INT_CPBS;
        }
    }

    if (period_end) {
        if (at91_tcb_auto_rc(ch)) {
            ch->status |= TCB_INT_CPCS;
            at91_tcb_apply_tioa_effect(ch, TCB_CMR_ACPC_SHIFT);
        } else {
            ch->status |= TCB_INT_COVFS;
        }
        if (ch->cmr & TCB_CMR_CPCDIS) {
            ch->enabled = false;
            ch->running = false;
        } else if (ch->cmr & TCB_CMR_CPCSTOP) {
            ch->running = false;
        }
    }

    /*
     * Arm the next segment: the period restarts at zero, else continue.
     * This runs inside the ptimer callback, which already holds the
     * transaction, so the segment is armed directly.
     */
    at91_tcb_arm_segment(ch, period_end ? 0 : boundary);
    at91_tcb_update_irq(ch->owner);
}

static void at91_tcb_clock_changed(void *opaque, ClockEvent event)
{
    AT91TCBState *s = AT91_TCB(opaque);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91TCBChannel *ch = &s->channel[i];
        bool active;

        if (!ch->timer) {
            continue;
        }
        active = at91_tcb_clock_active(ch);

        ptimer_transaction_begin(ch->timer);
        if (!active) {
            if (ch->running && !ch->clock_suspended) {
                ptimer_stop(ch->timer);
                ch->clock_suspended = true;
            }
        } else {
            at91_tcb_set_period(ch);
            if (ch->running && ch->clock_suspended) {
                /* Segments are one-shot; the tick handler arms the next. */
                ptimer_run(ch->timer, 1);
                ch->clock_suspended = false;
            }
        }
        ptimer_transaction_commit(ch->timer);
    }
}

static void at91_tcb_reset(DeviceState *dev)
{
    AT91TCBState *s = AT91_TCB(dev);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91TCBChannel *ch = &s->channel[i];

        ptimer_transaction_begin(ch->timer);
        ptimer_stop(ch->timer);
        ptimer_set_period(ch->timer, 1);
        ptimer_set_limit(ch->timer, TCB_COUNTER_RANGE, 1);
        ptimer_transaction_commit(ch->timer);
        ch->cmr = 0;
        ch->smmr = 0;
        ch->ra = 0;
        ch->rb = 0;
        ch->rc = 0;
        ch->status = 0;
        ch->imr = 0;
        ch->emr = 0;
        ch->ssr = 0;
        ch->enabled = false;
        ch->running = false;
        ch->clock_suspended = false;
        ch->segment_start = 0;
        ch->segment_end = TCB_COUNTER_RANGE;
        memset(ch->compare_request_level, 0,
               sizeof(ch->compare_request_level));
        ch->tioa_out = false;
        ch->tioa_in = false;
        ch->tiob_in = false;
        ch->capture_request_level = false;
        ch->etrg_request_level = false;
    }
    s->bmr = 0;
    s->qimr = 0;
    s->qisr = 0;
    s->wpmr = 0;
    at91_tcb_update_irq(s);
}

static void at91_tcb_init(Object *obj)
{
    AT91TCBState *s = AT91_TCB(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    unsigned int i;

    memory_region_init_io(&s->mmio, obj, &at91_tcb_ops, s,
                          TYPE_AT91_TCB, TCB_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(DEVICE(s), s->compare_request, "compare-request",
                             ARRAY_SIZE(s->compare_request));
    qdev_init_gpio_out_named(DEVICE(s), s->tioa, "tioa",
                             ARRAY_SIZE(s->tioa));
    qdev_init_gpio_out_named(DEVICE(s), s->capture_request,
                             "capture-request",
                             ARRAY_SIZE(s->capture_request));
    qdev_init_gpio_out_named(DEVICE(s), s->etrg_request, "etrg-request",
                             ARRAY_SIZE(s->etrg_request));
    qdev_init_gpio_in_named(DEVICE(s), at91_tcb_tioa_input, "tioa-in",
                            AT91_TCB_NUM_CHANNELS);
    qdev_init_gpio_in_named(DEVICE(s), at91_tcb_tiob_input, "tiob-in",
                            AT91_TCB_NUM_CHANNELS);

    s->pclk = qdev_init_clock_in(DEVICE(s), "pclk",
                                 at91_tcb_clock_changed, s, ClockUpdate);
    s->gclk = qdev_init_clock_in(DEVICE(s), "gclk",
                                 at91_tcb_clock_changed, s, ClockUpdate);
    s->slck = qdev_init_clock_in(DEVICE(s), "slck",
                                 at91_tcb_clock_changed, s, ClockUpdate);

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        s->channel[i].owner = s;
    }
}

static void at91_tcb_realize(DeviceState *dev, Error **errp)
{
    AT91TCBState *s = AT91_TCB(dev);
    unsigned int i;

    if (!clock_has_source(s->pclk) || !clock_has_source(s->gclk) ||
        !clock_has_source(s->slck)) {
        error_setg(errp, TYPE_AT91_TCB
                   ": pclk, gclk and slck clocks must be connected");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91TCBChannel *ch = &s->channel[i];

        ch->timer = ptimer_init(at91_tcb_tick, ch,
            PTIMER_POLICY_WRAP_AFTER_ONE_PERIOD |
            PTIMER_POLICY_TRIGGER_ONLY_ON_DECREMENT |
            PTIMER_POLICY_NO_IMMEDIATE_RELOAD |
            PTIMER_POLICY_NO_COUNTER_ROUND_DOWN);
        ptimer_transaction_begin(ch->timer);
        ptimer_set_period(ch->timer, 1);
        ptimer_set_limit(ch->timer, TCB_COUNTER_RANGE, 1);
        ptimer_transaction_commit(ch->timer);
    }
}

static void at91_tcb_finalize(Object *obj)
{
    AT91TCBState *s = AT91_TCB(obj);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        if (s->channel[i].timer) {
            ptimer_free(s->channel[i].timer);
        }
    }
}

static const VMStateDescription at91_tcb_channel_vmstate = {
    .name = TYPE_AT91_TCB "/channel",
    .version_id = 3,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_PTIMER(timer, AT91TCBChannel),
        VMSTATE_UINT32(cmr, AT91TCBChannel),
        VMSTATE_UINT32(smmr, AT91TCBChannel),
        VMSTATE_UINT32(ra, AT91TCBChannel),
        VMSTATE_UINT32(rb, AT91TCBChannel),
        VMSTATE_UINT32(rc, AT91TCBChannel),
        VMSTATE_UINT32(status, AT91TCBChannel),
        VMSTATE_UINT32(imr, AT91TCBChannel),
        VMSTATE_UINT32(emr, AT91TCBChannel),
        VMSTATE_UINT32(ssr, AT91TCBChannel),
        VMSTATE_BOOL(enabled, AT91TCBChannel),
        VMSTATE_BOOL(running, AT91TCBChannel),
        VMSTATE_BOOL(clock_suspended, AT91TCBChannel),
        VMSTATE_UINT64_V(segment_start, AT91TCBChannel, 2),
        VMSTATE_UINT64_V(segment_end, AT91TCBChannel, 2),
        VMSTATE_BOOL_ARRAY_V(compare_request_level, AT91TCBChannel, 3, 2),
        VMSTATE_BOOL_V(tioa_out, AT91TCBChannel, 3),
        VMSTATE_BOOL_V(tioa_in, AT91TCBChannel, 3),
        VMSTATE_BOOL_V(tiob_in, AT91TCBChannel, 3),
        VMSTATE_BOOL_V(capture_request_level, AT91TCBChannel, 3),
        VMSTATE_BOOL_V(etrg_request_level, AT91TCBChannel, 3),
        VMSTATE_END_OF_LIST()
    },
};

static int at91_tcb_post_load(void *opaque, int version_id)
{
    AT91TCBState *s = opaque;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91TCBChannel *ch = &s->channel[i];

        /*
         * A version-1 channel ran one segment covering the whole period, and
         * any stream can be repaired to that shape without losing the count.
         */
        if (ch->segment_end <= ch->segment_start ||
            ch->segment_end > TCB_COUNTER_RANGE) {
            ch->segment_start = 0;
            ch->segment_end = at91_tcb_period_end(ch);
        }
    }
    at91_tcb_update_irq(s);
    return 0;
}

static const VMStateDescription at91_tcb_vmstate = {
    .name = TYPE_AT91_TCB,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_tcb_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, AT91TCBState),
        VMSTATE_CLOCK(gclk, AT91TCBState),
        VMSTATE_CLOCK(slck, AT91TCBState),
        VMSTATE_STRUCT_ARRAY(channel, AT91TCBState, AT91_TCB_NUM_CHANNELS,
                             1, at91_tcb_channel_vmstate, AT91TCBChannel),
        VMSTATE_UINT32(bmr, AT91TCBState),
        VMSTATE_UINT32(qimr, AT91TCBState),
        VMSTATE_UINT32(qisr, AT91TCBState),
        VMSTATE_UINT32(wpmr, AT91TCBState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_tcb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Timer Counter Block";
    dc->realize = at91_tcb_realize;
    dc->vmsd = &at91_tcb_vmstate;
    device_class_set_legacy_reset(dc, at91_tcb_reset);
}

static const TypeInfo at91_tcb_info = {
    .name = TYPE_AT91_TCB,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91TCBState),
    .instance_init = at91_tcb_init,
    .instance_finalize = at91_tcb_finalize,
    .class_init = at91_tcb_class_init,
};

static void at91_tcb_register_types(void)
{
    type_register_static(&at91_tcb_info);
}

type_init(at91_tcb_register_types)
