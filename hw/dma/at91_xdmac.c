/*
 * Microchip AT91 Extensible DMA Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/dma/at91_xdmac.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "system/dma.h"

#define XDMAC_GTYPE             0x00
#define XDMAC_GCFG              0x04
#define XDMAC_GWAC              0x08
#define XDMAC_GIE               0x0c
#define XDMAC_GID               0x10
#define XDMAC_GIM               0x14
#define XDMAC_GIS               0x18
#define XDMAC_GE                0x1c
#define XDMAC_GD                0x20
#define XDMAC_GS                0x24
#define XDMAC_GRS               0x28
#define XDMAC_GWS               0x2c
#define XDMAC_GRWS              0x30
#define XDMAC_GRWR              0x34
#define XDMAC_GSWR              0x38
#define XDMAC_GSWS              0x3c
#define XDMAC_GSWF              0x40

#define XDMAC_CHANNEL_BASE      0x50
#define XDMAC_CHANNEL_STRIDE    0x40
#define XDMAC_CIE               0x00
#define XDMAC_CID               0x04
#define XDMAC_CIM               0x08
#define XDMAC_CIS               0x0c
#define XDMAC_CSA               0x10
#define XDMAC_CDA               0x14
#define XDMAC_CNDA              0x18
#define XDMAC_CNDC              0x1c
#define XDMAC_CUBC              0x20
#define XDMAC_CBC               0x24
#define XDMAC_CC                0x28
#define XDMAC_CDS_MSP           0x2c
#define XDMAC_CSUS              0x30
#define XDMAC_CDUS              0x34

#define XDMAC_VERSION           0xffc
#define XDMAC_MMIO_SIZE         0x1000

#define XDMAC_CHANNEL_MASK      MAKE_64BIT_MASK(0, AT91_XDMAC_NUM_CHANNELS)
#define XDMAC_REQUEST_MASK      MAKE_64BIT_MASK(0, AT91_XDMAC_NUM_REQUESTS)

/* The 4 KiB multiport FIFO provides 256 bytes to each of 16 channels. */
#define XDMAC_GTYPE_VALUE \
    (((AT91_XDMAC_NUM_REQUESTS - 1) << 16) | \
     (AT91_XDMAC_FIFO_BYTES_PER_CHANNEL << 5) | \
     (AT91_XDMAC_NUM_CHANNELS - 1))

#define XDMAC_GCFG_MASK         (BIT(8) | 0xf)
#define XDMAC_GWAC_MASK         0xffff

#define XDMAC_CIS_BIS           BIT(0)
#define XDMAC_CIS_LIS           BIT(1)
#define XDMAC_CIS_DIS           BIT(2)
#define XDMAC_CIS_FIS           BIT(3)
#define XDMAC_CIS_RBEIS         BIT(4)
#define XDMAC_CIS_WBEIS         BIT(5)
#define XDMAC_CIS_ROIS          BIT(6)
#define XDMAC_CIS_MASK          0x7f

#define XDMAC_CNDA_MASK         0xfffffffd
#define XDMAC_CNDC_NDE          BIT(0)
#define XDMAC_CNDC_NDSUP        BIT(1)
#define XDMAC_CNDC_NDDUP        BIT(2)
#define XDMAC_CNDC_NDVIEW_SHIFT 3
#define XDMAC_CNDC_NDVIEW_MASK  (3U << XDMAC_CNDC_NDVIEW_SHIFT)
#define XDMAC_CNDC_MASK         0x7f
#define XDMAC_CUBC_MASK         0x00ffffff
#define XDMAC_CBC_MASK          0x00000fff

#define XDMAC_CC_TYPE           BIT(0)
#define XDMAC_CC_MBSIZE_MASK    (3U << 1)
#define XDMAC_CC_DSYNC          BIT(4)
#define XDMAC_CC_SWREQ          BIT(6)
#define XDMAC_CC_MEMSET         BIT(7)
#define XDMAC_CC_CSIZE_SHIFT    8
#define XDMAC_CC_CSIZE_MASK     (7U << XDMAC_CC_CSIZE_SHIFT)
#define XDMAC_CC_DWIDTH_SHIFT   11
#define XDMAC_CC_DWIDTH_MASK    (3U << XDMAC_CC_DWIDTH_SHIFT)
#define XDMAC_CC_SIF            BIT(13)
#define XDMAC_CC_DIF            BIT(14)
#define XDMAC_CC_SAM_SHIFT      16
#define XDMAC_CC_SAM_MASK       (3U << XDMAC_CC_SAM_SHIFT)
#define XDMAC_CC_DAM_SHIFT      18
#define XDMAC_CC_DAM_MASK       (3U << XDMAC_CC_DAM_SHIFT)
#define XDMAC_CC_INITD          BIT(21)
#define XDMAC_CC_RDIP           BIT(22)
#define XDMAC_CC_WRIP           BIT(23)
#define XDMAC_CC_PERID_SHIFT    24
#define XDMAC_CC_PERID_MASK     (0x7fU << XDMAC_CC_PERID_SHIFT)
#define XDMAC_CC_WRITABLE_MASK \
    (XDMAC_CC_TYPE | XDMAC_CC_MBSIZE_MASK | XDMAC_CC_DSYNC | \
     XDMAC_CC_SWREQ | XDMAC_CC_MEMSET | XDMAC_CC_CSIZE_MASK | \
     XDMAC_CC_DWIDTH_MASK | XDMAC_CC_SIF | XDMAC_CC_DIF | \
     XDMAC_CC_SAM_MASK | XDMAC_CC_DAM_MASK | XDMAC_CC_PERID_MASK)

#define XDMAC_AM_FIXED          0
#define XDMAC_AM_INCREMENTED    1
#define XDMAC_AM_UBS            2
#define XDMAC_AM_UBS_DS         3

#define XDMAC_BH_WORK_LIMIT     4096
/* Bound work so a continuously runnable channel cannot starve the others. */
#define XDMAC_CHANNEL_WORK_QUANTUM 16

typedef struct AT91XDMACCycleDetector {
    uint32_t anchor;
    uint32_t power;
    uint32_t distance;
    bool seen;
} AT91XDMACCycleDetector;

typedef struct AT91XDMACBHRunState {
    AT91XDMACCycleDetector cycle[AT91_XDMAC_NUM_CHANNELS];
    uint32_t yielded;
} AT91XDMACBHRunState;

static uint32_t at91_xdmac_global_status(AT91XDMACState *s)
{
    uint32_t status = 0;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        if (s->channel[i].enabled) {
            status |= BIT(i);
        }
    }
    return status;
}

static uint32_t at91_xdmac_global_irq_status(AT91XDMACState *s)
{
    uint32_t status = 0;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91XDMACChannel *ch = &s->channel[i];

        if (ch->cis & ch->cim) {
            status |= BIT(i);
        }
    }
    return status;
}

static void at91_xdmac_update_irq(AT91XDMACState *s)
{
    qemu_set_irq(s->irq, !!(at91_xdmac_global_irq_status(s) & s->gim));
}

