/*
 * Microchip SAM9X7 SoC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_SAM9X7_H
#define HW_ARM_SAM9X7_H

#include "hw/block/at91_nand.h"
#include "hw/char/at91_dbgu.h"
#include "hw/core/clock.h"
#include "hw/core/or-irq.h"
#include "hw/core/sysbus.h"
#include "hw/dma/at91_xdmac.h"
#include "hw/gpio/at91_pio.h"
#include "hw/i2c/at91_twi.h"
#include "hw/intc/at91_aic.h"
#include "hw/misc/at91_flexcom.h"
#include "hw/misc/at91_pmc.h"
#include "hw/misc/at91_mpddrc.h"
#include "hw/misc/at91_pmecc.h"
#include "hw/misc/at91_rstc.h"
#include "hw/misc/at91_sckc.h"
#include "hw/misc/at91_sfr.h"
#include "hw/misc/at91_smc.h"
#include "hw/misc/at91_sysc.h"
#include "hw/net/cadence_gem.h"
#include "hw/sd/at91_sdhci.h"
#include "hw/ssi/at91_ospi.h"
#include "hw/timer/at91_pit64b.h"
#include "hw/timer/at91_pit.h"
#include "hw/timer/at91_rtt.h"
#include "hw/timer/at91_tcb.h"
#include "hw/watchdog/at91_wdt.h"
#include "qom/object.h"
#include "system/memory.h"
#include "target/arm/cpu.h"

#define TYPE_SAM9X7 "sam9x7"
OBJECT_DECLARE_SIMPLE_TYPE(SAM9X7State, SAM9X7)

#define SAM9X7_NUM_PIO              4
#define SAM9X7_NUM_SDMMC            2
#define SAM9X7_NUM_FLEXCOM          13
#define SAM9X7_NUM_GMAC_QUEUES       6

/* Internal memories and external-memory windows. */
#define SAM9X7_BOOT_BASE             0x00000000
#define SAM9X7_ROM_BASE              0x00100000
#define SAM9X7_ROM_SIZE              0x0002c000
#define SAM9X7_SRAM0_BASE            0x00300000
#define SAM9X7_SRAM0_SIZE            0x00010000
#define SAM9X7_SRAM1_BASE            0x00400000
#define SAM9X7_SRAM1_SIZE            0x00001000
#define SAM9X7_DDR_BASE              0x20000000
#define SAM9X7_DDR_SIZE              0x10000000
#define SAM9X7_NAND_BASE             0x30000000
#define SAM9X7_NAND_SIZE             0x00800000
#define SAM9X7_QSPI_MEM_BASE         0x60000000
#define SAM9X7_QSPI_MEM_SIZE         0x20000000
#define SAM9X7_SDMMC0_BASE           0x80000000
#define SAM9X7_SDMMC1_BASE           0x90000000

/* User peripherals. */
#define SAM9X7_XDMAC_BASE            0xf0008000
#define SAM9X7_QSPI_BASE             0xf0014000
#define SAM9X7_PIT64B0_BASE          0xf0028000
#define SAM9X7_PIT64B1_BASE          0xf0040000
#define SAM9X7_TCB_BASE              0xf8008000
#define SAM9X7_SFR_BASE              0xf8050000
#define SAM9X7_GMAC_BASE             0xf802c000

/* System peripherals. */
#define SAM9X7_MATRIX_BASE           0xffffde00
#define SAM9X7_PMECC_BASE            0xffffe000
#define SAM9X7_PMERRLOC_BASE         0xffffe600
#define SAM9X7_MPDDRC_BASE           0xffffe800
#define SAM9X7_SMC_BASE              0xffffea00
#define SAM9X7_AIC_BASE              0xfffff100
#define SAM9X7_DBGU_BASE             0xfffff200
#define SAM9X7_PIOA_BASE             0xfffff400
#define SAM9X7_PIOB_BASE             0xfffff600
#define SAM9X7_PIOC_BASE             0xfffff800
#define SAM9X7_PIOD_BASE             0xfffffa00
#define SAM9X7_PMC_BASE              0xfffffc00
#define SAM9X7_RSTC_BASE             0xfffffe00
#define SAM9X7_SHDWC_BASE            0xfffffe10
#define SAM9X7_RTT_BASE              0xfffffe20
#define SAM9X7_PIT_BASE              0xfffffe40
#define SAM9X7_SCKC_BASE             0xfffffe50
#define SAM9X7_GPBR_BASE             0xfffffe60
#define SAM9X7_RTC_BASE              0xfffffea8
#define SAM9X7_SYSCWP_BASE           0xfffffedc
#define SAM9X7_WDT_BASE              0xffffff80

/* SAM9X75D2G identification. Version bits select silicon revision A1. */
#define SAM9X75_A1_CIDR              0x89750031
#define SAM9X75_D2G_EXID             0x00000020

struct SAM9X7State {
    SysBusDevice parent_obj;

    ARMCPU cpu;
    AT91AIC5State aic;
    AT91DBGUState dbgu;
    AT91PMCState pmc;
    AT91PMECCState pmecc;
    AT91MPDDRCState mpddrc;
    AT91SYSCWPState sysc;
    AT91RSTCState rstc;
    AT91SCKCState sckc;
    AT91SFRState sfr;
    AT91SMCState smc;
    AT91NANDState nand;
    AT91OSPIState qspi;
    AT91XDMACState xdmac;
    CadenceGEMState gmac;
    AT91FlexcomState flexcom[SAM9X7_NUM_FLEXCOM];
    AT91TWIState twi[SAM9X7_NUM_FLEXCOM];
    AT91SDHCIState sdmmc[SAM9X7_NUM_SDMMC];
    AT91PIOState pio[SAM9X7_NUM_PIO];
    AT91PIT64BState pit64b[2];
    AT91PITState pit;
    AT91RTTState rtt;
    AT91TCBState tcb;
    AT91WDTState wdt;
    OrIRQState sys_irq;

    Clock *main_xtal;
    Clock *slow_rc;
    Clock *slow_xtal;

    MemoryRegion rom;
    MemoryRegion boot_alias;
    MemoryRegion sram0;
    MemoryRegion sram1;
    MemoryRegion *memory;
};

#endif /* HW_ARM_SAM9X7_H */
