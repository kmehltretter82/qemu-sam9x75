/*
 * Bosch M_CAN controller
 *
 * This is a deliberately untimed first implementation.  It models the host
 * register interface, the externally supplied message RAM, interrupt routing,
 * and the FIFO paths needed by the Linux m_can driver.  Arbitration, wire bit
 * timing, error confinement, and retransmission require a richer QEMU CAN bus
 * API and are intentionally not guessed here.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/irq.h"
#include "hw/net/can/bosch_m_can.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/module.h"

enum {
    MCAN_CREL       = 0x000,
    MCAN_ENDN       = 0x004,
    MCAN_CUST       = 0x008,
    MCAN_DBTP       = 0x00c,
    MCAN_TEST       = 0x010,
    MCAN_RWD        = 0x014,
    MCAN_CCCR       = 0x018,
    MCAN_NBTP       = 0x01c,
    MCAN_TSCC       = 0x020,
    MCAN_TSCV       = 0x024,
    MCAN_TOCC       = 0x028,
    MCAN_TOCV       = 0x02c,
    MCAN_ECR        = 0x040,
    MCAN_PSR        = 0x044,
    MCAN_TDCR       = 0x048,
    MCAN_IR         = 0x050,
    MCAN_IE         = 0x054,
    MCAN_ILS        = 0x058,
    MCAN_ILE        = 0x05c,
    MCAN_GFC        = 0x080,
    MCAN_SIDFC      = 0x084,
    MCAN_XIDFC      = 0x088,
    MCAN_XIDAM      = 0x090,
    MCAN_HPMS       = 0x094,
    MCAN_NDAT1      = 0x098,
    MCAN_NDAT2      = 0x09c,
    MCAN_RXF0C      = 0x0a0,
    MCAN_RXF0S      = 0x0a4,
    MCAN_RXF0A      = 0x0a8,
    MCAN_RXBC       = 0x0ac,
    MCAN_RXF1C      = 0x0b0,
    MCAN_RXF1S      = 0x0b4,
    MCAN_RXF1A      = 0x0b8,
    MCAN_RXESC      = 0x0bc,
    MCAN_TXBC       = 0x0c0,
    MCAN_TXFQS      = 0x0c4,
    MCAN_TXESC      = 0x0c8,
    MCAN_TXBRP      = 0x0cc,
    MCAN_TXBAR      = 0x0d0,
    MCAN_TXBCR      = 0x0d4,
    MCAN_TXBTO      = 0x0d8,
    MCAN_TXBCF      = 0x0dc,
    MCAN_TXBTIE     = 0x0e0,
    MCAN_TXBCIE     = 0x0e4,
    MCAN_TXEFC      = 0x0f0,
    MCAN_TXEFS      = 0x0f4,
    MCAN_TXEFA      = 0x0f8,

    /* Microchip SAM9X7 timestamping-unit extension. */
    MCAN_TSU_TSCFG  = 0x164,
    MCAN_TSU_TSS1   = 0x168,
    MCAN_TSU_TSS2   = 0x16c,
    MCAN_TSU_TS0    = 0x170,
    MCAN_TSU_TS15   = 0x1ac,
    MCAN_TSU_ATB    = 0x1b0,
};

#define MCAN_REG(s, offset) ((s)->regs[(offset) / sizeof(uint32_t)])

#define MCAN_CCCR_BRSE      BIT(9)
#define MCAN_CCCR_FDOE      BIT(8)
#define MCAN_CCCR_TEST      BIT(7)
#define MCAN_CCCR_MON       BIT(5)
#define MCAN_CCCR_CSR       BIT(4)
#define MCAN_CCCR_CSA       BIT(3)
#define MCAN_CCCR_CCE       BIT(1)
#define MCAN_CCCR_INIT      BIT(0)
#define MCAN_CCCR_MASK      0x0000ffff
#define MCAN_CCCR_DIRECT_MASK \
    (MCAN_CCCR_CSR | MCAN_CCCR_CCE | MCAN_CCCR_INIT)

#define MCAN_TEST_LBCK      BIT(4)
#define MCAN_TEST_WRITABLE_MASK 0x00000070

#define MCAN_RWD_WDC_MASK   0x000000ff

#define MCAN_TSCC_TSS_MASK  0x00000003
#define MCAN_TSCC_TSS_INTERNAL 1
#define MCAN_TSCC_TSS_EXTERNAL 2

#define MCAN_TOCC_TOS_SHIFT 1
#define MCAN_TOCC_TOS_LEN   2
#define MCAN_TOCC_TOP_SHIFT 16

#define MCAN_ECR_CEL_MASK   MAKE_64BIT_MASK(16, 8)

#define MCAN_PSR_LEC_MASK   MAKE_64BIT_MASK(0, 3)
#define MCAN_PSR_DLEC_MASK  MAKE_64BIT_MASK(8, 3)
#define MCAN_PSR_RESI       BIT(11)
#define MCAN_PSR_RBRS       BIT(12)
#define MCAN_PSR_RFDF       BIT(13)
#define MCAN_PSR_PXE        BIT(14)
#define MCAN_PSR_READ_CLEAR (MCAN_PSR_PXE | MCAN_PSR_RFDF | \
                             MCAN_PSR_RBRS | MCAN_PSR_RESI)

#define MCAN_IR_RF0N        BIT(0)
#define MCAN_IR_RF0W        BIT(1)
#define MCAN_IR_RF0F        BIT(2)
#define MCAN_IR_RF0L        BIT(3)
#define MCAN_IR_RF1N        BIT(4)
#define MCAN_IR_RF1W        BIT(5)
#define MCAN_IR_RF1F        BIT(6)
#define MCAN_IR_RF1L        BIT(7)
#define MCAN_IR_HPM         BIT(8)
#define MCAN_IR_TC          BIT(9)
#define MCAN_IR_TCF         BIT(10)
#define MCAN_IR_TFE         BIT(11)
#define MCAN_IR_TEFN        BIT(12)
#define MCAN_IR_TEFW        BIT(13)
#define MCAN_IR_TEFF        BIT(14)
#define MCAN_IR_TEFL        BIT(15)
#define MCAN_IR_TSW         BIT(16)
#define MCAN_IR_MRAF        BIT(17)
#define MCAN_IR_DRX         BIT(19)
#define MCAN_IR_MASK        0x3fcfffff

#define MCAN_ILE_EINT0      BIT(0)
#define MCAN_ILE_EINT1      BIT(1)

#define MCAN_RXF0C_F0SA_MASK 0x0000fffc
#define MCAN_RXF0C_F0S_SHIFT 16
#define MCAN_RXF0C_F0WM_SHIFT 24
#define MCAN_RXF0C_F0OM      BIT(31)

#define MCAN_RXF1C_F1SA_MASK  0x0000fffc
#define MCAN_RXF1C_F1S_SHIFT  16
#define MCAN_RXF1C_F1WM_SHIFT 24
#define MCAN_RXF1C_F1OM       BIT(31)

#define MCAN_SIDFC_FLSSA_MASK 0x0000fffc
#define MCAN_SIDFC_LSS_SHIFT  16
#define MCAN_XIDFC_FLESA_MASK 0x0000fffc
#define MCAN_XIDFC_LSE_SHIFT  16
#define MCAN_RXBC_RBSA_MASK   0x0000fffc

#define MCAN_FILTER_ACTION_DISABLE       0
#define MCAN_FILTER_ACTION_FIFO0         1
#define MCAN_FILTER_ACTION_FIFO1         2
#define MCAN_FILTER_ACTION_REJECT        3
#define MCAN_FILTER_ACTION_PRIORITY      4
#define MCAN_FILTER_ACTION_PRIORITY_FIFO0 5
#define MCAN_FILTER_ACTION_PRIORITY_FIFO1 6
#define MCAN_FILTER_ACTION_RX_BUFFER     7

#define MCAN_FILTER_ID_MASK       0x1fffffff
#define MCAN_STD_FILTER_ID_MASK   0x7ff
#define MCAN_STD_FILTER_ID1_SHIFT 16
#define MCAN_STD_FILTER_ACTION_SHIFT 27
#define MCAN_STD_FILTER_TYPE_SHIFT 30
#define MCAN_EXT_FILTER_ACTION_SHIFT 29
#define MCAN_EXT_FILTER_TYPE_SHIFT 30

#define MCAN_RX_FILTER_INDEX_SHIFT 24

#define MCAN_TXBC_TBSA_MASK  0x0000fffc
#define MCAN_TXBC_NDTB_SHIFT 16
#define MCAN_TXBC_TFQS_SHIFT 24
#define MCAN_TXBC_TFQM       BIT(30)

