/*
 * Bosch M_CAN controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NET_CAN_BOSCH_M_CAN_H
#define HW_NET_CAN_BOSCH_M_CAN_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "net/can_emu.h"
#include "qom/object.h"
#include "system/memory.h"

#define TYPE_BOSCH_M_CAN "bosch-m-can"
OBJECT_DECLARE_SIMPLE_TYPE(BoschMCanState, BOSCH_M_CAN)

/* Standard M_CAN registers plus the SAM9X7 timestamping extension. */
#define BOSCH_M_CAN_MMIO_SIZE 0x200
#define BOSCH_M_CAN_REG_WORDS (BOSCH_M_CAN_MMIO_SIZE / sizeof(uint32_t))

struct BoschMCanState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    MemoryRegion *message_ram;
    MemoryRegion message_ram_root;
    MemoryRegion message_ram_alias;
    AddressSpace *message_ram_as;

    qemu_irq irq[2];
    Clock *hclk;
    Clock *cclk;

    CanBusClientState bus_client;
    CanBusState *canbus;

    uint32_t regs[BOSCH_M_CAN_REG_WORDS];

    /* Read-only synthesis values supplied by the integration. */
    uint32_t crel;
    uint32_t sam_tss2_reset;

    /* Integration-specific register behavior. */
    uint32_t dbtp_mask;
    bool tsu_destructive_read;
    bool rwd_unprotected;

    /* Canonical FIFO state; status registers are derived from these. */
    uint8_t rxf0_get;
    uint8_t rxf0_put;
    uint8_t rxf0_fill;
    uint8_t rxf1_get;
    uint8_t rxf1_put;
    uint8_t rxf1_fill;
    uint8_t tx_fifo_get;
    uint8_t tx_fifo_put;
    uint8_t txe_get;
    uint8_t txe_put;
    uint8_t txe_fill;
    uint16_t timestamp_counter;

    /*
     * ISO 11898-1 error counters.  TEC reaches 256 in bus-off; REC never
     * exceeds 127 because the bus API has no receive-error source.
     */
    uint16_t tec;
    uint8_t rec;
    /*
     * Set when a transmit error would carry TEC past 255.  Silicon keeps the
     * last in-range value (248 was measured after one unacknowledged frame)
     * and reports bus-off separately, so TEC itself never overflows.
     */
    bool tx_error_overflow;

    bool resources_ready;
};

#endif /* HW_NET_CAN_BOSCH_M_CAN_H */
