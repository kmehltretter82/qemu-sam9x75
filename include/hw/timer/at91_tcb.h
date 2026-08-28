/*
 * Microchip AT91 Timer Counter Block
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_TIMER_AT91_TCB_H
#define HW_TIMER_AT91_TCB_H

#include "hw/core/clock.h"
#include "hw/core/ptimer.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_TCB "at91-tcb"
OBJECT_DECLARE_SIMPLE_TYPE(AT91TCBState, AT91_TCB)

#define AT91_TCB_NUM_CHANNELS 3

typedef struct AT91TCBChannel {
    AT91TCBState *owner;
    ptimer_state *timer;

    uint32_t cmr;
    uint32_t smmr;
    uint32_t ra;
    uint32_t rb;
    uint32_t rc;
    uint32_t status;
    uint32_t imr;
    uint32_t emr;
    uint32_t ssr;
    bool enabled;
    bool running;
    bool clock_suspended;
    /*
     * The ptimer runs one segment at a time, from segment_start up to the
     * next compare boundary, so RA and RB compares can fire inside a period.
     */
    uint64_t segment_start;
    uint64_t segment_end;
    /* UPDOWN modes reverse at the period end and again at zero. */
    bool counting_down;
    /*
     * A channel clocked from XC0-XC2 is not driven by the ptimer: it
     * advances one count per selected edge, so it keeps its own counter.
     */
    bool edge_clocked;
    uint32_t edge_counter;
    /* Levels driven on the per-channel compare request lines. */
    bool compare_request_level[3];
    /* TIOA pin state: the level this channel drives, and what it sees. */
    bool tioa_out;
    bool tiob_out;
    bool tioa_in;
    bool tiob_in;
    bool capture_request_level;
    bool etrg_request_level;
} AT91TCBChannel;

struct AT91TCBState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    /*
     * XDMAC compare request lines, three per channel in RA, RB, RC order.
     * DS60001813E Table 16.1 assigns requests 43-48 to the CPA, CPB and CPC
     * events of channel 1 of each timer block.
     */
    qemu_irq compare_request[AT91_TCB_NUM_CHANNELS * 3];
    /*
     * Per-channel TIOA output and the capture request it can drive.  The
     * matching input is a named GPIO in; the Curiosity board routes neither.
     */
    qemu_irq tioa[AT91_TCB_NUM_CHANNELS];
    qemu_irq tiob[AT91_TCB_NUM_CHANNELS];
    /* External TCLK inputs, one per channel, and the last XC levels. */
    bool tclk_in[AT91_TCB_NUM_CHANNELS];
    bool xc_level[AT91_TCB_NUM_CHANNELS];
    /* Quadrature decoder: last phase levels and the decoded direction. */
    bool qdec_pha;
    bool qdec_phb;
    bool qdec_dir;
    bool qdec_seen;
    qemu_irq capture_request[AT91_TCB_NUM_CHANNELS];
    qemu_irq etrg_request[AT91_TCB_NUM_CHANNELS];
    Clock *pclk;
    Clock *gclk;
    Clock *slck;
    AT91TCBChannel channel[AT91_TCB_NUM_CHANNELS];

    uint32_t bmr;
    uint32_t qimr;
    uint32_t qisr;
    uint32_t wpmr;
};

#endif /* HW_TIMER_AT91_TCB_H */