static unsigned int at91_xdmac_perid(const AT91XDMACChannel *ch)
{
    return (ch->cc & XDMAC_CC_PERID_MASK) >> XDMAC_CC_PERID_SHIFT;
}

static bool at91_xdmac_is_peripheral(const AT91XDMACChannel *ch)
{
    return ch->cc & XDMAC_CC_TYPE;
}

static bool at91_xdmac_is_source_peripheral(const AT91XDMACChannel *ch)
{
    return at91_xdmac_is_peripheral(ch) && !(ch->cc & XDMAC_CC_DSYNC);
}

static bool at91_xdmac_uses_p2m_fifo(const AT91XDMACChannel *ch)
{
    return ch->enabled && at91_xdmac_is_source_peripheral(ch) &&
           !(ch->cc & XDMAC_CC_MEMSET);
}

static unsigned int at91_xdmac_data_width(const AT91XDMACChannel *ch)
{
    unsigned int dwidth = (ch->cc & XDMAC_CC_DWIDTH_MASK) >>
                          XDMAC_CC_DWIDTH_SHIFT;

    return dwidth <= 2 ? 1U << dwidth : 0;
}

static unsigned int at91_xdmac_memory_burst_bytes(
    const AT91XDMACChannel *ch)
{
    unsigned int mbsize = (ch->cc & XDMAC_CC_MBSIZE_MASK) >> 1;
    unsigned int data = mbsize ? 1U << (mbsize + 1) : 1;

    return data * at91_xdmac_data_width(ch);
}

static void at91_xdmac_fifo_reset(AT91XDMACChannel *ch)
{
    ch->fifo_head = 0;
    ch->fifo_tail = 0;
    ch->fifo_count = 0;
    ch->write_burst_remaining = 0;
    ch->flush_remaining = 0;
    ch->read_in_progress = false;
    ch->write_in_progress = false;
    ch->flush_pending = false;
    ch->disable_pending = false;
}

static void at91_xdmac_fifo_push(AT91XDMACChannel *ch,
                                 const uint8_t *data, unsigned int size)
{
    unsigned int first = MIN(size, AT91_XDMAC_FIFO_BYTES_PER_CHANNEL -
                                   ch->fifo_tail);

    g_assert(size <= AT91_XDMAC_FIFO_BYTES_PER_CHANNEL - ch->fifo_count);
    memcpy(&ch->fifo[ch->fifo_tail], data, first);
    memcpy(ch->fifo, data + first, size - first);
    ch->fifo_tail = (ch->fifo_tail + size) %
                    AT91_XDMAC_FIFO_BYTES_PER_CHANNEL;
    ch->fifo_count += size;
}

static void at91_xdmac_fifo_peek(const AT91XDMACChannel *ch,
                                 uint8_t *data, unsigned int size)
{
    unsigned int first = MIN(size, AT91_XDMAC_FIFO_BYTES_PER_CHANNEL -
                                   ch->fifo_head);

    g_assert(size <= ch->fifo_count);
    memcpy(data, &ch->fifo[ch->fifo_head], first);
    memcpy(data + first, ch->fifo, size - first);
}

static void at91_xdmac_fifo_pop(AT91XDMACChannel *ch, unsigned int size)
{
    g_assert(size <= ch->fifo_count);
    ch->fifo_head = (ch->fifo_head + size) %
                    AT91_XDMAC_FIFO_BYTES_PER_CHANNEL;
    ch->fifo_count -= size;
}

static bool at91_xdmac_has_request(AT91XDMACState *s,
                                   AT91XDMACChannel *ch,
                                   unsigned int index)
{
    unsigned int perid;

    if (ch->request_remaining) {
        return true;
    }
    if (ch->cc & XDMAC_CC_SWREQ) {
        return s->sw_requests & BIT(index);
    }

    perid = at91_xdmac_perid(ch);
    return perid < AT91_XDMAC_NUM_REQUESTS &&
           (s->request_level & BIT_ULL(perid));
}

static bool at91_xdmac_fifo_can_read(AT91XDMACState *s,
                                     AT91XDMACChannel *ch,
                                     unsigned int index,
                                     unsigned int width)
{
    if (ch->disable_pending || !ch->read_ubc ||
        ch->fifo_count + width > AT91_XDMAC_FIFO_BYTES_PER_CHANNEL) {
        return false;
    }

    /* An accepted chunk is allowed to finish after a read suspend. */
    if (!ch->request_remaining && (s->grs & BIT(index))) {
        return false;
    }
    return at91_xdmac_has_request(s, ch, index);
}

static bool at91_xdmac_fifo_can_write(AT91XDMACState *s,
                                      AT91XDMACChannel *ch,
                                      unsigned int index,
                                      unsigned int width)
{
    if (ch->fifo_count < width) {
        return false;
    }

    /* Already scheduled bus writes finish even if GWS is asserted later. */
    if (ch->write_burst_remaining) {
        return true;
    }

    /* Disable and flush override write suspend so pending RX data drains. */
    if (ch->disable_pending || ch->flush_pending) {
        return true;
    }
    if (s->gws & BIT(index)) {
        return false;
    }

    return ch->fifo_count >= at91_xdmac_memory_burst_bytes(ch) ||
           !ch->read_ubc;
}

static bool at91_xdmac_channel_runnable(AT91XDMACState *s,
                                        unsigned int index)
{
    AT91XDMACChannel *ch = &s->channel[index];
    unsigned int width;

    if (!ch->enabled || ch->error_stalled || !clock_get_hz(s->pclk)) {
        return false;
    }
    if (at91_xdmac_uses_p2m_fifo(ch)) {
        if (ch->disable_pending) {
            return true;
        }
        /* Descriptor fetch and boundary state must honor an earlier pause. */
        if (ch->needs_fetch) {
            return !((s->grs | s->gws) & BIT(index));
        }
        if (!ch->cubc) {
            return !((s->grs | s->gws) & BIT(index));
        }
        width = at91_xdmac_data_width(ch);
        if (!width) {
            return true;
        }
        return at91_xdmac_fifo_can_write(s, ch, index, width) ||
               at91_xdmac_fifo_can_read(s, ch, index, width);
    }

    /* The legacy atomic path cannot stop between its read and write. */
    if ((s->grs | s->gws) & BIT(index)) {
        return false;
    }
    if (ch->needs_fetch || !ch->cubc) {
        return true;
    }
    return !at91_xdmac_is_peripheral(ch) ||
           at91_xdmac_has_request(s, ch, index);
}

static void at91_xdmac_schedule(AT91XDMACState *s)
{
    unsigned int i;

    if (!s->bh) {
        return;
    }
    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        if (at91_xdmac_channel_runnable(s, i)) {
            qemu_bh_schedule(s->bh);
            return;
        }
    }
}

