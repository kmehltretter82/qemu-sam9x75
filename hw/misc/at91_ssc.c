/*
 * Microchip AT91 Synchronous Serial Controller
 *
 * A register-level model of the SAM9X7 SSC: enable and reset control,
 * the transmit and receive holding registers with their status flags and
 * interrupts, loop mode, write protection and the two XDMAC request lines.
 * Frame timing, the sync and compare units and the external TK/TF/RK/RF
 * pins are not modeled, so only loop mode moves data.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/misc/at91_ssc.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/ptimer.h"
#include "migration/vmstate.h"
#include "qemu/log.h"

enum {
    SSC_CR      = 0x00,
    SSC_CMR     = 0x04,
    SSC_RCMR    = 0x10,
    SSC_RFMR    = 0x14,
    SSC_TCMR    = 0x18,
    SSC_TFMR    = 0x1c,
    SSC_RHR     = 0x20,
    SSC_THR     = 0x24,
    SSC_RSHR    = 0x30,
    SSC_TSHR    = 0x34,
    SSC_RC0R    = 0x38,
    SSC_RC1R    = 0x3c,
    SSC_SR      = 0x40,
    SSC_IER     = 0x44,
    SSC_IDR     = 0x48,
    SSC_IMR     = 0x4c,
    SSC_WPMR    = 0xe4,
    SSC_WPSR    = 0xe8,
};

#define SSC_CR_RXEN         BIT(0)
#define SSC_CR_RXDIS        BIT(1)
#define SSC_CR_TXEN         BIT(8)
#define SSC_CR_TXDIS        BIT(9)
#define SSC_CR_SWRST        BIT(15)

#define SSC_SR_TXRDY        BIT(0)
#define SSC_SR_TXEMPTY      BIT(1)
#define SSC_SR_RXRDY        BIT(4)
#define SSC_SR_OVRUN        BIT(5)
#define SSC_SR_TXSYN        BIT(10)
#define SSC_SR_RXSYN        BIT(11)
#define SSC_SR_TXEN         BIT(16)
#define SSC_SR_RXEN         BIT(17)
#define SSC_SR_INT_MASK     0x00000f3f

#define SSC_CMR_DIV_MASK    0x00000fff
#define SSC_RFMR_LOOP       BIT(5)
#define SSC_RFMR_DATLEN(v)  (((v) & 0x1f) + 1)
/* DATNB gives the words per frame, which is DATNB + 1. */
#define SSC_FMR_DATNB(v)    ((((v) >> 8) & 0xf) + 1)
/* FSLEN gives the comparison pattern length, which is FSLEN + 1 bits. */
#define SSC_FMR_FSLEN(v)    ((((v) >> 16) & 0xf) + 1)
#define SSC_SR_CP0          BIT(8)
#define SSC_SR_CP1          BIT(9)

#define SSC_WPMR_WPEN       BIT(0)
#define SSC_WPMR_KEY        0x53534300  /* "SSC" */
#define SSC_WPMR_KEY_MASK   0xffffff00

static void at91_ssc_set_request(qemu_irq irq, bool *old, bool level)
{
    if (*old != level) {
        *old = level;
        qemu_set_irq(irq, level);
    }
}

static void at91_ssc_update(AT91SSCState *s)
{
    bool clocked = clock_is_enabled(s->pclk);

    s->status &= ~(SSC_SR_TXRDY | SSC_SR_TXEMPTY | SSC_SR_RXRDY |
                   SSC_SR_TXEN | SSC_SR_RXEN);
    if (s->tx_enabled) {
        s->status |= SSC_SR_TXEN;
        if (!s->thr_full) {
            s->status |= SSC_SR_TXRDY | SSC_SR_TXEMPTY;
        }
    } else {
        s->status |= SSC_SR_TXEMPTY;
    }
    if (s->rx_enabled) {
        s->status |= SSC_SR_RXEN;
    }
    if (s->rhr_full) {
        s->status |= SSC_SR_RXRDY;
    }

    /* A gated peripheral clock stops the controller requesting service. */
    at91_ssc_set_request(s->tx_request, &s->tx_request_level,
                         clocked && (s->status & SSC_SR_TXRDY));
    at91_ssc_set_request(s->rx_request, &s->rx_request_level,
                         clocked && (s->status & SSC_SR_RXRDY));
    qemu_set_irq(s->irq, clocked && (s->status & s->imr & SSC_SR_INT_MASK));
}

