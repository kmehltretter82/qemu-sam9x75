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

#define MCAN_IR_RF0N        BIT(0)
#define MCAN_IR_RF0W        BIT(1)
#define MCAN_IR_RF0F        BIT(2)
#define MCAN_IR_RF0L        BIT(3)
#define MCAN_IR_TC          BIT(9)
#define MCAN_IR_TCF         BIT(10)
#define MCAN_IR_TFE         BIT(11)
#define MCAN_IR_TEFN        BIT(12)
#define MCAN_IR_TEFW        BIT(13)
#define MCAN_IR_TEFF        BIT(14)
#define MCAN_IR_TEFL        BIT(15)
#define MCAN_IR_TSW         BIT(16)
#define MCAN_IR_MRAF        BIT(17)
#define MCAN_IR_MASK        0x3fcfffff

#define MCAN_ILE_EINT0      BIT(0)
#define MCAN_ILE_EINT1      BIT(1)

#define MCAN_RXF0C_F0SA_MASK 0x0000fffc
#define MCAN_RXF0C_F0S_SHIFT 16
#define MCAN_RXF0C_F0WM_SHIFT 24
#define MCAN_RXF0C_F0OM      BIT(31)

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
#define MCAN_TSS2_DOCUMENTED_RESET 0x000a0000

static const uint8_t mcan_data_size[8] = {
    8, 12, 16, 20, 24, 32, 48, 64,
};

static void bosch_m_can_process_tx(BoschMCanState *s);