#define MCAN_TXEFC_EFSA_MASK  0x0000fffc
#define MCAN_TXEFC_EFS_SHIFT  16
#define MCAN_TXEFC_EFWM_SHIFT 24

#define MCAN_ELEMENT_ESI     BIT(31)
#define MCAN_ELEMENT_XTD     BIT(30)
#define MCAN_ELEMENT_RTR     BIT(29)
#define MCAN_ELEMENT_EFC     BIT(23)
#define MCAN_ELEMENT_FDF     BIT(21)
#define MCAN_ELEMENT_BRS     BIT(20)
#define MCAN_ELEMENT_DLC_SHIFT 16
#define MCAN_ELEMENT_MM_MASK  0xff000000
#define MCAN_RX_ELEMENT_ANMF  BIT(31)
#define MCAN_TX_EVENT_ET_TX   (1U << 22)

#define MCAN_TSCFG_MASK      0x0000ff07
#define MCAN_TSCFG_TBPRE_MASK 0x0000ff00
#define MCAN_DBTP_GENERIC_MASK 0x009f1fff
#define MCAN_TSS2_DOCUMENTED_RESET 0x000a0000

static const uint8_t mcan_data_size[8] = {
    8, 12, 16, 20, 24, 32, 48, 64,
};

typedef enum MCanRxStoreResult {
    MCAN_RX_STORE_OK,
    MCAN_RX_STORE_LOST,
    MCAN_RX_STORE_ERROR,
} MCanRxStoreResult;

typedef struct MCanFilterResult {
    unsigned action;
    unsigned filter_index;
    unsigned buffer_index;
    bool extended;
    bool matched;
    bool error;
} MCanFilterResult;

static void bosch_m_can_process_tx(BoschMCanState *s);

static unsigned mcan_rxf0_size(BoschMCanState *s)
{
    return MIN(extract32(MCAN_REG(s, MCAN_RXF0C), MCAN_RXF0C_F0S_SHIFT, 7),
               64U);
}

static unsigned mcan_rxf1_size(BoschMCanState *s)
{
    return MIN(extract32(MCAN_REG(s, MCAN_RXF1C), MCAN_RXF1C_F1S_SHIFT, 7),
               64U);
}

static unsigned mcan_tx_ndtb(BoschMCanState *s)
{
    return MIN(extract32(MCAN_REG(s, MCAN_TXBC), MCAN_TXBC_NDTB_SHIFT, 6),
               32U);
}

static unsigned mcan_tx_fifo_size(BoschMCanState *s)
{
    unsigned ndtb = mcan_tx_ndtb(s);
    unsigned size = MIN(extract32(MCAN_REG(s, MCAN_TXBC),
                                  MCAN_TXBC_TFQS_SHIFT, 6), 32U);

    return MIN(size, 32U - ndtb);
}

static unsigned mcan_txe_size(BoschMCanState *s)
{
    return MIN(extract32(MCAN_REG(s, MCAN_TXEFC),
                         MCAN_TXEFC_EFS_SHIFT, 6), 32U);
}

static bool bosch_m_can_active(BoschMCanState *s)
{
    return s->resources_ready && clock_is_enabled(s->cclk) &&
           !(MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_INIT);
}

static void bosch_m_can_record_success(BoschMCanState *s,
                                       const qemu_can_frame *frame)
{
    uint32_t clear = (frame->flags & QEMU_CAN_FRMF_TYPE_FD) &&
                     (frame->flags & QEMU_CAN_FRMF_BRS) ?
                     MCAN_PSR_DLEC_MASK : MCAN_PSR_LEC_MASK;

    MCAN_REG(s, MCAN_PSR) &= ~clear;
}

static void bosch_m_can_record_rx(BoschMCanState *s,
                                  const qemu_can_frame *frame)
{
    uint32_t psr;

    bosch_m_can_record_success(s, frame);
    if (!(frame->flags & QEMU_CAN_FRMF_TYPE_FD)) {
        return;
    }

    psr = MCAN_REG(s, MCAN_PSR);
    psr &= ~(MCAN_PSR_RBRS | MCAN_PSR_RESI);
    psr |= MCAN_PSR_RFDF;
    if (frame->flags & QEMU_CAN_FRMF_BRS) {
        psr |= MCAN_PSR_RBRS;
    }
    if (frame->flags & QEMU_CAN_FRMF_ESI) {
        psr |= MCAN_PSR_RESI;
    }
    MCAN_REG(s, MCAN_PSR) = psr;
}

static void bosch_m_can_update_irq(BoschMCanState *s)
{
    uint32_t pending = MCAN_REG(s, MCAN_IR) & MCAN_REG(s, MCAN_IE);
    uint32_t ile = MCAN_REG(s, MCAN_ILE);
    uint32_t ils = MCAN_REG(s, MCAN_ILS);

    /* hclk-off IRQ behavior is hardware-gated; preserve latched levels. */
    qemu_set_irq(s->irq[0], (ile & MCAN_ILE_EINT0) && (pending & ~ils));
    qemu_set_irq(s->irq[1], (ile & MCAN_ILE_EINT1) && (pending & ils));
}

static void bosch_m_can_raise_ir(BoschMCanState *s, uint32_t bits)
{
    MCAN_REG(s, MCAN_IR) |= bits & MCAN_IR_MASK;
    bosch_m_can_update_irq(s);
}

static bool bosch_m_can_mram_range(BoschMCanState *s, hwaddr address,
                                   size_t length)
{
    uint64_t ram_size;

    if (!s->message_ram_as) {
        return false;
    }
    ram_size = memory_region_size(s->message_ram);
    return address <= ram_size && length <= ram_size - address;
}

static bool bosch_m_can_mram_read(BoschMCanState *s, hwaddr address,
                                  void *buffer, size_t length)
{
    const MemTxAttrs attrs = { .memory = true };

    if (!bosch_m_can_mram_range(s, address, length) ||
        address_space_read(s->message_ram_as, address, attrs,
                           buffer, length) != MEMTX_OK) {
        bosch_m_can_raise_ir(s, MCAN_IR_MRAF);
        return false;
    }
    return true;
}

static bool bosch_m_can_mram_write(BoschMCanState *s, hwaddr address,
                                   const void *buffer, size_t length)
{
    const MemTxAttrs attrs = { .memory = true };

    if (!bosch_m_can_mram_range(s, address, length) ||
        address_space_write(s->message_ram_as, address, attrs,
                            buffer, length) != MEMTX_OK) {
        bosch_m_can_raise_ir(s, MCAN_IR_MRAF);
        return false;
    }
    return true;
}

static bool bosch_m_can_mram_read32(BoschMCanState *s, hwaddr address,
                                    uint32_t *value)
{
    uint8_t bytes[sizeof(*value)];

    if (!bosch_m_can_mram_read(s, address, bytes, sizeof(bytes))) {
        return false;
    }
    *value = ldl_le_p(bytes);
    return true;
}

static bool bosch_m_can_mram_write32(BoschMCanState *s, hwaddr address,
                                     uint32_t value)
{
    uint8_t bytes[sizeof(value)];

    stl_le_p(bytes, value);
    return bosch_m_can_mram_write(s, address, bytes, sizeof(bytes));
}

static uint16_t bosch_m_can_capture_timestamp(BoschMCanState *s)
{
    unsigned tss = MCAN_REG(s, MCAN_TSCC) & MCAN_TSCC_TSS_MASK;
    uint16_t timestamp = 0;

    if (tss == MCAN_TSCC_TSS_INTERNAL ||
        tss == MCAN_TSCC_TSS_EXTERNAL) {
        timestamp = s->timestamp_counter;
    }

    /* Untimed approximation: internal TSS advances once per frame. */
    if (tss == MCAN_TSCC_TSS_INTERNAL) {
        s->timestamp_counter++;
        if (s->timestamp_counter == 0) {
            bosch_m_can_raise_ir(s, MCAN_IR_TSW);
        }
    }
    return timestamp;
}

static uint32_t bosch_m_can_rxf_status(BoschMCanState *s, unsigned fifo)
{
    unsigned size = fifo ? mcan_rxf1_size(s) : mcan_rxf0_size(s);
    unsigned fill = fifo ? s->rxf1_fill : s->rxf0_fill;
    unsigned get = fifo ? s->rxf1_get : s->rxf0_get;
    unsigned put = fifo ? s->rxf1_put : s->rxf0_put;
    uint32_t lost_ir = fifo ? MCAN_IR_RF1L : MCAN_IR_RF0L;
    uint32_t value = fill | ((uint32_t)get << 8) |
                     ((uint32_t)put << 16);

    if (size && fill == size) {
        value |= BIT(24);
    }
    if (MCAN_REG(s, MCAN_IR) & lost_ir) {
        value |= BIT(25);
    }
    return value;
}

