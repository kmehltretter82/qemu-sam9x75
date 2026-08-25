/*
 * Microchip AT91 programmable multibit ECC controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_PMECC_H
#define HW_MISC_AT91_PMECC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_PMECC "at91-pmecc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PMECCState, AT91_PMECC)

#define AT91_PMECC_MAX_SECTORS       8
#define AT91_PMECC_MAX_STRENGTH      24
#define AT91_PMECC_ECC_BYTES         44
#define AT91_PMECC_REM_REGS          12
#define AT91_PMECC_PARITY_WORDS      6

struct AT91PMECCState {
    SysBusDevice parent_obj;

    MemoryRegion pmecc_mmio;
    MemoryRegion errloc_mmio;
    qemu_irq irq;
    Clock *pclk;

    uint32_t cfg;
    uint32_t sarea;
    uint32_t saddr;
    uint32_t eaddr;
    uint32_t imr;
    uint32_t isr;
    bool enabled;

    uint32_t errloc_cfg;
    uint32_t errloc_prim;
    uint32_t errloc_len;
    uint32_t errloc_imr;
    uint32_t errloc_isr;
    uint32_t sigma[25];
    uint32_t errloc[AT91_PMECC_MAX_STRENGTH];

    uint8_t ecc[AT91_PMECC_MAX_SECTORS][AT91_PMECC_ECC_BYTES];
    uint32_t rem[AT91_PMECC_MAX_SECTORS][AT91_PMECC_REM_REGS];

    bool busy;
    bool stream_active;
    bool stream_write;
    uint8_t stream_m;
    uint8_t stream_strength;
    uint8_t stream_nsectors;
    uint16_t stream_sector_size;
    uint16_t stream_ecc_bits;
    uint16_t stream_ecc_bytes;
    uint32_t stream_page_size;
    uint32_t stream_next_column;
    uint64_t encode_rem[AT91_PMECC_MAX_SECTORS]
                       [AT91_PMECC_PARITY_WORDS];
    uint16_t decode_rem[AT91_PMECC_MAX_SECTORS]
                        [AT91_PMECC_MAX_STRENGTH];

    /* Rebuilt from the latched profile; not migrated. */
    uint64_t generator[AT91_PMECC_PARITY_WORDS];
    uint16_t minpoly[AT91_PMECC_MAX_STRENGTH];
    uint16_t *alpha_to;
    int16_t *index_of;
    uint8_t gf_m;
};

void at91_pmecc_transfer_byte(AT91PMECCState *s, bool write, uint8_t value);

#endif /* HW_MISC_AT91_PMECC_H */
