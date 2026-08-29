/*
 * DMA coherency checker — see include/hw/misc/dma-coherency.h
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/error-report.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "qemu/units.h"
#include "qemu/notify.h"
#include "hw/misc/dma-coherency.h"

/*
 * Model: guest RAM is tracked per cache line.  For each line we keep the
 * CPU's view as of its last cache-maintenance op:
 *
 *   state    meaning after the op
 *   CLEAN    cache == RAM (after a clean, or after a DMA read completed)
 *   INVALID  CPU holds no copy (after an invalidate)
 *   UNKNOWN  never maintained: default at boot
 *
 * plus a content hash of the line taken at that moment.  A DMA then checks:
 *
 *   DMA READ of a line whose current contents differ from the hash taken at
 *   its last CLEAN -> the CPU wrote it since, without a clean: the device
 *   reads stale RAM.  (Missing dma_sync_single_for_device(TO_DEVICE).)
 *
 *   DMA WRITE to a line that is not INVALID -> the CPU may hold a stale copy
 *   it will read back.  (Missing dma_sync_single_for_device(FROM_DEVICE).)
 *
 * The content hash stands in for a CPU-store hook: we cannot cheaply see
 * every guest store, but we can see whether the bytes changed.
 */

#define DMACC_LINE_SHIFT 5           /* ARM926: 32-byte D-cache lines */
#define DMACC_LINE_SIZE  (1u << DMACC_LINE_SHIFT)

enum { L_UNKNOWN = 0, L_CLEAN = 1, L_INVALID = 2, L_DEVWRITTEN = 3 };

typedef struct {
    uint8_t  state;
    uint8_t  enrolled;   /* CPU issued a cache op on this line at least once */
    uint32_t hash;
} DmaccLine;

static struct {
    hwaddr ram_base;
    hwaddr ram_size;
    DmaccLine *lines;
    uint64_t n_reports;
    uint64_t n_dma;
    uint64_t n_ops;
    /* ends of recent device writes, for burst detection */
    hwaddr   last_write_end[4];
    unsigned last_write_idx;
} dmacc;

static const char *dmacc_requester;

void dmacc_set_requester(const char *who)
{
    dmacc_requester = who;
}

bool dmacc_enabled(void)
{
    return qemu_loglevel_mask(LOG_DMA_COHERENCY);
}

static uint32_t line_hash(hwaddr paddr)
{
    uint8_t buf[DMACC_LINE_SIZE];
    uint32_t h = 2166136261u;
    int i;

    if (address_space_read(&address_space_memory, paddr,
                           MEMTXATTRS_UNSPECIFIED, buf, sizeof(buf))
        != MEMTX_OK) {
        return 0;
    }
    for (i = 0; i < DMACC_LINE_SIZE; i++) {
        h = (h ^ buf[i]) * 16777619u;
    }
    return h;
}

static bool in_ram(hwaddr paddr)
{
    return dmacc.lines && paddr >= dmacc.ram_base &&
           paddr < dmacc.ram_base + dmacc.ram_size;
}

static DmaccLine *line_for(hwaddr paddr)
{
    return &dmacc.lines[(paddr - dmacc.ram_base) >> DMACC_LINE_SHIFT];
}

static void dmacc_exit_notify(Notifier *n, void *data)
{
    if (!dmacc.lines) {
        return;
    }
    qemu_log_mask(LOG_DMA_COHERENCY,
                  "dma-coherency: summary: %" PRIu64 " cache ops observed, %"
                  PRIu64 " DMA accesses checked, %" PRIu64 " violations\n",
                  dmacc.n_ops, dmacc.n_dma, dmacc.n_reports);
}

static Notifier dmacc_exit_notifier = { .notify = dmacc_exit_notify };

/*
 * Guest RAM is often nested inside SoC container regions, so walking the
 * top-level subregions finds the wrong thing.  Instead resolve the region
 * that actually backs the address the guest DMAs into.  We probe upward
 * from the lowest RAM address the flat view exposes.
 */
static void lazy_init(void)
{
    MemoryRegion *sysmem = get_system_memory();
    MemoryRegionSection sec;
    hwaddr probe, best_size = 0, best_base = 0;

    if (dmacc.lines) {
        return;
    }
    /* Probe candidate bases: every 16 MiB across the 32-bit space. */
    for (probe = 0; probe < 0xffffffffULL; probe += 16 * MiB) {
        sec = memory_region_find(sysmem, probe, 1);
        if (sec.mr) {
            if (memory_region_is_ram(sec.mr) && !memory_region_is_rom(sec.mr)
                && !memory_region_is_romd(sec.mr)) {
                hwaddr base = probe - sec.offset_within_region;
                hwaddr size = memory_region_size(sec.mr);
                if (size > best_size) {
                    best_size = size;
                    best_base = base;
                }
            }
            memory_region_unref(sec.mr);
        }
    }
    if (!best_size) {
        return;
    }
    dmacc.ram_base = best_base;
    dmacc.ram_size = best_size;
    dmacc.lines = g_new0(DmaccLine, best_size >> DMACC_LINE_SHIFT);
    qemu_add_exit_notifier(&dmacc_exit_notifier);
    qemu_log_mask(LOG_DMA_COHERENCY,
                  "dma-coherency: tracking RAM 0x%" HWADDR_PRIx "+0x%"
                  HWADDR_PRIx " in %u-byte lines\n",
                  best_base, best_size, DMACC_LINE_SIZE);
}