static void at91_xdmac_set_error(AT91XDMACState *s,
                                 AT91XDMACChannel *ch,
                                 unsigned int index, uint32_t error)
{
    ch->cis |= error;
    ch->error_stalled = true;
    ch->request_remaining = 0;
    ch->read_in_progress = false;
    ch->write_in_progress = false;
    ch->write_burst_remaining = 0;
    s->sw_requests &= ~BIT(index);
    at91_xdmac_update_irq(s);
}

static bool at91_xdmac_fetch_descriptor(AT91XDMACState *s,
                                        AT91XDMACChannel *ch,
                                        unsigned int index)
{
    uint32_t words[9];
    uint32_t control = ch->cndc;
    uint32_t ubc;
    unsigned int view = (control & XDMAC_CNDC_NDVIEW_MASK) >>
                        XDMAC_CNDC_NDVIEW_SHIFT;
    unsigned int count;
    unsigned int i;
    MemTxResult result;

    /* Descriptor-controlled parameters may change only at an empty boundary. */
    g_assert(!ch->fifo_count);
    g_assert(!ch->write_burst_remaining);
    g_assert(!ch->flush_remaining);
    g_assert(!ch->flush_pending);
    g_assert(!ch->disable_pending);
    g_assert(!ch->request_remaining);

    switch (view) {
    case 0:
        count = 3;
        break;
    case 1:
        count = 4;
        break;
    case 2:
        count = 5;
        break;
    default:
        count = 9;
        break;
    }

    result = dma_memory_read(&s->dma_as, ch->cnda & ~3U, words,
                             count * sizeof(words[0]),
                             MEMTXATTRS_UNSPECIFIED);
    if (result != MEMTX_OK) {
        at91_xdmac_set_error(s, ch, index, XDMAC_CIS_RBEIS);
        return false;
    }
    for (i = 0; i < count; i++) {
        words[i] = le32_to_cpu(words[i]);
    }

    ubc = words[1];
    if (view == 0) {
        if (control & XDMAC_CNDC_NDSUP) {
            ch->csa = words[2];
        }
        if (control & XDMAC_CNDC_NDDUP) {
            ch->cda = words[2];
        }
    } else {
        if (control & XDMAC_CNDC_NDSUP) {
            ch->csa = words[2];
        }
        if (control & XDMAC_CNDC_NDDUP) {
            ch->cda = words[3];
        }
    }
    if (view >= 2) {
        ch->cc = words[4] & XDMAC_CC_WRITABLE_MASK;
    }
    if (view == 3) {
        ch->cbc = words[5] & XDMAC_CBC_MASK;
        ch->cds_msp = words[6];
        ch->csus = words[7] & XDMAC_CUBC_MASK;
        ch->cdus = words[8] & XDMAC_CUBC_MASK;
    }

    ch->cnda = words[0] & XDMAC_CNDA_MASK;
    ch->cndc = (ubc >> 24) & XDMAC_CNDC_MASK;
    ch->cubc = ubc & XDMAC_CUBC_MASK;
    ch->initial_ublen = ch->cubc;
    ch->read_ubc = ch->cubc;
    ch->needs_fetch = false;
    ch->initd = true;
    return true;
}

static void at91_xdmac_complete_block(AT91XDMACState *s,
                                      AT91XDMACChannel *ch,
                                      unsigned int index)
{
    ch->cis |= XDMAC_CIS_BIS;
    ch->request_remaining = 0;
    s->sw_requests &= ~BIT(index);

    if (ch->descriptor_mode && (ch->cndc & XDMAC_CNDC_NDE)) {
        ch->needs_fetch = true;
        ch->initd = false;
    } else {
        if (ch->descriptor_mode) {
            ch->cis |= XDMAC_CIS_LIS;
        }
        ch->enabled = false;
    }
    at91_xdmac_update_irq(s);
}

static void at91_xdmac_fifo_finish_microblock(AT91XDMACState *s,
                                              AT91XDMACChannel *ch,
                                              unsigned int index)
{
    g_assert(!ch->cubc);
    g_assert(!ch->read_ubc);
    g_assert(!ch->fifo_count);
    g_assert(!ch->write_burst_remaining);
    g_assert(!ch->flush_remaining);
    g_assert(!ch->flush_pending);
    g_assert(!ch->disable_pending);
    g_assert(!ch->request_remaining);

    if (ch->cbc) {
        ch->cbc--;
        ch->cubc = ch->initial_ublen;
        ch->read_ubc = ch->initial_ublen;
    } else {
        at91_xdmac_complete_block(s, ch, index);
    }
}

static void at91_xdmac_finish_disable(AT91XDMACState *s,
                                      AT91XDMACChannel *ch,
                                      unsigned int index)
{
    ch->enabled = false;
    ch->needs_fetch = false;
    ch->error_stalled = false;
    ch->request_remaining = 0;
    ch->cis |= XDMAC_CIS_DIS;
    s->sw_requests &= ~BIT(index);
    s->grs &= ~BIT(index);
    s->gws &= ~BIT(index);
    at91_xdmac_fifo_reset(ch);
    at91_xdmac_update_irq(s);
}

static uint32_t at91_xdmac_advance_address(uint32_t address,
                                           unsigned int mode,
                                           unsigned int width,
                                           int32_t data_stride,
                                           int32_t microblock_stride,
                                           bool microblock_end)
{
    int64_t increment;

    switch (mode) {
    case XDMAC_AM_FIXED:
        return address;
    case XDMAC_AM_INCREMENTED:
        increment = width;
        break;
    case XDMAC_AM_UBS:
        increment = width + (microblock_end ? microblock_stride : 0);
        break;
    case XDMAC_AM_UBS_DS:
        increment = width + (microblock_end ? microblock_stride :
                             data_stride);
        break;
    default:
        g_assert_not_reached();
    }
    return address + increment;
}

static bool at91_xdmac_fifo_read_one(AT91XDMACState *s,
                                     AT91XDMACChannel *ch,
                                     unsigned int index,
                                     unsigned int width)
{
    uint8_t data[4];
    unsigned int sam = (ch->cc & XDMAC_CC_SAM_MASK) >>
                       XDMAC_CC_SAM_SHIFT;
    unsigned int csize;
    int32_t source_data_stride = sextract32(ch->cds_msp, 0, 16);
    int32_t source_micro_stride = sextract32(ch->csus, 0, 24);
    bool microblock_end;
    MemTxResult result;

    if (!ch->request_remaining) {
        if (!at91_xdmac_has_request(s, ch, index)) {
            return false;
        }
        csize = (ch->cc & XDMAC_CC_CSIZE_MASK) >>
                XDMAC_CC_CSIZE_SHIFT;
        ch->request_remaining = MIN(1U << MIN(csize, 4U), ch->read_ubc);
    }

    ch->read_in_progress = true;
    result = dma_memory_read(&s->dma_as, ch->csa, data, width,
                             MEMTXATTRS_UNSPECIFIED);
    ch->read_in_progress = false;
    if (result != MEMTX_OK) {
        at91_xdmac_set_error(s, ch, index, XDMAC_CIS_RBEIS);
        return false;
    }

    at91_xdmac_fifo_push(ch, data, width);
    ch->read_ubc--;
    microblock_end = !ch->read_ubc;
    ch->csa = at91_xdmac_advance_address(ch->csa, sam, width,
                                         source_data_stride,
                                         source_micro_stride,
                                         microblock_end);

    ch->request_remaining--;
    if (!ch->request_remaining && (ch->cc & XDMAC_CC_SWREQ)) {
        s->sw_requests &= ~BIT(index);
    }
    return true;
}