static uint32_t bosch_m_can_txe_status(BoschMCanState *s)
{
    unsigned size = mcan_txe_size(s);
    uint32_t value = s->txe_fill |
                     ((uint32_t)s->txe_get << 8) |
                     ((uint32_t)s->txe_put << 16);

    if (size && s->txe_fill == size) {
        value |= BIT(24);
    }
    if (MCAN_REG(s, MCAN_IR) & MCAN_IR_TEFL) {
        value |= BIT(25);
    }
    return value;
}

static unsigned bosch_m_can_tx_fifo_pending(BoschMCanState *s)
{
    unsigned first = mcan_tx_ndtb(s);
    unsigned count = 0;
    unsigned i;

    for (i = 0; i < mcan_tx_fifo_size(s); i++) {
        if (MCAN_REG(s, MCAN_TXBRP) & BIT(first + i)) {
            count++;
        }
    }
    return count;
}

static uint32_t bosch_m_can_txfqs(BoschMCanState *s)
{
    unsigned size = mcan_tx_fifo_size(s);
    unsigned pending = bosch_m_can_tx_fifo_pending(s);
    uint32_t value = (uint32_t)s->tx_fifo_put << 16;

    if (pending >= size && size) {
        value |= BIT(21);
    }
    if (!(MCAN_REG(s, MCAN_TXBC) & MCAN_TXBC_TFQM)) {
        value |= (uint32_t)s->tx_fifo_get << 8;
        value |= size - MIN(size, pending);
    }
    return value;
}

static unsigned bosch_m_can_ack_count(unsigned get, unsigned ack,
                                      unsigned size, unsigned fill)
{
    unsigned count;

    if (!size || !fill || ack >= size) {
        return 0;
    }
    count = (ack + size - get) % size + 1;
    return count <= fill ? count : 0;
}

static void bosch_m_can_ack_rxf(BoschMCanState *s, unsigned fifo,
                                uint32_t value)
{
    unsigned size = fifo ? mcan_rxf1_size(s) : mcan_rxf0_size(s);
    uint8_t *get = fifo ? &s->rxf1_get : &s->rxf0_get;
    uint8_t *fill = fifo ? &s->rxf1_fill : &s->rxf0_fill;
    unsigned ack = value & 0x3f;
    unsigned count = bosch_m_can_ack_count(*get, ack, size, *fill);

    if (count) {
        *fill -= count;
        *get = (ack + 1) % size;
    }
}

static void bosch_m_can_ack_txe(BoschMCanState *s, uint32_t value)
{
    unsigned size = mcan_txe_size(s);
    unsigned ack = value & 0x1f;
    unsigned count = bosch_m_can_ack_count(s->txe_get, ack, size,
                                           s->txe_fill);

    if (count) {
        s->txe_fill -= count;
        s->txe_get = (ack + 1) % size;
    }
}

static bool bosch_m_can_store_rx_element(BoschMCanState *s, hwaddr address,
                                         unsigned data_size,
                                         const qemu_can_frame *frame,
                                         uint16_t timestamp,
                                         unsigned filter_index,
                                         bool nonmatching)
{
    unsigned length = frame->can_dlc;
    unsigned padded_length = 0;
    unsigned write_length;
    uint32_t word0 = 0;
    uint32_t word1;
    uint8_t element[8 + 64] = { 0 };

    if (!bosch_m_can_mram_range(s, address, 8 + data_size)) {
        bosch_m_can_raise_ir(s, MCAN_IR_MRAF);
        return false;
    }

    if (frame->can_id & QEMU_CAN_EFF_FLAG) {
        word0 = (frame->can_id & QEMU_CAN_EFF_MASK) | MCAN_ELEMENT_XTD;
    } else {
        word0 = (frame->can_id & QEMU_CAN_SFF_MASK) << 18;
    }
    if (frame->can_id & QEMU_CAN_RTR_FLAG) {
        word0 |= MCAN_ELEMENT_RTR;
    }
    if (frame->flags & QEMU_CAN_FRMF_ESI) {
        word0 |= MCAN_ELEMENT_ESI;
    }

    word1 = ((uint32_t)can_len2dlc(length) << MCAN_ELEMENT_DLC_SHIFT) |
            timestamp;
    if (nonmatching) {
        word1 |= MCAN_RX_ELEMENT_ANMF;
    } else {
        word1 |= (filter_index & 0x7f) << MCAN_RX_FILTER_INDEX_SHIFT;
    }
    if (frame->flags & QEMU_CAN_FRMF_TYPE_FD) {
        word1 |= MCAN_ELEMENT_FDF;
    }
    if (frame->flags & QEMU_CAN_FRMF_BRS) {
        word1 |= MCAN_ELEMENT_BRS;
    }
    stl_le_p(element, word0);
    stl_le_p(element + 4, word1);
    if (!(frame->can_id & QEMU_CAN_RTR_FLAG)) {
        padded_length = ROUND_UP(MIN(length, data_size), 4);
        memcpy(element + 8, frame->data, MIN(length, data_size));
    }
    write_length = 8 + padded_length;
    return bosch_m_can_mram_write(s, address, element, write_length);
}

static MCanRxStoreResult bosch_m_can_store_rxf(BoschMCanState *s,
                                                unsigned fifo,
                                                const qemu_can_frame *frame,
                                                uint16_t timestamp,
                                                unsigned filter_index,
                                                bool nonmatching,
                                                unsigned *stored_index)
{
    hwaddr config_offset = fifo ? MCAN_RXF1C : MCAN_RXF0C;
    uint32_t config = MCAN_REG(s, config_offset);
    uint32_t start_mask = fifo ? MCAN_RXF1C_F1SA_MASK :
                                 MCAN_RXF0C_F0SA_MASK;
    uint32_t overwrite = fifo ? MCAN_RXF1C_F1OM : MCAN_RXF0C_F0OM;
    unsigned wm_shift = fifo ? MCAN_RXF1C_F1WM_SHIFT :
                               MCAN_RXF0C_F0WM_SHIFT;
    unsigned size = fifo ? mcan_rxf1_size(s) : mcan_rxf0_size(s);
    unsigned data_size = mcan_data_size[extract32(MCAN_REG(s, MCAN_RXESC),
                                                  fifo ? 4 : 0, 3)];
    uint8_t *get = fifo ? &s->rxf1_get : &s->rxf0_get;
    uint8_t *put = fifo ? &s->rxf1_put : &s->rxf0_put;
    uint8_t *fill = fifo ? &s->rxf1_fill : &s->rxf0_fill;
    uint32_t new_ir = fifo ? MCAN_IR_RF1N : MCAN_IR_RF0N;
    uint32_t watermark_ir = fifo ? MCAN_IR_RF1W : MCAN_IR_RF0W;
    uint32_t full_ir = fifo ? MCAN_IR_RF1F : MCAN_IR_RF0F;
    uint32_t lost_ir = fifo ? MCAN_IR_RF1L : MCAN_IR_RF0L;
    unsigned watermark = extract32(config, wm_shift, 7);
    unsigned old_fill = *fill;
    bool full = size && old_fill == size;
    hwaddr address;

    if (!size || (full && !(config & overwrite))) {
        bosch_m_can_raise_ir(s, lost_ir);
        return MCAN_RX_STORE_LOST;
    }

    address = (config & start_mask) + *put * (8 + data_size);
    if (!bosch_m_can_store_rx_element(s, address, data_size, frame,
                                      timestamp, filter_index,
                                      nonmatching)) {
        return MCAN_RX_STORE_ERROR;
    }

    *stored_index = *put;
    *put = (*put + 1) % size;
    if (full) {
        *get = (*get + 1) % size;
    } else {
        (*fill)++;
    }

    bosch_m_can_raise_ir(s, new_ir);
    if (watermark && watermark <= 64 && old_fill < watermark &&
        *fill >= watermark) {
        bosch_m_can_raise_ir(s, watermark_ir);
    }
    if (!full && *fill == size) {
        bosch_m_can_raise_ir(s, full_ir);
    }
    return MCAN_RX_STORE_OK;
}

static bool bosch_m_can_rx_buffer_new_data(BoschMCanState *s,
                                            unsigned index)
{
    hwaddr offset = index < 32 ? MCAN_NDAT1 : MCAN_NDAT2;

    return MCAN_REG(s, offset) & BIT(index & 31);
}

