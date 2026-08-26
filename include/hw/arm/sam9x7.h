/*
 * Microchip SAM9X7 SoC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_ARM_SAM9X7_H
#define HW_ARM_SAM9X7_H

#include "hw/audio/at91_classd.h"
#include "hw/audio/at91_i2smcc.h"
#include "hw/block/at91_nand.h"
#include "hw/char/at91_dbgu.h"
#include "hw/char/at91_usart.h"
#include "hw/core/clock.h"
#include "hw/core/or-irq.h"
#include "hw/core/sysbus.h"
#include "hw/dma/at91_xdmac.h"
#include "hw/gpio/at91_pio.h"
#include "hw/i2c/at91_twi.h"
#include "hw/intc/at91_aic.h"
#include "hw/misc/at91_aes.h"
#include "hw/misc/at91_bsc.h"
#include "hw/misc/at91_sha.h"
#include "hw/misc/at91_tdes.h"
#include "hw/misc/at91_flexcom.h"
#include "hw/misc/at91_gpbr.h"
#include "hw/misc/at91_matrix.h"
#include "hw/misc/at91_pmc.h"
#include "hw/misc/at91_mpddrc.h"
#include "hw/misc/at91_pmecc.h"
#include "hw/misc/at91_rstc.h"
#include "hw/misc/at91_shdwc.h"
#include "hw/misc/at91_sckc.h"
#include "hw/misc/at91_sfr.h"
#include "hw/misc/at91_smc.h"
#include "hw/misc/at91_sysc.h"
#include "hw/misc/at91_trng.h"
#include "hw/net/cadence_gem.h"
#include "hw/net/can/bosch_m_can.h"
#include "hw/nvram/at91_otpc.h"
#include "hw/rtc/at91_rtc.h"
#include "hw/sd/at91_sdhci.h"
#include "hw/ssi/at91_ospi.h"
#include "hw/ssi/at91_spi.h"
#include "hw/timer/at91_pit64b.h"
#include "hw/timer/at91_pit.h"
#include "hw/timer/at91_rtt.h"
#include "hw/timer/at91_tcb.h"
#include "hw/usb/at91-udphs.h"
#include "hw/usb/hcd-ehci.h"
#include "hw/usb/hcd-ohci.h"
#include "hw/watchdog/at91_wdt.h"
#include "qom/object.h"
#include "system/memory.h"
#include "target/arm/cpu.h"

#define TYPE_SAM9X7 "sam9x7"
OBJECT_DECLARE_SIMPLE_TYPE(SAM9X7State, SAM9X7)

#define SAM9X7_NUM_PIO              4
#define SAM9X7_NUM_SDMMC            2
#define SAM9X7_NUM_FLEXCOM          13
#define SAM9X7_NUM_FLEXCOM_SPI       6
#define SAM9X7_NUM_GMAC_QUEUES       6
#define SAM9X7_NUM_MCAN              2

#define SAM9X7_GPIO_RESET            "reset"
#define SAM9X7_RESET_POWER           0
#define SAM9X7_RESET_REQUEST         1

/* Internal memories and external-memory windows. */
#define SAM9X7_BOOT_BASE             0x00000000
#define SAM9X7_BOOT_ROM_SIZE         0x00014000
#define SAM9X7_ECC_ROM_BASE          0x00100000
#define SAM9X7_ECC_ROM_SIZE          0x00018000
#define SAM9X7_SRAM0_BASE            0x00300000
#define SAM9X7_SRAM0_SIZE            0x00010000
#define SAM9X7_SRAM1_BASE            0x00400000
#define SAM9X7_SRAM1_SIZE            0x00001000
#define SAM9X7_UDPHS_FIFO_BASE       0x00500000
#define SAM9X7_UDPHS_FIFO_SIZE       0x00100000
#define SAM9X7_UHPHS_OHCI_BASE       0x00600000
#define SAM9X7_UHPHS_EHCI_BASE       0x00700000
#define SAM9X7_UHPHS_WINDOW_SIZE     0x00100000
#define SAM9X7_DDR_BASE              0x20000000
#define SAM9X7_DDR_SIZE              0x10000000
#define SAM9X7_NAND_BASE             0x30000000
#define SAM9X7_NAND_SIZE             0x10000000
#define SAM9X7_QSPI_MEM_BASE         0x60000000
#define SAM9X7_QSPI_MEM_SIZE         0x20000000
#define SAM9X7_SDMMC0_BASE           0x80000000
#define SAM9X7_SDMMC1_BASE           0x90000000