static unsigned mcan_rxf0_size(BoschMCanState *s)
{
    return MIN(extract32(MCAN_REG(s, MCAN_RXF0C), MCAN_RXF0C_F0S_SHIFT, 7),
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
    uint16_t timestamp = s->timestamp_counter;

    /* Untimed approximation: internal TSS advances once per frame. */
    if ((MCAN_REG(s, MCAN_TSCC) & 3) == 1) {
        s->timestamp_counter++;
        if (s->timestamp_counter == 0) {
            bosch_m_can_raise_ir(s, MCAN_IR_TSW);
        }
    }
    return timestamp;
}

static uint32_t bosch_m_can_rxf0_status(BoschMCanState *s)
{
    unsigned size = mcan_rxf0_size(s);
    uint32_t value = s->rxf0_fill |
                     ((uint32_t)s->rxf0_get << 8) |
                     ((uint32_t)s->rxf0_put << 16);

    if (size && s->rxf0_fill == size) {
        value |= BIT(24);
    }
    if (MCAN_REG(s, MCAN_IR) & MCAN_IR_RF0L) {
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

static void bosch_m_can_ack_rxf0(BoschMCanState *s, uint32_t value)
{
    unsigned size = mcan_rxf0_size(s);
    unsigned ack = value & 0x3f;
    unsigned count = bosch_m_can_ack_count(s->rxf0_get, ack, size,
                                           s->rxf0_fill);

    if (count) {
        s->rxf0_fill -= count;
        s->rxf0_get = (ack + 1) % size;
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

static bool bosch_m_can_store_rxf0(BoschMCanState *s,
                                   const qemu_can_frame *frame,
                                   uint16_t timestamp)
{
    unsigned size = mcan_rxf0_size(s);
    unsigned data_size = mcan_data_size[MCAN_REG(s, MCAN_RXESC) & 7];
    unsigned length = frame->can_dlc;
    unsigned padded_length;
    hwaddr address;
    uint32_t word0 = 0;
    uint32_t word1;
    uint8_t data[64] = { 0 };

    if (!size) {
        bosch_m_can_raise_ir(s, MCAN_IR_RF0L);
        return false;
    }
    if (s->rxf0_fill == size) {
        if (!(MCAN_REG(s, MCAN_RXF0C) & MCAN_RXF0C_F0OM)) {
            bosch_m_can_raise_ir(s, MCAN_IR_RF0L);
            return false;
        }
        s->rxf0_get = (s->rxf0_get + 1) % size;
        s->rxf0_fill--;
    }

    address = (MCAN_REG(s, MCAN_RXF0C) & MCAN_RXF0C_F0SA_MASK) +
              s->rxf0_put * (8 + data_size);
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

    word1 = MCAN_RX_ELEMENT_ANMF |
            ((uint32_t)can_len2dlc(length) << MCAN_ELEMENT_DLC_SHIFT) |
            timestamp;
    if (frame->flags & QEMU_CAN_FRMF_TYPE_FD) {
        word1 |= MCAN_ELEMENT_FDF;
    }
    if (frame->flags & QEMU_CAN_FRMF_BRS) {
        word1 |= MCAN_ELEMENT_BRS;
    }
    if (!bosch_m_can_mram_write32(s, address, word0) ||
        !bosch_m_can_mram_write32(s, address + 4, word1)) {
        return false;
    }

    if (!(frame->can_id & QEMU_CAN_RTR_FLAG)) {
        padded_length = ROUND_UP(MIN(length, data_size), 4);
        memcpy(data, frame->data, MIN(length, data_size));
        if (padded_length &&
            !bosch_m_can_mram_write(s, address + 8, data, padded_length)) {
            return false;
        }
    }

    s->rxf0_put = (s->rxf0_put + 1) % size;
    s->rxf0_fill++;
    bosch_m_can_raise_ir(s, MCAN_IR_RF0N);
    if (extract32(MCAN_REG(s, MCAN_RXF0C), MCAN_RXF0C_F0WM_SHIFT, 7) &&
        s->rxf0_fill >= extract32(MCAN_REG(s, MCAN_RXF0C),
                                  MCAN_RXF0C_F0WM_SHIFT, 7)) {
        bosch_m_can_raise_ir(s, MCAN_IR_RF0W);
    }
    if (s->rxf0_fill == size) {
        bosch_m_can_raise_ir(s, MCAN_IR_RF0F);
    }
    return true;
}

static bool bosch_m_can_receive_frame(BoschMCanState *s,
                                      const qemu_can_frame *frame,
                                      uint16_t timestamp)
{
    uint32_t gfc = MCAN_REG(s, MCAN_GFC);
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
    if (frame->can_id & QEMU_CAN_RTR_FLAG) {
        bool reject = frame->can_id & QEMU_CAN_EFF_FLAG ?
                      gfc & BIT(0) : gfc & BIT(1);

        if (reject) {
            return true;
        }
    }

    /* Only the no-filter/global FIFO0 path used by the SAM9X7 DTS is here. */
    action = frame->can_id & QEMU_CAN_EFF_FLAG ?
             extract32(gfc, 2, 2) : extract32(gfc, 4, 2);
    if (action != 0) {
        return true;
    }
    bosch_m_can_store_rxf0(s, frame, timestamp);
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
    unsigned data_size = mcan_data_size[MCAN_REG(s, MCAN_TXESC) & 7];
    unsigned length;
    unsigned padded_length;
    hwaddr address = (MCAN_REG(s, MCAN_TXBC) & MCAN_TXBC_TBSA_MASK) +
                     index * (8 + data_size);

    if (!bosch_m_can_mram_range(s, address, 8 + data_size) ||
        !bosch_m_can_mram_read32(s, address, word0) ||
        !bosch_m_can_mram_read32(s, address + 4, word1)) {
        bosch_m_can_raise_ir(s, MCAN_IR_MRAF);
        return false;
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

static uint32_t bosch_m_can_register_mask(hwaddr offset)
{
    switch (offset) {
    case MCAN_DBTP:  return 0x009f1fff;
    case MCAN_RWD:   return 0x0000ffff;
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
    case MCAN_TSCV:
        return s->timestamp_counter;
    case MCAN_RXF0S:
        return bosch_m_can_rxf0_status(s);
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

    MCAN_REG(s, offset) = value & bosch_m_can_register_mask(offset);
    switch (offset) {
    case MCAN_RXF0C:
        s->rxf0_get = s->rxf0_put = s->rxf0_fill = 0;
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
            MCAN_REG(s, MCAN_TEST) = v & 0x003f3ff0;
        }
        break;
    case MCAN_DBTP: case MCAN_RWD: case MCAN_NBTP: case MCAN_TSCC:
    case MCAN_TOCC: case MCAN_TDCR: case MCAN_GFC: case MCAN_SIDFC:
    case MCAN_XIDFC: case MCAN_XIDAM: case MCAN_RXF0C: case MCAN_RXBC:
    case MCAN_RXF1C: case MCAN_RXESC: case MCAN_TXBC: case MCAN_TXESC:
    case MCAN_TXEFC:
        bosch_m_can_write_protected(s, offset, v);
        break;
    case MCAN_TSCV:
        s->timestamp_counter = 0;
        break;
    case MCAN_TOCV:
        MCAN_REG(s, MCAN_TOCV) = v & 0xffff;
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
        bosch_m_can_ack_rxf0(s, v);
        break;
    case MCAN_RXF1A:
        break; /* FIFO1 is not in this first slice. */
    case MCAN_TXBAR:
        bosch_m_can_tx_add(s, v);
        break;
    case MCAN_TXBCR:
        bosch_m_can_tx_cancel(s, v);
        break;
    case MCAN_TXBTIE: case MCAN_TXBCIE:
        MCAN_REG(s, offset) = v;
        break;
    case MCAN_TXEFA:
        bosch_m_can_ack_txe(s, v);
        break;
    case MCAN_TSU_TSCFG:
        MCAN_REG(s, offset) = v & MCAN_TSCFG_MASK;
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
    MCAN_REG(s, MCAN_DBTP) = 0x00000a33;
    MCAN_REG(s, MCAN_CCCR) = MCAN_CCCR_INIT;
    MCAN_REG(s, MCAN_NBTP) = 0x06000a03;
    MCAN_REG(s, MCAN_TOCC) = 0xffff0000;
    MCAN_REG(s, MCAN_TOCV) = 0x0000ffff;
    MCAN_REG(s, MCAN_PSR) = 0x00000707;
    MCAN_REG(s, MCAN_XIDAM) = 0x1fffffff;
    /* TODO: DFP reset conflicts with TSS2 field positions; probe +0x16c. */
    MCAN_REG(s, MCAN_TSU_TSS2) = s->sam_tss2_reset;

    s->rxf0_get = s->rxf0_put = s->rxf0_fill = 0;
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

    size = mcan_rxf0_size(s);
    if (size) {
        s->rxf0_get %= size;
        s->rxf0_put %= size;
        s->rxf0_fill = MIN(s->rxf0_fill, size);
    } else {
        s->rxf0_get = s->rxf0_put = s->rxf0_fill = 0;
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
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = bosch_m_can_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, BoschMCanState, BOSCH_M_CAN_REG_WORDS),
        VMSTATE_UINT8(rxf0_get, BoschMCanState),
        VMSTATE_UINT8(rxf0_put, BoschMCanState),
        VMSTATE_UINT8(rxf0_fill, BoschMCanState),
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