static bool at91_xdmac_fifo_write_one(AT91XDMACState *s,
                                      AT91XDMACChannel *ch,
                                      unsigned int index,
                                      unsigned int width)
{
    uint8_t data[4];
    unsigned int dam = (ch->cc & XDMAC_CC_DAM_MASK) >>
                       XDMAC_CC_DAM_SHIFT;
    int32_t dest_data_stride = sextract32(ch->cds_msp, 16, 16);
    int32_t dest_micro_stride = sextract32(ch->cdus, 0, 24);
    bool microblock_end;
    MemTxResult result;

    if (!ch->write_burst_remaining) {
        if (ch->disable_pending) {
            ch->write_burst_remaining = ch->fifo_count;
        } else if (ch->flush_pending) {
            ch->write_burst_remaining = ch->flush_remaining;
        } else if (!ch->read_ubc) {
            ch->write_burst_remaining = ch->fifo_count;
        } else {
            ch->write_burst_remaining =
                at91_xdmac_memory_burst_bytes(ch);
        }
    }

    at91_xdmac_fifo_peek(ch, data, width);
    ch->write_in_progress = true;
    result = dma_memory_write(&s->dma_as, ch->cda, data, width,
                              MEMTXATTRS_UNSPECIFIED);
    ch->write_in_progress = false;
    if (result != MEMTX_OK) {
        at91_xdmac_set_error(s, ch, index, XDMAC_CIS_WBEIS);
        if (ch->disable_pending) {
            /* GD must always converge even when its final drain faults. */
            at91_xdmac_finish_disable(s, ch, index);
        }
        return false;
    }

    at91_xdmac_fifo_pop(ch, width);
    g_assert(ch->write_burst_remaining >= width);
    ch->write_burst_remaining -= width;

    g_assert(ch->cubc);
    ch->cubc--;
    microblock_end = !ch->cubc;
    ch->cda = at91_xdmac_advance_address(ch->cda, dam, width,
                                         dest_data_stride,
                                         dest_micro_stride,
                                         microblock_end);

    if (ch->flush_pending) {
        g_assert(ch->flush_remaining >= width);
        ch->flush_remaining -= width;
        if (!ch->flush_remaining) {
            ch->flush_pending = false;
            ch->cis |= XDMAC_CIS_FIS;
            at91_xdmac_update_irq(s);
        }
    }

    if (microblock_end && !ch->disable_pending) {
        at91_xdmac_fifo_finish_microblock(s, ch, index);
    }
    return true;
}

static bool at91_xdmac_transfer_one(AT91XDMACState *s,
                                    AT91XDMACChannel *ch,
                                    unsigned int index)
{
    uint8_t data[4];
    unsigned int dwidth = (ch->cc & XDMAC_CC_DWIDTH_MASK) >>
                          XDMAC_CC_DWIDTH_SHIFT;
    unsigned int width;
    unsigned int sam = (ch->cc & XDMAC_CC_SAM_MASK) >>
                       XDMAC_CC_SAM_SHIFT;
    unsigned int dam = (ch->cc & XDMAC_CC_DAM_MASK) >>
                       XDMAC_CC_DAM_SHIFT;
    int32_t source_data_stride = sextract32(ch->cds_msp, 0, 16);
    int32_t dest_data_stride = sextract32(ch->cds_msp, 16, 16);
    int32_t source_micro_stride = sextract32(ch->csus, 0, 24);
    int32_t dest_micro_stride = sextract32(ch->cdus, 0, 24);
    bool microblock_end;
    MemTxResult result;

    if (dwidth > 2) {
        at91_xdmac_set_error(s, ch, index, XDMAC_CIS_RBEIS);
        return false;
    }
    width = 1U << dwidth;

    if (ch->cc & XDMAC_CC_MEMSET) {
        uint32_t pattern = cpu_to_le32(ch->cds_msp);

        memcpy(data, &pattern, width);
    } else {
        result = dma_memory_read(&s->dma_as, ch->csa, data, width,
                                 MEMTXATTRS_UNSPECIFIED);
        if (result != MEMTX_OK) {
            at91_xdmac_set_error(s, ch, index, XDMAC_CIS_RBEIS);
            return false;
        }
    }

    result = dma_memory_write(&s->dma_as, ch->cda, data, width,
                              MEMTXATTRS_UNSPECIFIED);
    if (result != MEMTX_OK) {
        at91_xdmac_set_error(s, ch, index, XDMAC_CIS_WBEIS);
        return false;
    }

    ch->cubc--;
    microblock_end = !ch->cubc;
    ch->csa = at91_xdmac_advance_address(ch->csa, sam, width,
                                         source_data_stride,
                                         source_micro_stride,
                                         microblock_end);
    ch->cda = at91_xdmac_advance_address(ch->cda, dam, width,
                                         dest_data_stride,
                                         dest_micro_stride,
                                         microblock_end);

    if (microblock_end) {
        if (ch->cbc) {
            ch->cbc--;
            ch->cubc = ch->initial_ublen;
        } else {
            at91_xdmac_complete_block(s, ch, index);
        }
    }
    return true;
}

/*
 * Detect a repeated address in a descriptor stream with Brent's algorithm.
 * The detector is local to one BH invocation; it is a liveness fence, not
 * guest-visible XDMAC state.
 */
static bool at91_xdmac_cycle_observe(AT91XDMACCycleDetector *cycle,
                                     uint32_t address)
{
    if (!cycle->seen) {
        cycle->anchor = address;
        cycle->power = 1;
        cycle->distance = 0;
        cycle->seen = true;
        return false;
    }

    cycle->distance++;
    if (address == cycle->anchor) {
        return true;
    }
    if (cycle->distance == cycle->power) {
        cycle->anchor = address;
        cycle->power <<= 1;
        cycle->distance = 0;
    }
    return false;
}

