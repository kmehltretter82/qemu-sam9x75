/*
 * Microchip AT91 Power Management Controller v2
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MISC_AT91_PMC_H
#define HW_MISC_AT91_PMC_H

#include "hw/core/clock.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_AT91_PMC "at91-pmc-v2"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PMCState, AT91_PMC)

#define AT91_PMC_NUM_PLLS 5
#define AT91_PMC_NUM_PIDS 70
#define AT91_PMC_NUM_PCKS 2

struct AT91PMCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    Clock *main_xtal;
    Clock *td_slck;
    Clock *md_slck;

    Clock *mainck;
    Clock *cpu;
    Clock *mck;
    Clock *pck[AT91_PMC_NUM_PCKS];
    Clock *pclk[AT91_PMC_NUM_PIDS];
    Clock *gclk[AT91_PMC_NUM_PIDS];

    uint32_t pll_ctrl0[AT91_PMC_NUM_PLLS];
    uint32_t pll_ctrl1[AT91_PMC_NUM_PLLS];
    uint32_t pll_ssr[AT91_PMC_NUM_PLLS];
    uint32_t pll_acr[AT91_PMC_NUM_PLLS];
    uint32_t active_pll_ctrl0[AT91_PMC_NUM_PLLS];
    uint32_t active_pll_ctrl1[AT91_PMC_NUM_PLLS];
    uint32_t active_pll_ssr[AT91_PMC_NUM_PLLS];
    uint32_t active_pll_acr[AT91_PMC_NUM_PLLS];

    uint32_t scsr;
    uint32_t pll_updt;
    uint32_t mor;
    uint32_t mcfr;
    uint32_t mckr;
    uint32_t usb;
    uint32_t pck_reg[AT91_PMC_NUM_PCKS];
    uint32_t imr;
    uint32_t fsmr;
    uint32_t wcr;
    uint32_t wpmr;
    uint32_t wpsr;
    uint32_t mcklim;
    uint32_t pcr[AT91_PMC_NUM_PIDS];
    uint32_t pll_imr;
    uint32_t pll_isr0;
    uint32_t pll_isr1;
    uint8_t selected_pid;
};

#endif /* HW_MISC_AT91_PMC_H */
