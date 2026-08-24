/*
 * Microchip SAM9X7 bus matrix
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_MATRIX_H
#define HW_MISC_AT91_MATRIX_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_MATRIX "at91-matrix"
OBJECT_DECLARE_SIMPLE_TYPE(AT91MatrixState, AT91_MATRIX)

#define AT91_MATRIX_NUM_HOSTS  14
#define AT91_MATRIX_NUM_CLIENTS 12

struct AT91MatrixState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    qemu_irq cpu_remap;

    uint32_t mcfg[AT91_MATRIX_NUM_HOSTS];
    uint32_t scfg[AT91_MATRIX_NUM_CLIENTS];
    uint32_t pras[AT91_MATRIX_NUM_CLIENTS];
    uint32_t prbs[AT91_MATRIX_NUM_CLIENTS];
    uint32_t mrcr;
    uint32_t meimr;
    uint32_t mesr;
    uint32_t mear[AT91_MATRIX_NUM_HOSTS];
    uint32_t wpmr;
    uint32_t wpsr;
};

#endif /* HW_MISC_AT91_MATRIX_H */