static unsigned int at91_xdmac_run_channel(AT91XDMACState *s,
                                           unsigned int index,
                                           unsigned int budget,
                                           AT91XDMACBHRunState *run_state)
{
    AT91XDMACChannel *ch = &s->channel[index];
    unsigned int used = 0;

    while (budget && at91_xdmac_channel_runnable(s, index)) {
        if (ch->needs_fetch) {
            uint32_t descriptor = ch->cnda & ~3U;
            bool repeated = at91_xdmac_cycle_observe(
                &run_state->cycle[index], descriptor);
            bool fetched = at91_xdmac_fetch_descriptor(s, ch, index);

            used++;
            budget--;
            if (fetched && repeated && (ch->cndc & XDMAC_CNDC_NDE)) {
                run_state->yielded |= BIT(index);
                break;
            }
            continue;
        }

        if (at91_xdmac_uses_p2m_fifo(ch)) {
            unsigned int width = at91_xdmac_data_width(ch);
            bool can_read;

            if (!width) {
                at91_xdmac_set_error(s, ch, index, XDMAC_CIS_RBEIS);
                used++;
                break;
            }
            if (ch->disable_pending && !ch->fifo_count) {
                at91_xdmac_finish_disable(s, ch, index);
                used++;
                budget--;
                continue;
            }
            if (!ch->cubc) {
                at91_xdmac_fifo_finish_microblock(s, ch, index);
                used++;
                budget--;
                continue;
            }
            can_read = at91_xdmac_fifo_can_read(s, ch, index, width);
            /*
             * A flush is nonblocking: source reads can be scheduled while
             * its finite FIFO snapshot drains.  Prefer one waiting read when
             * space is available; a full FIFO necessarily schedules a write
             * next, so the snapshot still makes bounded progress toward FIS.
             */
            if (ch->flush_pending && can_read) {
                if (!at91_xdmac_fifo_read_one(s, ch, index, width)) {
                    used++;
                    break;
                }
                used++;
                budget--;
                continue;
            }
            if (at91_xdmac_fifo_can_write(s, ch, index, width)) {
                if (!at91_xdmac_fifo_write_one(s, ch, index, width)) {
                    used++;
                    break;
                }
                used++;
                budget--;
                continue;
            }
            if (can_read) {
                if (!at91_xdmac_fifo_read_one(s, ch, index, width)) {
                    used++;
                    break;
                }
                used++;
                budget--;
                continue;
            }
            break;
        }

        if (!ch->cubc) {
            at91_xdmac_complete_block(s, ch, index);
            used++;
            budget--;
            continue;
        }

        if (at91_xdmac_is_peripheral(ch) && !ch->request_remaining) {
            unsigned int csize = (ch->cc & XDMAC_CC_CSIZE_MASK) >>
                                 XDMAC_CC_CSIZE_SHIFT;

            if (!at91_xdmac_has_request(s, ch, index)) {
                break;
            }
            ch->request_remaining = MIN(1U << MIN(csize, 4U), ch->cubc);
        }

        if (!at91_xdmac_transfer_one(s, ch, index)) {
            used++;
            break;
        }
        used++;
        budget--;

        if (at91_xdmac_is_peripheral(ch) && ch->request_remaining) {
            ch->request_remaining--;
            if (!ch->request_remaining && (ch->cc & XDMAC_CC_SWREQ)) {
                s->sw_requests &= ~BIT(index);
            }
        }
    }
    return used;
}

static void at91_xdmac_reschedule_after_bh(AT91XDMACState *s,
                                           uint32_t yielded, bool exhausted)
{
    bool idle_work = false;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        if (!at91_xdmac_channel_runnable(s, i)) {
            continue;
        }
        if (!exhausted && !(yielded & BIT(i))) {
            qemu_bh_schedule(s->bh);
            return;
        }
        idle_work = true;
    }

    if (idle_work) {
        /*
         * A request-driven descriptor ring may be permanently runnable.  Do
         * not enqueue the regular BH at zero virtual time after a ring was
         * observed or the global work budget was exhausted.  A distinct idle
         * BH avoids mixing BH_IDLE into the normal work BH, which could not
         * then be promoted by an event.  Any regular wake independently queued
         * during this callback is retained.
         */
        qemu_bh_schedule_idle(s->idle_bh);
    }
}

static void at91_xdmac_bh(void *opaque)
{
    AT91XDMACState *s = opaque;
    AT91XDMACBHRunState run_state = { };
    unsigned int budget = XDMAC_BH_WORK_LIMIT;
    unsigned int i;

    while (budget) {
        bool progress = false;

        for (i = 0; i < ARRAY_SIZE(s->channel) && budget; i++) {
            unsigned int channel_budget = MIN(budget,
                                               XDMAC_CHANNEL_WORK_QUANTUM);
            unsigned int used;

            if (run_state.yielded & BIT(i)) {
                continue;
            }
            used = at91_xdmac_run_channel(s, i, channel_budget,
                                          &run_state);

            budget -= used;
            progress |= used != 0;
        }
        if (!progress) {
            break;
        }
    }
    at91_xdmac_reschedule_after_bh(s, run_state.yielded, !budget);
}

static void at91_xdmac_idle_bh(void *opaque)
{
    AT91XDMACState *s = opaque;

    at91_xdmac_schedule(s);
}

static void at91_xdmac_clock_changed(void *opaque, ClockEvent event)
{
    AT91XDMACState *s = opaque;

    at91_xdmac_schedule(s);
}

static void at91_xdmac_request(void *opaque, int n, int level)
{
    AT91XDMACState *s = opaque;
    uint64_t mask = BIT_ULL(n);
    unsigned int i;

    if (level && (s->request_level & mask)) {
        for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
            AT91XDMACChannel *ch = &s->channel[i];

            if (ch->enabled && at91_xdmac_is_peripheral(ch) &&
                !(ch->cc & XDMAC_CC_SWREQ) &&
                at91_xdmac_perid(ch) == n && ch->request_remaining) {
                ch->cis |= XDMAC_CIS_ROIS;
            }
        }
        at91_xdmac_update_irq(s);
    }

    if (level) {
        s->request_level |= mask;
    } else {
        s->request_level &= ~mask;
    }
    at91_xdmac_schedule(s);
}

static bool at91_xdmac_decode_channel(hwaddr offset, unsigned int *index,
                                      hwaddr *reg)
{
    hwaddr channel_offset;

    if (offset < XDMAC_CHANNEL_BASE || offset >= 0x450) {
        return false;
    }
    channel_offset = offset - XDMAC_CHANNEL_BASE;
    *index = channel_offset / XDMAC_CHANNEL_STRIDE;
    *reg = channel_offset % XDMAC_CHANNEL_STRIDE;
    return *index < AT91_XDMAC_NUM_CHANNELS;
}

static uint64_t at91_xdmac_channel_read(AT91XDMACState *s,
                                        unsigned int index, hwaddr reg)
{
    AT91XDMACChannel *ch = &s->channel[index];
    uint32_t value;

    switch (reg) {
    case XDMAC_CIE:
    case XDMAC_CID:
        return 0;
    case XDMAC_CIM:
        return ch->cim;
    case XDMAC_CIS:
        value = ch->cis;
        ch->cis = 0;
        at91_xdmac_update_irq(s);
        return value;
    case XDMAC_CSA:
        return ch->csa;
    case XDMAC_CDA:
        return ch->cda;
    case XDMAC_CNDA:
        return ch->cnda;
    case XDMAC_CNDC:
        return ch->cndc;
    case XDMAC_CUBC:
        return ch->cubc;
    case XDMAC_CBC:
        return ch->cbc;
    case XDMAC_CC:
        value = ch->cc | (ch->initd ? XDMAC_CC_INITD : 0);
        if (ch->read_in_progress || ch->request_remaining) {
            value |= XDMAC_CC_RDIP;
        }
        if (ch->write_in_progress || ch->write_burst_remaining) {
            value |= XDMAC_CC_WRIP;
        }
        return value;
    case XDMAC_CDS_MSP:
        return ch->cds_msp;
    case XDMAC_CSUS:
        return ch->csus;
    case XDMAC_CDUS:
        return ch->cdus;
    default:
        return 0;
    }
}