/* User peripherals. */
#define SAM9X7_OTPC_BASE             0xeff00000
#define SAM9X7_XDMAC_BASE            0xf0008000
#define SAM9X7_QSPI_BASE             0xf0014000
#define SAM9X7_I2SMCC_BASE           0xf001c000
#define SAM9X7_PIT64B0_BASE          0xf0028000
#define SAM9X7_SHA_BASE              0xf002c000
#define SAM9X7_TRNG_BASE             0xf0030000
#define SAM9X7_AES_BASE              0xf0034000
#define SAM9X7_TDES_BASE             0xf0038000
#define SAM9X7_CLASSD_BASE           0xf003c000
#define SAM9X7_PIT64B1_BASE          0xf0040000
#define SAM9X7_MCAN0_BASE            0xf8000000
#define SAM9X7_MCAN1_BASE            0xf8004000
#define SAM9X7_TCB_BASE              0xf8008000
#define SAM9X7_TCB1_BASE             0xf800c000
#define SAM9X7_SFR_BASE              0xf8050000
#define SAM9X7_GMAC_BASE             0xf802c000
#define SAM9X7_UDPHS_BASE            0xf803c000
#define SAM9X7_UDPHS_SIZE            0x00000400

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
#define SAM9X7_BSC_BASE              0xfffffe54
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
    AT91AESState aes;
    AT91SHAState sha;
    AT91TDESState tdes;
    AT91TRNGState trng;
    AT91CLASSDState classd;
    AT91I2SMCCState i2smcc;
    AT91DBGUState dbgu;
    AT91BSCState bsc;
    AT91OTPCState otpc;
    AT91MatrixState matrix;
    AT91PMCState pmc;
    AT91PMECCState pmecc;
    AT91MPDDRCState mpddrc;
    AT91GPBRState gpbr;
    AT91SYSCWPState sysc;
    AT91RSTCState rstc;
    AT91SHDWCState shdwc;
    AT91SCKCState sckc;
    AT91SFRState sfr;
    AT91SMCState smc;
    AT91NANDState nand;
    AT91OSPIState qspi;
    AT91XDMACState xdmac;
    AT91UDPHSState udphs;
    AT91UHPHSEHCIState uhphs_ehci;
    OHCISysBusState uhphs_ohci;
    CadenceGEMState gmac;
    BoschMCanState mcan[SAM9X7_NUM_MCAN];
    AT91FlexcomState flexcom[SAM9X7_NUM_FLEXCOM];
    AT91USARTState usart[SAM9X7_NUM_FLEXCOM];
    AT91SPIState spi[SAM9X7_NUM_FLEXCOM_SPI];
    AT91TWIState twi[SAM9X7_NUM_FLEXCOM];
    AT91SDHCIState sdmmc[SAM9X7_NUM_SDMMC];
    AT91PIOState pio[SAM9X7_NUM_PIO];
    AT91PIT64BState pit64b[2];
    AT91PITState pit;
    AT91RTTState rtt;
    AT91RTCState rtc;
    AT91TCBState tcb;
    AT91TCBState tcb1;
    AT91WDTState wdt;
    OrIRQState sys_irq;
    OrIRQState ebi_irq;
    OrIRQState uhphs_irq;

    Clock *main_xtal;
    Clock *slow_rc;
    Clock *slow_xtal;

    bool core_reset_requested;
    bool power_reset_requested;
    bool cpu_reset_hold_active;
    /* Derived from SFR outputs and reconstructed after migration. */
    bool nand_cs2_assigned;
    bool nand_d16_assigned;

    MemoryRegion rom;
    MemoryRegion boot_alias;
    MemoryRegion ecc_alias;
    MemoryRegion boot_sram_alias;
    MemoryRegion sram0;
    MemoryRegion sram1;
    MemoryRegion uhphs_ohci_window;
    MemoryRegion uhphs_ehci_window;
    MemoryRegion ddr_window;
    MemoryRegion nand_window;
    MemoryRegion *memory;
    MemoryRegion *ddr_memory;
    CanBusState *canbus[SAM9X7_NUM_MCAN];
};

#endif /* HW_ARM_SAM9X7_H */