static MCanRxStoreResult bosch_m_can_store_rx_buffer(
    BoschMCanState *s, unsigned index, const qemu_can_frame *frame,
    uint16_t timestamp, unsigned filter_index)
{
    unsigned data_size = mcan_data_size[extract32(MCAN_REG(s, MCAN_RXESC),
                                                  8, 3)];
    hwaddr address = (MCAN_REG(s, MCAN_RXBC) & MCAN_RXBC_RBSA_MASK) +
                     index * (8 + data_size);
    hwaddr ndat_offset = index < 32 ? MCAN_NDAT1 : MCAN_NDAT2;

    if (bosch_m_can_rx_buffer_new_data(s, index)) {
        return MCAN_RX_STORE_LOST;
    }
    if (!bosch_m_can_store_rx_element(s, address, data_size, frame,
                                      timestamp, filter_index, false)) {
        return MCAN_RX_STORE_ERROR;
    }

    MCAN_REG(s, ndat_offset) |= BIT(index & 31);
    bosch_m_can_raise_ir(s, MCAN_IR_DRX);
    return MCAN_RX_STORE_OK;
}

static bool bosch_m_can_standard_filter_matches(uint32_t filter,
                                                unsigned id,
                                                unsigned action)
{
    unsigned id1 = extract32(filter, MCAN_STD_FILTER_ID1_SHIFT, 11);
    unsigned id2 = filter & MCAN_STD_FILTER_ID_MASK;

    if (action == MCAN_FILTER_ACTION_RX_BUFFER) {
        return id == id1;
    }

    switch (extract32(filter, MCAN_STD_FILTER_TYPE_SHIFT, 2)) {
    case 0: /* Range. */
        return id >= id1 && id <= id2;
    case 1: /* Dual ID. */
        return id == id1 || id == id2;
    case 2: /* Classic mask. */
        return (id & id2) == (id1 & id2);
    default: /* SFT == 3 disables the element. */
        return false;
    }
}

static bool bosch_m_can_extended_filter_matches(BoschMCanState *s,
                                                uint32_t word0,
                                                uint32_t word1,
                                                unsigned id,
                                                unsigned action)
{
    unsigned id1 = word0 & MCAN_FILTER_ID_MASK;
    unsigned id2 = word1 & MCAN_FILTER_ID_MASK;
    unsigned type = extract32(word1, MCAN_EXT_FILTER_TYPE_SHIFT, 2);
    unsigned filtered_id = type == 3 ? id : id & MCAN_REG(s, MCAN_XIDAM);

    if (action == MCAN_FILTER_ACTION_RX_BUFFER) {
        return (id & MCAN_REG(s, MCAN_XIDAM)) == id1;
    }

    switch (type) {
    case 0: /* Range, with XIDAM applied to the received identifier. */
    case 3: /* Range, without XIDAM. */
        return filtered_id >= id1 && filtered_id <= id2;
    case 1: /* Dual ID. */
        return filtered_id == id1 || filtered_id == id2;
    case 2: /* Classic mask. */
        return (filtered_id & id2) == (id1 & id2);
    default:
        g_assert_not_reached();
    }
}

static MCanFilterResult bosch_m_can_filter_standard(BoschMCanState *s,
                                                     unsigned id)
{
    MCanFilterResult result = { 0 };
    uint32_t config = MCAN_REG(s, MCAN_SIDFC);
    unsigned count = MIN(extract32(config, MCAN_SIDFC_LSS_SHIFT, 8),
                         128U);
    hwaddr address = config & MCAN_SIDFC_FLSSA_MASK;
    unsigned i;

    for (i = 0; i < count; i++, address += sizeof(uint32_t)) {
        uint32_t filter;
        unsigned action;

        if (!bosch_m_can_mram_read32(s, address, &filter)) {
            result.error = true;
            return result;
        }
        action = extract32(filter, MCAN_STD_FILTER_ACTION_SHIFT, 3);
        if (action == MCAN_FILTER_ACTION_DISABLE ||
            !bosch_m_can_standard_filter_matches(filter, id, action)) {
            continue;
        }

        if (action == MCAN_FILTER_ACTION_RX_BUFFER) {
            unsigned selector = extract32(filter, 9, 2);
            unsigned buffer_index = filter & 0x3f;

            /* Debug-message sequencing and its DMA handshake are separate. */
            if (selector) {
                result.action = MCAN_FILTER_ACTION_REJECT;
            } else if (bosch_m_can_rx_buffer_new_data(s, buffer_index)) {
                continue;
            } else {
                result.action = action;
                result.buffer_index = buffer_index;
            }
        } else {
            result.action = action;
        }
        result.filter_index = i;
        result.matched = true;
        return result;
    }
    return result;
}

static MCanFilterResult bosch_m_can_filter_extended(BoschMCanState *s,
                                                     unsigned id)
{
    MCanFilterResult result = { .extended = true };
    uint32_t config = MCAN_REG(s, MCAN_XIDFC);
    unsigned count = MIN(extract32(config, MCAN_XIDFC_LSE_SHIFT, 7),
                         64U);
    hwaddr address = config & MCAN_XIDFC_FLESA_MASK;
    unsigned i;

    for (i = 0; i < count; i++, address += 2 * sizeof(uint32_t)) {
        uint8_t bytes[2 * sizeof(uint32_t)];
        uint32_t word0;
        uint32_t word1;
        unsigned action;

        if (!bosch_m_can_mram_read(s, address, bytes, sizeof(bytes))) {
            result.error = true;
            return result;
        }
        word0 = ldl_le_p(bytes);
        word1 = ldl_le_p(bytes + sizeof(uint32_t));
        action = extract32(word0, MCAN_EXT_FILTER_ACTION_SHIFT, 3);
        if (action == MCAN_FILTER_ACTION_DISABLE ||
            !bosch_m_can_extended_filter_matches(s, word0, word1, id,
                                                 action)) {
            continue;
        }

        if (action == MCAN_FILTER_ACTION_RX_BUFFER) {
            unsigned selector = extract32(word1, 9, 2);
            unsigned buffer_index = word1 & 0x3f;

            /* Debug-message sequencing and its DMA handshake are separate. */
            if (selector) {
                result.action = MCAN_FILTER_ACTION_REJECT;
            } else if (bosch_m_can_rx_buffer_new_data(s, buffer_index)) {
                continue;
            } else {
                result.action = action;
                result.buffer_index = buffer_index;
            }
        } else {
            result.action = action;
        }
        result.filter_index = i;
        result.matched = true;
        return result;
    }
    return result;
}

static void bosch_m_can_update_priority_status(BoschMCanState *s,
                                               MCanFilterResult filter,
                                               MCanRxStoreResult stored,
                                               unsigned buffer_index)
{
    unsigned msi = 0;

    if (filter.action == MCAN_FILTER_ACTION_PRIORITY_FIFO0 ||
        filter.action == MCAN_FILTER_ACTION_PRIORITY_FIFO1) {
        if (stored == MCAN_RX_STORE_OK) {
            msi = filter.action == MCAN_FILTER_ACTION_PRIORITY_FIFO0 ? 2 : 3;
        } else {
            msi = 1;
            buffer_index = 0;
        }
    } else {
        buffer_index = 0;
    }

    MCAN_REG(s, MCAN_HPMS) = (filter.extended ? BIT(15) : 0) |
                             ((filter.filter_index & 0x7f) << 8) |
                             (msi << 6) | (buffer_index & 0x3f);
    bosch_m_can_raise_ir(s, MCAN_IR_HPM);
}