static uint64_t at91_xdmac_read(void *opaque, hwaddr offset, unsigned size)
{
    AT91XDMACState *s = opaque;
    unsigned int index;
    hwaddr reg;

    if (at91_xdmac_decode_channel(offset, &index, &reg)) {
        return at91_xdmac_channel_read(s, index, reg);
    }

    switch (offset) {
    case XDMAC_GTYPE:
        return XDMAC_GTYPE_VALUE;
    case XDMAC_GCFG:
        return s->gcfg;
    case XDMAC_GWAC:
        return s->gwac;
    case XDMAC_GIE:
    case XDMAC_GID:
    case XDMAC_GE:
    case XDMAC_GD:
    case XDMAC_GRWS:
    case XDMAC_GRWR:
    case XDMAC_GSWR:
    case XDMAC_GSWF:
        return 0;
    case XDMAC_GIM:
        return s->gim;
    case XDMAC_GIS:
        return at91_xdmac_global_irq_status(s);
    case XDMAC_GS:
        return at91_xdmac_global_status(s);
    case XDMAC_GRS:
        return s->grs;
    case XDMAC_GWS:
        return s->gws;
    case XDMAC_GSWS:
        return s->sw_requests;
    case XDMAC_VERSION:
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_XDMAC ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_xdmac_start_channels(AT91XDMACState *s, uint32_t mask)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91XDMACChannel *ch = &s->channel[i];

        if (!(mask & BIT(i)) || ch->enabled) {
            continue;
        }
        ch->enabled = true;
        ch->descriptor_mode = ch->cndc & XDMAC_CNDC_NDE;
        ch->needs_fetch = ch->descriptor_mode;
        ch->initd = !ch->descriptor_mode;
        ch->error_stalled = false;
        ch->initial_ublen = ch->cubc;
        ch->request_remaining = 0;
        ch->read_ubc = ch->cubc;
        at91_xdmac_fifo_reset(ch);
    }
    at91_xdmac_schedule(s);
}

static void at91_xdmac_disable_channels(AT91XDMACState *s, uint32_t mask)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91XDMACChannel *ch = &s->channel[i];

        if (!(mask & BIT(i)) || !ch->enabled) {
            continue;
        }

        if (at91_xdmac_uses_p2m_fifo(ch) && ch->fifo_count &&
            !ch->error_stalled) {
            bool flush_write_scheduled =
                ch->flush_pending && ch->write_burst_remaining &&
                ch->write_burst_remaining >= ch->flush_remaining;

            /*
             * P2M disable is graceful: stop accepting source requests,
             * override GWS, and retain GS until every byte already in the
             * FIFO reaches RAM.  An unscheduled flush is discarded, but an
             * already-scheduled write must finish with FIS before DIS.
             */
            ch->disable_pending = true;
            if (!flush_write_scheduled) {
                ch->flush_pending = false;
                ch->flush_remaining = 0;
            }
            ch->request_remaining = 0;
            s->sw_requests &= ~BIT(i);
            s->gws &= ~BIT(i);
            continue;
        }

        at91_xdmac_finish_disable(s, ch, i);
    }
    at91_xdmac_schedule(s);
}

static void at91_xdmac_channel_write(AT91XDMACState *s,
                                     unsigned int index, hwaddr reg,
                                     uint32_t value)
{
    AT91XDMACChannel *ch = &s->channel[index];

    switch (reg) {
    case XDMAC_CIE:
        ch->cim |= value & XDMAC_CIS_MASK;
        at91_xdmac_update_irq(s);
        return;
    case XDMAC_CID:
        ch->cim &= ~(value & XDMAC_CIS_MASK);
        at91_xdmac_update_irq(s);
        return;
    case XDMAC_CIM:
    case XDMAC_CIS:
        return;
    default:
        break;
    }

    if (ch->enabled) {
        return;
    }

    switch (reg) {
    case XDMAC_CSA:
        ch->csa = value;
        break;
    case XDMAC_CDA:
        ch->cda = value;
        break;
    case XDMAC_CNDA:
        ch->cnda = value & XDMAC_CNDA_MASK;
        break;
    case XDMAC_CNDC:
        ch->cndc = value & XDMAC_CNDC_MASK;
        break;
    case XDMAC_CUBC:
        ch->cubc = value & XDMAC_CUBC_MASK;
        break;
    case XDMAC_CBC:
        ch->cbc = value & XDMAC_CBC_MASK;
        break;
    case XDMAC_CC:
        ch->cc = value & XDMAC_CC_WRITABLE_MASK;
        break;
    case XDMAC_CDS_MSP:
        ch->cds_msp = value;
        break;
    case XDMAC_CSUS:
        ch->csus = value & XDMAC_CUBC_MASK;
        break;
    case XDMAC_CDUS:
        ch->cdus = value & XDMAC_CUBC_MASK;
        break;
    default:
        break;
    }
}

