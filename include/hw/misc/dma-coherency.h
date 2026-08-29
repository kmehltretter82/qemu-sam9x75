/*
 * DMA coherency checker
 *
 * Enforces the Linux DMA ownership protocol from the emulator side.  QEMU has
 * no cache model, so a driver that forgets a dma_sync_*() cannot fail here the
 * way it fails on non-coherent silicon.  This checker records the guest's
 * cache-maintenance operations and the contents of DMA target regions, and
 * reports a DMA that touches memory the CPU wrote without a subsequent clean
 * (device would read stale RAM) or that the CPU still holds without a
 * preceding invalidate (CPU would later read stale cache).
 *
 * It is deliberately stricter than real hardware: it flags the protocol
 * violation, not the eventual data corruption.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HW_MISC_DMA_COHERENCY_H
#define HW_MISC_DMA_COHERENCY_H

#include "exec/hwaddr.h"

typedef enum {
    DMACC_OP_CLEAN,      /* CPU pushed dirty lines to RAM   (c7,c10,x) */
    DMACC_OP_INVALIDATE, /* CPU discarded its lines         (c7,c6,x)  */
    DMACC_OP_FLUSH,      /* clean + invalidate              (c7,c14,x) */
} DmaccCacheOp;

bool dmacc_enabled(void);

/* Called from the CPU when it executes a cache-maintenance operation. */
void dmacc_cache_op(DmaccCacheOp op, hwaddr paddr, bool whole_cache);

/* Called from the DMA layer before a device reads/writes guest memory. */
void dmacc_dma_access(hwaddr paddr, hwaddr len, bool is_write,
                      const char *who);

/*
 * A device that knows more than "device" (e.g. a DMA controller channel)
 * can set a label that the generic hook will use until cleared.
 */
void dmacc_set_requester(const char *who);

#endif