static bool bosch_m_can_receive_frame(BoschMCanState *s,
                                      const qemu_can_frame *frame,
                                      uint16_t timestamp)
{
    MCanFilterResult filter;
    MCanRxStoreResult stored = MCAN_RX_STORE_OK;
    uint32_t gfc = MCAN_REG(s, MCAN_GFC);
    unsigned buffer_index = 0;
    unsigned action;

    if (!bosch_m_can_active(s) || (frame->can_id & QEMU_CAN_ERR_FLAG)) {
        return false;
    }
    if ((frame->flags & QEMU_CAN_FRMF_TYPE_FD) &&
        !(MCAN_REG(s, MCAN_CCCR) & BIT(8))) {
        return false;
    }
    if (frame->can_dlc > 64 ||
        (!(frame->flags & QEMU_CAN_FRMF_TYPE_FD) && frame->can_dlc > 8)) {
        return false;
    }
    bosch_m_can_record_rx(s, frame);
    if (frame->can_id & QEMU_CAN_RTR_FLAG) {
        bool reject = frame->can_id & QEMU_CAN_EFF_FLAG ?
                      gfc & BIT(0) : gfc & BIT(1);

        if (reject) {
            return true;
        }
    }

    if (frame->can_id & QEMU_CAN_EFF_FLAG) {
        filter = bosch_m_can_filter_extended(s,
                                              frame->can_id &
                                              QEMU_CAN_EFF_MASK);
    } else {
        filter = bosch_m_can_filter_standard(s,
                                              frame->can_id &
                                              QEMU_CAN_SFF_MASK);
    }
    if (filter.error) {
        return true;
    }

    if (!filter.matched) {
        action = frame->can_id & QEMU_CAN_EFF_FLAG ?
                 extract32(gfc, 2, 2) : extract32(gfc, 4, 2);
        if (action < 2) {
            bosch_m_can_store_rxf(s, action, frame, timestamp, 0, true,
                                  &buffer_index);
        }
        return true;
    }

    action = filter.action;
    switch (action) {
    case MCAN_FILTER_ACTION_FIFO0:
    case MCAN_FILTER_ACTION_PRIORITY_FIFO0:
        stored = bosch_m_can_store_rxf(s, 0, frame, timestamp,
                                       filter.filter_index, false,
                                       &buffer_index);
        break;
    case MCAN_FILTER_ACTION_FIFO1:
    case MCAN_FILTER_ACTION_PRIORITY_FIFO1:
        stored = bosch_m_can_store_rxf(s, 1, frame, timestamp,
                                       filter.filter_index, false,
                                       &buffer_index);
        break;
    case MCAN_FILTER_ACTION_RX_BUFFER:
        bosch_m_can_store_rx_buffer(s, filter.buffer_index, frame,
                                    timestamp, filter.filter_index);
        break;
    case MCAN_FILTER_ACTION_REJECT:
    case MCAN_FILTER_ACTION_PRIORITY:
        break;
    default:
        g_assert_not_reached();
    }

    if (action >= MCAN_FILTER_ACTION_PRIORITY &&
        action <= MCAN_FILTER_ACTION_PRIORITY_FIFO1) {
        bosch_m_can_update_priority_status(s, filter, stored,
                                           buffer_index);
    }
    return true;
}

static bool bosch_m_can_can_receive(CanBusClientState *client)
{
    BoschMCanState *s = container_of(client, BoschMCanState, bus_client);

    return bosch_m_can_active(s);
}

static ssize_t bosch_m_can_receive(CanBusClientState *client,
                                   const qemu_can_frame *frames,
                                   size_t frames_count)
{
    BoschMCanState *s = container_of(client, BoschMCanState, bus_client);
    size_t i;
    ssize_t accepted = 0;

    for (i = 0; i < frames_count; i++) {
        uint16_t timestamp = bosch_m_can_capture_timestamp(s);

        if (bosch_m_can_receive_frame(s, &frames[i], timestamp)) {
            accepted++;
        }
    }
    return MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_MON ? 0 : accepted;
}

static CanBusClientInfo bosch_m_can_bus_info = {
    .can_receive = bosch_m_can_can_receive,
    .receive = bosch_m_can_receive,
};

static bool bosch_m_can_fetch_tx(BoschMCanState *s, unsigned index,
                                 qemu_can_frame *frame, uint32_t *word0,
                                 uint32_t *word1)
{
    uint32_t cccr = MCAN_REG(s, MCAN_CCCR);
    unsigned data_size = mcan_data_size[MCAN_REG(s, MCAN_TXESC) & 7];
    unsigned length;
    unsigned padded_length;
    bool fd;
    bool brs;
    bool esi;
    hwaddr address = (MCAN_REG(s, MCAN_TXBC) & MCAN_TXBC_TBSA_MASK) +
                     index * (8 + data_size);

    if (!bosch_m_can_mram_range(s, address, 8 + data_size) ||
        !bosch_m_can_mram_read32(s, address, word0) ||
        !bosch_m_can_mram_read32(s, address + 4, word1)) {
        bosch_m_can_raise_ir(s, MCAN_IR_MRAF);
        return false;
    }

    fd = (cccr & MCAN_CCCR_FDOE) && !(*word0 & MCAN_ELEMENT_RTR) &&
         (*word1 & MCAN_ELEMENT_FDF);
    brs = fd && (cccr & MCAN_CCCR_BRSE) &&
          (*word1 & MCAN_ELEMENT_BRS);
    esi = fd && (*word0 & MCAN_ELEMENT_ESI);

    /* Keep Message RAM intact, but report and transmit the effective format. */
    *word0 &= ~MCAN_ELEMENT_ESI;
    *word1 &= ~(MCAN_ELEMENT_FDF | MCAN_ELEMENT_BRS);
    if (esi) {
        *word0 |= MCAN_ELEMENT_ESI;
    }
    if (fd) {
        *word1 |= MCAN_ELEMENT_FDF;
    }
    if (brs) {
        *word1 |= MCAN_ELEMENT_BRS;
    }

    memset(frame, 0, sizeof(*frame));
    if (*word0 & MCAN_ELEMENT_XTD) {
        frame->can_id = (*word0 & QEMU_CAN_EFF_MASK) | QEMU_CAN_EFF_FLAG;
    } else {
        frame->can_id = (*word0 >> 18) & QEMU_CAN_SFF_MASK;
    }
    if (*word0 & MCAN_ELEMENT_RTR) {
        frame->can_id |= QEMU_CAN_RTR_FLAG;
    }
    if (*word0 & MCAN_ELEMENT_ESI) {
        frame->flags |= QEMU_CAN_FRMF_ESI;
    }
    if (*word1 & MCAN_ELEMENT_FDF) {
        frame->flags |= QEMU_CAN_FRMF_TYPE_FD;
    }
    if (*word1 & MCAN_ELEMENT_BRS) {
        frame->flags |= QEMU_CAN_FRMF_BRS;
    }

    length = can_dlc2len(extract32(*word1, MCAN_ELEMENT_DLC_SHIFT, 4));
    if (!(frame->flags & QEMU_CAN_FRMF_TYPE_FD)) {
        length = MIN(length, 8U);
    }
    frame->can_dlc = length;
    if (!(frame->can_id & QEMU_CAN_RTR_FLAG)) {
        padded_length = ROUND_UP(MIN(length, data_size), 4);
        if (padded_length &&
            !bosch_m_can_mram_read(s, address + 8, frame->data,
                                   padded_length)) {
            return false;
        }
        if (length > data_size) {
            memset(frame->data + data_size, 0xcc, length - data_size);
        }
    }
    return true;
}

static bool bosch_m_can_push_txe(BoschMCanState *s, uint32_t word0,
                                 uint32_t word1, uint16_t timestamp)
{
    unsigned size = mcan_txe_size(s);
    hwaddr address;
    uint32_t event1;
    unsigned watermark;

    if (!size || s->txe_fill == size) {
        bosch_m_can_raise_ir(s, MCAN_IR_TEFL);
        if (size && s->txe_fill == size) {
            bosch_m_can_raise_ir(s, MCAN_IR_TEFF);
        }
        return false;
    }

    address = (MCAN_REG(s, MCAN_TXEFC) & MCAN_TXEFC_EFSA_MASK) +
              s->txe_put * 8;
    event1 = (word1 & (MCAN_ELEMENT_MM_MASK | MCAN_ELEMENT_FDF |
                       MCAN_ELEMENT_BRS | (0xfU << MCAN_ELEMENT_DLC_SHIFT))) |
             MCAN_TX_EVENT_ET_TX | timestamp;
    if (!bosch_m_can_mram_write32(s, address, word0) ||
        !bosch_m_can_mram_write32(s, address + 4, event1)) {
        return false;
    }

    s->txe_put = (s->txe_put + 1) % size;
    s->txe_fill++;
    bosch_m_can_raise_ir(s, MCAN_IR_TEFN);
    watermark = extract32(MCAN_REG(s, MCAN_TXEFC),
                          MCAN_TXEFC_EFWM_SHIFT, 6);
    if (watermark && s->txe_fill >= watermark) {
        bosch_m_can_raise_ir(s, MCAN_IR_TEFW);
    }
    if (s->txe_fill == size) {
        bosch_m_can_raise_ir(s, MCAN_IR_TEFF);
    }
    return true;
}

static void bosch_m_can_advance_tx_get(BoschMCanState *s, unsigned index)
{
    unsigned first = mcan_tx_ndtb(s);
    unsigned size = mcan_tx_fifo_size(s);

    if (size && index == s->tx_fifo_get && index >= first &&
        index < first + size) {
        s->tx_fifo_get = first + (index - first + 1) % size;
    }
}