static void at91_xdmac_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    AT91XDMACState *s = opaque;
    uint32_t val = value;
    uint32_t mask = val & XDMAC_CHANNEL_MASK;
    unsigned int index;
    hwaddr reg;

    if (at91_xdmac_decode_channel(offset, &index, &reg)) {
        at91_xdmac_channel_write(s, index, reg, val);
        return;
    }

    switch (offset) {
    case XDMAC_GTYPE:
    case XDMAC_GIM:
    case XDMAC_GIS:
    case XDMAC_GS:
    case XDMAC_GSWS:
    case XDMAC_VERSION:
        break;
    case XDMAC_GCFG:
        s->gcfg = val & XDMAC_GCFG_MASK;
        break;
    case XDMAC_GWAC:
        s->gwac = val & XDMAC_GWAC_MASK;
        break;
    case XDMAC_GIE:
        s->gim |= mask;
        at91_xdmac_update_irq(s);
        break;
    case XDMAC_GID:
        s->gim &= ~mask;
        at91_xdmac_update_irq(s);
        break;
    case XDMAC_GE:
        at91_xdmac_start_channels(s, mask);
        break;
    case XDMAC_GD:
        at91_xdmac_disable_channels(s, mask);
        break;
    case XDMAC_GRS:
        s->grs = mask;
        at91_xdmac_schedule(s);
        break;
    case XDMAC_GWS:
        s->gws = mask;
        for (index = 0; index < ARRAY_SIZE(s->channel); index++) {
            if (s->channel[index].disable_pending) {
                s->gws &= ~BIT(index);
            }
        }
        at91_xdmac_schedule(s);
        break;
    case XDMAC_GRWS:
        s->grs |= mask;
        s->gws |= mask;
        for (index = 0; index < ARRAY_SIZE(s->channel); index++) {
            if (s->channel[index].disable_pending) {
                s->gws &= ~BIT(index);
            }
        }
        at91_xdmac_schedule(s);
        break;
    case XDMAC_GRWR:
        s->grs &= ~mask;
        s->gws &= ~mask;
        at91_xdmac_schedule(s);
        break;
    case XDMAC_GSWR:
        for (index = 0; index < ARRAY_SIZE(s->channel); index++) {
            if ((mask & BIT(index)) &&
                (s->sw_requests & BIT(index))) {
                s->channel[index].cis |= XDMAC_CIS_ROIS;
            }
        }
        s->sw_requests |= mask;
        at91_xdmac_update_irq(s);
        at91_xdmac_schedule(s);
        break;
    case XDMAC_GSWF:
        for (index = 0; index < ARRAY_SIZE(s->channel); index++) {
            AT91XDMACChannel *ch = &s->channel[index];

            if ((mask & BIT(index)) && ch->enabled &&
                at91_xdmac_is_source_peripheral(ch) &&
                !ch->disable_pending) {
                if (at91_xdmac_uses_p2m_fifo(ch) && ch->fifo_count) {
                    /* Snapshot bytes that preceded this flush request. */
                    ch->flush_pending = true;
                    ch->flush_remaining = ch->fifo_count;
                } else {
                    ch->cis |= XDMAC_CIS_FIS;
                }
            }
        }
        at91_xdmac_update_irq(s);
        at91_xdmac_schedule(s);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_XDMAC ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_xdmac_ops = {
    .read = at91_xdmac_read,
    .write = at91_xdmac_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_xdmac_reset(DeviceState *dev)
{
    AT91XDMACState *s = AT91_XDMAC(dev);

    if (s->bh) {
        qemu_bh_cancel(s->bh);
    }
    if (s->idle_bh) {
        qemu_bh_cancel(s->idle_bh);
    }
    s->gcfg = 0;
    s->gwac = 0;
    s->gim = 0;
    s->grs = 0;
    s->gws = 0;
    s->sw_requests = 0;
    s->request_level = 0;
    memset(s->fifo_migration, 0, sizeof(s->fifo_migration));
    memset(s->channel, 0, sizeof(s->channel));
    at91_xdmac_update_irq(s);
}

static void at91_xdmac_init(Object *obj)
{
    AT91XDMACState *s = AT91_XDMAC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &at91_xdmac_ops, s,
                          TYPE_AT91_XDMAC, XDMAC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_in_named(dev, at91_xdmac_request, "request",
                            AT91_XDMAC_NUM_REQUESTS);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_xdmac_clock_changed,
                                 s, ClockUpdate);
}

static void at91_xdmac_realize(DeviceState *dev, Error **errp)
{
    AT91XDMACState *s = AT91_XDMAC(dev);

    if (!s->dma_mr) {
        error_setg(errp, TYPE_AT91_XDMAC ": dma-memory link is not set");
        return;
    }
    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_XDMAC ": pclk must be connected");
        return;
    }

    address_space_init(&s->dma_as, s->dma_mr, TYPE_AT91_XDMAC "-dma");
    s->bh = qemu_bh_new_guarded(at91_xdmac_bh, s,
                                &dev->mem_reentrancy_guard);
    s->idle_bh = qemu_bh_new_guarded(at91_xdmac_idle_bh, s,
                                     &dev->mem_reentrancy_guard);
}

static void at91_xdmac_unrealize(DeviceState *dev)
{
    AT91XDMACState *s = AT91_XDMAC(dev);

    qemu_bh_delete(s->idle_bh);
    s->idle_bh = NULL;
    qemu_bh_delete(s->bh);
    s->bh = NULL;
    address_space_destroy(&s->dma_as);
}

static int at91_xdmac_pre_save(void *opaque)
{
    AT91XDMACState *s = opaque;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91XDMACChannel *ch = &s->channel[i];
        AT91XDMACFifoMigrationState *fifo = &s->fifo_migration[i];

        fifo->read_ubc = ch->read_ubc;
        fifo->fifo_head = ch->fifo_head;
        fifo->fifo_tail = ch->fifo_tail;
        fifo->fifo_count = ch->fifo_count;
        fifo->write_burst_remaining = ch->write_burst_remaining;
        fifo->flush_remaining = ch->flush_remaining;
        memcpy(fifo->fifo, ch->fifo, sizeof(fifo->fifo));
        fifo->flush_pending = ch->flush_pending;
        fifo->disable_pending = ch->disable_pending;

        /* Synchronous DMA transactions cannot span a save boundary. */
        ch->read_in_progress = false;
        ch->write_in_progress = false;
    }
    return 0;
}

static int at91_xdmac_restore_fifo(AT91XDMACState *s, unsigned int index,
                                   int version_id)
{
    AT91XDMACChannel *ch = &s->channel[index];
    AT91XDMACFifoMigrationState *fifo = &s->fifo_migration[index];
    uint32_t chunk;
    unsigned int width;

    if (!at91_xdmac_uses_p2m_fifo(ch) ||
        (version_id == 2 && (ch->descriptor_mode || ch->cbc))) {
        /* Version 2 used the legacy atomic path for descriptors and CBC. */
        ch->read_ubc = ch->cubc;
        at91_xdmac_fifo_reset(ch);
        return 0;
    }

    if (fifo->fifo_head >= AT91_XDMAC_FIFO_BYTES_PER_CHANNEL ||
        fifo->fifo_tail >= AT91_XDMAC_FIFO_BYTES_PER_CHANNEL ||
        fifo->fifo_count > AT91_XDMAC_FIFO_BYTES_PER_CHANNEL ||
        fifo->write_burst_remaining > fifo->fifo_count ||
        fifo->flush_remaining > fifo->fifo_count ||
        ch->cubc > ch->initial_ublen ||
        fifo->read_ubc > ch->cubc ||
        (fifo->fifo_head + fifo->fifo_count) %
            AT91_XDMAC_FIFO_BYTES_PER_CHANNEL != fifo->fifo_tail ||
        fifo->flush_pending != (fifo->flush_remaining != 0) ||
        (fifo->disable_pending &&
         (ch->error_stalled || ch->request_remaining ||
          (fifo->flush_pending &&
           (!fifo->write_burst_remaining ||
            fifo->write_burst_remaining < fifo->flush_remaining)) ||
          (s->gws & BIT(index)))) ||
        (ch->error_stalled &&
         (fifo->write_burst_remaining || ch->request_remaining)) ||
        (ch->needs_fetch &&
         (fifo->fifo_count || fifo->write_burst_remaining ||
          fifo->flush_remaining || fifo->flush_pending ||
          fifo->disable_pending))) {
        return -EINVAL;
    }

    width = at91_xdmac_data_width(ch);
    if (!width) {
        if (fifo->fifo_count || fifo->write_burst_remaining ||
            fifo->flush_remaining || fifo->read_ubc != ch->cubc) {
            return -EINVAL;
        }
    } else if ((fifo->fifo_count % width) ||
               (fifo->write_burst_remaining % width) ||
               (fifo->flush_remaining % width) ||
               fifo->fifo_count !=
                   (uint64_t)(ch->cubc - fifo->read_ubc) * width) {
        return -EINVAL;
    }

    chunk = 1U << MIN((ch->cc & XDMAC_CC_CSIZE_MASK) >>
                      XDMAC_CC_CSIZE_SHIFT, 4U);
    if (ch->request_remaining > MIN(chunk, fifo->read_ubc) ||
        (ch->request_remaining && (ch->cc & XDMAC_CC_SWREQ) &&
         !(s->sw_requests & BIT(index)))) {
        return -EINVAL;
    }

    ch->read_ubc = fifo->read_ubc;
    ch->fifo_head = fifo->fifo_head;
    ch->fifo_tail = fifo->fifo_tail;
    ch->fifo_count = fifo->fifo_count;
    ch->write_burst_remaining = fifo->write_burst_remaining;
    ch->flush_remaining = fifo->flush_remaining;
    memcpy(ch->fifo, fifo->fifo, sizeof(ch->fifo));
    ch->flush_pending = fifo->flush_pending;
    ch->disable_pending = fifo->disable_pending;
    ch->read_in_progress = false;
    ch->write_in_progress = false;
    return 0;
}