/*
 * DS60001813E: the divided clock is the peripheral clock over 2 x DIV, and
 * DIV 0 leaves the divider inactive.  A word therefore occupies DATLEN bit
 * periods, which is 2 x DIV x DATLEN peripheral clock cycles.
 */
static uint64_t at91_ssc_word_ticks(const AT91SSCState *s)
{
    unsigned int div = s->cmr & SSC_CMR_DIV_MASK;

    if (!div) {
        return 0;
    }
    return 2ULL * div * SSC_RFMR_DATLEN(s->rfmr);
}

/*
 * Loop mode ties TD back to RD, so a word shifted out is received.  Without
 * it there is no modeled serial partner and the word is simply shifted out.
 */
/*
 * A frame carries DATNB + 1 words; the sync status is reported once the
 * last of them has gone by, and is cleared by reading the status register.
 */
static void at91_ssc_advance_frame(uint8_t *word, unsigned int per_frame,
                                   uint32_t *status, uint32_t flag)
{
    if (++*word >= per_frame) {
        *word = 0;
        *status |= flag;
    }
}

/*
 * DS60001813E 56.8.6.1: the compare patterns are FSLEN + 1 bits wide and
 * are matched against the last bits received.  This model works a word at
 * a time rather than sampling a continuous bit stream, so the comparison
 * is against the low FSLEN + 1 bits of each received word; that is exact
 * when the pattern is no wider than a word, which is how the start
 * conditions use it.
 */
static void at91_ssc_compare(AT91SSCState *s, uint32_t data)
{
    unsigned int bits = SSC_FMR_FSLEN(s->rfmr);
    uint32_t mask = MAKE_64BIT_MASK(0, bits);

    if ((data & mask) == (s->rc0r & mask)) {
        s->status |= SSC_SR_CP0;
    }
    if ((data & mask) == (s->rc1r & mask)) {
        s->status |= SSC_SR_CP1;
    }
}

static void at91_ssc_complete_word(AT91SSCState *s)
{
    unsigned int bits = SSC_RFMR_DATLEN(s->rfmr);
    uint32_t data = s->thr & MAKE_64BIT_MASK(0, bits);

    s->thr_full = false;
    at91_ssc_advance_frame(&s->tx_frame_word, SSC_FMR_DATNB(s->tfmr),
                           &s->status, SSC_SR_TXSYN);
    if (!(s->rfmr & SSC_RFMR_LOOP) || !s->rx_enabled) {
        return;
    }
    if (s->rhr_full) {
        s->status |= SSC_SR_OVRUN;
    }
    s->rhr = data;
    s->rhr_full = true;
    at91_ssc_advance_frame(&s->rx_frame_word, SSC_FMR_DATNB(s->rfmr),
                           &s->status, SSC_SR_RXSYN);
    at91_ssc_compare(s, data);
}

static void at91_ssc_shift_done(void *opaque)
{
    AT91SSCState *s = opaque;

    at91_ssc_complete_word(s);
    at91_ssc_update(s);
}

static void at91_ssc_transmit(AT91SSCState *s, uint32_t value)
{
    uint64_t ticks = at91_ssc_word_ticks(s);

    s->thr = value;
    if (!s->tx_enabled) {
        return;
    }
    if (!ticks) {
        /*
         * With the divider inactive there is no bit clock, so the word is
         * held in the holding register and never shifted -- which is what
         * the hardware does, and why a driver must program CMR.
         */
        s->thr_full = true;
        return;
    }
    s->thr_full = true;
    ptimer_transaction_begin(s->shifter);
    ptimer_stop(s->shifter);
    ptimer_set_limit(s->shifter, ticks, 1);
    ptimer_run(s->shifter, 1);
    ptimer_transaction_commit(s->shifter);
}