static bool bosch_m_can_transmit_one(BoschMCanState *s, unsigned index)
{
    qemu_can_frame frame;
    uint32_t word0;
    uint32_t word1;
    uint16_t timestamp;
    bool loopback;
    bool internal_loopback;
    bool transmitted = false;

    if (!bosch_m_can_fetch_tx(s, index, &frame, &word0, &word1)) {
        return false;
    }

    timestamp = bosch_m_can_capture_timestamp(s);
    loopback = (MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_TEST) &&
               (MCAN_REG(s, MCAN_TEST) & MCAN_TEST_LBCK);
    internal_loopback = loopback &&
                        (MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_MON);

    if (loopback) {
        bosch_m_can_receive_frame(s, &frame, timestamp);
        transmitted = true; /* Loopback ignores acknowledge errors. */
    }
    if (!internal_loopback && s->bus_client.bus) {
        transmitted |= can_bus_client_send(&s->bus_client, &frame, 1) > 0;
    }
    if (!transmitted) {
        /* CanBus has no arbitration/ACK/retry/error event API. */
        return false;
    }

    bosch_m_can_record_success(s, &frame);
    MCAN_REG(s, MCAN_TXBRP) &= ~BIT(index);
    MCAN_REG(s, MCAN_TXBTO) |= BIT(index);
    bosch_m_can_advance_tx_get(s, index);
    if (MCAN_REG(s, MCAN_TXBTIE) & BIT(index)) {
        bosch_m_can_raise_ir(s, MCAN_IR_TC);
    }
    if (word1 & MCAN_ELEMENT_EFC) {
        bosch_m_can_push_txe(s, word0, word1, timestamp);
    }
    if (!bosch_m_can_tx_fifo_pending(s)) {
        bosch_m_can_raise_ir(s, MCAN_IR_TFE);
    }
    return true;
}

static uint32_t bosch_m_can_configured_tx_mask(BoschMCanState *s)
{
    unsigned count = MIN(mcan_tx_ndtb(s) + mcan_tx_fifo_size(s), 32U);

    return count == 32 ? UINT32_MAX : count ? (1U << count) - 1 : 0;
}

static void bosch_m_can_process_tx(BoschMCanState *s)
{
    uint32_t pending;
    unsigned index;

    if (!bosch_m_can_active(s) ||
        ((MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_MON) &&
         !((MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_TEST) &&
           (MCAN_REG(s, MCAN_TEST) & MCAN_TEST_LBCK)))) {
        return;
    }

    pending = MCAN_REG(s, MCAN_TXBRP);
    for (index = 0; index < 32; index++) {
        if (pending & BIT(index)) {
            bosch_m_can_transmit_one(s, index);
        }
    }
}

static void bosch_m_can_tx_add(BoschMCanState *s, uint32_t value)
{
    unsigned first = mcan_tx_ndtb(s);
    unsigned size = mcan_tx_fifo_size(s);
    uint32_t requests = value & bosch_m_can_configured_tx_mask(s);

    MCAN_REG(s, MCAN_TXBTO) &= ~requests;
    MCAN_REG(s, MCAN_TXBCF) &= ~requests;
    MCAN_REG(s, MCAN_TXBRP) |= requests;
    if (size && (requests & BIT(s->tx_fifo_put))) {
        s->tx_fifo_put = first + (s->tx_fifo_put - first + 1) % size;
    }
    bosch_m_can_process_tx(s);
}

static void bosch_m_can_tx_cancel(BoschMCanState *s, uint32_t value)
{
    uint32_t cancelled = value & MCAN_REG(s, MCAN_TXBRP) &
                         bosch_m_can_configured_tx_mask(s);

    MCAN_REG(s, MCAN_TXBRP) &= ~cancelled;
    MCAN_REG(s, MCAN_TXBCF) |= cancelled;
    if (cancelled & MCAN_REG(s, MCAN_TXBCIE)) {
        bosch_m_can_raise_ir(s, MCAN_IR_TCF);
    }
}

static bool bosch_m_can_config_enabled(BoschMCanState *s)
{
    return (MCAN_REG(s, MCAN_CCCR) &
            (MCAN_CCCR_INIT | MCAN_CCCR_CCE)) ==
           (MCAN_CCCR_INIT | MCAN_CCCR_CCE);
}

static uint32_t bosch_m_can_register_mask(BoschMCanState *s, hwaddr offset)
{
    switch (offset) {
    case MCAN_DBTP:  return s->dbtp_mask;
    case MCAN_RWD:   return MCAN_RWD_WDC_MASK;
    case MCAN_NBTP:  return 0xffffff7f;
    case MCAN_TSCC:  return 0x000f0003;
    case MCAN_TOCC:  return 0xffff0007;
    case MCAN_TDCR:  return 0x00007f7f;
    case MCAN_GFC:   return 0x0000003f;
    case MCAN_SIDFC: return 0x00fffffc;
    case MCAN_XIDFC: return 0x007ffffc;
    case MCAN_XIDAM: return 0x1fffffff;
    case MCAN_RXF0C: return 0xff7ffffc;
    case MCAN_RXBC:  return 0x0000fffc;
    case MCAN_RXF1C: return 0xff7ffffc;
    case MCAN_RXESC: return 0x00000777;
    case MCAN_TXBC:  return 0x7f3ffffc;
    case MCAN_TXESC: return 0x00000007;
    case MCAN_TXEFC: return 0x3f3ffffc;
    default:         return 0;
    }
}

static void bosch_m_can_write_cccr(BoschMCanState *s, uint32_t value)
{
    uint32_t old = MCAN_REG(s, MCAN_CCCR);
    uint32_t next = old;
    uint32_t protected_mask = MCAN_CCCR_MASK &
                              ~(MCAN_CCCR_DIRECT_MASK | MCAN_CCCR_CSA);

    value &= MCAN_CCCR_MASK;
    next = (next & ~MCAN_CCCR_INIT) | (value & MCAN_CCCR_INIT);
    next = (next & ~MCAN_CCCR_CSR) | (value & MCAN_CCCR_CSR);
    if (next & MCAN_CCCR_INIT) {
        next = (next & ~MCAN_CCCR_CCE) | (value & MCAN_CCCR_CCE);
    } else {
        next &= ~MCAN_CCCR_CCE;
    }
    if ((old & (MCAN_CCCR_INIT | MCAN_CCCR_CCE)) ==
        (MCAN_CCCR_INIT | MCAN_CCCR_CCE)) {
        next = (next & ~protected_mask) | (value & protected_mask);
    }
    if (next & MCAN_CCCR_CSR) {
        if (!MCAN_REG(s, MCAN_TXBRP)) {
            next |= MCAN_CCCR_INIT | MCAN_CCCR_CSA;
        }
    } else {
        next &= ~MCAN_CCCR_CSA;
    }
    if (!(next & MCAN_CCCR_TEST)) {
        MCAN_REG(s, MCAN_TEST) = 0;
    }
    if (next & MCAN_CCCR_CCE) {
        /*
         * M_CAN initialization resets handler state, but not the latched
         * interrupt register or Message RAM contents.
         */
        MCAN_REG(s, MCAN_HPMS) = 0;
        s->rxf0_get = s->rxf0_put = s->rxf0_fill = 0;
        s->rxf1_get = s->rxf1_put = s->rxf1_fill = 0;
        MCAN_REG(s, MCAN_TXBRP) = 0;
        MCAN_REG(s, MCAN_TXBTO) = 0;
        MCAN_REG(s, MCAN_TXBCF) = 0;
        s->tx_fifo_get = s->tx_fifo_put = mcan_tx_ndtb(s);
        s->txe_get = s->txe_put = s->txe_fill = 0;
        MCAN_REG(s, MCAN_TOCV) = MCAN_REG(s, MCAN_TOCC) >> 16;
    }
    MCAN_REG(s, MCAN_CCCR) = next;
    bosch_m_can_process_tx(s);
}