static int at91_xdmac_post_load(void *opaque, int version_id)
{
    AT91XDMACState *s = opaque;
    unsigned int i;
    int ret;

    s->gim &= XDMAC_CHANNEL_MASK;
    s->grs &= XDMAC_CHANNEL_MASK;
    s->gws &= XDMAC_CHANNEL_MASK;
    s->sw_requests &= XDMAC_CHANNEL_MASK;
    s->request_level &= XDMAC_REQUEST_MASK;

    for (i = 0; i < ARRAY_SIZE(s->channel); i++) {
        AT91XDMACChannel *ch = &s->channel[i];

        if (ch->needs_fetch &&
            (!ch->descriptor_mode || ch->request_remaining)) {
            return -EINVAL;
        }
        if (version_id < 2) {
            ch->read_ubc = ch->cubc;
            at91_xdmac_fifo_reset(ch);
            continue;
        }
        ret = at91_xdmac_restore_fifo(s, i, version_id);
        if (ret) {
            return ret;
        }
    }

    at91_xdmac_update_irq(s);
    at91_xdmac_schedule(s);
    return 0;
}

static const VMStateDescription vmstate_at91_xdmac_channel = {
    .name = TYPE_AT91_XDMAC "/channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(cim, AT91XDMACChannel),
        VMSTATE_UINT32(cis, AT91XDMACChannel),
        VMSTATE_UINT32(csa, AT91XDMACChannel),
        VMSTATE_UINT32(cda, AT91XDMACChannel),
        VMSTATE_UINT32(cnda, AT91XDMACChannel),
        VMSTATE_UINT32(cndc, AT91XDMACChannel),
        VMSTATE_UINT32(cubc, AT91XDMACChannel),
        VMSTATE_UINT32(cbc, AT91XDMACChannel),
        VMSTATE_UINT32(cc, AT91XDMACChannel),
        VMSTATE_UINT32(cds_msp, AT91XDMACChannel),
        VMSTATE_UINT32(csus, AT91XDMACChannel),
        VMSTATE_UINT32(cdus, AT91XDMACChannel),
        VMSTATE_UINT32(initial_ublen, AT91XDMACChannel),
        VMSTATE_UINT32(request_remaining, AT91XDMACChannel),
        VMSTATE_BOOL(enabled, AT91XDMACChannel),
        VMSTATE_BOOL(descriptor_mode, AT91XDMACChannel),
        VMSTATE_BOOL(needs_fetch, AT91XDMACChannel),
        VMSTATE_BOOL(initd, AT91XDMACChannel),
        VMSTATE_BOOL(error_stalled, AT91XDMACChannel),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_at91_xdmac_fifo = {
    .name = TYPE_AT91_XDMAC "/fifo",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(read_ubc, AT91XDMACFifoMigrationState),
        VMSTATE_UINT16(fifo_head, AT91XDMACFifoMigrationState),
        VMSTATE_UINT16(fifo_tail, AT91XDMACFifoMigrationState),
        VMSTATE_UINT16(fifo_count, AT91XDMACFifoMigrationState),
        VMSTATE_UINT16(write_burst_remaining,
                       AT91XDMACFifoMigrationState),
        VMSTATE_UINT16(flush_remaining, AT91XDMACFifoMigrationState),
        VMSTATE_BUFFER(fifo, AT91XDMACFifoMigrationState),
        VMSTATE_BOOL(flush_pending, AT91XDMACFifoMigrationState),
        VMSTATE_BOOL(disable_pending, AT91XDMACFifoMigrationState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_at91_xdmac = {
    .name = TYPE_AT91_XDMAC,
    .version_id = 3,
    .minimum_version_id = 1,
    .pre_save = at91_xdmac_pre_save,
    .post_load = at91_xdmac_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(gcfg, AT91XDMACState),
        VMSTATE_UINT32(gwac, AT91XDMACState),
        VMSTATE_UINT32(gim, AT91XDMACState),
        VMSTATE_UINT32(grs, AT91XDMACState),
        VMSTATE_UINT32(gws, AT91XDMACState),
        VMSTATE_UINT32(sw_requests, AT91XDMACState),
        VMSTATE_UINT64(request_level, AT91XDMACState),
        VMSTATE_STRUCT_ARRAY(fifo_migration, AT91XDMACState,
                             AT91_XDMAC_NUM_CHANNELS, 2,
                             vmstate_at91_xdmac_fifo,
                             AT91XDMACFifoMigrationState),
        VMSTATE_STRUCT_ARRAY(channel, AT91XDMACState,
                             AT91_XDMAC_NUM_CHANNELS, 1,
                             vmstate_at91_xdmac_channel,
                             AT91XDMACChannel),
        VMSTATE_CLOCK(pclk, AT91XDMACState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_xdmac_properties[] = {
    DEFINE_PROP_LINK("dma-memory", AT91XDMACState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void at91_xdmac_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Extensible DMA Controller";
    dc->realize = at91_xdmac_realize;
    dc->unrealize = at91_xdmac_unrealize;
    dc->vmsd = &vmstate_at91_xdmac;
    device_class_set_legacy_reset(dc, at91_xdmac_reset);
    device_class_set_props(dc, at91_xdmac_properties);
}

static const TypeInfo at91_xdmac_info = {
    .name = TYPE_AT91_XDMAC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91XDMACState),
    .instance_init = at91_xdmac_init,
    .class_init = at91_xdmac_class_init,
};

static void at91_xdmac_register_types(void)
{
    type_register_static(&at91_xdmac_info);
}

type_init(at91_xdmac_register_types)