static void at91_ssc_soft_reset(AT91SSCState *s)
{
    if (s->shifter) {
        ptimer_transaction_begin(s->shifter);
        ptimer_stop(s->shifter);
        ptimer_transaction_commit(s->shifter);
    }
    s->cmr = 0;
    s->rcmr = 0;
    s->rfmr = 0;
    s->tcmr = 0;
    s->tfmr = 0;
    s->rhr = 0;
    s->thr = 0;
    s->rshr = 0;
    s->tshr = 0;
    s->rc0r = 0;
    s->rc1r = 0;
    s->status = 0;
    s->imr = 0;
    s->rx_enabled = false;
    s->tx_enabled = false;
    s->thr_full = false;
    s->rhr_full = false;
    s->tx_frame_word = 0;
    s->rx_frame_word = 0;
}

static bool at91_ssc_write_protected(AT91SSCState *s, hwaddr offset)
{
    if (!(s->wpmr & SSC_WPMR_WPEN)) {
        return false;
    }
    switch (offset) {
    case SSC_CMR:
    case SSC_RCMR:
    case SSC_RFMR:
    case SSC_TCMR:
    case SSC_TFMR:
    case SSC_RC0R:
    case SSC_RC1R:
        s->wpsr = ((offset & 0xffff) << 8) | 1;
        return true;
    default:
        return false;
    }
}

static uint64_t at91_ssc_read(void *opaque, hwaddr offset, unsigned int size)
{
    AT91SSCState *s = opaque;
    uint32_t value;

    switch (offset) {
    case SSC_CMR:
        return s->cmr;
    case SSC_RCMR:
        return s->rcmr;
    case SSC_RFMR:
        return s->rfmr;
    case SSC_TCMR:
        return s->tcmr;
    case SSC_TFMR:
        return s->tfmr;
    case SSC_RHR:
        value = s->rhr;
        s->rhr_full = false;
        at91_ssc_update(s);
        return value;
    case SSC_RSHR:
        return s->rshr;
    case SSC_TSHR:
        return s->tshr;
    case SSC_RC0R:
        return s->rc0r;
    case SSC_RC1R:
        return s->rc1r;
    case SSC_SR:
        value = s->status;
        /* OVRUN and the sync reports are cleared by this read. */
        s->status &= ~(SSC_SR_OVRUN | SSC_SR_TXSYN | SSC_SR_RXSYN |
                       SSC_SR_CP0 | SSC_SR_CP1);
        at91_ssc_update(s);
        return value;
    case SSC_IMR:
        return s->imr;
    case SSC_WPMR:
        return s->wpmr;
    case SSC_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case SSC_CR:
    case SSC_THR:
    case SSC_IER:
    case SSC_IDR:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_SSC, offset);
        return 0;
    }
}

static void at91_ssc_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91SSCState *s = opaque;
    uint32_t v = value;

    if (at91_ssc_write_protected(s, offset)) {
        at91_ssc_update(s);
        return;
    }

    switch (offset) {
    case SSC_CR:
        if (v & SSC_CR_SWRST) {
            at91_ssc_soft_reset(s);
            break;
        }
        if (v & SSC_CR_RXDIS) {
            s->rx_enabled = false;
        } else if (v & SSC_CR_RXEN) {
            s->rx_enabled = true;
        }
        if (v & SSC_CR_TXDIS) {
            s->tx_enabled = false;
        } else if (v & SSC_CR_TXEN) {
            s->tx_enabled = true;
        }
        break;
    case SSC_CMR:
        s->cmr = v & SSC_CMR_DIV_MASK;
        break;
    case SSC_RCMR:
        s->rcmr = v;
        break;
    case SSC_RFMR:
        s->rfmr = v;
        break;
    case SSC_TCMR:
        s->tcmr = v;
        break;
    case SSC_TFMR:
        s->tfmr = v;
        break;
    case SSC_THR:
        at91_ssc_transmit(s, v);
        break;
    case SSC_TSHR:
        s->tshr = v;
        break;
    case SSC_RC0R:
        s->rc0r = v;
        break;
    case SSC_RC1R:
        s->rc1r = v;
        break;
    case SSC_IER:
        s->imr |= v & SSC_SR_INT_MASK;
        break;
    case SSC_IDR:
        s->imr &= ~(v & SSC_SR_INT_MASK);
        break;
    case SSC_WPMR:
        if ((v & SSC_WPMR_KEY_MASK) == SSC_WPMR_KEY) {
            s->wpmr = v & SSC_WPMR_WPEN;
        }
        break;
    case SSC_RHR:
    case SSC_RSHR:
    case SSC_SR:
    case SSC_IMR:
    case SSC_WPSR:
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_SSC, offset);
        break;
    }
    at91_ssc_update(s);
}