static uint64_t bosch_m_can_read(void *opaque, hwaddr offset,
                                 unsigned size G_GNUC_UNUSED)
{
    BoschMCanState *s = opaque;

    switch (offset) {
    case MCAN_CREL:
        /* SAM9X7 reserves this Linux-required value; probe real silicon. */
        return s->crel;
    case MCAN_ENDN:
        return 0x87654321;
    case MCAN_CUST:
        return 0;
    case MCAN_ECR: {
        uint32_t value = MCAN_REG(s, offset);

        MCAN_REG(s, offset) &= ~MCAN_ECR_CEL_MASK;
        return value;
    }
    case MCAN_PSR: {
        uint32_t value = MCAN_REG(s, offset);

        MCAN_REG(s, offset) &= ~(MCAN_PSR_LEC_MASK | MCAN_PSR_DLEC_MASK |
                                 MCAN_PSR_READ_CLEAR);
        MCAN_REG(s, offset) |= MCAN_PSR_LEC_MASK | MCAN_PSR_DLEC_MASK;
        return value;
    }
    case MCAN_TSCV:
        switch (MCAN_REG(s, MCAN_TSCC) & MCAN_TSCC_TSS_MASK) {
        case MCAN_TSCC_TSS_INTERNAL:
        case MCAN_TSCC_TSS_EXTERNAL:
            return s->timestamp_counter;
        default:
            return 0;
        }
    case MCAN_TSU_TSCFG: {
        uint32_t value = MCAN_REG(s, offset);

        if (s->tsu_destructive_read) {
            MCAN_REG(s, offset) = 0;
        }
        return value;
    }
    case MCAN_TSU_ATB: {
        uint32_t value = MCAN_REG(s, offset);

        if (s->tsu_destructive_read) {
            MCAN_REG(s, MCAN_TSU_TSCFG) &= ~MCAN_TSCFG_TBPRE_MASK;
        }
        return value;
    }
    case MCAN_RXF0S:
        return bosch_m_can_rxf_status(s, 0);
    case MCAN_RXF1S:
        return bosch_m_can_rxf_status(s, 1);
    case MCAN_TXFQS:
        return bosch_m_can_txfqs(s);
    case MCAN_TXEFS:
        return bosch_m_can_txe_status(s);
    default:
        return MCAN_REG(s, offset); /* Reserved words decode as zero. */
    }
}

static void bosch_m_can_write_protected(BoschMCanState *s, hwaddr offset,
                                        uint32_t value)
{
    if (!bosch_m_can_config_enabled(s)) {
        return;
    }

    MCAN_REG(s, offset) = value & bosch_m_can_register_mask(s, offset);
    switch (offset) {
    case MCAN_RXF0C:
        s->rxf0_get = s->rxf0_put = s->rxf0_fill = 0;
        break;
    case MCAN_RXF1C:
        s->rxf1_get = s->rxf1_put = s->rxf1_fill = 0;
        break;
    case MCAN_TXBC:
        MCAN_REG(s, MCAN_TXBRP) = 0;
        MCAN_REG(s, MCAN_TXBTO) = 0;
        MCAN_REG(s, MCAN_TXBCF) = 0;
        s->tx_fifo_get = s->tx_fifo_put = mcan_tx_ndtb(s);
        break;
    case MCAN_TXEFC:
        s->txe_get = s->txe_put = s->txe_fill = 0;
        break;
    default:
        break;
    }
}

static void bosch_m_can_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size G_GNUC_UNUSED)
{
    BoschMCanState *s = opaque;
    uint32_t v = value;

    switch (offset) {
    case MCAN_CCCR:
        bosch_m_can_write_cccr(s, v);
        break;
    case MCAN_TEST:
        if (MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_TEST) {
            MCAN_REG(s, MCAN_TEST) = v & MCAN_TEST_WRITABLE_MASK;
        }
        break;
    case MCAN_RWD:
        if (s->rwd_unprotected) {
            MCAN_REG(s, MCAN_RWD) = v & MCAN_RWD_WDC_MASK;
        } else {
            bosch_m_can_write_protected(s, offset, v);
        }
        break;
    case MCAN_DBTP: case MCAN_NBTP: case MCAN_TSCC:
    case MCAN_TOCC: case MCAN_TDCR: case MCAN_GFC: case MCAN_SIDFC:
    case MCAN_XIDFC: case MCAN_XIDAM: case MCAN_RXF0C: case MCAN_RXBC:
    case MCAN_RXF1C: case MCAN_RXESC: case MCAN_TXBC: case MCAN_TXESC:
    case MCAN_TXEFC:
        bosch_m_can_write_protected(s, offset, v);
        break;
    case MCAN_TSCV:
        if ((MCAN_REG(s, MCAN_TSCC) & MCAN_TSCC_TSS_MASK) !=
            MCAN_TSCC_TSS_EXTERNAL) {
            s->timestamp_counter = 0;
        }
        break;
    case MCAN_TOCV:
        if (extract32(MCAN_REG(s, MCAN_TOCC), MCAN_TOCC_TOS_SHIFT,
                      MCAN_TOCC_TOS_LEN) == 0) {
            MCAN_REG(s, MCAN_TOCV) =
                extract32(MCAN_REG(s, MCAN_TOCC), MCAN_TOCC_TOP_SHIFT, 16);
        }
        break;
    case MCAN_IR:
        MCAN_REG(s, MCAN_IR) &= ~(v & MCAN_IR_MASK);
        bosch_m_can_update_irq(s);
        break;
    case MCAN_IE:
        MCAN_REG(s, MCAN_IE) = v & MCAN_IR_MASK;
        bosch_m_can_update_irq(s);
        break;
    case MCAN_ILS:
        MCAN_REG(s, MCAN_ILS) = v & MCAN_IR_MASK;
        bosch_m_can_update_irq(s);
        break;
    case MCAN_ILE:
        MCAN_REG(s, MCAN_ILE) = v & 3;
        bosch_m_can_update_irq(s);
        break;
    case MCAN_NDAT1: case MCAN_NDAT2:
        MCAN_REG(s, offset) &= ~v;
        break;
    case MCAN_RXF0A:
        bosch_m_can_ack_rxf(s, 0, v);
        break;
    case MCAN_RXF1A:
        bosch_m_can_ack_rxf(s, 1, v);
        break;
    case MCAN_TXBAR:
        if (!(MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_CCE)) {
            bosch_m_can_tx_add(s, v);
        }
        break;
    case MCAN_TXBCR:
        if (!(MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_CCE)) {
            bosch_m_can_tx_cancel(s, v);
        }
        break;
    case MCAN_TXBTIE: case MCAN_TXBCIE:
        MCAN_REG(s, offset) = v;
        break;
    case MCAN_TXEFA:
        bosch_m_can_ack_txe(s, v);
        break;
    case MCAN_TSU_TSCFG:
        if (bosch_m_can_config_enabled(s)) {
            MCAN_REG(s, offset) = v & MCAN_TSCFG_MASK;
        }
        break;
    case MCAN_CREL: case MCAN_ENDN: case MCAN_CUST: case MCAN_ECR:
    case MCAN_PSR: case MCAN_HPMS: case MCAN_RXF0S: case MCAN_RXF1S:
    case MCAN_TXFQS: case MCAN_TXBRP: case MCAN_TXBTO: case MCAN_TXBCF:
    case MCAN_TXEFS: case MCAN_TSU_TSS1: case MCAN_TSU_TSS2:
    case MCAN_TSU_ATB:
        break;
    default:
        break;
    }
}

static const MemoryRegionOps bosch_m_can_ops = {
    .read = bosch_m_can_read,
    .write = bosch_m_can_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void bosch_m_can_clock_changed(void *opaque,
                                      ClockEvent event G_GNUC_UNUSED)
{
    BoschMCanState *s = opaque;

    if (s->resources_ready) {
        bosch_m_can_process_tx(s);
        bosch_m_can_update_irq(s);
    }
}

static void bosch_m_can_reset(DeviceState *dev)
{
    BoschMCanState *s = BOSCH_M_CAN(dev);

    memset(s->regs, 0, sizeof(s->regs));
    MCAN_REG(s, MCAN_DBTP) = 0x00000a33 & s->dbtp_mask;
    MCAN_REG(s, MCAN_CCCR) = MCAN_CCCR_INIT;
    MCAN_REG(s, MCAN_NBTP) = 0x06000a03;
    MCAN_REG(s, MCAN_TOCC) = 0xffff0000;
    MCAN_REG(s, MCAN_TOCV) = 0x0000ffff;
    MCAN_REG(s, MCAN_PSR) = 0x00000707;
    MCAN_REG(s, MCAN_XIDAM) = 0x1fffffff;
    /* TODO: DFP reset conflicts with TSS2 field positions; probe +0x16c. */
    MCAN_REG(s, MCAN_TSU_TSS2) = s->sam_tss2_reset;

    s->rxf0_get = s->rxf0_put = s->rxf0_fill = 0;
    s->rxf1_get = s->rxf1_put = s->rxf1_fill = 0;
    s->tx_fifo_get = s->tx_fifo_put = 0;
    s->txe_get = s->txe_put = s->txe_fill = 0;
    s->timestamp_counter = 0;
    /* Shared system SRAM is deliberately not cleared by peripheral reset. */
    bosch_m_can_update_irq(s);
}

static void bosch_m_can_init(Object *obj)
{
    BoschMCanState *s = BOSCH_M_CAN(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &bosch_m_can_ops, s,
                          TYPE_BOSCH_M_CAN, BOSCH_M_CAN_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[0]);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq[1]);
    s->hclk = qdev_init_clock_in(dev, "hclk", bosch_m_can_clock_changed,
                                 s, ClockUpdate);
    s->cclk = qdev_init_clock_in(dev, "cclk", bosch_m_can_clock_changed,
                                 s, ClockUpdate);
}