void dmacc_cache_op(DmaccCacheOp op, hwaddr paddr, bool whole_cache)
{
    hwaddr a, end;

    if (!dmacc_enabled()) {
        return;
    }
    lazy_init();
    dmacc.n_ops++;

    if (whole_cache) {
        a = dmacc.ram_base;
        end = dmacc.ram_base + dmacc.ram_size;
    } else {
        if (!in_ram(paddr)) {
            return;
        }
        a = paddr & ~(hwaddr)(DMACC_LINE_SIZE - 1);
        end = a + DMACC_LINE_SIZE;
    }

    for (; a < end; a += DMACC_LINE_SIZE) {
        DmaccLine *l = line_for(a);
        if (!whole_cache) {
            l->enrolled = 1;
        }
        switch (op) {
        case DMACC_OP_CLEAN:
            l->state = L_CLEAN;
            l->hash = whole_cache ? 0 : line_hash(a);
            break;
        case DMACC_OP_INVALIDATE:
        case DMACC_OP_FLUSH:
            l->state = L_INVALID;
            l->hash = 0;
            break;
        }
    }
}

void dmacc_dma_access(hwaddr paddr, hwaddr len, bool is_write,
                      const char *who)
{
    hwaddr a, end;

    if (!dmacc_enabled()) {
        return;
    }
    if (dmacc_requester) {
        who = dmacc_requester;
    }
    lazy_init();
    if (!in_ram(paddr)) {
        return;
    }
    dmacc.n_dma++;

    a = paddr & ~(hwaddr)(DMACC_LINE_SIZE - 1);
    end = paddr + len;

    /*
     * Devices such as the XDMAC issue one dma_memory_write() per bus beat,
     * so a single transfer arrives as many small sequential writes.  Only
     * the first beat of a transfer is a coherency decision point; a beat
     * that continues exactly where the previous one ended is the same
     * transfer and must not be re-judged.
     */
    if (is_write) {
        bool continues = false;
        unsigned i;

        for (i = 0; i < ARRAY_SIZE(dmacc.last_write_end); i++) {
            if (paddr == dmacc.last_write_end[i]) {
                continues = true;
                dmacc.last_write_end[i] = end;
                break;
            }
        }
        if (!continues) {
            dmacc.last_write_end[dmacc.last_write_idx++ %
                                 ARRAY_SIZE(dmacc.last_write_end)] = end;
        }
        if (continues) {
            /* still mark the newly-touched lines as device-written */
            for (; a < end; a += DMACC_LINE_SIZE) {
                DmaccLine *l = line_for(a);
                if (l->enrolled && l->state == L_INVALID) {
                    l->state = L_DEVWRITTEN;
                }
            }
            return;
        }
    }


    for (; a < end; a += DMACC_LINE_SIZE) {
        DmaccLine *l = line_for(a);

        /*
         * Lines the CPU has never maintained are presumed to be coherent
         * (uncacheable) DMA memory: descriptor rings, dma_alloc_coherent()
         * pools.  Those need no sync and must not be checked.
         */
        if (!l->enrolled) {
            continue;
        }

        if (is_write) {
            /*
             * Device writes RAM.  The CPU must have invalidated its copy
             * first, or it will later read stale cache.
             */
            if (l->state != L_INVALID) {
                dmacc.n_reports++;
                qemu_log_mask(LOG_DMA_COHERENCY,
                    "dma-coherency: %s DMA WRITE to 0x%" HWADDR_PRIx
                    " (line 0x%" HWADDR_PRIx ") but CPU cache not invalidated"
                    " (state=%s): missing dma_sync_single_for_device"
                    "(DMA_FROM_DEVICE)\n",
                    who, paddr, a,
                    l->state == L_CLEAN ? "clean" :
                    l->state == L_DEVWRITTEN ? "device-written, then reused"
                                             : "unknown");
            }
            /*
             * The device wrote RAM.  The CPU will now read it (and may cache
             * it).  Any further device write to this line needs a new
             * invalidate first: that is the per-transfer rule of the DMA API.
             */
            l->state = L_DEVWRITTEN;
        } else {
            /*
             * Device reads RAM.  If the CPU wrote this line since its last
             * clean, the device sees stale data.
             */
            if (l->state == L_CLEAN && l->hash && l->hash != line_hash(a)) {
                dmacc.n_reports++;
                qemu_log_mask(LOG_DMA_COHERENCY,
                    "dma-coherency: %s DMA READ from 0x%" HWADDR_PRIx
                    " (line 0x%" HWADDR_PRIx ") but CPU modified it after the"
                    " last clean: missing dma_sync_single_for_device"
                    "(DMA_TO_DEVICE)\n",
                    who, paddr, a);
            } else if (l->state == L_UNKNOWN) {
                dmacc.n_reports++;
                qemu_log_mask(LOG_DMA_COHERENCY,
                    "dma-coherency: %s DMA READ from 0x%" HWADDR_PRIx
                    " (line 0x%" HWADDR_PRIx ") that was never cleaned"
                    " (state=unknown)\n",
                    who, paddr, a);
            }
        }
    }
}