static const MemoryRegionOps at91_ssc_ops = {
    .read = at91_ssc_read,
    .write = at91_ssc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_ssc_clock_changed(void *opaque, ClockEvent event)
{
    AT91SSCState *s = AT91_SSC(opaque);

    if (s->shifter && clock_is_enabled(s->pclk)) {
        ptimer_transaction_begin(s->shifter);
        ptimer_set_period_from_clock(s->shifter, s->pclk, 1);
        ptimer_transaction_commit(s->shifter);
    }
    at91_ssc_update(s);
}

static void at91_ssc_reset_hold(Object *obj, ResetType type)
{
    AT91SSCState *s = AT91_SSC(obj);

    at91_ssc_soft_reset(s);
    s->wpmr = 0;
    s->wpsr = 0;
    s->tx_request_level = false;
    s->rx_request_level = false;
    at91_ssc_update(s);
}

static int at91_ssc_post_load(void *opaque, int version_id)
{
    AT91SSCState *s = opaque;

    /* Request levels are derived; re-drive them from the loaded flags. */
    s->tx_request_level = false;
    s->rx_request_level = false;
    at91_ssc_update(s);
    return 0;
}

static const VMStateDescription at91_ssc_vmstate = {
    .name = TYPE_AT91_SSC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_ssc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cmr, AT91SSCState),
        VMSTATE_UINT32(rcmr, AT91SSCState),
        VMSTATE_UINT32(rfmr, AT91SSCState),
        VMSTATE_UINT32(tcmr, AT91SSCState),
        VMSTATE_UINT32(tfmr, AT91SSCState),
        VMSTATE_UINT32(rhr, AT91SSCState),
        VMSTATE_UINT32(thr, AT91SSCState),
        VMSTATE_UINT32(rshr, AT91SSCState),
        VMSTATE_UINT32(tshr, AT91SSCState),
        VMSTATE_UINT32(rc0r, AT91SSCState),
        VMSTATE_UINT32(rc1r, AT91SSCState),
        VMSTATE_UINT32(status, AT91SSCState),
        VMSTATE_UINT32(imr, AT91SSCState),
        VMSTATE_UINT32(wpmr, AT91SSCState),
        VMSTATE_UINT32(wpsr, AT91SSCState),
        VMSTATE_BOOL(rx_enabled, AT91SSCState),
        VMSTATE_BOOL(tx_enabled, AT91SSCState),
        VMSTATE_BOOL(thr_full, AT91SSCState),
        VMSTATE_BOOL(rhr_full, AT91SSCState),
        VMSTATE_UINT8(tx_frame_word, AT91SSCState),
        VMSTATE_UINT8(rx_frame_word, AT91SSCState),
        VMSTATE_PTIMER(shifter, AT91SSCState),
        VMSTATE_CLOCK(pclk, AT91SSCState),
        VMSTATE_CLOCK(gclk, AT91SSCState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_ssc_init(Object *obj)
{
    AT91SSCState *s = AT91_SSC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_ssc_ops, s,
                          TYPE_AT91_SSC, 0x4000);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    s->shifter = ptimer_init(at91_ssc_shift_done, s, PTIMER_POLICY_LEGACY);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_ssc_clock_changed, s,
                                 ClockUpdate);
    s->gclk = qdev_init_clock_in(dev, "gclk", at91_ssc_clock_changed, s,
                                 ClockUpdate);
}

static void at91_ssc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "AT91 Synchronous Serial Controller";
    dc->vmsd = &at91_ssc_vmstate;
    rc->phases.hold = at91_ssc_reset_hold;
}

static const TypeInfo at91_ssc_types[] = {
    {
        .name          = TYPE_AT91_SSC,
        .parent        = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(AT91SSCState),
        .instance_init = at91_ssc_init,
        .class_init    = at91_ssc_class_init,
    },
};

DEFINE_TYPES(at91_ssc_types)