static void bosch_m_can_cleanup(BoschMCanState *s)
{
    s->resources_ready = false;
    can_bus_remove_client(&s->bus_client);
    if (s->message_ram_as) {
        address_space_destroy_free(s->message_ram_as);
        s->message_ram_as = NULL;
        memory_region_del_subregion(&s->message_ram_root,
                                    &s->message_ram_alias);
        object_unparent(OBJECT(&s->message_ram_alias));
        object_unparent(OBJECT(&s->message_ram_root));
    }
}

static void bosch_m_can_realize(DeviceState *dev, Error **errp)
{
    BoschMCanState *s = BOSCH_M_CAN(dev);

    if (!s->message_ram) {
        error_setg(errp, TYPE_BOSCH_M_CAN ": message-ram link is not set");
        return;
    }
    if (!clock_has_source(s->hclk) || !clock_has_source(s->cclk)) {
        error_setg(errp, TYPE_BOSCH_M_CAN
                   ": hclk and cclk must be connected");
        return;
    }

    /*
     * A terminating root prevents FlatView from collapsing the full-size
     * alias back to the linked RAM's address in its system container.
     */
    memory_region_init_io(&s->message_ram_root, OBJECT(dev), NULL, NULL,
                          TYPE_BOSCH_M_CAN "-message-ram-root",
                          memory_region_size(s->message_ram));
    memory_region_init_alias(&s->message_ram_alias, OBJECT(dev),
                             TYPE_BOSCH_M_CAN "-message-ram-alias",
                             s->message_ram, 0,
                             memory_region_size(s->message_ram));
    memory_region_add_subregion(&s->message_ram_root, 0,
                                &s->message_ram_alias);
    s->message_ram_as = g_new0(AddressSpace, 1);
    address_space_init(s->message_ram_as, &s->message_ram_root,
                       TYPE_BOSCH_M_CAN "-message-ram");
    s->bus_client.info = &bosch_m_can_bus_info;
    if (s->canbus && can_bus_insert_client(s->canbus,
                                           &s->bus_client) < 0) {
        bosch_m_can_cleanup(s);
        error_setg(errp, TYPE_BOSCH_M_CAN ": CAN bus insertion failed");
        return;
    }
    s->resources_ready = true;
    bosch_m_can_update_irq(s);
}

static void bosch_m_can_unrealize(DeviceState *dev)
{
    bosch_m_can_cleanup(BOSCH_M_CAN(dev));
}

static int bosch_m_can_post_load(void *opaque, int version_id)
{
    BoschMCanState *s = opaque;
    unsigned size;

    MCAN_REG(s, MCAN_IR) &= MCAN_IR_MASK;
    MCAN_REG(s, MCAN_IE) &= MCAN_IR_MASK;
    MCAN_REG(s, MCAN_ILS) &= MCAN_IR_MASK;
    MCAN_REG(s, MCAN_ILE) &= 3;
    MCAN_REG(s, MCAN_DBTP) &= s->dbtp_mask;
    MCAN_REG(s, MCAN_RWD) &= MCAN_RWD_WDC_MASK;
    MCAN_REG(s, MCAN_TOCV) &= 0xffff;
    MCAN_REG(s, MCAN_TSU_TSCFG) &= MCAN_TSCFG_MASK;
    if (MCAN_REG(s, MCAN_CCCR) & MCAN_CCCR_TEST) {
        MCAN_REG(s, MCAN_TEST) &= MCAN_TEST_WRITABLE_MASK;
    } else {
        MCAN_REG(s, MCAN_TEST) = 0;
    }

    size = mcan_rxf0_size(s);
    if (size) {
        s->rxf0_get %= size;
        s->rxf0_put %= size;
        s->rxf0_fill = MIN(s->rxf0_fill, size);
    } else {
        s->rxf0_get = s->rxf0_put = s->rxf0_fill = 0;
    }
    size = mcan_rxf1_size(s);
    if (size) {
        s->rxf1_get %= size;
        s->rxf1_put %= size;
        s->rxf1_fill = MIN(s->rxf1_fill, size);
    } else {
        s->rxf1_get = s->rxf1_put = s->rxf1_fill = 0;
    }
    size = mcan_txe_size(s);
    if (size) {
        s->txe_get %= size;
        s->txe_put %= size;
        s->txe_fill = MIN(s->txe_fill, size);
    } else {
        s->txe_get = s->txe_put = s->txe_fill = 0;
    }
    size = mcan_tx_fifo_size(s);
    if (size) {
        unsigned first = mcan_tx_ndtb(s);

        if (s->tx_fifo_get < first || s->tx_fifo_get >= first + size) {
            s->tx_fifo_get = first;
        }
        if (s->tx_fifo_put < first || s->tx_fifo_put >= first + size) {
            s->tx_fifo_put = first;
        }
    } else {
        s->tx_fifo_get = s->tx_fifo_put = 0;
    }
    bosch_m_can_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_bosch_m_can = {
    .name = TYPE_BOSCH_M_CAN,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = bosch_m_can_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, BoschMCanState, BOSCH_M_CAN_REG_WORDS),
        VMSTATE_UINT8(rxf0_get, BoschMCanState),
        VMSTATE_UINT8(rxf0_put, BoschMCanState),
        VMSTATE_UINT8(rxf0_fill, BoschMCanState),
        VMSTATE_UINT8_V(rxf1_get, BoschMCanState, 2),
        VMSTATE_UINT8_V(rxf1_put, BoschMCanState, 2),
        VMSTATE_UINT8_V(rxf1_fill, BoschMCanState, 2),
        VMSTATE_UINT8(tx_fifo_get, BoschMCanState),
        VMSTATE_UINT8(tx_fifo_put, BoschMCanState),
        VMSTATE_UINT8(txe_get, BoschMCanState),
        VMSTATE_UINT8(txe_put, BoschMCanState),
        VMSTATE_UINT8(txe_fill, BoschMCanState),
        VMSTATE_UINT16(timestamp_counter, BoschMCanState),
        VMSTATE_CLOCK(hclk, BoschMCanState),
        VMSTATE_CLOCK(cclk, BoschMCanState),
        VMSTATE_END_OF_LIST(),
    },
};

static const Property bosch_m_can_properties[] = {
    /* Provisional Linux-functional 3.3 value; hardware must provide final. */
    DEFINE_PROP_UINT32("crel", BoschMCanState, crel, 0x33000000),
    DEFINE_PROP_UINT32("sam-tss2-reset", BoschMCanState, sam_tss2_reset,
                       MCAN_TSS2_DOCUMENTED_RESET),
    DEFINE_PROP_UINT32("dbtp-mask", BoschMCanState, dbtp_mask,
                       MCAN_DBTP_GENERIC_MASK),
    DEFINE_PROP_BOOL("tsu-destructive-read", BoschMCanState,
                     tsu_destructive_read, false),
    DEFINE_PROP_BOOL("rwd-unprotected", BoschMCanState, rwd_unprotected,
                     false),
    DEFINE_PROP_LINK("message-ram", BoschMCanState, message_ram,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_LINK("canbus", BoschMCanState, canbus, TYPE_CAN_BUS,
                     CanBusState *),
};

static void bosch_m_can_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Bosch M_CAN Controller";
    dc->realize = bosch_m_can_realize;
    dc->unrealize = bosch_m_can_unrealize;
    dc->vmsd = &vmstate_bosch_m_can;
    device_class_set_legacy_reset(dc, bosch_m_can_reset);
    device_class_set_props(dc, bosch_m_can_properties);
}

static const TypeInfo bosch_m_can_info = {
    .name = TYPE_BOSCH_M_CAN,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(BoschMCanState),
    .instance_init = bosch_m_can_init,
    .class_init = bosch_m_can_class_init,
};

static void bosch_m_can_register_types(void)
{
    type_register_static(&bosch_m_can_info);
}

type_init(bosch_m_can_register_types)
