/*
 * QTest tests for the Microchip SAM9X75 Curiosity machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "elf.h"
#include "libqtest.h"
#include "migration/migration-qmp.h"
#include "qemu/bswap.h"
#include "qemu/bitops.h"
#include "qobject/qlist.h"

#define SAM9X75_MACHINE         "-machine sam9x75-curiosity"
#define SOC_RESET_POWER_IRQ     0

#define SAM9X7_BOOT_BASE        0x00000000
#define SAM9X7_ROM_BASE         0x00100000
#define SAM9X7_ROM_SIZE         0x0002c000
#define SAM9X7_SRAM0_BASE       0x00300000
#define SAM9X7_SRAM1_BASE       0x00400000
#define SAM9X7_UDPHS_FIFO_BASE  0x00500000
#define SAM9X7_UHPHS_OHCI_BASE  0x00600000
#define SAM9X7_UHPHS_EHCI_BASE  0x00700000
#define SAM9X7_DDR_BASE         0x20000000
#define SAM9X7_NAND_BASE        0x30000000
#define SAM9X7_NAND_SIZE        0x10000000
#define SAM9X7_NAND_SLICE_SIZE  0x00800000
#define SAM9X7_QSPI_MEM_BASE    0x60000000
#define SAM9X7_SDMMC0_BASE      0x80000000
#define SAM9X7_SDMMC1_BASE      0x90000000
#define SAM9X7_OTPC_BASE        0xeff00000
#define SAM9X7_XDMAC_BASE       0xf0008000
#define SAM9X7_QSPI_BASE        0xf0014000
#define SAM9X7_I2SMCC_BASE      0xf001c000
#define SAM9X7_SHA_BASE         0xf002c000
#define SAM9X7_CLASSD_BASE      0xf003c000
#define SAM9X7_FLEXCOM4_BASE    0xf0000000
#define SAM9X7_SPI4_BASE        (SAM9X7_FLEXCOM4_BASE + 0x400)
#define SAM9X7_FLEXCOM0_BASE    0xf801c000
#define SAM9X7_USART0_BASE      (SAM9X7_FLEXCOM0_BASE + 0x200)
#define SAM9X7_SPI0_BASE        (SAM9X7_FLEXCOM0_BASE + 0x400)
#define SAM9X7_TWI0_BASE        (SAM9X7_FLEXCOM0_BASE + 0x600)
#define SAM9X7_FLEXCOM3_BASE    0xf8028000
#define SAM9X7_USART3_BASE      (SAM9X7_FLEXCOM3_BASE + 0x200)
#define SAM9X7_SPI3_BASE        (SAM9X7_FLEXCOM3_BASE + 0x400)
#define SAM9X7_FLEXCOM6_BASE    0xf8010000
#define SAM9X7_USART6_BASE      (SAM9X7_FLEXCOM6_BASE + 0x200)
#define SAM9X7_SPI6_BASE        (SAM9X7_FLEXCOM6_BASE + 0x400)
#define SAM9X7_TWI6_BASE        (SAM9X7_FLEXCOM6_BASE + 0x600)
#define SAM9X7_FLEXCOM7_BASE    0xf8014000
#define SAM9X7_TWI7_BASE        (SAM9X7_FLEXCOM7_BASE + 0x600)
#define SAM9X7_FLEXCOM12_BASE   0xf0024000
#define SAM9X7_SPI12_BASE       (SAM9X7_FLEXCOM12_BASE + 0x400)
#define SAM9X7_PIT64B0_BASE     0xf0028000
#define SAM9X7_TRNG_BASE        0xf0030000
#define SAM9X7_AES_BASE         0xf0034000
#define SAM9X7_TDES_BASE        0xf0038000
#define SAM9X7_PIT64B1_BASE     0xf0040000
#define SAM9X7_MCAN0_BASE       0xf8000000
#define SAM9X7_MCAN1_BASE       0xf8004000
#define SAM9X7_TCB_BASE         0xf8008000
#define SAM9X7_TCB1_BASE        0xf800c000
#define SAM9X7_GMAC_BASE        0xf802c000
#define SAM9X7_UDPHS_BASE       0xf803c000
#define SAM9X7_SFR_BASE         0xf8050000
#define SAM9X7_MATRIX_BASE      0xffffde00
#define SAM9X7_PMECC_BASE       0xffffe000
#define SAM9X7_PMERRLOC_BASE    0xffffe600
#define SAM9X7_MPDDRC_BASE      0xffffe800
#define SAM9X7_SMC_BASE         0xffffea00
#define SAM9X7_RSTC_BASE        0xfffffe00
#define SAM9X7_SHDWC_BASE       0xfffffe10
#define SAM9X7_RTT_BASE         0xfffffe20
#define SAM9X7_PIT_BASE         0xfffffe40
#define SAM9X7_SCKC_BASE        0xfffffe50
#define SAM9X7_BSC_BASE         0xfffffe54
#define SAM9X7_GPBR_BASE        0xfffffe60
#define SAM9X7_RTC_BASE         0xfffffea8
#define SAM9X7_SYSCWP_BASE      0xfffffedc
#define SAM9X7_AIC_BASE         0xfffff100
#define SAM9X7_DBGU_BASE        0xfffff200
#define SAM9X7_PIOA_BASE        0xfffff400
#define SAM9X7_PIOB_BASE        0xfffff600
#define SAM9X7_PIOC_BASE        0xfffff800
#define SAM9X7_PIOD_BASE        0xfffffa00
#define SAM9X7_PMC_BASE         0xfffffc00
#define SAM9X7_WDT_BASE         0xffffff80

#define SDHCI_SYSAD             0x00
#define SDHCI_BLKSIZE           0x04
#define SDHCI_BLKCNT            0x06
#define SDHCI_ARGUMENT          0x08
#define SDHCI_TRNMOD            0x0c
#define SDHCI_CMDREG            0x0e
#define SDHCI_RSPREG0           0x10
#define SDHCI_PRNSTS            0x24
#define SDHCI_HOSTCTL           0x28
#define SDHCI_CLKCON            0x2c
#define SDHCI_SWRST             0x2f
#define SDHCI_NORINTSTS         0x30
#define SDHCI_ERRINTSTS         0x32
#define SDHCI_NORINTSTSEN       0x34
#define SDHCI_ERRINTSTSEN       0x36
#define SDHCI_NORINTSIGEN       0x38
#define SDHCI_HOSTCTL2          0x3e
#define SDHCI_ADMAERR           0x54
#define SDHCI_ADMASYSADDR       0x58
#define SDHCI_PRESET_INIT       0x60
#define SDHCI_PRESET_DEFAULT    0x62
#define SDHCI_PRESET_HIGH_SPEED 0x64
#define SDHCI_PRESET_SDR12      0x66
#define SDHCI_PRESET_DDR50      0x6e
#define SDMMC_MC1R              0x204
#define SDMMC_ACR               0x208
#define SDMMC_CC2R              0x20c
#define SDMMC_CACR              0x230
#define SDMMC_DBGR              0x234

#define SDHCI_TRNS_DMA          BIT(0)
#define SDHCI_TRNS_BLK_CNT_EN   BIT(1)
#define SDHCI_TRNS_READ         BIT(4)
#define SDHCI_TRNS_MULTI        BIT(5)
#define SDHCI_CMD_RESPONSE      2
#define SDHCI_CMD_RESPONSE_BUSY 3
#define SDHCI_CMD_DATA_PRESENT  BIT(5)
#define SDHCI_CTRL_ADMA2_32     BIT(4)
#define SDHCI_CLOCK_ENABLE      (BIT(2) | BIT(1) | BIT(0))
#define SDHCI_RESET_ALL         BIT(0)
#define SDHCI_RESET_CMD         BIT(1)
#define SDHCI_RESET_DATA        BIT(2)
#define SDHCI_HOSTCTL2_PRESET   BIT(15)
#define SDHCI_HOSTCTL2_ASYNC    BIT(14)
#define SDHCI_CARD_PRESENT      BIT(16)
#define SDMMC_CACR_KEY          (0x46U << 8)
#define SDMMC_CACR_CAPWREN      BIT(0)
#define SDHCI_INT_CMD_COMPLETE  BIT(0)
#define SDHCI_INT_XFER_COMPLETE BIT(1)
#define SDHCI_INT_DMA           BIT(3)
#define SDHCI_INT_ERROR         BIT(15)
#define SDHCI_ERR_ADMA          BIT(9)

#define SDHCI_CMD_INDEX(index)  ((index) << 8)
#define SDHCI_ADMA2_VALID       BIT(0)
#define SDHCI_ADMA2_END         BIT(1)
#define SDHCI_ADMA2_INT         BIT(2)
#define SDHCI_ADMA2_TRAN        BIT(5)
#define SDHCI_ADMA_STATE_STOP   0
#define SDHCI_ADMA_STATE_FDS    1
#define SDHCI_ADMA_STATE_TFR    3
#define SDHCI_ADMA_LENGTH_MISMATCH BIT(2)

#define CLOCK_PERIOD_1SEC       (1000000000ULL << 32)
#define CLOCK_PERIOD_FROM_HZ(hz) \
    ((hz) ? CLOCK_PERIOD_1SEC / (hz) : 0)

#define AIC_SSR                 0x00
#define AIC_SMR                 0x04
#define AIC_SVR                 0x08
#define AIC_IVR                 0x10
#define AIC_FVR                 0x14
#define AIC_ISR                 0x18
#define AIC_IPR0                0x20
#define AIC_IPR1                0x24
#define AIC_IPR2                0x28
#define AIC_IMR                 0x30
#define AIC_CISR                0x34
#define AIC_EOICR               0x38
#define AIC_SPU                 0x3c
#define AIC_IECR                0x40
#define AIC_IDCR                0x44
#define AIC_ICCR                0x48
#define AIC_ISCR                0x4c
#define AIC_FFER                0x50
#define AIC_DCR                 0x6c
#define AIC_WPMR                0xe4
#define AIC_WPSR                0xe8

#define UHPFS_HC_CONTROL        0x04
#define UHPFS_HC_COMMAND_STATUS 0x08
#define UHPFS_HC_INT_STATUS     0x0c
#define UHPFS_HC_INT_ENABLE     0x10
#define UHPFS_HC_HCCA           0x18
#define UHPFS_HC_CONTROL_HEAD   0x20
#define UHPFS_HC_BULK_HEAD      0x28
#define UHPFS_HC_FM_INTERVAL    0x34
#define UHPFS_HC_FM_REMAINING   0x38
#define UHPFS_HC_FM_NUMBER      0x3c
#define UHPFS_HC_LS_THRESHOLD   0x44
#define UHPFS_HC_RH_DESC_A      0x48
#define UHPFS_HC_RH_DESC_B      0x4c
#define UHPFS_HC_RH_STATUS      0x50
#define UHPFS_HC_RH_PORT(n)     (0x54 + (n) * 4)

#define UHPFS_COMMAND_HCR       BIT(0)
#define UHPFS_COMMAND_CLF       BIT(1)
#define UHPFS_COMMAND_BLF       BIT(2)
#define UHPFS_CONTROL_OPERATIONAL (2U << 6)
#define UHPFS_CONTROL_CLE       BIT(4)
#define UHPFS_CONTROL_BLE       BIT(5)
#define UHPFS_INT_SF            BIT(2)
#define UHPFS_INT_UE            BIT(4)
#define UHPFS_INT_RHSC          BIT(6)
#define UHPFS_INT_MIE           BIT(31)
#define UHPFS_RHA_PSM           BIT(8)
#define UHPFS_RHA_NPS           BIT(9)
#define UHPFS_RHB_PPCM(n)       BIT(17 + (n))
#define UHPFS_RHS_LPS           BIT(0)
#define UHPFS_RHS_LPSC          BIT(16)
#define UHPFS_PORT_CCS          BIT(0)
#define UHPFS_PORT_PES          BIT(1)
#define UHPFS_PORT_PRS          BIT(4)
#define UHPFS_PORT_PPS          BIT(8)
#define UHPFS_PORT_LSDA         BIT(9)
#define UHPFS_PORT_CSC          BIT(16)

#define UHPHS_USBCMD            0x10
#define UHPHS_USBSTS            0x14
#define UHPHS_USBINTR           0x18
#define UHPHS_PERIODICLISTBASE  0x24
#define UHPHS_ASYNCLISTADDR     0x28
#define UHPHS_CONFIGFLAG        0x50
#define UHPHS_PORTSC(n)         (0x54 + (n) * 4)
#define UHPHS_INSNREG06         0xa8
#define UHPHS_INSNREG07         0xac

#define UHPHS_USBCMD_RUN        BIT(0)
#define UHPHS_USBCMD_HCRESET    BIT(1)
#define UHPHS_USBCMD_FLS_1      BIT(2)
#define UHPHS_USBCMD_FLS_2      BIT(3)
#define UHPHS_USBCMD_FLS_MASK   (BIT(2) | BIT(3))
#define UHPHS_USBCMD_PSE        BIT(4)
#define UHPHS_USBCMD_ASE        BIT(5)
#define UHPHS_USBSTS_PCD        BIT(2)
#define UHPHS_USBSTS_FLR        BIT(3)
#define UHPHS_USBSTS_HSE        BIT(4)
#define UHPHS_USBSTS_HCHLT      BIT(12)
#define UHPHS_PORTSC_CONNECT    BIT(0)
#define UHPHS_PORTSC_PED        BIT(2)
#define UHPHS_PORTSC_PRESET     BIT(8)
#define UHPHS_PORTSC_PPOWER     BIT(12)
#define UHPHS_PORTSC_POWNER     BIT(13)
#define UHPHS_INSNREG06_AHB_ERR BIT(31)

#define UDPHS_CTRL              0x000
#define UDPHS_FNUM              0x004
#define UDPHS_IEN               0x010
#define UDPHS_INTSTA            0x014
#define UDPHS_CLRINT            0x018
#define UDPHS_EPTRST            0x01c
#define UDPHS_TST               0x0e0
#define UDPHS_EPTCFG(n)         (0x100 + (n) * 0x20)
#define UDPHS_EPTCTLENB(n)      (0x104 + (n) * 0x20)
#define UDPHS_EPTCTLDIS(n)      (0x108 + (n) * 0x20)
#define UDPHS_EPTCTL(n)         (0x10c + (n) * 0x20)
#define UDPHS_EPTSETSTA(n)      (0x114 + (n) * 0x20)
#define UDPHS_EPTCLRSTA(n)      (0x118 + (n) * 0x20)
#define UDPHS_EPTSTA(n)         (0x11c + (n) * 0x20)
#define UDPHS_DMANXTDSC(n)      (0x300 + (n) * 0x10)
#define UDPHS_DMAADDRESS(n)     (0x304 + (n) * 0x10)
#define UDPHS_DMACONTROL(n)     (0x308 + (n) * 0x10)
#define UDPHS_DMASTATUS(n)      (0x30c + (n) * 0x10)

#define UDPHS_CTRL_DEV_ADDR(value) ((value) & 0x7f)
#define UDPHS_CTRL_FADDR_EN     BIT(7)
#define UDPHS_CTRL_EN_UDPHS     BIT(8)
#define UDPHS_CTRL_DETACH       BIT(9)
#define UDPHS_CTRL_REWAKEUP     BIT(10)
#define UDPHS_CTRL_PULLD_DIS    BIT(11)
#define UDPHS_INT_SPEED         BIT(0)
#define UDPHS_INT_ENDRESET      BIT(4)
#define UDPHS_INT_EPT(n)        BIT(8 + (n))
#define UDPHS_INT_DMA(n)        BIT(24 + (n))
#define UDPHS_EPTCFG_SIZE(value) ((value) & 7)
#define UDPHS_EPTCFG_DIR_IN     BIT(3)
#define UDPHS_EPTCFG_TYPE(value) ((value) << 4)
#define UDPHS_EPTCFG_BANKS(value) ((value) << 6)
#define UDPHS_EPTCFG_NB_TRANS(value) ((value) << 8)
#define UDPHS_EPTCFG_MAPPED     BIT(31)
#define UDPHS_EPTCTL_ENABLE     BIT(0)
#define UDPHS_EPTCTL_AUTO_VALID BIT(1)
#define UDPHS_EPTCTL_INTDIS_DMA BIT(3)
#define UDPHS_EPTCTL_DATAX_RX   BIT(6)
#define UDPHS_EPTCTL_MDATA_RX   BIT(7)
#define UDPHS_EPTCTL_RXRDY_IE   BIT(9)
#define UDPHS_EPTCTL_TXCOMPLT_IE BIT(10)
#define UDPHS_EPTCTL_TXRDY_IE   BIT(11)
#define UDPHS_EPTCTL_RX_SETUP_IE BIT(12)
#define UDPHS_EPTCTL_NAK_IN_IE  BIT(14)
#define UDPHS_EPTCTL_BUSY_IE    BIT(18)
#define UDPHS_EPTCTL_SHORT_IE   BIT(31)
#define UDPHS_EPTSTA_FORCESTALL BIT(5)
#define UDPHS_EPTSTA_RXRDY      BIT(9)
#define UDPHS_EPTSTA_TXCOMPLT   BIT(10)
#define UDPHS_EPTSTA_TXRDY      BIT(11)
#define UDPHS_EPTSTA_RX_SETUP   BIT(12)
#define UDPHS_EPTSTA_ERR_FL_ISO BIT(12)
#define UDPHS_EPTSTA_NAK_IN     BIT(14)
#define UDPHS_EPTSTA_NAK_OUT    BIT(15)
#define UDPHS_EPTSTA_TOGGLE_MASK (3U << 6)
#define UDPHS_EPTSTA_BUSY(value) ((value) << 18)
#define UDPHS_EPTSTA_BYTE_COUNT(value) ((value) << 20)
#define UDPHS_EPTSTA_SHORT      BIT(31)
#define UDPHS_EPTSTA_RESET      BIT(6)
#define UDPHS_DMA_CHANN_ENB     BIT(0)
#define UDPHS_DMA_LDNXT_DSC     BIT(1)
#define UDPHS_DMA_END_TR_EN     BIT(2)
#define UDPHS_DMA_END_BF_EN     BIT(3)
#define UDPHS_DMA_END_TR_IE     BIT(4)
#define UDPHS_DMA_END_BF_IE     BIT(5)
#define UDPHS_DMA_DESC_LD_IE    BIT(6)
#define UDPHS_DMA_BUFF_LENGTH(value) ((value) << 16)
#define UDPHS_DMA_CHANN_ACT     BIT(1)
#define UDPHS_DMA_END_TR_ST     BIT(4)
#define UDPHS_DMA_END_BF_ST     BIT(5)
#define UDPHS_DMA_DESC_LD_ST    BIT(6)
#define UDPHS_DMA_BUFF_COUNT(value) ((value) << 16)

#define MCAN_CREL               0x000
#define MCAN_ENDN               0x004
#define MCAN_DBTP               0x00c
#define MCAN_TEST               0x010
#define MCAN_RWD                0x014
#define MCAN_CCCR               0x018
#define MCAN_NBTP               0x01c
#define MCAN_TSCC               0x020
#define MCAN_TSCV               0x024
#define MCAN_TOCC               0x028
#define MCAN_TOCV               0x02c
#define MCAN_PSR                0x044
#define MCAN_IR                 0x050
#define MCAN_IE                 0x054
#define MCAN_ILS                0x058
#define MCAN_ILE                0x05c
#define MCAN_GFC                0x080
#define MCAN_SIDFC              0x084
#define MCAN_XIDFC              0x088
#define MCAN_XIDAM              0x090
#define MCAN_HPMS               0x094
#define MCAN_NDAT1              0x098
#define MCAN_NDAT2              0x09c
#define MCAN_RXF0C              0x0a0
#define MCAN_RXF0S              0x0a4
#define MCAN_RXF0A              0x0a8
#define MCAN_RXBC               0x0ac
#define MCAN_RXF1C              0x0b0
#define MCAN_RXF1S              0x0b4
#define MCAN_RXF1A              0x0b8
#define MCAN_RXESC              0x0bc
#define MCAN_TXBC               0x0c0
#define MCAN_TXFQS              0x0c4
#define MCAN_TXESC              0x0c8
#define MCAN_TXBRP              0x0cc
#define MCAN_TXBAR              0x0d0
#define MCAN_TXBTO              0x0d8
#define MCAN_TXBCF              0x0dc
#define MCAN_TXBTIE             0x0e0
#define MCAN_TXEFC              0x0f0
#define MCAN_TXEFS              0x0f4
#define MCAN_TXEFA              0x0f8
#define MCAN_TSU_TSCFG          0x164
#define MCAN_TSU_TSS2           0x16c
#define MCAN_TSU_ATB            0x1b0

#define MCAN_CCCR_INIT          BIT(0)
#define MCAN_CCCR_CCE           BIT(1)
#define MCAN_CCCR_MON           BIT(5)
#define MCAN_CCCR_TEST          BIT(7)
#define MCAN_CCCR_FDOE          BIT(8)
#define MCAN_CCCR_BRSE          BIT(9)
#define MCAN_TEST_LBCK          BIT(4)
#define MCAN_IR_RF0N            BIT(0)
#define MCAN_IR_RF0W            BIT(1)
#define MCAN_IR_RF0F            BIT(2)
#define MCAN_IR_RF0L            BIT(3)
#define MCAN_IR_RF1N            BIT(4)
#define MCAN_IR_RF1W            BIT(5)
#define MCAN_IR_RF1F            BIT(6)
#define MCAN_IR_RF1L            BIT(7)
#define MCAN_IR_HPM             BIT(8)
#define MCAN_IR_TC              BIT(9)
#define MCAN_IR_TEFN            BIT(12)
#define MCAN_IR_MRAF            BIT(17)
#define MCAN_IR_DRX             BIT(19)
#define MCAN_ILE_EINT0          BIT(0)
#define MCAN_ILE_EINT1          BIT(1)
#define MCAN_ELEMENT_EFC        BIT(23)
#define MCAN_ELEMENT_ESI        BIT(31)
#define MCAN_ELEMENT_RTR        BIT(29)
#define MCAN_ELEMENT_XTD        BIT(30)
#define MCAN_ELEMENT_FDF        BIT(21)
#define MCAN_ELEMENT_BRS        BIT(20)
#define MCAN_ELEMENT_MM(marker) ((uint32_t)(marker) << 24)
#define MCAN_ELEMENT_DLC(dlc)   ((uint32_t)(dlc) << 16)
#define MCAN_TX_EVENT_ET_TX     BIT(22)
#define MCAN_RXF0C_SIZE(size)   ((uint32_t)(size) << 16)
#define MCAN_RXF1C_SIZE(size)   ((uint32_t)(size) << 16)
#define MCAN_RXFC_WATERMARK(wm) ((uint32_t)(wm) << 24)
#define MCAN_RXFC_OVERWRITE     BIT(31)
#define MCAN_SIDFC_SIZE(size)   ((uint32_t)(size) << 16)
#define MCAN_XIDFC_SIZE(size)   ((uint32_t)(size) << 16)
#define MCAN_STD_FILTER(type, action, id1, id2) \
    (((uint32_t)(type) << 30) | ((uint32_t)(action) << 27) | \
     ((uint32_t)(id1) << 16) | (id2))
#define MCAN_EXT_FILTER0(action, id1) \
    (((uint32_t)(action) << 29) | (id1))
#define MCAN_EXT_FILTER1(type, id2) \
    (((uint32_t)(type) << 30) | (id2))
#define MCAN_FILTER_FIFO0       1
#define MCAN_FILTER_FIFO1       2
#define MCAN_FILTER_REJECT      3
#define MCAN_FILTER_PRIORITY    4
#define MCAN_FILTER_PRIO_FIFO0  5
#define MCAN_FILTER_PRIO_FIFO1  6
#define MCAN_FILTER_RX_BUFFER   7
#define MCAN_RXFS_STATUS(fill, get, put) \
    ((uint32_t)(fill) | ((uint32_t)(get) << 8) | \
     ((uint32_t)(put) << 16))
#define MCAN_TXBC_FIFO_SIZE(size) ((uint32_t)(size) << 24)
#define MCAN_TXEFC_SIZE(size)   ((uint32_t)(size) << 16)
#define MCAN_TXEFS_STATUS(fill, get, put) \
    ((uint32_t)(fill) | ((uint32_t)(get) << 8) | \
     ((uint32_t)(put) << 16))
#define MCAN_TOCC_TOS(tos)      ((uint32_t)(tos) << 1)
#define MCAN_TOCC_TOP(top)      ((uint32_t)(top) << 16)

#define MATRIX_MCFG(n)          ((n) * 4)
#define MATRIX_SCFG(n)          (0x40 + (n) * 4)
#define MATRIX_PRAS(n)          (0x80 + (n) * 8)
#define MATRIX_PRBS(n)          (0x84 + (n) * 8)
#define MATRIX_MRCR             0x100
#define MATRIX_MEIER            0x150
#define MATRIX_MEIDR            0x154
#define MATRIX_MEIMR            0x158
#define MATRIX_MESR             0x15c
#define MATRIX_MEAR(n)          (0x160 + (n) * 4)
#define MATRIX_WPMR             0x1e4
#define MATRIX_WPSR             0x1e8

#define MATRIX_WPMR_KEY         0x4d415400
#define MATRIX_WPMR_WPEN        BIT(0)
#define MATRIX_WPMR_CFGFRZ      BIT(7)

#define AIC_SMR_LEVEL_HIGH      (2U << 5)
#define AIC_SMR_EDGE_RISING     (3U << 5)
#define AIC_CISR_FIQ            BIT(0)
#define AIC_CISR_IRQ            BIT(1)
#define AIC_DCR_GMSK            BIT(1)
#define AIC_WPMR_KEY            0x41494300

#define FLEX_MR                 0x00
#define FLEX_RHR                0x10
#define FLEX_THR                0x20
#define FLEX_MODE_NONE          0
#define FLEX_MODE_USART         1
#define FLEX_MODE_SPI           2
#define FLEX_MODE_TWI           3

#define SPI_CR                  0x00
#define SPI_MR                  0x04
#define SPI_RDR                 0x08
#define SPI_TDR                 0x0c
#define SPI_SR                  0x10
#define SPI_IER                 0x14
#define SPI_IDR                 0x18
#define SPI_IMR                 0x1c
#define SPI_CSR(n)              (0x30 + (n) * 4)
#define SPI_FMR                 0x40
#define SPI_FLR                 0x44
#define SPI_CMPR                0x48
#define SPI_CRCR                0x4c
#define SPI_TPMR                0x50
#define SPI_TPHR                0x54
#define SPI_WPMR                0xe4
#define SPI_WPSR                0xe8
#define SPI_NUM_CS              4

#define SPI_CR_SPIEN            BIT(0)
#define SPI_CR_SPIDIS           BIT(1)
#define SPI_CR_SWRST            BIT(7)
#define SPI_CR_REQCLR           BIT(12)
#define SPI_CR_TXFCLR           BIT(16)
#define SPI_CR_RXFCLR           BIT(17)
#define SPI_CR_LASTXFER         BIT(24)
#define SPI_CR_FIFOEN           BIT(30)
#define SPI_CR_FIFODIS          BIT(31)

#define SPI_MR_MSTR             BIT(0)
#define SPI_MR_PS               BIT(1)
#define SPI_MR_WDRBT            BIT(5)
#define SPI_MR_LLB              BIT(7)
#define SPI_MR_PCS(pcs)         ((uint32_t)(pcs) << 16)

#define SPI_INT_RDRF            BIT(0)
#define SPI_INT_TDRE            BIT(1)
#define SPI_INT_OVRES           BIT(3)
#define SPI_INT_TXEMPTY         BIT(9)
#define SPI_INT_CMP             BIT(11)
#define SPI_STATUS_SPIENS       BIT(16)
#define SPI_INT_TXFEF           BIT(24)
#define SPI_INT_TXFFF           BIT(25)
#define SPI_INT_TXFTHF          BIT(26)
#define SPI_INT_RXFEF           BIT(27)
#define SPI_INT_RXFFF           BIT(28)
#define SPI_INT_RXFTHF          BIT(29)
#define SPI_INT_TXFPTEF         BIT(30)
#define SPI_INT_RXFPTEF         BIT(31)

#define SPI_CSR_CSAAT           BIT(3)
#define SPI_CSR_BITS(bits)      (((uint32_t)(bits) - 8) << 4)
#define SPI_CSR_SCBR(divisor)   ((uint32_t)(divisor) << 8)
#define SPI_CSR_DLYBS(cycles)   ((uint32_t)(cycles) << 16)
#define SPI_CSR_DLYBCT(value)   ((uint32_t)(value) << 24)

#define SPI_FMR_TXRDYM_TWO      1
#define SPI_FMR_RXRDYM_TWO      BIT(4)
#define SPI_FMR_TXFTHRES(value) ((uint32_t)(value) << 16)
#define SPI_FMR_RXFTHRES(value) ((uint32_t)(value) << 24)
#define SPI_WPMR_WPEN           BIT(0)
#define SPI_WPMR_WPITEN         BIT(1)
#define SPI_WPMR_WPCREN         BIT(2)
#define SPI_WPMR_KEY            0x53504900

#define US_CR                   0x00
#define US_MR                   0x04
#define US_IER                  0x08
#define US_IDR                  0x0c
#define US_IMR                  0x10
#define US_CSR                  0x14
#define US_RHR                  0x18
#define US_THR                  0x1c
#define US_BRGR                 0x20
#define US_RTOR                 0x24
#define US_TTGR                 0x28
#define US_FIDI                 0x40
#define US_IF                   0x4c
#define US_MAN                  0x50
#define US_LINMR                0x54
#define US_LINIR                0x58
#define US_LINBRR               0x5c
#define US_LONMR                0x60
#define US_LONPR                0x64
#define US_LONDL                0x68
#define US_LONL2HDR             0x6c
#define US_LONBL                0x70
#define US_LONB1TX              0x74
#define US_LONB1RX              0x78
#define US_LONPRIO              0x7c
#define US_IDTTX                0x80
#define US_IDTRX                0x84
#define US_ICDIFF               0x88
#define US_CMPR                 0x90
#define US_FMR                  0xa0
#define US_FLR                  0xa4
#define US_FIER                 0xa8
#define US_FIDR                 0xac
#define US_FIMR                 0xb0
#define US_FESR                 0xb4
#define US_WPMR                 0xe4
#define US_WPSR                 0xe8
#define US_NAME                 0xf0
#define US_VERSION              0xfc

#define US_CR_RXEN              BIT(4)
#define US_CR_TXEN              BIT(6)
#define US_CR_RSTSTA            BIT(8)
#define US_CR_STTTO             BIT(11)
#define US_CR_RETTO             BIT(15)
#define US_CR_TXFCLR            BIT(24)
#define US_CR_RXFCLR            BIT(25)
#define US_CR_TXFLCLR           BIT(26)
#define US_CR_FIFOEN            BIT(30)
#define US_CR_FIFODIS           BIT(31)
#define US_MR_CHRL_8            (3U << 6)
#define US_MR_PAR_NONE          (4U << 9)
#define US_MR_LOCAL_LOOPBACK    (2U << 14)
#define US_MR_NORMAL_LOCAL      (US_MR_CHRL_8 | US_MR_PAR_NONE | \
                                 US_MR_LOCAL_LOOPBACK)
#define US_INT_RXRDY            BIT(0)
#define US_INT_TXRDY            BIT(1)
#define US_INT_TIMEOUT          BIT(8)
#define US_INT_TXEMPTY          BIT(9)
#define US_FMR_TXRDYM_FOUR      2
#define US_FMR_RXRDYM_FOUR      (2U << 4)
#define US_FMR_TXFTHRES(value)  ((value) << 8)
#define US_FMR_RXFTHRES(value)  ((value) << 16)
#define US_FMR_RXFTHRES2(value) ((value) << 24)
#define US_FIFO_INT_TXFEF       BIT(0)
#define US_FIFO_INT_TXFFF       BIT(1)
#define US_FIFO_INT_TXFTHF      BIT(2)
#define US_FIFO_INT_RXFEF       BIT(3)
#define US_FIFO_INT_RXFTHF      BIT(5)
#define US_FIFO_INT_TXFPTEF     BIT(6)
#define US_FIFO_INT_RXFPTEF     BIT(7)
#define US_FIFO_STATUS_TXFLOCK  BIT(8)
#define US_FIFO_INT_RXFTHF2     BIT(9)
#define US_WPMR_WPEN           BIT(0)
#define US_WPMR_WPITEN         BIT(1)
#define US_WPMR_WPCREN         BIT(2)
#define US_WPMR_KEY            0x55534100

#define GEM_NWCTRL              0x000
#define GEM_NWCFG               0x004
#define GEM_RXQBASE             0x018
#define GEM_TXQBASE             0x01c
#define GEM_ISR                 0x024
#define GEM_IER                 0x028
#define GEM_PHYMNTNC            0x034
#define GEM_SPADDR1LO           0x088
#define GEM_SPADDR1HI           0x08c
#define GEM_MODID               0x0fc
#define GEM_DESCONF6            0x294
#define GEM_INT_Q1_STATUS       0x400
#define GEM_TRANSMIT_Q1_PTR     0x440
#define GEM_INT_Q1_ENABLE       0x600

#define GEM_OCTTXLO             0x100
#define GEM_OCTTXHI             0x104
#define GEM_TXCNT               0x108
#define GEM_TXBCNT              0x10c
#define GEM_TXMCNT              0x110
#define GEM_TX64CNT             0x118
#define GEM_TXURUNCNT           0x134
#define GEM_SINGLECOLLCNT       0x138
#define GEM_OCTRXLO             0x150
#define GEM_OCTRXHI             0x154
#define GEM_RXCNT               0x158
#define GEM_RXBROADCNT          0x15c
#define GEM_RXMULTICNT          0x160
#define GEM_RX64CNT             0x168
#define GEM_RXUDPCCNT           0x1b0
#define GEM_STAT_FIRST          GEM_OCTTXLO
#define GEM_STAT_LAST           GEM_RXUDPCCNT

#define GEM_NWCTRL_LBL          BIT(1)
#define GEM_NWCTRL_RXEN         BIT(2)
#define GEM_NWCTRL_TXEN         BIT(3)
#define GEM_NWCTRL_MPE          BIT(4)
#define GEM_NWCTRL_CLRSTAT      BIT(5)
#define GEM_NWCTRL_INCSTAT      BIT(6)
#define GEM_NWCTRL_WESTAT       BIT(7)
#define GEM_NWCTRL_TSTART       BIT(9)
#define GEM_NWCFG_PROMISC       BIT(4)
#define GEM_NWCFG_FCS_REMOVE    BIT(17)
#define GEM_INT_XMIT_COMPLETE   BIT(7)
#define GEM_RX_DESC_OWNERSHIP   BIT(0)
#define GEM_RX_DESC_WRAP        BIT(1)
#define GEM_TX_DESC_LAST        BIT(15)
#define GEM_TX_DESC_WRAP        BIT(30)
#define GEM_TX_DESC_USED        BIT(31)
#define GEM_NUM_QUEUES          6

#define GEM_MDIO_READ           (BIT(30) | (2U << 28) | (2U << 16))
#define GEM_MDIO_PHY(addr)      ((addr) << 23)
#define GEM_MDIO_REG(reg)       ((reg) << 18)

#define TWI_CR                  0x00
#define TWI_MMR                 0x04
#define TWI_SMR                 0x08
#define TWI_IADR                0x0c
#define TWI_CWGR                0x10
#define TWI_SR                  0x20
#define TWI_IER                 0x24
#define TWI_IDR                 0x28
#define TWI_IMR                 0x2c
#define TWI_RHR                 0x30
#define TWI_THR                 0x34
#define TWI_SMBTR               0x38
#define TWI_HSR                 0x3c
#define TWI_ACR                 0x40
#define TWI_FILTR               0x44
#define TWI_HSCWGR              0x48
#define TWI_FMR                 0x50
#define TWI_FLR                 0x54
#define TWI_FSR                 0x60
#define TWI_FIER                0x64
#define TWI_FIDR                0x68
#define TWI_FIMR                0x6c
#define TWI_WPMR                0xe4
#define TWI_WPSR                0xe8

#define TWI_TEST_LATENCY_NS     10000

#define TWI_CR_START            BIT(0)
#define TWI_CR_STOP             BIT(1)
#define TWI_CR_MSEN             BIT(2)
#define TWI_CR_MSDIS            BIT(3)
#define TWI_CR_SVDIS            BIT(5)
#define TWI_CR_SWRST            BIT(7)
#define TWI_CR_ACMEN            BIT(16)
#define TWI_CR_THRCLR           BIT(24)
#define TWI_CR_RXFCLR           BIT(25)
#define TWI_CR_LOCKCLR          BIT(26)
#define TWI_CR_FIFOEN           BIT(28)
#define TWI_CR_FIFODIS          BIT(29)

#define TWI_MMR_IADRSZ_1        BIT(8)
#define TWI_MMR_MREAD           BIT(12)
#define TWI_MMR_DADR(addr)      ((addr) << 16)

#define TWI_CWGR_BRSRCCLK       BIT(20)

#define TWI_SR_TXCOMP           BIT(0)
#define TWI_SR_RXRDY            BIT(1)
#define TWI_SR_TXRDY            BIT(2)
#define TWI_SR_SVREAD           BIT(3)
#define TWI_SR_NACK             BIT(8)
#define TWI_SR_SCL              BIT(24)
#define TWI_SR_SDA              BIT(25)

#define TWI_ACR_DATAL(len)      (len)
#define TWI_ACR_DIR             BIT(8)

#define TWI_FMR_TXRDYM_TWO      1
#define TWI_FMR_TXRDYM_FOUR     2
#define TWI_FMR_RXRDYM_TWO      (1U << 4)
#define TWI_FMR_RXRDYM_FOUR     (2U << 4)
#define TWI_FMR_TXFTHRES(n)     ((n) << 16)
#define TWI_FMR_RXFTHRES(n)     ((n) << 24)

#define TWI_FSR_TXFEF           BIT(0)
#define TWI_FSR_TXFFF           BIT(1)
#define TWI_FSR_TXFTHF          BIT(2)
#define TWI_FSR_RXFEF           BIT(3)
#define TWI_FSR_RXFFF           BIT(4)
#define TWI_FSR_RXFTHF          BIT(5)
#define TWI_FSR_TXFPTEF         BIT(6)
#define TWI_FSR_RXFPTEF         BIT(7)

#define TWI_WPMR_WPEN           BIT(0)
#define TWI_WPMR_WPITEN         BIT(1)
#define TWI_WPMR_WPCREN         BIT(2)
#define TWI_WPMR_KEY            0x54574900

#define DBGU_CR                 0x00
#define DBGU_MR                 0x04
#define DBGU_IER                0x08
#define DBGU_IDR                0x0c
#define DBGU_IMR                0x10
#define DBGU_CSR                0x14
#define DBGU_RHR                0x18
#define DBGU_THR                0x1c
#define DBGU_BRGR               0x20
#define DBGU_RTOR               0x28
#define DBGU_CIDR               0x40
#define DBGU_EXID               0x44
#define DBGU_FNTR               0x48
#define DBGU_WPMR               0xe4
#define DBGU_PDC_PTCR           0x120
#define DBGU_PDC_PTSR           0x124

#define DBGU_CR_RXEN            BIT(4)
#define DBGU_CR_TXEN            BIT(6)
#define DBGU_CR_RETTO           BIT(10)
#define DBGU_CR_STTTO           BIT(11)
#define DBGU_MR_MASK            (BIT(4) | (7U << 9) | BIT(12) | \
                                 (3U << 14))
#define DBGU_MR_LOCAL_LOOPBACK  (2U << 14)
#define DBGU_INT_RXRDY          BIT(0)
#define DBGU_INT_TXRDY          BIT(1)
#define DBGU_INT_TIMEOUT        BIT(8)
#define DBGU_INT_TXEMPTY        BIT(9)
#define DBGU_WPMR_WPEN          BIT(0)
#define DBGU_WPMR_KEY           0x55415200

#define I2SMCC_CR               0x00
#define I2SMCC_MRA              0x04
#define I2SMCC_MRB              0x08
#define I2SMCC_SR               0x0c
#define I2SMCC_IERA             0x10
#define I2SMCC_IDRA             0x14
#define I2SMCC_IMRA             0x18
#define I2SMCC_ISRA             0x1c
#define I2SMCC_IERB             0x20
#define I2SMCC_IDRB             0x24
#define I2SMCC_IMRB             0x28
#define I2SMCC_ISRB             0x2c
#define I2SMCC_RHR              0x30
#define I2SMCC_THR              0x34
#define I2SMCC_WPMR             0xe4
#define I2SMCC_WPSR             0xe8
#define I2SMCC_VERSION          0xfc

#define I2SMCC_CR_RXEN          BIT(0)
#define I2SMCC_CR_RXDIS         BIT(1)
#define I2SMCC_CR_CKEN          BIT(2)
#define I2SMCC_CR_CKDIS         BIT(3)
#define I2SMCC_CR_TXEN          BIT(4)
#define I2SMCC_CR_TXDIS         BIT(5)
#define I2SMCC_CR_SWRST         BIT(7)
#define I2SMCC_MRA_MODE_MASTER  BIT(0)
#define I2SMCC_MRA_DATA_16      (4U << 1)
#define I2SMCC_MRA_DATA_16C     (5U << 1)
#define I2SMCC_MRA_FORMAT_TDM   (2U << 6)
#define I2SMCC_MRA_RXLOOP       BIT(9)
#define I2SMCC_MRA_TXMONO       BIT(10)
#define I2SMCC_MRA_NBCHAN(n)    (((n) - 1) << 13)
#define I2SMCC_MRA_ISCKDIV(n)   ((n) << 24)
#define I2SMCC_SR_RXEN          BIT(0)
#define I2SMCC_SR_TXEN          BIT(4)
#define I2SMCC_INT_TXLRDY       BIT(0)
#define I2SMCC_INT_TXRRDY       BIT(1)
#define I2SMCC_INT_TXLUNF       BIT(8)
#define I2SMCC_INT_TXRUNF       BIT(9)
#define I2SMCC_INT_RXLRDY       BIT(16)
#define I2SMCC_INT_RXRRDY       BIT(17)
#define I2SMCC_INT_RXLOVF       BIT(24)
#define I2SMCC_INT_RXROVF       BIT(25)
#define I2SMCC_INT_A_MASK       0x03030303
#define I2SMCC_INT_WERR         BIT(0)
#define I2SMCC_WPMR_WPCFEN      BIT(0)
#define I2SMCC_WPMR_WPITEN      BIT(1)
#define I2SMCC_WPMR_WPCTEN      BIT(2)
#define I2SMCC_WPMR_KEY         0x49325300
#define I2SMCC_WPSR_WPVS        BIT(0)

#define CLASSD_CR                0x00
#define CLASSD_MR                0x04
#define CLASSD_INTPMR            0x08
#define CLASSD_INTSR             0x0c
#define CLASSD_THR               0x10
#define CLASSD_IER               0x14
#define CLASSD_IDR               0x18
#define CLASSD_IMR               0x1c
#define CLASSD_ISR               0x20
#define CLASSD_WPMR              0xe4

#define CLASSD_CR_SWRST          BIT(0)
#define CLASSD_MR_LEN            BIT(0)
#define CLASSD_MR_REN            BIT(4)
#define CLASSD_MR_MASK           0x00310133
#define CLASSD_MR_RESET          0x00010022
#define CLASSD_INTPMR_DSPCLKFREQ BIT(16)
#define CLASSD_INTPMR_FRAME(n)   ((n) << 20)
#define CLASSD_INTPMR_MASK       0x7f7d7f7f
#define CLASSD_INTPMR_RESET      0x00304e4e
#define CLASSD_INTSR_CFGERR      BIT(0)
#define CLASSD_INT_DATRDY        BIT(0)
#define CLASSD_WPMR_WPEN         BIT(0)
#define CLASSD_WPMR_KEY          0x434c4400

#define RTC_CR                  0x00
#define RTC_MR                  0x04
#define RTC_TIMR                0x08
#define RTC_CALR                0x0c
#define RTC_TIMALR              0x10
#define RTC_CALALR              0x14
#define RTC_SR                  0x18
#define RTC_SCCR                0x1c
#define RTC_IER                 0x20
#define RTC_IDR                 0x24
#define RTC_IMR                 0x28
#define RTC_VER                 0x2c
#define RTC_TMR                 0x58
#define RTC_TDPR                0x5c
#define RTC_TSTR0               0xb0
#define RTC_TSDR0               0xb4
#define RTC_TSSR0               0xb8
#define RTC_TSTR1               0xbc
#define RTC_TSDR1               0xc0
#define RTC_TSSR1               0xc4

#define RTC_CR_UPDTIM           BIT(0)
#define RTC_CR_UPDCAL           BIT(1)
#define RTC_MR_HRMOD            BIT(0)
#define RTC_MR_UTC              BIT(2)
#define RTC_MR_HIGHPPM          BIT(15)
#define RTC_SR_ACKUPD           BIT(0)
#define RTC_SR_ALARM            BIT(1)
#define RTC_SR_SEC              BIT(2)
#define RTC_TIMALR_SECEN        BIT(7)
#define RTC_TIMALR_MINEN        BIT(15)
#define RTC_TIMALR_HOUREN       BIT(23)
#define RTC_CALALR_UTCEN        BIT(0)
#define RTC_CALALR_MTHEN        BIT(23)
#define RTC_CALALR_DATEEN       BIT(31)
#define RTC_VER_NVTIM           BIT(0)
#define RTC_VER_NVCALALR        BIT(3)
#define RTC_TMR_LOCK            BIT(31)
#define RTC_SECOND_NS           1000000000LL

#define GPBR_MR                 0x00
#define GPBR_FCLR               0x04
#define GPBR_REG(index)         (0x08 + (index) * 4)
#define GPBR_FCLR_ENABLE        BIT(0)
#define GPBR_MR_WP(index)       BIT(index)
#define GPBR_MR_RP(index)       BIT((index) + 16)

#define BSC_CR                  0x00
#define BSC_CR_BOOT_MASK        0x00000007
#define BSC_CR_WPKEY            0x66830000

#define OTPC_CR                 0x00
#define OTPC_MR                 0x04
#define OTPC_AR                 0x08
#define OTPC_SR                 0x0c
#define OTPC_IER                0x10
#define OTPC_IDR                0x14
#define OTPC_IMR                0x18
#define OTPC_ISR                0x1c
#define OTPC_HR                 0x20
#define OTPC_DR                 0x24
#define OTPC_BAR                0x30
#define OTPC_CAR                0x34
#define OTPC_LRMR               0x40
#define OTPC_UHC0R              0x50
#define OTPC_UHC1R              0x54
#define OTPC_UID0R              0x60
#define OTPC_UID1R              0x64
#define OTPC_UID2R              0x68
#define OTPC_UID3R              0x6c
#define OTPC_WPMR               0xe4
#define OTPC_WPSR               0xe8
#define OTPC_VERSION            0xfc

#define OTPC_CR_PGM             BIT(0)
#define OTPC_CR_CKSGEN          BIT(1)
#define OTPC_CR_INVLD           BIT(2)
#define OTPC_CR_HIDE            BIT(4)
#define OTPC_CR_READ            BIT(6)
#define OTPC_CR_FLUSH           BIT(7)
#define OTPC_CR_KBSTART         BIT(8)
#define OTPC_CR_REFRESH         BIT(15)
#define OTPC_CR_KEY             0x71670000
#define OTPC_LRMR_KEY           0x73640000
#define OTPC_MR_UHCRRDIS        BIT(0)
#define OTPC_MR_NPCKT           BIT(4)
#define OTPC_MR_EMUL            BIT(7)
#define OTPC_MR_RDDIS           BIT(8)
#define OTPC_MR_WRDIS           BIT(9)
#define OTPC_MR_LOCK            BIT(15)
#define OTPC_MR_ADDR(addr)      ((uint32_t)(addr) << 16)
#define OTPC_AR_INCRT           BIT(16)
#define OTPC_SR_ONEF            BIT(9)
#define OTPC_SR_EMUL            BIT(3)
#define OTPC_INT_EOP            BIT(0)
#define OTPC_INT_EOI            BIT(2)
#define OTPC_INT_PGERR          BIT(4)
#define OTPC_INT_LKERR          BIT(5)
#define OTPC_INT_IVERR          BIT(6)
#define OTPC_INT_WERR           BIT(7)
#define OTPC_INT_EOR            BIT(8)
#define OTPC_INT_EOF            BIT(9)
#define OTPC_INT_EOH            BIT(10)
#define OTPC_INT_EORF           BIT(11)
#define OTPC_INT_CKERR          BIT(12)
#define OTPC_INT_COERR          BIT(13)
#define OTPC_INT_HDERR          BIT(14)
#define OTPC_INT_KBERR          BIT(16)
#define OTPC_INT_SECE           BIT(28)
#define OTPC_PACKET_REGULAR     1
#define OTPC_PACKET_KEY         2
#define OTPC_PACKET_BOOT        3
#define OTPC_PACKET_SECURE_BOOT 4
#define OTPC_PACKET_HARDWARE    5
#define OTPC_PACKET_CUSTOM      6
#define OTPC_HEADER(type, size) (BIT(7) | ((size) << 8) | (type))
#define OTPC_UHC1_UPGDIS        BIT(1)
#define OTPC_UHC1_UHCINVDIS     BIT(2)
#define OTPC_UHC1_UHCPGDIS      BIT(4)
#define OTPC_UHC1_BCINVDIS      BIT(5)
#define OTPC_UHC1_BCPGDIS       BIT(7)
#define OTPC_UHC1_SBCINVDIS     BIT(8)
#define OTPC_UHC1_SBCPGDIS      BIT(10)
#define OTPC_UHC1_CINVDIS       BIT(14)
#define OTPC_UHC1_CPGDIS        BIT(16)
#define OTPC_WPMR_WPCFEN        BIT(0)
#define OTPC_WPMR_WPITEN        BIT(1)
#define OTPC_WPMR_WPCTEN        BIT(2)
#define OTPC_WPMR_FIRSTE        BIT(4)
#define OTPC_WPMR_KEY           0x4f545000
#define OTPC_WPSR_WPVS          BIT(0)
#define OTPC_WPSR_SWE           BIT(3)
#define OTPC_WPSR_SWETYP(type)  ((type) << 24)
#define OTPC_WPSR_READ_WO       0
#define OTPC_WPSR_WRITE_RO      1
#define OTPC_WPSR_KEY_ERROR     3
#define OTPC_VERSION_RESET      0x00000202
#define OTPC_WPSR_ECLASS        BIT(31)

#define PMC_SCER                0x00
#define PMC_SCDR                0x04
#define PMC_SCSR                0x08
#define PMC_PLL_CTRL0           0x0c
#define PMC_PLL_CTRL1           0x10
#define PMC_PLL_ACR             0x18
#define PMC_PLL_UPDT            0x1c
#define PMC_MOR                 0x20
#define PMC_MCFR                0x24
#define PMC_MCKR                0x28
#define PMC_RESERVED_LEGACY_MCKR 0x30
#define PMC_USB                 0x38
#define PMC_IER                 0x60
#define PMC_IDR                 0x64
#define PMC_SR                  0x68
#define PMC_WPMR                0x80
#define PMC_WPSR                0x84
#define PMC_PCR                 0x88
#define PMC_CSR1                0xa4
#define PMC_GCSR1               0xc4
#define PMC_PLL_ISR0            0xec
#define PMC_RESERVED_LEGACY_PCR 0x10c

#define PMC_PLL_CTRL0_ENPLL     BIT(28)
#define PMC_PLL_CTRL0_ENPLLCK   BIT(29)
#define PMC_PLL_CTRL0_ENLOCK    BIT(31)
#define PMC_PLL_CTRL0_MASK      0xf00ff0ff
#define PMC_PLL_ACR_MASK        0x3f073fff
#define PMC_PLL_ACR_RESET       0x00020033
#define PMC_PLL_ACR_UTMIVR      BIT(12)
#define PMC_PLL_ACR_UTMIBG      BIT(13)
#define PMC_UPLL_ACR_CONFIG     0x12023010
#define PMC_PLL_UPDT_UPDATE     BIT(8)
#define PMC_PLL_UPDT_MASK       0x003f0007
#define PMC_SC_UHP              BIT(6)
#define PMC_USB_USBS_UPLL       1
#define PMC_USB_USBDIV(value)   ((value) << 8)
#define PMC_MOR_MOSCXTEN        BIT(0)
#define PMC_MOR_MOSCRCEN        BIT(3)
#define PMC_MOR_ALWAYS_ONE      BIT(5)
#define PMC_MOR_KEY             0x00370000
#define PMC_MOR_MOSCSEL         BIT(24)
#define PMC_MCFR_MAINRDY        BIT(16)
#define PMC_MCFR_RCMEAS         BIT(20)
#define PMC_MCFR_CCSS           BIT(24)
#define PMC_SR_MOSCXTS          BIT(0)
#define PMC_SR_MCKRDY           BIT(3)
#define PMC_SR_MOSCSELS         BIT(16)
#define PMC_SR_MOSCRCS          BIT(17)
#define PMC_PCR_EN              BIT(28)
#define PMC_PCR_GCKEN           BIT(29)
#define PMC_PCR_CMD             BIT(31)
#define PMC_WPMR_WPEN           BIT(0)
#define PMC_WPMR_KEY            0x504d4300

#define RSTC_CR                 0x00
#define RSTC_SR                 0x04
#define RSTC_MR                 0x08

#define RSTC_CR_PROCRST         BIT(0)
#define RSTC_CR_EXTRST          BIT(3)
#define RSTC_SR_URSTS           BIT(0)
#define RSTC_SR_RSTTYP_MASK     (7U << 8)
#define RSTC_SR_RSTTYP(value)   ((value) << 8)
#define RSTC_SR_NRSTL           BIT(16)
#define RSTC_SR_SRCMP           BIT(17)
#define RSTC_MR_URSTEN          BIT(0)
#define RSTC_MR_URSTIEN         BIT(4)
#define RSTC_MR_ENGCLR          BIT(20)
#define RSTC_KEY                0xa5000000
#define RSTC_TYPE_BACKUP        1
#define RSTC_TYPE_WATCHDOG      2
#define RSTC_TYPE_SOFTWARE      3

#define SHDWC_CR                0x00
#define SHDWC_MR                0x04
#define SHDWC_SR                0x08
#define SHDWC_WUIR              0x0c

#define SHDWC_CR_SHDW           BIT(0)
#define SHDWC_CR_KEY            0xa5000000
#define SHDWC_MR_RTTWKEN        BIT(16)
#define SHDWC_MR_RTCWKEN        BIT(17)
#define SHDWC_MR_WKUPDBC(value) ((value) << 24)
#define SHDWC_SR_WKUPS          BIT(0)
#define SHDWC_SR_RTTWK          BIT(4)
#define SHDWC_SR_RTCWK          BIT(5)
#define SHDWC_SR_WKUPIS0        BIT(16)
#define SHDWC_WUIR_WKUPEN0      BIT(0)
#define SHDWC_WUIR_WKUPT0       BIT(16)
#define SHDWC_SLCK_CYCLE_NS      31250LL

#define RTT_MR                  0x00
#define RTT_AR                  0x04
#define RTT_VR                  0x08
#define RTT_SR                  0x0c
#define RTT_MODR                0x10
#define RTT_TSR                 0x14

#define RTT_MR_ALMIEN           BIT(16)
#define RTT_MR_RTTINCIEN        BIT(17)
#define RTT_MR_RTTRST           BIT(18)
#define RTT_MR_RTTDIS           BIT(20)
#define RTT_MR_INC2AEN          BIT(21)
#define RTT_MR_RTC1HZ           BIT(24)
#define RTT_SR_ALMS             BIT(0)
#define RTT_SR_RTTINC           BIT(1)
#define RTT_SR_RTTINC2          BIT(2)
#define RTT_TSR_TS_OVF          BIT(31)

#define PIT_MR                  0x00
#define PIT_SR                  0x04
#define PIT_PIVR                0x08
#define PIT_PIIR                0x0c

#define PIT_MR_PITEN            BIT(24)
#define PIT_MR_PITIEN           BIT(25)

#define SCKC_CR                 0x00
#define SCKC_CR_OSC32EN         BIT(1)
#define SCKC_CR_TD_OSCSEL       BIT(24)

#define SYSC_WPMR               0x00
#define SYSC_WPSR               0x04
#define SYSC_WPMR_WPEN          BIT(0)
#define SYSC_WPMR_WPITEN        BIT(1)
#define SYSC_WPMR_KEY           0x53594300

#define SFR_CCFG_EBICSA         0x004
#define SFR_OHCIICR             0x010
#define SFR_OHCIISR             0x014
#define SFR_CAL1                0x0b4
#define SFR_WPMR                0x0e4
#define SFR_PUFWORUCR0          0x214
#define SFR_REMAP_MP_DDR        0x260

#define SFR_OHCIICR_ARIE        BIT(4)
#define SFR_CCFG_EBI_CS1A       BIT(1)
#define SFR_CCFG_EBI_CS2A       BIT(2)
#define SFR_CCFG_NFD0_ON_D16    BIT(24)
#define SFR_CCFG_DDR_MP_EN      BIT(25)
#define SFR_WPMR_WPEN           BIT(0)
#define SFR_WPMR_KEY            0x53465200

#define MPDDRC_RTR              0x04
#define MPDDRC_CR               0x08
#define MPDDRC_LPR              0x1c
#define MPDDRC_IO_CALIBR        0x34
#define MPDDRC_CONF_ARBITER     0x44
#define MPDDRC_IER              0xc0
#define MPDDRC_IMR              0xc8
#define MPDDRC_ISR              0xcc
#define MPDDRC_WPMR             0xe4
#define MPDDRC_WPSR             0xe8

#define MPDDRC_WPMR_WPEN        BIT(0)
#define MPDDRC_WPMR_KEY         0x44445200
#define MPDDRC_WPSR_SWE         BIT(3)
#define MPDDRC_WPSR_ECLASS      BIT(31)

#define PIT64B_CR               0x00
#define PIT64B_MR               0x04
#define PIT64B_LSB_PR           0x08
#define PIT64B_MSB_PR           0x0c
#define PIT64B_IER              0x10
#define PIT64B_IMR              0x18
#define PIT64B_ISR              0x1c
#define PIT64B_TLSBR            0x20
#define PIT64B_TMSBR            0x24
#define PIT64B_WPMR             0xe4
#define PIT64B_WPSR             0xe8

#define PIT64B_CR_START         BIT(0)
#define PIT64B_CR_SWRST         BIT(8)
#define PIT64B_MR_CONT          BIT(0)
#define PIT64B_INT_PERIOD       BIT(0)
#define PIT64B_INT_OVRE         BIT(1)
#define PIT64B_WPMR_WPEN        BIT(0)
#define PIT64B_WPMR_KEY         0x50495400

#define TCB_CCR                 0x00
#define TCB_CMR                 0x04
#define TCB_CV                  0x10
#define TCB_RC                  0x1c
#define TCB_SR                  0x20
#define TCB_IER                 0x24
#define TCB_IDR                 0x28
#define TCB_IMR                 0x2c
#define TCB_CSR                 0x34
#define TCB_SSR                 0x38
#define TCB_BCR                 0xc0
#define TCB_BMR                 0xc4
#define TCB_QIMR                0xd0
#define TCB_WPMR                0xe4
#define TCB_CHANNEL(n)          ((n) * 0x40)

#define TCB_CCR_CLKEN           BIT(0)
#define TCB_CCR_CLKDIS          BIT(1)
#define TCB_CCR_SWTRG           BIT(2)
#define TCB_CMR_CLOCK2          1
#define TCB_CMR_CPCSTOP         BIT(6)
#define TCB_CMR_WAVESEL_UP_RC   (2U << 13)
#define TCB_CMR_WAVE            BIT(15)
#define TCB_INT_CPCS            BIT(4)
#define TCB_INT_SECE            BIT(10)
#define TCB_INT_MASK            (0xffU | TCB_INT_SECE)
#define TCB_SR_CLKSTA           BIT(16)
#define TCB_BMR_MASK            0x03f3ff3f
#define TCB_WPMR_WPEN           BIT(0)
#define TCB_WPMR_WPITEN         BIT(1)
#define TCB_WPMR_WPCREN         BIT(2)
#define TCB_WPMR_FIRSTE         BIT(4)
#define TCB_WPMR_KEY            0x54494d00

#define XDMAC_GTYPE             0x00
#define XDMAC_GCFG              0x04
#define XDMAC_GWAC              0x08
#define XDMAC_GIE               0x0c
#define XDMAC_GID               0x10
#define XDMAC_GIM               0x14
#define XDMAC_GIS               0x18
#define XDMAC_GE                0x1c
#define XDMAC_GD                0x20
#define XDMAC_GS                0x24
#define XDMAC_GRS               0x28
#define XDMAC_GWS               0x2c
#define XDMAC_GRWS              0x30
#define XDMAC_GRWR              0x34
#define XDMAC_GSWR              0x38
#define XDMAC_GSWS              0x3c
#define XDMAC_GSWF              0x40
#define XDMAC_CHANNEL(n)        (0x50 + (n) * 0x40)
#define XDMAC_CIE               0x00
#define XDMAC_CID               0x04
#define XDMAC_CIM               0x08
#define XDMAC_CIS               0x0c
#define XDMAC_CSA               0x10
#define XDMAC_CDA               0x14
#define XDMAC_CNDA              0x18
#define XDMAC_CNDC              0x1c
#define XDMAC_CUBC              0x20
#define XDMAC_CBC               0x24
#define XDMAC_CC                0x28
#define XDMAC_CDS_MSP           0x2c
#define XDMAC_CSUS              0x30
#define XDMAC_CDUS              0x34

#define XDMAC_INT_BIS           BIT(0)
#define XDMAC_INT_LIS           BIT(1)
#define XDMAC_INT_DIS           BIT(2)
#define XDMAC_INT_FIS           BIT(3)
#define XDMAC_INT_RBEIS         BIT(4)
#define XDMAC_INT_WBEIS         BIT(5)
#define XDMAC_INT_ROIS          BIT(6)
#define XDMAC_CNDC_NDE          BIT(0)
#define XDMAC_CNDC_NDSUP        BIT(1)
#define XDMAC_CNDC_NDDUP        BIT(2)
#define XDMAC_CNDC_NDVIEW2      (2U << 3)
#define XDMAC_MBR_UBC_NDE       BIT(24)
#define XDMAC_MBR_UBC_NSEN      BIT(25)
#define XDMAC_MBR_UBC_NDEN      BIT(26)
#define XDMAC_MBR_UBC_NDV1      (1U << 27)
#define XDMAC_MBR_UBC_NDV2      (2U << 27)
#define XDMAC_MBR_UBC_NDV3      (3U << 27)
#define XDMAC_CC_TYPE_PER       BIT(0)
#define XDMAC_CC_DSYNC_MEM2PER  BIT(4)
#define XDMAC_CC_SWREQ          BIT(6)
#define XDMAC_CC_MEMSET         BIT(7)
#define XDMAC_CC_MBSIZE_SIXTEEN (3U << 1)
#define XDMAC_CC_DWIDTH_HALFWORD (1U << 11)
#define XDMAC_CC_DWIDTH_WORD    (2U << 11)
#define XDMAC_CC_CSIZE_4        (2U << 8)
#define XDMAC_CC_CSIZE_16       (4U << 8)
#define XDMAC_CC_SAM_INC        (1U << 16)
#define XDMAC_CC_DAM_INC        (1U << 18)
#define XDMAC_CC_DAM_UBS        (2U << 18)
#define XDMAC_CC_INITD          BIT(21)
#define XDMAC_CC_RDIP           BIT(22)
#define XDMAC_CC_WRIP           BIT(23)
#define XDMAC_CC_PERID(id)      ((id) << 24)

#define AES_CR                  0x00
#define AES_MR                  0x04
#define AES_IER                 0x10
#define AES_IDR                 0x14
#define AES_IMR                 0x18
#define AES_ISR                 0x1c
#define AES_KEYWR(index)        (0x20 + (index) * 4)
#define AES_IDATAR(index)       (0x40 + (index) * 4)
#define AES_ODATAR(index)       (0x50 + (index) * 4)
#define AES_IVR(index)          (0x60 + (index) * 4)
#define AES_AADLENR             0x70
#define AES_CLENR               0x74
#define AES_GHASHR(index)       (0x78 + (index) * 4)
#define AES_TAGR(index)         (0x88 + (index) * 4)
#define AES_CTRR                0x98
#define AES_GCMHR(index)        (0x9c + (index) * 4)
#define AES_EMR                 0xb0
#define AES_BCNT                0xb4
#define AES_TWR(index)          (0xc0 + (index) * 4)
#define AES_ALPHAR(index)       (0xd0 + (index) * 4)
#define AES_WPMR                0xe4
#define AES_WPSR                0xe8
#define AES_VERSION             0xfc

#define AES_CR_START            BIT(0)
#define AES_CR_SWRST            BIT(8)
#define AES_MR_CIPHER           BIT(0)
#define AES_MR_GTAGEN           BIT(1)
#define AES_MR_SMOD_AUTO        (1U << 8)
#define AES_MR_SMOD_DMA         (2U << 8)
#define AES_MR_KEYSIZE_192      (1U << 10)
#define AES_MR_KEYSIZE_256      (2U << 10)
#define AES_MR_OPMODE_CBC       (1U << 12)
#define AES_MR_OPMODE_OFB       (2U << 12)
#define AES_MR_OPMODE_CFB       (3U << 12)
#define AES_MR_OPMODE_CTR       (4U << 12)
#define AES_MR_OPMODE_GCM       (5U << 12)
#define AES_MR_OPMODE_XTS       (6U << 12)
#define AES_MR_CFBS_8           (4U << 16)
#define AES_MR_CKEY             (0xeU << 20)
#define AES_INT_DATRDY          BIT(0)
#define AES_INT_TAGRDY          BIT(16)
#define AES_INT_SECE            BIT(19)
#define AES_WPMR_WPEN           BIT(0)
#define AES_WPMR_WPITEN         BIT(1)
#define AES_WPMR_WPCREN         BIT(2)
#define AES_WPMR_KEY            0x41455300
#define AES_WPSR_WPVS           BIT(0)

#define TDES_CR                 0x00
#define TDES_MR                 0x04
#define TDES_IER                0x10
#define TDES_IDR                0x14
#define TDES_IMR                0x18
#define TDES_ISR                0x1c
#define TDES_KEYWR(index)       (0x20 + (index) * 4)
#define TDES_IDATAR(index)      (0x40 + (index) * 4)
#define TDES_ODATAR(index)      (0x50 + (index) * 4)
#define TDES_IVR(index)         (0x60 + (index) * 4)
#define TDES_XTEA_RNDR          0x70
#define TDES_WPMR               0xe4
#define TDES_WPSR               0xe8
#define TDES_VERSION            0xfc

#define TDES_CR_START           BIT(0)
#define TDES_CR_SWRST           BIT(8)
#define TDES_CR_UNLOCK          BIT(24)
#define TDES_MR_CIPHER          BIT(0)
#define TDES_MR_ALGO_TDES       (1U << 1)
#define TDES_MR_ALGO_XTEA       (2U << 1)
#define TDES_MR_KEYMOD_2KEY     BIT(4)
#define TDES_MR_SMOD_AUTO       (1U << 8)
#define TDES_MR_SMOD_DMA        (2U << 8)
#define TDES_MR_OPMODE_CBC      (1U << 12)
#define TDES_MR_OPMODE_OFB      (2U << 12)
#define TDES_MR_OPMODE_CFB      (3U << 12)
#define TDES_MR_LOD             BIT(15)
#define TDES_MR_CFBS_32         (1U << 16)
#define TDES_MR_CFBS_16         (2U << 16)
#define TDES_MR_CFBS_8          (3U << 16)
#define TDES_INT_DATRDY         BIT(0)
#define TDES_INT_URAD           BIT(8)
#define TDES_INT_SECE           BIT(16)
#define TDES_WPMR_WPEN          BIT(0)
#define TDES_WPMR_WPITEN        BIT(1)
#define TDES_WPMR_WPCREN        BIT(2)
#define TDES_WPMR_ACTION_LOCK   (1U << 5)
#define TDES_WPMR_KEY           0x44455300
#define TDES_WPSR_WPVS          BIT(0)
#define TDES_WPSR_SWE           BIT(3)
#define TDES_WPSR_WPVSRC(offset) ((offset) << 8)
#define TDES_WPSR_SWETYP(type)  ((type) << 24)
#define TDES_WPSR_ECLASS        BIT(31)
#define TDES_SWE_READ_WO        0
#define TDES_SWE_WEIRD_ACTION   4
#define TDES_SWE_INCOMPLETE_KEY 5

#define SHA_CR                  0x00
#define SHA_MR                  0x04
#define SHA_IER                 0x10
#define SHA_IDR                 0x14
#define SHA_IMR                 0x18
#define SHA_ISR                 0x1c
#define SHA_MSR                 0x20
#define SHA_BCR                 0x30
#define SHA_IDATAR(index)       (0x40 + (index) * 4)
#define SHA_IODATAR(index)      (0x80 + (index) * 4)
#define SHA_WPMR                0xe4
#define SHA_WPSR                0xe8
#define SHA_VERSION             0xfc

#define SHA_CR_START            BIT(0)
#define SHA_CR_FIRST            BIT(4)
#define SHA_CR_SWRST            BIT(8)
#define SHA_CR_WUIHV            BIT(12)
#define SHA_CR_WUIEHV           BIT(13)
#define SHA_MR_SMOD_AUTO        1
#define SHA_MR_SMOD_DMA         2
#define SHA_MR_ALGO(algo)       ((algo) << 8)
#define SHA_MR_CHECK_EHV        (1U << 24)
#define SHA_MR_CHECK_MESSAGE    (2U << 24)
#define SHA_ALGO_SHA1           0
#define SHA_ALGO_SHA256         1
#define SHA_ALGO_SHA384         2
#define SHA_ALGO_SHA512         3
#define SHA_ALGO_SHA224         4
#define SHA_ALGO_HMAC_SHA256    9
#define SHA_INT_DATRDY          BIT(0)
#define SHA_INT_URAD            BIT(8)
#define SHA_INT_CHECKF          BIT(16)
#define SHA_INT_SECE            BIT(24)
#define SHA_ISR_CHKST_OK        (5U << 20)
#define SHA_WPMR_WPEN           BIT(0)
#define SHA_WPMR_WPITEN         BIT(1)
#define SHA_WPMR_WPCREN         BIT(2)
#define SHA_WPMR_KEY            0x53484100
#define SHA_WPSR_WPVS           BIT(0)
#define SHA_WPSR_WPVSRC(offset) ((offset) << 8)

#define TRNG_CR                 0x00
#define TRNG_MR                 0x04
#define TRNG_PKBCR              0x08
#define TRNG_IER                0x10
#define TRNG_IDR                0x14
#define TRNG_IMR                0x18
#define TRNG_ISR                0x1c
#define TRNG_ODATA              0x50
#define TRNG_WPMR               0xe4
#define TRNG_WPSR               0xe8

#define TRNG_CR_ENABLE          BIT(0)
#define TRNG_CR_KEY             0x524e4700
#define TRNG_MR_HALFR           BIT(0)
#define TRNG_MR_DIFF            BIT(7)
#define TRNG_INT_DATRDY         BIT(0)
#define TRNG_INT_SECE           BIT(1)
#define TRNG_WPMR_WPEN          BIT(0)
#define TRNG_WPMR_WPITEN        BIT(1)
#define TRNG_WPMR_WPCREN        BIT(2)
#define TRNG_WPMR_KEY           0x524e4700
#define TRNG_WPSR_WPVS          BIT(0)
#define TRNG_WPSR_SWE           BIT(3)
#define TRNG_WPSR_WPVSRC(offset) ((offset) << 8)
#define TRNG_WPSR_SWETYP(type)  ((type) << 24)
#define TRNG_WPSR_ECLASS        BIT(31)
#define TRNG_SWE_TRNG_DIS       3

#define PIO_PER                 0x000
#define PIO_PDR                 0x004
#define PIO_PSR                 0x008
#define PIO_OER                 0x010
#define PIO_OSR                 0x018
#define PIO_IFER                0x020
#define PIO_IFSR                0x028
#define PIO_SODR                0x030
#define PIO_CODR                0x034
#define PIO_ODSR                0x038
#define PIO_PDSR                0x03c
#define PIO_IER                 0x040
#define PIO_IMR                 0x048
#define PIO_ISR                 0x04c
#define PIO_MDER                0x050
#define PIO_MDSR                0x058
#define PIO_PUDR                0x060
#define PIO_PUER                0x064
#define PIO_PUSR                0x068
#define PIO_ABCDSR0             0x070
#define PIO_ABCDSR1             0x074
#define PIO_IFSCER              0x084
#define PIO_IFSCSR              0x088
#define PIO_SCDR                0x08c
#define PIO_PPDER               0x094
#define PIO_PPDSR               0x098
#define PIO_OWER                0x0a0
#define PIO_OWSR                0x0a8
#define PIO_AIMER               0x0b0
#define PIO_AIMDR               0x0b4
#define PIO_AIMMR               0x0b8
#define PIO_ESR                 0x0c0
#define PIO_LSR                 0x0c4
#define PIO_ELSR                0x0c8
#define PIO_FELLSR              0x0d0
#define PIO_REHLSR              0x0d4
#define PIO_FRLHSR              0x0d8
#define PIO_WPMR                0x0e4
#define PIO_WPSR                0x0e8
#define PIO_SCHMITT             0x100
#define PIO_SLEWR               0x110
#define PIO_DRIVER              0x118

#define PIO_WPMR_WPEN           BIT(0)
#define PIO_WPMR_KEY            0x50494f00

#define WDT_CR                  0x00
#define WDT_MR                  0x04
#define WDT_VR                  0x08
#define WDT_WLR                 0x0c
#define WDT_ILR                 0x10
#define WDT_IER                 0x14
#define WDT_IDR                 0x18
#define WDT_ISR                 0x1c
#define WDT_IMR                 0x20

#define WDT_CR_WDRSTT           BIT(0)
#define WDT_CR_LOCKMR           BIT(4)
#define WDT_CR_KEY              0xa5000000
#define WDT_MR_PERIODRST        BIT(4)
#define WDT_MR_RPTHRST          BIT(5)
#define WDT_MR_WDDIS            BIT(12)
#define WDT_INT_PER             BIT(0)
#define WDT_INT_RPTH            BIT(1)
#define WDT_INT_LVL             BIT(2)

#define NAND_DATA               SAM9X7_NAND_BASE
#define NAND_ALE                (SAM9X7_NAND_BASE + BIT(21))
#define NAND_CLE                (SAM9X7_NAND_BASE + BIT(22))
#define NAND_PAGE_SIZE          4096
#define NAND_OOB_SIZE           256
#define NAND_PAGE_TOTAL_SIZE    (NAND_PAGE_SIZE + NAND_OOB_SIZE)
#define NAND_NUM_PAGES          (64 * 2048)
#define NAND_DATA_SIZE          ((uint64_t)NAND_PAGE_SIZE * NAND_NUM_PAGES)
#define NAND_RAW_SIZE           ((uint64_t)NAND_PAGE_TOTAL_SIZE * \
                                 NAND_NUM_PAGES)
#define NAND_TEST_OOB_COLUMN    (NAND_PAGE_SIZE + 17)

#define NAND_CMD_READ0          0x00
#define NAND_CMD_PAGE_PROGRAM   0x10
#define NAND_CMD_READ_START     0x30
#define NAND_CMD_ERASE          0x60
#define NAND_CMD_STATUS         0x70
#define NAND_CMD_PROGRAM_START  0x80
#define NAND_CMD_RANDOM_INPUT   0x85
#define NAND_CMD_READ_ID        0x90
#define NAND_CMD_ERASE_START    0xd0
#define NAND_CMD_READ_PARAM     0xec
#define NAND_CMD_GET_FEATURES   0xee
#define NAND_CMD_SET_FEATURES   0xef
#define NAND_CMD_RESET          0xff

#define NAND_STATUS_FAIL        BIT(0)
#define NAND_STATUS_TRUE_READY  BIT(5)
#define NAND_STATUS_READY       BIT(6)
#define NAND_STATUS_WP          BIT(7)
#define NAND_STATUS_IDLE        (NAND_STATUS_TRUE_READY | \
                                 NAND_STATUS_READY | NAND_STATUS_WP)

#define SMC_SETUP2              0x20
#define SMC_PULSE2              0x24
#define SMC_CYCLE2              0x28
#define SMC_MODE2               0x2c
#define SMC_OCMS                0x80
#define SMC_KEY1                0x84
#define SMC_KEY2                0x88
#define SMC_SRIER               0x90
#define SMC_WPMR                0xe4
#define SMC_WPSR                0xe8
#define SMC_OCMS_MASK           0x00000711
#define SMC_SRIER_SRIE          BIT(0)
#define SMC_WPMR_KEY            0x534d4300
#define SMC_WPMR_WPEN           BIT(0)
#define SMC_WPSR_WPVS           BIT(0)
#define SMC_WPSR_SEQE           BIT(2)
#define SMC_WPSR_SWE            BIT(3)
#define SMC_WPSR_TYPE_MASK      (3U << 24)
#define SMC_WPSR_TYPE_WRITE_RO  (1U << 24)

#define PMECC_CFG               0x00
#define PMECC_SAREA             0x04
#define PMECC_SADDR             0x08
#define PMECC_EADDR             0x0c
#define PMECC_RESERVED_CLK      0x10
#define PMECC_CTRL              0x14
#define PMECC_SR                0x18
#define PMECC_IER               0x1c
#define PMECC_IMR               0x24
#define PMECC_ECC0              0x40
#define PMECC_BANK_COUNT        8
#define PMECC_BANK_STRIDE       0x40
#define PMECC_ECC_FIRST         0x40
#define PMECC_ECC_REG_COUNT     11
#define PMECC_REM_FIRST         0x240
#define PMECC_REM_REG_COUNT     12
#define PMECC_CTRL_ENABLE       BIT(4)
#define PMECC_CTRL_DISABLE      BIT(5)
#define PMECC_SR_ENABLE         BIT(4)

#define PMERRLOC_CFG            0x00
#define PMERRLOC_PRIM           0x04
#define PMERRLOC_LEN            0x08
#define PMERRLOC_DIS            0x0c
#define PMERRLOC_IER            0x14
#define PMERRLOC_IMR            0x1c
#define PMERRLOC_ISR            0x20
#define PMERRLOC_DONE           BIT(0)

#define QSPI_CR                 0x00
#define QSPI_MR                 0x04
#define QSPI_RDR                0x08
#define QSPI_TDR                0x0c
#define QSPI_ISR                0x10
#define QSPI_IER                0x14
#define QSPI_IDR                0x18
#define QSPI_IMR                0x1c
#define QSPI_SCR                0x20
#define QSPI_SR                 0x24
#define QSPI_IAR                0x30
#define QSPI_WICR               0x34
#define QSPI_IFR                0x38
#define QSPI_RICR               0x3c
#define QSPI_WRACNT             0x54
#define QSPI_WPMR               0xe4
#define QSPI_WPSR               0xe8

#define QSPI_CR_QSPIEN          BIT(0)
#define QSPI_CR_QSPIDIS         BIT(1)
#define QSPI_CR_STTFR           BIT(9)
#define QSPI_CR_LASTXFER        BIT(24)
#define QSPI_MR_SMM             BIT(0)
#define QSPI_ISR_RDRF           BIT(0)
#define QSPI_ISR_TDRE           BIT(1)
#define QSPI_ISR_TXEMPTY        BIT(2)
#define QSPI_ISR_OVRES          BIT(3)
#define QSPI_ISR_CSR            BIT(8)
#define QSPI_ISR_LWRA           BIT(11)
#define QSPI_ISR_CSFA           BIT(14)
#define QSPI_ISR_CSRA           BIT(15)
#define QSPI_ISR_RFRSHD         BIT(16)
#define QSPI_SR_QSPIENS         BIT(1)
#define QSPI_SR_CSS             BIT(2)
#define QSPI_SR_HIDLE           BIT(4)
#define QSPI_IFR_INSTEN         BIT(4)
#define QSPI_IFR_ADDREN         BIT(5)
#define QSPI_IFR_DATAEN         BIT(7)
#define QSPI_IFR_ADDRL_3        (2U << 10)
#define QSPI_IFR_TFRTYP_MEM     BIT(12)
#define QSPI_IFR_NBDUM(n)       ((n) << 16)
#define QSPI_WPMR_WPEN          BIT(0)
#define QSPI_WPMR_KEY           0x51535000

static uint64_t get_clock_period(QTestState *qts, const char *path)
{
    QDict *response;
    uint64_t period;

    response = qtest_qmp(qts,
                         "{ 'execute': 'qom-get', 'arguments': { "
                         "'path': %s, 'property': 'qtest-clock-period' } }",
                         path);
    g_assert_false(qdict_haskey(response, "error"));
    period = qdict_get_int(response, "return");
    qobject_unref(response);
    return period;
}

static int64_t qom_get_int(QTestState *qts, const char *path,
                           const char *property)
{
    QDict *response;
    int64_t value;

    response = qtest_qmp(qts,
                         "{ 'execute': 'qom-get', 'arguments': { "
                         "'path': %s, 'property': %s } }",
                         path, property);
    g_assert_false(qdict_haskey(response, "error"));
    value = qdict_get_int(response, "return");
    qobject_unref(response);
    return value;
}

static bool qom_has_property(QTestState *qts, const char *path,
                             const char *property)
{
    QDict *response;
    QList *properties;
    QListEntry *entry;
    bool found = false;

    response = qtest_qmp(qts,
                         "{ 'execute': 'qom-list', 'arguments': { "
                         "'path': %s } }", path);
    g_assert_false(qdict_haskey(response, "error"));
    properties = qdict_get_qlist(response, "return");
    QLIST_FOREACH_ENTRY(properties, entry) {
        QDict *item = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(item, "name"), property)) {
            found = true;
            break;
        }
    }
    qobject_unref(response);
    return found;
}

static void send_input_key(QTestState *qts, const char *qcode, bool down)
{
    qtest_qmp_assert_success(qts,
        "{ 'execute': 'input-send-event', 'arguments': { 'events': ["
        "{ 'type': 'key', 'data': { 'down': %i, 'key': {"
        "'type': 'qcode', 'data': %s } } } ] } }", down, qcode);
}

static void pmc_write_pcr(QTestState *qts, unsigned int id,
                          uint32_t config);

static uint64_t usart_cycles_to_ns(QTestState *qts, unsigned int pid,
                                   uint64_t cycles)
{
    g_autofree char *path = g_strdup_printf(
        "/machine/soc/pmc/pclk[%u]", pid);
    uint64_t period = get_clock_period(qts, path);

    return (period * cycles) >> 32;
}

static uint32_t usart_wait_status(QTestState *qts, uint64_t base,
                                  uint32_t mask)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    uint32_t status;

    do {
        status = qtest_readl(qts, base + US_CSR);
        if ((status & mask) == mask) {
            return status;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);

    g_error("timed out waiting for USART status mask 0x%08x "
            "(status 0x%08x)", mask, status);
}

static QTestState *qtest_init_with_flexcom0_serial(int *sock_fd)
{
    g_autofree char *sock_dir = NULL;
    g_autofree char *sock_path = NULL;
    QTestState *qts;
    int server_fd;

    sock_dir = g_dir_make_tmp("qtest-flexcom-serial-XXXXXX", NULL);
    g_assert_nonnull(sock_dir);
    sock_path = g_strdup_printf("%s/sock", sock_dir);
    server_fd = qtest_socket_server(sock_path);

    qts = qtest_initf("-chardev socket,id=s1,path=%s "
                      "-serial null -serial chardev:s1 %s",
                      sock_path, SAM9X75_MACHINE);
    *sock_fd = accept(server_fd, NULL, NULL);
    g_assert_cmpint(*sock_fd, >=, 0);
    close(server_fd);
    unlink(sock_path);
    rmdir(sock_dir);
    return qts;
}

static uint32_t dbgu_wait_status(QTestState *qts, uint32_t mask)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    uint32_t status;

    do {
        status = qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR);
        if ((status & mask) == mask) {
            return status;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);

    g_error("timed out waiting for DBGU status mask 0x%08x (status 0x%08x)",
            mask, status);
}

static uint32_t ebi_read_assignments(QTestState *qts)
{
    return qtest_readl(qts, SAM9X7_SFR_BASE + SFR_CCFG_EBICSA);
}

static void ebi_update_assignments(QTestState *qts, uint32_t clear,
                                   uint32_t set)
{
    uint32_t value = ebi_read_assignments(qts);

    value &= ~clear;
    value |= set;
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_CCFG_EBICSA, value);
}

static void ebi_enable_ddr(QTestState *qts)
{
    ebi_update_assignments(qts, 0, SFR_CCFG_EBI_CS1A);
}

static void ebi_enable_nand(QTestState *qts)
{
    ebi_update_assignments(qts, 0,
                           SFR_CCFG_EBI_CS2A | SFR_CCFG_NFD0_ON_D16);
}

static void ebi_enable_ddr_and_nand(QTestState *qts)
{
    ebi_update_assignments(qts, 0,
                           SFR_CCFG_EBI_CS1A | SFR_CCFG_EBI_CS2A |
                           SFR_CCFG_NFD0_ON_D16);
}

static void test_memory_and_identification(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CIDR), ==,
                    0x89750031);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_EXID), ==,
                    0x00000020);

    qtest_writel(qts, SAM9X7_SRAM0_BASE, 0x5a17c0de);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE), ==, 0x5a17c0de);

    qtest_writel(qts, SAM9X7_SRAM1_BASE, 0x0f0e0d0c);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE), ==, 0x0f0e0d0c);

    ebi_enable_ddr(qts);
    qtest_writel(qts, SAM9X7_DDR_BASE, 0xc001d00d);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0xc001d00d);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_ROM_BASE), ==,
                    qtest_readl(qts, SAM9X7_BOOT_BASE));

    qtest_quit(qts);
}

static void test_matrix_registers_and_protection(void)
{
    static const uint32_t pras_reset[] = {
        0x00000777, 0x00077777, 0x00007700, 0x00070000,
        0x00000077, 0x00077777, 0x00077000, 0x00007000,
        0x00077000, 0x00077000, 0x00077070, 0x00000000,
    };
    static const uint32_t prbs_reset[] = {
        0x00000000, 0x00110000, 0x00010000, 0x00100000,
        0x00000000, 0x00110000, 0x00110000, 0x00000000,
        0x00100000, 0x00100000, 0x00110000, 0x00110000,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    for (i = 0; i < 14; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_MCFG(i)), ==, 4);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_MEAR(i)), ==, 0);
        qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MCFG(i),
                     UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_MCFG(i)), ==, 7);
    }
    for (i = 0; i < 12; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_SCFG(i)), ==, 0x000001ff);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_PRAS(i)), ==, pras_reset[i]);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_PRBS(i)), ==, prbs_reset[i]);

        qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_SCFG(i),
                     UINT32_MAX);
        qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_PRAS(i),
                     UINT32_MAX);
        qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_PRBS(i),
                     UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_SCFG(i)), ==, 0x003f01ff);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_PRAS(i)), ==, 0x77777777);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE +
                                    MATRIX_PRBS(i)), ==, 0x00777777);
    }

    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MEIMR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MESR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPSR),
                    ==, 0);

    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MEIER, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MEIMR),
                    ==, 0x00003fff);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MEIDR, 0x1555);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MEIMR),
                    ==, 0x00002aaa);

    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR,
                 MATRIX_WPMR_KEY | MATRIX_WPMR_WPEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR),
                    ==, MATRIX_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR),
                    ==, MATRIX_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_SCFG(3), 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_SCFG(3)),
                    ==, 0x003f01ff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPSR),
                    ==, (MATRIX_SCFG(3) << 8) | 1);

    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR, MATRIX_WPMR_KEY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPSR),
                    ==, 0);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_SCFG(3), 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_SCFG(3)),
                    ==, 0);

    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR,
                 MATRIX_WPMR_KEY | MATRIX_WPMR_CFGFRZ);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR, MATRIX_WPMR_KEY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR),
                    ==, MATRIX_WPMR_CFGFRZ);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR, BIT(12));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPSR),
                    ==, (MATRIX_MRCR << 8) | 1);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MCFG(0)),
                    ==, 4);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_SCFG(3)),
                    ==, 0x000001ff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MEIMR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_WPMR),
                    ==, 0);

    qtest_quit(qts);
}

static void test_matrix_boot_remap(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t rom_word = qtest_readl(qts, SAM9X7_ROM_BASE);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==, rom_word);
    qtest_writel(qts, SAM9X7_SRAM0_BASE, 0x1234abcd);
    qtest_writel(qts, SAM9X7_SRAM0_BASE + 4, 0x5678ef90);

    /* A non-CPU host remap must not alter the CPU's boot view. */
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR, BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==, rom_word);

    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR, BIT(12));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==, 0x1234abcd);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE + 4), ==,
                    0x5678ef90);
    qtest_writel(qts, SAM9X7_BOOT_BASE + 4, 0xa5a55a5a);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + 4), ==,
                    0xa5a55a5a);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_ROM_BASE), ==, rom_word);

    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==, rom_word);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR, BIT(13));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==, 0x1234abcd);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==, rom_word);

    qtest_quit(qts);
}

static void test_matrix_remap_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    qtest_writel(from, SAM9X7_SRAM0_BASE, 0xfeed9750);
    qtest_writel(from, SAM9X7_MATRIX_BASE + MATRIX_MCFG(4), 2);
    qtest_writel(from, SAM9X7_MATRIX_BASE + MATRIX_MRCR,
                 BIT(12) | BIT(13));
    qtest_writel(from, SAM9X7_MATRIX_BASE + MATRIX_WPMR,
                 MATRIX_WPMR_KEY | MATRIX_WPMR_WPEN);
    g_assert_cmphex(qtest_readl(from, SAM9X7_BOOT_BASE), ==, 0xfeed9750);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_MATRIX_BASE + MATRIX_MCFG(4)),
                    ==, 2);
    g_assert_cmphex(qtest_readl(to, SAM9X7_MATRIX_BASE + MATRIX_MRCR),
                    ==, BIT(12) | BIT(13));
    g_assert_cmphex(qtest_readl(to, SAM9X7_MATRIX_BASE + MATRIX_WPMR),
                    ==, MATRIX_WPMR_WPEN);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM0_BASE), ==, 0xfeed9750);
    g_assert_cmphex(qtest_readl(to, SAM9X7_BOOT_BASE), ==, 0xfeed9750);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_rom_image_loading(void)
{
    const uint32_t first = cpu_to_le32(0xea000006);
    const uint32_t last = cpu_to_le32(0x9750cafe);
    g_autofree char *rom_path = NULL;
    QTestState *qts;
    GError *error = NULL;
    int fd;
    int ret;

    fd = g_file_open_tmp("sam9x75-rom-XXXXXX", &rom_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, SAM9X7_ROM_SIZE);
    g_assert_cmpint(ret, ==, 0);
    ret = pwrite(fd, &first, sizeof(first), 0);
    g_assert_cmpint(ret, ==, sizeof(first));
    ret = pwrite(fd, &last, sizeof(last), SAM9X7_ROM_SIZE - sizeof(last));
    g_assert_cmpint(ret, ==, sizeof(last));
    close(fd);

    qts = qtest_initf(SAM9X75_MACHINE " -bios %s", rom_path);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_ROM_BASE), ==,
                    le32_to_cpu(first));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==,
                    le32_to_cpu(first));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_ROM_BASE +
                                SAM9X7_ROM_SIZE - sizeof(last)), ==,
                    le32_to_cpu(last));

    qtest_writel(qts, SAM9X7_SRAM0_BASE, 0x12349750);
    qtest_writel(qts, SAM9X7_MATRIX_BASE + MATRIX_MRCR,
                 BIT(12) | BIT(13));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==, 0x12349750);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BOOT_BASE), ==,
                    le32_to_cpu(first));

    qtest_quit(qts);
    unlink(rom_path);
}

static void test_rom_cpu_entry(void)
{
    static const uint8_t semihost_exit[] = {
        0x18, 0x00, 0xa0, 0xe3, /* mov r0, #SYS_EXIT */
        0x04, 0x10, 0x9f, 0xe5, /* ldr r1, [pc, #4] */
        0x56, 0x34, 0x12, 0xef, /* svc 0x123456 */
        0xfe, 0xff, 0xff, 0xea, /* b . */
        0x26, 0x00, 0x02, 0x00, /* ADP_Stopped_ApplicationExit */
    };
    g_autofree char *rom_path = NULL;
    const char *qemu = qtest_qemu_binary(NULL);
    GError *error = NULL;
    gchar *argv[] = {
        (gchar *)qemu,
        (gchar *)"-machine", (gchar *)"sam9x75-curiosity",
        (gchar *)"-bios", rom_path,
        (gchar *)"-display", (gchar *)"none",
        (gchar *)"-serial", (gchar *)"none",
        (gchar *)"-monitor", (gchar *)"none",
        (gchar *)"-nic", (gchar *)"none",
        (gchar *)"-run-with", (gchar *)"exit-with-parent=on",
        (gchar *)"-semihosting-config",
        (gchar *)"enable=on,target=native",
        NULL,
    };
    int wait_status;
    int fd;
    int ret;

    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }

    fd = g_file_open_tmp("sam9x75-rom-entry-XXXXXX", &rom_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, SAM9X7_ROM_SIZE);
    g_assert_cmpint(ret, ==, 0);
    ret = pwrite(fd, semihost_exit, sizeof(semihost_exit), 0);
    g_assert_cmpint(ret, ==, sizeof(semihost_exit));
    close(fd);

    argv[4] = rom_path;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, NULL, NULL, &wait_status,
                               &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 0);

    unlink(rom_path);
}

static void test_direct_linux_ddr_assignment(void)
{
    static const uint8_t check_ccfg_and_exit[] = {
        0x1c, 0x00, 0x9f, 0xe5, /* ldr r0, [pc, #28] */
        0x00, 0x00, 0x90, 0xe5, /* ldr r0, [r0] */
        0x18, 0x10, 0x9f, 0xe5, /* ldr r1, [pc, #24] */
        0x01, 0x00, 0x50, 0xe1, /* cmp r0, r1 */
        0xfe, 0xff, 0xff, 0x1a, /* bne . */
        0x18, 0x00, 0xa0, 0xe3, /* mov r0, #SYS_EXIT */
        0x0c, 0x10, 0x9f, 0xe5, /* ldr r1, [pc, #12] */
        0x56, 0x34, 0x12, 0xef, /* svc 0x123456 */
        0xfe, 0xff, 0xff, 0xea, /* b . */
        0x04, 0x00, 0x05, 0xf8, /* SFR_CCFG_EBICSA */
        0x02, 0x03, 0x00, 0x00, /* reset value plus CS1A */
        0x26, 0x00, 0x02, 0x00, /* ADP_Stopped_ApplicationExit */
    };
    g_autofree char *kernel_path = NULL;
    const char *qemu = qtest_qemu_binary(NULL);
    GError *error = NULL;
    gchar *argv[] = {
        (gchar *)qemu,
        (gchar *)"-machine", (gchar *)"sam9x75-curiosity",
        (gchar *)"-kernel", kernel_path,
        (gchar *)"-display", (gchar *)"none",
        (gchar *)"-serial", (gchar *)"none",
        (gchar *)"-monitor", (gchar *)"none",
        (gchar *)"-nic", (gchar *)"none",
        (gchar *)"-run-with", (gchar *)"exit-with-parent=on",
        (gchar *)"-semihosting-config",
        (gchar *)"enable=on,target=native",
        NULL,
    };
    int wait_status;
    int fd;
    int ret;

    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }

    fd = g_file_open_tmp("sam9x75-linux-entry-XXXXXX", &kernel_path,
                         &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = pwrite(fd, check_ccfg_and_exit, sizeof(check_ccfg_and_exit), 0);
    g_assert_cmpint(ret, ==, sizeof(check_ccfg_and_exit));
    close(fd);

    argv[4] = kernel_path;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, NULL, NULL, &wait_status,
                               &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 0);

    unlink(kernel_path);
}

static void test_firmware_elf_true_reset_assignment(void)
{
    static const uint8_t check_ccfg_and_exit[] = {
        0x1c, 0x00, 0x9f, 0xe5, /* ldr r0, [pc, #28] */
        0x00, 0x00, 0x90, 0xe5, /* ldr r0, [r0] */
        0x18, 0x10, 0x9f, 0xe5, /* ldr r1, [pc, #24] */
        0x01, 0x00, 0x50, 0xe1, /* cmp r0, r1 */
        0xfe, 0xff, 0xff, 0x1a, /* bne . */
        0x18, 0x00, 0xa0, 0xe3, /* mov r0, #SYS_EXIT */
        0x0c, 0x10, 0x9f, 0xe5, /* ldr r1, [pc, #12] */
        0x56, 0x34, 0x12, 0xef, /* svc 0x123456 */
        0xfe, 0xff, 0xff, 0xea, /* b . */
        0x04, 0x00, 0x05, 0xf8, /* SFR_CCFG_EBICSA */
        0x00, 0x03, 0x00, 0x00, /* true hardware reset value */
        0x26, 0x00, 0x02, 0x00, /* ADP_Stopped_ApplicationExit */
    };
    const off_t payload_offset = 0x100;
    g_autofree char *firmware_path = NULL;
    const char *qemu = qtest_qemu_binary(NULL);
    Elf32_Ehdr ehdr = { 0 };
    Elf32_Phdr phdr = { 0 };
    GError *error = NULL;
    gchar *argv[] = {
        (gchar *)qemu,
        (gchar *)"-machine", (gchar *)"sam9x75-curiosity",
        (gchar *)"-kernel", firmware_path,
        (gchar *)"-display", (gchar *)"none",
        (gchar *)"-serial", (gchar *)"none",
        (gchar *)"-monitor", (gchar *)"none",
        (gchar *)"-nic", (gchar *)"none",
        (gchar *)"-run-with", (gchar *)"exit-with-parent=on",
        (gchar *)"-semihosting-config",
        (gchar *)"enable=on,target=native",
        NULL,
    };
    int wait_status;
    int fd;
    int ret;

    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }

    memcpy(ehdr.e_ident, ELFMAG, SELFMAG);
    ehdr.e_ident[EI_CLASS] = ELFCLASS32;
    ehdr.e_ident[EI_DATA] = ELFDATA2LSB;
    ehdr.e_ident[EI_VERSION] = EV_CURRENT;
    ehdr.e_type = cpu_to_le16(ET_EXEC);
    ehdr.e_machine = cpu_to_le16(EM_ARM);
    ehdr.e_version = cpu_to_le32(EV_CURRENT);
    ehdr.e_entry = cpu_to_le32(SAM9X7_SRAM0_BASE);
    ehdr.e_phoff = cpu_to_le32(sizeof(ehdr));
    ehdr.e_ehsize = cpu_to_le16(sizeof(ehdr));
    ehdr.e_phentsize = cpu_to_le16(sizeof(phdr));
    ehdr.e_phnum = cpu_to_le16(1);

    phdr.p_type = cpu_to_le32(PT_LOAD);
    phdr.p_offset = cpu_to_le32(payload_offset);
    phdr.p_vaddr = cpu_to_le32(SAM9X7_SRAM0_BASE);
    phdr.p_paddr = cpu_to_le32(SAM9X7_SRAM0_BASE);
    phdr.p_filesz = cpu_to_le32(sizeof(check_ccfg_and_exit));
    phdr.p_memsz = cpu_to_le32(sizeof(check_ccfg_and_exit));
    phdr.p_flags = cpu_to_le32(PF_R | PF_X);
    phdr.p_align = cpu_to_le32(4);

    fd = g_file_open_tmp("sam9x75-firmware-entry-XXXXXX", &firmware_path,
                         &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = pwrite(fd, &ehdr, sizeof(ehdr), 0);
    g_assert_cmpint(ret, ==, sizeof(ehdr));
    ret = pwrite(fd, &phdr, sizeof(phdr), sizeof(ehdr));
    g_assert_cmpint(ret, ==, sizeof(phdr));
    ret = pwrite(fd, check_ccfg_and_exit, sizeof(check_ccfg_and_exit),
                 payload_offset);
    g_assert_cmpint(ret, ==, sizeof(check_ccfg_and_exit));
    close(fd);

    argv[4] = firmware_path;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, NULL, NULL, &wait_status,
                               &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 0);

    unlink(firmware_path);
}

static void assert_rom_image_size_rejected(off_t size)
{
    g_autofree char *rom_path = NULL;
    g_autofree char *stderr_text = NULL;
    const char *qemu = qtest_qemu_binary(NULL);
    GError *error = NULL;
    gchar *argv[] = {
        (gchar *)qemu,
        (gchar *)"-machine", (gchar *)"sam9x75-curiosity",
        (gchar *)"-bios", rom_path,
        (gchar *)"-display", (gchar *)"none",
        (gchar *)"-serial", (gchar *)"none",
        (gchar *)"-monitor", (gchar *)"none",
        (gchar *)"-nic", (gchar *)"none",
        NULL,
    };
    int wait_status;
    int fd;
    int ret;

    fd = g_file_open_tmp("sam9x75-rom-size-XXXXXX", &rom_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, size);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    argv[4] = rom_path;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), !=, 0);
    g_assert_nonnull(g_strstr_len(stderr_text, -1,
                                 "requires a complete 176 KiB ROM image"));

    unlink(rom_path);
}

static void test_rom_image_size_validation(void)
{
    assert_rom_image_size_rejected(SAM9X7_ROM_SIZE - 1);
    assert_rom_image_size_rejected(SAM9X7_ROM_SIZE + 1);
}

static void test_rom_boot_path_validation(void)
{
    g_autofree char *image_path = NULL;
    g_autofree char *stderr_text = NULL;
    const char *qemu = qtest_qemu_binary(NULL);
    GError *error = NULL;
    gchar *argv[] = {
        (gchar *)qemu,
        (gchar *)"-machine", (gchar *)"sam9x75-curiosity",
        (gchar *)"-bios", image_path,
        (gchar *)"-kernel", image_path,
        (gchar *)"-display", (gchar *)"none",
        (gchar *)"-serial", (gchar *)"none",
        (gchar *)"-monitor", (gchar *)"none",
        (gchar *)"-nic", (gchar *)"none",
        NULL,
    };
    int wait_status;
    int fd;

    fd = g_file_open_tmp("sam9x75-boot-path-XXXXXX", &image_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    argv[4] = image_path;
    argv[6] = image_path;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), !=, 0);
    g_assert_nonnull(g_strstr_len(stderr_text, -1,
                                 "-bios and -kernel select different"));

    unlink(image_path);
}

static void test_dbgu_registers_and_loopback(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t duration;
    uint64_t period;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR), ==,
                    DBGU_INT_TXRDY | DBGU_INT_TXEMPTY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_PDC_PTSR),
                    ==, 0);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_PDC_PTCR, BIT(9));

    pmc_write_pcr(qts, 47, PMC_PCR_EN);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_BRGR, 217);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_BRGR), ==, 217);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_RTOR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_RTOR), ==,
                    0xff);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_FNTR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_FNTR), ==, 1);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_MR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_MR), ==,
                    DBGU_MR_MASK);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_WPMR,
                 DBGU_WPMR_KEY | DBGU_WPMR_WPEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_WPMR), ==,
                    DBGU_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_BRGR, 42);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_RTOR, 42);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_FNTR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_BRGR), ==, 217);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_RTOR), ==,
                    0xff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_FNTR), ==, 1);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_WPMR), ==,
                    DBGU_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_WPMR, DBGU_WPMR_KEY);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_IER,
                 DBGU_INT_RXRDY | DBGU_INT_TXRDY | DBGU_INT_TIMEOUT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_IMR), ==,
                    DBGU_INT_RXRDY | DBGU_INT_TXRDY | DBGU_INT_TIMEOUT);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_IDR, DBGU_INT_TXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_IMR), ==,
                    DBGU_INT_RXRDY | DBGU_INT_TIMEOUT);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_MR,
                 DBGU_MR_LOCAL_LOOPBACK);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_RTOR, 3);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_CR,
                 DBGU_CR_RXEN | DBGU_CR_TXEN);
    qtest_writeb(qts, SAM9X7_DBGU_BASE + DBGU_THR, 0x5a);
    g_assert_true(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                  DBGU_INT_RXRDY);
    g_assert_cmphex(qtest_readb(qts, SAM9X7_DBGU_BASE + DBGU_RHR), ==, 0x5a);
    g_assert_false(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                   DBGU_INT_RXRDY);

    period = get_clock_period(qts, "/machine/soc/pmc/pclk[47]");
    duration = (period * 16 * 217 * 3) >> 32;
    g_assert_cmpuint(duration, >, 1);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_CR, DBGU_CR_RETTO);
    qtest_clock_step(qts, duration - 1);
    g_assert_false(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                   DBGU_INT_TIMEOUT);
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                  DBGU_INT_TIMEOUT);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_CR, DBGU_CR_STTTO);
    qtest_clock_step(qts, duration);
    g_assert_false(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                   DBGU_INT_TIMEOUT);
    qtest_writeb(qts, SAM9X7_DBGU_BASE + DBGU_THR, 0xa6);
    qtest_clock_step(qts, duration);
    g_assert_true(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                  DBGU_INT_TIMEOUT);

    qtest_quit(qts);
}

static void test_dbgu_chardev(void)
{
    int sock_fd;
    uint8_t value;
    QTestState *qts = qtest_init_with_serial(SAM9X75_MACHINE, &sock_fd);

    pmc_write_pcr(qts, 47, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_BRGR, 217);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_CR,
                 DBGU_CR_RXEN | DBGU_CR_TXEN);

    value = 0xa5;
    g_assert_cmpint(send(sock_fd, &value, 1, 0), ==, 1);
    dbgu_wait_status(qts, DBGU_INT_RXRDY);
    g_assert_cmphex(qtest_readb(qts, SAM9X7_DBGU_BASE + DBGU_RHR), ==, value);

    qtest_writeb(qts, SAM9X7_DBGU_BASE + DBGU_THR, 0x3c);
    g_assert_cmpint(recv(sock_fd, &value, 1, 0), ==, 1);
    g_assert_cmphex(value, ==, 0x3c);

    close(sock_fd);
    qtest_quit(qts);
}

static void aic_select(QTestState *qts, unsigned int source)
{
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_SSR, source);
}

static void aic_configure(QTestState *qts, unsigned int source,
                          uint32_t mode, uint32_t vector)
{
    aic_select(qts, source);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_IDCR, 1);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_ICCR, 1);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_SMR, mode);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_SVR, vector);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_IECR, 1);
}

static void aic_set_input(QTestState *qts, unsigned int source, int level)
{
    qtest_set_irq_in(qts, "/machine/soc/aic", "unnamed-gpio-in",
                     source, level);
}

static uint16_t gem_mdio_read(QTestState *qts, unsigned int phy,
                              unsigned int reg)
{
    uint32_t command = GEM_MDIO_READ | GEM_MDIO_PHY(phy) |
                       GEM_MDIO_REG(reg);

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_PHYMNTNC, command);
    return qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_PHYMNTNC);
}

static void twi_enable_master(QTestState *qts, uint64_t flexcom_base,
                              uint64_t twi_base, unsigned int pid)
{
    pmc_write_pcr(qts, pid, PMC_PCR_EN);
    qtest_writeb(qts, flexcom_base + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(qts, twi_base + TWI_CR, TWI_CR_SWRST);
    qtest_writel(qts, twi_base + TWI_CR,
                 TWI_CR_MSEN | TWI_CR_SVDIS);
}

static void twi_wait_status(QTestState *qts, uint64_t twi_base,
                            uint32_t mask)
{
    unsigned int i;

    for (i = 0; i < 128; i++) {
        if (qtest_readl(qts, twi_base + TWI_SR) & mask) {
            return;
        }
        qtest_clock_step_next(qts);
    }
    g_assert_not_reached();
}

static void twi_read_regs(QTestState *qts, uint64_t twi_base,
                          uint8_t address, uint8_t reg, uint8_t *data,
                          size_t length)
{
    size_t i;

    g_assert_cmpuint(length, >, 0);
    qtest_writel(qts, twi_base + TWI_MMR,
                 TWI_MMR_DADR(address) | TWI_MMR_MREAD |
                 TWI_MMR_IADRSZ_1);
    qtest_writel(qts, twi_base + TWI_IADR, reg);
    qtest_writel(qts, twi_base + TWI_CR,
                 TWI_CR_START | (length == 1 ? TWI_CR_STOP : 0));

    for (i = 0; i < length; i++) {
        if (i == length - 1 && length != 1) {
            qtest_writel(qts, twi_base + TWI_CR, TWI_CR_STOP);
        }
        twi_wait_status(qts, twi_base, TWI_SR_RXRDY);
        data[i] = qtest_readb(qts, twi_base + TWI_RHR);
    }
}

static uint8_t twi_read_reg(QTestState *qts, uint64_t twi_base,
                            uint8_t address, uint8_t reg)
{
    uint8_t value;

    twi_read_regs(qts, twi_base, address, reg, &value, 1);
    return value;
}

static uint8_t twi_read_reg_before_deadline(QTestState *qts,
                                            uint64_t twi_base,
                                            uint8_t address, uint8_t reg,
                                            int64_t deadline)
{
    int64_t now = qtest_clock_step(qts, 1);
    uint8_t value;

    g_assert_cmpint(deadline - now, >, TWI_TEST_LATENCY_NS);
    qtest_clock_step(qts, deadline - now - TWI_TEST_LATENCY_NS);
    value = twi_read_reg(qts, twi_base, address, reg);
    now = qtest_clock_step(qts, 1);
    g_assert_cmpint(now, <, deadline);
    if (deadline - now > 1) {
        qtest_clock_step(qts, deadline - now - 1);
    }
    return value;
}

static void qtest_clock_step_to(QTestState *qts, int64_t deadline)
{
    int64_t now = qtest_clock_step(qts, 1);

    g_assert_cmpint(now, <=, deadline);
    if (now < deadline) {
        qtest_clock_step(qts, deadline - now);
    }
}

static void twi_write_reg(QTestState *qts, uint64_t twi_base,
                          uint8_t address, uint8_t reg, uint8_t value)
{
    qtest_writel(qts, twi_base + TWI_MMR,
                 TWI_MMR_DADR(address) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, twi_base + TWI_IADR, reg);
    qtest_writeb(qts, twi_base + TWI_THR, value);
    qtest_writel(qts, twi_base + TWI_CR, TWI_CR_STOP);
    twi_wait_status(qts, twi_base, TWI_SR_TXCOMP);
}

static void twi_send_byte(QTestState *qts, uint64_t twi_base,
                          uint8_t address, uint8_t value)
{
    qtest_writel(qts, twi_base + TWI_MMR, TWI_MMR_DADR(address));
    qtest_writeb(qts, twi_base + TWI_THR, value);
    qtest_writel(qts, twi_base + TWI_CR, TWI_CR_STOP);
    twi_wait_status(qts, twi_base, TWI_SR_TXCOMP);
}

static void twi6_enable_master(QTestState *qts)
{
    twi_enable_master(qts, SAM9X7_FLEXCOM6_BASE, SAM9X7_TWI6_BASE, 9);
}

static uint8_t twi6_read_reg(QTestState *qts, uint8_t address, uint8_t reg)
{
    return twi_read_reg(qts, SAM9X7_TWI6_BASE, address, reg);
}

static uint8_t twi6_read_reg_before_deadline(QTestState *qts,
                                             uint8_t address, uint8_t reg,
                                             int64_t deadline)
{
    return twi_read_reg_before_deadline(qts, SAM9X7_TWI6_BASE, address,
                                        reg, deadline);
}

static void twi6_write_reg(QTestState *qts, uint8_t address, uint8_t reg,
                           uint8_t value)
{
    twi_write_reg(qts, SAM9X7_TWI6_BASE, address, reg, value);
}

static void test_mcp16502_registers_and_regulators(void)
{
    static const struct {
        uint8_t reg;
        uint8_t value;
    } reset_values[] = {
        { 0x00, 0xdb }, { 0x01, 0x20 }, { 0x02, 0x54 }, { 0x03, 0xc0 },
        { 0x10, 0xf7 }, { 0x11, 0xb7 }, { 0x12, 0x37 }, { 0x13, 0xf7 },
        { 0x14, 0x09 }, { 0x15, 0xb0 },
        { 0x20, 0xeb }, { 0x21, 0xab }, { 0x22, 0xab }, { 0x23, 0xeb },
        { 0x24, 0x1d }, { 0x25, 0xa0 },
        { 0x30, 0xe3 }, { 0x31, 0xa3 }, { 0x32, 0x23 }, { 0x33, 0xe3 },
        { 0x34, 0x2c }, { 0x35, 0xb0 },
        { 0x40, 0xe3 }, { 0x41, 0xa3 }, { 0x42, 0x23 }, { 0x43, 0xe3 },
        { 0x44, 0x2c }, { 0x45, 0xa0 },
        { 0x50, 0xb7 }, { 0x51, 0xb7 }, { 0x52, 0x37 }, { 0x53, 0xb7 },
        { 0x54, 0x09 }, { 0x55, 0xa0 },
        { 0x60, 0x37 }, { 0x61, 0x37 }, { 0x62, 0x37 }, { 0x63, 0x37 },
        { 0x64, 0x01 }, { 0x65, 0xa0 },
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    twi6_enable_master(qts);

    for (i = 0; i < ARRAY_SIZE(reset_values); i++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, reset_values[i].reg), ==,
                        reset_values[i].value);
    }

    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x04), ==, 0);
    for (i = 0x05; i <= 0x09; i++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, i), ==, 0x07);
    }
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x0a), ==, 0);

    twi6_write_reg(qts, 0x5b, 0x00, 0);
    twi6_write_reg(qts, 0x5b, 0x01, 0xff);
    twi6_write_reg(qts, 0x5b, 0x05, 0xff);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x00), ==, 0xdb);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x01), ==, 0x20);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);

    twi6_write_reg(qts, 0x5b, 0x02, 0xff);
    twi6_write_reg(qts, 0x5b, 0x50, 0xff);
    twi6_write_reg(qts, 0x5b, 0x55, 0xff);
    twi6_write_reg(qts, 0x5b, 0x0b, 0xff);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x02), ==, 0xf7);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x50), ==, 0xbf);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x55), ==, 0xaf);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x0b), ==, 0);

    twi6_write_reg(qts, 0x5b, 0x40, 0x63);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x40), ==, 0x63);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x08), ==, 0);
    twi6_write_reg(qts, 0x5b, 0x40, 0xff);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x08), ==, 0x07);

    qtest_system_reset(qts);
    twi6_enable_master(qts);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x02), ==, 0x54);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x40), ==, 0xe3);

    qtest_quit(qts);
}

static void test_board_rgb_led_and_user_button(void)
{
    const uint32_t leds = BIT(14) | BIT(20) | BIT(21);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR,
                 PMC_PCR_CMD | PMC_PCR_EN | 4);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(9));
    send_input_key(qts, "0", true);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(9));
    send_input_key(qts, "0", false);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(9));

    qtest_writel(qts, SAM9X7_PIOC_BASE + PIO_PER, leds);
    qtest_writel(qts, SAM9X7_PIOC_BASE + PIO_CODR, leds);
    qtest_writel(qts, SAM9X7_PIOC_BASE + PIO_OER, leds);
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-red",
                                "intensity-percent"), ==, 0);
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-blue",
                                "intensity-percent"), ==, 0);
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-green",
                                "intensity-percent"), ==, 0);

    qtest_writel(qts, SAM9X7_PIOC_BASE + PIO_SODR, BIT(14) | BIT(21));
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-red",
                                "intensity-percent"), ==, 100);
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-blue",
                                "intensity-percent"), ==, 0);
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-green",
                                "intensity-percent"), ==, 100);

    qtest_writel(qts, SAM9X7_PIOC_BASE + PIO_CODR, BIT(14));
    qtest_writel(qts, SAM9X7_PIOC_BASE + PIO_SODR, BIT(20));
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-red",
                                "intensity-percent"), ==, 0);
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-blue",
                                "intensity-percent"), ==, 100);
    g_assert_cmpint(qom_get_int(qts, "/machine/rgb-led-green",
                                "intensity-percent"), ==, 100);

    qtest_quit(qts);
}

static void test_board_wakeup_start_reset_and_pmic_modes(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -global at91-shdwc.request-system-shutdown=off");
    uint32_t value;
    unsigned int reg;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    twi6_enable_master(qts);
    for (reg = 0x05; reg <= 0x09; reg++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, reg), ==, 0x07);
    }

    /* LPM high followed by SHDN low selects the AB hibernate settings. */
    qtest_set_irq_in(qts, "/machine/mcp16502", "lpm", 0, 1);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR,
                 SHDWC_WUIR_WKUPEN0);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_MR,
                 SHDWC_MR_WKUPDBC(1));
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR,
                 SHDWC_CR_KEY | SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x06), ==, 0x07);
    for (reg = 0x07; reg <= 0x0a; reg++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, reg), ==, 0);
    }

    /* START passes through nSTRT/nSTRTO and wakes the real WKUP0 input. */
    send_input_key(qts, "s", true);
    qtest_clock_step(qts, 3 * SHDWC_SLCK_CYCLE_NS);
    value = qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR);
    g_assert_cmphex(value, ==, SHDWC_SR_WKUPS | SHDWC_SR_WKUPIS0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x06), ==, 0x07);
    for (reg = 0x07; reg <= 0x0a; reg++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, reg), ==, 0);
    }
    qtest_clock_step(qts, 22 * 1000000LL);
    qtest_qmp_eventwait(qts, "RESET");
    twi6_enable_master(qts);
    for (reg = 0x05; reg <= 0x09; reg++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, reg), ==, 0x07);
    }
    send_input_key(qts, "s", false);

    /* The dedicated WKUP switch reaches the same active-low WKUP0 net. */
    qtest_set_irq_in(qts, "/machine/mcp16502", "lpm", 0, 0);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR,
                 SHDWC_CR_KEY | SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);
    send_input_key(qts, "w", true);
    qtest_clock_step(qts, 3 * SHDWC_SLCK_CYCLE_NS);
    value = qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR);
    g_assert_cmphex(value, ==, SHDWC_SR_WKUPS | SHDWC_SR_WKUPIS0);
    send_input_key(qts, "w", false);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);
    qtest_clock_step(qts, 31 * 1000000LL);
    qtest_qmp_eventwait(qts, "RESET");
    twi6_enable_master(qts);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);

    /* HPM is ignored until HPMPEN is set, then selects register x3. */
    twi6_write_reg(qts, 0x5b, 0x13, 0x77);
    qtest_set_irq_in(qts, "/machine/mcp16502", "hpm", 0, 1);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);
    twi6_write_reg(qts, 0x5b, 0x03, 0xe0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);
    qtest_set_irq_in(qts, "/machine/mcp16502", "hpm", 0, 0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);

    /* RESET reaches the RSTC's active-low NRST input and records URSTS. */
    qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    send_input_key(qts, "r", true);
    qtest_clock_step(qts, SHDWC_SLCK_CYCLE_NS + 1);
    qtest_qmp_eventwait(qts, "RESET");
    send_input_key(qts, "r", false);
    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & (RSTC_SR_URSTS | RSTC_SR_NRSTL), ==,
                    RSTC_SR_URSTS | RSTC_SR_NRSTL);

    qtest_quit(qts);
}

static void test_board_power_reset_domains(void)
{
    const uint32_t gpbr_marker = 0x9a750001;
    const uint32_t bsc_config = 5;
    const uint32_t slow_clock_config = SCKC_CR_TD_OSCSEL |
                                       SCKC_CR_OSC32EN | 1;
    const uint32_t shdwc_mode = SHDWC_MR_WKUPDBC(1);
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -global at91-shdwc.request-system-shutdown=off");
    uint32_t value;

    qtest_irq_intercept_in(qts, "/machine/soc");
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    twi6_enable_master(qts);

    /* Seed one external, four backup-domain and two core-domain blocks. */
    twi6_write_reg(qts, 0x5b, 0x13, 0x77);
    qtest_writel(qts, SAM9X7_BSC_BASE + BSC_CR,
                 BSC_CR_WPKEY | bsc_config);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(0), gpbr_marker);
    qtest_writel(qts, SAM9X7_SCKC_BASE + SCKC_CR, slow_clock_config);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_MR, shdwc_mode);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR,
                 SHDWC_WUIR_WKUPEN0);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_OER, BIT(0));
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_OSR), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR), ==,
                    0x1cf1f3ff);

    /* SHDN makes the PMIC assert board NRST before removing core rails. */
    qtest_set_irq_in(qts, "/machine/mcp16502", "lpm", 0, 1);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR,
                 SHDWC_CR_KEY | SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_true(qtest_get_irq(qts, SOC_RESET_POWER_IRQ));

    /* START wakes WKUP0; nRSTO releases only after rail sequencing. */
    send_input_key(qts, "s", true);
    qtest_clock_step(qts, 3 * SHDWC_SLCK_CYCLE_NS);
    qtest_clock_step(qts, 22 * 1000000LL);
    qtest_qmp_eventwait(qts, "RESET");
    send_input_key(qts, "s", false);
    g_assert_false(qtest_get_irq(qts, SOC_RESET_POWER_IRQ));

    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_BACKUP));
    g_assert_true(value & RSTC_SR_URSTS);
    g_assert_true(value & RSTC_SR_NRSTL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_OSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR), ==,
                    0x00207024);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==,
                    bsc_config);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(0)), ==,
                    gpbr_marker);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SCKC_BASE + SCKC_CR), ==,
                    slow_clock_config);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_MR), ==,
                    shdwc_mode);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR), ==,
                    SHDWC_WUIR_WKUPEN0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR), ==,
                    SHDWC_SR_WKUPS | SHDWC_SR_WKUPIS0);
    twi6_enable_master(qts);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x13), ==, 0x77);

    /* PROCRST uses the same core domain without disturbing backup power. */
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_OER, BIT(0));
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_CR,
                 RSTC_KEY | RSTC_CR_PROCRST);
    qtest_qmp_eventwait(qts, "RESET");

    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_SOFTWARE));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_OSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR), ==,
                    0x00207024);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==,
                    bsc_config);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(0)), ==,
                    gpbr_marker);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SCKC_BASE + SCKC_CR), ==,
                    slow_clock_config);
    twi6_enable_master(qts);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x13), ==, 0x77);

    /* WDT reset also targets VDDCORE and records its distinct cause. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_OER, BIT(0));
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_WLR, 0);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_PERIODRST);
    qtest_clock_step(qts, 4 * 1000000LL);
    qtest_qmp_eventwait(qts, "WATCHDOG");
    qtest_qmp_eventwait(qts, "RESET");

    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_WATCHDOG));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_OSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR), ==,
                    0x00207024);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==,
                    bsc_config);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(0)), ==,
                    gpbr_marker);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SCKC_BASE + SCKC_CR), ==,
                    slow_clock_config);
    twi6_enable_master(qts);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x13), ==, 0x77);

    qtest_quit(qts);
}

static void test_mcp16502_push_button_timeouts(void)
{
    const int64_t short_push_timeout = 2 * RTC_SECOND_NS;
    const int64_t short_interrupt_timeout = RTC_SECOND_NS / 10;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -global mcp16502-ab.request-system-shutdown=off");
    int64_t displaced_timeout;
    int64_t deadline;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR,
                 PMC_PCR_CMD | PMC_PCR_EN | 2);
    twi6_enable_master(qts);
    qtest_irq_intercept_in(qts, "/machine/soc");

    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));

    /* PBTO=00 is 2 seconds; PBINTTO=00 is 100 milliseconds. */
    twi6_write_reg(qts, 0x5b, 0x02, 0x04);
    send_input_key(qts, "s", true);
    deadline = qtest_clock_step(qts, 1) - 1 + short_push_timeout;
    g_assert_cmphex(twi6_read_reg_before_deadline(qts, 0x5b, 0x04,
                                                  deadline), ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));

    qtest_clock_step(qts, 1);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));
    g_assert_false(qtest_get_irq(qts, 0));

    /* Reading PBINT deasserts nINTO, cancels t9 and restarts t8. */
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x04), ==, BIT(5));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));
    send_input_key(qts, "s", false);
    qtest_clock_step(qts, short_push_timeout + short_interrupt_timeout);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x04), ==, 0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);

    /* If PBINT is not serviced within t9, all rails enter OFF state. */
    send_input_key(qts, "s", true);
    deadline = qtest_clock_step(qts, short_push_timeout) +
               short_interrupt_timeout;
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));
    g_assert_cmphex(twi6_read_reg_before_deadline(qts, 0x5b, 0x05,
                                                  deadline), ==, 0x07);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x04), ==, BIT(5));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));
    send_input_key(qts, "s", false);

    /* A new START must survive t1; release before it leaves the PMIC off. */
    send_input_key(qts, "s", true);
    deadline = qtest_clock_step(qts, 1) - 1 + 600000;
    g_assert_cmphex(twi6_read_reg_before_deadline(qts, 0x5b, 0x05,
                                                  deadline), ==, 0);
    send_input_key(qts, "s", false);
    qtest_clock_step(qts, 31 * 1000000LL);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);

    send_input_key(qts, "s", true);
    qtest_clock_step(qts, 600000);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 1);
    send_input_key(qts, "s", false);
    qtest_clock_step(qts, 30 * 1000000LL);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_qmp_eventwait(qts, "RESET");
    twi6_enable_master(qts);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);

    /* FSD=10 slows the timing oscillator by 16.5 percent. */
    qtest_system_reset(qts);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR,
                 PMC_PCR_CMD | PMC_PCR_EN | 2);
    twi6_enable_master(qts);
    twi6_write_reg(qts, 0x5b, 0x02, 0x04);
    twi6_write_reg(qts, 0x5b, 0x03, 0xc8);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x03), ==, 0xc8);
    displaced_timeout = short_push_timeout * 10000 / 8350;
    send_input_key(qts, "s", true);
    qtest_clock_step(qts, displaced_timeout - 1);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));
    qtest_clock_step(qts, 1);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & BIT(12));
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x04), ==, BIT(5));
    send_input_key(qts, "s", false);

    qtest_quit(qts);
}

static void test_mcp16502_push_button_shutdown_request(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    QDict *event;
    QDict *data;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    twi6_enable_master(qts);
    twi6_write_reg(qts, 0x5b, 0x02, 0x04);
    send_input_key(qts, "s", true);
    qtest_clock_step(qts, 2 * RTC_SECOND_NS + RTC_SECOND_NS / 10);

    event = qtest_qmp_eventwait_ref(qts, "SHUTDOWN");
    data = qdict_get_qdict(event, "data");
    g_assert_nonnull(data);
    g_assert_true(qdict_get_bool(data, "guest"));
    g_assert_cmpstr(qdict_get_str(data, "reason"), ==, "guest-shutdown");
    qobject_unref(event);

    qtest_quit(qts);
}

static void test_mcp16502_startup_sequence(void)
{
    const int64_t wake_time = 100000;
    const int64_t half_ms = 500000;
    const int64_t one_ms = 1000000;
    const int64_t out1_soft_start = 528000;
    const int64_t out2_slow_soft_start = 864000;
    const int64_t out34_soft_start = 368000;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    int64_t sequence_start;
    int64_t deadline;
    unsigned int reg;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    twi6_enable_master(qts);
    qtest_irq_intercept_in(qts, "/machine/soc");

    /* Build a two-rail step 1 with distinct delay and soft-start rates. */
    qtest_set_irq_in(qts, "/machine/mcp16502", "pwrhld", 0, 0);
    g_assert_true(qtest_get_irq(qts, 0));
    for (reg = 0x05; reg <= 0x0a; reg++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, reg), ==, 0);
    }
    twi6_write_reg(qts, 0x5b, 0x02, 0x50); /* RSTDLY=1 ms. */
    twi6_write_reg(qts, 0x5b, 0x14, 0x08); /* OUT1: t=0, SSR=8 us. */
    twi6_write_reg(qts, 0x5b, 0x24, 0x4a); /* OUT2: t=1 ms, SSR=16 us. */
    twi6_write_reg(qts, 0x5b, 0x34, 0x24);
    twi6_write_reg(qts, 0x5b, 0x44, 0x24);
    twi6_write_reg(qts, 0x5b, 0x54, 0x01);
    twi6_write_reg(qts, 0x5b, 0x64, 0x01);

    qtest_set_irq_in(qts, "/machine/mcp16502", "pwrhld", 0, 1);
    sequence_start = qtest_clock_step(qts, 1) - 1;
    deadline = sequence_start + wake_time;
    g_assert_cmphex(twi6_read_reg_before_deadline(qts, 0x5b, 0x05,
                                                  deadline), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 1);
    qtest_clock_step_to(qts, sequence_start + wake_time + out1_soft_start);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);

    deadline = sequence_start + wake_time + one_ms;
    g_assert_cmphex(twi6_read_reg_before_deadline(qts, 0x5b, 0x06,
                                                  deadline), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x06), ==, 1);
    qtest_clock_step_to(qts, deadline + out2_slow_soft_start);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x06), ==, 0x07);
    deadline += out2_slow_soft_start + one_ms;
    qtest_clock_step_to(qts, deadline - 1);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 1);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_qmp_eventwait(qts, "RESET");
    twi6_enable_master(qts);

    /* SEQEN=0 rails adopt their Active register only after nRSTO release. */
    for (reg = 0x05; reg <= 0x09; reg++) {
        g_assert_cmphex(twi6_read_reg(qts, 0x5b, reg), ==, 0x07);
    }
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x0a), ==, 0);

    /* The AB hibernate rail stays up and is skipped by the next sequence. */
    qtest_system_reset(qts);
    twi6_enable_master(qts);
    qtest_set_irq_in(qts, "/machine/mcp16502", "lpm", 0, 1);
    qtest_set_irq_in(qts, "/machine/mcp16502", "pwrhld", 0, 0);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x06), ==, 0x07);

    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x06), ==, 0x07);
    qtest_set_irq_in(qts, "/machine/mcp16502", "pwrhld", 0, 1);
    sequence_start = qtest_clock_step(qts, 1) - 1;
    deadline = sequence_start + half_ms;
    g_assert_cmphex(twi6_read_reg_before_deadline(qts, 0x5b, 0x05,
                                                  deadline), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 1);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x09), ==, 1);
    qtest_clock_step_to(qts, deadline + out1_soft_start);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x05), ==, 0x07);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x09), ==, 0x07);

    deadline += out1_soft_start + 4 * one_ms;
    g_assert_cmphex(twi6_read_reg_before_deadline(qts, 0x5b, 0x07,
                                                  deadline), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x07), ==, 1);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x08), ==, 1);
    qtest_clock_step_to(qts, deadline + out34_soft_start);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x07), ==, 0x07);
    g_assert_cmphex(twi6_read_reg(qts, 0x5b, 0x08), ==, 0x07);
    deadline += out34_soft_start + 16 * one_ms;
    qtest_clock_step_to(qts, deadline - 1);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 1);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_qmp_eventwait(qts, "RESET");

    qtest_quit(qts);
}

#define PAC1934_I2C_ADDRESS         0x10
#define PAC1934_REFRESH_DELAY_NS    1000000

static uint64_t pac1934_read_be(QTestState *qts, uint8_t reg, size_t length)
{
    uint8_t data[6];
    uint64_t value = 0;
    size_t i;

    g_assert_cmpuint(length, <=, sizeof(data));
    twi_read_regs(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, reg,
                  data, length);
    for (i = 0; i < length; i++) {
        value = (value << 8) | data[i];
    }
    return value;
}

static void pac1934_refresh(QTestState *qts, uint8_t command)
{
    twi_send_byte(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, command);
    qtest_clock_step(qts, PAC1934_REFRESH_DELAY_NS);
}

static void test_pac1934_register_protocol_and_board_wiring(void)
{
    static const uint8_t ids[] = { 0x5b, 0x5d, 0x03 };
    static const uint8_t initial_status[] = {
        0x00, 0x00, 0x95, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x5b, 0x5d, 0x03,
    };
    uint8_t data[76];
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    int64_t deadline;
    uint32_t status;
    size_t i;

    twi_enable_master(qts, SAM9X7_FLEXCOM7_BASE, SAM9X7_TWI7_BASE, 10);

    twi_read_regs(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0xfd,
                  data, 3);
    g_assert_cmpmem(data, 3, ids, sizeof(ids));

    twi_read_regs(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1c,
                  data, sizeof(initial_status));
    g_assert_cmpmem(data, sizeof(initial_status), initial_status,
                    sizeof(initial_status));

    twi_read_regs(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x02,
                  data, sizeof(data));
    for (i = 0; i < sizeof(data); i++) {
        g_assert_cmphex(data[i], ==, 0);
    }

    g_assert_true(qtest_readl(qts, SAM9X7_PIOB_BASE + PIO_PDSR) & BIT(18));

    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x01, 0xff);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1c, 0xfa);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1d, 0xa5);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x20, 0xff);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x01), ==, 0xfe);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x1c), ==, 0xfa);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x1d), ==, 0xa5);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x20), ==, 0x9f);

    /* Match the Linux driver's individual configuration writes. */
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1c, 0x40);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1d, 0);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x20, 0);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x01, 0x40);
    twi_send_byte(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x00);
    deadline = qtest_clock_step(qts, 1) - 1 + PAC1934_REFRESH_DELAY_NS;
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x21), ==, 0);
    g_assert_cmphex(twi_read_reg_before_deadline(qts, SAM9X7_TWI7_BASE,
                                                 PAC1934_I2C_ADDRESS, 0x21,
                                                 deadline), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x21), ==, 0x40);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x22), ==, 0x40);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x24), ==, 0);

    /* A second snapshot associates the results with CH2 disabled. */
    pac1934_refresh(qts, 0x00);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x24), ==, 0x40);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x25), ==, 0x40);

    /* CH2 is skipped; the following result is CH3, then CH4. */
    twi_read_regs(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x07,
                  data, 6);
    g_assert_cmphex(((uint16_t)data[0] << 8) | data[1], ==,
                    3300U * 65536 / 32000);
    g_assert_cmphex(((uint16_t)data[2] << 8) | data[3], ==,
                    1150U * 65536 / 32000);
    g_assert_cmphex(((uint16_t)data[4] << 8) | data[5], ==,
                    1350U * 65536 / 32000);

    /* NO_SKIP returns FF for a disabled channel. */
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1c, 0x42);
    twi_read_regs(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x08,
                  data, 2);
    g_assert_cmphex(data[0], ==, 0xff);
    g_assert_cmphex(data[1], ==, 0xff);

    /* SMBus mode prefixes a block with the addressed register's width. */
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1c, 0x46);
    twi_read_regs(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x07,
                  data, 3);
    g_assert_cmphex(data[0], ==, 2);
    g_assert_cmphex(((uint16_t)data[1] << 8) | data[2], ==,
                    3300U * 65536 / 32000);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x1c, 0x42);

    /* REFRESH_V activates pending settings but does not rewrite LAT. */
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x01, 0xc0);
    pac1934_refresh(qts, 0x1f);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x21), ==, 0xc0);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x24), ==, 0x40);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x25), ==, 0x40);

    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_MMR,
                 TWI_MMR_DADR(PAC1934_I2C_ADDRESS) | TWI_MMR_MREAD |
                 TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_IADR, 0x1b);
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_CR,
                 TWI_CR_START | TWI_CR_STOP);
    status = qtest_readl(qts, SAM9X7_TWI7_BASE + TWI_SR);
    g_assert_true(status & TWI_SR_NACK);

    qtest_system_reset(qts);
    twi_enable_master(qts, SAM9X7_FLEXCOM7_BASE, SAM9X7_TWI7_BASE, 10);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x20), ==, 0x95);
    g_assert_cmphex(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                 PAC1934_I2C_ADDRESS, 0x21), ==, 0);

    qtest_quit(qts);
}

static void test_pac1934_measurements_accumulation_and_modes(void)
{
    const uint64_t power = 3300ULL * 1000 * (1U << 28) /
                           (32000ULL * 100000);
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE " -global pac1934.vsense1-microvolts=1000");

    twi_enable_master(qts, SAM9X7_FLEXCOM7_BASE, SAM9X7_TWI7_BASE, 10);

    pac1934_refresh(qts, 0x00);
    g_assert_cmphex(pac1934_read_be(qts, 0x07, 2), ==,
                    3300ULL * 65536 / 32000);
    g_assert_cmphex(pac1934_read_be(qts, 0x0b, 2), ==,
                    1000ULL * 65536 / 100000);
    g_assert_cmphex(pac1934_read_be(qts, 0x17, 4), ==, power << 4);

    qtest_clock_step(qts, 999 * PAC1934_REFRESH_DELAY_NS);
    pac1934_refresh(qts, 0x00);
    g_assert_cmphex(pac1934_read_be(qts, 0x02, 3), ==, 1024);
    g_assert_cmphex(pac1934_read_be(qts, 0x03, 6), ==, power * 1024);

    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x01, 0xc0);
    pac1934_refresh(qts, 0x00);
    qtest_clock_step(qts, 999 * PAC1934_REFRESH_DELAY_NS);
    pac1934_refresh(qts, 0x00);
    g_assert_cmphex(pac1934_read_be(qts, 0x02, 3), ==, 8);
    g_assert_cmphex(pac1934_read_be(qts, 0x03, 6), ==, power * 8);

    /* SLEEP stops conversion; SINGLE contributes exactly one sample. */
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x01, 0xe0);
    pac1934_refresh(qts, 0x00);
    qtest_clock_step(qts, 10ULL * 1000 * PAC1934_REFRESH_DELAY_NS);
    pac1934_refresh(qts, 0x00);
    g_assert_cmphex(pac1934_read_be(qts, 0x02, 3), ==, 0);

    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x01, 0xd0);
    pac1934_refresh(qts, 0x00);
    pac1934_refresh(qts, 0x00);
    g_assert_cmphex(pac1934_read_be(qts, 0x02, 3), ==, 1);
    g_assert_cmphex(pac1934_read_be(qts, 0x03, 6), ==, power);

    qtest_quit(qts);
}

static void test_pac1934_overflow_alert_and_clear(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -global pac1934.vsense1-microvolts=100000");

    qtest_qmp_assert_success(qts,
        "{'execute':'qom-set','arguments':{'path':'/machine/pac1934',"
        "'property':'vbus1-millivolts','value':32000}}");
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR,
                 PMC_PCR_CMD | PMC_PCR_EN | 3);
    twi_enable_master(qts, SAM9X7_FLEXCOM7_BASE, SAM9X7_TWI7_BASE, 10);
    twi_write_reg(qts, SAM9X7_TWI7_BASE, PAC1934_I2C_ADDRESS, 0x01,
                  BIT(3) | BIT(1));
    pac1934_refresh(qts, 0x00);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOB_BASE + PIO_PDSR) & BIT(18));

    qtest_clock_step(qts, 1025ULL * 1000 * PAC1934_REFRESH_DELAY_NS);
    g_assert_true(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                               PAC1934_I2C_ADDRESS, 0x01) & BIT(0));
    g_assert_false(qtest_readl(qts, SAM9X7_PIOB_BASE + PIO_PDSR) & BIT(18));

    pac1934_refresh(qts, 0x00);
    g_assert_false(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                                PAC1934_I2C_ADDRESS, 0x01) & BIT(0));
    g_assert_true(twi_read_reg(qts, SAM9X7_TWI7_BASE,
                               PAC1934_I2C_ADDRESS, 0x24) & BIT(0));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOB_BASE + PIO_PDSR) & BIT(18));

    qtest_quit(qts);
}

static void test_pac1934_i2c_jumpers(void)
{
    static const char * const routes[] = { "usb", "off" };
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(routes); i++) {
        QTestState *qts = qtest_initf(
            SAM9X75_MACHINE ",pac1934-route=%s", routes[i]);
        uint32_t status;

        twi_enable_master(qts, SAM9X7_FLEXCOM7_BASE, SAM9X7_TWI7_BASE, 10);
        qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_MMR,
                     TWI_MMR_DADR(PAC1934_I2C_ADDRESS) | TWI_MMR_MREAD |
                     TWI_MMR_IADRSZ_1);
        qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_IADR, 0xfd);
        qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_CR,
                     TWI_CR_START | TWI_CR_STOP);
        status = qtest_readl(qts, SAM9X7_TWI7_BASE + TWI_SR);
        g_assert_true(status & TWI_SR_NACK);

        qtest_quit(qts);
    }
}

static void test_flexcom_usart_registers_irq_and_protection(void)
{
    const uint64_t base = SAM9X7_USART3_BASE;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR), ==,
                    FLEX_MODE_USART);
    g_assert_cmphex(qtest_readl(qts, base + US_MR), ==, 0xc0000000);
    g_assert_cmphex(qtest_readl(qts, base + US_CSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_FIDI), ==, 0x174);
    g_assert_cmphex(qtest_readl(qts, base + US_MAN), ==, 0xb0011004);
    g_assert_cmphex(qtest_readl(qts, base + US_FMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_FLR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_NAME), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_VERSION), ==, 0);

    qtest_writel(qts, base + US_MR, UINT32_MAX);
    qtest_writel(qts, base + US_BRGR, UINT32_MAX);
    qtest_writel(qts, base + US_RTOR, UINT32_MAX);
    qtest_writel(qts, base + US_TTGR, UINT32_MAX);
    qtest_writel(qts, base + US_FIDI, UINT32_MAX);
    qtest_writel(qts, base + US_IF, UINT32_MAX);
    qtest_writel(qts, base + US_MAN, UINT32_MAX);
    qtest_writel(qts, base + US_LINMR, UINT32_MAX);
    qtest_writel(qts, base + US_LINIR, UINT32_MAX);
    qtest_writel(qts, base + US_LONMR, UINT32_MAX);
    qtest_writel(qts, base + US_LONPR, UINT32_MAX);
    qtest_writel(qts, base + US_LONDL, UINT32_MAX);
    qtest_writel(qts, base + US_LONL2HDR, UINT32_MAX);
    qtest_writel(qts, base + US_LONB1TX, UINT32_MAX);
    qtest_writel(qts, base + US_LONB1RX, UINT32_MAX);
    qtest_writel(qts, base + US_LONPRIO, UINT32_MAX);
    qtest_writel(qts, base + US_IDTTX, UINT32_MAX);
    qtest_writel(qts, base + US_IDTRX, UINT32_MAX);
    qtest_writel(qts, base + US_ICDIFF, UINT32_MAX);
    qtest_writel(qts, base + US_CMPR, UINT32_MAX);
    qtest_writel(qts, base + US_FMR, UINT32_MAX);
    qtest_writel(qts, base + US_FIER, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + US_MR), ==, 0xf7ffffff);
    g_assert_cmphex(qtest_readl(qts, base + US_BRGR), ==, 0x0007ffff);
    g_assert_cmphex(qtest_readl(qts, base + US_RTOR), ==, 0x0001ffff);
    g_assert_cmphex(qtest_readl(qts, base + US_TTGR), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, base + US_FIDI), ==, 0xffff);
    g_assert_cmphex(qtest_readl(qts, base + US_IF), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, base + US_MAN), ==, 0xf30f130f);
    g_assert_cmphex(qtest_readl(qts, base + US_LINMR), ==, 0x0003ffff);
    g_assert_cmphex(qtest_readl(qts, base + US_LINIR), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, base + US_LINBRR), ==, 0x0007ffff);
    g_assert_cmphex(qtest_readl(qts, base + US_LONMR), ==, 0x00ff003f);
    g_assert_cmphex(qtest_readl(qts, base + US_LONPR), ==, 0x00003fff);
    g_assert_cmphex(qtest_readl(qts, base + US_LONDL), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, base + US_LONL2HDR), ==, 0xff);
    g_assert_cmphex(qtest_readl(qts, base + US_LONBL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_LONB1TX), ==, 0x00ffffff);
    g_assert_cmphex(qtest_readl(qts, base + US_LONB1RX), ==, 0x00ffffff);
    g_assert_cmphex(qtest_readl(qts, base + US_LONPRIO), ==, 0x00007f7f);
    g_assert_cmphex(qtest_readl(qts, base + US_IDTTX), ==, 0x00ffffff);
    g_assert_cmphex(qtest_readl(qts, base + US_IDTRX), ==, 0x00ffffff);
    g_assert_cmphex(qtest_readl(qts, base + US_ICDIFF), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, base + US_CMPR), ==, 0x01ff71ff);
    g_assert_cmphex(qtest_readl(qts, base + US_FMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_FIMR), ==, 0);

    qtest_writel(qts, base + US_CR, US_CR_FIFOEN);
    g_assert_cmphex(qtest_readl(qts, base + US_FMR), ==, 0x3f3f3fb3);
    g_assert_cmphex(qtest_readl(qts, base + US_FIMR), ==, 0x000002ff);
    qtest_writel(qts, base + US_FIDR, 0x155);
    g_assert_cmphex(qtest_readl(qts, base + US_FIMR), ==, 0x000002aa);

    qtest_system_reset(qts);
    qtest_writel(qts, base + US_WPMR,
                 US_WPMR_KEY | US_WPMR_WPEN | US_WPMR_WPITEN |
                 US_WPMR_WPCREN);
    g_assert_cmphex(qtest_readl(qts, base + US_WPMR), ==, 7);
    qtest_writel(qts, base + US_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_WPMR), ==, 7);

    qtest_writel(qts, base + US_MR, US_MR_NORMAL_LOCAL);
    g_assert_cmphex(qtest_readl(qts, base + US_MR), ==, 0xc0000000);
    g_assert_cmphex(qtest_readl(qts, base + US_WPSR), ==,
                    (0x204U << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, base + US_WPSR), ==, 0);
    qtest_writel(qts, base + US_IER, US_INT_TXRDY);
    g_assert_cmphex(qtest_readl(qts, base + US_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_WPSR), ==,
                    (0x208U << 8) | 1);
    qtest_writel(qts, base + US_CR, US_CR_TXEN);
    g_assert_cmphex(qtest_readl(qts, base + US_CSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_WPSR), ==,
                    (0x200U << 8) | 1);
    qtest_writel(qts, base + US_WPMR, US_WPMR_KEY);

    pmc_write_pcr(qts, 8, PMC_PCR_EN);
    aic_configure(qts, 8, AIC_SMR_LEVEL_HIGH | 3, 0x80080008);
    qtest_writel(qts, base + US_MR, US_MR_NORMAL_LOCAL);
    qtest_writel(qts, base + US_BRGR, 217);
    qtest_writel(qts, base + US_CR, US_CR_RXEN | US_CR_TXEN);
    qtest_writel(qts, base + US_IER, US_INT_TXRDY);
    status = qtest_readl(qts, base + US_CSR);
    g_assert_cmphex(status & (US_INT_TXRDY | US_INT_TXEMPTY), ==,
                    US_INT_TXRDY | US_INT_TXEMPTY);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));

    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_TWI);
    g_assert_cmphex(qtest_readl(qts, base + US_MR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));
    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_USART);
    g_assert_cmphex(qtest_readl(qts, base + US_MR), ==,
                    US_MR_NORMAL_LOCAL);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));
    qtest_writel(qts, base + US_IDR, US_INT_TXRDY);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));

    qtest_quit(qts);
}

static void test_flexcom_usart_fifo_loopback_and_timeout(void)
{
    const uint64_t base = SAM9X7_USART6_BASE;
    const uint32_t fifo_mode = US_FMR_TXRDYM_FOUR |
                               US_FMR_RXRDYM_FOUR |
                               US_FMR_TXFTHRES(1) |
                               US_FMR_RXFTHRES(4) |
                               US_FMR_RXFTHRES2(2);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t character_ns;
    uint64_t timeout_ns;
    uint32_t events;
    unsigned int i;

    pmc_write_pcr(qts, 9, PMC_PCR_EN);
    qtest_writel(qts, base + US_MR, US_MR_NORMAL_LOCAL);
    qtest_writel(qts, base + US_BRGR, 217);
    qtest_writel(qts, base + US_CR,
                 US_CR_FIFOEN | US_CR_RXEN | US_CR_TXEN);
    qtest_writel(qts, base + US_FMR, fifo_mode);
    character_ns = usart_cycles_to_ns(qts, 9, 16 * 217 * 10);
    g_assert_cmpuint(character_ns, >, 1);

    qtest_writew(qts, SAM9X7_FLEXCOM6_BASE + FLEX_THR, 0x2211);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_FLEXCOM6_BASE + FLEX_THR), ==,
                    0x2211);
    for (i = 0; i < 2; i++) {
        qtest_clock_step(qts, character_ns);
    }
    g_assert_cmphex(qtest_readl(qts, base + US_FLR), ==, 2U << 16);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_FLEXCOM6_BASE + FLEX_RHR), ==,
                    0x2211);

    qtest_writel(qts, base + US_CR, US_CR_RSTSTA);
    qtest_writel(qts, base + US_THR, 0x44332211);
    g_assert_cmphex(qtest_readl(qts, base + US_FLR), ==, 3);
    for (i = 0; i < 4; i++) {
        qtest_clock_step(qts, character_ns);
    }
    g_assert_cmphex(qtest_readl(qts, base + US_FLR), ==, 4U << 16);
    g_assert_cmphex(qtest_readl(qts, base + US_CSR) &
                    (US_INT_RXRDY | US_INT_TXRDY | US_INT_TXEMPTY), ==,
                    US_INT_RXRDY | US_INT_TXRDY | US_INT_TXEMPTY);
    events = qtest_readl(qts, base + US_FESR);
    g_assert_cmphex(events & (US_FIFO_INT_TXFEF | US_FIFO_INT_TXFTHF |
                             US_FIFO_INT_RXFTHF), ==,
                    US_FIFO_INT_TXFEF | US_FIFO_INT_TXFTHF |
                    US_FIFO_INT_RXFTHF);
    g_assert_cmphex(qtest_readl(qts, base + US_RHR), ==, 0x44332211);
    events = qtest_readl(qts, base + US_FESR);
    g_assert_cmphex(events & (US_FIFO_INT_RXFEF | US_FIFO_INT_RXFTHF2), ==,
                    US_FIFO_INT_RXFEF | US_FIFO_INT_RXFTHF2);
    qtest_readb(qts, base + US_RHR);
    g_assert_true(qtest_readl(qts, base + US_FESR) &
                  US_FIFO_INT_RXFPTEF);
    qtest_writel(qts, base + US_CR, US_CR_RSTSTA);
    g_assert_cmphex(qtest_readl(qts, base + US_FESR), ==, 0);

    qtest_writel(qts, base + US_BRGR, 0);
    for (i = 0; i < 4; i++) {
        qtest_writel(qts, base + US_THR, 0x04030201);
    }
    g_assert_cmphex(qtest_readl(qts, base + US_FLR), ==, 16);
    qtest_writeb(qts, base + US_THR, 0xaa);
    events = qtest_readl(qts, base + US_FESR);
    g_assert_cmphex(events & (US_FIFO_INT_TXFFF | US_FIFO_INT_TXFPTEF |
                             US_FIFO_STATUS_TXFLOCK), ==,
                    US_FIFO_INT_TXFFF | US_FIFO_INT_TXFPTEF |
                    US_FIFO_STATUS_TXFLOCK);
    qtest_writel(qts, base + US_CR, US_CR_TXFLCLR);
    g_assert_false(qtest_readl(qts, base + US_FESR) &
                   US_FIFO_STATUS_TXFLOCK);
    qtest_writel(qts, base + US_CR, US_CR_TXFCLR);
    qtest_writel(qts, base + US_CR, US_CR_RSTSTA);
    g_assert_cmphex(qtest_readl(qts, base + US_FESR), ==, 0);

    qtest_writel(qts, base + US_BRGR, 217);
    qtest_writel(qts, base + US_RTOR, 3);
    qtest_writel(qts, base + US_CR, US_CR_RETTO);
    timeout_ns = usart_cycles_to_ns(qts, 9, 16 * 217 * 3);
    g_assert_cmpuint(timeout_ns, >, 1);
    qtest_clock_step(qts, timeout_ns - 1);
    g_assert_false(qtest_readl(qts, base + US_CSR) & US_INT_TIMEOUT);
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, base + US_CSR) & US_INT_TIMEOUT);

    qtest_writel(qts, base + US_CR, US_CR_STTTO);
    qtest_clock_step(qts, timeout_ns);
    g_assert_false(qtest_readl(qts, base + US_CSR) & US_INT_TIMEOUT);
    qtest_writeb(qts, base + US_THR, 0xa5);
    qtest_clock_step(qts, character_ns);
    g_assert_cmphex(qtest_readb(qts, base + US_RHR), ==, 0xa5);
    qtest_clock_step(qts, timeout_ns);
    g_assert_true(qtest_readl(qts, base + US_CSR) & US_INT_TIMEOUT);

    qtest_writel(qts, base + US_CR, US_CR_FIFODIS);
    g_assert_cmphex(qtest_readl(qts, base + US_FMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + US_FLR), ==, 0);
    qtest_quit(qts);
}

static void test_flexcom_usart_chardev(void)
{
    QTestState *qts;
    uint64_t character_ns;
    uint8_t value;
    int sock_fd;

    qts = qtest_init_with_flexcom0_serial(&sock_fd);
    pmc_write_pcr(qts, 5, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_USART0_BASE + US_MR,
                 US_MR_CHRL_8 | US_MR_PAR_NONE);
    qtest_writel(qts, SAM9X7_USART0_BASE + US_BRGR, 217);
    qtest_writel(qts, SAM9X7_USART0_BASE + US_CR,
                 US_CR_RXEN | US_CR_TXEN);
    character_ns = usart_cycles_to_ns(qts, 5, 16 * 217 * 10);

    value = 0xa5;
    g_assert_cmpint(send(sock_fd, &value, 1, 0), ==, 1);
    usart_wait_status(qts, SAM9X7_USART0_BASE, US_INT_RXRDY);
    g_assert_cmphex(qtest_readb(qts, SAM9X7_USART0_BASE + US_RHR), ==,
                    value);

    qtest_writeb(qts, SAM9X7_USART0_BASE + US_THR, 0x3c);
    qtest_clock_step(qts, character_ns);
    g_assert_cmpint(recv(sock_fd, &value, 1, 0), ==, 1);
    g_assert_cmphex(value, ==, 0x3c);

    close(sock_fd);
    qtest_quit(qts);
}

static void test_flexcom_usart_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint64_t character_ns;
    int64_t from_clock;

    pmc_write_pcr(from, 5, PMC_PCR_EN);
    qtest_writel(from, SAM9X7_USART0_BASE + US_MR, US_MR_NORMAL_LOCAL);
    qtest_writel(from, SAM9X7_USART0_BASE + US_BRGR, 217);
    qtest_writel(from, SAM9X7_USART0_BASE + US_FMR,
                 US_FMR_TXFTHRES(2) | US_FMR_RXFTHRES(2));
    qtest_writel(from, SAM9X7_USART0_BASE + US_IER,
                 US_INT_RXRDY | US_INT_TXEMPTY);
    qtest_writel(from, SAM9X7_USART0_BASE + US_CR,
                 US_CR_FIFOEN | US_CR_RXEN | US_CR_TXEN);
    character_ns = usart_cycles_to_ns(from, 5, 16 * 217 * 10);
    qtest_clock_step(from, character_ns * 2);
    qtest_writel(from, SAM9X7_FLEXCOM0_BASE + FLEX_THR, 0x64636261);
    qtest_writel(from, SAM9X7_USART0_BASE + US_WPMR,
                 US_WPMR_KEY | US_WPMR_WPEN | US_WPMR_WPITEN |
                 US_WPMR_WPCREN);

    from_clock = qtest_clock_step(from, character_ns / 2);
    g_assert_cmphex(qtest_readl(from, SAM9X7_USART0_BASE + US_FLR), ==, 3);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_FLEXCOM0_BASE + FLEX_MR), ==,
                    FLEX_MODE_USART);
    g_assert_cmphex(qtest_readl(to, SAM9X7_FLEXCOM0_BASE + FLEX_THR), ==,
                    0x6261);
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_MR), ==,
                    US_MR_NORMAL_LOCAL);
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_BRGR), ==, 217);
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_IMR), ==,
                    US_INT_RXRDY | US_INT_TXEMPTY);
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_FMR), ==,
                    US_FMR_TXFTHRES(2) | US_FMR_RXFTHRES(2));
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_WPMR), ==,
                    US_WPMR_WPEN | US_WPMR_WPITEN | US_WPMR_WPCREN);
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_FLR), ==, 3);

    /* qtest virtual clocks are independent; align the destination clock. */
    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);
    g_assert_false(qtest_readl(to, SAM9X7_USART0_BASE + US_CSR) &
                   US_INT_RXRDY);
    qtest_clock_step(to, character_ns - character_ns / 2 - 1);
    g_assert_false(qtest_readl(to, SAM9X7_USART0_BASE + US_CSR) &
                   US_INT_RXRDY);
    qtest_clock_step(to, 1);
    g_assert_true(qtest_readl(to, SAM9X7_USART0_BASE + US_CSR) &
                  US_INT_RXRDY);
    qtest_clock_step(to, character_ns * 3);
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_FLR), ==,
                    4U << 16);
    g_assert_cmphex(qtest_readl(to, SAM9X7_USART0_BASE + US_RHR), ==,
                    0x64636261);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_flexcom_spi_registers_irq_and_protection(void)
{
    const uint64_t base = SAM9X7_SPI3_BASE;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR), ==,
                    FLEX_MODE_USART);
    g_assert_cmphex(qtest_readl(qts, base + SPI_SR), ==, 0);
    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_SPI);
    g_assert_cmphex(qtest_readl(qts, base + SPI_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + 0xfc), ==, 0);

    qtest_writel(qts, base + SPI_MR, UINT32_MAX);
    qtest_writel(qts, base + SPI_CSR(0), UINT32_MAX);
    qtest_writel(qts, base + SPI_FMR, UINT32_MAX);
    qtest_writel(qts, base + SPI_CMPR, UINT32_MAX);
    qtest_writel(qts, base + SPI_CRCR, UINT32_MAX);
    qtest_writel(qts, base + SPI_TPMR, UINT32_MAX);
    qtest_writel(qts, base + SPI_TPHR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + SPI_MR), ==, 0xff0ff1ff);
    g_assert_cmphex(qtest_readl(qts, base + SPI_CSR(0)), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_CMPR), ==, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + SPI_CRCR), ==, 0x0ff100ff);
    g_assert_cmphex(qtest_readl(qts, base + SPI_TPMR), ==, 0xf);
    g_assert_cmphex(qtest_readl(qts, base + SPI_TPHR), ==, 0);

    qtest_writel(qts, base + SPI_CR, SPI_CR_FIFOEN);
    qtest_writel(qts, base + SPI_FMR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FMR), ==, 0x3f3f0033);

    qtest_system_reset(qts);
    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_SPI);
    qtest_writel(qts, base + SPI_WPMR,
                 SPI_WPMR_KEY | SPI_WPMR_WPEN | SPI_WPMR_WPITEN |
                 SPI_WPMR_WPCREN);
    g_assert_cmphex(qtest_readl(qts, base + SPI_WPMR), ==, 7);
    qtest_writel(qts, base + SPI_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_WPMR), ==, 7);

    qtest_writel(qts, base + SPI_MR, SPI_MR_MSTR | SPI_MR_LLB);
    g_assert_cmphex(qtest_readl(qts, base + SPI_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_WPSR), ==,
                    (SPI_MR << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, base + SPI_WPSR), ==, 0);
    qtest_writel(qts, base + SPI_IER, SPI_INT_TDRE);
    g_assert_cmphex(qtest_readl(qts, base + SPI_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_WPSR), ==,
                    (SPI_IER << 8) | 1);
    qtest_writel(qts, base + SPI_CR, SPI_CR_SPIEN);
    g_assert_cmphex(qtest_readl(qts, base + SPI_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + SPI_WPSR), ==, 1);
    qtest_writel(qts, base + SPI_WPMR, SPI_WPMR_KEY);

    pmc_write_pcr(qts, 8, PMC_PCR_EN);
    aic_configure(qts, 8, AIC_SMR_LEVEL_HIGH | 3, 0x80080008);
    qtest_writel(qts, base + SPI_MR,
                 SPI_MR_MSTR | SPI_MR_LLB | SPI_MR_PCS(0xe));
    qtest_writel(qts, base + SPI_CSR(0),
                 SPI_CSR_BITS(16) | SPI_CSR_SCBR(217));
    qtest_writel(qts, base + SPI_CR, SPI_CR_SPIEN);
    qtest_writel(qts, base + SPI_IER, SPI_INT_TDRE);
    status = qtest_readl(qts, base + SPI_SR);
    g_assert_cmphex(status & (SPI_INT_TDRE | SPI_INT_TXEMPTY |
                             SPI_STATUS_SPIENS), ==,
                    SPI_INT_TDRE | SPI_INT_TXEMPTY | SPI_STATUS_SPIENS);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));

    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_TWI);
    g_assert_cmphex(qtest_readl(qts, base + SPI_MR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));
    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_SPI);
    g_assert_cmphex(qtest_readl(qts, base + SPI_MR), ==,
                    SPI_MR_MSTR | SPI_MR_LLB | SPI_MR_PCS(0xe));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));
    qtest_writel(qts, base + SPI_IDR, SPI_INT_TDRE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(8));

    qtest_quit(qts);
}

static void test_flexcom_spi_instance_capabilities(void)
{
    static const uint8_t num_cs[] = { 2, 2, 2, 2, 4, 4 };
    static const struct {
        uint64_t flex_base;
        uint64_t spi_base;
        unsigned int pid;
        unsigned int request;
    } absent[] = {
        { SAM9X7_FLEXCOM6_BASE, SAM9X7_SPI6_BASE, 9, 12 },
        { SAM9X7_FLEXCOM12_BASE, SAM9X7_SPI12_BASE, 33, 24 },
    };
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0x1f000;
    const uint32_t rx_target = SAM9X7_DDR_BASE + 0x1f100;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    QDict *response;
    unsigned int i;

    ebi_enable_ddr(qts);
    for (i = 0; i < ARRAY_SIZE(num_cs); i++) {
        g_autofree char *path = g_strdup_printf("/machine/soc/spi[%u]", i);
        unsigned int cs;

        g_assert_cmpint(qom_get_int(qts, path, "num-cs"), ==, num_cs[i]);
        for (cs = 0; cs <= SPI_NUM_CS; cs++) {
            g_autofree char *property = g_strdup_printf("cs[%u]", cs);

            g_assert_cmpint(qom_has_property(qts, path, property), ==,
                            cs < num_cs[i]);
        }
    }
    for (i = ARRAY_SIZE(num_cs); i < 13; i++) {
        g_autofree char *path = g_strdup_printf("/machine/soc/spi[%u]", i);

        response = qtest_qmp(qts,
            "{ 'execute': 'qom-list', 'arguments': { 'path': %s } }",
            path);
        g_assert_true(qdict_haskey(response, "error"));
        qobject_unref(response);
    }

    qtest_writel(qts, tx_source, 0x12345678);
    qtest_writel(qts, rx_target, 0xa5a5a5a5);
    for (i = 0; i < ARRAY_SIZE(absent); i++) {
        uint32_t pending;

        pmc_write_pcr(qts, absent[i].pid, PMC_PCR_EN);
        qtest_writeb(qts, absent[i].flex_base + FLEX_MR, FLEX_MODE_SPI);

        /*
         * Probe only externally visible functionality here.  Wrapper-mode,
         * reserved-window and common-register readback remain real-hardware
         * questions.
         */
        qtest_writel(qts, absent[i].spi_base + SPI_MR,
                     SPI_MR_MSTR | SPI_MR_LLB | SPI_MR_PCS(0xe));
        qtest_writel(qts, absent[i].spi_base + SPI_CSR(0),
                     SPI_CSR_BITS(8) | SPI_CSR_SCBR(217));
        qtest_writel(qts, absent[i].spi_base + SPI_IER, SPI_INT_TDRE);
        qtest_writel(qts, absent[i].spi_base + SPI_CR, SPI_CR_SPIEN);
        qtest_writeb(qts, absent[i].flex_base + FLEX_THR, 0x5a);
        qtest_clock_step(qts, 10000);
        if (absent[i].pid < 32) {
            pending = qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0);
            g_assert_false(pending & BIT(absent[i].pid));
        } else {
            pending = qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1);
            g_assert_false(pending & BIT(absent[i].pid - 32));
        }

        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(0) + XDMAC_CSA,
                     tx_source);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(0) + XDMAC_CDA,
                     absent[i].flex_base + FLEX_THR);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(0) + XDMAC_CUBC,
                     1);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(0) + XDMAC_CC,
                     XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                     XDMAC_CC_DWIDTH_WORD |
                     XDMAC_CC_PERID(absent[i].request));
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));

        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(1) + XDMAC_CSA,
                     absent[i].flex_base + FLEX_RHR);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(1) + XDMAC_CDA,
                     rx_target);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(1) + XDMAC_CUBC,
                     1);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(1) + XDMAC_CC,
                     XDMAC_CC_TYPE_PER | XDMAC_CC_DWIDTH_WORD |
                     XDMAC_CC_PERID(absent[i].request + 1));
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
        qtest_clock_step(qts, 10000);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) &
                        (BIT(0) | BIT(1)), ==, BIT(0) | BIT(1));
        g_assert_cmphex(qtest_readl(qts,
                                   SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(0) +
                                   XDMAC_CUBC), ==, 1);
        g_assert_cmphex(qtest_readl(qts,
                                   SAM9X7_XDMAC_BASE + XDMAC_CHANNEL(1) +
                                   XDMAC_CUBC), ==, 1);
        g_assert_cmphex(qtest_readl(qts, rx_target), ==, 0xa5a5a5a5);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(0) | BIT(1));
    }

    qtest_quit(qts);
}

static void test_flexcom_spi_fifo_loopback_and_timing(void)
{
    const uint64_t base = SAM9X7_SPI3_BASE;
    const uint32_t fifo_mode = SPI_FMR_TXRDYM_TWO |
                               SPI_FMR_RXRDYM_TWO |
                               SPI_FMR_TXFTHRES(0) |
                               SPI_FMR_RXFTHRES(2);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t dlybs_ns;
    uint64_t transfer_ns;
    uint64_t dlybct_ns;
    uint32_t status;
    unsigned int i;

    pmc_write_pcr(qts, 8, PMC_PCR_EN);
    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_SPI);
    qtest_writel(qts, base + SPI_MR,
                 SPI_MR_MSTR | SPI_MR_LLB | SPI_MR_PCS(0xe));
    qtest_writel(qts, base + SPI_CSR(0),
                 SPI_CSR_CSAAT | SPI_CSR_BITS(16) |
                 SPI_CSR_SCBR(217) | SPI_CSR_DLYBS(11) |
                 SPI_CSR_DLYBCT(3));
    qtest_writel(qts, base + SPI_CR, SPI_CR_FIFOEN);
    qtest_writel(qts, base + SPI_FMR, fifo_mode);
    qtest_writel(qts, base + SPI_CR, SPI_CR_SPIEN);

    dlybs_ns = usart_cycles_to_ns(qts, 8, 11);
    transfer_ns = usart_cycles_to_ns(qts, 8, 16 * 217);
    dlybct_ns = usart_cycles_to_ns(qts, 8, 3 * 32);
    g_assert_cmpuint(dlybs_ns, >, 1);
    g_assert_cmpuint(transfer_ns, >, dlybs_ns);
    g_assert_cmpuint(dlybct_ns, >, 1);

    qtest_writel(qts, SAM9X7_FLEXCOM3_BASE + FLEX_THR, 0x44332211);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_FLEXCOM3_BASE + FLEX_THR), ==,
                    0x2211);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 1);
    qtest_clock_step(qts, dlybs_ns - 1);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 1);
    qtest_clock_step(qts, 1);
    qtest_clock_step(qts, transfer_ns - 1);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 1);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==,
                    1 | (1U << 16));
    qtest_clock_step(qts, dlybct_ns);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 1U << 16);
    qtest_clock_step(qts, transfer_ns);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 2U << 16);
    qtest_clock_step(qts, dlybct_ns);

    status = qtest_readl(qts, base + SPI_SR);
    g_assert_cmphex(status & (SPI_INT_RDRF | SPI_INT_TDRE |
                             SPI_INT_TXEMPTY | SPI_STATUS_SPIENS), ==,
                    SPI_INT_RDRF | SPI_INT_TDRE | SPI_INT_TXEMPTY |
                    SPI_STATUS_SPIENS);
    g_assert_cmphex(status & (SPI_INT_TXFEF | SPI_INT_TXFTHF |
                             SPI_INT_RXFTHF), ==,
                    SPI_INT_TXFEF | SPI_INT_TXFTHF | SPI_INT_RXFTHF);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_FLEXCOM3_BASE + FLEX_RHR), ==,
                    0x2211);
    g_assert_cmphex(qtest_readl(qts, base + SPI_RDR), ==, 0x000e4433);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 0);
    qtest_readw(qts, base + SPI_RDR);
    g_assert_true(qtest_readl(qts, base + SPI_SR) & SPI_INT_RXFPTEF);

    qtest_system_reset(qts);
    pmc_write_pcr(qts, 8, PMC_PCR_EN);
    qtest_writeb(qts, SAM9X7_FLEXCOM3_BASE + FLEX_MR, FLEX_MODE_SPI);
    qtest_writel(qts, base + SPI_MR,
                 SPI_MR_MSTR | SPI_MR_LLB | SPI_MR_PCS(0xe));
    qtest_writel(qts, base + SPI_CSR(0), SPI_CSR_BITS(16));
    qtest_writel(qts, base + SPI_CR, SPI_CR_FIFOEN | SPI_CR_SPIEN);
    for (i = 0; i < 8; i++) {
        qtest_writel(qts, base + SPI_TDR, 0x20012000 + i);
    }
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 16);
    qtest_writel(qts, base + SPI_TDR, 0xaaaabbbb);
    status = qtest_readl(qts, base + SPI_SR);
    g_assert_cmphex(status & (SPI_INT_TXFFF | SPI_INT_TXFPTEF), ==,
                    SPI_INT_TXFFF | SPI_INT_TXFPTEF);
    g_assert_false(status & SPI_INT_TDRE);
    qtest_writel(qts, base + SPI_CR, SPI_CR_TXFCLR);
    g_assert_cmphex(qtest_readl(qts, base + SPI_FLR), ==, 0);

    qtest_quit(qts);
}

static void test_flexcom_spi_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    const uint32_t mode = SPI_MR_MSTR | SPI_MR_LLB | SPI_MR_PCS(0xe);
    const uint32_t csr = SPI_CSR_CSAAT | SPI_CSR_BITS(16) |
                         SPI_CSR_SCBR(217) | SPI_CSR_DLYBS(11);
    uint64_t dlybs_ns;
    uint64_t transfer_ns;
    int64_t from_clock;

    pmc_write_pcr(from, 5, PMC_PCR_EN);
    qtest_writeb(from, SAM9X7_FLEXCOM0_BASE + FLEX_MR, FLEX_MODE_SPI);
    qtest_writel(from, SAM9X7_SPI0_BASE + SPI_MR, mode);
    qtest_writel(from, SAM9X7_SPI0_BASE + SPI_CSR(0), csr);
    qtest_writel(from, SAM9X7_SPI0_BASE + SPI_CR, SPI_CR_FIFOEN);
    qtest_writel(from, SAM9X7_SPI0_BASE + SPI_FMR,
                 SPI_FMR_TXFTHRES(1) | SPI_FMR_RXFTHRES(2));
    qtest_writel(from, SAM9X7_SPI0_BASE + SPI_IER,
                 SPI_INT_RDRF | SPI_INT_TXEMPTY);
    qtest_writel(from, SAM9X7_SPI0_BASE + SPI_CR, SPI_CR_SPIEN);
    qtest_writel(from, SAM9X7_FLEXCOM0_BASE + FLEX_THR, 0x64636261);
    qtest_writel(from, SAM9X7_SPI0_BASE + SPI_WPMR,
                 SPI_WPMR_KEY | SPI_WPMR_WPEN | SPI_WPMR_WPITEN |
                 SPI_WPMR_WPCREN);

    dlybs_ns = usart_cycles_to_ns(from, 5, 11);
    transfer_ns = usart_cycles_to_ns(from, 5, 16 * 217);
    from_clock = qtest_clock_step(from, dlybs_ns + transfer_ns / 2);
    g_assert_cmphex(qtest_readl(from, SAM9X7_SPI0_BASE + SPI_FLR), ==, 1);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_FLEXCOM0_BASE + FLEX_MR), ==,
                    FLEX_MODE_SPI);
    g_assert_cmphex(qtest_readl(to, SAM9X7_FLEXCOM0_BASE + FLEX_THR), ==,
                    0x6261);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_MR), ==, mode);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_CSR(0)), ==,
                    csr);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_IMR), ==,
                    SPI_INT_RDRF | SPI_INT_TXEMPTY);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_FMR), ==,
                    SPI_FMR_TXFTHRES(1) | SPI_FMR_RXFTHRES(2));
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_WPMR), ==, 7);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_FLR), ==, 1);

    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);
    qtest_clock_step(to, transfer_ns - transfer_ns / 2 - 1);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_FLR), ==, 1);
    qtest_clock_step(to, 1);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_FLR), ==,
                    1U << 16);
    qtest_clock_step(to, transfer_ns);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SPI0_BASE + SPI_FLR), ==,
                    2U << 16);
    g_assert_cmphex(qtest_readw(to, SAM9X7_SPI0_BASE + SPI_RDR), ==,
                    0x6261);
    g_assert_cmphex(qtest_readw(to, SAM9X7_SPI0_BASE + SPI_RDR), ==,
                    0x6463);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_flexcom_twi_registers_nack_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_FLEXCOM6_BASE + FLEX_MR), ==,
                    FLEX_MODE_USART);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR), ==, 0);

    qtest_writeb(qts, SAM9X7_FLEXCOM6_BASE + FLEX_MR, FLEX_MODE_TWI);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_FLEXCOM6_BASE + FLEX_MR), ==,
                    FLEX_MODE_TWI);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR), ==,
                    0x03000009);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_SMR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CWGR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_SMBTR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_HSR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_ACR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_FILTR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_HSCWGR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_FMR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_MMR), ==,
                    0x017f7300);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SMR), ==,
                    0x007f7ffd);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_IADR), ==,
                    0x00ffffff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_CWGR), ==,
                    0x7f17ffff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SMBTR), ==,
                    0xffffff0f);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_HSR), ==,
                    0x000000ff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_ACR), ==,
                    0x03ff03ff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_FILTR), ==,
                    0x00000703);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_HSCWGR), ==,
                    0x0007ffff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_FMR), ==, 0);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_FIFOEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_FMR), ==,
                    0x3f3f0033);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_FIER, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_FIMR), ==,
                    0x000000ff);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_FIDR, 0x5a);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_FIMR), ==,
                    0x000000a5);

    twi6_enable_master(qts);
    status = qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR);
    g_assert_cmphex(status, ==, TWI_SR_SDA | TWI_SR_SCL | TWI_SR_SVREAD |
                    TWI_SR_TXRDY | TWI_SR_TXCOMP);

    aic_configure(qts, 9, AIC_SMR_LEVEL_HIGH | 3, 0x90090009);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IER, TWI_SR_NACK);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_IMR), ==,
                    TWI_SR_NACK);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_MREAD |
                 TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x42);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_START | TWI_CR_STOP);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(9));
    status = qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR);
    g_assert_cmphex(status & (TWI_SR_NACK | TWI_SR_TXRDY | TWI_SR_TXCOMP),
                    ==, TWI_SR_NACK | TWI_SR_TXRDY | TWI_SR_TXCOMP);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(9));
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IDR, TWI_SR_NACK);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CWGR, 0x12345);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_WPMR,
                 TWI_WPMR_KEY | TWI_WPMR_WPEN | TWI_WPMR_WPITEN |
                 TWI_WPMR_WPCREN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_WPMR), ==, 7);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_WPMR), ==, 7);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CWGR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_CWGR), ==,
                    0x12345);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_WPSR), ==,
                    (0x610U << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_WPSR), ==, 0);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IER, TWI_SR_NACK);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_WPSR), ==,
                    (0x624U << 8) | 1);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_SWRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_WPSR), ==,
                    (0x600U << 8) | 1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_WPMR, TWI_WPMR_KEY);

    qtest_writeb(qts, SAM9X7_FLEXCOM6_BASE + FLEX_MR, FLEX_MODE_USART);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR), ==, 0);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR, 0);
    qtest_writeb(qts, SAM9X7_FLEXCOM6_BASE + FLEX_MR, FLEX_MODE_TWI);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_MMR), ==,
                    TWI_MMR_DADR(0x53) | TWI_MMR_MREAD |
                    TWI_MMR_IADRSZ_1);

    qtest_quit(qts);
}

static void test_twi_eeprom_transfers_fifo_and_access_width(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at24c-eeprom,bus=i2c6,address=0x53,rom-size=256");
    uint32_t status;

    twi6_enable_master(qts);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x20);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_THR, 0x5a);
    twi_wait_status(qts, SAM9X7_TWI6_BASE, TWI_SR_TXRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_TXRDY);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_THR, 0xa5);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_STOP);
    twi_wait_status(qts, SAM9X7_TWI6_BASE, TWI_SR_TXCOMP);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_MREAD |
                 TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x20);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_START);
    twi_wait_status(qts, SAM9X7_TWI6_BASE, TWI_SR_RXRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_RXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_RHR), ==,
                    0x5a);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_STOP);
    twi_wait_status(qts, SAM9X7_TWI6_BASE, TWI_SR_RXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_RHR), ==,
                    0xa5);
    status = qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR);
    g_assert_true(status & TWI_SR_TXCOMP);
    g_assert_false(status & TWI_SR_RXRDY);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_MSDIS | TWI_CR_SVDIS);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_FIFOEN | TWI_CR_ACMEN);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_MSEN | TWI_CR_SVDIS);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_ACR, TWI_ACR_DATAL(2));
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x40);
    qtest_writew(qts, SAM9X7_TWI6_BASE + TWI_THR, 0xbbaa);
    twi_wait_status(qts, SAM9X7_TWI6_BASE, TWI_SR_TXCOMP);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_ACR,
                 TWI_ACR_DIR | TWI_ACR_DATAL(2));
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x40);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_START);
    twi_wait_status(qts, SAM9X7_TWI6_BASE, TWI_SR_TXCOMP);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_TWI6_BASE + TWI_RHR), ==,
                    0xbbaa);
    status = qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR);
    g_assert_true(status & TWI_SR_TXCOMP);
    g_assert_false(status & TWI_SR_RXRDY);

    qtest_quit(qts);
}

static void test_twi_fifo_levels_thresholds_and_clock(void)
{
    static const uint32_t words[] = {
        0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c,
    };
    const uint32_t fifo_mode = TWI_FMR_TXRDYM_FOUR |
                               TWI_FMR_RXRDYM_FOUR |
                               TWI_FMR_TXFTHRES(12) |
                               TWI_FMR_RXFTHRES(4);
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at24c-eeprom,bus=i2c0,address=0x53,rom-size=256");
    uint64_t gclk_byte_ns;
    uint64_t gclk_period;
    uint32_t status;
    unsigned int i;

    pmc_write_pcr(qts, 5, PMC_PCR_EN);
    qtest_writeb(qts, SAM9X7_FLEXCOM0_BASE + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_CR, TWI_CR_SWRST);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_CR,
                 TWI_CR_FIFOEN | TWI_CR_ACMEN);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_CR,
                 TWI_CR_MSEN | TWI_CR_SVDIS);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_FMR, fifo_mode);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_IADR, 0x20);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_ACR,
                 TWI_ACR_DATAL(sizeof(words)));

    aic_configure(qts, 5, AIC_SMR_LEVEL_HIGH | 3, 0x50050005);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_FIER,
                 TWI_FSR_TXFFF | TWI_FSR_TXFTHF | TWI_FSR_RXFTHF);

    /* With PCLK stopped, four word writes fill all 16 FIFO entries. */
    pmc_write_pcr(qts, 5, 0);
    for (i = 0; i < ARRAY_SIZE(words); i++) {
        qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_THR, words[i]);
    }
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FLR), ==,
                    16);
    g_assert_false(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                   TWI_SR_TXRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(5));
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FSR) &
                  TWI_FSR_TXFFF);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(5));

    /* The shifter resumes at PCLK enable and crosses TXFTHRES at 12. */
    pmc_write_pcr(qts, 5, PMC_PCR_EN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FLR), ==,
                    15);
    for (i = 0; i < 3; i++) {
        qtest_clock_step_next(qts);
    }
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FLR), ==,
                    12);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                  TWI_SR_TXRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FSR) &
                  TWI_FSR_TXFTHF);

    for (i = 0; i < 32; i++) {
        if (qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) & TWI_SR_TXCOMP) {
            break;
        }
        qtest_clock_step_next(qts);
    }
    g_assert_cmpuint(i, <, 32);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FLR), ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FSR) &
                  TWI_FSR_TXFEF);

    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_ACR,
                 TWI_ACR_DIR | TWI_ACR_DATAL(sizeof(words)));
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_IADR, 0x20);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_CR,
                 TWI_CR_RXFCLR | TWI_CR_START);
    for (i = 0; i < 3; i++) {
        qtest_clock_step_next(qts);
    }
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FLR), ==,
                    3U << 16);
    g_assert_false(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                   TWI_SR_RXRDY);
    qtest_clock_step_next(qts);
    status = qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR);
    g_assert_true(status & TWI_SR_RXRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FSR) &
                  TWI_FSR_RXFTHF);

    for (i = 0; i < ARRAY_SIZE(words); i++) {
        twi_wait_status(qts, SAM9X7_TWI0_BASE, TWI_SR_RXRDY);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_RHR), ==,
                        words[i]);
    }
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FLR), ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FSR) &
                  TWI_FSR_RXFEF);

    qtest_readb(qts, SAM9X7_TWI0_BASE + TWI_RHR);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_FSR) &
                  TWI_FSR_RXFPTEF);

    pmc_write_pcr(qts, 5, PMC_PCR_EN | (2U << 8) | (2U << 20) |
                           PMC_PCR_GCKEN);
    gclk_period = get_clock_period(qts, "/machine/soc/pmc/gclk[5]");
    g_assert_cmpuint(gclk_period, >, 0);
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_CWGR,
                 TWI_CWGR_BRSRCCLK | 3 | (3U << 8));
    gclk_byte_ns = (gclk_period * 54) >> 32;
    g_assert_cmpuint(gclk_byte_ns, >, 1);

    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_ACR, TWI_ACR_DATAL(1));
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_IADR, 0x50);
    qtest_writeb(qts, SAM9X7_TWI0_BASE + TWI_THR, 0x5a);
    qtest_clock_step(qts, gclk_byte_ns - 1);
    g_assert_false(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                   TWI_SR_TXCOMP);
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);

    /* Selecting GCLK still requires both the generic and peripheral clocks. */
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_ACR, TWI_ACR_DATAL(1));
    qtest_writel(qts, SAM9X7_TWI0_BASE + TWI_IADR, 0x51);
    qtest_writeb(qts, SAM9X7_TWI0_BASE + TWI_THR, 0xa5);
    pmc_write_pcr(qts, 5, PMC_PCR_EN | (2U << 8) | (2U << 20));
    qtest_clock_step(qts, 2 * gclk_byte_ns);
    g_assert_false(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                   TWI_SR_TXCOMP);
    pmc_write_pcr(qts, 5, PMC_PCR_EN | (2U << 8) | (2U << 20) |
                           PMC_PCR_GCKEN);
    qtest_clock_step(qts, gclk_byte_ns - 1);
    g_assert_false(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                   TWI_SR_TXCOMP);
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI0_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);

    qtest_quit(qts);
}

static void test_flexcom_twi_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    const uint32_t mmr = TWI_MMR_DADR(0x5b) | TWI_MMR_IADRSZ_1;
    const uint32_t cwgr = 217 | (217U << 8);
    const uint32_t fmr = TWI_FMR_TXRDYM_FOUR |
                         TWI_FMR_RXRDYM_FOUR |
                         TWI_FMR_TXFTHRES(2) |
                         TWI_FMR_RXFTHRES(4);
    uint64_t byte_ns;
    int64_t from_clock;

    pmc_write_pcr(from, 9, PMC_PCR_EN);
    qtest_writeb(from, SAM9X7_FLEXCOM6_BASE + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_SWRST);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_FIFOEN | TWI_CR_ACMEN);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_MSEN | TWI_CR_SVDIS);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_MMR, mmr);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_CWGR, cwgr);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_FMR, fmr);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_IER,
                 TWI_SR_TXCOMP | TWI_SR_RXRDY);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_FIER,
                 TWI_FSR_TXFEF | TWI_FSR_RXFTHF);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_IADR, 0x40);
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_ACR, TWI_ACR_DATAL(4));
    qtest_writel(from, SAM9X7_TWI6_BASE + TWI_THR, 0x44332211);

    byte_ns = usart_cycles_to_ns(from, 9, 9 * (217 + 217 + 6));
    g_assert_cmpuint(byte_ns, >, 1);
    from_clock = qtest_clock_step(from, byte_ns / 2);
    g_assert_cmphex(qtest_readl(from, SAM9X7_TWI6_BASE + TWI_FLR), ==, 3);
    g_assert_false(qtest_readl(from, SAM9X7_TWI6_BASE + TWI_SR) &
                   TWI_SR_TXCOMP);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_FLEXCOM6_BASE + FLEX_MR), ==,
                    FLEX_MODE_TWI);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_MMR), ==, mmr);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_CWGR), ==,
                    cwgr);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_ACR), ==, 4);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_FMR), ==, fmr);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_IMR), ==,
                    TWI_SR_TXCOMP | TWI_SR_RXRDY);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_FIMR), ==,
                    TWI_FSR_TXFEF | TWI_FSR_RXFTHF);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_FLR), ==, 3);

    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);
    qtest_clock_step(to, byte_ns - byte_ns / 2 - 1);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_FLR), ==, 3);
    qtest_clock_step(to, 1);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_FLR), ==, 2);
    twi_wait_status(to, SAM9X7_TWI6_BASE, TWI_SR_TXCOMP);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_FLR), ==, 0);

    qtest_writel(to, SAM9X7_TWI6_BASE + TWI_ACR,
                 TWI_ACR_DIR | TWI_ACR_DATAL(4));
    qtest_writel(to, SAM9X7_TWI6_BASE + TWI_IADR, 0x40);
    qtest_writel(to, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_RXFCLR | TWI_CR_START);
    twi_wait_status(to, SAM9X7_TWI6_BASE, TWI_SR_RXRDY);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_RHR), ==,
                    0x44332211);
    g_assert_true(qtest_readl(to, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_aic_dbgu_integration(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    aic_configure(qts, 47, AIC_SMR_LEVEL_HIGH | 7, 0x47c0ffee);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IMR), ==, 1);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_IER, DBGU_INT_TXRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(15));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR), ==,
                    AIC_CISR_IRQ);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IVR), ==,
                    0x47c0ffee);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_ISR), ==, 47);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR) &
                   AIC_CISR_IRQ);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_IDR, DBGU_INT_TXRDY);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_EOICR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_ISR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(15));

    qtest_quit(qts);
}

static void test_aic_priority_and_nesting(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    aic_configure(qts, 5, AIC_SMR_EDGE_RISING | 2, 0x55555555);
    aic_configure(qts, 6, AIC_SMR_EDGE_RISING | 5, 0x66666666);

    aic_set_input(qts, 5, 1);
    aic_set_input(qts, 6, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR), ==,
                    AIC_CISR_IRQ);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IVR), ==,
                    0x66666666);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_ISR), ==, 6);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR) &
                   AIC_CISR_IRQ);

    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_EOICR, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR) &
                  AIC_CISR_IRQ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IVR), ==,
                    0x55555555);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_ISR), ==, 5);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_EOICR, 0);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR), ==, 0);

    qtest_quit(qts);
}

static void test_aic_fiq_mask_and_write_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    aic_configure(qts, 0, AIC_SMR_EDGE_RISING, 0xf1f1f1f1);
    aic_set_input(qts, 0, 0);
    aic_set_input(qts, 0, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR) &
                  AIC_CISR_FIQ);

    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_DCR, AIC_DCR_GMSK);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR), ==, 0);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_DCR, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR) &
                  AIC_CISR_FIQ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_FVR), ==,
                    0xf1f1f1f1);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_CISR) &
                   AIC_CISR_FIQ);

    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_SPU, 0x11111111);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_WPMR, AIC_WPMR_KEY | 1);
    qtest_writel(qts, SAM9X7_AIC_BASE + AIC_SPU, 0x22222222);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_SPU), ==,
                    0x11111111);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_WPSR), ==,
                    (AIC_SPU << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_WPSR), ==, 0);

    qtest_quit(qts);
}

static void test_gem_registers_mdio_dma_and_irqs(void)
{
    static const unsigned int irq_source[GEM_NUM_QUEUES] = {
        24, 60, 61, 62, 63, 64,
    };
    const uint32_t descriptor_base = SAM9X7_DDR_BASE + 0x1000;
    const uint32_t packet_base = SAM9X7_DDR_BASE + 0x2000;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE " -nic user,mac=02:00:00:09:75:01");
    unsigned int q;

    ebi_enable_ddr(qts);
    g_assert_true(qtest_qom_get_bool(qts, "/machine", "ethernet-25mhz"));
    g_assert_true(qtest_qom_get_bool(qts, "/machine/soc/gmac",
                                     "phy-clocked"));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_MODID), ==,
                    0x00020118);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_DESCONF6) &
                    (BIT(23) | 0x7f), ==, BIT(23) | 0x3e);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_SPADDR1LO), ==,
                    0x09000002);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_SPADDR1HI), ==,
                    0x00000175);

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL, GEM_NWCTRL_MPE);
    g_assert_cmphex(gem_mdio_read(qts, 1, 2), ==, 0x0022);
    g_assert_cmphex(gem_mdio_read(qts, 1, 3), ==, 0x1650);
    g_assert_cmphex(gem_mdio_read(qts, 0, 2), ==, 0xffff);

    for (q = 0; q < GEM_NUM_QUEUES; q++) {
        uint32_t descriptor = descriptor_base + q * 0x100;
        uint32_t packet = packet_base + q * 0x100;

        aic_configure(qts, irq_source[q], AIC_SMR_LEVEL_HIGH | 7,
                      0x90000000 | q);
        qtest_memset(qts, packet, 0xff, 64);
        qtest_writel(qts, descriptor, packet);
        qtest_writel(qts, descriptor + 4,
                     GEM_TX_DESC_WRAP | GEM_TX_DESC_LAST | 64);

        if (q == 0) {
            qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXQBASE, descriptor);
            qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_IER,
                         GEM_INT_XMIT_COMPLETE);
        } else {
            qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TRANSMIT_Q1_PTR +
                         (q - 1) * 4, descriptor);
            qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_INT_Q1_ENABLE +
                         (q - 1) * 4, GEM_INT_XMIT_COMPLETE);
        }
    }

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL,
                 GEM_NWCTRL_MPE | GEM_NWCTRL_TXEN | GEM_NWCTRL_TSTART);

    for (q = 0; q < GEM_NUM_QUEUES; q++) {
        g_assert_true(qtest_readl(qts, descriptor_base + q * 0x100 + 4) &
                      GEM_TX_DESC_USED);
    }
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(24));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) &
                    (BIT(28) | BIT(29) | BIT(30) | BIT(31)), ==,
                    BIT(28) | BIT(29) | BIT(30) | BIT(31));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(0));

    g_assert_true(qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_ISR) &
                  GEM_INT_XMIT_COMPLETE);
    for (q = 1; q < GEM_NUM_QUEUES; q++) {
        g_assert_true(qtest_readl(qts, SAM9X7_GMAC_BASE +
                      GEM_INT_Q1_STATUS + (q - 1) * 4) &
                      GEM_INT_XMIT_COMPLETE);
    }
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(24));
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) &
                   (BIT(28) | BIT(29) | BIT(30) | BIT(31)));
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(0));

    qtest_quit(qts);
}

static void gem_transmit_test_frame(QTestState *qts,
                                    const uint8_t destination[6],
                                    uint32_t nwctrl)
{
    const uint32_t descriptor = SAM9X7_SRAM0_BASE + 0x3000;
    const uint32_t packet_addr = SAM9X7_SRAM0_BASE + 0x4000;
    uint8_t packet[64] = {
        [6] = 0x02,
        [11] = 0x01,
        [12] = 0x86,
        [13] = 0xdd,
    };

    memcpy(packet, destination, 6);
    qtest_memwrite(qts, packet_addr, packet, sizeof(packet));
    qtest_writel(qts, descriptor, packet_addr);
    qtest_writel(qts, descriptor + 4,
                 GEM_TX_DESC_WRAP | GEM_TX_DESC_LAST | sizeof(packet));
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXQBASE, descriptor);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL,
                 nwctrl | GEM_NWCTRL_TXEN | GEM_NWCTRL_TSTART);
    g_assert_true(qtest_readl(qts, descriptor + 4) & GEM_TX_DESC_USED);
}

static void gem_prepare_test_receive(QTestState *qts)
{
    const uint32_t descriptor = SAM9X7_SRAM0_BASE + 0x5000;
    const uint32_t packet_addr = SAM9X7_SRAM0_BASE + 0x6000;

    qtest_writel(qts, descriptor, packet_addr | GEM_RX_DESC_WRAP);
    qtest_writel(qts, descriptor + 4, 0);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_RXQBASE, descriptor);
}

static void gem_assert_test_receive(QTestState *qts)
{
    const uint32_t descriptor = SAM9X7_SRAM0_BASE + 0x5000;

    g_assert_true(qtest_readl(qts, descriptor) & GEM_RX_DESC_OWNERSHIP);
    g_assert_cmphex(qtest_readl(qts, descriptor + 4) & 0x1fff, ==, 64);
}

static void gem_assert_stat_rtc(QTestState *qts, uint32_t offset,
                                uint32_t expected)
{
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + offset), ==,
                    expected);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + offset), ==, 0);
}

static void test_gem_statistics_generated_and_clear(void)
{
    static const uint8_t ipv6_multicast[6] = {
        0x33, 0x33, 0x00, 0x00, 0x00, 0x01,
    };
    static const uint8_t broadcast[6] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    static const uint8_t unicast[6] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x02,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t nwcfg;

    nwcfg = qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_NWCFG);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCFG,
                 nwcfg | GEM_NWCFG_PROMISC | GEM_NWCFG_FCS_REMOVE);

    gem_prepare_test_receive(qts);
    gem_transmit_test_frame(qts, ipv6_multicast,
                            GEM_NWCTRL_LBL | GEM_NWCTRL_RXEN);
    gem_assert_test_receive(qts);
    gem_assert_stat_rtc(qts, GEM_OCTTXLO, 64);
    gem_assert_stat_rtc(qts, GEM_OCTTXHI, 0);
    gem_assert_stat_rtc(qts, GEM_TXCNT, 1);
    gem_assert_stat_rtc(qts, GEM_TXBCNT, 0);
    gem_assert_stat_rtc(qts, GEM_TXMCNT, 1);
    gem_assert_stat_rtc(qts, GEM_TX64CNT, 1);
    gem_assert_stat_rtc(qts, GEM_OCTRXLO, 64);
    gem_assert_stat_rtc(qts, GEM_OCTRXHI, 0);
    gem_assert_stat_rtc(qts, GEM_RXCNT, 1);
    gem_assert_stat_rtc(qts, GEM_RXBROADCNT, 0);
    gem_assert_stat_rtc(qts, GEM_RXMULTICNT, 1);
    gem_assert_stat_rtc(qts, GEM_RX64CNT, 1);

    gem_prepare_test_receive(qts);
    gem_transmit_test_frame(qts, broadcast,
                            GEM_NWCTRL_LBL | GEM_NWCTRL_RXEN);
    gem_assert_test_receive(qts);
    gem_assert_stat_rtc(qts, GEM_OCTTXLO, 64);
    gem_assert_stat_rtc(qts, GEM_OCTTXHI, 0);
    gem_assert_stat_rtc(qts, GEM_TXCNT, 1);
    gem_assert_stat_rtc(qts, GEM_TXBCNT, 1);
    gem_assert_stat_rtc(qts, GEM_TXMCNT, 0);
    gem_assert_stat_rtc(qts, GEM_TX64CNT, 1);
    gem_assert_stat_rtc(qts, GEM_OCTRXLO, 64);
    gem_assert_stat_rtc(qts, GEM_OCTRXHI, 0);
    gem_assert_stat_rtc(qts, GEM_RXCNT, 1);
    gem_assert_stat_rtc(qts, GEM_RXBROADCNT, 1);
    gem_assert_stat_rtc(qts, GEM_RXMULTICNT, 0);
    gem_assert_stat_rtc(qts, GEM_RX64CNT, 1);

    /* CLRSTAT takes effect before a simultaneous TSTART command. */
    gem_transmit_test_frame(qts, broadcast, 0);
    gem_transmit_test_frame(qts, unicast, GEM_NWCTRL_CLRSTAT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL) &
                    (GEM_NWCTRL_TXEN | GEM_NWCTRL_CLRSTAT), ==,
                    GEM_NWCTRL_TXEN);
    gem_assert_stat_rtc(qts, GEM_OCTTXLO, 64);
    gem_assert_stat_rtc(qts, GEM_OCTTXHI, 0);
    gem_assert_stat_rtc(qts, GEM_TXCNT, 1);
    gem_assert_stat_rtc(qts, GEM_TXBCNT, 0);
    gem_assert_stat_rtc(qts, GEM_TXMCNT, 0);
    gem_assert_stat_rtc(qts, GEM_TX64CNT, 1);

    qtest_quit(qts);
}

static void test_gem_statistics_test_controls(void)
{
    static const uint8_t unicast[6] = {
        0x02, 0x00, 0x00, 0x00, 0x00, 0x03,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t offset;

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL, GEM_NWCTRL_WESTAT);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXCNT, 7);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL, 0);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXCNT, 9);
    gem_assert_stat_rtc(qts, GEM_TXCNT, 7);

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL, GEM_NWCTRL_WESTAT);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_OCTTXHI, UINT32_MAX);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXURUNCNT, UINT32_MAX);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_SINGLECOLLCNT, UINT32_MAX);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_RXUDPCCNT, UINT32_MAX);
    gem_assert_stat_rtc(qts, GEM_OCTTXHI, UINT16_MAX);
    gem_assert_stat_rtc(qts, GEM_TXURUNCNT, 0x3ff);
    gem_assert_stat_rtc(qts, GEM_SINGLECOLLCNT, 0x3ffff);
    gem_assert_stat_rtc(qts, GEM_RXUDPCCNT, UINT8_MAX);

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL,
                 GEM_NWCTRL_WESTAT | GEM_NWCTRL_CLRSTAT);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL,
                 GEM_NWCTRL_WESTAT | GEM_NWCTRL_INCSTAT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL) &
                    (GEM_NWCTRL_WESTAT | GEM_NWCTRL_INCSTAT), ==,
                    GEM_NWCTRL_WESTAT);
    for (offset = GEM_STAT_FIRST; offset <= GEM_STAT_LAST; offset += 4) {
        gem_assert_stat_rtc(qts, offset, 1);
    }

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXCNT, UINT32_MAX);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_OCTTXHI, UINT16_MAX);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXURUNCNT, 0x3ff);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_SINGLECOLLCNT, 0x3ffff);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_RXUDPCCNT, UINT8_MAX);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL,
                 GEM_NWCTRL_WESTAT | GEM_NWCTRL_INCSTAT);
    gem_assert_stat_rtc(qts, GEM_TXCNT, UINT32_MAX);
    gem_assert_stat_rtc(qts, GEM_OCTTXHI, UINT16_MAX);
    gem_assert_stat_rtc(qts, GEM_TXURUNCNT, 0x3ff);
    gem_assert_stat_rtc(qts, GEM_SINGLECOLLCNT, 0x3ffff);
    gem_assert_stat_rtc(qts, GEM_RXUDPCCNT, UINT8_MAX);

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_OCTTXLO,
                 UINT32_MAX - 31);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_OCTTXHI, UINT16_MAX);
    gem_transmit_test_frame(qts, unicast, GEM_NWCTRL_WESTAT);
    gem_assert_stat_rtc(qts, GEM_OCTTXLO, UINT32_MAX);
    gem_assert_stat_rtc(qts, GEM_OCTTXHI, UINT16_MAX);

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL, GEM_NWCTRL_WESTAT);
    for (offset = GEM_STAT_FIRST; offset <= GEM_STAT_LAST; offset += 4) {
        qtest_writel(qts, SAM9X7_GMAC_BASE + offset, 5);
    }
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL,
                 GEM_NWCTRL_WESTAT | GEM_NWCTRL_CLRSTAT);
    for (offset = GEM_STAT_FIRST; offset <= GEM_STAT_LAST; offset += 4) {
        gem_assert_stat_rtc(qts, offset, 0);
    }

    qtest_quit(qts);
}

static void test_gem_statistics_migration(void)
{
    static const uint8_t multicast[6] = {
        0x01, 0x00, 0x5e, 0x00, 0x00, 0x01,
    };
    const char *source_env = g_getenv("QTEST_QEMU_BINARY_OLD") ?
                             "QTEST_QEMU_BINARY_OLD" :
                             "QTEST_QEMU_BINARY";
    QTestState *from = qtest_init_ext(source_env, SAM9X75_MACHINE,
                                      NULL, true);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    qtest_irq_intercept_out_named(from, "/machine/soc/gmac", "sysbus-irq");
    qtest_irq_intercept_out_named(to, "/machine/soc/gmac", "sysbus-irq");
    qtest_writel(from, SAM9X7_GMAC_BASE + GEM_IER,
                 GEM_INT_XMIT_COMPLETE);
    gem_transmit_test_frame(from, multicast, 0);
    g_assert_true(qtest_get_irq(from, 0));
    qtest_writel(from, SAM9X7_GMAC_BASE + GEM_NWCTRL, GEM_NWCTRL_WESTAT);
    qtest_writel(from, SAM9X7_GMAC_BASE + GEM_RXUDPCCNT, 7);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_true(qtest_get_irq(to, 0));
    gem_assert_stat_rtc(to, GEM_OCTTXLO, 64);
    gem_assert_stat_rtc(to, GEM_OCTTXHI, 0);
    gem_assert_stat_rtc(to, GEM_TXCNT, 1);
    gem_assert_stat_rtc(to, GEM_TXBCNT, 0);
    gem_assert_stat_rtc(to, GEM_TXMCNT, 1);
    gem_assert_stat_rtc(to, GEM_TX64CNT, 1);
    gem_assert_stat_rtc(to, GEM_RXUDPCCNT, 7);
    g_assert_true(qtest_readl(to, SAM9X7_GMAC_BASE + GEM_ISR) &
                  GEM_INT_XMIT_COMPLETE);
    g_assert_false(qtest_get_irq(to, 0));

    qtest_quit(from);
    qtest_quit(to);
}

static void test_board_ethernet_clock_jumper(void)
{
    const uint32_t descriptor = SAM9X7_DDR_BASE + 0x1000;
    const uint32_t packet = SAM9X7_DDR_BASE + 0x2000;
    QTestState *qts;
#ifndef _WIN32
    uint8_t received[68];
    int sockets[2];
    int ret;

    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sockets);
    g_assert_cmpint(ret, !=, -1);
    qts = qtest_initf(
        SAM9X75_MACHINE ",ethernet-25mhz=off"
        " -nic socket,fd=%d,mac=02:00:00:09:75:01", sockets[1]);
#else
    qts = qtest_init(SAM9X75_MACHINE ",ethernet-25mhz=off"
                     " -nic user,mac=02:00:00:09:75:01");
#endif

    ebi_enable_ddr(qts);
    g_assert_false(qtest_qom_get_bool(qts, "/machine",
                                      "ethernet-25mhz"));
    g_assert_false(qtest_qom_get_bool(qts, "/machine/soc/gmac",
                                      "phy-clocked"));

    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL, GEM_NWCTRL_MPE);
    g_assert_cmphex(gem_mdio_read(qts, 1, 2), ==, 0xffff);
    g_assert_cmphex(gem_mdio_read(qts, 1, 3), ==, 0xffff);

    /* The MAC completes DMA, but the unclocked PHY emits no frame. */
    qtest_memset(qts, packet, 0xff, 64);
    qtest_writel(qts, descriptor, packet);
    qtest_writel(qts, descriptor + 4,
                 GEM_TX_DESC_WRAP | GEM_TX_DESC_LAST | 64);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_TXQBASE, descriptor);
    qtest_writel(qts, SAM9X7_GMAC_BASE + GEM_NWCTRL,
                 GEM_NWCTRL_MPE | GEM_NWCTRL_TXEN | GEM_NWCTRL_TSTART);
    g_assert_true(qtest_readl(qts, descriptor + 4) & GEM_TX_DESC_USED);
#ifndef _WIN32
    errno = 0;
    ret = recv(sockets[0], received, sizeof(received), MSG_DONTWAIT);
    g_assert_cmpint(ret, ==, -1);
    g_assert_true(errno == EAGAIN || errno == EWOULDBLOCK);
#endif

    qtest_quit(qts);
#ifndef _WIN32
    close(sockets[0]);
    close(sockets[1]);
#endif
}

static void pmc_configure_pll(QTestState *qts, unsigned int id)
{
    uint32_t selector = id | (0x3fU << 16);

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT, selector);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_ACR, 0x00020010);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL1,
                 (65U << 24) | 0x002aaaab);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT,
                 selector | PMC_PLL_UPDT_UPDATE);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL0,
                 PMC_PLL_CTRL0_ENLOCK | PMC_PLL_CTRL0_ENPLL |
                 PMC_PLL_CTRL0_ENPLLCK);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT,
                 selector | PMC_PLL_UPDT_UPDATE);
}

static void pmc_configure_audio_pll(QTestState *qts)
{
    const uint32_t selector = 2 | (0x3fU << 16);

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT, selector);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_ACR, 0x00020010);
    /* 24 MHz * (3 + 1 + 0.096) = 98.304 MHz. */
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL1,
                 (3U << 24) | 0x624dd);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT,
                 selector | PMC_PLL_UPDT_UPDATE);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL0,
                 PMC_PLL_CTRL0_ENLOCK | PMC_PLL_CTRL0_ENPLL |
                 PMC_PLL_CTRL0_ENPLLCK);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT,
                 selector | PMC_PLL_UPDT_UPDATE);
}

static void pmc_write_pcr(QTestState *qts, unsigned int id, uint32_t config)
{
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR,
                 PMC_PCR_CMD | id | config);
}

static void pmc_set_upll_acr(QTestState *qts, uint32_t acr)
{
    const uint32_t selector = 1 | (0x3fU << 16);

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT, selector);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_ACR, acr);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT,
                 selector | PMC_PLL_UPDT_UPDATE);
}

static void pmc_enable_upll(QTestState *qts)
{
    const uint32_t selector = 1 | (0x3fU << 16);

    /* 24 MHz * (39 + 1), followed by UPLL's fixed /2 = 480 MHz. */
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT, selector);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_ACR,
                 PMC_UPLL_ACR_CONFIG);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL1, 39U << 24);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT,
                 selector | PMC_PLL_UPDT_UPDATE);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL0,
                 PMC_PLL_CTRL0_ENLOCK | PMC_PLL_CTRL0_ENPLL |
                 PMC_PLL_CTRL0_ENPLLCK);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT,
                 selector | PMC_PLL_UPDT_UPDATE);
}

static void pmc_configure_usb_host(QTestState *qts)
{
    pmc_write_pcr(qts, 22, PMC_PCR_EN);
    pmc_enable_upll(qts);

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_USB,
                 PMC_USB_USBS_UPLL | PMC_USB_USBDIV(9));
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_SCER, PMC_SC_UHP);
}

static void test_pmc_clock_tree_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MOR), ==,
                    PMC_MOR_MOSCRCEN | PMC_MOR_ALWAYS_ONE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MCKR), ==, 1);
    value = qtest_readl(qts, SAM9X7_PMC_BASE + PMC_SR);
    g_assert_cmphex(value & (PMC_SR_MCKRDY | PMC_SR_MOSCRCS), ==,
                    PMC_SR_MCKRDY | PMC_SR_MOSCRCS);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/mainck"), ==,
                     CLOCK_PERIOD_FROM_HZ(12000000));
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/mck"), ==,
                     CLOCK_PERIOD_FROM_HZ(12000000));
    value = qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MCFR);
    g_assert_true(value & PMC_MCFR_MAINRDY);
    g_assert_false(value & PMC_MCFR_RCMEAS);
    g_assert_cmphex(value & 0xffff, ==,
                    (12000000ULL * 16 / 32000) & 0xffff);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_PLL_ACR), ==,
                    PMC_PLL_ACR_RESET);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL0, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_PLL_CTRL0), ==,
                    PMC_PLL_CTRL0_MASK);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_ACR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_PLL_ACR), ==,
                    PMC_PLL_ACR_MASK);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT), ==,
                    PMC_PLL_UPDT_MASK);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PLL_UPDT, 0);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_USB, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_USB), ==,
                    0x00000f03);

    /* Vendor U-Boot probes legacy locations that SAM9X7 reserves. */
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_RESERVED_LEGACY_PCR, 47);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE +
                                PMC_RESERVED_LEGACY_PCR), ==, 0);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_RESERVED_LEGACY_MCKR,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE +
                                PMC_RESERVED_LEGACY_MCKR), ==, 0);

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MOR,
                 PMC_MOR_KEY | PMC_MOR_MOSCRCEN | PMC_MOR_MOSCXTEN);
    g_assert_true(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_SR) &
                  PMC_SR_MOSCXTS);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MOR,
                 PMC_MOR_KEY | PMC_MOR_MOSCRCEN | PMC_MOR_MOSCXTEN |
                 PMC_MOR_MOSCSEL);
    value = qtest_readl(qts, SAM9X7_PMC_BASE + PMC_SR);
    g_assert_cmphex(value & (PMC_SR_MOSCXTS | PMC_SR_MOSCSELS), ==,
                    PMC_SR_MOSCXTS | PMC_SR_MOSCSELS);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/mainck"), ==,
                     CLOCK_PERIOD_FROM_HZ(24000000));

    /* The last completed RC measurement remains valid after RC shutdown. */
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MOR,
                 PMC_MOR_KEY | PMC_MOR_MOSCXTEN | PMC_MOR_MOSCSEL);
    value = qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MCFR);
    g_assert_true(value & PMC_MCFR_MAINRDY);
    g_assert_cmphex(value & 0xffff, ==,
                    (12000000ULL * 16 / 32000) & 0xffff);

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MCFR,
                 PMC_MCFR_RCMEAS | PMC_MCFR_CCSS);
    value = qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MCFR);
    g_assert_true(value & PMC_MCFR_MAINRDY);
    g_assert_false(value & PMC_MCFR_RCMEAS);
    g_assert_true(value & PMC_MCFR_CCSS);
    g_assert_cmphex(value & 0xffff, ==,
                    (24000000ULL * 16 / 32000) & 0xffff);

    pmc_configure_pll(qts, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_PLL_ISR0) & BIT(0));
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MCKR, 2 | (4U << 8));
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/mck"), ==,
                     CLOCK_PERIOD_FROM_HZ(160000000));
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MCKR, 2 | (3U << 8));
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/mck"), ==,
                     CLOCK_PERIOD_FROM_HZ(266666666));

    /* PID-only writes select PCR and the selector is included in readback. */
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR, 12);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_PCR), ==, 12);

    pmc_write_pcr(qts, 37, PMC_PCR_EN | (2U << 8) | (2U << 20) |
                           PMC_PCR_GCKEN);
    g_assert_true(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_CSR1) & BIT(5));
    g_assert_true(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_GCSR1) & BIT(5));
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/pclk[37]"), ==,
                     CLOCK_PERIOD_FROM_HZ(266666666));
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/gclk[37]"), ==,
                     CLOCK_PERIOD_FROM_HZ(8000000));

    pmc_configure_usb_host(qts);
    g_assert_true(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_SCSR) &
                  PMC_SC_UHP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_USB), ==,
                    PMC_USB_USBS_UPLL | PMC_USB_USBDIV(9));
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/utmi"), ==,
                     CLOCK_PERIOD_FROM_HZ(480000000));
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/uhpck"), ==,
                     CLOCK_PERIOD_FROM_HZ(48000000));
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_SCDR, PMC_SC_UHP);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/uhpck"), ==,
                     0);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/utmi"), ==,
                     CLOCK_PERIOD_FROM_HZ(480000000));
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_SCER, PMC_SC_UHP);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/uhpck"), ==,
                     CLOCK_PERIOD_FROM_HZ(48000000));

    aic_configure(qts, 1, AIC_SMR_LEVEL_HIGH | 1, 0x01010101);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_IER, PMC_SR_MCKRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_IDR, PMC_SR_MCKRDY);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_WPMR,
                 PMC_WPMR_KEY | PMC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MCKR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MCKR), ==,
                    2 | (3U << 8));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_WPSR), ==,
                    (PMC_MCKR << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_WPSR), ==, 0);

    qtest_quit(qts);
}

static void pio_set_input(QTestState *qts, unsigned int bank,
                          unsigned int pin, int level)
{
    static const char * const paths[] = {
        "/machine/soc/pio[0]",
        "/machine/soc/pio[1]",
        "/machine/soc/pio[2]",
        "/machine/soc/pio[3]",
    };

    g_assert_cmpuint(bank, <, G_N_ELEMENTS(paths));
    qtest_set_irq_in(qts, paths[bank], "unnamed-gpio-in", pin, level);
}

static void test_pio_reset_gpio_mux_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    const uint32_t pin = BIT(7);

    /* SAM9X75 SiP package reset states from DS60001827. */
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PSR), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PUSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PPDSR), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR), ==,
                    UINT32_MAX);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOB_BASE + PIO_PSR), ==,
                    0x07ffffff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PSR), ==,
                    0xfdffffff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PUSR), ==,
                    BIT(25));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PPDSR), ==,
                    0xfdffffff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOD_BASE + PIO_PSR), ==,
                    0x00007ff3);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOD_BASE + PIO_PUSR), ==,
                    BIT(2) | BIT(3));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOD_BASE + PIO_PPDSR), ==,
                    0x00007ff3);

    /* Package-invalid lines consistently ignore writes and read as zero. */
    qtest_writel(qts, SAM9X7_PIOB_BASE + PIO_PDR, BIT(31));
    qtest_writel(qts, SAM9X7_PIOB_BASE + PIO_SODR, BIT(31));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOB_BASE + PIO_PSR), ==,
                    0x07ffffff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOB_BASE + PIO_ODSR), ==, 0);

    pmc_write_pcr(qts, 2, PMC_PCR_EN);
    qtest_irq_intercept_out(qts, "/machine/soc/pio[0]");

    /* Peripheral D selection, followed by returning the pin to GPIO. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_ABCDSR0, BIT(1));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_ABCDSR1, BIT(1));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PDR, BIT(1));
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PSR) & BIT(1));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ABCDSR0) & BIT(1));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ABCDSR1) & BIT(1));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PER, BIT(1));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PSR) & BIT(1));

    /* Push-pull, open-drain release, pulls, and externally driven input. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PUDR, pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_CODR, pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_OER, pin);
    g_assert_false(qtest_get_irq(qts, 7));
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_SODR, pin);
    g_assert_true(qtest_get_irq(qts, 7));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & pin);

    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_MDER, pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_MDSR) & pin);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PUER, pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & pin);
    pio_set_input(qts, 0, 7, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & pin);
    pio_set_input(qts, 0, 7, -1);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & pin);

    /* Direct ODSR writes affect only lines enabled in OWSR. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_CODR, pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_ODSR, pin);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ODSR) & pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_OWER, pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_OWSR) & pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_ODSR, pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ODSR) & pin);

    /* Hardware rejects simultaneous pull-up and pull-down enables. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PPDER, BIT(8));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PPDSR) & BIT(8));

    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PDR, BIT(1));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_WPMR,
                 PIO_WPMR_KEY | PIO_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PER, BIT(1));
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PSR) & BIT(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_WPSR), ==,
                    (PIO_PER << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_WPSR), ==, 0);

    /* Electrical controls are not among PIO3's protected registers. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_SCHMITT, BIT(9));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_SLEWR, BIT(10));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_DRIVER, BIT(11));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_SCHMITT), ==,
                    BIT(9));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_SLEWR), ==,
                    BIT(10));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_DRIVER), ==,
                    BIT(11));

    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_WPMR, 0);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PER, BIT(1));
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PSR) & BIT(1));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_WPMR, PIO_WPMR_KEY);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_PER, BIT(1));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PSR) & BIT(1));

    qtest_quit(qts);
}

static void test_pio_interrupt_filter_and_clock_gating(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    const uint32_t irq_pin = BIT(28);
    const uint32_t gate_pin = BIT(27);
    const uint32_t filter_pin = BIT(29);
    uint64_t pclk_period_ns;

    pmc_write_pcr(qts, 2, PMC_PCR_EN);
    pclk_period_ns = (get_clock_period(qts,
                                      "/machine/soc/pmc/pclk[2]") +
                      UINT32_MAX) >> 32;
    g_assert_cmpuint(pclk_period_ns, >, 1);
    aic_configure(qts, 2, AIC_SMR_LEVEL_HIGH | 4, 0x02020202);

    pio_set_input(qts, 0, 28, 1);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_IER, irq_pin);
    pio_set_input(qts, 0, 28, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(2));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR), ==,
                    irq_pin);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(2));

    /* Default mode detects both edges. */
    pio_set_input(qts, 0, 28, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR), ==,
                    irq_pin);

    /* Additional edge mode selects only the requested polarity. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_AIMER, irq_pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_ESR, irq_pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_REHLSR, irq_pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_AIMMR) & irq_pin);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ELSR) & irq_pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_FRLHSR) & irq_pin);
    pio_set_input(qts, 0, 28, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR) & irq_pin);
    pio_set_input(qts, 0, 28, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR), ==,
                    irq_pin);

    /* A level source reasserts after ISR reads until the pin changes. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_LSR, irq_pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_FELLSR, irq_pin);
    pio_set_input(qts, 0, 28, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(2));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR), ==,
                    irq_pin);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(2));
    pio_set_input(qts, 0, 28, 1);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(2));
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_AIMDR, irq_pin);

    /* PDSR and interrupt sampling freeze while the peripheral clock is off. */
    pio_set_input(qts, 0, 27, 1);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_IER, gate_pin);
    pmc_write_pcr(qts, 2, 0);
    pio_set_input(qts, 0, 27, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & gate_pin);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(2));
    pmc_write_pcr(qts, 2, PMC_PCR_EN);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) & gate_pin);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(2));
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR) & gate_pin);

    /* A pulse shorter than one peripheral-clock cycle is filtered. */
    pio_set_input(qts, 0, 29, 1);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_IER, filter_pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_IFER, filter_pin);
    pio_set_input(qts, 0, 29, 0);
    qtest_clock_step(qts, pclk_period_ns - 1);
    pio_set_input(qts, 0, 29, 1);
    qtest_clock_step(qts, 2 * pclk_period_ns);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) &
                  filter_pin);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR) &
                   filter_pin);

    pio_set_input(qts, 0, 29, 0);
    qtest_clock_step(qts, 2 * pclk_period_ns);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) &
                   filter_pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR) &
                  filter_pin);

    /* DIV=1 gives a 122.07 us divided-slow-clock debounce period. */
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_IFSCER, filter_pin);
    qtest_writel(qts, SAM9X7_PIOA_BASE + PIO_SCDR, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_IFSCSR) &
                  filter_pin);
    pio_set_input(qts, 0, 29, 1);
    qtest_clock_step(qts, 50000);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) &
                   filter_pin);
    qtest_clock_step(qts, 100000);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) &
                  filter_pin);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR) &
                  filter_pin);

    qtest_quit(qts);
}

static void test_pit64b_timing_gating_and_irq(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_MR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B1_BASE + PIT64B_MR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_TLSBR), ==,
                    0);

    aic_configure(qts, 37, AIC_SMR_LEVEL_HIGH | 3, 0x37373737);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_CR, PIT64B_CR_SWRST);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_MSB_PR, 0);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_LSB_PR, 4);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_IER,
                 PIT64B_INT_PERIOD);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_CR, PIT64B_CR_START);

    /* The peripheral clock is reset-gated, so a started timer is frozen. */
    qtest_clock_step(qts, 500000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_ISR), ==,
                    0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(5));

    pmc_write_pcr(qts, 37, PMC_PCR_EN);
    qtest_clock_step(qts, 200000);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(5));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_ISR), ==,
                    PIT64B_INT_PERIOD);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(5));

    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_CR, PIT64B_CR_SWRST);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_MR, PIT64B_MR_CONT);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_MSB_PR, 0);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_LSB_PR, 2);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_IER,
                 PIT64B_INT_PERIOD | PIT64B_INT_OVRE);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_CR, PIT64B_CR_START);
    qtest_clock_step(qts, 100000);
    qtest_clock_step(qts, 100000);
    value = qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_ISR);
    g_assert_cmphex(value & (PIT64B_INT_PERIOD | PIT64B_INT_OVRE), ==,
                    PIT64B_INT_PERIOD | PIT64B_INT_OVRE);

    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_CR, PIT64B_CR_SWRST);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_MR, PIT64B_MR_CONT);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_WPMR,
                 PIT64B_WPMR_KEY | PIT64B_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_PIT64B0_BASE + PIT64B_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_MR), ==,
                    PIT64B_MR_CONT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_WPSR), ==,
                    (PIT64B_MR << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B0_BASE + PIT64B_WPSR), ==,
                    0);

    /* Linux uses a full-range continuous PIT64B as its clocksource. */
    pmc_write_pcr(qts, 58, PMC_PCR_EN);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/pclk[58]"), !=, 0);
    qtest_writel(qts, SAM9X7_PIT64B1_BASE + PIT64B_CR, PIT64B_CR_SWRST);
    qtest_writel(qts, SAM9X7_PIT64B1_BASE + PIT64B_MR, PIT64B_MR_CONT);
    qtest_writel(qts, SAM9X7_PIT64B1_BASE + PIT64B_MSB_PR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_PIT64B1_BASE + PIT64B_LSB_PR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_PIT64B1_BASE + PIT64B_CR, PIT64B_CR_START);
    qtest_clock_step(qts, 100000);
    value = qtest_readl(qts, SAM9X7_PIT64B1_BASE + PIT64B_TLSBR);
    g_assert_cmpuint(value, >, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B1_BASE + PIT64B_TMSBR),
                    ==, 0);

    pmc_write_pcr(qts, 58, 0);
    value = qtest_readl(qts, SAM9X7_PIT64B1_BASE + PIT64B_TLSBR);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT64B1_BASE + PIT64B_TLSBR),
                    ==, value);
    pmc_write_pcr(qts, 58, PMC_PCR_EN);
    qtest_clock_step(qts, 100000);
    g_assert_cmpuint(qtest_readl(qts,
                                 SAM9X7_PIT64B1_BASE + PIT64B_TLSBR), >,
                     value);

    qtest_quit(qts);
}

static void test_tcb_clocksource_clockevent_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t ch0 = SAM9X7_TCB_BASE + TCB_CHANNEL(0);
    uint64_t ch1 = SAM9X7_TCB_BASE + TCB_CHANNEL(1);
    uint64_t ch2 = SAM9X7_TCB_BASE + TCB_CHANNEL(2);
    uint32_t value;

    g_assert_cmphex(qtest_readl(qts, ch0 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ch1 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ch2 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ch0 + TCB_CV), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ch0 + TCB_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB_BASE + TCB_BMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB_BASE + TCB_QIMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB_BASE + TCB_WPMR), ==, 0);

    /* Select the 12 MHz main clock so short qtest steps exercise MCK/8. */
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MCKR, 1);
    aic_configure(qts, 17, AIC_SMR_LEVEL_HIGH | 3, 0x17171717);
    qtest_writel(qts, ch0 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_WAVE);
    qtest_writel(qts, ch0 + TCB_CCR, TCB_CCR_CLKEN | TCB_CCR_SWTRG);

    /* A reset-gated peripheral clock leaves an enabled counter frozen. */
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, ch0 + TCB_CV), ==, 0);
    g_assert_true(qtest_readl(qts, ch0 + TCB_CSR) & TCB_SR_CLKSTA);

    pmc_write_pcr(qts, 17, PMC_PCR_EN);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/pclk[17]"), !=, 0);
    qtest_clock_step(qts, 100000);
    value = qtest_readl(qts, ch0 + TCB_CV);
    g_assert_cmpuint(value, >, 0);

    pmc_write_pcr(qts, 17, 0);
    value = qtest_readl(qts, ch0 + TCB_CV);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, ch0 + TCB_CV), ==, value);
    pmc_write_pcr(qts, 17, PMC_PCR_EN);
    qtest_clock_step(qts, 100000);
    g_assert_cmpuint(qtest_readl(qts, ch0 + TCB_CV), >, value);

    qtest_writel(qts, SAM9X7_TCB_BASE + TCB_BCR, 1);
    g_assert_cmpuint(qtest_readl(qts, ch0 + TCB_CV), <=, 1);

    /* Channel 2 is the Linux periodic and one-shot clockevent source. */
    qtest_writel(qts, ch2 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_WAVE |
                 TCB_CMR_WAVESEL_UP_RC);
    qtest_writel(qts, ch2 + TCB_RC, 4);
    qtest_writel(qts, ch2 + TCB_IER, TCB_INT_CPCS);
    g_assert_cmphex(qtest_readl(qts, ch2 + TCB_IMR), ==, TCB_INT_CPCS);
    qtest_writel(qts, ch2 + TCB_CCR, TCB_CCR_CLKEN | TCB_CCR_SWTRG);
    qtest_clock_step(qts, 10000);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(17));
    value = qtest_readl(qts, ch2 + TCB_SR);
    g_assert_true(value & TCB_INT_CPCS);
    g_assert_true(value & TCB_SR_CLKSTA);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(17));

    qtest_writel(qts, ch2 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_CPCSTOP | TCB_CMR_WAVE |
                 TCB_CMR_WAVESEL_UP_RC);
    qtest_writel(qts, ch2 + TCB_RC, 6);
    qtest_writel(qts, ch2 + TCB_CCR, TCB_CCR_CLKEN | TCB_CCR_SWTRG);
    qtest_clock_step(qts, 10000);
    g_assert_true(qtest_readl(qts, ch2 + TCB_SR) & TCB_INT_CPCS);
    g_assert_true(qtest_readl(qts, ch2 + TCB_CSR) & TCB_SR_CLKSTA);
    qtest_clock_step(qts, 10000);
    g_assert_false(qtest_readl(qts, ch2 + TCB_SR) & TCB_INT_CPCS);
    qtest_writel(qts, ch2 + TCB_CCR, TCB_CCR_CLKDIS);
    g_assert_false(qtest_readl(qts, ch2 + TCB_CSR) & TCB_SR_CLKSTA);

    qtest_writel(qts, SAM9X7_TCB_BASE + TCB_WPMR,
                 TCB_WPMR_KEY | TCB_WPMR_WPEN | TCB_WPMR_WPITEN |
                 TCB_WPMR_WPCREN);
    qtest_writel(qts, ch1 + TCB_CMR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, ch1 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, ch1 + TCB_SSR), ==,
                    ((TCB_CHANNEL(1) + TCB_CMR) << 8) | 1);
    qtest_writel(qts, ch2 + TCB_IER, TCB_INT_CPCS);
    g_assert_cmphex(qtest_readl(qts, ch2 + TCB_IMR), ==, TCB_INT_CPCS);
    qtest_writel(qts, ch0 + TCB_CCR, TCB_CCR_CLKDIS);
    g_assert_true(qtest_readl(qts, ch0 + TCB_CSR) & TCB_SR_CLKSTA);

    qtest_writel(qts, SAM9X7_TCB_BASE + TCB_WPMR, TCB_WPMR_KEY);
    qtest_writel(qts, ch0 + TCB_CCR, TCB_CCR_CLKDIS);
    g_assert_false(qtest_readl(qts, ch0 + TCB_CSR) & TCB_SR_CLKSTA);
    qtest_writel(qts, ch2 + TCB_IDR, TCB_INT_CPCS);
    g_assert_cmphex(qtest_readl(qts, ch2 + TCB_IMR), ==, 0);

    qtest_quit(qts);
}

static void test_tcb1_reset_masks_and_independence(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t tc0_ch0 = SAM9X7_TCB_BASE + TCB_CHANNEL(0);
    uint64_t tc0_ch1 = SAM9X7_TCB_BASE + TCB_CHANNEL(1);
    uint64_t tc1_ch0 = SAM9X7_TCB1_BASE + TCB_CHANNEL(0);
    uint64_t tc1_ch1 = SAM9X7_TCB1_BASE + TCB_CHANNEL(1);
    uint64_t tc1_ch2 = SAM9X7_TCB1_BASE + TCB_CHANNEL(2);

    g_assert_cmphex(qtest_readl(qts, tc1_ch0 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch1 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch2 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch0 + TCB_CV), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch0 + TCB_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_BMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_QIMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_WPMR), ==, 0);

    qtest_writel(qts, tc0_ch0 + TCB_CMR, TCB_CMR_CLOCK2);
    qtest_writel(qts, SAM9X7_TCB_BASE + TCB_BMR, 0x15);
    qtest_writel(qts, tc1_ch0 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_WAVE);
    qtest_writel(qts, SAM9X7_TCB1_BASE + TCB_BMR, UINT32_MAX);
    qtest_writel(qts, tc1_ch1 + TCB_IER, UINT32_MAX);

    g_assert_cmphex(qtest_readl(qts, tc0_ch0 + TCB_CMR), ==,
                    TCB_CMR_CLOCK2);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB_BASE + TCB_BMR), ==,
                    0x15);
    g_assert_cmphex(qtest_readl(qts, tc0_ch1 + TCB_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch0 + TCB_CMR), ==,
                    TCB_CMR_CLOCK2 | TCB_CMR_WAVE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_BMR), ==,
                    TCB_BMR_MASK);
    g_assert_cmphex(qtest_readl(qts, tc1_ch1 + TCB_IMR), ==,
                    TCB_INT_MASK);

    qtest_writel(qts, SAM9X7_TCB1_BASE + TCB_WPMR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_WPMR), ==, 0);
    qtest_writel(qts, SAM9X7_TCB1_BASE + TCB_WPMR,
                 TCB_WPMR_KEY | UINT8_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_WPMR), ==,
                    TCB_WPMR_WPEN | TCB_WPMR_WPITEN |
                    TCB_WPMR_WPCREN | TCB_WPMR_FIRSTE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB_BASE + TCB_WPMR), ==, 0);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, tc1_ch0 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch1 + TCB_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_BMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_QIMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TCB1_BASE + TCB_WPMR), ==, 0);

    qtest_quit(qts);
}

static void test_tcb1_clock_gating_and_irq(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t tc0_ch0 = SAM9X7_TCB_BASE + TCB_CHANNEL(0);
    uint64_t tc1_ch0 = SAM9X7_TCB1_BASE + TCB_CHANNEL(0);
    uint64_t tc1_ch1 = SAM9X7_TCB1_BASE + TCB_CHANNEL(1);
    uint64_t tc1_ch2 = SAM9X7_TCB1_BASE + TCB_CHANNEL(2);
    uint32_t tc0_value;
    uint32_t tc1_value;
    uint32_t gclk_value;
    uint32_t status;

    aic_configure(qts, 45, AIC_SMR_LEVEL_HIGH | 3, 0x45454545);
    qtest_writel(qts, tc0_ch0 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_WAVE);
    qtest_writel(qts, tc1_ch0 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_WAVE);
    qtest_writel(qts, tc1_ch1 + TCB_CMR, TCB_CMR_WAVE);
    qtest_writel(qts, tc0_ch0 + TCB_CCR,
                 TCB_CCR_CLKEN | TCB_CCR_SWTRG);
    qtest_writel(qts, tc1_ch0 + TCB_CCR,
                 TCB_CCR_CLKEN | TCB_CCR_SWTRG);
    qtest_writel(qts, tc1_ch1 + TCB_CCR,
                 TCB_CCR_CLKEN | TCB_CCR_SWTRG);

    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, tc0_ch0 + TCB_CV), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch0 + TCB_CV), ==, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch1 + TCB_CV), ==, 0);

    pmc_write_pcr(qts, 17, PMC_PCR_EN);
    pmc_write_pcr(qts, 45, PMC_PCR_EN);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/pclk[45]"), !=, 0);
    qtest_clock_step(qts, 100000);
    tc0_value = qtest_readl(qts, tc0_ch0 + TCB_CV);
    tc1_value = qtest_readl(qts, tc1_ch0 + TCB_CV);
    g_assert_cmpuint(tc0_value, >, 0);
    g_assert_cmpuint(tc1_value, >, 0);
    g_assert_cmphex(qtest_readl(qts, tc1_ch1 + TCB_CV), ==, 0);

    pmc_write_pcr(qts, 45, 0);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/pclk[45]"), ==, 0);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, tc1_ch0 + TCB_CV), ==, tc1_value);
    g_assert_cmpuint(qtest_readl(qts, tc0_ch0 + TCB_CV), >, tc0_value);

    pmc_write_pcr(qts, 45, PMC_PCR_EN);
    qtest_clock_step(qts, 100000);
    g_assert_cmpuint(qtest_readl(qts, tc1_ch0 + TCB_CV), >, tc1_value);

    pmc_write_pcr(qts, 45, PMC_PCR_EN | (2U << 8) | (2U << 20) |
                           PMC_PCR_GCKEN);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/gclk[45]"), !=, 0);
    qtest_clock_step(qts, 100000);
    gclk_value = qtest_readl(qts, tc1_ch1 + TCB_CV);
    g_assert_cmpuint(gclk_value, >, 0);

    pmc_write_pcr(qts, 45, PMC_PCR_EN | (2U << 8) | (2U << 20));
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/gclk[45]"), ==, 0);
    qtest_clock_step(qts, 100000);
    g_assert_cmphex(qtest_readl(qts, tc1_ch1 + TCB_CV), ==, gclk_value);

    qtest_writel(qts, tc1_ch2 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_WAVE |
                 TCB_CMR_WAVESEL_UP_RC);
    qtest_writel(qts, tc1_ch2 + TCB_RC, 4);
    qtest_writel(qts, tc1_ch2 + TCB_IER, TCB_INT_CPCS);
    qtest_writel(qts, tc1_ch2 + TCB_CCR,
                 TCB_CCR_CLKEN | TCB_CCR_SWTRG);
    qtest_clock_step(qts, 10000);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(13));
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(17));
    status = qtest_readl(qts, tc1_ch2 + TCB_SR);
    g_assert_true(status & TCB_INT_CPCS);
    g_assert_true(status & TCB_SR_CLKSTA);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(13));

    qtest_quit(qts);
}

static void test_tcb1_active_timer_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint64_t tc0_ch0 = SAM9X7_TCB_BASE + TCB_CHANNEL(0);
    uint64_t tc1_ch0 = SAM9X7_TCB1_BASE + TCB_CHANNEL(0);
    uint32_t before;
    uint32_t migrated;
    int64_t from_clock;

    pmc_write_pcr(from, 45, PMC_PCR_EN);
    qtest_writel(from, SAM9X7_TCB_BASE + TCB_BMR, 0x15);
    qtest_writel(from, SAM9X7_TCB1_BASE + TCB_BMR, 0x2a);
    qtest_writel(from, tc1_ch0 + TCB_CMR,
                 TCB_CMR_CLOCK2 | TCB_CMR_WAVE);
    qtest_writel(from, tc1_ch0 + TCB_RC, 0x11223344);
    qtest_writel(from, tc1_ch0 + TCB_IER, TCB_INT_CPCS);
    qtest_writel(from, tc1_ch0 + TCB_CCR,
                 TCB_CCR_CLKEN | TCB_CCR_SWTRG);
    qtest_writel(from, SAM9X7_TCB1_BASE + TCB_WPMR,
                 TCB_WPMR_KEY | TCB_WPMR_WPEN);
    from_clock = qtest_clock_step(from, 100000);
    before = qtest_readl(from, tc1_ch0 + TCB_CV);
    g_assert_cmpuint(before, >, 0);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, tc0_ch0 + TCB_CMR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TCB_BASE + TCB_BMR), ==, 0x15);
    g_assert_cmphex(qtest_readl(to, tc1_ch0 + TCB_CMR), ==,
                    TCB_CMR_CLOCK2 | TCB_CMR_WAVE);
    g_assert_cmphex(qtest_readl(to, tc1_ch0 + TCB_RC), ==, 0x11223344);
    g_assert_cmphex(qtest_readl(to, tc1_ch0 + TCB_IMR), ==, TCB_INT_CPCS);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TCB1_BASE + TCB_BMR), ==, 0x2a);
    g_assert_cmphex(qtest_readl(to, SAM9X7_TCB1_BASE + TCB_WPMR), ==,
                    TCB_WPMR_WPEN);
    g_assert_true(qtest_readl(to, tc1_ch0 + TCB_CSR) & TCB_SR_CLKSTA);
    g_assert_cmpuint(get_clock_period(to,
                                     "/machine/soc/pmc/pclk[45]"), !=, 0);

    /* qtest virtual clocks are independent; align the destination clock. */
    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);
    migrated = qtest_readl(to, tc1_ch0 + TCB_CV);
    g_assert_cmpuint(migrated, >=, before);
    qtest_clock_step(to, 100000);
    g_assert_cmpuint(qtest_readl(to, tc1_ch0 + TCB_CV), >, migrated);

    qtest_quit(to);
    qtest_quit(from);
}

static uint32_t xdmac_waitl(QTestState *qts, uint64_t offset,
                            uint32_t mask, uint32_t expected)
{
    int64_t deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    uint32_t value;

    do {
        value = qtest_readl(qts, SAM9X7_XDMAC_BASE + offset);
        if ((value & mask) == expected) {
            return value;
        }
        qtest_clock_step(qts, 1);
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);

    g_error("timed out waiting for XDMAC offset 0x%04" PRIx64
            " mask 0x%08x to become 0x%08x (value 0x%08x)",
            offset, mask, expected, value);
}

static void test_flexcom_twi_xdmac_requests_and_mode_gating(void)
{
    static const uint8_t tx_values[] = {
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65,
        0x76, 0x87, 0x98, 0xa9, 0xba, 0xcb,
    };
    static const uint8_t gated_values[] = { 0xdc, 0xed, 0xfe, 0x0f };
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0xf1c0;
    const uint32_t gated_source = SAM9X7_DDR_BASE + 0xf1d0;
    const uint32_t rx_target = SAM9X7_DDR_BASE + 0xf1e0;
    const uint32_t rx_descriptor = SAM9X7_DDR_BASE + 0xf200;
    const uint32_t mismatch_target = SAM9X7_DDR_BASE + 0xf220;
    const uint64_t flex_base = SAM9X7_FLEXCOM6_BASE;
    const uint64_t twi_base = SAM9X7_TWI6_BASE;
    const uint64_t tx_channel = XDMAC_CHANNEL(0);
    const uint64_t rx_channel = XDMAC_CHANNEL(1);
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at24c-eeprom,bus=i2c6,address=0x53,rom-size=256");
    uint32_t descriptor[5] = { 0 };
    uint8_t rx_values[sizeof(tx_values)] = { 0 };
    unsigned int i;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 9, PMC_PCR_EN);
    qtest_writeb(qts, flex_base + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(qts, twi_base + TWI_CR, TWI_CR_SWRST);
    qtest_writel(qts, twi_base + TWI_CR,
                 TWI_CR_FIFOEN | TWI_CR_ACMEN);
    qtest_writel(qts, twi_base + TWI_CR,
                 TWI_CR_MSEN | TWI_CR_SVDIS);
    qtest_writel(qts, twi_base + TWI_FMR,
                 TWI_FMR_TXRDYM_FOUR | TWI_FMR_RXRDYM_FOUR);
    qtest_writel(qts, twi_base + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, twi_base + TWI_IADR, 0x60);
    qtest_writel(qts, twi_base + TWI_ACR,
                 TWI_ACR_DATAL(sizeof(tx_values)));

    qtest_memwrite(qts, tx_source, tx_values, sizeof(tx_values));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 twi_base + TWI_THR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC,
                 sizeof(tx_values) / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_DWIDTH_WORD | XDMAC_CC_PERID(12) |
                 XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    twi_wait_status(qts, twi_base, TWI_SR_TXCOMP);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CUBC), ==, 0);

    qtest_writel(qts, twi_base + TWI_ACR,
                 TWI_ACR_DIR | TWI_ACR_DATAL(sizeof(rx_values)));
    qtest_writel(qts, twi_base + TWI_IADR, 0x60);
    qtest_writel(qts, twi_base + TWI_CR, TWI_CR_START);
    qtest_clock_step_next(qts);

    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NSEN |
                                XDMAC_MBR_UBC_NDEN |
                                XDMAC_MBR_UBC_NDV2 |
                                sizeof(rx_values) / sizeof(uint32_t));
    descriptor[2] = cpu_to_le32(twi_base + TWI_RHR);
    descriptor[3] = cpu_to_le32(rx_target);
    descriptor[4] = cpu_to_le32(XDMAC_CC_TYPE_PER |
                                XDMAC_CC_DWIDTH_WORD |
                                XDMAC_CC_PERID(13) | XDMAC_CC_DAM_INC);
    qtest_memwrite(qts, rx_descriptor, descriptor, sizeof(descriptor));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CNDA,
                 rx_descriptor);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CNDC,
                 XDMAC_CNDC_NDE | XDMAC_CNDC_NDSUP |
                 XDMAC_CNDC_NDDUP | XDMAC_CNDC_NDVIEW2);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DWIDTH_WORD |
                 XDMAC_CC_PERID(13) | XDMAC_CC_DAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    for (i = 0; i < 32; i++) {
        if (!(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(1))) {
            break;
        }
        qtest_clock_step_next(qts);
    }
    g_assert_cmpuint(i, <, 32);
    qtest_memread(qts, rx_target, rx_values, sizeof(rx_values));
    g_assert_cmpmem(rx_values, sizeof(rx_values),
                    tx_values, sizeof(tx_values));
    g_assert_true(qtest_readl(qts, twi_base + TWI_SR) &
                  TWI_SR_TXCOMP);

    /* The FIFO ready mode and XDMAC access width must describe one chunk. */
    qtest_writel(qts, twi_base + TWI_ACR,
                 TWI_ACR_DIR | TWI_ACR_DATAL(sizeof(rx_values)));
    qtest_writel(qts, twi_base + TWI_IADR, 0x60);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CNDC, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CSA,
                 twi_base + TWI_RHR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CDA,
                 mismatch_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CUBC,
                 sizeof(rx_values));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(13) |
                 XDMAC_CC_DAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    qtest_writel(qts, twi_base + TWI_CR, TWI_CR_RXFCLR | TWI_CR_START);
    for (i = 0; i < 16; i++) {
        if (qtest_readl(qts, twi_base + TWI_SR) & TWI_SR_TXCOMP) {
            break;
        }
        qtest_clock_step_next(qts);
    }
    g_assert_cmpuint(i, <, 16);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(1));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CUBC), ==, 3);
    g_assert_cmphex(qtest_readl(qts, twi_base + TWI_FLR), ==, 3U << 16);
    g_assert_false(qtest_readl(qts, twi_base + TWI_SR) & TWI_SR_RXRDY);

    qtest_writel(qts, twi_base + TWI_FMR, TWI_FMR_TXRDYM_FOUR);
    xdmac_waitl(qts, XDMAC_GS, BIT(1), 0);
    memset(rx_values, 0, sizeof(rx_values));
    qtest_memread(qts, mismatch_target, rx_values, sizeof(rx_values));
    g_assert_cmpmem(rx_values, sizeof(rx_values),
                    tx_values, sizeof(tx_values));
    g_assert_cmphex(qtest_readl(qts, twi_base + TWI_FLR), ==, 0);

    qtest_writel(qts, twi_base + TWI_FMR,
                 TWI_FMR_TXRDYM_FOUR | TWI_FMR_RXRDYM_FOUR);

    /* A request from an inactive FLEXCOM personality must not leak. */
    qtest_memwrite(qts, gated_source, gated_values, sizeof(gated_values));
    qtest_writel(qts, twi_base + TWI_ACR,
                 TWI_ACR_DATAL(sizeof(gated_values)));
    qtest_writel(qts, twi_base + TWI_IADR, 0x80);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 gated_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 twi_base + TWI_THR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC,
                 sizeof(gated_values) / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_DWIDTH_WORD | XDMAC_CC_PERID(12) |
                 XDMAC_CC_SAM_INC);
    qtest_writeb(qts, flex_base + FLEX_MR, FLEX_MODE_NONE);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    qtest_clock_step(qts, 10000);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CUBC), ==,
                    sizeof(gated_values) / sizeof(uint32_t));

    qtest_writeb(qts, flex_base + FLEX_MR, FLEX_MODE_TWI);
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    twi_wait_status(qts, twi_base, TWI_SR_TXCOMP);

    qtest_writel(qts, twi_base + TWI_ACR,
                 TWI_ACR_DIR | TWI_ACR_DATAL(sizeof(gated_values)));
    qtest_writel(qts, twi_base + TWI_IADR, 0x80);
    qtest_writel(qts, twi_base + TWI_CR,
                 TWI_CR_RXFCLR | TWI_CR_START);
    twi_wait_status(qts, twi_base, TWI_SR_RXRDY);
    g_assert_cmphex(qtest_readl(qts, twi_base + TWI_RHR), ==,
                    0x0ffeeddc);

    qtest_quit(qts);
}

static void test_dbgu_xdmac_requests(void)
{
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0xf000;
    const uint32_t rx_target = SAM9X7_DDR_BASE + 0xf100;
    const uint64_t tx_channel = XDMAC_CHANNEL(0);
    const uint64_t rx_channel = XDMAC_CHANNEL(1);
    QTestState *qts;
    int sock_fd;
    uint8_t value;

    qts = qtest_init_with_serial(SAM9X75_MACHINE, &sock_fd);
    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 47, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_BRGR, 217);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_CR,
                 DBGU_CR_RXEN | DBGU_CR_TXEN);

    value = 0x6d;
    qtest_memwrite(qts, tx_source, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_DBGU_BASE + DBGU_THR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(28) | XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    g_assert_cmpint(recv(sock_fd, &value, 1, 0), ==, 1);
    g_assert_cmphex(value, ==, 0x6d);

    value = 0xb2;
    g_assert_cmpint(send(sock_fd, &value, 1, 0), ==, 1);
    dbgu_wait_status(qts, DBGU_INT_RXRDY);
    value = 0;
    qtest_memwrite(qts, rx_target, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CSA,
                 SAM9X7_DBGU_BASE + DBGU_RHR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CDA,
                 rx_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(29) |
                 XDMAC_CC_DAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    xdmac_waitl(qts, XDMAC_GS, BIT(1), 0);
    qtest_memread(qts, rx_target, &value, sizeof(value));
    g_assert_cmphex(value, ==, 0xb2);
    g_assert_false(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                   DBGU_INT_RXRDY);

    close(sock_fd);
    qtest_quit(qts);
}

static void test_flexcom_usart_xdmac_requests_and_mode_gating(void)
{
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0xf180;
    const uint32_t rx_target = SAM9X7_DDR_BASE + 0xf190;
    const uint64_t tx_channel = XDMAC_CHANNEL(0);
    const uint64_t rx_channel = XDMAC_CHANNEL(1);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t character_ns;
    uint8_t value;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 5, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_USART0_BASE + US_MR, US_MR_NORMAL_LOCAL);
    qtest_writel(qts, SAM9X7_USART0_BASE + US_BRGR, 217);
    qtest_writel(qts, SAM9X7_USART0_BASE + US_CR,
                 US_CR_RXEN | US_CR_TXEN);
    character_ns = usart_cycles_to_ns(qts, 5, 16 * 217 * 10);

    value = 0;
    qtest_memwrite(qts, rx_target, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CSA,
                 SAM9X7_USART0_BASE + US_RHR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CDA,
                 rx_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(1) |
                 XDMAC_CC_DAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(1));

    value = 0x6d;
    qtest_memwrite(qts, tx_source, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_USART0_BASE + US_THR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(0) | XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(1));
    qtest_clock_step(qts, character_ns);
    xdmac_waitl(qts, XDMAC_GS, BIT(1), 0);
    qtest_memread(qts, rx_target, &value, sizeof(value));
    g_assert_cmphex(value, ==, 0x6d);

    value = 0xa7;
    qtest_memwrite(qts, tx_source, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_USART0_BASE + US_THR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(0) | XDMAC_CC_SAM_INC);
    qtest_writeb(qts, SAM9X7_FLEXCOM0_BASE + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    qtest_clock_step(qts, character_ns);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_USART0_BASE + US_MR), ==, 0);

    qtest_writeb(qts, SAM9X7_FLEXCOM0_BASE + FLEX_MR,
                 FLEX_MODE_USART);
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    qtest_clock_step(qts, character_ns);
    g_assert_cmphex(qtest_readb(qts, SAM9X7_USART0_BASE + US_RHR), ==,
                    0xa7);

    qtest_quit(qts);
}

static void test_xdmac_flexcom_live_migration(void)
{
    static const uint8_t payload[] = { 0x21, 0x43, 0x65, 0x87 };
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0x11b00;
    const uint32_t rx_target = SAM9X7_DDR_BASE + 0x11c00;
    const uint32_t rx_descriptor = SAM9X7_DDR_BASE + 0x11d00;
    const uint64_t tx_channel = XDMAC_CHANNEL(0);
    const uint64_t rx_channel = XDMAC_CHANNEL(1);
    const uint32_t tx_config = XDMAC_CC_TYPE_PER |
                               XDMAC_CC_DSYNC_MEM2PER |
                               XDMAC_CC_PERID(0) | XDMAC_CC_SAM_INC;
    const uint32_t rx_config = XDMAC_CC_TYPE_PER |
                               XDMAC_CC_PERID(1) | XDMAC_CC_DAM_INC;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint32_t descriptor[5] = { 0 };
    uint8_t result[sizeof(payload)] = { 0 };
    uint64_t character_ns;
    int64_t from_clock;
    uint32_t value;
    unsigned int i;

    ebi_enable_ddr(from);
    pmc_write_pcr(from, 20, PMC_PCR_EN);
    pmc_write_pcr(from, 5, PMC_PCR_EN);
    aic_configure(from, 20, AIC_SMR_LEVEL_HIGH | 3, 0x20202020);
    qtest_writeb(from, SAM9X7_FLEXCOM0_BASE + FLEX_MR, FLEX_MODE_USART);
    qtest_writel(from, SAM9X7_USART0_BASE + US_MR, US_MR_NORMAL_LOCAL);
    qtest_writel(from, SAM9X7_USART0_BASE + US_BRGR, 217);
    qtest_writel(from, SAM9X7_USART0_BASE + US_CR,
                 US_CR_FIFOEN | US_CR_RXEN | US_CR_TXEN);
    character_ns = usart_cycles_to_ns(from, 5, 16 * 217 * 10);
    g_assert_true(qtest_readl(from, SAM9X7_USART0_BASE + US_CSR) &
                  US_INT_TXRDY);

    qtest_memwrite(from, tx_source, payload, sizeof(payload));
    qtest_memwrite(from, rx_target, result, sizeof(result));
    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NSEN |
                                XDMAC_MBR_UBC_NDEN |
                                XDMAC_MBR_UBC_NDV2 |
                                sizeof(payload));
    descriptor[2] = cpu_to_le32(SAM9X7_USART0_BASE + US_RHR);
    descriptor[3] = cpu_to_le32(rx_target);
    descriptor[4] = cpu_to_le32(rx_config);
    qtest_memwrite(from, rx_descriptor, descriptor, sizeof(descriptor));

    qtest_writel(from, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CNDA,
                 rx_descriptor);
    qtest_writel(from, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CNDC,
                 XDMAC_CNDC_NDE | XDMAC_CNDC_NDSUP |
                 XDMAC_CNDC_NDDUP | XDMAC_CNDC_NDVIEW2);
    qtest_writel(from, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CIE,
                 XDMAC_INT_LIS);
    qtest_writel(from, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    xdmac_waitl(from, rx_channel + XDMAC_CC,
                XDMAC_CC_INITD, XDMAC_CC_INITD);

    qtest_writel(from, SAM9X7_XDMAC_BASE + XDMAC_GRWS, BIT(0));
    qtest_writel(from, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(from, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_USART0_BASE + US_THR);
    qtest_writel(from, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC,
                 sizeof(payload));
    qtest_writel(from, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 tx_config);
    qtest_writel(from, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CIE,
                 XDMAC_INT_BIS);
    qtest_writel(from, SAM9X7_XDMAC_BASE + XDMAC_GIE, BIT(0) | BIT(1));
    qtest_writel(from, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));

    g_assert_cmphex(qtest_readl(from, SAM9X7_XDMAC_BASE + XDMAC_GS), ==,
                    BIT(0) | BIT(1));
    g_assert_cmphex(qtest_readl(from, SAM9X7_XDMAC_BASE + XDMAC_GRS), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(from, SAM9X7_XDMAC_BASE + XDMAC_GWS), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(from,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CUBC), ==, sizeof(payload));
    g_assert_cmphex(qtest_readl(from,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CUBC), ==, sizeof(payload));
    from_clock = qtest_clock_step(from, 1);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_FLEXCOM0_BASE + FLEX_MR), ==,
                    FLEX_MODE_USART);
    g_assert_cmphex(qtest_readl(to, SAM9X7_XDMAC_BASE + XDMAC_GS), ==,
                    BIT(0) | BIT(1));
    g_assert_cmphex(qtest_readl(to, SAM9X7_XDMAC_BASE + XDMAC_GRS), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(to, SAM9X7_XDMAC_BASE + XDMAC_GWS), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(to, SAM9X7_XDMAC_BASE + XDMAC_GIM), ==,
                    BIT(0) | BIT(1));
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CIM), ==, XDMAC_INT_BIS);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CIM), ==, XDMAC_INT_LIS);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CSA), ==, tx_source);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CUBC), ==, sizeof(payload));
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CC), ==,
                    tx_config | XDMAC_CC_INITD);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CNDA), ==, 0);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CNDC), ==,
                    XDMAC_CNDC_NDSUP | XDMAC_CNDC_NDDUP |
                    XDMAC_CNDC_NDVIEW2);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CSA), ==,
                    SAM9X7_USART0_BASE + US_RHR);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CDA), ==, rx_target);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CUBC), ==, sizeof(payload));
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + rx_channel +
                                XDMAC_CC), ==,
                    rx_config | XDMAC_CC_INITD);

    /* Align independent qtest clocks before resuming the suspended TX. */
    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);
    qtest_writel(to, SAM9X7_XDMAC_BASE + XDMAC_GRWR, BIT(0));
    xdmac_waitl(to, XDMAC_GS, BIT(0), 0);
    g_assert_true(qtest_readl(to, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(1));
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(20));
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + tx_channel +
                                XDMAC_CIS), ==, XDMAC_INT_BIS);
    g_assert_false(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(20));

    for (i = 0; i < sizeof(payload); i++) {
        qtest_clock_step(to, character_ns);
    }
    xdmac_waitl(to, XDMAC_GS, BIT(1), 0);
    qtest_memread(to, rx_target, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), payload, sizeof(payload));
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(20));
    value = qtest_readl(to,
                        SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CIS);
    g_assert_cmphex(value & (XDMAC_INT_BIS | XDMAC_INT_LIS), ==,
                    XDMAC_INT_BIS | XDMAC_INT_LIS);
    g_assert_false(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(20));

    qtest_quit(to);
    qtest_quit(from);
}

static void test_flexcom_spi_xdmac_requests_and_mode_gating(void)
{
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0xf1a0;
    const uint32_t rx_target = SAM9X7_DDR_BASE + 0xf1b0;
    const uint64_t tx_channel = XDMAC_CHANNEL(0);
    const uint64_t rx_channel = XDMAC_CHANNEL(1);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t first_transfer_ns;
    uint64_t second_transfer_ns;
    uint16_t value;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 5, PMC_PCR_EN);
    qtest_writeb(qts, SAM9X7_FLEXCOM0_BASE + FLEX_MR, FLEX_MODE_SPI);
    qtest_writel(qts, SAM9X7_SPI0_BASE + SPI_MR,
                 SPI_MR_MSTR | SPI_MR_LLB | SPI_MR_PCS(0xe));
    qtest_writel(qts, SAM9X7_SPI0_BASE + SPI_CSR(0),
                 SPI_CSR_BITS(16) | SPI_CSR_SCBR(217));
    qtest_writel(qts, SAM9X7_SPI0_BASE + SPI_CR, SPI_CR_SPIEN);
    first_transfer_ns = usart_cycles_to_ns(qts, 5,
                                            DIV_ROUND_UP(217, 2) +
                                            16 * 217);
    second_transfer_ns = usart_cycles_to_ns(qts, 5,
                                             6 + DIV_ROUND_UP(217, 2) +
                                             16 * 217);

    value = 0;
    qtest_memwrite(qts, rx_target, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CSA,
                 SAM9X7_SPI0_BASE + SPI_RDR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CDA,
                 rx_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(1) |
                 XDMAC_CC_DWIDTH_HALFWORD | XDMAC_CC_DAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(1));

    value = 0x6d5a;
    qtest_memwrite(qts, tx_source, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_SPI0_BASE + SPI_TDR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(0) | XDMAC_CC_DWIDTH_HALFWORD |
                 XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(1));
    qtest_clock_step(qts, first_transfer_ns);
    xdmac_waitl(qts, XDMAC_GS, BIT(1), 0);
    qtest_memread(qts, rx_target, &value, sizeof(value));
    g_assert_cmphex(value, ==, 0x6d5a);

    value = 0xa7b2;
    qtest_memwrite(qts, tx_source, &value, sizeof(value));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_SPI0_BASE + SPI_TDR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(0) | XDMAC_CC_DWIDTH_HALFWORD |
                 XDMAC_CC_SAM_INC);
    qtest_writeb(qts, SAM9X7_FLEXCOM0_BASE + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    qtest_clock_step(qts, second_transfer_ns);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SPI0_BASE + SPI_MR), ==, 0);

    qtest_writeb(qts, SAM9X7_FLEXCOM0_BASE + FLEX_MR, FLEX_MODE_SPI);
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    qtest_clock_step(qts, second_transfer_ns);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SPI0_BASE + SPI_RDR), ==,
                    0xa7b2);

    qtest_quit(qts);
}

static void test_i2smcc_registers_irq_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_SR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA), ==,
                    I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IMRA), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IMRB), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_I2SMCC_BASE + I2SMCC_VERSION), ==,
                    0x100);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA, UINT32_MAX);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRB, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA), ==,
                    0xffffffcf);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRB), ==,
                    0x00000300);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IERA, UINT32_MAX);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IERB, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IMRA), ==,
                    I2SMCC_INT_A_MASK);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IMRB), ==,
                    I2SMCC_INT_WERR);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IDRA,
                 I2SMCC_INT_TXRUNF | I2SMCC_INT_RXROVF);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IMRA), ==,
                    I2SMCC_INT_A_MASK &
                    ~(I2SMCC_INT_TXRUNF | I2SMCC_INT_RXROVF));
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IDRA, UINT32_MAX);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IDRB,
                 I2SMCC_INT_WERR);

    pmc_write_pcr(qts, 34, PMC_PCR_EN);
    aic_configure(qts, 34, AIC_SMR_LEVEL_HIGH | 7, 0x34003400);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IERB,
                 I2SMCC_INT_WERR);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPMR,
                 I2SMCC_WPMR_KEY | I2SMCC_WPMR_WPCFEN |
                 I2SMCC_WPMR_WPITEN | I2SMCC_WPMR_WPCTEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPMR), ==,
                    7);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA), ==,
                    0xffffffcf);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(2));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPSR), ==,
                    (I2SMCC_MRA << 8) | I2SMCC_WPSR_WPVS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPSR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRB), ==,
                    I2SMCC_INT_WERR);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(2));

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_CKEN | I2SMCC_CR_TXEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_SR), ==,
                    0);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IERA, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPSR), ==,
                    (I2SMCC_IERA << 8) | I2SMCC_WPSR_WPVS);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPMR), ==,
                    7);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_WPMR,
                 I2SMCC_WPMR_KEY);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR, I2SMCC_CR_SWRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRB), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_IMRA), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA), ==,
                    I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY);

    qtest_quit(qts);
}

static uint64_t i2smcc_period_ns(QTestState *qts, uint64_t source_cycles)
{
    uint64_t clock_period =
        get_clock_period(qts, "/machine/soc/pmc/pclk[34]");

    return (clock_period * source_cycles + UINT32_MAX) >> 32;
}

static void test_i2smcc_loopback_timing_and_xdmac(void)
{
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0xf200;
    const uint32_t rx_target = SAM9X7_DDR_BASE + 0xf300;
    const uint64_t tx_channel = XDMAC_CHANNEL(0);
    const uint64_t rx_channel = XDMAC_CHANNEL(1);
    const uint16_t samples[] = {
        cpu_to_le16(0x1122), cpu_to_le16(0x3344),
    };
    uint16_t received[G_N_ELEMENTS(samples)] = { 0 };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t word_period;
    uint32_t status;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 34, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA,
                 I2SMCC_MRA_MODE_MASTER | I2SMCC_MRA_DATA_16 |
                 I2SMCC_MRA_RXLOOP | I2SMCC_MRA_ISCKDIV(1));
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_CKEN | I2SMCC_CR_TXEN | I2SMCC_CR_RXEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_SR), ==,
                    I2SMCC_SR_TXEN | I2SMCC_SR_RXEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                    (I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY), ==,
                    I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY);

    qtest_memwrite(qts, tx_source, samples, sizeof(samples));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_I2SMCC_BASE + I2SMCC_THR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC,
                 G_N_ELEMENTS(samples));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(36) | XDMAC_CC_DWIDTH_HALFWORD |
                 XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                    (I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY), ==, 0);

    word_period = i2smcc_period_ns(qts, 32);
    g_assert_cmpuint(word_period, >, 0);
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_TXLRDY);
    g_assert_true(status & I2SMCC_INT_RXLRDY);
    g_assert_false(status & I2SMCC_INT_TXRRDY);
    g_assert_false(status & I2SMCC_INT_RXRRDY);

    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_TXRRDY);
    g_assert_true(status & I2SMCC_INT_RXRRDY);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CSA,
                 SAM9X7_I2SMCC_BASE + I2SMCC_RHR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CDA,
                 rx_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CUBC,
                 G_N_ELEMENTS(samples));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(37) |
                 XDMAC_CC_DWIDTH_HALFWORD | XDMAC_CC_DAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    xdmac_waitl(qts, XDMAC_GS, BIT(1), 0);
    qtest_memread(qts, rx_target, received, sizeof(received));
    g_assert_cmpmem(received, sizeof(received), samples, sizeof(samples));
    g_assert_false(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                   (I2SMCC_INT_RXLRDY | I2SMCC_INT_RXRRDY));

    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_TXLUNF);
    g_assert_false(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                   I2SMCC_INT_TXLUNF);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_TXDIS | I2SMCC_CR_RXDIS | I2SMCC_CR_CKDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_SR), ==,
                    0);

    qtest_quit(qts);
}

static void test_i2smcc_tdm_compact_and_mono(void)
{
    const uint32_t tdm_samples[] = {
        0x01010101, 0x12121212, 0x23232323, 0x34343434,
        0x45454545, 0x56565656, 0x67676767, 0x78787878,
    };
    const uint32_t packed_samples = 0x33441122;
    const uint32_t mono_write = 0xa5a555aa;
    const uint32_t mono_sample = 0x000055aa;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t word_period;
    uint32_t status;
    unsigned int i;

    pmc_write_pcr(qts, 34, PMC_PCR_EN);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA,
                 I2SMCC_MRA_MODE_MASTER | I2SMCC_MRA_FORMAT_TDM |
                 I2SMCC_MRA_NBCHAN(8) | I2SMCC_MRA_RXLOOP |
                 I2SMCC_MRA_ISCKDIV(1));
    for (i = 0; i < G_N_ELEMENTS(tdm_samples); i++) {
        qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_THR,
                     tdm_samples[i]);
        status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
        if (i < 6) {
            g_assert_cmphex(status & (I2SMCC_INT_TXLRDY |
                                     I2SMCC_INT_TXRRDY), ==,
                            I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY);
        } else if (i == 6) {
            g_assert_cmphex(status & (I2SMCC_INT_TXLRDY |
                                     I2SMCC_INT_TXRRDY), ==,
                            I2SMCC_INT_TXRRDY);
        } else {
            g_assert_false(status & (I2SMCC_INT_TXLRDY |
                                     I2SMCC_INT_TXRRDY));
        }
    }

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_CKEN | I2SMCC_CR_TXEN | I2SMCC_CR_RXEN);
    word_period = i2smcc_period_ns(qts, 64);
    for (i = 0; i < G_N_ELEMENTS(tdm_samples); i++) {
        qtest_clock_step(qts, word_period);
    }
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_cmphex(status & (I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY |
                             I2SMCC_INT_RXLRDY | I2SMCC_INT_RXRRDY), ==,
                    I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY |
                    I2SMCC_INT_RXLRDY | I2SMCC_INT_RXRRDY);
    g_assert_false(status & (I2SMCC_INT_TXLUNF | I2SMCC_INT_TXRUNF |
                             I2SMCC_INT_RXLOVF | I2SMCC_INT_RXROVF));

    for (i = 0; i < G_N_ELEMENTS(tdm_samples); i++) {
        g_assert_cmphex(qtest_readl(qts,
                                   SAM9X7_I2SMCC_BASE + I2SMCC_RHR), ==,
                        tdm_samples[i]);
        status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
        if (i < 6) {
            g_assert_true(status & I2SMCC_INT_RXLRDY);
            g_assert_true(status & I2SMCC_INT_RXRRDY);
        } else if (i == 6) {
            g_assert_false(status & I2SMCC_INT_RXLRDY);
            g_assert_true(status & I2SMCC_INT_RXRRDY);
        } else {
            g_assert_false(status & (I2SMCC_INT_RXLRDY |
                                     I2SMCC_INT_RXRRDY));
        }
    }

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_TXDIS | I2SMCC_CR_RXDIS | I2SMCC_CR_CKDIS);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR, I2SMCC_CR_SWRST);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA,
                 I2SMCC_MRA_MODE_MASTER | I2SMCC_MRA_DATA_16C |
                 I2SMCC_MRA_RXLOOP | I2SMCC_MRA_ISCKDIV(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                    (I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY), ==,
                    I2SMCC_INT_TXLRDY);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_THR, packed_samples);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_CKEN | I2SMCC_CR_TXEN | I2SMCC_CR_RXEN);
    qtest_clock_step(qts, i2smcc_period_ns(qts, 32));
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_I2SMCC_BASE + I2SMCC_RHR), ==,
                    packed_samples);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_TXDIS | I2SMCC_CR_RXDIS | I2SMCC_CR_CKDIS);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR, I2SMCC_CR_SWRST);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA,
                 I2SMCC_MRA_MODE_MASTER | I2SMCC_MRA_DATA_16 |
                 I2SMCC_MRA_RXLOOP | I2SMCC_MRA_TXMONO |
                 I2SMCC_MRA_ISCKDIV(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                    (I2SMCC_INT_TXLRDY | I2SMCC_INT_TXRRDY), ==,
                    I2SMCC_INT_TXLRDY);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_THR, mono_write);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_CKEN | I2SMCC_CR_TXEN | I2SMCC_CR_RXEN);
    word_period = i2smcc_period_ns(qts, 32);
    qtest_clock_step(qts, word_period);
    qtest_clock_step(qts, word_period);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_I2SMCC_BASE + I2SMCC_RHR), ==,
                    mono_sample);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_I2SMCC_BASE + I2SMCC_RHR), ==,
                    mono_sample);

    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_TXLUNF);
    g_assert_false(status & I2SMCC_INT_TXRUNF);
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_false(status & I2SMCC_INT_TXLUNF);
    g_assert_true(status & I2SMCC_INT_TXRUNF);

    /* The first write after an underrun is discarded for synchronization. */
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_THR, 0xdead);
    g_assert_true(qtest_readl(qts,
                             SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                  I2SMCC_INT_TXLRDY);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_THR, 0xbeef);
    g_assert_false(qtest_readl(qts,
                              SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                   I2SMCC_INT_TXLRDY);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_TXDIS | I2SMCC_CR_RXDIS | I2SMCC_CR_CKDIS);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR, I2SMCC_CR_SWRST);

    /* TDM error flags identify even and odd channels, not access groups. */
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA,
                 I2SMCC_MRA_MODE_MASTER | I2SMCC_MRA_FORMAT_TDM |
                 I2SMCC_MRA_NBCHAN(3) | I2SMCC_MRA_ISCKDIV(1));
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_CKEN | I2SMCC_CR_TXEN);
    word_period = i2smcc_period_ns(qts, 64);
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_TXLUNF);
    g_assert_false(status & I2SMCC_INT_TXRUNF);
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_false(status & I2SMCC_INT_TXLUNF);
    g_assert_true(status & I2SMCC_INT_TXRUNF);
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_TXLUNF);
    g_assert_false(status & I2SMCC_INT_TXRUNF);

    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_TXDIS | I2SMCC_CR_CKDIS);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR, I2SMCC_CR_SWRST);
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_MRA,
                 I2SMCC_MRA_MODE_MASTER | I2SMCC_MRA_FORMAT_TDM |
                 I2SMCC_MRA_NBCHAN(3) | I2SMCC_MRA_ISCKDIV(1));
    qtest_writel(qts, SAM9X7_I2SMCC_BASE + I2SMCC_CR,
                 I2SMCC_CR_CKEN | I2SMCC_CR_RXEN);
    qtest_clock_step(qts, 3 * word_period);
    g_assert_false(qtest_readl(qts,
                              SAM9X7_I2SMCC_BASE + I2SMCC_ISRA) &
                   (I2SMCC_INT_RXLOVF | I2SMCC_INT_RXROVF));
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_RXLOVF);
    g_assert_false(status & I2SMCC_INT_RXROVF);
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_false(status & I2SMCC_INT_RXLOVF);
    g_assert_true(status & I2SMCC_INT_RXROVF);
    qtest_clock_step(qts, word_period);
    status = qtest_readl(qts, SAM9X7_I2SMCC_BASE + I2SMCC_ISRA);
    g_assert_true(status & I2SMCC_INT_RXLOVF);
    g_assert_false(status & I2SMCC_INT_RXROVF);

    qtest_quit(qts);
}

static void test_classd_registers_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_MR), ==,
                    CLASSD_MR_RESET);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTPMR), ==,
                    CLASSD_INTPMR_RESET);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_THR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_IMR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_ISR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_WPMR), ==,
                    0);

    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_MR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_INTPMR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_MR), ==,
                    CLASSD_MR_MASK);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTPMR), ==,
                    CLASSD_INTPMR_MASK);

    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_THR, 0xa5a55a5a);
    qtest_writew(qts, SAM9X7_CLASSD_BASE + CLASSD_THR, 0x1122);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_THR), ==,
                    0xa5a51122);

    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_IER, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_IMR), ==,
                    CLASSD_INT_DATRDY);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_IDR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_IMR), ==,
                    0);

    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_WPMR,
                 0x12345600 | CLASSD_WPMR_WPEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_WPMR), ==,
                    0);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_WPMR,
                 CLASSD_WPMR_KEY | CLASSD_WPMR_WPEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_WPMR), ==,
                    CLASSD_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_MR, 0);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_INTPMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_MR), ==,
                    CLASSD_MR_MASK);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTPMR), ==,
                    CLASSD_INTPMR_MASK);

    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_CR, CLASSD_CR_SWRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_MR), ==,
                    CLASSD_MR_RESET);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTPMR), ==,
                    CLASSD_INTPMR_RESET);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_WPMR), ==,
                    0);

    qtest_quit(qts);
}

static void test_classd_linux_regcache_resume(void)
{
    g_autofree char *log_path = NULL;
    g_autofree char *log = NULL;
    QTestState *qts;
    GError *error = NULL;
    unsigned int offset;
    int fd;

    fd = g_file_open_tmp("sam9x75-classd-log-XXXXXX", &log_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    qts = qtest_initf(SAM9X75_MACHINE " -d guest_errors -D %s",
                      log_path);
    for (offset = 0; offset <= CLASSD_WPMR; offset += sizeof(uint32_t)) {
        qtest_writel(qts, SAM9X7_CLASSD_BASE + offset, 0);
    }
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(log_path, &log, NULL, &error));
    g_assert_no_error(error);
    g_assert_null(strstr(log, "at91-classd"));
    g_assert_cmpint(g_unlink(log_path), ==, 0);
}

static void test_classd_timing_irq_and_xdmac(void)
{
    const uint32_t tx_source = SAM9X7_DDR_BASE + 0xf400;
    const uint32_t samples[] = {
        cpu_to_le32(0x11223344),
        cpu_to_le32(0x55667788),
        cpu_to_le32(0x99aabbcc),
    };
    const uint64_t channel = XDMAC_CHANNEL(0);
    const uint64_t sample_period = DIV_ROUND_UP(1000000000ULL, 48000);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    ebi_enable_ddr(qts);
    pmc_configure_audio_pll(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 42, PMC_PCR_EN | (6U << 8) | PMC_PCR_GCKEN);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/pmc/gclk[42]"), ==,
                     CLOCK_PERIOD_FROM_HZ(98303998));

    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_INTPMR,
                 CLASSD_INTPMR_FRAME(6));
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTSR), ==,
                    CLASSD_INTSR_CFGERR);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_INTPMR,
                 CLASSD_INTPMR_FRAME(3));
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTSR), ==, 0);

    aic_configure(qts, 42, AIC_SMR_LEVEL_HIGH | 7, 0x42004200);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_IER,
                 CLASSD_INT_DATRDY);

    qtest_memwrite(qts, tx_source, samples, sizeof(samples));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA,
                 tx_source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 SAM9X7_CLASSD_BASE + CLASSD_THR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 G_N_ELEMENTS(samples));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(35) | XDMAC_CC_DWIDTH_WORD |
                 XDMAC_CC_SAM_INC);

    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_MR,
                 CLASSD_MR_LEN | CLASSD_MR_REN);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(10));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_THR), ==,
                    0x11223344);

    qtest_clock_step(qts, sample_period);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_THR), ==,
                    0x55667788);
    qtest_clock_step(qts, sample_period);
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_THR), ==,
                    0x99aabbcc);

    g_assert_true(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_ISR) &
                  CLASSD_INT_DATRDY);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(10));
    qtest_clock_step(qts, sample_period);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(10));
    g_assert_true(qtest_readl(qts, SAM9X7_CLASSD_BASE + CLASSD_ISR) &
                  CLASSD_INT_DATRDY);

    pmc_write_pcr(qts, 42, PMC_PCR_EN | (2U << 8) | PMC_PCR_GCKEN);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_CLASSD_BASE + CLASSD_INTSR), ==,
                    CLASSD_INTSR_CFGERR);
    qtest_writel(qts, SAM9X7_CLASSD_BASE + CLASSD_MR, 0);

    qtest_quit(qts);
}

static void test_xdmac_registers_memcpy_and_descriptors(void)
{
    const uint32_t src0 = SAM9X7_DDR_BASE + 0x10000;
    const uint32_t dst0 = SAM9X7_DDR_BASE + 0x10100;
    const uint32_t desc0 = SAM9X7_DDR_BASE + 0x10200;
    const uint32_t desc1 = SAM9X7_DDR_BASE + 0x10220;
    const uint32_t desc2 = SAM9X7_DDR_BASE + 0x10260;
    const uint32_t desc3 = SAM9X7_DDR_BASE + 0x10280;
    const uint32_t src1 = SAM9X7_DDR_BASE + 0x10300;
    const uint32_t dst1 = SAM9X7_DDR_BASE + 0x10400;
    uint32_t descriptor[9];
    uint8_t source[16];
    uint8_t result[16];
    uint64_t ch0 = XDMAC_CHANNEL(0);
    uint64_t ch1 = XDMAC_CHANNEL(1);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;
    unsigned int i;

    ebi_enable_ddr(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GTYPE),
                    ==, 0x0032200f);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GCFG),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GWAC),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GIM),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GIS),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS),
                    ==, 0);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GCFG, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GCFG),
                    ==, 0x0000010f);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GWAC, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GWAC),
                    ==, 0x0000ffff);

    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    aic_configure(qts, 20, AIC_SMR_LEVEL_HIGH | 3, 0x20202020);

    for (i = 0; i < sizeof(source); i++) {
        source[i] = 0x80 + i;
    }
    memset(result, 0, sizeof(result));
    qtest_memwrite(qts, src0, source, sizeof(source));
    qtest_memwrite(qts, dst0, result, sizeof(result));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CSA, src0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CDA, dst0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CUBC,
                 sizeof(source) / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CC,
                 XDMAC_CC_PERID(0x7f) | XDMAC_CC_SAM_INC |
                 XDMAC_CC_DAM_INC | XDMAC_CC_DWIDTH_WORD);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CIE,
                 XDMAC_INT_BIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GIE, BIT(0));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);

    qtest_memread(qts, dst0, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), source, sizeof(source));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GIS) &
                  BIT(0));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(20));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CIS),
                    ==, XDMAC_INT_BIS);
    g_assert_false(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GIS) &
                   BIT(0));
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(20));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CC),
                    ==, XDMAC_CC_PERID(0x7f) | XDMAC_CC_SAM_INC |
                        XDMAC_CC_DAM_INC | XDMAC_CC_DWIDTH_WORD |
                        XDMAC_CC_INITD);

    for (i = 0; i < sizeof(source); i++) {
        source[i] = 0x30 + i;
    }
    memset(result, 0, sizeof(result));
    qtest_memwrite(qts, src1, source, sizeof(source));
    qtest_memwrite(qts, dst1, result, sizeof(result));

    memset(descriptor, 0, sizeof(descriptor));
    descriptor[0] = cpu_to_le32(desc1);
    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NDE |
                                XDMAC_MBR_UBC_NSEN |
                                XDMAC_MBR_UBC_NDEN |
                                XDMAC_MBR_UBC_NDV3 | 1);
    descriptor[2] = cpu_to_le32(src1);
    descriptor[3] = cpu_to_le32(dst1);
    descriptor[4] = cpu_to_le32(XDMAC_CC_PERID(0x7f) |
                                XDMAC_CC_SAM_INC | XDMAC_CC_DAM_INC |
                                XDMAC_CC_DWIDTH_WORD);
    qtest_memwrite(qts, desc0, descriptor, 5 * sizeof(descriptor[0]));

    memset(descriptor, 0, sizeof(descriptor));
    descriptor[0] = cpu_to_le32(desc2);
    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NSEN |
                                XDMAC_MBR_UBC_NDEN |
                                XDMAC_MBR_UBC_NDE |
                                XDMAC_MBR_UBC_NDV1 | 1);
    descriptor[2] = cpu_to_le32(src1 + 4);
    descriptor[3] = cpu_to_le32(dst1 + 4);
    descriptor[4] = cpu_to_le32(XDMAC_CC_PERID(0x7f) |
                                XDMAC_CC_SAM_INC | XDMAC_CC_DAM_INC |
                                XDMAC_CC_DWIDTH_WORD);
    qtest_memwrite(qts, desc1, descriptor, sizeof(descriptor));

    memset(descriptor, 0, sizeof(descriptor));
    descriptor[0] = cpu_to_le32(desc3);
    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NDE |
                                XDMAC_MBR_UBC_NSEN | 1);
    descriptor[2] = cpu_to_le32(src1 + 8);
    descriptor[3] = cpu_to_le32(dst1 + 8);
    qtest_memwrite(qts, desc2, descriptor, 4 * sizeof(descriptor[0]));

    memset(descriptor, 0, sizeof(descriptor));
    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NSEN | 1);
    descriptor[2] = cpu_to_le32(src1 + 12);
    qtest_memwrite(qts, desc3, descriptor, 3 * sizeof(descriptor[0]));

    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CNDA, desc0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CNDC,
                 XDMAC_CNDC_NDE | XDMAC_CNDC_NDSUP |
                 XDMAC_CNDC_NDDUP | XDMAC_CNDC_NDVIEW2);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CIE,
                 XDMAC_INT_LIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GIE, BIT(1));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1));
    xdmac_waitl(qts, XDMAC_GS, BIT(1), 0);

    qtest_memread(qts, dst1, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), source, sizeof(source));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GIS) &
                  BIT(1));
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CIS);
    g_assert_cmphex(value & (XDMAC_INT_BIS | XDMAC_INT_LIS), ==,
                    XDMAC_INT_BIS | XDMAC_INT_LIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CNDA),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CUBC),
                    ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CC) &
                  XDMAC_CC_INITD);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GID, BIT(0) | BIT(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GIM), ==,
                    0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CID,
                 XDMAC_INT_LIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CIM),
                    ==, 0);

    qtest_quit(qts);
}

static void test_xdmac_pacing_striding_and_errors(void)
{
    const uint32_t memset_dst = SAM9X7_DDR_BASE + 0x11000;
    const uint32_t sw_src = SAM9X7_DDR_BASE + 0x11100;
    const uint32_t sw_dst = SAM9X7_DDR_BASE + 0x11200;
    const uint32_t error_dst = SAM9X7_DDR_BASE + 0x11300;
    const uint32_t hw_src = SAM9X7_DDR_BASE + 0x11400;
    const uint32_t hw_dst = SAM9X7_DDR_BASE + 0x11500;
    uint8_t source[] = { 0xde, 0xad, 0xbe, 0xef };
    uint8_t result[20];
    uint8_t expected[20];
    uint64_t ch2 = XDMAC_CHANNEL(2);
    uint64_t ch3 = XDMAC_CHANNEL(3);
    uint64_t ch4 = XDMAC_CHANNEL(4);
    uint64_t ch5 = XDMAC_CHANNEL(5);
    uint64_t ch6 = XDMAC_CHANNEL(6);
    uint64_t ch7 = XDMAC_CHANNEL(7);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;
    unsigned int i;

    ebi_enable_ddr(qts);
    memset(result, 0xa5, sizeof(result));
    memcpy(expected, result, sizeof(expected));
    memset(&expected[0], 0x5a, 4);
    memset(&expected[6], 0x5a, 4);
    memset(&expected[12], 0x5a, 4);
    qtest_memwrite(qts, memset_dst, result, sizeof(result));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CDA, memset_dst);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CUBC, 4);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CBC, 2);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CDS_MSP,
                 0x5a5a5a5a);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CDUS, 2);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CC,
                 XDMAC_CC_PERID(0x7f) | XDMAC_CC_MEMSET |
                 XDMAC_CC_DAM_UBS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(2));

    /* The reset-gated peripheral clock leaves an enabled channel idle. */
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(2));
    qtest_memread(qts, memset_dst, result, sizeof(result));
    for (i = 0; i < sizeof(result); i++) {
        g_assert_cmphex(result[i], ==, 0xa5);
    }

    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    xdmac_waitl(qts, XDMAC_GS, BIT(2), 0);
    qtest_memread(qts, memset_dst, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), expected, sizeof(expected));
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS);
    g_assert_true(value & XDMAC_INT_BIS);

    qtest_memwrite(qts, sw_src, source, sizeof(source));
    memset(result, 0, sizeof(source));
    qtest_memwrite(qts, sw_dst, result, sizeof(source));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CSA, sw_src);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CDA, sw_dst);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CUBC,
                 sizeof(source));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_SWREQ | XDMAC_CC_SAM_INC |
                 XDMAC_CC_DAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(3));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(3));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CUBC),
                    ==, sizeof(source));

    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWS, BIT(3));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GRS) & BIT(3));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS) & BIT(3));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, BIT(3));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWS) & BIT(3));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, BIT(3));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CIS) &
                  XDMAC_INT_ROIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWR, BIT(3));
    xdmac_waitl(qts, ch3 + XDMAC_CUBC, UINT32_MAX, 3);

    for (i = 2; i > 0; i--) {
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, BIT(3));
        xdmac_waitl(qts, ch3 + XDMAC_CUBC, UINT32_MAX, i);
    }
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, BIT(3));
    xdmac_waitl(qts, XDMAC_GS, BIT(3), 0);
    qtest_memread(qts, sw_dst, result, sizeof(source));
    g_assert_cmpmem(result, sizeof(source), source, sizeof(source));

    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch4 + XDMAC_CUBC, 2);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch4 + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_SWREQ);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch4 + XDMAC_CIE,
                 XDMAC_INT_FIS | XDMAC_INT_DIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(4));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, BIT(4));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch4 + XDMAC_CIS) &
                  XDMAC_INT_FIS);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(4));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(4));
    g_assert_false(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(4));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch4 + XDMAC_CIS) &
                  XDMAC_INT_DIS);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch5 + XDMAC_CSA, 0xdeadbeec);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch5 + XDMAC_CDA, error_dst);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch5 + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch5 + XDMAC_CC,
                 XDMAC_CC_PERID(0x7f) | XDMAC_CC_SAM_INC |
                 XDMAC_CC_DAM_INC | XDMAC_CC_DWIDTH_WORD);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch5 + XDMAC_CIE,
                 XDMAC_INT_RBEIS | XDMAC_INT_WBEIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GIE, BIT(5));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(5));
    xdmac_waitl(qts, XDMAC_GIS, BIT(5), BIT(5));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(5));
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + ch5 + XDMAC_CIS);
    g_assert_true(value & XDMAC_INT_RBEIS);
    g_assert_false(value & XDMAC_INT_WBEIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(5));
    g_assert_false(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(5));

    qtest_memwrite(qts, hw_src, source, sizeof(source));
    memset(result, 0, sizeof(source));
    qtest_memwrite(qts, hw_dst, result, sizeof(source));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch6 + XDMAC_CSA, hw_src);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch6 + XDMAC_CDA, hw_dst);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch6 + XDMAC_CUBC, 2);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch6 + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(7) |
                 XDMAC_CC_SAM_INC | XDMAC_CC_DAM_INC |
                 XDMAC_CC_DWIDTH_HALFWORD);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(6));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(6));
    qtest_set_irq_in(qts, "/machine/soc/xdmac", "request", 7, 1);
    xdmac_waitl(qts, XDMAC_GS, BIT(6), 0);
    qtest_set_irq_in(qts, "/machine/soc/xdmac", "request", 7, 0);
    qtest_memread(qts, hw_dst, result, sizeof(source));
    g_assert_cmpmem(result, sizeof(source), source, sizeof(source));

    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch7 + XDMAC_CSA, hw_src);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch7 + XDMAC_CDA, 0xdeadbee0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch7 + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch7 + XDMAC_CC,
                 XDMAC_CC_PERID(0x7f) | XDMAC_CC_SAM_INC |
                 XDMAC_CC_DAM_INC | XDMAC_CC_DWIDTH_WORD);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch7 + XDMAC_CIE,
                 XDMAC_INT_RBEIS | XDMAC_INT_WBEIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GIE, BIT(7));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(7));
    xdmac_waitl(qts, XDMAC_GIS, BIT(7), BIT(7));
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + ch7 + XDMAC_CIS);
    g_assert_true(value & XDMAC_INT_WBEIS);
    g_assert_false(value & XDMAC_INT_RBEIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(7));
    g_assert_false(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(7));

    qtest_quit(qts);
}

static void test_xdmac_fair_scheduling_and_flush_scope(void)
{
    const uint32_t ring_desc = SAM9X7_DDR_BASE + 0x11600;
    const uint32_t ring_src = SAM9X7_DDR_BASE + 0x11700;
    const uint32_t ring_dst = SAM9X7_DDR_BASE + 0x11800;
    const uint32_t finite_src = SAM9X7_DDR_BASE + 0x11900;
    const uint32_t finite_dst = SAM9X7_DDR_BASE + 0x11a00;
    const uint64_t ch0 = XDMAC_CHANNEL(0);
    const uint64_t ch1 = XDMAC_CHANNEL(1);
    const uint64_t ch2 = XDMAC_CHANNEL(2);
    const uint64_t ch3 = XDMAC_CHANNEL(3);
    uint32_t descriptor[5] = { 0 };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);

    qtest_writel(qts, ring_src, 0x11223344);
    qtest_writel(qts, ring_dst, 0);
    descriptor[0] = cpu_to_le32(ring_desc);
    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NDE |
                                XDMAC_MBR_UBC_NSEN |
                                XDMAC_MBR_UBC_NDEN |
                                XDMAC_MBR_UBC_NDV2 | 1);
    descriptor[2] = cpu_to_le32(ring_src);
    descriptor[3] = cpu_to_le32(ring_dst);
    descriptor[4] = cpu_to_le32(XDMAC_CC_PERID(0x7f) |
                                XDMAC_CC_SAM_INC |
                                XDMAC_CC_DAM_INC |
                                XDMAC_CC_DWIDTH_WORD);
    qtest_memwrite(qts, ring_desc, descriptor, sizeof(descriptor));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CNDA, ring_desc);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CNDC,
                 XDMAC_CNDC_NDE | XDMAC_CNDC_NDSUP |
                 XDMAC_CNDC_NDDUP | XDMAC_CNDC_NDVIEW2);

    qtest_writel(qts, finite_src, 0xa1b2c3d4);
    qtest_writel(qts, finite_dst, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CSA, finite_src);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CDA, finite_dst);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch1 + XDMAC_CC,
                 XDMAC_CC_PERID(0x7f) | XDMAC_CC_SAM_INC |
                 XDMAC_CC_DAM_INC | XDMAC_CC_DWIDTH_WORD);

    /* A self-looping channel must not starve a finite higher channel. */
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0) | BIT(1));
    xdmac_waitl(qts, XDMAC_GS, BIT(1), 0);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));
    g_assert_cmphex(qtest_readl(qts, finite_dst), ==, 0xa1b2c3d4);
    g_assert_cmphex(qtest_readl(qts, ring_dst), ==, 0x11223344);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(0));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch0 + XDMAC_CIS) &
                  XDMAC_INT_DIS);

    /* A suspended source-peripheral flush completes even with an empty FIFO. */
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CSA, finite_src);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CDA, finite_dst);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_SWREQ);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWS, BIT(2));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(2));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GRS) & BIT(2));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS) & BIT(2));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, BIT(2));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS), ==,
                    XDMAC_INT_FIS);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(2));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS) &
                  XDMAC_INT_DIS);

    /* Flush requests after disable and on other transfer types are ignored. */
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, BIT(2));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS), ==, 0);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_SWREQ);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(2));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, BIT(2));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS), ==, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(2));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS) &
                  XDMAC_INT_DIS);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRS, BIT(2));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CC,
                 XDMAC_CC_PERID(0x7f));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(2));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, BIT(2));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS), ==, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, BIT(2));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CIS) &
                  XDMAC_INT_DIS);

    /* Suspend must precede descriptor fetch and zero-CUBC completion. */
    memset(descriptor, 0, sizeof(descriptor));
    descriptor[1] = cpu_to_le32(XDMAC_MBR_UBC_NSEN |
                                XDMAC_MBR_UBC_NDEN |
                                XDMAC_MBR_UBC_NDV2 | 1);
    descriptor[2] = cpu_to_le32(finite_src);
    descriptor[3] = cpu_to_le32(finite_dst);
    descriptor[4] = cpu_to_le32(XDMAC_CC_PERID(0x7f) |
                                XDMAC_CC_SAM_INC | XDMAC_CC_DAM_INC |
                                XDMAC_CC_DWIDTH_WORD);
    qtest_memwrite(qts, ring_desc, descriptor, sizeof(descriptor));
    qtest_writel(qts, finite_dst, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CNDA, ring_desc);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch2 + XDMAC_CNDC,
                 XDMAC_CNDC_NDE | XDMAC_CNDC_NDSUP |
                 XDMAC_CNDC_NDDUP | XDMAC_CNDC_NDVIEW2);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWS, BIT(2));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(2));
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(2));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + ch2 + XDMAC_CNDA),
                    ==, ring_desc);
    g_assert_false(qtest_readl(qts,
                               SAM9X7_XDMAC_BASE + ch2 + XDMAC_CC) &
                   XDMAC_CC_INITD);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWR, BIT(2));
    xdmac_waitl(qts, XDMAC_GS, BIT(2), 0);
    g_assert_cmphex(qtest_readl(qts, finite_dst), ==, 0xa1b2c3d4);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CNDC, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CUBC, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + ch3 + XDMAC_CC,
                 XDMAC_CC_PERID(0x7f));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWS, BIT(3));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(3));
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(3));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWR, BIT(3));
    xdmac_waitl(qts, XDMAC_GS, BIT(3), 0);
    g_assert_true(qtest_readl(qts,
                              SAM9X7_XDMAC_BASE + ch3 + XDMAC_CIS) &
                  XDMAC_INT_BIS);

    qtest_quit(qts);
}

static void test_xdmac_p2m_fifo_suspend_flush_and_disable(void)
{
    static const uint8_t payload[] = {
        0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0,
    };
    const uint32_t source = SAM9X7_DDR_BASE + 0x11e00;
    const uint32_t flush_target = SAM9X7_DDR_BASE + 0x11f00;
    const uint32_t disable_target = SAM9X7_DDR_BASE + 0x12000;
    const uint32_t word_target = SAM9X7_DDR_BASE + 0x12300;
    const unsigned int index = 3;
    const uint32_t channel_mask = BIT(index);
    const uint64_t channel = XDMAC_CHANNEL(index);
    const uint32_t config = XDMAC_CC_TYPE_PER |
                            XDMAC_CC_MBSIZE_SIXTEEN |
                            XDMAC_CC_SWREQ | XDMAC_CC_SAM_INC |
                            XDMAC_CC_DAM_INC;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t result[sizeof(payload)];
    uint32_t value;
    unsigned int i;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    qtest_memwrite(qts, source, payload, sizeof(payload));
    qtest_memset(qts, flush_target, 0, sizeof(payload));

    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA, source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 flush_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 sizeof(payload));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC, config);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS, channel_mask);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, channel_mask);

    /* GWS stops RAM writes but leaves source requests serviceable. */
    for (i = 0; i < 3; i++) {
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
        xdmac_waitl(qts, XDMAC_GSWS, channel_mask, 0);
    }
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) &
                  channel_mask);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CSA),
                    ==, source + 3);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, sizeof(payload));
    memset(result, 0xa5, sizeof(result));
    qtest_memread(qts, flush_target, result, sizeof(result));
    for (i = 0; i < sizeof(result); i++) {
        g_assert_cmphex(result[i], ==, 0);
    }

    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRS, channel_mask);
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC);
    g_assert_cmphex(value & (XDMAC_CC_RDIP | XDMAC_CC_WRIP), ==, 0);

    /* A flush drains its FIFO snapshot even while GWS remains asserted. */
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, channel_mask);
    value = xdmac_waitl(qts, channel + XDMAC_CIS,
                        XDMAC_INT_FIS, XDMAC_INT_FIS);
    g_assert_true(value & XDMAC_INT_FIS);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GRS) &
                  channel_mask);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS) &
                  channel_mask);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, sizeof(payload) - 3);
    qtest_memread(qts, flush_target, result, sizeof(result));
    g_assert_cmpmem(result, 3, payload, 3);
    for (i = 3; i < sizeof(result); i++) {
        g_assert_cmphex(result[i], ==, 0);
    }

    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GRWR, channel_mask);
    for (i = 3; i < sizeof(payload); i++) {
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
        xdmac_waitl(qts, XDMAC_GSWS, channel_mask, 0);
    }
    xdmac_waitl(qts, XDMAC_GS, channel_mask, 0);
    qtest_memread(qts, flush_target, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), payload, sizeof(payload));
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CIS);
    g_assert_true(value & XDMAC_INT_BIS);
    g_assert_false(value & XDMAC_INT_FIS);
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC);
    g_assert_cmphex(value & (XDMAC_CC_RDIP | XDMAC_CC_WRIP), ==, 0);

    /* GD also overrides GWS, but completes with DIS instead of BIS. */
    qtest_memset(qts, disable_target, 0, sizeof(payload));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA, source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 disable_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 sizeof(payload));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC, config);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS, channel_mask);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, channel_mask);
    for (i = 0; i < 3; i++) {
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
        xdmac_waitl(qts, XDMAC_GSWS, channel_mask, 0);
    }
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, channel_mask);
    xdmac_waitl(qts, XDMAC_GS, channel_mask, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS) &
                   channel_mask);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, sizeof(payload) - 3);
    qtest_memread(qts, disable_target, result, sizeof(result));
    g_assert_cmpmem(result, 3, payload, 3);
    for (i = 3; i < sizeof(result); i++) {
        g_assert_cmphex(result[i], ==, 0);
    }
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CIS);
    g_assert_true(value & XDMAC_INT_DIS);
    g_assert_false(value & (XDMAC_INT_BIS | XDMAC_INT_FIS));
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC);
    g_assert_cmphex(value & (XDMAC_CC_RDIP | XDMAC_CC_WRIP), ==, 0);

    /* A failed GD drain reports WBEIS and DIS instead of wedging GS. */
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA, source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 0xdeadbee0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 sizeof(payload));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC, config);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS, channel_mask);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, channel_mask);
    for (i = 0; i < 3; i++) {
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
        xdmac_waitl(qts, XDMAC_GSWS, channel_mask, 0);
    }
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, channel_mask);
    xdmac_waitl(qts, XDMAC_GS, channel_mask, 0);
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CIS);
    g_assert_cmphex(value & (XDMAC_INT_WBEIS | XDMAC_INT_DIS), ==,
                    XDMAC_INT_WBEIS | XDMAC_INT_DIS);
    g_assert_false(value & (XDMAC_INT_BIS | XDMAC_INT_FIS));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, sizeof(payload));

    /* FIFO occupancy is byte-based while CUBC remains transfer-based. */
    qtest_memset(qts, word_target, 0, sizeof(payload));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA, source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 word_target);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 sizeof(payload) / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC,
                 config | XDMAC_CC_DWIDTH_WORD);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS, channel_mask);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, channel_mask);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
    xdmac_waitl(qts, XDMAC_GSWS, channel_mask, 0);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CSA),
                    ==, source + sizeof(uint32_t));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, sizeof(payload) / sizeof(uint32_t));
    qtest_memread(qts, word_target, result, sizeof(result));
    for (i = 0; i < sizeof(result); i++) {
        g_assert_cmphex(result[i], ==, 0);
    }

    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, channel_mask);
    xdmac_waitl(qts, channel + XDMAC_CIS,
                XDMAC_INT_FIS, XDMAC_INT_FIS);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, 1);
    qtest_memread(qts, word_target, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(uint32_t), payload, sizeof(uint32_t));
    for (i = sizeof(uint32_t); i < sizeof(result); i++) {
        g_assert_cmphex(result[i], ==, 0);
    }
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, channel_mask);
    xdmac_waitl(qts, XDMAC_GS, channel_mask, 0);
    g_assert_true(qtest_readl(qts,
                              SAM9X7_XDMAC_BASE + channel + XDMAC_CIS) &
                  XDMAC_INT_DIS);

    qtest_quit(qts);
}

static void test_xdmac_p2m_flush_nonblocking_disable_order(void)
{
    const uint32_t source = SAM9X7_DDR_BASE + 0x12400;
    const unsigned int index = 5;
    const uint32_t channel_mask = BIT(index);
    const uint64_t channel = XDMAC_CHANNEL(index);
    uint32_t commands[] = {
        cpu_to_le32(PMC_PCR_CMD | 20),
        cpu_to_le32(PMC_PCR_CMD | 20),
        cpu_to_le32(PMC_PCR_CMD | PMC_PCR_EN | 20),
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;
    unsigned int i;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    qtest_memwrite(qts, source, commands, sizeof(commands));

    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA, source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 SAM9X7_PMC_BASE + PMC_PCR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 ARRAY_SIZE(commands));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_SWREQ |
                 XDMAC_CC_DWIDTH_WORD | XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GWS, channel_mask);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, channel_mask);

    /* Stage two words in the FIFO snapshot while RAM writes are suspended. */
    for (i = 0; i < 2; i++) {
        qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
        xdmac_waitl(qts, XDMAC_GSWS, channel_mask, 0);
    }
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CSA),
                    ==, source + 2 * sizeof(uint32_t));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, ARRAY_SIZE(commands));

    /* Queue a third read and a flush while the channel clock is stopped. */
    pmc_write_pcr(qts, 20, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWF, channel_mask);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWS) &
                  channel_mask);

    /*
     * Both operations become runnable together.  Nonblocking flush
     * arbitration accepts the source read before the first snapshot write,
     * whose PCR command gates the channel clock again.
     */
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CSA),
                    ==, source + sizeof(commands));
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, 2);
    g_assert_false(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GSWS) &
                   channel_mask);
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC);
    g_assert_true(value & XDMAC_CC_WRIP);

    /*
     * GD follows an already-scheduled flush burst while pclk is stopped.
     * Resuming lets its last snapshot word raise FIS, then drains the newly
     * read word and completes disable with DIS.
     */
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GD, channel_mask);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) &
                  channel_mask);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, 1);
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CIS);
    g_assert_cmphex(value & (XDMAC_INT_BIS | XDMAC_INT_DIS |
                             XDMAC_INT_FIS | XDMAC_INT_RBEIS |
                             XDMAC_INT_WBEIS | XDMAC_INT_ROIS),
                    ==, XDMAC_INT_FIS);

    /* The final, post-snapshot word drains only after FIS and then sets DIS. */
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    xdmac_waitl(qts, XDMAC_GS, channel_mask, 0);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, 0);
    value = qtest_readl(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CIS);
    g_assert_cmphex(value & (XDMAC_INT_BIS | XDMAC_INT_DIS |
                             XDMAC_INT_FIS | XDMAC_INT_RBEIS |
                             XDMAC_INT_WBEIS | XDMAC_INT_ROIS),
                    ==, XDMAC_INT_DIS);

    qtest_quit(qts);
}

static void test_xdmac_p2m_fifo_migration(void)
{
    static const uint8_t payload[] = {
        0x81, 0x72, 0x63, 0x54, 0x45, 0x36, 0x27, 0x18,
    };
    const uint32_t source = SAM9X7_DDR_BASE + 0x12100;
    const uint32_t target = SAM9X7_DDR_BASE + 0x12200;
    const unsigned int index = 4;
    const uint32_t channel_mask = BIT(index);
    const uint64_t channel = XDMAC_CHANNEL(index);
    const uint32_t config = XDMAC_CC_TYPE_PER |
                            XDMAC_CC_MBSIZE_SIXTEEN |
                            XDMAC_CC_SWREQ | XDMAC_CC_SAM_INC |
                            XDMAC_CC_DAM_INC;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint8_t result[sizeof(payload)];
    unsigned int i;

    ebi_enable_ddr(from);
    pmc_write_pcr(from, 20, PMC_PCR_EN);
    qtest_memwrite(from, source, payload, sizeof(payload));
    qtest_memset(from, target, 0, sizeof(payload));
    qtest_writel(from, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA, source);
    qtest_writel(from, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA, target);
    qtest_writel(from, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 sizeof(payload));
    qtest_writel(from, SAM9X7_XDMAC_BASE + channel + XDMAC_CC, config);
    qtest_writel(from, SAM9X7_XDMAC_BASE + XDMAC_GWS, channel_mask);
    qtest_writel(from, SAM9X7_XDMAC_BASE + XDMAC_GE, channel_mask);
    for (i = 0; i < 3; i++) {
        qtest_writel(from, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
        xdmac_waitl(from, XDMAC_GSWS, channel_mask, 0);
    }

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_true(qtest_readl(to, SAM9X7_XDMAC_BASE + XDMAC_GS) &
                  channel_mask);
    g_assert_true(qtest_readl(to, SAM9X7_XDMAC_BASE + XDMAC_GWS) &
                  channel_mask);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, sizeof(payload));
    qtest_memread(to, target, result, sizeof(result));
    for (i = 0; i < sizeof(result); i++) {
        g_assert_cmphex(result[i], ==, 0);
    }

    qtest_writel(to, SAM9X7_XDMAC_BASE + XDMAC_GSWF, channel_mask);
    xdmac_waitl(to, channel + XDMAC_CIS,
                XDMAC_INT_FIS, XDMAC_INT_FIS);
    g_assert_cmphex(qtest_readl(to,
                                SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC),
                    ==, sizeof(payload) - 3);
    qtest_memread(to, target, result, sizeof(result));
    g_assert_cmpmem(result, 3, payload, 3);

    qtest_writel(to, SAM9X7_XDMAC_BASE + XDMAC_GRWR, channel_mask);
    for (i = 3; i < sizeof(payload); i++) {
        qtest_writel(to, SAM9X7_XDMAC_BASE + XDMAC_GSWR, channel_mask);
        xdmac_waitl(to, XDMAC_GSWS, channel_mask, 0);
    }
    xdmac_waitl(to, XDMAC_GS, channel_mask, 0);
    qtest_memread(to, target, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), payload, sizeof(payload));

    qtest_quit(to);
    qtest_quit(from);
}

static void aes_write_bytes(QTestState *qts, uint64_t offset,
                            const uint8_t *data, size_t length)
{
    size_t i;

    g_assert_cmpuint(length % sizeof(uint32_t), ==, 0);
    for (i = 0; i < length; i += sizeof(uint32_t)) {
        qtest_writel(qts, SAM9X7_AES_BASE + offset + i,
                     ldl_le_p(data + i));
    }
}

static void aes_read_bytes(QTestState *qts, uint64_t offset,
                           uint8_t *data, size_t length)
{
    size_t i;

    g_assert_cmpuint(length % sizeof(uint32_t), ==, 0);
    for (i = 0; i < length; i += sizeof(uint32_t)) {
        stl_le_p(data + i,
                 qtest_readl(qts, SAM9X7_AES_BASE + offset + i));
    }
}

static void aes_configure(QTestState *qts, uint32_t mode,
                          const uint8_t *key, size_t key_length,
                          const uint8_t *iv)
{
    qtest_writel(qts, SAM9X7_AES_BASE + AES_CR, AES_CR_SWRST);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_MR, AES_MR_CKEY | mode);
    aes_write_bytes(qts, AES_KEYWR(0), key, key_length);
    if (iv) {
        aes_write_bytes(qts, AES_IVR(0), iv, 16);
    }
}

static void aes_process_cpu_block(QTestState *qts, const uint8_t input[16],
                                  uint8_t output[16])
{
    aes_write_bytes(qts, AES_IDATAR(0), input, 16);
    g_assert_true(qtest_readl(qts, SAM9X7_AES_BASE + AES_ISR) &
                  AES_INT_DATRDY);
    aes_read_bytes(qts, AES_ODATAR(0), output, 16);
}

static void aes_process_cpu_bytes(QTestState *qts, const uint8_t *input,
                                  uint8_t *output, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++) {
        qtest_writeb(qts, SAM9X7_AES_BASE + AES_IDATAR(0) + i, input[i]);
    }
    g_assert_true(qtest_readl(qts, SAM9X7_AES_BASE + AES_ISR) &
                  AES_INT_DATRDY);
    for (i = 0; i < length; i++) {
        output[i] = qtest_readb(qts,
                                SAM9X7_AES_BASE + AES_ODATAR(0) + i);
    }
}

static void aes_process_dma(QTestState *qts, uint32_t source,
                            uint32_t destination, size_t length)
{
    uint64_t tx_channel = XDMAC_CHANNEL(0);
    uint64_t rx_channel = XDMAC_CHANNEL(1);

    g_assert_cmpuint(length % sizeof(uint32_t), ==, 0);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CSA,
                 SAM9X7_AES_BASE + AES_ODATAR(0));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CDA,
                 destination);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CUBC,
                 length / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(33) |
                 XDMAC_CC_CSIZE_4 | XDMAC_CC_DWIDTH_WORD |
                 XDMAC_CC_DAM_INC);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA, source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_AES_BASE + AES_IDATAR(0));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC,
                 length / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(32) | XDMAC_CC_CSIZE_4 |
                 XDMAC_CC_DWIDTH_WORD | XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(1) | BIT(0));
    xdmac_waitl(qts, XDMAC_GS, BIT(1) | BIT(0), 0);
}

static void test_aes_registers_ecb_irq_and_protection(void)
{
    static const struct {
        uint8_t key[32];
        size_t key_length;
        uint32_t mode;
        uint8_t ciphertext[16];
    } vectors[] = {
        {
            .key = {
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
            },
            .key_length = 16,
            .ciphertext = {
                0x69, 0xc4, 0xe0, 0xd8, 0x6a, 0x7b, 0x04, 0x30,
                0xd8, 0xcd, 0xb7, 0x80, 0x70, 0xb4, 0xc5, 0x5a,
            },
        }, {
            .key = {
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
            },
            .key_length = 24,
            .mode = AES_MR_KEYSIZE_192,
            .ciphertext = {
                0xdd, 0xa9, 0x7c, 0xa4, 0x86, 0x4c, 0xdf, 0xe0,
                0x6e, 0xaf, 0x70, 0xa0, 0xec, 0x0d, 0x71, 0x91,
            },
        }, {
            .key = {
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
            },
            .key_length = 32,
            .mode = AES_MR_KEYSIZE_256,
            .ciphertext = {
                0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
                0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89,
            },
        },
    };
    static const uint8_t plaintext[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    uint8_t result[16];
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t mode;
    uint32_t value;
    unsigned int i;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_AES_BASE + AES_MR), ==,
                    0x00080000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AES_BASE + AES_VERSION), ==,
                    0x700);
    pmc_write_pcr(qts, 39, PMC_PCR_EN);
    aic_configure(qts, 39, AIC_SMR_LEVEL_HIGH | 3, 0x39393939);

    for (i = 0; i < ARRAY_SIZE(vectors); i++) {
        mode = vectors[i].mode | AES_MR_SMOD_AUTO | AES_MR_CIPHER;
        aes_configure(qts, mode, vectors[i].key, vectors[i].key_length,
                      NULL);
        /* Key loading computes the GCM H subkey and reports completion. */
        g_assert_true(qtest_readl(qts, SAM9X7_AES_BASE + AES_ISR) &
                      AES_INT_DATRDY);
        qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
        if (i == 0) {
            qtest_writel(qts, SAM9X7_AES_BASE + AES_IER,
                         AES_INT_DATRDY);
            aes_write_bytes(qts, AES_IDATAR(0), plaintext, 16);
            g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) &
                          BIT(7));
            aes_read_bytes(qts, AES_ODATAR(0), result, 16);
            g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) &
                           BIT(7));
        } else {
            aes_process_cpu_block(qts, plaintext, result);
        }
        g_assert_cmpmem(result, sizeof(result), vectors[i].ciphertext,
                        sizeof(vectors[i].ciphertext));

        aes_configure(qts, vectors[i].mode | AES_MR_SMOD_AUTO,
                      vectors[i].key, vectors[i].key_length, NULL);
        qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
        aes_process_cpu_block(qts, vectors[i].ciphertext, result);
        g_assert_cmpmem(result, sizeof(result), plaintext,
                        sizeof(plaintext));
    }

    mode = qtest_readl(qts, SAM9X7_AES_BASE + AES_MR);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_WPMR,
                 AES_WPMR_KEY | AES_WPMR_WPEN | AES_WPMR_WPITEN |
                 AES_WPMR_WPCREN);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_MR, AES_MR_CKEY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AES_BASE + AES_MR), ==, mode);
    value = qtest_readl(qts, SAM9X7_AES_BASE + AES_WPSR);
    g_assert_cmphex(value & (0xff00 | AES_WPSR_WPVS), ==,
                    (AES_MR << 8) | AES_WPSR_WPVS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AES_BASE + AES_WPSR), ==, 0);

    qtest_writel(qts, SAM9X7_AES_BASE + AES_CR, AES_CR_SWRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AES_BASE + AES_MR), ==, mode);
    value = qtest_readl(qts, SAM9X7_AES_BASE + AES_ISR);
    g_assert_true(value & AES_INT_SECE);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_WPMR, AES_WPMR_KEY);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_CR, AES_CR_SWRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_AES_BASE + AES_MR), ==,
                    0x00080000);

    qtest_quit(qts);
}

static void test_aes_chaining_gcm_and_xdmac(void)
{
    static const uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    static const uint8_t plaintext[32] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    };
    static const uint8_t cbc_ciphertext[32] = {
        0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46,
        0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
        0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee,
        0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
    };
    static const uint8_t ctr_iv[16] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
        0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
    };
    static const uint8_t ctr_ciphertext[32] = {
        0x87, 0x4d, 0x61, 0x91, 0xb6, 0x20, 0xe3, 0x26,
        0x1b, 0xef, 0x68, 0x64, 0x99, 0x0d, 0xb6, 0xce,
        0x98, 0x06, 0xf6, 0x6b, 0x79, 0x70, 0xfd, 0xff,
        0x86, 0x17, 0x18, 0x7b, 0xb9, 0xff, 0xfd, 0xff,
    };
    static const uint8_t zero[16] = { 0 };
    static const uint8_t gcm_iv[16] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2,
    };
    static const uint8_t gcm_ciphertext[16] = {
        0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92,
        0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78,
    };
    static const uint8_t gcm_tag[16] = {
        0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd,
        0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf,
    };
    static const uint8_t gcm_aad_key[16] = {
        0xfe, 0xff, 0xe9, 0x92, 0x86, 0x65, 0x73, 0x1c,
        0x6d, 0x6a, 0x8f, 0x94, 0x67, 0x30, 0x83, 0x08,
    };
    static const uint8_t gcm_aad_iv[16] = {
        0xca, 0xfe, 0xba, 0xbe, 0xfa, 0xce, 0xdb, 0xad,
        0xde, 0xca, 0xf8, 0x88, 0x00, 0x00, 0x00, 0x02,
    };
    static const uint8_t gcm_aad[32] = {
        0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
        0xfe, 0xed, 0xfa, 0xce, 0xde, 0xad, 0xbe, 0xef,
        0xab, 0xad, 0xda, 0xd2,
    };
    static const uint8_t gcm_aad_plaintext[64] = {
        0xd9, 0x31, 0x32, 0x25, 0xf8, 0x84, 0x06, 0xe5,
        0xa5, 0x59, 0x09, 0xc5, 0xaf, 0xf5, 0x26, 0x9a,
        0x86, 0xa7, 0xa9, 0x53, 0x15, 0x34, 0xf7, 0xda,
        0x2e, 0x4c, 0x30, 0x3d, 0x8a, 0x31, 0x8a, 0x72,
        0x1c, 0x3c, 0x0c, 0x95, 0x95, 0x68, 0x09, 0x53,
        0x2f, 0xcf, 0x0e, 0x24, 0x49, 0xa6, 0xb5, 0x25,
        0xb1, 0x6a, 0xed, 0xf5, 0xaa, 0x0d, 0xe6, 0x57,
        0xba, 0x63, 0x7b, 0x39,
    };
    static const uint8_t gcm_aad_ciphertext[60] = {
        0x42, 0x83, 0x1e, 0xc2, 0x21, 0x77, 0x74, 0x24,
        0x4b, 0x72, 0x21, 0xb7, 0x84, 0xd0, 0xd4, 0x9c,
        0xe3, 0xaa, 0x21, 0x2f, 0x2c, 0x02, 0xa4, 0xe0,
        0x35, 0xc1, 0x7e, 0x23, 0x29, 0xac, 0xa1, 0x2e,
        0x21, 0xd5, 0x14, 0xb2, 0x54, 0x66, 0x93, 0x1c,
        0x7d, 0x8f, 0x6a, 0x5a, 0xac, 0x84, 0xaa, 0x05,
        0x1b, 0xa3, 0x0b, 0x39, 0x6a, 0x0a, 0xac, 0x97,
        0x3d, 0x58, 0xe0, 0x91,
    };
    static const uint8_t gcm_aad_tag[16] = {
        0x5b, 0xc9, 0x4f, 0xbc, 0x32, 0x21, 0xa5, 0xdb,
        0x94, 0xfa, 0xe9, 0x5a, 0xe7, 0x12, 0x1a, 0x47,
    };
    const uint32_t dma_src = SAM9X7_DDR_BASE + 0x11800;
    const uint32_t dma_dst = SAM9X7_DDR_BASE + 0x11900;
    uint8_t result[64];
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 39, PMC_PCR_EN);
    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_CBC |
                       AES_MR_CIPHER, key, sizeof(key), iv);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    for (i = 0; i < 2; i++) {
        aes_process_cpu_block(qts, plaintext + i * 16, result + i * 16);
    }
    g_assert_cmpmem(result, sizeof(cbc_ciphertext), cbc_ciphertext,
                    sizeof(cbc_ciphertext));

    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_CTR |
                       AES_MR_CIPHER, key, sizeof(key), ctr_iv);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    for (i = 0; i < 2; i++) {
        aes_process_cpu_block(qts, plaintext + i * 16, result + i * 16);
    }
    g_assert_cmpmem(result, sizeof(ctr_ciphertext), ctr_ciphertext,
                    sizeof(ctr_ciphertext));

    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_GCM |
                       AES_MR_GTAGEN | AES_MR_CIPHER,
                  zero, sizeof(zero), gcm_iv);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_AADLENR, 0);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_CLENR, 16);
    aes_process_cpu_block(qts, zero, result);
    g_assert_cmpmem(result, 16, gcm_ciphertext, sizeof(gcm_ciphertext));
    g_assert_true(qtest_readl(qts, SAM9X7_AES_BASE + AES_ISR) &
                  AES_INT_TAGRDY);
    aes_read_bytes(qts, AES_TAGR(0), result, 16);
    g_assert_cmpmem(result, 16, gcm_tag, sizeof(gcm_tag));

    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    aes_configure(qts, AES_MR_SMOD_DMA | AES_MR_OPMODE_CBC |
                       AES_MR_CIPHER, key, sizeof(key), iv);
    qtest_memwrite(qts, dma_src, plaintext, sizeof(plaintext));
    memset(result, 0, sizeof(result));
    qtest_memwrite(qts, dma_dst, result, sizeof(result));

    aes_process_dma(qts, dma_src, dma_dst, sizeof(plaintext));
    qtest_memread(qts, dma_dst, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(plaintext), cbc_ciphertext,
                    sizeof(cbc_ciphertext));

    /*
     * Match the Linux GCM path: hash padded AAD through IDATAR, preserve the
     * intermediate state while switching SMOD, then use paired DMA channels
     * for a block-padded non-aligned payload.
     */
    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_GCM |
                       AES_MR_GTAGEN | AES_MR_CIPHER,
                  gcm_aad_key, sizeof(gcm_aad_key), gcm_aad_iv);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_AADLENR, 20);
    qtest_writel(qts, SAM9X7_AES_BASE + AES_CLENR, 60);
    for (i = 0; i < 2; i++) {
        aes_process_cpu_block(qts, gcm_aad + i * 16, result);
    }
    qtest_writel(qts, SAM9X7_AES_BASE + AES_MR,
                 AES_MR_SMOD_DMA | AES_MR_OPMODE_GCM |
                 AES_MR_GTAGEN | AES_MR_CIPHER);
    qtest_memwrite(qts, dma_src, gcm_aad_plaintext,
                   sizeof(gcm_aad_plaintext));
    memset(result, 0, sizeof(result));
    qtest_memwrite(qts, dma_dst, result, sizeof(result));
    aes_process_dma(qts, dma_src, dma_dst, sizeof(gcm_aad_plaintext));
    qtest_memread(qts, dma_dst, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(gcm_aad_ciphertext),
                    gcm_aad_ciphertext, sizeof(gcm_aad_ciphertext));
    g_assert_true(qtest_readl(qts, SAM9X7_AES_BASE + AES_ISR) &
                  AES_INT_TAGRDY);
    aes_read_bytes(qts, AES_TAGR(0), result, sizeof(gcm_aad_tag));
    g_assert_cmpmem(result, sizeof(gcm_aad_tag),
                    gcm_aad_tag, sizeof(gcm_aad_tag));

    qtest_quit(qts);
}

static void test_aes_feedback_and_xts(void)
{
    static const uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c,
    };
    static const uint8_t iv[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    };
    static const uint8_t plaintext[32] = {
        0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
        0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
        0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c,
        0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    };
    static const uint8_t ofb_ciphertext[32] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20,
        0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0x77, 0x89, 0x50, 0x8d, 0x16, 0x91, 0x8f, 0x03,
        0xf5, 0x3c, 0x52, 0xda, 0xc5, 0x4e, 0xd8, 0x25,
    };
    static const uint8_t cfb128_ciphertext[32] = {
        0x3b, 0x3f, 0xd9, 0x2e, 0xb7, 0x2d, 0xad, 0x20,
        0x33, 0x34, 0x49, 0xf8, 0xe8, 0x3c, 0xfb, 0x4a,
        0xc8, 0xa6, 0x45, 0x37, 0xa0, 0xb3, 0xa9, 0x3f,
        0xcd, 0xe3, 0xcd, 0xad, 0x9f, 0x1c, 0xe5, 0x8b,
    };
    static const uint8_t cfb8_ciphertext[32] = {
        0x3b, 0x79, 0x42, 0x4c, 0x9c, 0x0d, 0xd4, 0x36,
        0xba, 0xce, 0x9e, 0x0e, 0xd4, 0x58, 0x6a, 0x4f,
        0x32, 0xb9, 0xde, 0xd5, 0x0a, 0xe3, 0xba, 0x69,
        0xd4, 0x72, 0xe8, 0x82, 0x67, 0xfb, 0x50, 0x52,
    };
    static const uint8_t xts_key1[16] = {
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
        0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
    };
    static const uint8_t xts_key2[16] = {
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
        0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,
    };
    static const uint8_t xts_iv[16] = {
        0x33, 0x33, 0x33, 0x33, 0x33,
    };
    static const uint8_t xts_plaintext[32] = {
        [0 ... 31] = 0x44,
    };
    static const uint8_t xts_ciphertext[32] = {
        0xc4, 0x54, 0x18, 0x5e, 0x6a, 0x16, 0x93, 0x6e,
        0x39, 0x33, 0x40, 0x38, 0xac, 0xef, 0x83, 0x8b,
        0xfb, 0x18, 0x6f, 0xff, 0x74, 0x80, 0xad, 0xc4,
        0x28, 0x93, 0x82, 0xec, 0xd6, 0xd3, 0x94, 0xf0,
    };
    uint8_t encrypted_tweak[16];
    uint8_t register_tweak[16];
    uint8_t alpha[16] = { 1 };
    uint8_t result[32];
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    pmc_write_pcr(qts, 39, PMC_PCR_EN);
    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_OFB |
                       AES_MR_CIPHER, key, sizeof(key), iv);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    for (i = 0; i < 2; i++) {
        aes_process_cpu_block(qts, plaintext + i * 16, result + i * 16);
    }
    g_assert_cmpmem(result, sizeof(result), ofb_ciphertext,
                    sizeof(ofb_ciphertext));

    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_CFB |
                       AES_MR_CIPHER, key, sizeof(key), iv);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    for (i = 0; i < 2; i++) {
        aes_process_cpu_block(qts, plaintext + i * 16, result + i * 16);
    }
    g_assert_cmpmem(result, sizeof(result), cfb128_ciphertext,
                    sizeof(cfb128_ciphertext));

    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_CFB |
                       AES_MR_CFBS_8 | AES_MR_CIPHER,
                  key, sizeof(key), iv);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    for (i = 0; i < sizeof(plaintext); i++) {
        aes_process_cpu_bytes(qts, plaintext + i, result + i, 1);
    }
    g_assert_cmpmem(result, sizeof(result), cfb8_ciphertext,
                    sizeof(cfb8_ciphertext));

    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_CFB |
                       AES_MR_CFBS_8, key, sizeof(key), iv);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    for (i = 0; i < sizeof(plaintext); i++) {
        aes_process_cpu_bytes(qts, cfb8_ciphertext + i, result + i, 1);
    }
    g_assert_cmpmem(result, sizeof(result), plaintext, sizeof(plaintext));

    /* Follow the Linux driver's two-key XTS setup, including its byte swap. */
    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_CIPHER,
                  xts_key2, sizeof(xts_key2), NULL);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    aes_process_cpu_block(qts, xts_iv, encrypted_tweak);
    for (i = 0; i < sizeof(register_tweak); i++) {
        register_tweak[i] = encrypted_tweak[15 - i];
    }

    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_XTS |
                       AES_MR_CIPHER, xts_key1, sizeof(xts_key1), NULL);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    aes_write_bytes(qts, AES_TWR(0), register_tweak,
                    sizeof(register_tweak));
    aes_write_bytes(qts, AES_ALPHAR(0), alpha, sizeof(alpha));
    qtest_writel(qts, SAM9X7_AES_BASE + AES_BCNT,
                 sizeof(xts_plaintext));
    for (i = 0; i < 2; i++) {
        aes_process_cpu_block(qts, xts_plaintext + i * 16,
                              result + i * 16);
    }
    g_assert_cmpmem(result, sizeof(result), xts_ciphertext,
                    sizeof(xts_ciphertext));

    aes_configure(qts, AES_MR_SMOD_AUTO | AES_MR_OPMODE_XTS,
                  xts_key1, sizeof(xts_key1), NULL);
    qtest_readl(qts, SAM9X7_AES_BASE + AES_ODATAR(0));
    aes_write_bytes(qts, AES_TWR(0), register_tweak,
                    sizeof(register_tweak));
    aes_write_bytes(qts, AES_ALPHAR(0), alpha, sizeof(alpha));
    qtest_writel(qts, SAM9X7_AES_BASE + AES_BCNT,
                 sizeof(xts_ciphertext));
    for (i = 0; i < 2; i++) {
        aes_process_cpu_block(qts, xts_ciphertext + i * 16,
                              result + i * 16);
    }
    g_assert_cmpmem(result, sizeof(result), xts_plaintext,
                    sizeof(xts_plaintext));

    qtest_quit(qts);
}

static void tdes_write_words(QTestState *qts, uint64_t offset,
                             const uint8_t *data, size_t length)
{
    size_t i;

    g_assert_cmpuint(length % sizeof(uint32_t), ==, 0);
    for (i = 0; i < length; i += sizeof(uint32_t)) {
        qtest_writel(qts, SAM9X7_TDES_BASE + offset + i,
                     ldl_le_p(data + i));
    }
}

static void tdes_read_words(QTestState *qts, uint64_t offset,
                            uint8_t *data, size_t length)
{
    size_t i;

    g_assert_cmpuint(length % sizeof(uint32_t), ==, 0);
    for (i = 0; i < length; i += sizeof(uint32_t)) {
        stl_le_p(data + i,
                 qtest_readl(qts, SAM9X7_TDES_BASE + offset + i));
    }
}

static uint64_t tdes_duration(QTestState *qts, unsigned int cycles)
{
    uint64_t period = get_clock_period(qts, "/machine/soc/pmc/pclk[40]");

    g_assert_cmpuint(period, !=, 0);
    return (period * cycles) >> 32;
}

static void tdes_configure(QTestState *qts, uint32_t mode,
                           const uint8_t *key, size_t key_length,
                           const uint8_t *iv)
{
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_CR, TDES_CR_SWRST);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_MR, mode);
    tdes_write_words(qts, TDES_KEYWR(0), key, key_length);
    if (iv) {
        tdes_write_words(qts, TDES_IVR(0), iv, 8);
    }
}

static void tdes_write_segment(QTestState *qts, const uint8_t *input,
                               size_t length)
{
    switch (length) {
    case 1:
        qtest_writeb(qts, SAM9X7_TDES_BASE + TDES_IDATAR(0), input[0]);
        break;
    case 2:
        qtest_writew(qts, SAM9X7_TDES_BASE + TDES_IDATAR(0),
                     lduw_le_p(input));
        break;
    case 4:
        qtest_writel(qts, SAM9X7_TDES_BASE + TDES_IDATAR(0),
                     ldl_le_p(input));
        break;
    case 8:
        tdes_write_words(qts, TDES_IDATAR(0), input, length);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tdes_read_segment(QTestState *qts, uint8_t *output,
                              size_t length)
{
    switch (length) {
    case 1:
        output[0] = qtest_readb(qts,
                                SAM9X7_TDES_BASE + TDES_ODATAR(0));
        break;
    case 2:
        stw_le_p(output, qtest_readw(qts,
                                     SAM9X7_TDES_BASE + TDES_ODATAR(0)));
        break;
    case 4:
        stl_le_p(output, qtest_readl(qts,
                                     SAM9X7_TDES_BASE + TDES_ODATAR(0)));
        break;
    case 8:
        tdes_read_words(qts, TDES_ODATAR(0), output, length);
        break;
    default:
        g_assert_not_reached();
    }
}

static void tdes_process_cpu(QTestState *qts, const uint8_t *input,
                             uint8_t *output, size_t length,
                             uint64_t duration)
{
    tdes_write_segment(qts, input, length);
    qtest_clock_step(qts, duration);
    g_assert_true(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                  TDES_INT_DATRDY);
    tdes_read_segment(qts, output, length);
}

static void test_tdes_vectors_timing_irq_and_protection(void)
{
    static const uint8_t des_key[8] = {
        0x13, 0x34, 0x57, 0x79, 0x9b, 0xbc, 0xdf, 0xf1,
    };
    static const uint8_t des_plaintext[8] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    };
    static const uint8_t des_ciphertext[8] = {
        0x85, 0xe8, 0x13, 0x54, 0x0f, 0x0a, 0xb4, 0x05,
    };
    static const uint8_t tdes_key[24] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    };
    static const uint8_t tdes_plaintext[8] = {
        0x73, 0x6f, 0x6d, 0x65, 0x64, 0x61, 0x74, 0x61,
    };
    static const uint8_t tdes_ciphertext[8] = {
        0x18, 0xd7, 0x48, 0xe5, 0x63, 0x62, 0x05, 0x72,
    };
    static const uint8_t two_key[16] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
    };
    static const uint8_t two_key_plaintext[8] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    };
    static const uint8_t two_key_ciphertext[8] = {
        0x31, 0xa7, 0x36, 0x4c, 0xac, 0x91, 0xca, 0x39,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t result[8];
    uint64_t duration;
    uint32_t status;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_MR), ==, 2);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_VERSION), ==,
                    0x700);

    aic_configure(qts, 40, AIC_SMR_LEVEL_HIGH | 3, 0x40404040);
    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_CIPHER,
                   des_key, sizeof(des_key), NULL);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_IER, TDES_INT_DATRDY);
    tdes_write_segment(qts, des_plaintext, sizeof(des_plaintext));

    /* A transaction already accepted by TDES remains frozen while gated. */
    qtest_clock_step(qts, 1000000);
    g_assert_false(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                   TDES_INT_DATRDY);
    pmc_write_pcr(qts, 40, PMC_PCR_EN);
    duration = tdes_duration(qts, 18);
    g_assert_cmpuint(duration, >, 1);
    qtest_clock_step(qts, duration - 1);
    g_assert_false(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                   TDES_INT_DATRDY);
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(8));
    tdes_read_segment(qts, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), des_ciphertext,
                    sizeof(des_ciphertext));
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(8));

    tdes_configure(qts, TDES_MR_SMOD_AUTO,
                   des_key, sizeof(des_key), NULL);
    tdes_process_cpu(qts, des_ciphertext, result, sizeof(result), duration);
    g_assert_cmpmem(result, sizeof(result), des_plaintext,
                    sizeof(des_plaintext));

    duration = tdes_duration(qts, 50);
    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_TDES |
                        TDES_MR_CIPHER,
                   tdes_key, sizeof(tdes_key), NULL);
    tdes_write_segment(qts, tdes_plaintext, sizeof(tdes_plaintext));
    qtest_clock_step(qts, duration - 1);
    g_assert_false(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                   TDES_INT_DATRDY);
    qtest_clock_step(qts, 1);
    tdes_read_segment(qts, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), tdes_ciphertext,
                    sizeof(tdes_ciphertext));

    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_TDES |
                        TDES_MR_KEYMOD_2KEY | TDES_MR_CIPHER,
                   two_key, sizeof(two_key), NULL);
    tdes_process_cpu(qts, two_key_plaintext, result, sizeof(result),
                     duration);
    g_assert_cmpmem(result, sizeof(result), two_key_ciphertext,
                    sizeof(two_key_ciphertext));

    duration = tdes_duration(qts, 18);
    tdes_configure(qts, TDES_MR_CIPHER,
                   des_key, sizeof(des_key), NULL);
    tdes_write_segment(qts, des_plaintext, sizeof(des_plaintext));
    qtest_clock_step(qts, duration);
    g_assert_false(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                   TDES_INT_DATRDY);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_CR, TDES_CR_START);
    qtest_clock_step(qts, duration);
    tdes_read_segment(qts, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), des_ciphertext,
                    sizeof(des_ciphertext));

    /* A configured software-event action locks new work until UNLOCK. */
    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_CIPHER,
                   des_key, sizeof(des_key), NULL);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_WPMR,
                 TDES_WPMR_KEY | TDES_WPMR_ACTION_LOCK);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_CR), ==, 0);
    status = qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR);
    g_assert_cmphex(status & (TDES_INT_URAD | TDES_INT_SECE), ==,
                    TDES_INT_URAD | TDES_INT_SECE);
    status = qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPSR);
    g_assert_cmphex(status & (TDES_WPSR_SWETYP(0xf) | TDES_WPSR_SWE), ==,
                    TDES_WPSR_SWETYP(TDES_SWE_READ_WO) |
                    TDES_WPSR_SWE);
    tdes_write_segment(qts, des_plaintext, sizeof(des_plaintext));
    qtest_clock_step(qts, duration);
    g_assert_false(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                   TDES_INT_DATRDY);
    status = qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPSR);
    g_assert_cmphex(status & (TDES_WPSR_SWETYP(0xf) | TDES_WPSR_SWE), ==,
                    TDES_WPSR_SWETYP(TDES_SWE_WEIRD_ACTION) |
                    TDES_WPSR_SWE);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_CR, TDES_CR_UNLOCK);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_WPMR, TDES_WPMR_KEY);
    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_CIPHER,
                   des_key, sizeof(des_key), NULL);
    tdes_process_cpu(qts, des_plaintext, result, sizeof(result), duration);
    g_assert_cmpmem(result, sizeof(result), des_ciphertext,
                    sizeof(des_ciphertext));

    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_CR, TDES_CR_SWRST);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_MR,
                 TDES_MR_SMOD_AUTO | TDES_MR_CIPHER);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_KEYWR(0),
                 ldl_le_p(des_key));
    tdes_write_segment(qts, des_plaintext, sizeof(des_plaintext));
    status = qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPSR);
    g_assert_cmphex(status, ==,
                    TDES_WPSR_ECLASS |
                    TDES_WPSR_SWETYP(TDES_SWE_INCOMPLETE_KEY) |
                    TDES_WPSR_WPVSRC(TDES_CR) | TDES_WPSR_SWE);
    g_assert_true(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                  TDES_INT_SECE);

    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_CR, TDES_CR_SWRST);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_WPMR,
                 TDES_WPMR_KEY | TDES_WPMR_WPEN |
                 TDES_WPMR_WPITEN | TDES_WPMR_WPCREN);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_MR), ==, 2);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPSR), ==,
                    TDES_WPSR_WPVSRC(TDES_MR) | TDES_WPSR_WPVS);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_IER, TDES_INT_DATRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPSR), ==,
                    TDES_WPSR_WPVSRC(TDES_IER) | TDES_WPSR_WPVS);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_CR, TDES_CR_SWRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_MR), ==, 2);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPSR), ==,
                    TDES_WPSR_WPVSRC(TDES_CR) | TDES_WPSR_WPVS);

    qtest_quit(qts);
}

static void test_tdes_chaining_feedback_and_xtea(void)
{
    static const uint8_t key[24] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
        0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67,
    };
    static const uint8_t iv[8] = {
        0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
    };
    static const uint8_t plaintext[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    static const uint8_t cbc_ciphertext[16] = {
        0xdf, 0x1c, 0x83, 0x92, 0x4e, 0x00, 0xa5, 0x18,
        0x22, 0x34, 0xbf, 0xd6, 0xb2, 0x7d, 0x2d, 0xd7,
    };
    static const uint8_t ofb_ciphertext[16] = {
        0x8e, 0xbf, 0x93, 0xd9, 0x3b, 0x72, 0x33, 0xd2,
        0xbe, 0xe7, 0x02, 0xc4, 0xe7, 0x1f, 0x32, 0x0e,
    };
    static const uint8_t cfb_ciphertext[4][16] = {
        {
            0x8e, 0xbf, 0x93, 0xd9, 0x3b, 0x72, 0x33, 0xd2,
            0xf5, 0x03, 0x0c, 0x62, 0x3d, 0x7e, 0x9d, 0xc6,
        }, {
            0x8e, 0xbf, 0x93, 0xd9, 0x15, 0xb3, 0xc7, 0xa2,
            0x73, 0xfa, 0x90, 0xc0, 0xd0, 0xc5, 0x2b, 0x1a,
        }, {
            0x8e, 0xbf, 0xa9, 0xa3, 0x63, 0xdc, 0x28, 0x68,
            0x54, 0x95, 0x72, 0x1d, 0xb7, 0x08, 0x56, 0x24,
        }, {
            0x8e, 0x45, 0x4b, 0x5c, 0x83, 0xe1, 0x8f, 0x69,
            0x5f, 0x79, 0x26, 0xec, 0xa3, 0x16, 0x02, 0xd5,
        },
    };
    static const uint32_t cfb_mode[4] = {
        0, TDES_MR_CFBS_32, TDES_MR_CFBS_16, TDES_MR_CFBS_8,
    };
    static const size_t cfb_size[4] = { 8, 4, 2, 1 };
    static const uint8_t zero_key[16] = { 0 };
    static const uint8_t zero[8] = { 0 };
    static const uint8_t xtea_ciphertext[8] = {
        0xd8, 0xd4, 0xe9, 0xde, 0xd9, 0x1e, 0x13, 0xf7,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t result[16];
    uint64_t duration;
    unsigned int i;
    unsigned int j;

    pmc_write_pcr(qts, 40, PMC_PCR_EN);
    duration = tdes_duration(qts, 50);

    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_TDES |
                        TDES_MR_OPMODE_CBC | TDES_MR_CIPHER,
                   key, sizeof(key), iv);
    for (i = 0; i < 2; i++) {
        tdes_process_cpu(qts, plaintext + i * 8, result + i * 8, 8,
                         duration);
    }
    g_assert_cmpmem(result, sizeof(result), cbc_ciphertext,
                    sizeof(cbc_ciphertext));

    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_TDES |
                        TDES_MR_OPMODE_CBC,
                   key, sizeof(key), iv);
    for (i = 0; i < 2; i++) {
        tdes_process_cpu(qts, cbc_ciphertext + i * 8, result + i * 8, 8,
                         duration);
    }
    g_assert_cmpmem(result, sizeof(result), plaintext, sizeof(plaintext));

    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_TDES |
                        TDES_MR_OPMODE_OFB | TDES_MR_CIPHER,
                   key, sizeof(key), iv);
    for (i = 0; i < 2; i++) {
        tdes_process_cpu(qts, plaintext + i * 8, result + i * 8, 8,
                         duration);
    }
    g_assert_cmpmem(result, sizeof(result), ofb_ciphertext,
                    sizeof(ofb_ciphertext));

    for (i = 0; i < G_N_ELEMENTS(cfb_size); i++) {
        tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_TDES |
                            TDES_MR_OPMODE_CFB | cfb_mode[i] |
                            TDES_MR_CIPHER,
                       key, sizeof(key), iv);
        for (j = 0; j < sizeof(plaintext); j += cfb_size[i]) {
            tdes_process_cpu(qts, plaintext + j, result + j, cfb_size[i],
                             duration);
        }
        g_assert_cmpmem(result, sizeof(result), cfb_ciphertext[i],
                        sizeof(cfb_ciphertext[i]));

        tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_TDES |
                            TDES_MR_OPMODE_CFB | cfb_mode[i],
                       key, sizeof(key), iv);
        for (j = 0; j < sizeof(plaintext); j += cfb_size[i]) {
            tdes_process_cpu(qts, cfb_ciphertext[i] + j, result + j,
                             cfb_size[i], duration);
        }
        g_assert_cmpmem(result, sizeof(result), plaintext,
                        sizeof(plaintext));
    }

    /* XTEA uses 32-bit little-endian register words; validate 32 rounds. */
    duration = tdes_duration(qts, 66);
    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_XTEA |
                        TDES_MR_CIPHER,
                   zero_key, sizeof(zero_key), NULL);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_XTEA_RNDR, 31);
    tdes_process_cpu(qts, zero, result, sizeof(zero), duration);
    g_assert_cmpmem(result, sizeof(zero), xtea_ciphertext,
                    sizeof(xtea_ciphertext));

    tdes_configure(qts, TDES_MR_SMOD_AUTO | TDES_MR_ALGO_XTEA,
                   zero_key, sizeof(zero_key), NULL);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_XTEA_RNDR, 31);
    tdes_process_cpu(qts, xtea_ciphertext, result, sizeof(zero), duration);
    g_assert_cmpmem(result, sizeof(zero), zero, sizeof(zero));

    qtest_quit(qts);
}

static void tdes_process_dma(QTestState *qts, uint32_t source,
                             uint32_t destination, size_t length,
                             bool receive)
{
    uint64_t tx_channel = XDMAC_CHANNEL(0);
    uint64_t rx_channel = XDMAC_CHANNEL(1);
    uint64_t duration = tdes_duration(qts, 50);
    uint32_t channels = BIT(0);
    unsigned int blocks;
    unsigned int i;

    g_assert_cmpuint(length % sizeof(uint32_t), ==, 0);
    g_assert_cmpuint(length % 8, ==, 0);
    blocks = length / 8;
    if (receive) {
        qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CSA,
                     SAM9X7_TDES_BASE + TDES_ODATAR(0));
        qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CDA,
                     destination);
        qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CUBC,
                     length / sizeof(uint32_t));
        qtest_writel(qts, SAM9X7_XDMAC_BASE + rx_channel + XDMAC_CC,
                     XDMAC_CC_TYPE_PER | XDMAC_CC_PERID(30) |
                     XDMAC_CC_DWIDTH_WORD | XDMAC_CC_DAM_INC);
        channels |= BIT(1);
    }

    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CSA, source);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CDA,
                 SAM9X7_TDES_BASE + TDES_IDATAR(0));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CUBC,
                 length / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + tx_channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(31) | XDMAC_CC_DWIDTH_WORD |
                 XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, channels);

    /*
     * Let each completed cipher block generate the next pair of DMA
     * handshakes.  With LOD, leave the final block in flight so its DATRDY
     * timing can be checked independently below.
     */
    for (i = 0; i < blocks - !receive; i++) {
        qtest_clock_step(qts, duration);
    }
    xdmac_waitl(qts, XDMAC_GS, channels, 0);
}

static void test_tdes_xdmac_and_last_output(void)
{
    static const uint8_t key[24] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10,
        0x89, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67,
    };
    static const uint8_t iv[8] = {
        0x12, 0x34, 0x56, 0x78, 0x90, 0xab, 0xcd, 0xef,
    };
    static const uint8_t plaintext[16] = {
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    };
    static const uint8_t ciphertext[16] = {
        0xdf, 0x1c, 0x83, 0x92, 0x4e, 0x00, 0xa5, 0x18,
        0x22, 0x34, 0xbf, 0xd6, 0xb2, 0x7d, 0x2d, 0xd7,
    };
    const uint32_t dma_src = SAM9X7_DDR_BASE + 0x11a00;
    const uint32_t dma_dst = SAM9X7_DDR_BASE + 0x11b00;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t result[16] = { 0 };
    uint64_t duration;

    ebi_enable_ddr(qts);
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 40, PMC_PCR_EN);
    duration = tdes_duration(qts, 50);

    qtest_memwrite(qts, dma_src, plaintext, sizeof(plaintext));
    qtest_memwrite(qts, dma_dst, result, sizeof(result));
    tdes_configure(qts, TDES_MR_SMOD_DMA | TDES_MR_ALGO_TDES |
                        TDES_MR_OPMODE_CBC | TDES_MR_CIPHER,
                   key, sizeof(key), iv);
    tdes_process_dma(qts, dma_src, dma_dst, sizeof(plaintext), true);
    qtest_memread(qts, dma_dst, result, sizeof(result));
    g_assert_cmpmem(result, sizeof(result), ciphertext, sizeof(ciphertext));

    /* CBC-MAC uses only TX DMA and leaves the final block in ODATAR. */
    tdes_configure(qts, TDES_MR_SMOD_DMA | TDES_MR_ALGO_TDES |
                        TDES_MR_OPMODE_CBC | TDES_MR_LOD |
                        TDES_MR_CIPHER,
                   key, sizeof(key), iv);
    tdes_process_dma(qts, dma_src, 0, sizeof(plaintext), false);
    g_assert_false(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                   TDES_INT_DATRDY);
    qtest_clock_step(qts, duration - 1);
    g_assert_false(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                   TDES_INT_DATRDY);
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_ISR) &
                  TDES_INT_DATRDY);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_TDES_BASE + TDES_ODATAR(0)), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TDES_BASE + TDES_WPSR), ==,
                    TDES_WPSR_SWETYP(TDES_SWE_WEIRD_ACTION) |
                    TDES_WPSR_WPVSRC(TDES_ODATAR(0)) | TDES_WPSR_SWE);
    qtest_writel(qts, SAM9X7_TDES_BASE + TDES_MR,
                 TDES_MR_ALGO_TDES | TDES_MR_OPMODE_CBC |
                 TDES_MR_LOD | TDES_MR_CIPHER);
    tdes_read_segment(qts, result, 8);
    g_assert_cmpmem(result, 8, ciphertext + 8, 8);

    qtest_quit(qts);
}

static unsigned int sha_processing_cycles(unsigned int algorithm)
{
    switch (algorithm) {
    case SHA_ALGO_SHA1:
        return 87;
    case SHA_ALGO_SHA384:
    case SHA_ALGO_SHA512:
        return 90;
    default:
        return 74;
    }
}

static void sha_write_bytes(QTestState *qts, uint64_t offset,
                            const uint8_t *data, size_t length)
{
    size_t i;

    for (i = 0; i < length; i += sizeof(uint32_t)) {
        uint8_t word[sizeof(uint32_t)] = { 0 };
        size_t count = MIN(sizeof(word), length - i);

        memcpy(word, data + i, count);
        qtest_writel(qts, SAM9X7_SHA_BASE + offset,
                     ldl_le_p(word));
        offset += sizeof(uint32_t);
    }
}

static void sha_read_digest(QTestState *qts, uint8_t *digest, size_t length)
{
    size_t i;

    g_assert_cmpuint(length % sizeof(uint32_t), ==, 0);
    for (i = 0; i < length; i += sizeof(uint32_t)) {
        stl_le_p(digest + i,
                 qtest_readl(qts, SAM9X7_SHA_BASE + SHA_IODATAR(i / 4)));
    }
}

static uint64_t sha_duration(QTestState *qts, unsigned int algorithm)
{
    uint64_t period = get_clock_period(qts, "/machine/soc/pmc/pclk[41]");

    g_assert_cmpuint(period, !=, 0);
    return (period * sha_processing_cycles(algorithm)) >> 32;
}

static void sha_start_auto(QTestState *qts, unsigned int algorithm,
                           uint32_t extra_mode, const uint8_t *message,
                           size_t length)
{
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_SWRST);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MR,
                 SHA_MR_SMOD_AUTO | SHA_MR_ALGO(algorithm) | extra_mode);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MSR, length);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_BCR, length);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_IER, SHA_INT_DATRDY);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_FIRST);
    sha_write_bytes(qts, SHA_IDATAR(0), message, length);
}

static void test_sha_vectors_timing_irq_and_protection(void)
{
    static const struct {
        unsigned int algorithm;
        size_t digest_length;
        uint8_t digest[64];
    } vectors[] = {
        {
            .algorithm = SHA_ALGO_SHA1,
            .digest_length = 20,
            .digest = {
                0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a,
                0xba, 0x3e, 0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c,
                0x9c, 0xd0, 0xd8, 0x9d,
            },
        }, {
            .algorithm = SHA_ALGO_SHA224,
            .digest_length = 28,
            .digest = {
                0x23, 0x09, 0x7d, 0x22, 0x34, 0x05, 0xd8, 0x22,
                0x86, 0x42, 0xa4, 0x77, 0xbd, 0xa2, 0x55, 0xb3,
                0x2a, 0xad, 0xbc, 0xe4, 0xbd, 0xa0, 0xb3, 0xf7,
                0xe3, 0x6c, 0x9d, 0xa7,
            },
        }, {
            .algorithm = SHA_ALGO_SHA256,
            .digest_length = 32,
            .digest = {
                0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
                0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
                0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
                0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
            },
        }, {
            .algorithm = SHA_ALGO_SHA384,
            .digest_length = 48,
            .digest = {
                0xcb, 0x00, 0x75, 0x3f, 0x45, 0xa3, 0x5e, 0x8b,
                0xb5, 0xa0, 0x3d, 0x69, 0x9a, 0xc6, 0x50, 0x07,
                0x27, 0x2c, 0x32, 0xab, 0x0e, 0xde, 0xd1, 0x63,
                0x1a, 0x8b, 0x60, 0x5a, 0x43, 0xff, 0x5b, 0xed,
                0x80, 0x86, 0x07, 0x2b, 0xa1, 0xe7, 0xcc, 0x23,
                0x58, 0xba, 0xec, 0xa1, 0x34, 0xc8, 0x25, 0xa7,
            },
        }, {
            .algorithm = SHA_ALGO_SHA512,
            .digest_length = 64,
            .digest = {
                0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba,
                0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
                0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2,
                0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
                0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8,
                0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
                0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e,
                0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f,
            },
        },
    };
    static const uint8_t message[] = { 'a', 'b', 'c' };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t digest[64];
    unsigned int i;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_MR), ==, 0x100);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_VERSION), ==,
                    0x700);

    pmc_write_pcr(qts, 41, PMC_PCR_EN);
    aic_configure(qts, 41, AIC_SMR_LEVEL_HIGH | 3, 0x41414141);

    for (i = 0; i < G_N_ELEMENTS(vectors); i++) {
        uint64_t duration = sha_duration(qts, vectors[i].algorithm);

        g_assert_cmpuint(duration, >, 1);
        sha_start_auto(qts, vectors[i].algorithm, 0,
                       message, sizeof(message));
        qtest_clock_step(qts, duration - 1);
        g_assert_false(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_ISR) &
                       SHA_INT_DATRDY);
        qtest_clock_step(qts, 1);
        g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(9));
        g_assert_true(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_ISR) &
                      SHA_INT_DATRDY);
        memset(digest, 0, sizeof(digest));
        sha_read_digest(qts, digest, vectors[i].digest_length);
        g_assert_cmpmem(digest, vectors[i].digest_length,
                        vectors[i].digest, vectors[i].digest_length);
        g_assert_false(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_ISR) &
                       SHA_INT_DATRDY);
    }

    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_SWRST);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_WPMR,
                 SHA_WPMR_KEY | SHA_WPMR_WPEN | SHA_WPMR_WPITEN |
                 SHA_WPMR_WPCREN);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_MR), ==, 0x100);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_WPSR), ==,
                    SHA_WPSR_WPVSRC(SHA_MR) | SHA_WPSR_WPVS);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_IER, SHA_INT_DATRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_WPSR), ==,
                    SHA_WPSR_WPVSRC(SHA_IER) | SHA_WPSR_WPVS);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_FIRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_WPSR), ==,
                    SHA_WPSR_WPVSRC(SHA_CR) | SHA_WPSR_WPVS);

    qtest_quit(qts);
}

static void sha_process_unpadded_sha256_block(QTestState *qts,
                                               const uint8_t block[64],
                                               uint32_t state[8])
{
    uint64_t duration = sha_duration(qts, SHA_ALGO_SHA256);
    unsigned int i;

    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_SWRST);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MR,
                 SHA_MR_SMOD_AUTO | SHA_MR_ALGO(SHA_ALGO_SHA256));
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_FIRST);
    sha_write_bytes(qts, SHA_IDATAR(0), block, 64);
    qtest_clock_step(qts, duration);
    g_assert_true(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_ISR) &
                  SHA_INT_DATRDY);
    for (i = 0; i < 8; i++) {
        state[i] = qtest_readl(qts,
                               SAM9X7_SHA_BASE + SHA_IODATAR(i));
    }
}

static void sha_load_ir(QTestState *qts, uint32_t command,
                        const uint32_t state[8])
{
    unsigned int i;

    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, command);
    for (i = 0; i < 8; i++) {
        qtest_writel(qts, SAM9X7_SHA_BASE + SHA_IDATAR(i), state[i]);
    }
}

static void test_sha_hmac_check_and_manual_padding(void)
{
    static const uint8_t sha256_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    static const uint8_t hmac_sha256[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
    };
    static const uint8_t hmac_message[] = "Hi There";
    uint8_t ipad[64];
    uint8_t opad[64];
    uint8_t padded[64] = { 'a', 'b', 'c', 0x80 };
    uint8_t digest[32];
    uint32_t ipad_state[8];
    uint32_t opad_state[8];
    uint32_t expected_state[8];
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t duration;
    uint32_t status;
    unsigned int i;

    pmc_write_pcr(qts, 41, PMC_PCR_EN);
    duration = sha_duration(qts, SHA_ALGO_SHA256);

    memset(ipad, 0x36, sizeof(ipad));
    memset(opad, 0x5c, sizeof(opad));
    for (i = 0; i < 20; i++) {
        ipad[i] ^= 0x0b;
        opad[i] ^= 0x0b;
    }
    sha_process_unpadded_sha256_block(qts, ipad, ipad_state);
    sha_process_unpadded_sha256_block(qts, opad, opad_state);

    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_SWRST);
    sha_load_ir(qts, SHA_CR_WUIHV, ipad_state);
    sha_load_ir(qts, SHA_CR_WUIEHV, opad_state);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MR,
                 SHA_MR_SMOD_AUTO | SHA_MR_ALGO(SHA_ALGO_HMAC_SHA256));
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MSR,
                 sizeof(hmac_message) - 1);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_BCR,
                 sizeof(hmac_message) - 1);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_FIRST);
    sha_write_bytes(qts, SHA_IDATAR(0), hmac_message,
                    sizeof(hmac_message) - 1);
    qtest_clock_step(qts, 2 * duration);
    sha_read_digest(qts, digest, sizeof(digest));
    g_assert_cmpmem(digest, sizeof(digest), hmac_sha256,
                    sizeof(hmac_sha256));

    /* Exercise the software-padded block path used by the Linux driver. */
    padded[63] = 24;
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_SWRST);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MR,
                 SHA_MR_SMOD_AUTO | SHA_MR_ALGO(SHA_ALGO_SHA256));
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_FIRST);
    sha_write_bytes(qts, SHA_IDATAR(0), padded, sizeof(padded));
    qtest_clock_step(qts, duration);
    sha_read_digest(qts, digest, sizeof(digest));
    g_assert_cmpmem(digest, sizeof(digest), sha256_abc,
                    sizeof(sha256_abc));

    for (i = 0; i < 8; i++) {
        expected_state[i] = ldl_le_p(sha256_abc + i * 4);
    }
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_SWRST);
    sha_load_ir(qts, SHA_CR_WUIEHV, expected_state);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MR,
                 SHA_MR_SMOD_AUTO | SHA_MR_ALGO(SHA_ALGO_SHA256) |
                 SHA_MR_CHECK_EHV);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MSR, 3);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_BCR, 3);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_FIRST);
    sha_write_bytes(qts, SHA_IDATAR(0), (const uint8_t *)"abc", 3);
    qtest_clock_step(qts, duration);
    status = qtest_readl(qts, SAM9X7_SHA_BASE + SHA_ISR);
    g_assert_cmphex(status & (SHA_INT_DATRDY | SHA_INT_CHECKF |
                             SHA_ISR_CHKST_OK), ==,
                    SHA_INT_DATRDY | SHA_INT_CHECKF | SHA_ISR_CHKST_OK);
    sha_read_digest(qts, digest, sizeof(digest));

    sha_start_auto(qts, SHA_ALGO_SHA256, SHA_MR_CHECK_MESSAGE,
                   (const uint8_t *)"abc", 3);
    qtest_clock_step(qts, duration);
    status = qtest_readl(qts, SAM9X7_SHA_BASE + SHA_ISR);
    g_assert_true(status & SHA_INT_DATRDY);
    g_assert_false(status & SHA_INT_CHECKF);
    for (i = 0; i < 8; i++) {
        qtest_writel(qts, SAM9X7_SHA_BASE + SHA_IDATAR(0),
                     expected_state[i]);
    }
    status = qtest_readl(qts, SAM9X7_SHA_BASE + SHA_ISR);
    g_assert_cmphex(status & (SHA_INT_CHECKF | SHA_ISR_CHKST_OK), ==,
                    SHA_INT_CHECKF | SHA_ISR_CHKST_OK);

    qtest_quit(qts);
}

static void test_sha_xdmac_auto_padding(void)
{
    static const uint8_t expected[32] = {
        0x47, 0x1f, 0xb9, 0x43, 0xaa, 0x23, 0xc5, 0x11,
        0xf6, 0xf7, 0x2f, 0x8d, 0x16, 0x52, 0xd9, 0xc8,
        0x80, 0xcf, 0xa3, 0x92, 0xad, 0x80, 0x50, 0x31,
        0x20, 0x54, 0x77, 0x03, 0xe5, 0x6a, 0x2b, 0xe5,
    };
    const uint32_t source_address = SAM9X7_DDR_BASE + 0x12000;
    uint8_t source[128];
    uint8_t digest[32];
    uint64_t channel = XDMAC_CHANNEL(0);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint64_t duration;
    unsigned int i;

    ebi_enable_ddr(qts);
    for (i = 0; i < sizeof(source); i++) {
        source[i] = i;
    }
    qtest_memwrite(qts, source_address, source, sizeof(source));
    pmc_write_pcr(qts, 20, PMC_PCR_EN);
    pmc_write_pcr(qts, 41, PMC_PCR_EN);
    duration = sha_duration(qts, SHA_ALGO_SHA256);
    aic_configure(qts, 41, AIC_SMR_LEVEL_HIGH | 3, 0x41414141);

    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_SWRST);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MR,
                 SHA_MR_SMOD_DMA | SHA_MR_ALGO(SHA_ALGO_SHA256));
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_MSR, sizeof(source));
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_BCR, sizeof(source));
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_IER, SHA_INT_DATRDY);
    qtest_writel(qts, SAM9X7_SHA_BASE + SHA_CR, SHA_CR_FIRST);

    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA,
                 source_address);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 SAM9X7_SHA_BASE + SHA_IDATAR(0));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC,
                 sizeof(source) / sizeof(uint32_t));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER | XDMAC_CC_DSYNC_MEM2PER |
                 XDMAC_CC_PERID(34) | XDMAC_CC_CSIZE_16 |
                 XDMAC_CC_DWIDTH_WORD | XDMAC_CC_SAM_INC);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    qtest_clock_step(qts, duration);
    xdmac_waitl(qts, XDMAC_GS, BIT(0), 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHA_BASE + SHA_BCR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(9));

    qtest_clock_step(qts, 2 * duration);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(9));
    sha_read_digest(qts, digest, sizeof(digest));
    g_assert_cmpmem(digest, sizeof(digest), expected, sizeof(expected));

    qtest_quit(qts);
}

static void test_trng_timing_irq_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE " -seed 1");
    uint64_t period;
    uint64_t duration;
    uint64_t normal_duration;
    uint32_t first;
    uint32_t second;
    uint32_t status;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_WPSR), ==, 0);

    /* An enabled generator remains idle while its peripheral clock is gated. */
    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_CR,
                 TRNG_CR_KEY | TRNG_CR_ENABLE);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==, 0);
    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_CR, TRNG_CR_KEY);

    pmc_write_pcr(qts, 38, PMC_PCR_EN);
    period = get_clock_period(qts, "/machine/soc/pmc/pclk[38]");
    g_assert_cmpuint(period, !=, 0);
    duration = (period * 168) >> 32;
    g_assert_cmpuint(duration, >, 1);

    aic_configure(qts, 38, AIC_SMR_LEVEL_HIGH | 3, 0x38383838);
    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_MR,
                 TRNG_MR_HALFR | TRNG_MR_DIFF);
    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_IER, TRNG_INT_DATRDY);

    /* The ASCII key is mandatory, and HALFR doubles readiness to 168 ticks. */
    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_CR, TRNG_CR_ENABLE);
    qtest_clock_step(qts, duration);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==, 0);

    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_CR,
                 TRNG_CR_KEY | TRNG_CR_ENABLE);
    qtest_clock_step(qts, duration - 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(6));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==,
                    TRNG_INT_DATRDY);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(6));
    first = qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ODATA);

    qtest_clock_step(qts, duration);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==,
                    TRNG_INT_DATRDY);
    second = qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ODATA);
    g_assert_cmphex(first, !=, second);

    /* Clearing HALFR reschedules the pending value for the normal 84 ticks. */
    normal_duration = (period * 84) >> 32;
    g_assert_cmpuint(normal_duration, >, 1);
    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_MR, TRNG_MR_DIFF);
    qtest_clock_step(qts, normal_duration - 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==,
                    TRNG_INT_DATRDY);
    qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ODATA);

    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_WPMR,
                 TRNG_WPMR_KEY | TRNG_WPMR_WPEN |
                 TRNG_WPMR_WPITEN | TRNG_WPMR_WPCREN);

    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_MR), ==,
                    TRNG_MR_DIFF);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_WPSR), ==,
                    TRNG_WPSR_WPVSRC(TRNG_MR) | TRNG_WPSR_WPVS);

    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_IDR, TRNG_INT_DATRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_IMR), ==,
                    TRNG_INT_DATRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_WPSR), ==,
                    TRNG_WPSR_WPVSRC(TRNG_IDR) | TRNG_WPSR_WPVS);

    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_CR, TRNG_CR_KEY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_WPSR), ==,
                    TRNG_WPSR_WPVSRC(TRNG_CR) | TRNG_WPSR_WPVS);
    qtest_clock_step(qts, duration);
    status = qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR);
    g_assert_cmphex(status & (TRNG_INT_DATRDY | TRNG_INT_SECE), ==,
                    TRNG_INT_DATRDY | TRNG_INT_SECE);
    qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ODATA);

    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_WPMR, TRNG_WPMR_KEY);
    qtest_writel(qts, SAM9X7_TRNG_BASE + TRNG_CR, TRNG_CR_KEY);

    /* Invalid output reads are classified as critical security events. */
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ODATA), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_WPSR), ==,
                    TRNG_WPSR_ECLASS |
                    TRNG_WPSR_SWETYP(TRNG_SWE_TRNG_DIS) |
                    TRNG_WPSR_WPVSRC(TRNG_ODATA) | TRNG_WPSR_SWE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==,
                    TRNG_INT_SECE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TRNG_BASE + TRNG_ISR), ==, 0);

    qtest_quit(qts);
}

static void test_system_slowclock_pit_reset_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & (RSTC_SR_URSTS | RSTC_SR_NRSTL), ==,
                    RSTC_SR_URSTS | RSTC_SR_NRSTL);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_SCKC_BASE + SCKC_CR), ==, 1);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/sckc/td-slck"), ==,
                     CLOCK_PERIOD_FROM_HZ(32000));
    qtest_writel(qts, SAM9X7_SCKC_BASE + SCKC_CR,
                 SCKC_CR_TD_OSCSEL | SCKC_CR_OSC32EN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SCKC_BASE + SCKC_CR), ==,
                    SCKC_CR_TD_OSCSEL | SCKC_CR_OSC32EN | 1);
    g_assert_cmpuint(get_clock_period(qts,
                                     "/machine/soc/sckc/td-slck"), ==,
                     CLOCK_PERIOD_FROM_HZ(32768));

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SCKC_BASE + SCKC_CR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SCKC_BASE + SCKC_CR), ==,
                    SCKC_CR_TD_OSCSEL | SCKC_CR_OSC32EN | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    (0x50 << 8) | 1);
    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR), ==,
                    SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR, SYSC_WPMR_KEY);

    /* Use the 12 MHz main RC clock so a short virtual step expires PIT. */
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_MCKR, 1);
    aic_configure(qts, 1, AIC_SMR_LEVEL_HIGH | 3, 0x11110001);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT_BASE + PIT_MR), ==,
                    0x000fffff);
    qtest_writel(qts, SAM9X7_PIT_BASE + PIT_MR,
                 1 | PIT_MR_PITEN | PIT_MR_PITIEN);
    qtest_clock_step(qts, 3000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT_BASE + PIT_SR), ==, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    value = qtest_readl(qts, SAM9X7_PIT_BASE + PIT_PIIR);
    g_assert_cmpuint(value >> 20, >=, 1);
    value = qtest_readl(qts, SAM9X7_PIT_BASE + PIT_PIVR);
    g_assert_cmpuint(value >> 20, >=, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT_BASE + PIT_SR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    qtest_writel(qts, SAM9X7_PIT_BASE + PIT_MR, 0);
    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_PIT_BASE + PIT_MR, PIT_MR_PITEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PIT_BASE + PIT_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    (0x40 << 8) | 1);
    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR, SYSC_WPMR_KEY);

    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_MR,
                 RSTC_KEY | RSTC_MR_URSTIEN);
    qtest_set_irq_in(qts, "/machine/soc/rstc", "nrst", 0, 0);
    qtest_clock_step(qts, 40000);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_true(value & RSTC_SR_URSTS);
    g_assert_false(value & RSTC_SR_NRSTL);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    qtest_set_irq_in(qts, "/machine/soc/rstc", "nrst", 0, 1);

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_MR,
                 RSTC_KEY | RSTC_MR_URSTEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_MR), ==,
                    RSTC_MR_URSTIEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    (RSTC_MR << 8) | 1);

    /* EXTRST alone pulses NRST_OUT without resetting the processor. */
    qtest_irq_intercept_out_named(qts, "/machine/soc/rstc", "nrst-out");
    qtest_system_reset(qts);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_CR, RSTC_CR_EXTRST);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_CR,
                 RSTC_KEY | RSTC_CR_EXTRST);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_true(qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR) &
                  RSTC_SR_SRCMP);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_false(qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR) &
                   RSTC_SR_SRCMP);

    qtest_quit(qts);
}

static void test_rstc_a1_general_reset_reporting(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    /* Revision A1 reports an initial General reset as Backup. */
    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_BACKUP));

    /* The erratum must not change a distinct warm-reset cause. */
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_CR,
                 RSTC_KEY | RSTC_CR_PROCRST);
    qtest_qmp_eventwait(qts, "RESET");
    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_SOFTWARE));

    /* A later explicit cold General reset is also reported as Backup. */
    qtest_system_reset(qts);
    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_BACKUP));

    qtest_quit(qts);
}

static void test_rtt_count_alarm_modulo_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;
    uint32_t stopped_value;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    aic_configure(qts, 1, AIC_SMR_LEVEL_HIGH | 3, 0x1111e771);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_MR), ==,
                    0x00008000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_AR), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_MODR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_TSR), ==, 0);

    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_AR, 3);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MR,
                 RTT_MR_RTTRST | RTT_MR_ALMIEN | RTT_MR_RTTINCIEN | 32);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_MR), ==,
                    RTT_MR_ALMIEN | RTT_MR_RTTINCIEN | 32);

    /* RTPRES=32 on the 32 kHz slow clock produces a 1 ms increment. */
    qtest_clock_step(qts, 1100000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), ==, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR), ==,
                    RTT_SR_RTTINC);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    qtest_clock_step(qts, 2000000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), ==, 3);
    value = qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR);
    g_assert_cmphex(value, ==, RTT_SR_ALMS | RTT_SR_RTTINC);

    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_AR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MODR, 1);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MR,
                 RTT_MR_RTTRST | RTT_MR_INC2AEN | 32);
    qtest_clock_step(qts, 64100000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), ==, 64);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_TSR), ==, 64);
    value = qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR);
    g_assert_cmphex(value, ==, RTT_SR_RTTINC | RTT_SR_RTTINC2);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    /* A second modulo event before SR is read marks the timestamp stale. */
    qtest_clock_step(qts, 128000000);
    value = qtest_readl(qts, SAM9X7_RTT_BASE + RTT_TSR);
    g_assert_true(value & RTT_TSR_TS_OVF);
    g_assert_cmphex(value & ~RTT_TSR_TS_OVF, ==, 192);
    g_assert_true(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR) &
                  RTT_SR_RTTINC2);
    g_assert_false(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_TSR) &
                   RTT_TSR_TS_OVF);

    stopped_value = qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MR, RTT_MR_RTTDIS | 32);
    qtest_clock_step(qts, 10000000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), ==,
                    stopped_value);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MR, 32);
    qtest_clock_step(qts, 1100000);
    g_assert_cmpuint(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), >,
                     stopped_value);

    /* RTC1HZ changes the counter source but not the prescaler status flag. */
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MR,
                 RTT_MR_RTTRST | RTT_MR_RTC1HZ | RTT_MR_RTTINCIEN | 32);
    qtest_clock_step(qts, 2100000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR), ==,
                    RTT_SR_RTTINC);
    qtest_clock_step(qts, 1000000000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_VR), ==, 1);
    qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR);

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_MR), ==,
                    RTT_MR_RTC1HZ | RTT_MR_RTTINCIEN | 32);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    (0x20 << 8) | 1);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_AR), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    (0x24 << 8) | 1);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MODR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_MODR), ==, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    (0x30 << 8) | 1);

    qtest_quit(qts);
}

static void shdwc_expect_shutdown(QTestState *qts)
{
    QDict *event = qtest_qmp_eventwait_ref(qts, "SHUTDOWN");
    QDict *data = qdict_get_qdict(event, "data");

    g_assert_nonnull(data);
    g_assert_true(qdict_get_bool(data, "guest"));
    g_assert_cmpstr(qdict_get_str(data, "reason"), ==, "guest-shutdown");
    qobject_unref(event);
}

static void test_shdwc_registers_shutdown_and_pin_wake(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -global at91-shdwc.request-system-shutdown=off");
    uint32_t mode = SHDWC_MR_RTTWKEN | SHDWC_MR_RTCWKEN |
                    SHDWC_MR_WKUPDBC(7);
    uint32_t wakeup_inputs = SHDWC_WUIR_WKUPEN0 |
                             SHDWC_WUIR_WKUPT0;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_irq_intercept_out_named(qts, "/machine/soc/shdwc", "shdn");
    qtest_system_reset(qts);
    g_assert_true(qtest_get_irq(qts, 0));

    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_CR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_MR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR), ==,
                    0);

    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_MR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_MR), ==,
                    mode);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR), ==,
                    wakeup_inputs);

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_MR), ==,
                    mode);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    0x00001401);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR), ==,
                    wakeup_inputs);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    0x00001c01);
    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR, SYSC_WPMR_KEY);

    /* The VDDBU-backed configuration survives an ordinary system reset. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_MR), ==,
                    mode);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR), ==,
                    wakeup_inputs);
    qtest_set_irq_in(qts, "/machine/soc/shdwc", "vddbu-reset", 0, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_MR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR), ==,
                    0);
    qtest_set_irq_in(qts, "/machine/soc/shdwc", "vddbu-reset", 0, 0);

    /* A missing key must not start the two-slow-clock shutdown delay. */
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR, SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_MR,
                 SHDWC_MR_WKUPDBC(1));
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_WUIR, wakeup_inputs);
    qtest_set_irq_in(qts, "/machine/soc/shdwc", "wkup", 0, 0);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR,
                 SHDWC_CR_KEY | SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS - 1);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 1);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_set_irq_in(qts, "/machine/soc/shdwc", "wkup", 0, 1);
    qtest_clock_step(qts, 3 * SHDWC_SLCK_CYCLE_NS - 1);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, 1);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR), ==,
                    SHDWC_SR_WKUPS | SHDWC_SR_WKUPIS0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR), ==,
                    0);

    qtest_quit(qts);
}

static void test_shdwc_guest_shutdown(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE " -action shutdown=pause");

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR,
                 SHDWC_CR_KEY | SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    shdwc_expect_shutdown(qts);

    qtest_quit(qts);
}

static void test_shdwc_rtc_alarm_wake(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -rtc base=2024-02-28T23:59:58,clock=vm"
        " -global at91-shdwc.request-system-shutdown=off");

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_irq_intercept_out_named(qts, "/machine/soc/shdwc", "shdn");
    qtest_system_reset(qts);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_IDR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_SCCR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TIMALR,
                 RTC_TIMALR_SECEN | 0x59);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_MR,
                 SHDWC_MR_RTCWKEN);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_IMR), ==, 0);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR,
                 SHDWC_CR_KEY | SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_clock_step(qts,
                     RTC_SECOND_NS - 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, SHDWC_SLCK_CYCLE_NS);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR), ==,
                    SHDWC_SR_RTCWK);
    g_assert_true(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_SR) &
                  RTC_SR_ALARM);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    qtest_quit(qts);
}

static void test_shdwc_rtt_alarm_wake(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -global at91-shdwc.request-system-shutdown=off");

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_irq_intercept_out_named(qts, "/machine/soc/shdwc", "shdn");
    qtest_system_reset(qts);
    qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_AR, 1);
    qtest_writel(qts, SAM9X7_RTT_BASE + RTT_MR,
                 RTT_MR_RTTRST | RTT_MR_RTC1HZ);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_MR,
                 SHDWC_MR_RTTWKEN);

    g_assert_false(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_MR) &
                   RTT_MR_ALMIEN);
    qtest_writel(qts, SAM9X7_SHDWC_BASE + SHDWC_CR,
                 SHDWC_CR_KEY | SHDWC_CR_SHDW);
    qtest_clock_step(qts, 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_clock_step(qts,
                     RTC_SECOND_NS - 2 * SHDWC_SLCK_CYCLE_NS);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_clock_step(qts, SHDWC_SLCK_CYCLE_NS);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SHDWC_BASE + SHDWC_SR), ==,
                    SHDWC_SR_RTTWK);
    g_assert_true(qtest_readl(qts, SAM9X7_RTT_BASE + RTT_SR) & RTT_SR_ALMS);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    qtest_quit(qts);
}

static void rtc_begin_update(QTestState *qts)
{
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_CR,
                 RTC_CR_UPDTIM | RTC_CR_UPDCAL);
    qtest_clock_step(qts, RTC_SECOND_NS);
    g_assert_true(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_SR) &
                  RTC_SR_ACKUPD);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_SCCR, RTC_SR_ACKUPD);
}

static void test_bsc_key_retention_and_factory_reset(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -global at91-bsc.factory-boot-sequence=2");

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==, 2);

    /* An incorrect or absent key aborts the complete BOOT field write. */
    qtest_writel(qts, SAM9X7_BSC_BASE + BSC_CR, 5);
    qtest_writel(qts, SAM9X7_BSC_BASE + BSC_CR, 0x12340007);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==, 2);

    /* Reserved bits are ignored and the write-only key reads as zero. */
    qtest_writel(qts, SAM9X7_BSC_BASE + BSC_CR,
                 BSC_CR_WPKEY |
                 (UINT16_MAX & ~BSC_CR_BOOT_MASK) | 5);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==,
                    5 & BSC_CR_BOOT_MASK);

    /* BSC_CR is retained; RomBOOT observes the selection after reset. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==, 5);

    /* Removing VDDBU restores the configured factory value. */
    qtest_set_irq_in(qts, "/machine/soc/bsc", "vddbu-reset", 0, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==, 2);
    qtest_set_irq_in(qts, "/machine/soc/bsc", "vddbu-reset", 0, 0);

    qtest_quit(qts);
}

static void test_bsc_migration(void)
{
    const char *args = SAM9X75_MACHINE
        " -global at91-bsc.factory-boot-sequence=2";
    QTestState *from = qtest_init(args);
    QTestState *to = qtest_initf("%s -incoming defer", args);

    qtest_writel(from, SAM9X7_BSC_BASE + BSC_CR, BSC_CR_WPKEY | 7);
    qtest_system_reset(from);
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_BSC_BASE + BSC_CR), ==, 7);
    qtest_system_reset(to);
    g_assert_cmphex(qtest_readl(to, SAM9X7_BSC_BASE + BSC_CR), ==, 7);
    qtest_set_irq_in(to, "/machine/soc/bsc", "vddbu-reset", 0, 1);
    g_assert_cmphex(qtest_readl(to, SAM9X7_BSC_BASE + BSC_CR), ==, 2);

    qtest_quit(from);
    qtest_quit(to);
}

static void otpc_enable_emulation(QTestState *qts)
{
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    g_assert_true(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                  OTPC_SR_EMUL);
}

static void otpc_stage_new_packet(QTestState *qts, uint32_t header,
                                  const uint32_t *payload,
                                  size_t payload_words)
{
    uint32_t pending;
    size_t i;

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_NPCKT);
    /* Entering NPCKT can automatically flush an earlier packet buffer. */
    pending = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    g_assert_cmphex(pending & ~OTPC_INT_EOF, ==, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_HR, header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    for (i = 0; i < payload_words; i++) {
        qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, payload[i]);
    }
}

static void otpc_seed_protection_chain(QTestState *qts, uint32_t uhc1)
{
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 0 * 4,
                 OTPC_HEADER(OTPC_PACKET_HARDWARE, 1));
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 2 * 4, uhc1);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 3 * 4,
                 OTPC_HEADER(OTPC_PACKET_BOOT, 17));
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 22 * 4,
                 OTPC_HEADER(OTPC_PACKET_SECURE_BOOT, 7));
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 31 * 4,
                 OTPC_HEADER(OTPC_PACKET_CUSTOM, 0));
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 33 * 4,
                 OTPC_HEADER(OTPC_PACKET_REGULAR, 0));
    otpc_enable_emulation(qts);
}

typedef struct OTPCGateCase {
    uint32_t gate;
    uint32_t address;
    uint32_t header;
} OTPCGateCase;

static void otpc_select_and_read(QTestState *qts, uint32_t address)
{
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(address));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);
}

static void otpc_clear_protection(QTestState *qts)
{
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 2 * 4, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
}

static void otpc_test_program_gate(const OTPCGateCase *test)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    otpc_seed_protection_chain(qts, test->gate);
    otpc_select_and_read(qts, test->address);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 1);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    g_assert_false(value & OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_SRAM1_BASE +
                                (test->address + 1) * 4), ==, 0);

    otpc_clear_protection(qts);
    otpc_select_and_read(qts, test->address);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 1);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_SRAM1_BASE +
                                (test->address + 1) * 4), ==, 1);
    qtest_quit(qts);
}

static void otpc_test_invalidation_gate(const OTPCGateCase *test)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    otpc_seed_protection_chain(qts, test->gate);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(test->address));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_INVLD);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    g_assert_false(value & OTPC_INT_EOI);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_SRAM1_BASE + test->address * 4), ==,
                    test->header);

    otpc_clear_protection(qts);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_INVLD);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOI);
    g_assert_cmphex(qtest_readl(qts,
                                SAM9X7_SRAM1_BASE + test->address * 4), ==,
                    test->header | (3U << 4));
    qtest_quit(qts);
}

static void test_otpc_program_update_invalidate_and_hide(void)
{
    const uint32_t header = OTPC_HEADER(OTPC_PACKET_REGULAR, 1);
    const uint32_t custom_header = OTPC_HEADER(OTPC_PACKET_CUSTOM, 0);
    const uint32_t payload[] = { 0, 0x0000000f };
    const uint32_t custom_payload[] = { 0xc0579750 };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    otpc_enable_emulation(qts);

    /* Append a two-word packet and return its controller-selected address. */
    otpc_stage_new_packet(qts, header, payload, ARRAY_SIZE(payload));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_PGM | OTPC_CR_INVLD);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_KEY_ERROR) |
                    OTPC_WPSR_ECLASS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM | OTPC_CR_INVLD);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 0 * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 1 * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 2 * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR) &
                    OTPC_MR_ADDR(UINT16_MAX), ==, OTPC_MR_ADDR(0));
    g_assert_true(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR) &
                  OTPC_MR_NPCKT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 0 * 4), ==,
                    header);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 1 * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 2 * 4), ==,
                    0x0000000f);

    /* A read supplies the temporary buffer used for a whole-word update. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(0));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0x55aa55aa);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0x0000000f);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 1 * 4), ==,
                    0x55aa55aa);

    /* Hiding is per packet, survives REFRESH, and changes no SRAM bits. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_HIDE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOH);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                   OTPC_SR_ONEF);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);

    qtest_system_reset(qts);
    otpc_enable_emulation(qts);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(0));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0x55aa55aa);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);

    /* Invalidation sets only INVLD and does not reclaim packet space. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_INVLD);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOI);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 0 * 4), ==,
                    header | (3U << 4));

    otpc_stage_new_packet(qts, custom_header, custom_payload,
                          ARRAY_SIZE(custom_payload));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR) &
                    OTPC_MR_ADDR(UINT16_MAX), ==, OTPC_MR_ADDR(3));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 3 * 4), ==,
                    custom_header);

    qtest_quit(qts);
}

static void test_otpc_program_validation(void)
{
    const uint32_t bad_hardware = OTPC_HEADER(OTPC_PACKET_HARDWARE, 0);
    const uint32_t regular = OTPC_HEADER(OTPC_PACKET_REGULAR, 0);
    const uint32_t largest =
        OTPC_HEADER(OTPC_PACKET_REGULAR, UINT8_MAX);
    const uint32_t payload[] = { 1 };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    otpc_enable_emulation(qts);
    otpc_stage_new_packet(qts, bad_hardware, payload,
                          ARRAY_SIZE(payload));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_WERR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE), ==, 0);

    otpc_stage_new_packet(qts, regular, payload, ARRAY_SIZE(payload));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(0));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_WERR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 1 * 4), ==, 1);
    qtest_quit(qts);

    qts = qtest_init(SAM9X75_MACHINE);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 0 * 4, largest);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 257 * 4, largest);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 514 * 4, largest);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 771 * 4,
                 OTPC_HEADER(OTPC_PACKET_REGULAR, 250));
    otpc_enable_emulation(qts);
    otpc_stage_new_packet(qts, regular, payload, ARRAY_SIZE(payload));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_WERR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 1023 * 4), ==, 0);
    qtest_quit(qts);
}

static void test_otpc_uhc_program_invalidation_gates(void)
{
    static const OTPCGateCase program_gates[] = {
        { OTPC_UHC1_UPGDIS, 33,
          OTPC_HEADER(OTPC_PACKET_REGULAR, 0) },
        { OTPC_UHC1_UHCPGDIS, 0,
          OTPC_HEADER(OTPC_PACKET_HARDWARE, 1) },
        { OTPC_UHC1_BCPGDIS, 3,
          OTPC_HEADER(OTPC_PACKET_BOOT, 17) },
        { OTPC_UHC1_SBCPGDIS, 22,
          OTPC_HEADER(OTPC_PACKET_SECURE_BOOT, 7) },
        { OTPC_UHC1_CPGDIS, 31,
          OTPC_HEADER(OTPC_PACKET_CUSTOM, 0) },
    };
    static const OTPCGateCase invalidation_gates[] = {
        { OTPC_UHC1_UHCINVDIS, 0,
          OTPC_HEADER(OTPC_PACKET_HARDWARE, 1) },
        { OTPC_UHC1_BCINVDIS, 3,
          OTPC_HEADER(OTPC_PACKET_BOOT, 17) },
        { OTPC_UHC1_SBCINVDIS, 22,
          OTPC_HEADER(OTPC_PACKET_SECURE_BOOT, 7) },
        { OTPC_UHC1_CINVDIS, 31,
          OTPC_HEADER(OTPC_PACKET_CUSTOM, 0) },
    };
    const uint32_t payload[] = { 1 };
    QTestState *qts;
    uint32_t value;
    size_t i;

    for (i = 0; i < ARRAY_SIZE(program_gates); i++) {
        otpc_test_program_gate(&program_gates[i]);
    }
    for (i = 0; i < ARRAY_SIZE(invalidation_gates); i++) {
        otpc_test_invalidation_gate(&invalidation_gates[i]);
    }
    /* The special-packet program gate also covers new allocation. */
    qts = qtest_init(SAM9X75_MACHINE);
    otpc_seed_protection_chain(qts, OTPC_UHC1_CPGDIS);
    otpc_stage_new_packet(qts, OTPC_HEADER(OTPC_PACKET_CUSTOM, 0),
                          payload, ARRAY_SIZE(payload));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    g_assert_false(value & OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 35 * 4), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 36 * 4), ==, 0);
    otpc_clear_protection(qts);
    otpc_stage_new_packet(qts, OTPC_HEADER(OTPC_PACKET_CUSTOM, 0),
                          payload, ARRAY_SIZE(payload));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 35 * 4), ==,
                    OTPC_HEADER(OTPC_PACKET_CUSTOM, 0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 36 * 4), ==, 1);
    qtest_quit(qts);
}

static void test_otpc_physical_backend_persistence(void)
{
    const uint32_t header = OTPC_HEADER(OTPC_PACKET_REGULAR, 1);
    const uint32_t invalid_header = header | (3U << 4);
    const uint32_t payload = 0x12349750;
    g_autofree char *contents = NULL;
    g_autofree char *otp_path = NULL;
    GError *error = NULL;
    QDict *response;
    QTestState *qts;
    gsize length;
    int fd;

    fd = g_file_open_tmp("sam9x75-otp-XXXXXX", &otp_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 10 * 1024), ==, 0);
    close(fd);

    qts = qtest_initf(
        "-machine sam9x75-curiosity,otpc-drive=otp0,"
        "otpc-write-enable=on "
        "-drive if=none,id=otp0,format=raw,file=%s",
        otp_path);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_NPCKT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_HR, header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, payload);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    qtest_system_reset(qts);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_ADDR(0));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    payload);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);

    response = qtest_qmp_assert_failure_ref(qts,
        "{ 'execute': 'migrate', "
        "  'arguments': { 'uri': 'file:/dev/null' } }");
    g_assert_nonnull(g_strstr_len(qdict_get_str(response, "desc"), -1,
                                 "migration is disabled while a writable "
                                 "physical OTP backing image is attached"));
    qobject_unref(response);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_INVLD);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOI);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(otp_path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, 10 * 1024);
    g_assert_cmphex(ldl_le_p(contents), ==, invalid_header);
    g_assert_cmphex(ldl_le_p(contents + sizeof(uint32_t)), ==, payload);
    g_clear_pointer(&contents, g_free);

    /* Reopen as an immutable factory image: reads work, writes fail safely. */
    qts = qtest_initf(
        "-machine sam9x75-curiosity,otpc-drive=otpnode "
        "-blockdev driver=file,node-name=otpfile,filename=%s "
        "-blockdev driver=raw,node-name=otpnode,file=otpfile",
        otp_path);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_ADDR(0));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    invalid_header);
    qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, payload);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0x5a5a9750);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_PGERR);

    /* Once any physical bit is set, REFRESH cannot activate emulation. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_false(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                   OTPC_SR_EMUL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(otp_path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmphex(ldl_le_p(contents), ==, invalid_header);
    g_assert_cmphex(ldl_le_p(contents + sizeof(uint32_t)), ==, payload);
    g_assert_cmphex(ldl_le_p(contents + 2 * sizeof(uint32_t)), ==, 0);

    /* Incoming migration must not replace irreversible write-through state. */
    {
        QTestState *from = qtest_init(SAM9X75_MACHINE);
        QTestState *to = qtest_initf(
            "-machine sam9x75-curiosity,otpc-drive=otp0,"
            "otpc-write-enable=on "
            "-drive if=none,id=otp0,format=raw,file=%s "
            "-incoming defer",
            otp_path);

        migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
        migrate_qmp(from, to, NULL, NULL, "{}");
        migration_event_wait(to, "failed");
        response = migrate_query(to);
        g_assert_cmpstr(qdict_get_str(response, "status"), ==, "failed");
        g_assert_nonnull(g_strstr_len(
            qdict_get_str(response, "error-desc"), -1,
            "cannot load state while a writable physical OTP backing image "
            "is attached"));
        qobject_unref(response);

        qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_ADDR(0));
        qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
        g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                        invalid_header);
        qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_AR, 0);
        g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                        payload);
        qtest_quit(from);
        qtest_quit(to);
    }

    g_clear_pointer(&contents, g_free);
    g_assert_true(g_file_get_contents(otp_path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmphex(ldl_le_p(contents), ==, invalid_header);
    g_assert_cmphex(ldl_le_p(contents + sizeof(uint32_t)), ==, payload);
    g_assert_cmphex(ldl_le_p(contents + 2 * sizeof(uint32_t)), ==, 0);
    unlink(otp_path);
}

static void test_otpc_physical_backend_fault_latch(void)
{
    const uint32_t header = OTPC_HEADER(OTPC_PACKET_REGULAR, 1);
    g_autofree char *contents = NULL;
    g_autofree char *otp_path = NULL;
    GError *error = NULL;
    QTestState *qts;
    gsize length;
    int fd;

    fd = g_file_open_tmp("sam9x75-otp-fault-XXXXXX", &otp_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, 10 * 1024), ==, 0);
    close(fd);

    qts = qtest_initf(
        "-machine sam9x75-curiosity,otpc-drive=otpraw,"
        "otpc-write-enable=on "
        "-blockdev driver=file,node-name=otpfile,filename=%s "
        "-blockdev driver=blkdebug,node-name=otpdebug,image=otpfile,"
        "inject-error.0.event=write_aio,inject-error.0.errno=5,"
        "inject-error.0.immediately=on,inject-error.0.once=on "
        "-blockdev driver=raw,node-name=otpraw,file=otpdebug",
        otp_path);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_NPCKT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_HR, header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 1);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_PGERR);

    /*
     * The injected error is one-shot, but the device must remain fail-closed.
     */
    qtest_system_reset(qts);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_NPCKT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_HR, header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 3);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_PGERR);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(otp_path, &contents, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, 10 * 1024);
    g_assert_cmphex(ldl_le_p(contents), ==, 0);
    g_assert_cmphex(ldl_le_p(contents + sizeof(uint32_t)), ==, 0);
    unlink(otp_path);
}

static void assert_otpc_backend_rejected(off_t image_size,
                                         bool write_enable,
                                         bool readonly,
                                         const char *message)
{
    g_autofree char *drive_arg = NULL;
    g_autofree char *machine_arg = NULL;
    g_autofree char *otp_path = NULL;
    g_autofree char *stderr_text = NULL;
    const char *qemu = qtest_qemu_binary(NULL);
    GError *error = NULL;
    gchar *argv[] = {
        (gchar *)qemu,
        (gchar *)"-machine", machine_arg,
        (gchar *)"-drive", drive_arg,
        (gchar *)"-display", (gchar *)"none",
        (gchar *)"-serial", (gchar *)"none",
        (gchar *)"-monitor", (gchar *)"none",
        (gchar *)"-nic", (gchar *)"none",
        (gchar *)"-run-with", (gchar *)"exit-with-parent=on",
        NULL,
    };
    int wait_status;
    int fd;

    fd = g_file_open_tmp("sam9x75-otp-invalid-XXXXXX", &otp_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, image_size), ==, 0);
    close(fd);

    machine_arg = g_strdup_printf(
        "sam9x75-curiosity,otpc-drive=otp0%s",
        write_enable ? ",otpc-write-enable=on" : "");
    drive_arg = g_strdup_printf(
        "if=none,id=otp0,file=%s,format=raw%s", otp_path,
        readonly ? ",readonly=on" : "");
    argv[2] = machine_arg;
    argv[4] = drive_arg;
    g_assert_true(g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), !=, 0);
    g_assert_nonnull(g_strstr_len(stderr_text, -1, message));
    unlink(otp_path);
}

static void test_otpc_backend_validation(void)
{
    if (!g_test_subprocess()) {
        g_test_trap_subprocess(NULL, 10 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed();
        return;
    }

    assert_otpc_backend_rejected(10 * 1024 - 512, false, true,
        "OTP backing image must be exactly 10240 bytes");
    assert_otpc_backend_rejected(10 * 1024 + 1, false, true,
        "OTP backing image must be exactly 10240 bytes");
    assert_otpc_backend_rejected(10 * 1024, true, true,
        "write-enable requires a writable OTP drive");
}

static void test_otpc_identification_and_reserved_offsets(void)
{
    static const struct {
        unsigned int first;
        unsigned int last;
    } reserved_ranges[] = {
        { 0x28, 0x2c },
        { 0x38, 0x3c },
        { 0x44, 0x4c },
        { 0x58, 0x5c },
        { 0x70, 0xe0 },
        { 0xec, 0xf8 },
    };
    static const uint32_t uids[] = {
        0x01234567,
        0x89abcdef,
        0x55aa9750,
        0xfedcba98,
    };
    g_autofree char *log_path = NULL;
    g_autofree char *log = NULL;
    QTestState *qts;
    GError *error = NULL;
    uint32_t value;
    unsigned int range;
    unsigned int offset;
    unsigned int i;
    int fd;

    fd = g_file_open_tmp("sam9x75-otpc-log-XXXXXX", &log_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    qts = qtest_initf(SAM9X75_MACHINE
                      " -global at91-otpc.uid0=0x%08x"
                      " -global at91-otpc.uid1=0x%08x"
                      " -global at91-otpc.uid2=0x%08x"
                      " -global at91-otpc.uid3=0x%08x"
                      " -d guest_errors -D %s",
                      uids[0], uids[1], uids[2], uids[3], log_path);

    for (i = 0; i < ARRAY_SIZE(uids); i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UID0R +
                                    i * sizeof(uint32_t)), ==, uids[i]);
        qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_UID0R +
                     i * sizeof(uint32_t), ~uids[i]);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UID0R +
                                    i * sizeof(uint32_t)), ==, uids[i]);
        value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
        g_assert_cmphex(value & (OTPC_WPSR_SWE |
                                 OTPC_WPSR_SWETYP(0xf) |
                                 OTPC_WPSR_ECLASS), ==,
                        OTPC_WPSR_SWE |
                        OTPC_WPSR_SWETYP(OTPC_WPSR_WRITE_RO));
        g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                        OTPC_INT_SECE);
    }

    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_VERSION), ==,
                    OTPC_VERSION_RESET);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_VERSION, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_VERSION), ==,
                    OTPC_VERSION_RESET);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_WRITE_RO));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);

    qtest_system_reset(qts);
    for (i = 0; i < ARRAY_SIZE(uids); i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UID0R +
                                    i * sizeof(uint32_t)), ==, uids[i]);
    }
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_VERSION), ==,
                    OTPC_VERSION_RESET);

    for (range = 0; range < ARRAY_SIZE(reserved_ranges); range++) {
        for (offset = reserved_ranges[range].first;
             offset <= reserved_ranges[range].last;
             offset += sizeof(uint32_t)) {
            g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + offset), ==,
                            0);
            qtest_writel(qts, SAM9X7_OTPC_BASE + offset, UINT32_MAX);
            g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + offset), ==,
                            0);
        }
    }
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(log_path, &log, NULL, &error));
    g_assert_no_error(error);
    g_assert_null(strstr(log, "at91-otpc"));
    g_clear_pointer(&log, g_free);

    qts = qtest_initf(SAM9X75_MACHINE " -d guest_errors -D %s", log_path);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + 0x100), ==, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + 0x100, UINT32_MAX);
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(log_path, &log, NULL, &error));
    g_assert_no_error(error);
    g_assert_nonnull(strstr(log, "read from bad offset 0x100"));
    g_assert_nonnull(strstr(log, "write to bad offset 0x100"));
    g_assert_cmpint(g_unlink(log_path), ==, 0);
}

static void test_otpc_identification_migration(void)
{
    static const uint32_t uids[] = {
        0x76543210,
        0xfedcba98,
        0x0badcafe,
        0x55aa33cc,
    };
    const char *args = SAM9X75_MACHINE
        " -global at91-otpc.uid0=0x76543210"
        " -global at91-otpc.uid1=0xfedcba98"
        " -global at91-otpc.uid2=0x0badcafe"
        " -global at91-otpc.uid3=0x55aa33cc";
    QTestState *from = qtest_init(args);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    unsigned int i;

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    for (i = 0; i < ARRAY_SIZE(uids); i++) {
        g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_UID0R +
                                    i * sizeof(uint32_t)), ==, uids[i]);
    }
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_VERSION), ==,
                    OTPC_VERSION_RESET);

    qtest_system_reset(to);
    for (i = 0; i < ARRAY_SIZE(uids); i++) {
        g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_UID0R +
                                    i * sizeof(uint32_t)), ==, uids[i]);
    }
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_VERSION), ==,
                    OTPC_VERSION_RESET);

    qtest_quit(from);
    qtest_quit(to);
}

static void test_otpc_registers_protection_and_irq(void)
{
    const uint32_t protection = OTPC_WPMR_WPCFEN |
                                OTPC_WPMR_WPITEN |
                                OTPC_WPMR_WPCTEN;
    const uint32_t command_errors = OTPC_INT_WERR |
                                    OTPC_INT_LKERR |
                                    OTPC_INT_IVERR |
                                    OTPC_INT_HDERR |
                                    OTPC_INT_KBERR;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    aic_configure(qts, 46, AIC_SMR_LEVEL_HIGH | 3, 0x2e2e2e2e);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_AR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_BAR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_CAR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_LRMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC0R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UID0R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UID1R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UID2R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UID3R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR), ==, 0);

    /* Refreshing the blank OTP area completes without a corruption error. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_IER,
                 OTPC_INT_EORF | OTPC_INT_SECE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_IMR), ==,
                    OTPC_INT_EORF | OTPC_INT_SECE);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);

    /* The prescribed max-address probe finds no ones in QEMU's blank OTP. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_ADDR(UINT16_MAX));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                   OTPC_SR_ONEF);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, 0);

    /* Invalid register access reports a warning-class software event. */
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_CR), ==, 0);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_READ_WO));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));

    /* LRMR uses its own key but does not classify a bad key as CR/WPMR SWE. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_LRMR, 0x13);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_LRMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_LRMR,
                 OTPC_LRMR_KEY | 0x13);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_LRMR), ==,
                    0x13);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_SR, UINT32_MAX);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_WRITE_RO));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));

    /* A wrong WPMR key is rejected and reported as a software error. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR, protection);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPMR), ==, 0);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_KEY_ERROR) |
                    OTPC_WPSR_ECLASS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));

    /* Invalid command setups report their command-specific failure bits. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_IER, command_errors);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_CKSGEN);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_INVLD);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_HIDE);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_KBSTART);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    command_errors);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));

    /* FIRSTE also selects the first software error in an error series. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR,
                 OTPC_WPMR_KEY | OTPC_WPMR_FIRSTE);
    qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_CR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR, 0);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_READ_WO));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);

    /* Without FIRSTE, the last software error in the series wins. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR, OTPC_WPMR_KEY);
    qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_CR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR, 0);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_KEY_ERROR) |
                    OTPC_WPSR_ECLASS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);

    /* FIRSTE retains the first protected register in an error series. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR,
                 OTPC_WPMR_KEY | protection | OTPC_WPMR_FIRSTE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPMR), ==,
                    protection | OTPC_WPMR_FIRSTE);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_IER, OTPC_INT_EOR);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_true(value & OTPC_WPSR_WPVS);
    g_assert_cmphex(value & 0x00ffff00, ==, OTPC_MR << 8);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_IMR) &
                   OTPC_INT_EOR);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));

    /* With FIRSTE clear, the last protected register is reported. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR,
                 OTPC_WPMR_KEY | protection);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_IER, OTPC_INT_EOR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_true(value & OTPC_WPSR_WPVS);
    g_assert_cmphex(value & 0x00ffff00, ==, OTPC_CR << 8);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_IMR) &
                   OTPC_INT_EOR);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_SECE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(14));

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_WPMR, OTPC_WPMR_KEY);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_IDR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_IMR), ==, 0);

    /* WRDIS covers DR, while hardware owns protected HR fields. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_NPCKT | OTPC_MR_WRDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_HR,
                 (UINT32_MAX & ~7U) | OTPC_PACKET_CUSTOM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    0x0000ff86);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0xfeed9750);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_FLUSH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOF);

    /* MR.LOCK freezes configuration and packet commands until reset. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_LOCK | OTPC_MR_EMUL | OTPC_MR_NPCKT |
                 OTPC_MR_ADDR(3));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR), ==,
                    OTPC_MR_LOCK | OTPC_MR_EMUL | OTPC_MR_NPCKT |
                    OTPC_MR_ADDR(3));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR), ==,
                    OTPC_MR_LOCK | OTPC_MR_EMUL | OTPC_MR_NPCKT |
                    OTPC_MR_ADDR(3));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_HR,
                 (UINT32_MAX & ~7U) | OTPC_PACKET_CUSTOM);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0xfeed9750);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    0x0000ff86);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0xfeed9750);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);

    qtest_quit(qts);
}

static void test_otpc_emulation_scan_and_read(void)
{
    const uint32_t boot_header = OTPC_HEADER(OTPC_PACKET_BOOT, 17);
    const uint32_t secure_header =
        OTPC_HEADER(OTPC_PACKET_SECURE_BOOT, 7);
    const uint32_t hardware_header =
        OTPC_HEADER(OTPC_PACKET_HARDWARE, 1);
    const uint32_t custom_header = OTPC_HEADER(OTPC_PACKET_CUSTOM, 0);
    const uint32_t key_header = OTPC_HEADER(OTPC_PACKET_KEY, 0);
    const uint32_t uhc1_raw = 0xabc04420;
    const uint32_t uhc1 = 0x00004420;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    /* Two boot packets exercise last-special-wins during the area scan. */
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 0 * 4, boot_header);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 1 * 4, 0x11111111);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 19 * 4, custom_header);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 20 * 4, 0xdead0001);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 21 * 4, hardware_header);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 22 * 4, 0x5a);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 23 * 4, uhc1_raw);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 24 * 4,
                 OTPC_HEADER(OTPC_PACKET_HARDWARE, 0) | (3U << 4));
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 25 * 4, 0xc0570001);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 26 * 4, boot_header);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 27 * 4, 0xfeed9750);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 45 * 4, secure_header);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 46 * 4, 0x5ec00001);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 54 * 4, key_header);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 55 * 4, 0x01234567);

    /* BSC only instructs RomBOOT; it does not directly enable OTPC mode. */
    qtest_writel(qts, SAM9X7_BSC_BASE + BSC_CR, BSC_CR_WPKEY | 1);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_BSC_BASE + BSC_CR), ==, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE), ==, boot_header);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                    OTPC_SR_EMUL, ==, 0);

    qtest_irq_intercept_out_named(qts, "/machine/soc/otpc", "sysbus-irq");
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_IER,
                 OTPC_INT_EORF | OTPC_INT_EOR | OTPC_INT_EOF);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                    OTPC_SR_EMUL, ==, 0);

    /* REFRESH is not one of the four CR commands that require KEY. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                    OTPC_SR_EMUL, ==, OTPC_SR_EMUL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_WPSR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_BAR), ==,
                    (45U << 16) | 26);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_CAR), ==, 19);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC0R), ==,
                    0x5a);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==,
                    uhc1);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    g_assert_false(qtest_get_irq(qts, 0));

    /* A max-address read returns raw bits at the prospective packet tail. */
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 56 * 4, 0);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 57 * 4, 0x80000000);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(UINT16_MAX));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0x80000000);
    g_assert_true(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                  OTPC_SR_ONEF);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 56 * 4, OTPC_PACKET_REGULAR);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 57 * 4, 0);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    OTPC_PACKET_REGULAR);
    g_assert_false(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                   OTPC_SR_ONEF);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 56 * 4, 0);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_UHCRRDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC0R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==, 0);

    /* An address inside the payload resolves to its containing packet. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(27));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    boot_header);
    g_assert_true(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                  OTPC_SR_ONEF);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0xfeed9750);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_AR), ==, 1);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT | 1);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0x12345678);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_AR), ==,
                    OTPC_AR_INCRT | 2);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0x12345678);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_AR), ==,
                    OTPC_AR_INCRT | 1);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(27) | OTPC_MR_WRDIS);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_DR, 0x87654321);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0x12345678);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(27) |
                 OTPC_MR_WRDIS | OTPC_MR_RDDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);

    /* KEY payload data is never exposed through the system bus. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(55));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    key_header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    g_assert_true(value & OTPC_INT_EOR);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_FLUSH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOF);

    /* UHC1.UPGDIS makes the packet PGM command nonfunctional. */
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 23 * 4, uhc1_raw | BIT(1));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==,
                    uhc1 | BIT(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 23 * 4, uhc1_raw);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);

    /* UHC1.URDDIS makes the packet READ command nonfunctional. */
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 23 * 4, uhc1_raw | BIT(0));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==,
                    uhc1 | BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(27));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 23 * 4, uhc1_raw);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);

    /* UHC1.URFDIS still permits refresh while emulation is active. */
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 23 * 4,
                 uhc1_raw | BIT(17));
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==,
                    uhc1 | BIT(17));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 23 * 4, uhc1_raw);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);

    /* MR.EMUL is only a request; REFRESH latches the active backing area. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                  OTPC_SR_EMUL);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_false(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                   OTPC_SR_EMUL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_BAR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_CAR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC0R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==, 0);
    qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);

    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_true(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR) &
                  OTPC_SR_EMUL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_BAR), ==,
                    (45U << 16) | 26);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_CAR), ==, 19);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC0R), ==,
                    0x5a);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_UHC1R), ==,
                    uhc1);

    qtest_quit(qts);
}

static void test_otpc_corruption_bounds_and_reset(void)
{
    const uint32_t regular_header =
        OTPC_HEADER(OTPC_PACKET_REGULAR, 0);
    const uint32_t largest_packet =
        OTPC_HEADER(OTPC_PACKET_REGULAR, UINT8_MAX);
    const uint32_t bad_specials[] = {
        OTPC_HEADER(OTPC_PACKET_BOOT, 16),
        OTPC_HEADER(OTPC_PACKET_SECURE_BOOT, 6),
        OTPC_HEADER(OTPC_PACKET_HARDWARE, 0),
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;
    size_t i;

    /* A nonblank header without ONE is corrupt and stops the scan. */
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 0 * 4, regular_header);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 1 * 4, 0x11112222);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 2 * 4, OTPC_PACKET_REGULAR);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 3 * 4, 0xa5a50001);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 4 * 4, regular_header);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    g_assert_true(value & OTPC_INT_COERR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    OTPC_PACKET_REGULAR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR) &
                    OTPC_MR_ADDR(UINT16_MAX), ==, OTPC_MR_ADDR(2));

    /* Corruption repair exposes SIZE payload words plus the next header. */
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    OTPC_PACKET_REGULAR);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0xa5a50001);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    regular_header);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);

    /* The three fixed-format special packets require their exact sizes. */
    for (i = 0; i < ARRAY_SIZE(bad_specials); i++) {
        qtest_writel(qts, SAM9X7_SRAM1_BASE, bad_specials[i]);
        qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                     OTPC_CR_KEY | OTPC_CR_REFRESH);
        value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
        g_assert_true(value & OTPC_INT_COERR);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                        bad_specials[i]);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR) &
                        OTPC_MR_ADDR(UINT16_MAX), ==, 0);
    }

    /* QEMU defensively reports an out-of-SRAM packet extent as COERR. */
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 0 * 4, largest_packet);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 257 * 4, largest_packet);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 514 * 4, largest_packet);
    qtest_writel(qts, SAM9X7_SRAM1_BASE + 771 * 4, largest_packet);
    qtest_writel(qts, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    value = qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR);
    g_assert_true(value & OTPC_INT_COERR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    largest_packet);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR) &
                    OTPC_MR_ADDR(UINT16_MAX), ==, OTPC_MR_ADDR(771));

    /* Ordinary QEMU system reset returns to OTP mode and retains SRAM1. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_SR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_BAR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM1_BASE + 771 * 4), ==,
                    largest_packet);

    qtest_quit(qts);
}

static void test_otpc_migration(void)
{
    const char *args = SAM9X75_MACHINE;
    const uint32_t regular_header =
        OTPC_HEADER(OTPC_PACKET_REGULAR, 0);
    const uint32_t custom_header = OTPC_HEADER(OTPC_PACKET_CUSTOM, 1);
    const uint32_t pending = OTPC_INT_EORF | OTPC_INT_EOR | OTPC_INT_SECE;
    QTestState *from = qtest_init(args);
    QTestState *to = qtest_initf("%s -incoming defer", args);
    uint32_t value;

    qtest_writel(from, SAM9X7_SRAM1_BASE + 0 * 4, regular_header);
    qtest_writel(from, SAM9X7_SRAM1_BASE + 1 * 4, 0x11112222);
    qtest_writel(from, SAM9X7_SRAM1_BASE + 2 * 4, custom_header);
    qtest_writel(from, SAM9X7_SRAM1_BASE + 3 * 4, 0x33445566);
    qtest_writel(from, SAM9X7_SRAM1_BASE + 4 * 4, 0x778899aa);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_IER, pending);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_EMUL);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(3));
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(from, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0x33445566);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_WPMR,
                 OTPC_WPMR_KEY | OTPC_WPMR_FIRSTE);
    qtest_readl(from, SAM9X7_OTPC_BASE + OTPC_CR);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_LOCK | OTPC_MR_EMUL | OTPC_MR_ADDR(3));

    qtest_irq_intercept_out_named(to, "/machine/soc/otpc", "sysbus-irq");
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_true(qtest_get_irq(to, 0));
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_MR), ==,
                    OTPC_MR_LOCK | OTPC_MR_EMUL | OTPC_MR_ADDR(3));
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_SR) &
                    OTPC_SR_EMUL, ==, OTPC_SR_EMUL);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_CAR), ==, 2);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    custom_header);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_AR), ==, 1);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_IMR), ==,
                    pending);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_WPMR), ==,
                    OTPC_WPMR_FIRSTE);
    value = qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_WPSR);
    g_assert_cmphex(value & (OTPC_WPSR_SWE |
                             OTPC_WPSR_SWETYP(0xf) |
                             OTPC_WPSR_ECLASS), ==,
                    OTPC_WPSR_SWE |
                    OTPC_WPSR_SWETYP(OTPC_WPSR_READ_WO));
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_WPSR) & 0xf,
                    ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0x778899aa);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_AR), ==, 2);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM1_BASE + 4 * 4), ==,
                    0x778899aa);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    pending);
    g_assert_false(qtest_get_irq(to, 0));

    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_IER, OTPC_INT_EOF);
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_FLUSH);
    g_assert_true(qtest_get_irq(to, 0));
    qtest_system_reset(to);
    g_assert_false(qtest_get_irq(to, 0));
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_MR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_AR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_SR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_WPSR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_CAR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM1_BASE + 4 * 4), ==,
                    0x778899aa);

    qtest_quit(from);
    qtest_quit(to);
}

static void test_otpc_migration_v2_state(void)
{
    const uint32_t header = OTPC_HEADER(OTPC_PACKET_REGULAR, 0);
    const uint32_t physical_payload = 0xa5a59750;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    qtest_writel(from, SAM9X7_SRAM1_BASE + 0 * 4, header);
    qtest_writel(from, SAM9X7_SRAM1_BASE + 1 * 4, 0x11112222);
    qtest_writel(from, SAM9X7_SRAM1_BASE + 2 * 4, header);
    qtest_writel(from, SAM9X7_SRAM1_BASE + 3 * 4, 0);
    otpc_enable_emulation(from);

    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_MR,
                 OTPC_MR_EMUL | OTPC_MR_ADDR(0));
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_HIDE);
    g_assert_cmphex(qtest_readl(from, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOH);

    /* Migrate an update staged by a READ, including its source and address. */
    otpc_select_and_read(from, 2);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_DR, 0x33445566);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM1_BASE + 3 * 4), ==,
                    0x33445566);

    otpc_select_and_read(to, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    header);
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    g_assert_false(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_SR) &
                   OTPC_SR_ONEF);

    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_REFRESH);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EORF);
    otpc_select_and_read(to, 0);
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);

    qtest_system_reset(to);
    otpc_enable_emulation(to);
    otpc_select_and_read(to, 0);
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    0x11112222);

    qtest_quit(from);
    qtest_quit(to);

    /* VM-local physical contents and their separate hide state also migrate. */
    from = qtest_init(SAM9X75_MACHINE);
    to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_NPCKT);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_HR, header);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_AR, OTPC_AR_INCRT);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_DR, physical_payload);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_PGM);
    g_assert_cmphex(qtest_readl(from, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOP);
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_ADDR(0));
    qtest_writel(from, SAM9X7_OTPC_BASE + OTPC_CR,
                 OTPC_CR_KEY | OTPC_CR_HIDE);
    g_assert_cmphex(qtest_readl(from, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOH);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_HR), ==,
                    header);
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_DR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);

    qtest_system_reset(to);
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_MR, OTPC_MR_ADDR(0));
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_CR, OTPC_CR_READ);
    qtest_writel(to, SAM9X7_OTPC_BASE + OTPC_AR, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_DR), ==,
                    physical_payload);
    g_assert_cmphex(qtest_readl(to, SAM9X7_OTPC_BASE + OTPC_ISR), ==,
                    OTPC_INT_EOR);

    qtest_quit(from);
    qtest_quit(to);
}

static void test_gpbr_protection_and_retention(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t mode = GPBR_MR_WP(0) | GPBR_MR_RP(1);
    unsigned int i;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_FCLR), ==,
                    0);
    for (i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(i)),
                        ==, 0);
        qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(i),
                     0x11110000 + i);
    }
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_FCLR, GPBR_FCLR_ENABLE);

    /* The VDDBU-backed state survives an ordinary system reset. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_FCLR), ==,
                    GPBR_FCLR_ENABLE);
    for (i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(i)),
                        ==, 0x11110000 + i);
    }

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_FCLR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_FCLR), ==,
                    GPBR_FCLR_ENABLE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    0x00006401);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(0), 0xaaaaaaaa);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(0)), ==,
                    0x11110000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    0x00006801);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_MR, mode);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    0x00006001);

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR, SYSC_WPMR_KEY);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_MR, mode);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_MR), ==,
                    mode);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(0), 0xaaaaaaaa);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(0)), ==,
                    0x11110000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(1)), ==,
                    0);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(2), 0xaaaaaaaa);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(2)), ==,
                    0xaaaaaaaa);

    qtest_set_irq_in(qts, "/machine/soc/gpbr", "vddbu-reset", 0, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_FCLR), ==,
                    0);
    for (i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(i)),
                        ==, 0);
    }
    qtest_set_irq_in(qts, "/machine/soc/gpbr", "vddbu-reset", 0, 0);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_MR, GPBR_MR_WP(7));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_MR), ==,
                    GPBR_MR_WP(7));

    qtest_quit(qts);
}

static void gpbr_trigger_tamper(QTestState *qts)
{
    qtest_set_irq_in(qts, "/machine/soc/rtc", "tamper", 0, 0);
    qtest_clock_step(qts, 61036);
}

static void gpbr_clear_tamper(QTestState *qts)
{
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSSR0), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSSR1), ==,
                    BIT(0));
    qtest_set_irq_in(qts, "/machine/soc/rtc", "tamper", 0, 1);
}

static void test_gpbr_tamper_clear(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -rtc base=2024-02-28T23:59:58,clock=vm");
    unsigned int i;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_MR,
                 RSTC_KEY | RSTC_MR_URSTEN | RSTC_MR_ENGCLR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_MR), ==,
                    RSTC_MR_URSTEN | RSTC_MR_ENGCLR);
    qtest_set_irq_in(qts, "/machine/soc/rtc", "tamper", 0, 1);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TDPR, 0);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TMR, BIT(0));

    for (i = 0; i < 8; i++) {
        qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(i),
                     0x22220000 + i);
    }
    gpbr_trigger_tamper(qts);
    g_assert_cmphex((qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSTR0) >> 24) &
                    0xf, ==, 1);
    for (i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(i)),
                        ==, 0);
    }
    for (i = 4; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(i)),
                        ==, 0x22220000 + i);
    }

    /* Writes remain blocked until the debounced tamper root cause clears. */
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(4), 0xaaaaaaaa);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(4)), ==,
                    0x22220004);
    gpbr_clear_tamper(qts);
    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(4), 0xaaaaaaaa);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(4)), ==,
                    0xaaaaaaaa);

    qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_FCLR, GPBR_FCLR_ENABLE);
    for (i = 0; i < 8; i++) {
        qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(i),
                     0x33330000 + i);
    }
    gpbr_trigger_tamper(qts);
    for (i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(i)),
                        ==, 0);
    }

    gpbr_clear_tamper(qts);
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_MR,
                 RSTC_KEY | RSTC_MR_URSTEN);
    for (i = 0; i < 8; i++) {
        qtest_writel(qts, SAM9X7_GPBR_BASE + GPBR_REG(i),
                     0x44440000 + i);
    }
    gpbr_trigger_tamper(qts);
    for (i = 0; i < 8; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_GPBR_BASE + GPBR_REG(i)),
                        ==, 0x44440000 + i);
    }

    qtest_quit(qts);
}

static void test_rtc_calendar_alarm_irq_and_protection(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -rtc base=2024-02-28T23:59:58,clock=vm");
    uint32_t value;

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_MR), ==,
                    WDT_MR_WDDIS);
    aic_configure(qts, 1, AIC_SMR_LEVEL_HIGH | 3, 0x1111ca1e);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_CR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==,
                    0x00235958);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_CALR), ==,
                    0x28822420);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_SR), ==,
                    RTC_SR_SEC);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_CALALR), ==,
                    0x01010000);

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_SCCR, RTC_SR_SEC);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_IER, RTC_SR_SEC);
    qtest_clock_step(qts, RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==,
                    0x00235959);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_SCCR, RTC_SR_SEC);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    qtest_clock_step(qts, RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_CALR), ==,
                    0x29a22420);

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_IER, RTC_SR_ACKUPD);
    rtc_begin_update(qts);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TIMR, 0x00123456);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_CALR, 0x30462520);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==,
                    0x00123456);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_CALR), ==,
                    0x30462520);

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TIMR, 0x0012347a);
    g_assert_true(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_VER) &
                  RTC_VER_NVTIM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==,
                    0x00123456);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TIMR, 0x00123456);
    g_assert_false(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_VER) &
                   RTC_VER_NVTIM);

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_CR, 0);
    qtest_clock_step(qts, RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==,
                    0x00123457);

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_IDR, 0x3f);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_SCCR, 0x3f);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TIMALR,
                 RTC_TIMALR_SECEN | 0x59);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_IER, RTC_SR_ALARM);
    qtest_clock_step(qts, 2 * RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==,
                    0x00123459);
    g_assert_true(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_SR) &
                  RTC_SR_ALARM);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_SCCR, RTC_SR_ALARM);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    value = RTC_CALALR_MTHEN | RTC_CALALR_DATEEN |
            (0x13U << 16) | (0x31U << 24);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_CALALR, value);
    g_assert_true(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_VER) &
                  RTC_VER_NVCALALR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_CALALR), ==,
                    0x01010000);

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_MR, RTC_MR_HRMOD);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_MR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    0x0000ac01);

    qtest_writel(qts, SAM9X7_SYSCWP_BASE + SYSC_WPMR,
                 SYSC_WPMR_KEY | SYSC_WPMR_WPITEN);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_IER, RTC_SR_SEC);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_IMR), ==,
                    RTC_SR_ALARM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SYSCWP_BASE + SYSC_WPSR), ==,
                    0);

    qtest_quit(qts);
}

static void test_rtc_utc_tamper_and_lock(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -rtc base=2024-02-28T23:59:58,clock=vm");

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_set_irq_in(qts, "/machine/soc/rtc", "tamper", 0, 1);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TDPR, 0);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TMR, BIT(0));
    qtest_set_irq_in(qts, "/machine/soc/rtc", "tamper", 0, 0);
    qtest_clock_step(qts, 61035);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSTR0), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSTR0), ==,
                    0x01235958);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSDR0), ==,
                    0x28822420);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSSR0), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSTR0), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSTR1), ==,
                    0x00235958);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSSR1), ==,
                    BIT(0));

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TMR, BIT(0) | RTC_TMR_LOCK);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TMR), ==,
                    BIT(0));

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_MR, RTC_MR_UTC);
    rtc_begin_update(qts);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TIMR, 100);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_CR, 0);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_TIMALR, 102);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_CALALR, RTC_CALALR_UTCEN);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_SCCR, 0x3f);
    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_IER, RTC_SR_ALARM);
    qtest_clock_step(qts, 2 * RTC_SECOND_NS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TIMR), ==, 102);
    g_assert_true(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_SR) &
                  RTC_SR_ALARM);

    qtest_set_irq_in(qts, "/machine/soc/rtc", "tamper", 0, 1);
    qtest_set_irq_in(qts, "/machine/soc/rtc", "tamper", 0, 0);
    qtest_clock_step(qts, 61036);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSTR0), ==,
                    0x01000000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSDR0), ==,
                    102);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_TSSR0), ==,
                    BIT(0));

    qtest_writel(qts, SAM9X7_RTC_BASE + RTC_MR,
                 RTC_MR_UTC | RTC_MR_HIGHPPM | (1U << 8));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_RTC_BASE + RTC_MR), ==,
                    RTC_MR_UTC | RTC_MR_HIGHPPM | (1U << 8));

    qtest_quit(qts);
}

static void test_sfr_registers_resume_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_CCFG_EBICSA),
                    ==, 0x00000300);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_CAL1), ==,
                    0x00000084);

    aic_configure(qts, 1, AIC_SMR_LEVEL_HIGH | 3, 0x11110002);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_OHCIICR,
                 SFR_OHCIICR_ARIE);
    qtest_set_irq_in(qts, "/machine/soc/sfr", "resume", 2, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_OHCIISR), ==,
                    BIT(2));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    qtest_set_irq_in(qts, "/machine/soc/sfr", "resume", 2, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_CAL1, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_CAL1), ==,
                    0x000001ff);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_PUFWORUCR0, 0x000000a5);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_PUFWORUCR0, 0x0000005a);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_PUFWORUCR0), ==,
                    0x000000ff);

    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_WPMR,
                 SFR_WPMR_KEY | SFR_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_CAL1, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_CAL1), ==,
                    0x000001ff);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_WPMR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_WPMR), ==,
                    SFR_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_WPMR, SFR_WPMR_KEY);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_CAL1, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SFR_BASE + SFR_CAL1), ==, 0);

    qtest_quit(qts);
}

static void test_mpddrc_registers_errors_and_irq(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR), ==,
                    0x00207024);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_MPDDRC_BASE + MPDDRC_IO_CALIBR), ==,
                    0x00870000);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CONF_ARBITER,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_MPDDRC_BASE + MPDDRC_CONF_ARBITER), ==,
                    0x7f7f7f0f);

    aic_configure(qts, 49, AIC_SMR_LEVEL_HIGH | 3, 0x49494949);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_IER, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_IMR), ==,
                    1);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR), ==,
                    0x1cf1f3ff);

    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPMR,
                 MPDDRC_WPMR_KEY | MPDDRC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR), ==,
                    0x1cf1f3ff);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPSR), ==,
                    (MPDDRC_CR << 8) | 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_ISR), ==,
                    1);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));

    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPMR,
                 MPDDRC_WPMR_KEY);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_RTR, 1);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_LPR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_LPR), ==,
                    0x0133f007);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPSR), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_ISR), ==, 0);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_IO_CALIBR, 0x1234);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_MPDDRC_BASE + MPDDRC_IO_CALIBR), ==,
                    0x00871234);
    value = MPDDRC_WPSR_ECLASS | MPDDRC_WPSR_SWE | (3U << 24) |
            (MPDDRC_IO_CALIBR << 8);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPSR), ==,
                    value);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_ISR), ==,
                    1);

    /* A later software error keeps an unread protection source intact. */
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPMR,
                 MPDDRC_WPMR_KEY | MPDDRC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_CR, 0);
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPMR,
                 MPDDRC_WPMR_KEY);
    qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_IER);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPSR), ==,
                    (MPDDRC_CR << 8) | 1 | MPDDRC_WPSR_SWE);

    qtest_quit(qts);
}

static void test_wdt_reset_disable_and_lock(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE " -watchdog-action none");
    uint32_t value;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_MR), ==,
                    WDT_MR_PERIODRST | WDT_MR_RPTHRST);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_WLR), ==,
                    0x00000fff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ILR), ==,
                    0x00000fff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_VR), ==,
                    0x00000fff);

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    value = qtest_readl(qts, SAM9X7_WDT_BASE + WDT_VR);
    qtest_clock_step(qts, 1000000000LL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_VR), ==, value);

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_WLR, (2U << 16) | 7);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_ILR, 3);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_CR,
                 WDT_CR_KEY | WDT_CR_LOCKMR);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, 0);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_WLR, 0xfff);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_ILR, 0xfff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_MR), ==,
                    WDT_MR_WDDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_WLR), ==,
                    (2U << 16) | 7);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ILR), ==, 3);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_MR), ==,
                    WDT_MR_PERIODRST | WDT_MR_RPTHRST);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_MR), ==,
                    WDT_MR_WDDIS);

    qtest_quit(qts);
}

static void test_wdt_events_and_system_irq(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE " -watchdog-action none");
    uint32_t all_events = WDT_INT_PER | WDT_INT_RPTH | WDT_INT_LVL;

    aic_configure(qts, 1, AIC_SMR_LEVEL_HIGH | 5, 0x1d71d71d);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, 0);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_WLR, (4U << 16) | 7);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_ILR, 3);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_IER, all_events);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_IMR), ==,
                    all_events);

    /* Four divided slow-clock ticks take the down-counter from 7 to 3. */
    qtest_clock_step(qts, 4 * 4000000);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ISR), ==,
                    WDT_INT_LVL);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(1));

    /* Four more ticks reach the end of the eight-tick PERIOD=7 cycle. */
    qtest_clock_step(qts, 4 * 4000000);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ISR), ==,
                    WDT_INT_PER);

    /* An immediate reload is inside RPTH=4's forbidden window. */
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_ILR, 0xfff);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_WLR, (4U << 16) | 7);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_CR,
                 WDT_CR_KEY | WDT_CR_WDRSTT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ISR), ==,
                    WDT_INT_RPTH);

    /* Reloading after five watchdog ticks is within the permitted window. */
    qtest_clock_step(qts, 5 * 4000000);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_CR,
                 WDT_CR_KEY | WDT_CR_WDRSTT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ISR), ==, 0);

    /* CR/MR writes inside the three-SLCK synchronization guard end early. */
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_ISR), ==,
                    WDT_INT_PER);

    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_IDR, all_events);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_WDT_BASE + WDT_IMR), ==, 0);

    qtest_quit(qts);
}

static void sdhci_issue_command(QTestState *qts, uint16_t block_size,
                                uint16_t block_count, uint32_t argument,
                                uint16_t transfer_mode, uint16_t command)
{
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_BLKSIZE, block_size);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_BLKCNT, block_count);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ARGUMENT, argument);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_TRNMOD, transfer_mode);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_CMDREG, command);
}

static QTestState *sdhci_start_test_card(const uint8_t *contents,
                                         size_t contents_len,
                                         char **sd_path)
{
    QTestState *qts;
    GError *error = NULL;
    uint16_t rca;
    ssize_t ret;
    int fd;

    fd = g_file_open_tmp("sam9x75-sdhci-XXXXXX", sd_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = write(fd, contents, contents_len);
    g_assert_cmpint(ret, ==, contents_len);
    ret = ftruncate(fd, 1 << 20);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    qts = qtest_initf(SAM9X75_MACHINE
                      " -drive file=%s,if=sd,format=raw,auto-read-only=off",
                      *sd_path);

    ebi_enable_ddr(qts);
    qtest_writeb(qts, SAM9X7_SDMMC0_BASE + SDHCI_SWRST,
                 SDHCI_RESET_ALL);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_CLKCON,
                 SDHCI_CLOCK_ENABLE);

    /* Bring the SD card to transfer state. */
    sdhci_issue_command(qts, 0, 0, 0, 0, SDHCI_CMD_INDEX(55));
    sdhci_issue_command(qts, 0, 0, 0x41200000, 0,
                        SDHCI_CMD_INDEX(41));
    sdhci_issue_command(qts, 0, 0, 0, 0, SDHCI_CMD_INDEX(2));
    sdhci_issue_command(qts, 0, 0, 0, 0,
                        SDHCI_CMD_INDEX(3) | SDHCI_CMD_RESPONSE);
    rca = qtest_readl(qts, SAM9X7_SDMMC0_BASE + SDHCI_RSPREG0) >> 16;
    sdhci_issue_command(qts, 0, 0, (uint32_t)rca << 16, 0,
                        SDHCI_CMD_INDEX(7) | SDHCI_CMD_RESPONSE_BUSY);

    return qts;
}

static void sdhci_enable_adma_interrupts(QTestState *qts)
{
    qtest_writeb(qts, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL,
                 SDHCI_CTRL_ADMA2_32);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTSEN,
                 SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE |
                 SDHCI_INT_DMA);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_ERRINTSTSEN,
                 SDHCI_ERR_ADMA);
}

static void sdhci_assert_adma_fault(QTestState *qts, uint8_t state,
                                    uint32_t descriptor_addr,
                                    bool dma_interrupt)
{
    uint16_t normal_status;

    g_assert_cmphex(qtest_readb(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ADMAERR),
                    ==, state);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR),
                    ==, descriptor_addr);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ERRINTSTS) &
                    SDHCI_ERR_ADMA, !=, 0);

    normal_status = qtest_readw(qts,
                                SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTS);
    g_assert_cmphex(normal_status & SDHCI_INT_ERROR, !=, 0);
    g_assert_cmphex(normal_status & SDHCI_INT_XFER_COMPLETE, ==, 0);
    g_assert_cmphex(normal_status & SDHCI_INT_DMA, ==,
                    dma_interrupt ? SDHCI_INT_DMA : 0);
}

static void test_sdhci_adma2_linux_nop_terminator(void)
{
    const uint64_t descriptor_addr = SAM9X7_DDR_BASE + 0x1000;
    const uint64_t data_addr = SAM9X7_DDR_BASE + 0x2000;
    g_autofree char *sd_path = NULL;
    uint8_t expected[4096];
    uint8_t actual[4096];
    QTestState *qts;
    size_t i;

    for (i = 0; i < sizeof(expected); i++) {
        expected[i] = i ^ 0xa5;
    }

    qts = sdhci_start_test_card(expected, sizeof(expected), &sd_path);

    sdhci_enable_adma_interrupts(qts);
    qtest_writel(qts, descriptor_addr,
                 (sizeof(expected) << 16) |
                 SDHCI_ADMA2_TRAN | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 4, data_addr);
    qtest_writel(qts, descriptor_addr + 8,
                 SDHCI_ADMA2_END | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 12, 0);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);
    /* Match the 4 KiB Linux table sampled on physical SAM9X75 silicon. */
    sdhci_issue_command(qts, 512, sizeof(expected) / 512, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                        SDHCI_TRNS_READ | SDHCI_TRNS_MULTI,
                        SDHCI_CMD_INDEX(18) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    g_assert_cmphex(qtest_readb(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMAERR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR),
                    ==, descriptor_addr + 16);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_BLKCNT),
                    ==, 0);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ERRINTSTS) &
                    SDHCI_ERR_ADMA, ==, 0);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTS) &
                    (SDHCI_INT_ERROR | SDHCI_INT_DMA |
                     SDHCI_INT_XFER_COMPLETE),
                    ==, SDHCI_INT_XFER_COMPLETE);
    qtest_memread(qts, data_addr, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_sdhci_adma2_end_int_success(void)
{
    const uint32_t descriptor_addr = SAM9X7_SRAM0_BASE + 0x1000;
    const uint32_t data_addr = SAM9X7_SRAM0_BASE + 0x2000;
    g_autofree char *sd_path = NULL;
    uint8_t expected[512];
    uint8_t actual[512];
    QTestState *qts;
    size_t i;

    for (i = 0; i < sizeof(expected); i++) {
        expected[i] = i ^ 0xc3;
    }

    qts = sdhci_start_test_card(expected, sizeof(expected), &sd_path);
    sdhci_enable_adma_interrupts(qts);

    qtest_writel(qts, descriptor_addr,
                 (sizeof(expected) << 16) | SDHCI_ADMA2_TRAN |
                 SDHCI_ADMA2_INT | SDHCI_ADMA2_END |
                 SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 4, data_addr);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);

    sdhci_issue_command(qts, sizeof(expected), 1, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                        SDHCI_TRNS_READ | SDHCI_TRNS_MULTI,
                        SDHCI_CMD_INDEX(18) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    g_assert_cmphex(qtest_readb(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ADMAERR),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR),
                    ==, descriptor_addr + 8);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ERRINTSTS) &
                    SDHCI_ERR_ADMA, ==, 0);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTS) &
                    (SDHCI_INT_ERROR | SDHCI_INT_DMA |
                     SDHCI_INT_XFER_COMPLETE),
                    ==, SDHCI_INT_DMA | SDHCI_INT_XFER_COMPLETE);
    qtest_memread(qts, data_addr, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_sdhci_adma2_overlong_table_mismatch(void)
{
    const uint32_t descriptor_addr = SAM9X7_SRAM0_BASE + 0x1000;
    const uint32_t data_addr = SAM9X7_SRAM0_BASE + 0x2000;
    g_autofree char *sd_path = NULL;
    uint8_t expected[512];
    uint8_t actual[1024];
    QTestState *qts;
    size_t i;

    for (i = 0; i < sizeof(expected); i++) {
        expected[i] = i ^ 0x3c;
    }

    qts = sdhci_start_test_card(expected, sizeof(expected), &sd_path);
    sdhci_enable_adma_interrupts(qts);
    qtest_memset(qts, data_addr, 0xa7, sizeof(actual));

    qtest_writel(qts, descriptor_addr,
                 (sizeof(expected) << 16) |
                 SDHCI_ADMA2_TRAN | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 4, data_addr);
    qtest_writel(qts, descriptor_addr + 8,
                 (sizeof(expected) << 16) | SDHCI_ADMA2_TRAN |
                 SDHCI_ADMA2_INT | SDHCI_ADMA2_END |
                 SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 12,
                 data_addr + sizeof(expected));
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);

    /* The descriptor table must not transfer beyond the requested block. */
    sdhci_issue_command(qts, sizeof(expected), 1, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                        SDHCI_TRNS_READ | SDHCI_TRNS_MULTI,
                        SDHCI_CMD_INDEX(18) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    sdhci_assert_adma_fault(qts,
                            SDHCI_ADMA_LENGTH_MISMATCH |
                            SDHCI_ADMA_STATE_TFR,
                            descriptor_addr + 16, false);
    qtest_memread(qts, data_addr, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(expected), expected, sizeof(expected));
    for (i = sizeof(expected); i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0xa7);
    }

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_sdhci_adma2_block_count_mismatch(void)
{
    const uint32_t descriptor_addr = SAM9X7_SRAM0_BASE + 0x1000;
    const uint32_t data_addr = SAM9X7_SRAM0_BASE + 0x2000;
    g_autofree char *sd_path = NULL;
    uint8_t expected[512];
    uint8_t actual[512];
    QTestState *qts;
    size_t i;

    for (i = 0; i < sizeof(expected); i++) {
        expected[i] = i ^ 0x96;
    }

    qts = sdhci_start_test_card(expected, sizeof(expected), &sd_path);
    sdhci_enable_adma_interrupts(qts);

    qtest_writel(qts, descriptor_addr,
                 (sizeof(expected) << 16) | SDHCI_ADMA2_TRAN |
                 SDHCI_ADMA2_INT | SDHCI_ADMA2_END |
                 SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 4, data_addr);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);

    /* The table describes one block, while the command requests two. */
    sdhci_issue_command(qts, sizeof(expected), 2, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                        SDHCI_TRNS_READ | SDHCI_TRNS_MULTI,
                        SDHCI_CMD_INDEX(18) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    sdhci_assert_adma_fault(qts,
                            SDHCI_ADMA_LENGTH_MISMATCH |
                            SDHCI_ADMA_STATE_TFR,
                            descriptor_addr + 8, true);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_BLKCNT),
                    ==, 1);
    qtest_memread(qts, data_addr, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_sdhci_adma2_partial_end_nop_mismatch(void)
{
    const uint32_t descriptor_addr = SAM9X7_SRAM0_BASE + 0x1000;
    const uint32_t data_addr = SAM9X7_SRAM0_BASE + 0x2000;
    g_autofree char *sd_path = NULL;
    uint8_t expected[512];
    uint8_t actual[512];
    QTestState *qts;
    size_t i;

    for (i = 0; i < sizeof(expected); i++) {
        expected[i] = i ^ 0x69;
    }

    qts = sdhci_start_test_card(expected, sizeof(expected), &sd_path);
    sdhci_enable_adma_interrupts(qts);
    qtest_memset(qts, data_addr, 0xa7, sizeof(actual));

    /* A partial transfer followed by Linux's END/NOP must not succeed. */
    qtest_writel(qts, descriptor_addr,
                 (128U << 16) | SDHCI_ADMA2_TRAN | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 4, data_addr);
    qtest_writel(qts, descriptor_addr + 8,
                 SDHCI_ADMA2_INT | SDHCI_ADMA2_END | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 12, 0);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);

    sdhci_issue_command(qts, sizeof(expected), 1, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_READ,
                        SDHCI_CMD_INDEX(17) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    sdhci_assert_adma_fault(qts,
                            SDHCI_ADMA_LENGTH_MISMATCH |
                            SDHCI_ADMA_STATE_STOP,
                            descriptor_addr + 16, true);
    qtest_memread(qts, data_addr, actual, sizeof(actual));
    g_assert_cmpmem(actual, 128, expected, 128);
    for (i = 128; i < sizeof(actual); i++) {
        g_assert_cmphex(actual[i], ==, 0xa7);
    }

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_sdhci_adma2_descriptor_tail_fault(void)
{
    const uint32_t descriptor_addr = SAM9X7_SRAM1_BASE + 0xffc;
    g_autofree char *sd_path = NULL;
    uint8_t sector[512] = {};
    QTestState *qts;

    qts = sdhci_start_test_card(sector, sizeof(sector), &sd_path);
    sdhci_enable_adma_interrupts(qts);

    /*
     * Only the first half of this descriptor is mapped.  If the failed
     * eight-byte fetch were ignored, these attributes would complete the
     * command as a valid END/NOP descriptor.
     */
    qtest_writel(qts, descriptor_addr,
                 SDHCI_ADMA2_VALID | SDHCI_ADMA2_END | SDHCI_ADMA2_INT);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);

    sdhci_issue_command(qts, sizeof(sector), 1, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                        SDHCI_TRNS_READ,
                        SDHCI_CMD_INDEX(17) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    sdhci_assert_adma_fault(qts, SDHCI_ADMA_STATE_FDS, descriptor_addr,
                            false);

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_sdhci_adma2_mmio_descriptor_fault(void)
{
    const uint32_t descriptor_addr = SAM9X7_SDMMC0_BASE;
    g_autofree char *sd_path = NULL;
    uint8_t sector[512] = {};
    QTestState *qts;

    qts = sdhci_start_test_card(sector, sizeof(sector), &sd_path);
    sdhci_enable_adma_interrupts(qts);

    /*
     * Make the controller's first eight MMIO bytes look like a valid
     * END/NOP descriptor.  ADMA fetches are memory-only bus transactions
     * and must reject this device region instead of executing it.
     */
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_SYSAD,
                 SDHCI_ADMA2_VALID | SDHCI_ADMA2_END | SDHCI_ADMA2_INT);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);

    sdhci_issue_command(qts, sizeof(sector), 1, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                        SDHCI_TRNS_READ,
                        SDHCI_CMD_INDEX(17) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    sdhci_assert_adma_fault(qts, SDHCI_ADMA_STATE_FDS, descriptor_addr,
                            false);

    qtest_quit(qts);
    unlink(sd_path);
}

static void test_sdhci_adma2_data_fault(bool card_to_memory)
{
    const uint32_t descriptor_addr = SAM9X7_SRAM0_BASE + 0x1000;
    const uint32_t fault_addr = SAM9X7_SDMMC1_BASE;
    g_autofree char *disk_contents = NULL;
    g_autofree char *sd_path = NULL;
    uint8_t sector[512];
    QTestState *qts;
    GError *error = NULL;
    gsize disk_len;
    uint32_t attributes = (sizeof(sector) << 16) | SDHCI_ADMA2_TRAN |
                          SDHCI_ADMA2_INT | SDHCI_ADMA2_VALID;
    uint16_t transfer_mode = SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                             SDHCI_TRNS_MULTI;
    uint16_t command;
    size_t i;

    for (i = 0; i < sizeof(sector); i++) {
        sector[i] = i ^ 0x5a;
    }

    qts = sdhci_start_test_card(sector, sizeof(sector), &sd_path);
    sdhci_enable_adma_interrupts(qts);

    if (card_to_memory) {
        attributes |= SDHCI_ADMA2_END;
    }
    qtest_writel(qts, descriptor_addr, attributes);
    qtest_writel(qts, descriptor_addr + 4, fault_addr);
    qtest_writel(qts, descriptor_addr + 8,
                 SDHCI_ADMA2_END | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 12, 0);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);

    if (card_to_memory) {
        transfer_mode |= SDHCI_TRNS_READ;
        command = SDHCI_CMD_INDEX(18);
    } else {
        command = SDHCI_CMD_INDEX(25);
    }

    sdhci_issue_command(qts, sizeof(sector), 1, 0, transfer_mode,
                        command | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);

    /* Neither END handling nor a later retry may continue after a fault. */
    qtest_clock_step(qts, 1000);
    sdhci_assert_adma_fault(qts, SDHCI_ADMA_STATE_TFR,
                            descriptor_addr + 8, false);
    qtest_clock_step(qts, 1000);
    sdhci_assert_adma_fault(qts, SDHCI_ADMA_STATE_TFR,
                            descriptor_addr + 8, false);

    qtest_quit(qts);

    if (!card_to_memory) {
        g_file_get_contents(sd_path, &disk_contents, &disk_len, &error);
        g_assert_no_error(error);
        g_assert_cmpuint(disk_len, >=, sizeof(sector));
        g_assert_cmpmem(disk_contents, sizeof(sector),
                        sector, sizeof(sector));
    }
    unlink(sd_path);
}

static void test_sdhci_adma2_read_data_fault(void)
{
    test_sdhci_adma2_data_fault(true);
}

static void test_sdhci_adma2_write_data_fault(void)
{
    test_sdhci_adma2_data_fault(false);
}

static void test_sdhci_preset_registers(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL2, UINT16_MAX);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL2),
                    ==, SDHCI_HOSTCTL2_PRESET | SDHCI_HOSTCTL2_ASYNC);

    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_INIT), ==, 0);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_DEFAULT), ==, 0);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_HIGH_SPEED), ==, 0);

    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_PRESET_INIT, UINT16_MAX);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_PRESET_DEFAULT, 0x0404);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_PRESET_HIGH_SPEED, 0x0201);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_INIT), ==, 0x07ff);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_DEFAULT), ==, 0x0404);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_HIGH_SPEED), ==, 0x0201);

    /* Linux's SAM9X60 fallback writes these reserved SAM9X7 slots. */
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_PRESET_SDR12, 0x0404);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_PRESET_DDR50, 0x0401);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_SDR12), ==, 0);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_DDR50), ==, 0);

    qtest_writeb(qts, SAM9X7_SDMMC0_BASE + SDHCI_SWRST,
                 SDHCI_RESET_ALL);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_DEFAULT), ==, 0x0404);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_DEFAULT), ==, 0);

    qtest_quit(qts);
}

static void test_sdhci_host_control2_migration(void)
{
    const uint16_t hostctl2 = SDHCI_HOSTCTL2_PRESET;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    qtest_writew(from, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL2, hostctl2);
    g_assert_cmphex(qtest_readw(from, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL2),
                    ==, hostctl2);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readw(to, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL2),
                    ==, hostctl2);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_sdhci_software_reset_all(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_PRESET_DEFAULT, 0x0404);
    qtest_writeb(qts, SAM9X7_SDMMC0_BASE + SDMMC_MC1R, 0x3b);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDMMC_ACR, 3);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDMMC_CC2R, 1);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDMMC_CACR,
                 SDMMC_CACR_KEY | SDMMC_CACR_CAPWREN);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDMMC_DBGR, 1);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL2,
                 SDHCI_HOSTCTL2_PRESET);

    qtest_writeb(qts, SAM9X7_SDMMC0_BASE + SDHCI_SWRST,
                 SDHCI_RESET_ALL | SDHCI_RESET_CMD | SDHCI_RESET_DATA);

    g_assert_cmphex(qtest_readb(qts, SAM9X7_SDMMC0_BASE + SDMMC_MC1R),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SDMMC0_BASE + SDMMC_ACR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SDMMC0_BASE + SDMMC_CC2R), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SDMMC0_BASE + SDMMC_CACR), ==, 0);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL2),
                    ==, 0);

    /* SRR.SWRSTALL does not include PVR or DBGR. */
    g_assert_cmphex(qtest_readw(qts, SAM9X7_SDMMC0_BASE +
                                SDHCI_PRESET_DEFAULT), ==, 0x0404);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SDMMC0_BASE + SDMMC_DBGR), ==, 1);

    qtest_quit(qts);
}

static void test_sdhci_software_reset_command_irq(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    aic_configure(qts, 12, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0012);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_CLKCON,
                 SDHCI_CLOCK_ENABLE);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTSEN,
                 SDHCI_INT_CMD_COMPLETE);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_NORINTSIGEN,
                 SDHCI_INT_CMD_COMPLETE);
    sdhci_issue_command(qts, 0, 0, 0, 0, SDHCI_CMD_INDEX(0));

    g_assert_true(qtest_readw(qts, SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTS) &
                  SDHCI_INT_CMD_COMPLETE);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(12));

    qtest_writeb(qts, SAM9X7_SDMMC0_BASE + SDHCI_SWRST,
                 SDHCI_RESET_CMD);
    g_assert_false(qtest_readw(qts, SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTS) &
                   SDHCI_INT_CMD_COMPLETE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(12));

    qtest_quit(qts);
}

static uint8_t flexcom_spi_transfer_byte(QTestState *qts, uint64_t base,
                                         unsigned int pid, uint8_t value,
                                         unsigned int divisor)
{
    uint64_t transfer_ns = usart_cycles_to_ns(qts, pid,
                                               1 + 8 * divisor);

    qtest_writeb(qts, base + SPI_TDR, value);
    qtest_clock_step(qts, transfer_ns);
    g_assert_true(qtest_readl(qts, base + SPI_SR) & SPI_INT_RDRF);
    return qtest_readb(qts, base + SPI_RDR);
}

static void test_board_m2_interface_jumper(void)
{
    static const uint8_t cmd0[] = { 0x40, 0, 0, 0, 0, 0x95 };
    g_autofree char *sd_path = NULL;
    QDict *response;
    QTestState *qts;
    GError *error = NULL;
    uint8_t value = 0xff;
    unsigned int i;
    int fd;
    int ret;

    fd = g_file_open_tmp("sam9x75-m2-sd-XXXXXX", &sd_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = ftruncate(fd, 1 << 20);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    qts = qtest_initf(SAM9X75_MACHINE
                      " -drive file=%s,if=sd,index=1,format=raw,"
                      "auto-read-only=off", sd_path);
    response = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':'/machine',"
        "'property':'m2-interface'}}");
    g_assert_false(qdict_haskey(response, "error"));
    g_assert_cmpstr(qdict_get_str(response, "return"), ==, "sdio");
    qobject_unref(response);
    qtest_clock_step(qts, 1000);
    g_assert_true(qtest_readl(qts, SAM9X7_SDMMC1_BASE + SDHCI_PRNSTS) &
                  SDHCI_CARD_PRESENT);
    qtest_quit(qts);

    qts = qtest_initf(SAM9X75_MACHINE
                      ",m2-interface=spi"
                      " -drive file=%s,if=sd,index=1,format=raw,"
                      "auto-read-only=off", sd_path);
    response = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':'/machine',"
        "'property':'m2-interface'}}");
    g_assert_false(qdict_haskey(response, "error"));
    g_assert_cmpstr(qdict_get_str(response, "return"), ==, "spi");
    qobject_unref(response);
    g_assert_false(qtest_readl(qts, SAM9X7_SDMMC1_BASE + SDHCI_PRNSTS) &
                   SDHCI_CARD_PRESENT);

    pmc_write_pcr(qts, 13, PMC_PCR_EN);
    qtest_writeb(qts, SAM9X7_FLEXCOM4_BASE + FLEX_MR, FLEX_MODE_SPI);
    qtest_writel(qts, SAM9X7_SPI4_BASE + SPI_MR,
                 SPI_MR_MSTR | SPI_MR_PCS(0xd));
    qtest_writel(qts, SAM9X7_SPI4_BASE + SPI_CSR(1),
                 SPI_CSR_CSAAT | SPI_CSR_BITS(8) |
                 SPI_CSR_SCBR(217) | SPI_CSR_DLYBS(1));
    qtest_writel(qts, SAM9X7_SPI4_BASE + SPI_CR, SPI_CR_SPIEN);

    for (i = 0; i < 10; i++) {
        g_assert_cmphex(flexcom_spi_transfer_byte(qts, SAM9X7_SPI4_BASE,
                                                 13, 0xff, 217), ==, 0xff);
    }
    for (i = 0; i < ARRAY_SIZE(cmd0); i++) {
        g_assert_cmphex(flexcom_spi_transfer_byte(qts, SAM9X7_SPI4_BASE,
                                                 13, cmd0[i], 217), ==,
                        0xff);
    }
    for (i = 0; i < 8 && value == 0xff; i++) {
        value = flexcom_spi_transfer_byte(qts, SAM9X7_SPI4_BASE,
                                          13, 0xff, 217);
    }
    g_assert_cmphex(value, ==, 0x01);
    qtest_writel(qts, SAM9X7_SPI4_BASE + SPI_CR, SPI_CR_LASTXFER);
    qtest_quit(qts);

    unlink(sd_path);
}

static void nand_command(QTestState *qts, uint8_t command)
{
    qtest_writeb(qts, NAND_CLE, command);
}

static void nand_address(QTestState *qts, uint8_t address)
{
    qtest_writeb(qts, NAND_ALE, address);
}

static void nand_page_address(QTestState *qts, uint32_t column,
                              uint32_t page)
{
    nand_address(qts, column);
    nand_address(qts, column >> 8);
    nand_address(qts, page);
    nand_address(qts, page >> 8);
    nand_address(qts, page >> 16);
}

static void nand_program(QTestState *qts, uint32_t page, uint32_t column,
                         const uint8_t *data, size_t length)
{
    size_t i;

    nand_command(qts, NAND_CMD_PROGRAM_START);
    nand_page_address(qts, column, page);
    for (i = 0; i < length; i++) {
        qtest_writeb(qts, NAND_DATA, data[i]);
    }
    nand_command(qts, NAND_CMD_PAGE_PROGRAM);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
}

static void nand_start_read(QTestState *qts, uint32_t page, uint32_t column)
{
    nand_command(qts, NAND_CMD_READ0);
    nand_page_address(qts, column, page);
    nand_command(qts, NAND_CMD_READ_START);
}

static void nand_set_features(QTestState *qts, uint8_t address,
                              const uint8_t *data, size_t length)
{
    size_t i;

    nand_command(qts, NAND_CMD_SET_FEATURES);
    nand_address(qts, address);
    for (i = 0; i < length; i++) {
        qtest_writeb(qts, NAND_DATA, data[i]);
    }
}

static void nand_get_features(QTestState *qts, uint8_t address,
                              uint8_t data[4])
{
    unsigned int i;

    nand_command(qts, NAND_CMD_GET_FEATURES);
    nand_address(qts, address);
    for (i = 0; i < 4; i++) {
        data[i] = qtest_readb(qts, NAND_DATA);
    }
}

static void nand_migrate(QTestState *from, QTestState *to)
{
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);
}

static void test_ebi_chip_select_assignments(void)
{
    const uint32_t reset_assignments = 0x00000300;
    const uint32_t ddr_marker = 0x9750cafe;
    const uint32_t assignments = SFR_CCFG_EBI_CS1A |
                                 SFR_CCFG_EBI_CS2A |
                                 SFR_CCFG_NFD0_ON_D16;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t assigned;

    g_assert_cmphex(ebi_read_assignments(qts), ==, reset_assignments);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0);

    qtest_writel(qts, SAM9X7_DDR_BASE, UINT32_MAX);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_REMAP_MP_DDR, 0x3fff);
    ebi_update_assignments(qts, SFR_CCFG_EBI_CS1A,
                           SFR_CCFG_DDR_MP_EN);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_SFR_BASE + SFR_REMAP_MP_DDR), ==,
                    0x3fff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0);

    /* CS1A alone controls decode, irrespective of the multi-port route. */
    ebi_update_assignments(qts, 0, SFR_CCFG_EBI_CS1A);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0);
    qtest_writel(qts, SAM9X7_DDR_BASE, ddr_marker);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, ddr_marker);
    ebi_update_assignments(qts, SFR_CCFG_EBI_CS1A, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0);
    qtest_writel(qts, SAM9X7_DDR_BASE, UINT32_MAX);
    ebi_enable_ddr(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, ddr_marker);
    ebi_update_assignments(qts, SFR_CCFG_DDR_MP_EN, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, ddr_marker);
    ebi_update_assignments(qts, 0, SFR_CCFG_DDR_MP_EN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, ddr_marker);

    /* CS1A cannot substitute for either of the two NAND assignments. */
    ebi_update_assignments(qts,
                           SFR_CCFG_EBI_CS2A | SFR_CCFG_NFD0_ON_D16,
                           SFR_CCFG_EBI_CS2A);
    nand_command(qts, NAND_CMD_READ_ID);
    nand_address(qts, 0x00);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0);

    ebi_update_assignments(qts, 0, SFR_CCFG_NFD0_ON_D16);
    nand_command(qts, NAND_CMD_READ_ID);
    nand_address(qts, 0x00);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0xc2);

    /* NAND assignment cannot keep DDR decoded without CS1A. */
    ebi_update_assignments(qts, SFR_CCFG_EBI_CS1A, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0);
    ebi_update_assignments(qts, 0, SFR_CCFG_EBI_CS1A);

    /* A disabled window neither advances nor mutates the NAND protocol. */
    ebi_update_assignments(qts, SFR_CCFG_EBI_CS2A, 0);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0);
    nand_command(qts, NAND_CMD_STATUS);
    ebi_update_assignments(qts, 0, SFR_CCFG_EBI_CS2A);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0xdc);

    ebi_update_assignments(qts, SFR_CCFG_NFD0_ON_D16, 0);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0);
    nand_command(qts, NAND_CMD_STATUS);
    ebi_update_assignments(qts, 0, SFR_CCFG_NFD0_ON_D16);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0x90);

    assigned = ebi_read_assignments(qts);
    g_assert_cmphex(assigned & assignments, ==, assignments);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_WPMR,
                 SFR_WPMR_KEY | SFR_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_CCFG_EBICSA,
                 assigned & ~assignments);
    g_assert_cmphex(ebi_read_assignments(qts), ==, assigned);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, ddr_marker);

    qtest_writel(qts, SAM9X7_SFR_BASE + SFR_WPMR, SFR_WPMR_KEY);
    ebi_update_assignments(qts, assignments | SFR_CCFG_DDR_MP_EN, 0);
    g_assert_cmphex(ebi_read_assignments(qts), ==, reset_assignments);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0);

    qtest_quit(qts);
}

static void test_nand_cs2_mirroring(void)
{
    static const uint8_t id[] = { 0xc2, 0xdc, 0x90, 0xa2, 0x57, 0x03 };
    const uint64_t aperture_end = SAM9X7_NAND_BASE + SAM9X7_NAND_SIZE;
    const uint64_t top_data = aperture_end - SAM9X7_NAND_SLICE_SIZE;
    const unsigned int num_slices =
        SAM9X7_NAND_SIZE / SAM9X7_NAND_SLICE_SIZE;
    unsigned int slice;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    ebi_enable_nand(qts);
    for (slice = 0; slice < num_slices; slice++) {
        uint64_t command = SAM9X7_NAND_BASE +
                           slice * SAM9X7_NAND_SLICE_SIZE;
        uint64_t address = SAM9X7_NAND_BASE +
                           ((slice + 1) % num_slices) *
                           SAM9X7_NAND_SLICE_SIZE;
        unsigned int i;

        qtest_writeb(qts, command + BIT(22), NAND_CMD_READ_ID);
        qtest_writeb(qts, address + BIT(21), slice & 1 ? 0x20 : 0x00);
        if (slice & 1) {
            for (i = 0; i < 4; i++) {
                uint64_t data = SAM9X7_NAND_BASE +
                    ((slice + i + 2) % num_slices) *
                    SAM9X7_NAND_SLICE_SIZE;

                g_assert_cmphex(qtest_readb(qts, data), ==, "ONFI"[i]);
            }
        } else {
            for (i = 0; i < ARRAY_SIZE(id); i++) {
                uint64_t data = SAM9X7_NAND_BASE +
                    ((slice + i + 2) % num_slices) *
                    SAM9X7_NAND_SLICE_SIZE;

                g_assert_cmphex(qtest_readb(qts, data), ==, id[i]);
            }
        }
    }

    /* The canonical and top mirrors also share one protocol cursor. */
    qtest_writeb(qts, top_data + BIT(22), NAND_CMD_READ_ID);
    qtest_writeb(qts, NAND_ALE, 0x20);
    g_assert_cmphex(qtest_readb(qts, top_data), ==, 'O');
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 'N');

    qtest_writeb(qts, top_data + BIT(22), NAND_CMD_READ_ID);
    qtest_writeb(qts, top_data + BIT(21), 0x20);
    qtest_writeb(qts, aperture_end + BIT(22), NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 'O');
    g_assert_cmphex(qtest_readb(qts, aperture_end), ==, 0);

    qtest_quit(qts);
}

static void test_ebi_assignment_migration_reset(void)
{
    static const uint8_t nand_payload[] = { 0x6d, 0xb2 };
    const uint32_t reset_assignments = 0x00000300;
    const uint32_t enabled_assignments = reset_assignments |
                                         SFR_CCFG_EBI_CS1A |
                                         SFR_CCFG_EBI_CS2A |
                                         SFR_CCFG_NFD0_ON_D16;
    const uint32_t ddr_marker = 0x51a9c75a;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint32_t value;

    g_assert_cmphex(ebi_read_assignments(to), ==, reset_assignments);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0);

    ebi_enable_ddr_and_nand(from);
    qtest_writel(from, SAM9X7_DDR_BASE, ddr_marker);
    nand_program(from, 23, 31, nand_payload, sizeof(nand_payload));
    nand_command(from, NAND_CMD_READ_ID);
    nand_address(from, 0x00);
    g_assert_cmphex(qtest_readb(from, NAND_DATA), ==, 0xc2);

    nand_migrate(from, to);
    g_assert_cmphex(ebi_read_assignments(to), ==, enabled_assignments);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, ddr_marker);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0xdc);

    qtest_writel(to, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    qtest_writel(to, SAM9X7_RSTC_BASE + RSTC_CR,
                 RSTC_KEY | RSTC_CR_PROCRST);
    qtest_qmp_eventwait(to, "RESET");
    value = qtest_readl(to, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_SOFTWARE));
    g_assert_cmphex(ebi_read_assignments(to), ==, reset_assignments);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0);

    ebi_enable_ddr_and_nand(to);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, ddr_marker);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0x90);

    qtest_system_reset(to);
    g_assert_cmphex(ebi_read_assignments(to), ==, reset_assignments);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0);

    ebi_enable_ddr_and_nand(to);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, ddr_marker);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0xff);
    nand_start_read(to, 23, 31);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, nand_payload[0]);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, nand_payload[1]);
    nand_command(to, NAND_CMD_READ_ID);
    nand_address(to, 0x00);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0xc2);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_ebi_assignment_migration_deassert(void)
{
    const uint32_t partial_assignments = 0x00000300 |
                                         SFR_CCFG_NFD0_ON_D16;
    const uint32_t source_marker = 0xdec0de75;
    const uint32_t destination_marker = 0xbad0cafe;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    ebi_enable_ddr_and_nand(from);
    qtest_writel(from, SAM9X7_DDR_BASE, source_marker);
    nand_command(from, NAND_CMD_READ_ID);
    nand_address(from, 0x00);
    g_assert_cmphex(qtest_readb(from, NAND_DATA), ==, 0xc2);
    ebi_update_assignments(from,
                           SFR_CCFG_EBI_CS1A | SFR_CCFG_EBI_CS2A, 0);
    g_assert_cmphex(ebi_read_assignments(from), ==, partial_assignments);
    g_assert_cmphex(qtest_readl(from, SAM9X7_DDR_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(from, NAND_DATA), ==, 0);

    /* Migration must deassert stale destination routes as well as assert. */
    ebi_enable_ddr_and_nand(to);
    qtest_writel(to, SAM9X7_DDR_BASE, destination_marker);
    nand_command(to, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, NAND_STATUS_IDLE);

    nand_migrate(from, to);
    g_assert_cmphex(ebi_read_assignments(to), ==, partial_assignments);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0);

    ebi_update_assignments(to, 0,
                           SFR_CCFG_EBI_CS1A | SFR_CCFG_EBI_CS2A);
    g_assert_cmphex(qtest_readl(to, SAM9X7_DDR_BASE), ==, source_marker);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0xdc);

    qtest_quit(to);
    qtest_quit(from);
}

static const uint8_t nand_parameter_page[256] = {
    0x4f, 0x4e, 0x46, 0x49, 0x02, 0x00, 0x18, 0x00,
    0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4d, 0x41, 0x43, 0x52, 0x4f, 0x4e, 0x49, 0x58,
    0x20, 0x20, 0x20, 0x20, 0x4d, 0x58, 0x33, 0x30,
    0x4c, 0x46, 0x34, 0x47, 0x32, 0x38, 0x41, 0x44,
    0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04,
    0x00, 0x00, 0x40, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x08, 0x00, 0x00, 0x01, 0x23, 0x01, 0x28,
    0x00, 0x06, 0x04, 0x08, 0x00, 0x00, 0x04, 0x00,
    0x08, 0x01, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0a, 0x3f, 0x00, 0x3f, 0x00, 0xbc, 0x02, 0x70,
    0x17, 0x19, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8d, 0xed,
};

static void test_nand_identification_program_and_erase(void)
{
    static const uint8_t expected_id[] = {
        0xc2, 0xdc, 0x90, 0xa2, 0x57, 0x03,
    };
    static const uint8_t payload[] = {
        0x5a, 0xa5, 0x00, 0xff, 0x36, 0xc9,
    };
    static const uint8_t oob_payload[] = {
        0x69, 0x96, 0x3c,
    };
    static const uint8_t feature[] = {
        0x03, 0x00, 0x00, 0x00,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t parameter_page[256];
    uint8_t redundant_page[256];
    unsigned int copy;
    unsigned int i;

    ebi_enable_nand(qts);
    g_assert_true(qtest_qom_get_bool(qts, "/machine", "nand-cs"));
    g_assert_true(qtest_qom_get_bool(qts, "/machine", "qspi-cs"));

    nand_command(qts, NAND_CMD_READ_ID);
    nand_address(qts, 0x00);
    for (i = 0; i < G_N_ELEMENTS(expected_id); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, expected_id[i]);
    }

    nand_command(qts, NAND_CMD_READ_ID);
    nand_address(qts, 0x20);
    for (i = 0; i < 4; i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, "ONFI"[i]);
    }

    nand_command(qts, NAND_CMD_READ_PARAM);
    nand_address(qts, 0x00);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
    /* Repeated status commands must retain the suspended data producer. */
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_command(qts, NAND_CMD_READ0);
    for (i = 0; i < sizeof(parameter_page); i++) {
        parameter_page[i] = qtest_readb(qts, NAND_DATA);
    }
    g_assert_cmpmem(parameter_page, sizeof(parameter_page),
                    nand_parameter_page, sizeof(nand_parameter_page));
    g_assert_cmphex(lduw_le_p(parameter_page + 254), ==, 0xed8d);
    for (copy = 1; copy < 8; copy++) {
        for (i = 0; i < sizeof(redundant_page); i++) {
            redundant_page[i] = qtest_readb(qts, NAND_DATA);
        }
        g_assert_cmpmem(redundant_page, sizeof(redundant_page),
                        parameter_page, sizeof(parameter_page));
    }

    nand_command(qts, NAND_CMD_SET_FEATURES);
    nand_address(qts, 0x01);
    for (i = 0; i < G_N_ELEMENTS(feature); i++) {
        qtest_writeb(qts, NAND_DATA, feature[i]);
    }
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_command(qts, NAND_CMD_GET_FEATURES);
    nand_address(qts, 0x01);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_command(qts, NAND_CMD_READ0);
    for (i = 0; i < G_N_ELEMENTS(feature); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, feature[i]);
    }

    nand_command(qts, NAND_CMD_PROGRAM_START);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    for (i = 0; i < G_N_ELEMENTS(payload); i++) {
        qtest_writeb(qts, NAND_DATA, payload[i]);
    }
    nand_command(qts, NAND_CMD_PAGE_PROGRAM);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);

    nand_command(qts, NAND_CMD_PROGRAM_START);
    nand_address(qts, NAND_TEST_OOB_COLUMN & 0xff);
    nand_address(qts, NAND_TEST_OOB_COLUMN >> 8);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    for (i = 0; i < G_N_ELEMENTS(oob_payload); i++) {
        qtest_writeb(qts, NAND_DATA, oob_payload[i]);
    }
    nand_command(qts, NAND_CMD_PAGE_PROGRAM);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);

    nand_command(qts, NAND_CMD_READ0);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_command(qts, NAND_CMD_READ_START);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_command(qts, NAND_CMD_READ0);
    for (i = 0; i < G_N_ELEMENTS(payload); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, payload[i]);
    }

    nand_command(qts, NAND_CMD_READ0);
    nand_address(qts, NAND_TEST_OOB_COLUMN & 0xff);
    nand_address(qts, NAND_TEST_OOB_COLUMN >> 8);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_command(qts, NAND_CMD_READ_START);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_command(qts, NAND_CMD_READ0);
    for (i = 0; i < G_N_ELEMENTS(oob_payload); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, oob_payload[i]);
    }

    nand_command(qts, NAND_CMD_ERASE);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_command(qts, NAND_CMD_ERASE_START);
    nand_command(qts, NAND_CMD_READ0);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_command(qts, NAND_CMD_READ_START);
    for (i = 0; i < G_N_ELEMENTS(payload); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0xff);
    }

    nand_command(qts, NAND_CMD_READ0);
    nand_address(qts, NAND_TEST_OOB_COLUMN & 0xff);
    nand_address(qts, NAND_TEST_OOB_COLUMN >> 8);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_command(qts, NAND_CMD_READ_START);
    for (i = 0; i < G_N_ELEMENTS(oob_payload); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0xff);
    }

    qtest_quit(qts);
}

static void test_nand_features_and_reset_domains(void)
{
    static const struct {
        uint8_t address;
        uint8_t value[4];
    } feature_cases[] = {
        /* Emulator-only: do not replay these writes on physical NAND. */
        { 0x01, { 4, 0, 0, 0 } },
        { 0x80, { 1, 0, 0, 0 } },
        { 0x89, { 5, 0, 0, 0 } },
        { 0x90, { 3, 0, 0, 0 } },
        { 0xb0, { 7, 0, 0, 0 } },
    };
    static const uint8_t timing_mode_0[4] = { 0, 0, 0, 0 };
    static const uint8_t timing_mode_3[4] = { 3, 0, 0, 0 };
    static const uint8_t timing_mode_4_reserved[4] = {
        0xf4, 0xaa, 0x55, 0xff,
    };
    static const uint8_t timing_mode_4[4] = { 4, 0, 0, 0 };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t feature[4];
    uint32_t value;
    unsigned int i;

    ebi_enable_nand(qts);
    qtest_writel(qts, SAM9X7_WDT_BASE + WDT_MR, WDT_MR_WDDIS);
    for (i = 0; i < G_N_ELEMENTS(feature_cases); i++) {
        nand_get_features(qts, feature_cases[i].address, feature);
        g_assert_cmpmem(feature, sizeof(feature), timing_mode_0,
                        sizeof(timing_mode_0));
        nand_set_features(qts, feature_cases[i].address,
                          feature_cases[i].value,
                          sizeof(feature_cases[i].value));
        nand_get_features(qts, feature_cases[i].address, feature);
        g_assert_cmpmem(feature, sizeof(feature), feature_cases[i].value,
                        sizeof(feature_cases[i].value));
    }
    /* U5.PT is unconnected and pulled low, so feature A0h is invalid. */
    nand_get_features(qts, 0xa0, feature);
    g_assert_cmpmem(feature, sizeof(feature), timing_mode_0,
                    sizeof(timing_mode_0));

    nand_set_features(qts, 0x01, timing_mode_3, sizeof(timing_mode_3));
    nand_get_features(qts, 0x01, feature);
    g_assert_cmpmem(feature, sizeof(feature), timing_mode_3,
                    sizeof(timing_mode_3));

    /* Deterministic emulator policy for a malformed interruption before P4. */
    nand_set_features(qts, 0x01, timing_mode_4, 1);
    nand_command(qts, NAND_CMD_READ_ID);
    nand_address(qts, 0x00);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0xc2);
    nand_get_features(qts, 0x01, feature);
    g_assert_cmpmem(feature, sizeof(feature), timing_mode_3,
                    sizeof(timing_mode_3));

    /* Reserved parameter bits and P2-P4 do not affect the target. */
    nand_set_features(qts, 0x01, timing_mode_4_reserved,
                      sizeof(timing_mode_4_reserved));
    nand_get_features(qts, 0x01, feature);
    g_assert_cmpmem(feature, sizeof(feature), timing_mode_4,
                    sizeof(timing_mode_4));

    nand_command(qts, NAND_CMD_RESET);
    nand_command(qts, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, NAND_STATUS_IDLE);
    for (i = 0; i < G_N_ELEMENTS(feature_cases); i++) {
        nand_get_features(qts, feature_cases[i].address, feature);
        g_assert_cmpmem(feature, sizeof(feature), feature_cases[i].value,
                        sizeof(feature_cases[i].value));
    }

    /* The external NAND protocol and volatile features survive PROCRST. */
    nand_command(qts, NAND_CMD_GET_FEATURES);
    nand_address(qts, 0x01);
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 4);
    qtest_writel(qts, SAM9X7_RSTC_BASE + RSTC_CR,
                 RSTC_KEY | RSTC_CR_PROCRST);
    qtest_qmp_eventwait(qts, "RESET");
    value = qtest_readl(qts, SAM9X7_RSTC_BASE + RSTC_SR);
    g_assert_cmphex(value & RSTC_SR_RSTTYP_MASK, ==,
                    RSTC_SR_RSTTYP(RSTC_TYPE_SOFTWARE));
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0);
    ebi_enable_nand(qts);
    for (i = 1; i < sizeof(feature); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, 0);
    }
    nand_get_features(qts, 0x01, feature);
    g_assert_cmpmem(feature, sizeof(feature), timing_mode_4,
                    sizeof(timing_mode_4));

    qtest_system_reset(qts);
    ebi_enable_nand(qts);
    for (i = 0; i < G_N_ELEMENTS(feature_cases); i++) {
        nand_get_features(qts, feature_cases[i].address, feature);
        g_assert_cmpmem(feature, sizeof(feature), timing_mode_0,
                        sizeof(timing_mode_0));
    }
    nand_get_features(qts, 0xa0, feature);
    g_assert_cmpmem(feature, sizeof(feature), timing_mode_0,
                    sizeof(timing_mode_0));

    qtest_quit(qts);
}

static void test_nand_parameter_status_poll_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *middle = qtest_init(SAM9X75_MACHINE " -incoming defer");
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint8_t parameter_page[256];
    unsigned int i;

    ebi_enable_nand(from);
    nand_command(from, NAND_CMD_READ_PARAM);
    nand_address(from, 0x00);
    nand_command(from, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(from, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_migrate(from, middle);

    nand_command(middle, NAND_CMD_READ0);
    for (i = 0; i < 37; i++) {
        g_assert_cmphex(qtest_readb(middle, NAND_DATA), ==,
                        nand_parameter_page[i]);
    }

    nand_migrate(middle, to);

    memcpy(parameter_page, nand_parameter_page, i);
    for (; i < sizeof(parameter_page); i++) {
        parameter_page[i] = qtest_readb(to, NAND_DATA);
    }
    g_assert_cmpmem(parameter_page, sizeof(parameter_page),
                    nand_parameter_page, sizeof(nand_parameter_page));

    qtest_quit(to);
    qtest_quit(middle);
    qtest_quit(from);
}

static void test_nand_page_status_poll_migration(void)
{
    static const uint8_t first_payload[] = {
        0x5a, 0xa5, 0x00, 0xff, 0x36, 0xc9,
    };
    static const uint8_t first_oob[] = { 0x69, 0x96, 0x3c };
    static const uint8_t last_payload[] = { 0xde, 0xad, 0xbe, 0xef };
    const uint32_t last_page = NAND_NUM_PAGES - 1;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *middle = qtest_init(SAM9X75_MACHINE " -incoming defer");
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    unsigned int i;

    ebi_enable_nand(from);
    nand_program(from, 0, 0, first_payload, sizeof(first_payload));
    nand_program(from, 0, NAND_TEST_OOB_COLUMN,
                 first_oob, sizeof(first_oob));
    nand_program(from, last_page, 5, last_payload, sizeof(last_payload));

    nand_start_read(from, 0, 0);
    nand_command(from, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(from, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_migrate(from, middle);

    nand_command(middle, NAND_CMD_READ0);
    for (i = 0; i < 2; i++) {
        g_assert_cmphex(qtest_readb(middle, NAND_DATA), ==,
                        first_payload[i]);
    }

    nand_migrate(middle, to);

    for (; i < sizeof(first_payload); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, first_payload[i]);
    }
    nand_start_read(to, 0, NAND_TEST_OOB_COLUMN);
    for (i = 0; i < sizeof(first_oob); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, first_oob[i]);
    }
    nand_start_read(to, last_page, 5);
    for (i = 0; i < sizeof(last_payload); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, last_payload[i]);
    }

    qtest_quit(to);
    qtest_quit(middle);
    qtest_quit(from);
}

static void test_nand_random_data_input_migration(void)
{
    static const uint8_t payload[] = { 0x5a, 0xa5, 0x36, 0xc9 };
    static const uint8_t oob[] = { 0x69, 0x96, 0x3c, 0xc3 };
    const uint32_t page = 17;
    const uint32_t column = 32;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    unsigned int i;

    ebi_enable_nand(from);
    nand_command(from, NAND_CMD_PROGRAM_START);
    nand_page_address(from, column, page);
    for (i = 0; i < sizeof(payload); i++) {
        qtest_writeb(from, NAND_DATA, payload[i]);
    }

    nand_command(from, NAND_CMD_RANDOM_INPUT);
    nand_address(from, NAND_TEST_OOB_COLUMN & 0xff);
    nand_address(from, NAND_TEST_OOB_COLUMN >> 8);
    qtest_writeb(from, NAND_DATA, oob[0]);

    nand_migrate(from, to);

    for (i = 1; i < sizeof(oob); i++) {
        qtest_writeb(to, NAND_DATA, oob[i]);
    }
    nand_command(to, NAND_CMD_PAGE_PROGRAM);
    nand_command(to, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, NAND_STATUS_IDLE);

    nand_start_read(to, page, column);
    for (i = 0; i < sizeof(payload); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, payload[i]);
    }
    nand_start_read(to, page, NAND_TEST_OOB_COLUMN);
    for (i = 0; i < sizeof(oob); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, oob[i]);
    }

    qtest_quit(to);
    qtest_quit(from);
}

static void test_nand_program_old_source_migration(void)
{
    static const uint8_t payload[] = { 0xde, 0xad, 0xbe, 0xef };
    const char *source_env = g_getenv("QTEST_QEMU_BINARY_OLD") ?
                             "QTEST_QEMU_BINARY_OLD" :
                             "QTEST_QEMU_BINARY";
    const uint32_t page = 23;
    const uint32_t column = 5;
    QTestState *from = qtest_init_ext(source_env, SAM9X75_MACHINE,
                                      NULL, true);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    unsigned int i;

    ebi_enable_nand(from);
    nand_command(from, NAND_CMD_PROGRAM_START);
    nand_page_address(from, column, page);
    qtest_writeb(from, NAND_DATA, payload[0]);
    qtest_writeb(from, NAND_DATA, payload[1]);

    nand_migrate(from, to);

    qtest_writeb(to, NAND_DATA, payload[2]);
    qtest_writeb(to, NAND_DATA, payload[3]);
    nand_command(to, NAND_CMD_PAGE_PROGRAM);
    nand_command(to, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, NAND_STATUS_IDLE);

    nand_start_read(to, page, column);
    for (i = 0; i < sizeof(payload); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, payload[i]);
    }

    qtest_quit(to);
    qtest_quit(from);
}

static void test_nand_off_device_program_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    ebi_enable_nand(from);
    nand_command(from, NAND_CMD_PROGRAM_START);
    nand_page_address(from, 0, 0x00ffffff);
    qtest_writeb(from, NAND_DATA, 0x5a);

    nand_migrate(from, to);

    nand_command(to, NAND_CMD_PAGE_PROGRAM);
    nand_command(to, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(to, NAND_DATA), ==,
                    NAND_STATUS_IDLE | NAND_STATUS_FAIL);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_nand_features_status_poll_migration(void)
{
    static const uint8_t timing_mode_3[4] = { 3, 0, 0, 0 };
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *middle = qtest_init(SAM9X75_MACHINE " -incoming defer");
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    unsigned int i;

    ebi_enable_nand(from);
    nand_set_features(from, 0x01, timing_mode_3, sizeof(timing_mode_3));
    nand_command(from, NAND_CMD_GET_FEATURES);
    nand_address(from, 0x01);
    nand_command(from, NAND_CMD_STATUS);
    g_assert_cmphex(qtest_readb(from, NAND_DATA), ==, NAND_STATUS_IDLE);
    nand_migrate(from, middle);

    nand_command(middle, NAND_CMD_READ0);
    g_assert_cmphex(qtest_readb(middle, NAND_DATA), ==, timing_mode_3[0]);

    nand_migrate(middle, to);

    for (i = 1; i < sizeof(timing_mode_3); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, timing_mode_3[i]);
    }

    qtest_quit(to);
    qtest_quit(middle);
    qtest_quit(from);
}

static void test_nand_set_features_migration(void)
{
    static const uint8_t timing_mode_4[4] = { 4, 0, 0, 0 };
    const char *source_env = g_getenv("QTEST_QEMU_BINARY_OLD") ?
                             "QTEST_QEMU_BINARY_OLD" :
                             "QTEST_QEMU_BINARY";
    QTestState *from = qtest_init_ext(source_env, SAM9X75_MACHINE,
                                      NULL, true);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint8_t feature[4];
    unsigned int i;

    ebi_enable_nand(from);
    nand_set_features(from, 0x01, timing_mode_4, 2);
    nand_migrate(from, to);
    for (i = 2; i < sizeof(timing_mode_4); i++) {
        qtest_writeb(to, NAND_DATA, timing_mode_4[i]);
    }
    nand_get_features(to, 0x01, feature);
    g_assert_cmpmem(feature, sizeof(feature), timing_mode_4,
                    sizeof(timing_mode_4));

    qtest_quit(to);
    qtest_quit(from);
}

static void test_nand_empty_media_migration(void)
{
    static const uint8_t payload[] = { 0x12, 0x34, 0x56, 0x78 };
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    unsigned int i;

    /* Incoming state replaces, rather than merges with, destination media. */
    ebi_enable_nand(from);
    ebi_enable_nand(to);
    nand_program(to, 9, 0, payload, sizeof(payload));
    nand_migrate(from, to);
    nand_start_read(to, 9, 0);
    for (i = 0; i < sizeof(payload); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, 0xff);
    }

    qtest_quit(to);
    qtest_quit(from);
}

static void nand_test_backend_migration(bool raw)
{
    static const uint8_t payload[] = { 0xa6, 0x59, 0x3c, 0xc3 };
    static const uint8_t oob[] = { 0x96, 0x69, 0x5a };
    const uint32_t page = 11;
    const uint64_t image_size = raw ? NAND_RAW_SIZE : NAND_DATA_SIZE;
    const size_t erased_size = raw ? NAND_PAGE_TOTAL_SIZE : NAND_PAGE_SIZE;
    g_autofree uint8_t *erased = g_malloc(erased_size);
    g_autofree char *image_path = NULL;
    g_autofree char *args = NULL;
    GError *error = NULL;
    QTestState *from;
    QTestState *to;
    off_t offset;
    ssize_t ret;
    unsigned int i;
    int fd;

    fd = g_file_open_tmp("sam9x75-nand-XXXXXX", &image_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(ftruncate(fd, image_size), ==, 0);
    memset(erased, 0xff, erased_size);
    offset = (off_t)page * erased_size;
    ret = pwrite(fd, erased, erased_size, offset);
    g_assert_cmpint(ret, ==, erased_size);
    close(fd);

    args = g_strdup_printf(
        SAM9X75_MACHINE
        " -drive file=%s,file.locking=off,if=mtd,index=0,format=raw",
        image_path);
    from = qtest_init(args);
    to = qtest_initf("%s -incoming defer", args);

    ebi_enable_nand(from);
    nand_program(from, page, 0, payload, sizeof(payload));
    nand_program(from, page, NAND_TEST_OOB_COLUMN, oob, sizeof(oob));
    nand_migrate(from, to);

    nand_start_read(to, page, 0);
    for (i = 0; i < sizeof(payload); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, payload[i]);
    }
    nand_start_read(to, page, NAND_TEST_OOB_COLUMN);
    for (i = 0; i < sizeof(oob); i++) {
        g_assert_cmphex(qtest_readb(to, NAND_DATA), ==, oob[i]);
    }

    qtest_quit(to);
    qtest_quit(from);
    unlink(image_path);
}

static void test_nand_data_backend_migration(void)
{
    nand_test_backend_migration(false);
}

static void test_nand_raw_backend_migration(void)
{
    nand_test_backend_migration(true);
}

static void qspi_configure_read(QTestState *qts, uint8_t opcode,
                                uint32_t ifr);

static void test_board_memory_cs_jumpers(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE
                                 ",nand-cs=off,qspi-cs=off");

    g_assert_false(qtest_qom_get_bool(qts, "/machine", "nand-cs"));
    g_assert_false(qtest_qom_get_bool(qts, "/machine", "qspi-cs"));

    ebi_enable_nand(qts);
    nand_command(qts, NAND_CMD_READ_ID);
    nand_address(qts, 0x00);
    g_assert_cmphex(qtest_readl(qts, NAND_DATA), ==, UINT32_MAX);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_MR, QSPI_MR_SMM);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_QSPIEN);
    qspi_configure_read(qts, 0x9f, QSPI_IFR_INSTEN | QSPI_IFR_DATAEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_MEM_BASE), ==, 0);

    qtest_quit(qts);
}

static void test_smc_and_pmecc_registers(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_SETUP2), ==,
                    0x01010101);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_PULSE2), ==,
                    0x01010101);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_CYCLE2), ==,
                    0x00030003);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_MODE2), ==,
                    0x10001000);

    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_SETUP2, UINT32_MAX);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_PULSE2, UINT32_MAX);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_CYCLE2, UINT32_MAX);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_MODE2, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_SETUP2), ==,
                    0x3f3f3f3f);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_PULSE2), ==,
                    0x7f7f7f7f);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_CYCLE2), ==,
                    0x01ff01ff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_MODE2), ==,
                    0x311f3133);

    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_WPMR,
                 SMC_WPMR_KEY | BIT(0));
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_PULSE2, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_PULSE2), ==,
                    0x7f7f7f7f);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR), ==,
                    (SMC_PULSE2 << 8) | SMC_WPSR_WPVS);

    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_CFG, UINT32_MAX);
    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_SAREA, UINT32_MAX);
    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_SADDR, 0x1a5);
    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_EADDR, 0x1e7);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_CFG), ==,
                    0x00111317);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_SAREA), ==,
                    0x1ff);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_SADDR), ==,
                    0x1a5);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_EADDR), ==,
                    0x1e7);
    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_RESERVED_CLK, 2);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE +
                                PMECC_RESERVED_CLK), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_ECC0), ==,
                    UINT32_MAX);

    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_CTRL,
                 PMECC_CTRL_ENABLE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_SR), ==,
                    PMECC_SR_ENABLE);
    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_IER, BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_IMR), ==,
                    BIT(0));
    qtest_writel(qts, SAM9X7_PMECC_BASE + PMECC_CTRL,
                 PMECC_CTRL_DISABLE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE + PMECC_SR), ==, 0);

    qtest_writel(qts, SAM9X7_PMERRLOC_BASE + PMERRLOC_CFG, UINT32_MAX);
    qtest_writel(qts, SAM9X7_PMERRLOC_BASE + PMERRLOC_PRIM, 0x2345);
    qtest_writel(qts, SAM9X7_PMERRLOC_BASE + PMERRLOC_IER, PMERRLOC_DONE);
    qtest_writel(qts, SAM9X7_PMERRLOC_BASE + PMERRLOC_LEN, 0x3456);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_PMERRLOC_BASE + PMERRLOC_CFG), ==,
                    0x001f0001);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_PMERRLOC_BASE + PMERRLOC_PRIM), ==,
                    0x2345);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_PMERRLOC_BASE + PMERRLOC_LEN), ==,
                    0x3456);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_PMERRLOC_BASE + PMERRLOC_IMR), ==,
                    PMERRLOC_DONE);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_PMERRLOC_BASE + PMERRLOC_ISR), ==,
                    PMERRLOC_DONE);
    qtest_writel(qts, SAM9X7_PMERRLOC_BASE + PMERRLOC_DIS, 0);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_PMERRLOC_BASE + PMERRLOC_ISR), ==, 0);

    qtest_quit(qts);
}

static void test_pmecc_banked_windows(void)
{
    static const unsigned int bad_offsets[] = {
        0x6c, 0x22c, 0x270, 0x430, 0x5fc,
    };
    g_autofree char *log_path = NULL;
    g_autofree char *log = NULL;
    QTestState *qts;
    GError *error = NULL;
    unsigned int bank;
    unsigned int offset;
    int fd;

    fd = g_file_open_tmp("sam9x75-pmecc-log-XXXXXX", &log_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    close(fd);

    qts = qtest_initf(SAM9X75_MACHINE " -d guest_errors -D %s", log_path);
    for (bank = 0; bank < PMECC_BANK_COUNT; bank++) {
        uint64_t ecc = SAM9X7_PMECC_BASE + PMECC_ECC_FIRST +
                       bank * PMECC_BANK_STRIDE;
        uint64_t rem = SAM9X7_PMECC_BASE + PMECC_REM_FIRST +
                       bank * PMECC_BANK_STRIDE;

        for (offset = 0; offset < PMECC_ECC_REG_COUNT * sizeof(uint32_t);
             offset++) {
            g_assert_cmphex(qtest_readb(qts, ecc + offset), ==, UINT8_MAX);
        }
        for (offset = 0; offset < PMECC_REM_REG_COUNT; offset++) {
            g_assert_cmphex(qtest_readl(qts, rem + offset * sizeof(uint32_t)),
                            ==, 0);
        }
    }
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(log_path, &log, NULL, &error));
    g_assert_no_error(error);
    g_assert_null(strstr(log, "at91-pmecc"));
    g_clear_pointer(&log, g_free);

    qts = qtest_initf(SAM9X75_MACHINE " -d guest_errors -D %s", log_path);
    for (offset = 0; offset < G_N_ELEMENTS(bad_offsets); offset++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_PMECC_BASE +
                                    bad_offsets[offset]), ==, 0);
    }
    qtest_quit(qts);

    g_assert_true(g_file_get_contents(log_path, &log, NULL, &error));
    g_assert_no_error(error);
    for (offset = 0; offset < G_N_ELEMENTS(bad_offsets); offset++) {
        g_autofree char *message =
            g_strdup_printf("bad offset 0x%x", bad_offsets[offset]);

        g_assert_nonnull(strstr(log, message));
    }
    g_assert_cmpint(g_unlink(log_path), ==, 0);
}

static void test_smc_safety_and_shared_irq(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    aic_configure(qts, 49, AIC_SMR_LEVEL_HIGH | 3, 0x49eb149e);

    /* The A1 erratum makes OCMS the exception to SMC write protection. */
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_WPMR,
                 SMC_WPMR_KEY | SMC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_OCMS, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_OCMS), ==,
                    SMC_OCMS_MASK);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR), ==, 0);

    /* SRIER remains protected and reports the attempted write. */
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_SRIER, SMC_SRIER_SRIE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_SRIER), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR), ==,
                    (SMC_SRIER << 8) | SMC_WPSR_WPVS);

    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_WPMR, SMC_WPMR_KEY);

    /* Enabling SRIE immediately exposes an already-pending safety error. */
    qtest_readl(qts, SAM9X7_SMC_BASE + SMC_KEY1);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_SRIER, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_SRIER), ==,
                    SMC_SRIER_SRIE);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR), ==,
                    (SMC_KEY1 << 8) | SMC_WPSR_SWE);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));
    value = qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR);
    g_assert_cmphex(value & (SMC_WPSR_WPVS | SMC_WPSR_SEQE |
                            SMC_WPSR_SWE | SMC_WPSR_TYPE_MASK), ==, 0);

    /* A software error must not overwrite an unread WP violation source. */
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_WPMR,
                 SMC_WPMR_KEY | SMC_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_PULSE2, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_PULSE2), ==,
                    0x01010101);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_WPMR, SMC_WPMR_KEY);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_WPSR, 0);
    value = (SMC_PULSE2 << 8) | SMC_WPSR_WPVS | SMC_WPSR_SWE |
            SMC_WPSR_TYPE_WRITE_RO;
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR), ==,
                    value);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));

    /* Scrambling keys are unprotected and a second write is not an error. */
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_KEY1, 0x11223344);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_KEY1, 0xa5a5a5a5);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_KEY2, 0x55667788);
    qtest_writel(qts, SAM9X7_SMC_BASE + SMC_KEY2, 0x5a5a5a5a);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR), ==, 0);

    /* MPDDRC and SMC are independent producers on the shared PID49 line. */
    qtest_writel(qts, SAM9X7_MPDDRC_BASE + MPDDRC_IER, 1);
    qtest_readl(qts, SAM9X7_SMC_BASE + SMC_KEY1);
    qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_IER);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SMC_BASE + SMC_WPSR), ==,
                    (SMC_KEY1 << 8) | SMC_WPSR_SWE);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_WPSR), ==,
                    (MPDDRC_IER << 8) | MPDDRC_WPSR_SWE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MPDDRC_BASE + MPDDRC_ISR), ==, 1);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));

    qtest_quit(qts);
}

static void test_smc_shared_irq_migration_and_reset(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    aic_configure(from, 49, AIC_SMR_LEVEL_HIGH | 3, 0x49eb149e);
    qtest_writel(from, SAM9X7_SMC_BASE + SMC_SETUP2, 0x02030405);
    qtest_writel(from, SAM9X7_SMC_BASE + SMC_OCMS, SMC_OCMS_MASK);
    qtest_writel(from, SAM9X7_SMC_BASE + SMC_KEY1, 0x11223344);
    qtest_writel(from, SAM9X7_SMC_BASE + SMC_KEY2, 0x55667788);
    qtest_writel(from, SAM9X7_SMC_BASE + SMC_SRIER, SMC_SRIER_SRIE);
    qtest_writel(from, SAM9X7_SMC_BASE + SMC_WPMR,
                 SMC_WPMR_KEY | SMC_WPMR_WPEN);
    qtest_writel(from, SAM9X7_SMC_BASE + SMC_PULSE2, 0);

    qtest_writel(from, SAM9X7_MPDDRC_BASE + MPDDRC_IER, 1);
    qtest_readl(from, SAM9X7_MPDDRC_BASE + MPDDRC_IER);
    g_assert_true(qtest_readl(from, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_SETUP2), ==,
                    0x02030405);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_OCMS), ==,
                    SMC_OCMS_MASK);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_SRIER), ==,
                    SMC_SRIER_SRIE);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_WPMR), ==,
                    SMC_WPMR_WPEN);
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));

    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_WPSR), ==,
                    (SMC_PULSE2 << 8) | SMC_WPSR_WPVS);
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));
    g_assert_cmphex(qtest_readl(to, SAM9X7_MPDDRC_BASE + MPDDRC_WPSR), ==,
                    (MPDDRC_IER << 8) | MPDDRC_WPSR_SWE);
    g_assert_cmphex(qtest_readl(to, SAM9X7_MPDDRC_BASE + MPDDRC_ISR), ==, 1);
    g_assert_false(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));

    qtest_system_reset(to);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_SETUP2), ==,
                    0x01010101);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_PULSE2), ==,
                    0x01010101);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_CYCLE2), ==,
                    0x00030003);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_MODE2), ==,
                    0x10001000);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_OCMS), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_SRIER), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_WPMR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SMC_BASE + SMC_WPSR), ==, 0);
    g_assert_false(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR1) & BIT(17));

    qtest_quit(to);
    qtest_quit(from);
}

static void test_sys_irq_migration_and_reset(void)
{
    const char *path = "/machine/soc/sys-irq";
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    qtest_irq_intercept_out(from, path);
    qtest_irq_intercept_out(to, path);
    qtest_set_irq_in(from, path, NULL, 7, 1);
    g_assert_true(qtest_get_irq(from, 0));

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_true(qtest_get_irq(to, 0));
    qtest_system_reset(to);
    g_assert_false(qtest_get_irq(to, 0));

    qtest_quit(to);
    qtest_quit(from);
}

static void qspi_finish_transfer(QTestState *qts)
{
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_LASTXFER);
}

static void qspi_command(QTestState *qts, uint8_t opcode, uint32_t address,
                         uint32_t ifr)
{
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IAR, address);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WICR, opcode);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IFR, ifr);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_STTFR);
}

static void qspi_configure_read(QTestState *qts, uint8_t opcode,
                                uint32_t ifr)
{
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_RICR, opcode);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IFR, ifr);
}

static void test_qspi_status_irq_and_edges(void)
{
    const char *path = "/machine/soc/qspi";
    const uint32_t addressed_data = QSPI_IFR_INSTEN | QSPI_IFR_ADDREN |
                                    QSPI_IFR_DATAEN | QSPI_IFR_ADDRL_3 |
                                    QSPI_IFR_TFRTYP_MEM;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    qtest_irq_intercept_out_named(qts, path, "sysbus-irq");
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_SR), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_SCR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_SCR), ==,
                    0x00ff0003);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IER,
                 QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY | BIT(9));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_IMR), ==,
                    QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_QSPIEN);
    g_assert_true(qtest_get_irq(qts, 0));
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_cmphex(value & (QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY |
                            QSPI_ISR_RFRSHD), ==,
                    QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY | QSPI_ISR_RFRSHD);
    g_assert_false(value & BIT(9));
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_cmphex(value & (QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY), ==,
                    QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY);
    g_assert_false(value & QSPI_ISR_RFRSHD);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_QSPIDIS);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_SR), ==, 0);
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_false(value & (QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY));
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_QSPIEN);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IDR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_IMR), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IER, QSPI_ISR_OVRES);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_TDR, 0x9f);
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_true(value & QSPI_ISR_RDRF);
    g_assert_true(value & QSPI_ISR_CSFA);
    g_assert_false(value & (QSPI_ISR_OVRES | QSPI_ISR_CSRA));
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_TDR, 0);
    g_assert_true(qtest_get_irq(qts, 0));
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_true(value & (QSPI_ISR_RDRF | QSPI_ISR_OVRES));
    g_assert_false(qtest_get_irq(qts, 0));
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_true(value & QSPI_ISR_RDRF);
    g_assert_false(value & QSPI_ISR_OVRES);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_TDR, 0);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_true(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR) &
                  QSPI_ISR_OVRES);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_RDR);
    qspi_finish_transfer(qts);
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IDR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IER,
                 QSPI_ISR_CSFA | QSPI_ISR_CSRA);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_TDR, 0x9f);
    g_assert_true(qtest_get_irq(qts, 0));
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_true(value & QSPI_ISR_CSFA);
    g_assert_false(value & QSPI_ISR_CSRA);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_RDR);
    qspi_finish_transfer(qts);
    g_assert_true(qtest_get_irq(qts, 0));
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_cmphex(value & (QSPI_ISR_CSR | QSPI_ISR_CSFA |
                            QSPI_ISR_CSRA), ==,
                    QSPI_ISR_CSR | QSPI_ISR_CSRA);
    g_assert_false(qtest_get_irq(qts, 0));

    /* A rise autoclears an unread fall event. */
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_TDR, 0x9f);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_RDR);
    qspi_finish_transfer(qts);
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_cmphex(value & (QSPI_ISR_CSR | QSPI_ISR_CSFA |
                            QSPI_ISR_CSRA), ==,
                    QSPI_ISR_CSR | QSPI_ISR_CSRA);
    g_assert_false(qtest_get_irq(qts, 0));

    /* A fall autoclears an unread rise-autoclear event. */
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_TDR, 0x9f);
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_RDR);
    qspi_finish_transfer(qts);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_TDR, 0x9f);
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_cmphex(value & (QSPI_ISR_CSR | QSPI_ISR_CSFA |
                            QSPI_ISR_CSRA), ==,
                    QSPI_ISR_CSR | QSPI_ISR_CSFA);
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_RDR);
    qspi_finish_transfer(qts);
    qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IDR, UINT32_MAX);
    qspi_command(qts, 0x06, 0, QSPI_IFR_INSTEN);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_MR, QSPI_MR_SMM);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WICR, 0x02);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IFR, addressed_data);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WRACNT, 0);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IER, QSPI_ISR_LWRA);
    qtest_writeb(qts, SAM9X7_QSPI_MEM_BASE + 0x40000, 0xa5);
    qspi_finish_transfer(qts);
    g_assert_false(qtest_get_irq(qts, 0));
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_false(value & QSPI_ISR_LWRA);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WRACNT, 2);
    qtest_writeb(qts, SAM9X7_QSPI_MEM_BASE + 0x40010, 0x5a);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR) &
                   QSPI_ISR_LWRA);
    qtest_writeb(qts, SAM9X7_QSPI_MEM_BASE + 0x40011, 0xc3);
    g_assert_true(qtest_get_irq(qts, 0));
    g_assert_true(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR) &
                  QSPI_ISR_LWRA);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR) &
                   QSPI_ISR_LWRA);
    qtest_writeb(qts, SAM9X7_QSPI_MEM_BASE + 0x40012, 0x3c);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR) &
                   QSPI_ISR_LWRA);
    qspi_finish_transfer(qts);

    qtest_quit(qts);
}

static void test_qspi_status_migration_and_reset(void)
{
    const char *path = "/machine/soc/qspi";
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint32_t value;

    qtest_irq_intercept_out_named(from, path, "sysbus-irq");
    qtest_irq_intercept_out_named(to, path, "sysbus-irq");
    qtest_writel(from, SAM9X7_QSPI_BASE + QSPI_IER, QSPI_ISR_OVRES);
    qtest_writel(from, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_QSPIEN);
    qtest_readl(from, SAM9X7_QSPI_BASE + QSPI_ISR);
    qtest_writel(from, SAM9X7_QSPI_BASE + QSPI_TDR, 0x9f);
    g_assert_false(qtest_get_irq(from, 0));
    qtest_writel(from, SAM9X7_QSPI_BASE + QSPI_TDR, 0);
    g_assert_true(qtest_get_irq(from, 0));
    g_assert_cmphex(qtest_readl(from, SAM9X7_QSPI_BASE + QSPI_SR), ==,
                    QSPI_SR_QSPIENS);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_true(qtest_get_irq(to, 0));
    g_assert_cmphex(qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_IMR), ==,
                    QSPI_ISR_OVRES);
    g_assert_cmphex(qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_SR), ==,
                    QSPI_SR_QSPIENS);
    value = qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_true(value & QSPI_ISR_RDRF);
    g_assert_true(value & QSPI_ISR_OVRES);
    g_assert_false(qtest_get_irq(to, 0));
    value = qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_true(value & QSPI_ISR_RDRF);
    g_assert_false(value & QSPI_ISR_OVRES);
    qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_RDR);

    qtest_system_reset(to);
    g_assert_false(qtest_get_irq(to, 0));
    g_assert_cmphex(qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_IMR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_SR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_SCR), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_QSPI_BASE + QSPI_WRACNT), ==, 0);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_qspi_flash_read_program_and_erase(void)
{
    const uint32_t addressed_data = QSPI_IFR_INSTEN | QSPI_IFR_ADDREN |
                                    QSPI_IFR_DATAEN | QSPI_IFR_ADDRL_3 |
                                    QSPI_IFR_TFRTYP_MEM;
    const uint32_t flash_offset = 0x1234;
    const uint32_t last_flash_word = 0x7ffffc;
    const uint32_t payload = 0xa55ac33c;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE " -nic user,mac=02:00:00:09:75:01");
    uint32_t value;

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_MR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_SCR, UINT32_MAX);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IFR, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_MR), ==,
                    0xffff2fbd);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_SCR), ==,
                    0x00ff0003);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_IFR), ==,
                    0x7fffdfff);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_MR, QSPI_MR_SMM);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_CR, QSPI_CR_QSPIEN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_SR), ==,
                    QSPI_SR_QSPIENS | QSPI_SR_CSS | QSPI_SR_HIDLE);
    value = qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_ISR);
    g_assert_cmphex(value & (QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY |
                            QSPI_ISR_RFRSHD), ==,
                    QSPI_ISR_TDRE | QSPI_ISR_TXEMPTY | QSPI_ISR_RFRSHD);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IER, QSPI_ISR_TDRE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_IMR), ==,
                    QSPI_ISR_TDRE);

    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WPMR,
                 QSPI_WPMR_KEY | QSPI_WPMR_WPEN);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_MR, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_MR), ==,
                    QSPI_MR_SMM);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_BASE + QSPI_WPSR), ==,
                    (QSPI_MR << 8) | BIT(0));
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WPMR, QSPI_WPMR_KEY);

    qspi_configure_read(qts, 0x9f, QSPI_IFR_INSTEN | QSPI_IFR_DATAEN);
    g_assert_cmphex(qtest_readb(qts, SAM9X7_QSPI_MEM_BASE), ==, 0xbf);
    g_assert_cmphex(qtest_readb(qts, SAM9X7_QSPI_MEM_BASE + 1), ==, 0x26);
    g_assert_cmphex(qtest_readb(qts, SAM9X7_QSPI_MEM_BASE + 2), ==, 0x43);
    qspi_finish_transfer(qts);

    qspi_configure_read(qts, 0x5a,
                        addressed_data | QSPI_IFR_NBDUM(8));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_MEM_BASE), ==,
                    0x50444653);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_MEM_BASE + 0x18), ==,
                    0x1c0200bf);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_MEM_BASE + 0x200), ==,
                    0xff4326bf);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_MEM_BASE + 0x260), ==,
                    0x00000230);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_QSPI_MEM_BASE + 0x264), ==,
                    0x40017509);
    qspi_finish_transfer(qts);

    qspi_command(qts, 0x06, 0, QSPI_IFR_INSTEN);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WICR, 0x02);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WRACNT, sizeof(payload));
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IFR, addressed_data);
    qtest_writel(qts, SAM9X7_QSPI_MEM_BASE + flash_offset, payload);
    qspi_finish_transfer(qts);

    qspi_configure_read(qts, 0x03, addressed_data);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_QSPI_MEM_BASE + flash_offset), ==,
                    payload);
    qspi_finish_transfer(qts);

    qspi_command(qts, 0x06, 0, QSPI_IFR_INSTEN);
    qspi_command(qts, 0x20, flash_offset + 0x800,
                 QSPI_IFR_INSTEN | QSPI_IFR_ADDREN | QSPI_IFR_ADDRL_3);
    qspi_configure_read(qts, 0x03, addressed_data);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_QSPI_MEM_BASE + flash_offset), ==,
                    UINT32_MAX);
    qspi_finish_transfer(qts);

    qspi_command(qts, 0x06, 0, QSPI_IFR_INSTEN);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WICR, 0x02);
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_WRACNT, sizeof(payload));
    qtest_writel(qts, SAM9X7_QSPI_BASE + QSPI_IFR, addressed_data);
    qtest_writel(qts, SAM9X7_QSPI_MEM_BASE + last_flash_word, payload);
    qspi_finish_transfer(qts);

    qspi_configure_read(qts, 0x03, addressed_data);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_QSPI_MEM_BASE + last_flash_word), ==,
                    payload);
    qspi_finish_transfer(qts);

    qspi_command(qts, 0x06, 0, QSPI_IFR_INSTEN);
    qspi_command(qts, 0x20, 0x7fffff,
                 QSPI_IFR_INSTEN | QSPI_IFR_ADDREN | QSPI_IFR_ADDRL_3);
    qspi_configure_read(qts, 0x03, addressed_data);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_QSPI_MEM_BASE + last_flash_word), ==,
                    UINT32_MAX);
    qspi_finish_transfer(qts);

    qtest_quit(qts);
}

static void test_uhphs_registers_reset_and_companions(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE), ==, 0x10);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_CONTROL), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_INT_ENABLE), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_INTERVAL), ==, 0x2edf);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_LS_THRESHOLD), ==, 0x628);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_RH_DESC_A), ==, 0x0a001203);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_RH_DESC_B), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_RH_STATUS), ==, 0);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                    UHPFS_HC_RH_PORT(i)), ==,
                        UHPFS_PORT_PPS);

        /* NPS makes both global and per-port power controls read-only. */
        qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE +
                     UHPFS_HC_RH_PORT(i), UHPFS_PORT_LSDA);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                    UHPFS_HC_RH_PORT(i)), ==,
                        UHPFS_PORT_PPS);
    }
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPS);
    for (i = 0; i < 3; i++) {
        g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                  UHPFS_HC_RH_PORT(i)) & UHPFS_PORT_PPS);
    }

    /* Ganged power: only the hub-status global controls take effect. */
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_DESC_A,
                 0x0a001000);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_DESC_A) &
                   (UHPFS_RHA_NPS | UHPFS_RHA_PSM));
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(0),
                 UHPFS_PORT_LSDA);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_PPS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPS);
    for (i = 0; i < 3; i++) {
        g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                   UHPFS_HC_RH_PORT(i)) & UHPFS_PORT_PPS);
        qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE +
                     UHPFS_HC_RH_PORT(i), UHPFS_PORT_PPS);
        g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                   UHPFS_HC_RH_PORT(i)) & UHPFS_PORT_PPS);
    }
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPSC);

    /* Mixed power: PPCM selects per-port rather than global control. */
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_DESC_A,
                 0x0a001000 | UHPFS_RHA_PSM);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_DESC_B,
                 UHPFS_RHB_PPCM(0));
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPS);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_PPS);
    for (i = 1; i < 3; i++) {
        g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                   UHPFS_HC_RH_PORT(i)) & UHPFS_PORT_PPS);
    }
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(0),
                 UHPFS_PORT_LSDA);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_PPS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(0),
                 UHPFS_PORT_PPS);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_PPS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(1),
                 UHPFS_PORT_PPS);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_PPS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPSC);
    for (i = 0; i < 3; i++) {
        g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                  UHPFS_HC_RH_PORT(i)) & UHPFS_PORT_PPS);
    }

    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_DESC_A,
                 0x55001d00);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_RH_DESC_A), ==, 0x55001903);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_DESC_B,
                 UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_RH_DESC_B), ==, 0x000e000e);

    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA,
                 SAM9X7_SRAM0_BASE + 0x1000);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + 0x64, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE + 0x64), ==,
                    UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_HCCA), ==,
                    SAM9X7_SRAM0_BASE + 0x1000);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE), ==,
                    0x01000010);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x04), ==,
                    0x00001303);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x08), ==,
                    0x00000026);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_USBCMD), ==, 0x00080b00);

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_FLS_1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_USBCMD) & UHPHS_USBCMD_FLS_MASK,
                    ==, UHPHS_USBCMD_FLS_1);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_FLS_2);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_USBCMD) & UHPHS_USBCMD_FLS_MASK,
                    ==, UHPHS_USBCMD_FLS_2);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_USBSTS), ==, BIT(12));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_USBINTR), ==, 0);
    for (i = 0; i < 3; i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                    UHPHS_PORTSC(i)), ==,
                        UHPHS_PORTSC_POWNER | UHPHS_PORTSC_PPOWER);
    }

    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA,
                 0x20001000);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_HCR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_HCCA), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_RH_DESC_A), ==, 0x55001903);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_RH_DESC_B), ==, 0x000e000e);

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_ASYNCLISTADDR,
                 0x20002000);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_HCRESET);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_ASYNCLISTADDR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_USBCMD), ==, 0x00080b00);

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_CONFIGFLAG, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_CONFIGFLAG), ==, 1);
    for (i = 0; i < 3; i++) {
        g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                   UHPHS_PORTSC(i)) & UHPHS_PORTSC_POWNER);
    }

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PORTSC(0),
                 UHPHS_PORTSC_POWNER);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_PORTSC(0)) & UHPHS_PORTSC_POWNER);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_CONFIGFLAG, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_PORTSC(0)) & UHPHS_PORTSC_POWNER);

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_CONFIGFLAG, 0);
    for (i = 0; i < 3; i++) {
        g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                  UHPHS_PORTSC(i)) & UHPHS_PORTSC_POWNER);
    }
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PORTSC(0), 0);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_PORTSC(0)) & UHPHS_PORTSC_POWNER);

    qtest_quit(qts);
}

static void uhphs_ohci_submit_nonzero_setup(QTestState *qts,
                                             uint8_t address)
{
    static const uint8_t setup[8];
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t ed = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t td = SAM9X7_SRAM0_BASE + 0x120;
    const uint32_t tail = SAM9X7_SRAM0_BASE + 0x130;
    const uint32_t buffer = SAM9X7_SRAM0_BASE + 0x140;

    qtest_memset(qts, hcca, 0, 0x100);
    qtest_memwrite(qts, buffer, setup, sizeof(setup));
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);
    qtest_writel(qts, ed, (64U << 16) | (1U << 7) | address);
    qtest_writel(qts, ed + 4, tail);
    qtest_writel(qts, ed + 8, td);
    qtest_writel(qts, ed + 12, 0);
    qtest_writel(qts, td, 0xf2000000);
    qtest_writel(qts, td + 4, buffer);
    qtest_writel(qts, td + 8, tail);
    qtest_writel(qts, td + 12, buffer + sizeof(setup) - 1);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL_HEAD, ed);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_CLE);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_CLF);
    qtest_clock_step(qts, 1000000);
}

static void test_uhphs_hotplug_shared_irq(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    aic_configure(qts, 22, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0022);
    qtest_qmp_device_add(qts, "usb-kbd", "sam9x75-usb-kbd",
                         "{'bus': 'usb-bus.0', 'port': '2'}");

    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_INT_ENABLE,
                 UHPFS_INT_MIE | UHPFS_INT_RHSC);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_HCR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_INT_STATUS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_INT_ENABLE), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));

    qtest_qmp_device_del(qts, "sam9x75-usb-kbd");
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_PORTSC(1)) & UHPHS_PORTSC_POWNER);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_INT_ENABLE,
                 UHPFS_INT_MIE | UHPFS_INT_RHSC);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_INT_STATUS,
                 UHPFS_INT_RHSC);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));

    qtest_qmp_device_add(qts, "usb-kbd", "sam9x75-usb-kbd-replug",
                         "{'bus': 'usb-bus.0', 'port': '2'}");
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));

    /* A resolved ordinary USB device cannot receive SETUP on EP1. */
    pmc_configure_usb_host(qts);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(1),
                 UHPFS_PORT_PRS);
    qtest_clock_step(qts, 2 * 1000000LL);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_PES);
    uhphs_ohci_submit_nonzero_setup(qts, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_INT_STATUS) & UHPFS_INT_UE);

    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_HCR);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_INT_STATUS) & UHPFS_INT_UE);

    /* A nonzero SETUP with no target retains the same fatal behavior. */
    uhphs_ohci_submit_nonzero_setup(qts, 0x7f);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_INT_STATUS) & UHPFS_INT_UE);
    qtest_qmp_device_del(qts, "sam9x75-usb-kbd-replug");
    qtest_quit(qts);
}

static void test_uhphs_power_connection_lifecycle(void)
{
    const uint64_t port_addr = SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(1);
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t port;

    qtest_qmp_device_add(qts, "usb-kbd", "sam9x75-power-usb-kbd",
                         "{'bus': 'usb-bus.0', 'port': '2'}");
    g_assert_true(qtest_readl(qts, port_addr) & UHPFS_PORT_CCS);

    /* Select ganged switching, then remove power from every root port. */
    qtest_writel(qts, port_addr, UHPFS_PORT_CSC);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_DESC_A,
                 0x0a001000);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPS);
    port = qtest_readl(qts, port_addr);
    g_assert_false(port & UHPFS_PORT_PPS);
    g_assert_false(port & UHPFS_PORT_CCS);
    g_assert_true(port & UHPFS_PORT_CSC);

    /* Power restoration must redetect a device that stayed plugged in. */
    qtest_writel(qts, port_addr, UHPFS_PORT_CSC);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPSC);
    port = qtest_readl(qts, port_addr);
    g_assert_true(port & UHPFS_PORT_PPS);
    g_assert_true(port & UHPFS_PORT_CCS);
    g_assert_true(port & UHPFS_PORT_CSC);

    /* A device plugged in while power is off stays hidden until power-on. */
    qtest_writel(qts, port_addr, UHPFS_PORT_CSC);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPS);
    qtest_writel(qts, port_addr, UHPFS_PORT_CSC);
    qtest_qmp_device_del(qts, "sam9x75-power-usb-kbd");
    qtest_qmp_device_add(qts, "usb-kbd", "sam9x75-unpowered-usb-kbd",
                         "{'bus': 'usb-bus.0', 'port': '2'}");
    port = qtest_readl(qts, port_addr);
    g_assert_false(port & UHPFS_PORT_PPS);
    g_assert_false(port & UHPFS_PORT_CCS);
    g_assert_false(port & UHPFS_PORT_CSC);

    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_STATUS,
                 UHPFS_RHS_LPSC);
    port = qtest_readl(qts, port_addr);
    g_assert_true(port & UHPFS_PORT_PPS);
    g_assert_true(port & UHPFS_PORT_CCS);
    g_assert_true(port & UHPFS_PORT_CSC);

    qtest_qmp_device_del(qts, "sam9x75-unpowered-usb-kbd");
    qtest_quit(qts);
}

static void test_uhphs_pmc_clock_gating(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE + 0x2000;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t ehci_frame;
    uint32_t ohci_frame;
    uint32_t ohci_remaining;

    qtest_memset(qts, SAM9X7_SRAM0_BASE, 1, 0x1000);
    qtest_memset(qts, hcca, 0, 0x100);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PERIODICLISTBASE,
                 SAM9X7_SRAM0_BASE);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_RUN | UHPHS_USBCMD_PSE);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL);

    /* The register interfaces remain visible, but neither schedule runs. */
    qtest_clock_step(qts, 20 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_NUMBER), ==, 0);

    pmc_configure_usb_host(qts);
    qtest_clock_step(qts, 20 * 1000000LL);
    ehci_frame = qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c);
    ohci_frame = qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                             UHPFS_HC_FM_NUMBER);
    g_assert_cmphex(ehci_frame, >, 0);
    g_assert_cmphex(ohci_frame, >, 0);

    /* SCER.UHP supplies OHCI's 48/12 MHz clocks, not EHCI's UTMI path. */
    qtest_clock_step(qts, 250000);
    ehci_frame = qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c);
    ohci_frame = qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                             UHPFS_HC_FM_NUMBER);
    ohci_remaining = qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                 UHPFS_HC_FM_REMAINING);
    g_assert_cmphex(ohci_remaining & 0x3fff, >, 0);
    g_assert_cmphex(ohci_remaining & 0x3fff, <, 0x2edf);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_SCDR, PMC_SC_UHP);
    ohci_remaining = qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                 UHPFS_HC_FM_REMAINING);
    qtest_clock_step(qts, 20 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_NUMBER), ==, ohci_frame);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_REMAINING), ==,
                    ohci_remaining);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c), !=,
                    ehci_frame);

    /* The PID-22 peripheral clock is common to both host interfaces. */
    pmc_write_pcr(qts, 22, 0);
    ehci_frame = qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c);
    qtest_clock_step(qts, 20 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c), ==,
                    ehci_frame);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_NUMBER), ==, ohci_frame);

    pmc_write_pcr(qts, 22, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_SCER, PMC_SC_UHP);
    qtest_clock_step(qts, 20 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c), !=,
                    ehci_frame);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_NUMBER), !=, ohci_frame);

    qtest_quit(qts);
}

static void test_uhphs_ehci_frame_list_size(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t frindex;

    pmc_configure_usb_host(qts);

    /* Every little-endian entry has the terminate bit set. */
    qtest_memset(qts, SAM9X7_SRAM0_BASE, 1, 0x1000);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PERIODICLISTBASE,
                 SAM9X7_SRAM0_BASE);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c, 0x7f8);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBINTR,
                 UHPHS_USBSTS_FLR);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_FLS_2 | UHPHS_USBCMD_RUN |
                 UHPHS_USBCMD_PSE);
    qtest_clock_step(qts, 1);
    qtest_clock_step_next(qts);

    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_USBSTS) & UHPHS_USBSTS_FLR);
    frindex = qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + 0x1c);
    g_assert_cmphex(frindex, >=, 0x800);
    g_assert_cmphex(frindex, <, 0x1000);

    qtest_quit(qts);
}

static void test_uhphs_ehci_dma_error(void)
{
    const uint32_t invalid_dma = 0x10000000;
    const uint32_t qh_addr = SAM9X7_SRAM0_BASE;
    const uint32_t qtd_addr = SAM9X7_SRAM0_BASE + 0x100;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;

    pmc_configure_usb_host(qts);
    aic_configure(qts, 22, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0022);
    qtest_qmp_device_add(qts, "usb-kbd", "sam9x75-dma-usb-kbd",
                         "{'bus': 'usb-bus.0', 'port': '2'}");
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_INT_ENABLE,
                 UHPFS_INT_MIE | UHPFS_INT_RHSC);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PERIODICLISTBASE,
                 invalid_dma);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBINTR,
                 UHPHS_USBSTS_HSE);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_RUN | UHPHS_USBCMD_PSE);
    qtest_clock_step(qts, 1);
    qtest_clock_step_next(qts);

    status = qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBSTS);
    g_assert_true(status & UHPHS_USBSTS_HSE);
    g_assert_true(status & UHPHS_USBSTS_HCHLT);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_USBCMD) & UHPHS_USBCMD_RUN);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_INSNREG06) &
                  UHPHS_INSNREG06_AHB_ERR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG07), ==, invalid_dma + 4);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBSTS,
                 UHPHS_USBSTS_HSE);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_INT_STATUS,
                 UHPFS_INT_RHSC);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_INSNREG06, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_INSNREG06) &
                   UHPHS_INSNREG06_AHB_ERR);

    qtest_system_reset(qts);
    pmc_configure_usb_host(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG06), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG07), ==, 0);

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_CONFIGFLAG, 1);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PORTSC(1),
                 UHPHS_PORTSC_PRESET);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PORTSC(1), 0);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_PORTSC(1)) & UHPHS_PORTSC_PED);

    qtest_memset(qts, qh_addr, 0, 0x200);
    qtest_writel(qts, qh_addr, qh_addr | BIT(1));
    qtest_writel(qts, qh_addr + 4,
                 (64U << 16) | BIT(15) | BIT(14) | (2U << 12));
    qtest_writel(qts, qh_addr + 8, BIT(30));
    qtest_writel(qts, qh_addr + 16, qtd_addr);
    qtest_writel(qts, qh_addr + 20, BIT(0));
    qtest_writel(qts, qtd_addr, BIT(0));
    qtest_writel(qts, qtd_addr + 4, BIT(0));
    qtest_writel(qts, qtd_addr + 8,
                 (8U << 16) | (3U << 10) | (2U << 8) | BIT(7));
    qtest_writel(qts, qtd_addr + 12, invalid_dma);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_ASYNCLISTADDR,
                 qh_addr);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBINTR,
                 UHPHS_USBSTS_HSE);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_RUN | UHPHS_USBCMD_ASE);
    qtest_clock_step(qts, 1);
    qtest_clock_step_next(qts);

    status = qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBSTS);
    g_assert_true(status & UHPHS_USBSTS_HSE);
    g_assert_true(status & UHPHS_USBSTS_HCHLT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG07), ==, invalid_dma);
    qtest_qmp_device_del(qts, "sam9x75-dma-usb-kbd");

    qtest_system_reset(qts);
    pmc_configure_usb_host(qts);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA,
                 invalid_dma);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL);
    qtest_clock_step_next(qts);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_INT_STATUS) & UHPFS_INT_UE);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_INSNREG06) &
                  UHPHS_INSNREG06_AHB_ERR);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG07), ==, invalid_dma);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG06), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG07), ==, 0);

    qtest_quit(qts);
}

static void test_uhphs_gated_async_migration(void)
{
    const uint32_t invalid_dma = 0x10000000;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint32_t status;

    /* A gated controller must not traverse this invalid async schedule. */
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_ASYNCLISTADDR,
                 invalid_dma);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBINTR,
                 UHPHS_USBSTS_HSE);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_RUN | UHPHS_USBCMD_ASE);
    qtest_clock_step(from, 20 * 1000000LL);
    g_assert_false(qtest_readl(from, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_USBSTS) & UHPHS_USBSTS_HSE);
    g_assert_false(qtest_readl(from, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_INSNREG06) &
                   UHPHS_INSNREG06_AHB_ERR);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_false(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_USBSTS) & UHPHS_USBSTS_HSE);
    g_assert_false(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_INSNREG06) &
                   UHPHS_INSNREG06_AHB_ERR);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_ASYNCLISTADDR), ==, invalid_dma);

    /* Restoring the clocks releases the deferred schedule and its fault. */
    pmc_configure_usb_host(to);
    qtest_clock_step(to, 1);
    qtest_clock_step_next(to);
    status = qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBSTS);
    g_assert_true(status & UHPHS_USBSTS_HSE);
    g_assert_true(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_INSNREG06) &
                  UHPHS_INSNREG06_AHB_ERR);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG07), ==, invalid_dma);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_uhphs_frame_phase_migration(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE + 0x2000;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    uint32_t ehci_frame;
    uint32_t ohci_frame;
    uint32_t ohci_remaining;
    int64_t from_clock;

    qtest_clock_step(from, 5 * 1000000LL);
    pmc_configure_usb_host(from);
    qtest_memset(from, SAM9X7_SRAM0_BASE, 1, 0x1000);
    qtest_memset(from, hcca, 0, 0x100);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PERIODICLISTBASE,
                 SAM9X7_SRAM0_BASE);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_RUN | UHPHS_USBCMD_PSE);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL);

    qtest_clock_step(from, 1);
    qtest_clock_step(from, 2 * 1000000LL);
    ehci_frame = qtest_readl(from, SAM9X7_UHPHS_EHCI_BASE + 0x1c);
    g_assert_cmphex(ehci_frame, >=, 8);
    from_clock = qtest_clock_step(from, 7 * 125000LL);
    ohci_frame = qtest_readl(from, SAM9X7_UHPHS_OHCI_BASE +
                             UHPFS_HC_FM_NUMBER);
    ohci_remaining = qtest_readl(from, SAM9X7_UHPHS_OHCI_BASE +
                                 UHPFS_HC_FM_REMAINING);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    /* qtest virtual clocks are independent; align the destination clock. */
    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);

    /* Both host controllers preserve the elapsed part of their frame. */
    g_assert_cmpuint(get_clock_period(to, "/machine/soc/uhphs-ohci/pclk"),
                     !=, 0);
    g_assert_cmpuint(get_clock_period(to, "/machine/soc/uhphs-ohci/uhpck"),
                     !=, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE + 0x1c), ==,
                    ehci_frame);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_NUMBER), ==, ohci_frame);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_REMAINING), ==,
                    ohci_remaining);

    qtest_clock_step(to, 100000);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE + 0x1c), ==,
                    ehci_frame);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_NUMBER), ==, ohci_frame);
    qtest_clock_step(to, 50000);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_FM_NUMBER), ==,
                    (ohci_frame + 1) & 0xffff);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE + 0x1c), ==,
                    ehci_frame);
    qtest_clock_step(to, 2050000);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE + 0x1c), ==,
                    (ehci_frame + 24) & 0x3ff8);

    qtest_quit(to);
    qtest_quit(from);
}

/*
 * Drive a BOT command entirely through OHCI descriptors.  Keeping the
 * descriptor layout fixed lets the write test below inspect its exact state
 * while the backend request is still in flight.
 */
static void ohci_bot_command(QTestState *qts, const uint8_t cbw[31],
                             uint32_t data_len, bool data_in)
{
    const uint32_t out_ed = SAM9X7_SRAM0_BASE + 0x300;
    const uint32_t in_ed = SAM9X7_SRAM0_BASE + 0x310;
    const uint32_t cbw_td = SAM9X7_SRAM0_BASE + 0x320;
    const uint32_t data_td = SAM9X7_SRAM0_BASE + 0x330;
    const uint32_t out_tail = SAM9X7_SRAM0_BASE + 0x340;
    const uint32_t csw_td = SAM9X7_SRAM0_BASE + 0x350;
    const uint32_t in_tail = SAM9X7_SRAM0_BASE + 0x360;
    const uint32_t cbw_buf = SAM9X7_SRAM0_BASE + 0x400;
    const uint32_t data_buf = SAM9X7_SRAM0_BASE + 0x500;
    const uint32_t csw_buf = SAM9X7_SRAM0_BASE + 0x700;

    qtest_memwrite(qts, cbw_buf, cbw, 31);
    qtest_memset(qts, csw_buf, 0, 13);

    qtest_writel(qts, out_ed, 0x02000901);
    qtest_writel(qts, out_ed + 4, out_tail);
    qtest_writel(qts, out_ed + 8, cbw_td);
    qtest_writel(qts, out_ed + 12, in_ed);
    qtest_writel(qts, cbw_td, 0xf2000000);
    qtest_writel(qts, cbw_td + 4, cbw_buf);
    qtest_writel(qts, cbw_td + 8, data_len && !data_in ? data_td : out_tail);
    qtest_writel(qts, cbw_td + 12, cbw_buf + 30);

    qtest_writel(qts, in_ed, 0x02001081);
    qtest_writel(qts, in_ed + 4, in_tail);
    qtest_writel(qts, in_ed + 8, data_len && data_in ? data_td : csw_td);
    qtest_writel(qts, in_ed + 12, 0);

    if (data_len) {
        qtest_writel(qts, data_td, 0xf3000000);
        qtest_writel(qts, data_td + 4, data_buf);
        qtest_writel(qts, data_td + 8, data_in ? csw_td : out_tail);
        qtest_writel(qts, data_td + 12, data_buf + data_len - 1);
    }
    qtest_writel(qts, csw_td, 0xf2000000);
    qtest_writel(qts, csw_td + 4, csw_buf);
    qtest_writel(qts, csw_td + 8, in_tail);
    qtest_writel(qts, csw_td + 12, csw_buf + 12);

    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_BULK_HEAD,
                 out_ed);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_BLE);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_BLF);
}

static void ohci_wait_for_ed(QTestState *qts, uint32_t ed, uint32_t tail)
{
    unsigned int i;

    for (i = 0; i < 100; i++) {
        if ((qtest_readl(qts, ed + 8) & ~3U) == tail) {
            return;
        }
        qtest_clock_step(qts, 1000000);
    }
    g_assert_cmphex(qtest_readl(qts, ed + 8) & ~3U, ==, tail);
}

static void test_uhphs_ohci_active_async_migration(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t control_ed = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t setup_td = SAM9X7_SRAM0_BASE + 0x120;
    const uint32_t status_td = SAM9X7_SRAM0_BASE + 0x130;
    const uint32_t control_tail = SAM9X7_SRAM0_BASE + 0x140;
    const uint32_t config_setup_td = SAM9X7_SRAM0_BASE + 0x160;
    const uint32_t config_status_td = SAM9X7_SRAM0_BASE + 0x170;
    const uint32_t config_tail = SAM9X7_SRAM0_BASE + 0x180;
    const uint32_t setup_buf = SAM9X7_SRAM0_BASE + 0x200;
    const uint32_t out_ed = SAM9X7_SRAM0_BASE + 0x300;
    const uint32_t in_ed = SAM9X7_SRAM0_BASE + 0x310;
    const uint32_t cbw_td = SAM9X7_SRAM0_BASE + 0x320;
    const uint32_t data_td = SAM9X7_SRAM0_BASE + 0x330;
    const uint32_t out_tail = SAM9X7_SRAM0_BASE + 0x340;
    const uint32_t csw_td = SAM9X7_SRAM0_BASE + 0x350;
    const uint32_t in_tail = SAM9X7_SRAM0_BASE + 0x360;
    const uint32_t cbw_buf = SAM9X7_SRAM0_BASE + 0x400;
    const uint32_t data_buf = SAM9X7_SRAM0_BASE + 0x500;
    const uint32_t csw_buf = SAM9X7_SRAM0_BASE + 0x700;
    const uint8_t set_addr[] = { 0x00, 0x05, 0x01, 0x00,
                                 0x00, 0x00, 0x00, 0x00 };
    const uint8_t set_config[] = { 0x00, 0x09, 0x01, 0x00,
                                   0x00, 0x00, 0x00, 0x00 };
    const uint8_t tur_cbw[] = {
        0x55, 0x53, 0x42, 0x43, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const uint8_t sense_cbw[] = {
        0x55, 0x53, 0x42, 0x43, 0x02, 0x00, 0x00, 0x00,
        0x12, 0x00, 0x00, 0x00, 0x80, 0x00, 0x06, 0x03,
        0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const uint8_t write_cbw[] = {
        0x55, 0x53, 0x42, 0x43, 0x78, 0x56, 0x34, 0x12,
        0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x2a,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    QDict *status;
    QTestState *from = qtest_init(
        SAM9X75_MACHINE
        " -blockdev driver=null-co,node-name=usb-null,size=1048576,"
        "read-zeroes=on"
        " -blockdev driver=blkdebug,node-name=usb-disk,image=usb-null,"
        "inject-error.0.event=none,inject-error.0.iotype=write,"
        "inject-error.0.errno=5,inject-error.0.once=on,"
        "inject-error.0.immediately=off"
        " -device usb-storage,id=sam9x75-ohci-msd,drive=usb-disk,"
        "bus=usb-bus.0,port=2,werror=stop");
    QTestState *to = qtest_init(
        SAM9X75_MACHINE
        " -blockdev driver=null-co,node-name=usb-null,size=1048576,"
        "read-zeroes=on"
        " -blockdev driver=blkdebug,node-name=usb-disk,image=usb-null"
        " -device usb-storage,id=sam9x75-ohci-msd,drive=usb-disk,"
        "bus=usb-bus.0,port=2,werror=stop -incoming defer");

    pmc_configure_usb_host(from);
    qtest_memset(from, hcca, 0, 0x800);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(1),
                 UHPFS_PORT_PRS);
    g_assert_true(qtest_readl(from, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    g_assert_true(qtest_readl(from, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_PES);

    qtest_memwrite(from, setup_buf, set_addr, sizeof(set_addr));
    qtest_writel(from, control_ed, 0x00400000);
    qtest_writel(from, control_ed + 4, control_tail);
    qtest_writel(from, control_ed + 8, setup_td);
    qtest_writel(from, control_ed + 12, 0);
    qtest_writel(from, setup_td, 0xf2000000);
    qtest_writel(from, setup_td + 4, setup_buf);
    qtest_writel(from, setup_td + 8, status_td);
    qtest_writel(from, setup_td + 12, setup_buf + 7);
    qtest_writel(from, status_td, 0xf3100000);
    qtest_writel(from, status_td + 4, 0);
    qtest_writel(from, status_td + 8, control_tail);
    qtest_writel(from, status_td + 12, 0);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL_HEAD,
                 control_ed);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_CLE);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_CLF);
    ohci_wait_for_ed(from, control_ed, control_tail);

    qtest_memwrite(from, setup_buf, set_config, sizeof(set_config));
    qtest_writel(from, control_ed, 0x00400001);
    qtest_writel(from, control_ed + 4, config_tail);
    qtest_writel(from, control_ed + 8, config_setup_td);
    qtest_writel(from, control_ed + 12, 0);
    qtest_writel(from, config_setup_td, 0xf2000000);
    qtest_writel(from, config_setup_td + 4, setup_buf);
    qtest_writel(from, config_setup_td + 8, config_status_td);
    qtest_writel(from, config_setup_td + 12, setup_buf + 7);
    qtest_writel(from, config_status_td, 0xf3100000);
    qtest_writel(from, config_status_td + 4, 0);
    qtest_writel(from, config_status_td + 8, config_tail);
    qtest_writel(from, config_status_td + 12, 0);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL_HEAD,
                 control_ed);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_CLF);
    ohci_wait_for_ed(from, control_ed, config_tail);

    ohci_bot_command(from, tur_cbw, 0, false);
    ohci_wait_for_ed(from, out_ed, out_tail);
    ohci_wait_for_ed(from, in_ed, in_tail);
    g_assert_cmphex(qtest_readb(from, csw_buf + 12), ==, 1);
    qtest_clock_step(from, 1000000);

    ohci_bot_command(from, sense_cbw, 18, true);
    ohci_wait_for_ed(from, out_ed, out_tail);
    ohci_wait_for_ed(from, in_ed, in_tail);
    g_assert_cmphex(qtest_readb(from, csw_buf + 12), ==, 0);
    qtest_clock_step(from, 1000000);

    qtest_memwrite(from, cbw_buf, write_cbw, sizeof(write_cbw));
    qtest_memset(from, data_buf, 0x5a, 512);
    qtest_memset(from, csw_buf, 0xcc, 13);
    qtest_writel(from, out_ed, 0x02000901);
    qtest_writel(from, out_ed + 4, out_tail);
    qtest_writel(from, out_ed + 8, cbw_td);
    qtest_writel(from, out_ed + 12, in_ed);
    qtest_writel(from, cbw_td, 0xf2000000);
    qtest_writel(from, cbw_td + 4, cbw_buf);
    qtest_writel(from, cbw_td + 8, data_td);
    qtest_writel(from, cbw_td + 12, cbw_buf + 30);
    qtest_writel(from, data_td, 0xf3000000);
    qtest_writel(from, data_td + 4, data_buf);
    qtest_writel(from, data_td + 8, out_tail);
    qtest_writel(from, data_td + 12, data_buf + 511);
    qtest_writel(from, in_ed, 0x02001081);
    qtest_writel(from, in_ed + 4, in_tail);
    qtest_writel(from, in_ed + 8, csw_td);
    qtest_writel(from, in_ed + 12, 0);
    qtest_writel(from, csw_td, 0xf2000000);
    qtest_writel(from, csw_td + 4, csw_buf);
    qtest_writel(from, csw_td + 8, in_tail);
    qtest_writel(from, csw_td + 12, csw_buf + 12);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_BULK_HEAD,
                 out_ed);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_BLE);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_BLF);
    qtest_clock_step(from, 1000000);
    qtest_qmp_eventwait(from, "STOP");
    status = qtest_qmp_assert_success_ref(from,
                                          "{ 'execute': 'query-status' }");
    g_assert_cmpstr(qdict_get_str(status, "status"), ==, "io-error");
    g_assert_false(qdict_get_bool(status, "running"));
    qobject_unref(status);
    g_assert_cmphex(qtest_readl(from, out_ed + 8) & ~3U, ==, out_tail);
    g_assert_cmphex(qtest_readl(from, in_ed + 8) & ~3U, ==, csw_td);
    g_assert_cmphex(qtest_readl(from, csw_td), ==, 0xf2000000);
    g_assert_cmphex(qtest_readl(from, csw_buf), ==, 0xcccccccc);
    g_assert_true(qtest_readl(from, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_COMMAND_STATUS) & UHPFS_COMMAND_BLF);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    qtest_qmp_assert_success(to, "{ 'execute': 'cont' }");
    qtest_qmp_eventwait(to, "RESUME");
    ohci_wait_for_ed(to, in_ed, in_tail);
    g_assert_cmphex(qtest_readl(to, out_ed + 8) & ~3U, ==, out_tail);
    g_assert_false(qtest_readl(to, out_ed + 8) & 1);
    g_assert_false(qtest_readl(to, in_ed + 8) & 1);
    g_assert_cmphex(qtest_readl(to, csw_td) >> 28, ==, 0);
    g_assert_cmphex(qtest_readl(to, csw_td + 4), ==, 0);
    g_assert_cmphex(qtest_readl(to, csw_buf), ==, 0x53425355);
    g_assert_cmphex(qtest_readl(to, csw_buf + 4), ==, 0x12345678);
    g_assert_cmphex(qtest_readl(to, csw_buf + 8), ==, 0);
    g_assert_cmphex(qtest_readb(to, csw_buf + 12), ==, 0);
    g_assert_false(qtest_readl(to, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_INT_STATUS) & UHPFS_INT_UE);
    status = qtest_qmp_assert_success_ref(to,
                                          "{ 'execute': 'query-status' }");
    g_assert_cmpstr(qdict_get_str(status, "status"), ==, "running");
    g_assert_true(qdict_get_bool(status, "running"));
    qobject_unref(status);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_uhphs_migration(void)
{
    const uint32_t invalid_dma = 0x10000000;
    const uint32_t hcca = SAM9X7_SRAM0_BASE + 0x1000;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    pmc_configure_usb_host(from);
    aic_configure(from, 22, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0022);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA,
                 hcca);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PERIODICLISTBASE,
                 invalid_dma);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_CONFIGFLAG, 1);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBINTR,
                 UHPHS_USBSTS_HSE);
    qtest_writel(from, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_RUN | UHPHS_USBCMD_PSE);
    qtest_clock_step(from, 1);
    qtest_clock_step_next(from);

    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_INT_ENABLE,
                 UHPFS_INT_MIE | UHPFS_INT_SF);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL);
    qtest_clock_step(from, 2 * 1000000LL);
    g_assert_true(qtest_readl(from, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_INT_STATUS) & UHPFS_INT_SF);
    g_assert_true(qtest_readl(from, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_OHCI_BASE +
                                UHPFS_HC_HCCA), ==, hcca);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_PERIODICLISTBASE), ==, invalid_dma);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_CONFIGFLAG), ==, 1);
    g_assert_false(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_PORTSC(0)) & UHPHS_PORTSC_POWNER);
    g_assert_true(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_INSNREG06) &
                  UHPHS_INSNREG06_AHB_ERR);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UHPHS_EHCI_BASE +
                                UHPHS_INSNREG07), ==, invalid_dma + 4);
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));
    qtest_writel(to, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_INT_STATUS,
                 UHPFS_INT_SF);
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));
    qtest_writel(to, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBSTS,
                 UHPHS_USBSTS_HSE);
    g_assert_false(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(22));

    qtest_quit(to);
    qtest_quit(from);
}

static void udphs_configure_endpoint(QTestState *qts, unsigned int ep,
                                     uint32_t config, uint32_t interrupts)
{
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCFG(ep), config);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTCFG(ep)), ==,
                    config | UDPHS_EPTCFG_MAPPED);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLENB(ep),
                 UDPHS_EPTCTL_ENABLE | interrupts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTCTL(ep)), ==,
                    UDPHS_EPTCTL_ENABLE | interrupts);
}

static void udphs_start_test_link(QTestState *qts, uint32_t hcca)
{
    pmc_configure_usb_host(qts);
    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_TST, 3);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_PULLD_DIS);
    qtest_clock_step(qts, 1000000);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(1),
                 UHPFS_PORT_PRS);
    qtest_clock_step(qts, 2 * 1000000LL);
    qtest_memset(qts, hcca, 0, 0x100);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);
}

static void test_udphs_registers_endpoints_clocks_and_irq(void)
{
    static const uint8_t endpoint_banks[] = { 1, 2, 2, 3, 3, 3, 3 };
    static const uint8_t full_packet[64] = { 0xa5 };
    static const uint8_t partial_packet[] = { 0x91, 0x75, 0xc1 };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t control;
    uint32_t status;
    unsigned int ep;

    qtest_irq_intercept_out_named(qts, "/machine/soc/udphs",
                                  "device-mode");
    aic_configure(qts, 23, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0017);
    pmc_write_pcr(qts, 4, PMC_PCR_EN);

    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/pclk"),
                     ==, 0);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/utmi"),
                     ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_FNUM), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_TST), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));
    g_assert_false(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(8));

    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    pmc_enable_upll(qts);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/pclk"),
                     !=, 0);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/utmi"),
                     ==, CLOCK_PERIOD_FROM_HZ(480000000));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL), ==,
                    UDPHS_CTRL_DETACH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN), ==,
                    UDPHS_INT_ENDRESET);
    for (ep = 0; ep < ARRAY_SIZE(endpoint_banks); ep++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_EPTSTA(ep)), ==,
                        UDPHS_EPTSTA_RESET);
    }

    /* The UTMI regulator and bandgap are independent hard clock gates. */
    pmc_set_upll_acr(qts, PMC_UPLL_ACR_CONFIG & ~PMC_PLL_ACR_UTMIBG);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/utmi"),
                     ==, 0);
    pmc_set_upll_acr(qts, PMC_UPLL_ACR_CONFIG & ~PMC_PLL_ACR_UTMIVR);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/utmi"),
                     ==, 0);
    pmc_set_upll_acr(qts, PMC_UPLL_ACR_CONFIG);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/utmi"),
                     ==, CLOCK_PERIOD_FROM_HZ(480000000));

    control = UDPHS_CTRL_DEV_ADDR(0x35) | UDPHS_CTRL_FADDR_EN |
              UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_DETACH |
              UDPHS_CTRL_PULLD_DIS;
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL, control);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL), ==,
                    control);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 control & ~UDPHS_CTRL_EN_UDPHS);
    g_assert_false(qtest_get_irq(qts, 0));

    /* Keep the controller enabled but detached while programming it. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_DETACH);
    g_assert_true(qtest_get_irq(qts, 0));

    for (ep = 0; ep < ARRAY_SIZE(endpoint_banks); ep++) {
        uint32_t config = UDPHS_EPTCFG_SIZE(3) |
                          UDPHS_EPTCFG_TYPE(ep ? 2 : 0) |
                          UDPHS_EPTCFG_BANKS(endpoint_banks[ep]);

        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_EPTCFG(ep)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_EPTCTL(ep)), ==,
                        ep == 0 ? UDPHS_EPTCTL_ENABLE : 0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_EPTSTA(ep)), ==,
                        UDPHS_EPTSTA_RESET);
        udphs_configure_endpoint(qts, ep, config, 0);
    }

    /* EP0 has one bank; an overcommitted configuration is not mapped. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCFG(0),
                 UDPHS_EPTCFG_SIZE(3) | UDPHS_EPTCFG_BANKS(2));
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                               UDPHS_EPTCFG(0)) & UDPHS_EPTCFG_MAPPED);
    udphs_configure_endpoint(qts, 0,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_BANKS(1),
                             UDPHS_EPTCTL_TXRDY_IE);

    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_EPT(0));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_EPT(0));
    /* TXRDY is active-low in the endpoint interrupt logic. */
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                  UDPHS_INT_EPT(0));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(0),
                 UDPHS_EPTSTA_TXRDY);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));

    /* Status follows the current CPU bank, not an older queued IN bank. */
    udphs_configure_endpoint(qts, 1,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID |
                             UDPHS_EPTCTL_BUSY_IE);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_EPT(1));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16),
                   partial_packet, sizeof(partial_packet));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & UDPHS_EPTSTA_BYTE_COUNT(0x7ff), ==,
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(partial_packet)));
    g_assert_false(status & UDPHS_EPTSTA_TXRDY);
    g_assert_false(status & UDPHS_EPTSTA_SHORT);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(1),
                 UDPHS_EPTSTA_TXRDY);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & UDPHS_EPTSTA_BUSY(3), ==,
                    UDPHS_EPTSTA_BUSY(1));
    g_assert_cmphex(status & UDPHS_EPTSTA_BYTE_COUNT(0x7ff), ==, 0);
    g_assert_false(status & UDPHS_EPTSTA_TXRDY);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16),
                   full_packet, sizeof(full_packet));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & UDPHS_EPTSTA_BUSY(3), ==,
                    UDPHS_EPTSTA_BUSY(2));
    g_assert_cmphex(status & UDPHS_EPTSTA_BYTE_COUNT(0x7ff), ==,
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(full_packet)));
    g_assert_true(status & UDPHS_EPTSTA_TXRDY);
    g_assert_false(status & UDPHS_EPTSTA_SHORT);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTRST, BIT(1));

    /* BYTE_COUNT advances per FIFO access, independent of its address. */
    udphs_configure_endpoint(qts, 5,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(3), 0);
    qtest_writeb(qts, SAM9X7_UDPHS_FIFO_BASE + (5U << 16) + 10, 0x51);
    qtest_writeb(qts, SAM9X7_UDPHS_FIFO_BASE + (5U << 16) + 10, 0x52);
    qtest_writeb(qts, SAM9X7_UDPHS_FIFO_BASE + (5U << 16) + 2, 0x53);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(5)) &
                    UDPHS_EPTSTA_BYTE_COUNT(0x7ff), ==,
                    UDPHS_EPTSTA_BYTE_COUNT(3));

    /* Isochronous PID-selection control bits are implemented too. */
    udphs_configure_endpoint(qts, 6,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(3),
                             UDPHS_EPTCTL_DATAX_RX |
                             UDPHS_EPTCTL_MDATA_RX);

    /* DMA channels 1--6 have distinct register files. */
    for (ep = 1; ep <= 6; ep++) {
        uint32_t descriptor = SAM9X7_SRAM0_BASE + ep * 0x20;
        uint32_t address = SAM9X7_SRAM0_BASE + 0x400 + ep * 0x20;
        uint32_t dma_control = UDPHS_DMA_END_TR_EN |
                               UDPHS_DMA_END_BF_EN |
                               UDPHS_DMA_END_TR_IE |
                               UDPHS_DMA_END_BF_IE |
                               UDPHS_DMA_DESC_LD_IE |
                               UDPHS_DMA_BUFF_LENGTH(0x20 + ep);

        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMANXTDSC(ep)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMAADDRESS(ep)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMACONTROL(ep)), ==, 0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMASTATUS(ep)), ==, 0);
        qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMANXTDSC(ep),
                     descriptor);
        qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(ep),
                     address);
        qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(ep),
                     dma_control);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMANXTDSC(ep)), ==, descriptor);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMAADDRESS(ep)), ==, address);
        /* Stop Now cannot update bits 31:2 of DMACONTROL. */
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMACONTROL(ep)), ==, 0);
    }

    /* LDNXT_DSC loads one aligned, three-word transfer descriptor. */
    {
        const uint32_t descriptor = SAM9X7_SRAM0_BASE + 0xc00;
        const uint32_t next = SAM9X7_SRAM0_BASE + 0xc40;
        const uint32_t address = SAM9X7_SRAM0_BASE + 0xd00;
        const uint32_t desc_control = UDPHS_DMA_END_BF_EN |
                                      UDPHS_DMA_END_BF_IE |
                                      UDPHS_DMA_DESC_LD_IE |
                                      UDPHS_DMA_BUFF_LENGTH(0x26);
        uint32_t dma_status;

        qtest_writel(qts, descriptor, next);
        qtest_writel(qts, descriptor + 4, address);
        qtest_writel(qts, descriptor + 8, desc_control);
        qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMANXTDSC(6),
                     descriptor);
        qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(6),
                     UDPHS_DMA_LDNXT_DSC | UDPHS_DMA_DESC_LD_IE);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMANXTDSC(6)), ==, next);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMAADDRESS(6)), ==, address);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMACONTROL(6)), ==,
                        desc_control & 0xff);
        dma_status = qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                 UDPHS_DMASTATUS(6));
        g_assert_cmphex(dma_status, ==,
                        UDPHS_DMA_BUFF_COUNT(0x26) |
                        UDPHS_DMA_DESC_LD_ST);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                    UDPHS_DMASTATUS(6)), ==,
                        UDPHS_DMA_BUFF_COUNT(0x26));
    }
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL), ==,
                    0);
    g_assert_false(qtest_get_irq(qts, 0));
    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    pmc_enable_upll(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL), ==,
                    UDPHS_CTRL_DETACH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN), ==,
                    UDPHS_INT_ENDRESET);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTCFG(6)), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(6)), ==,
                    UDPHS_EPTSTA_RESET);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTCTL(0)), ==,
                    UDPHS_EPTCTL_ENABLE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMANXTDSC(6)), ==, 0);
    g_assert_false(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

static void test_udphs_shared_port_a_mux(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    pmc_configure_usb_host(qts);
    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    qtest_qmp_device_add(qts, "usb-kbd", "sam9x75-port-a-kbd",
                         "{'bus': 'usb-bus.0', 'port': '1'}");
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_CCS);

    /* EN_UDPHS takes the shared UTMI away from host Port A immediately. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_DETACH);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL), ==,
                    UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_DETACH);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_CCS);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                               UHPHS_PORTSC(0)) & UHPHS_PORTSC_CONNECT);

    /* A host-controller reset must not reconnect the muxed-away port. */
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_HCRESET);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_CCS);

    /* Returning to host mode makes an attached device visible again. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_DETACH);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_CCS);

    /* System reset returns Port A to host mode without losing the device. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_DETACH);
    qtest_system_reset(qts);
    pmc_configure_usb_host(qts);
    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_CCS);

    /* Deleting a physical device while Port A is muxed must remain safe. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_DETACH);
    qtest_qmp_device_del(qts, "sam9x75-port-a-kbd");
    qtest_qmp_device_add(qts, "usb-kbd", "sam9x75-muxed-port-a-kbd",
                         "{'bus': 'usb-bus.0', 'port': '1'}");
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_CCS);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_DETACH);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(0)) & UHPFS_PORT_CCS);
    qtest_qmp_device_del(qts, "sam9x75-muxed-port-a-kbd");
    qtest_quit(qts);
}

static void udphs_ohci_start_control_token(QTestState *qts, uint32_t ed,
                                           uint32_t td, uint32_t tail,
                                           uint32_t buffer,
                                           unsigned int ep,
                                           uint32_t td_flags,
                                           size_t length)
{
    qtest_writel(qts, ed, (64U << 16) | (ep << 7));
    qtest_writel(qts, ed + 4, tail);
    qtest_writel(qts, ed + 8, td);
    qtest_writel(qts, ed + 12, 0);
    qtest_writel(qts, td, td_flags);
    qtest_writel(qts, td + 4, length ? buffer : 0);
    qtest_writel(qts, td + 8, tail);
    qtest_writel(qts, td + 12, length ? buffer + length - 1 : 0);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL_HEAD,
                 ed);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_CLE);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_CLF);
}

static void udphs_ohci_control_token(QTestState *qts, uint32_t ed,
                                     uint32_t td, uint32_t tail,
                                     uint32_t buffer, unsigned int ep,
                                     uint32_t td_flags, size_t length)
{
    udphs_ohci_start_control_token(qts, ed, td, tail, buffer, ep,
                                   td_flags, length);
    ohci_wait_for_ed(qts, ed, tail);
}

static void udphs_ohci_start_bulk_transfer(QTestState *qts, uint32_t ed,
                                           uint32_t td, uint32_t tail,
                                           uint32_t buffer,
                                           unsigned int ep, bool in,
                                           uint32_t td_flags,
                                           size_t length);

static void udphs_ohci_bulk_transfer_flags(QTestState *qts, uint32_t ed,
                                           uint32_t td, uint32_t tail,
                                           uint32_t buffer,
                                           unsigned int ep, bool in,
                                           uint32_t td_flags,
                                           size_t length);

static void udphs_ohci_bulk_transfer(QTestState *qts, uint32_t ed,
                                     uint32_t td, uint32_t tail,
                                     uint32_t buffer, unsigned int ep,
                                     bool in, size_t length);

static void test_udphs_control_setup_interlock(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t ed = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t td = SAM9X7_SRAM0_BASE + 0x120;
    const uint32_t tail = SAM9X7_SRAM0_BASE + 0x130;
    const uint32_t setup_buffer = SAM9X7_SRAM0_BASE + 0x180;
    const uint32_t host_buffer = SAM9X7_SRAM0_BASE + 0x200;
    const uint8_t first_setup[] = {
        0xc0, 0x75, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    };
    const uint8_t replacement_setup[] = {
        0x40, 0x76, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
    };
    const uint8_t out_data[] = { 0x91, 0x75, 0xc0, 0xde };
    const uint8_t untouched[] = { 0xa5, 0xa5, 0xa5, 0xa5 };
    uint8_t observed[sizeof(first_setup)];
    uint32_t status;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-control-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2");

    udphs_start_test_link(qts, hcca);
    udphs_configure_endpoint(qts, 1,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_BANKS(1), 0);

    qtest_memwrite(qts, setup_buffer, first_setup, sizeof(first_setup));
    udphs_ohci_control_token(qts, ed, td, tail, setup_buffer, 1,
                             0xf2000000, sizeof(first_setup));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RX_SETUP |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RX_SETUP |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(first_setup)));

    /* IN must NAK without consuming or replacing the pending SETUP. */
    qtest_memset(qts, host_buffer, 0xa5, sizeof(untouched));
    udphs_ohci_start_control_token(qts, ed, td, tail, host_buffer, 1,
                                   0xf3100000, sizeof(untouched));
    qtest_clock_step(qts, 3 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, ed + 8) & ~3U, ==, td);
    g_assert_cmphex(qtest_readl(qts, td) >> 28, ==, 0xf);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RX_SETUP |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RX_SETUP |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(first_setup)));
    g_assert_true(status & UDPHS_EPTSTA_NAK_IN);
    g_assert_false(status & (UDPHS_EPTSTA_TXCOMPLT |
                             UDPHS_EPTSTA_RXRDY));
    qtest_memread(qts, host_buffer, observed, sizeof(untouched));
    g_assert_cmpmem(observed, sizeof(untouched), untouched,
                    sizeof(untouched));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16),
                  observed, sizeof(first_setup));
    g_assert_cmpmem(observed, sizeof(first_setup), first_setup,
                    sizeof(first_setup));

    /* A new SETUP is accepted and atomically replaces the pending one. */
    qtest_memwrite(qts, setup_buffer, replacement_setup,
                   sizeof(replacement_setup));
    udphs_ohci_control_token(qts, ed, td, tail, setup_buffer, 1,
                             0xf2000000, sizeof(replacement_setup));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RX_SETUP |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RX_SETUP |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(replacement_setup)));
    g_assert_false(status & (UDPHS_EPTSTA_NAK_IN |
                             UDPHS_EPTSTA_NAK_OUT));

    /* OUT remains pending until firmware clears RX_SETUP, then retries. */
    qtest_memwrite(qts, host_buffer, out_data, sizeof(out_data));
    udphs_ohci_start_control_token(qts, ed, td, tail, host_buffer, 1,
                                   0xf3080000, sizeof(out_data));
    qtest_clock_step(qts, 3 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, ed + 8) & ~3U, ==, td);
    g_assert_cmphex(qtest_readl(qts, td) >> 28, ==, 0xf);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RX_SETUP |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RX_SETUP |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(replacement_setup)));
    g_assert_true(status & UDPHS_EPTSTA_NAK_OUT);
    g_assert_false(status & UDPHS_EPTSTA_RXRDY);
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16),
                  observed, sizeof(replacement_setup));
    g_assert_cmpmem(observed, sizeof(replacement_setup),
                    replacement_setup, sizeof(replacement_setup));

    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(1),
                 UDPHS_EPTSTA_RX_SETUP);
    ohci_wait_for_ed(qts, ed, tail);
    g_assert_cmphex(qtest_readl(qts, td) >> 28, ==, 0);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RX_SETUP |
                              UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(out_data)));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16),
                  observed, sizeof(out_data));
    g_assert_cmpmem(observed, sizeof(out_data), out_data,
                    sizeof(out_data));

    qtest_quit(qts);
}

static void test_udphs_iso_error_paths(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t ed = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t td = SAM9X7_SRAM0_BASE + 0x120;
    const uint32_t tail = SAM9X7_SRAM0_BASE + 0x130;
    const uint32_t host_buffer = SAM9X7_SRAM0_BASE + 0x200;
    const uint8_t first_out[] = { 0x31, 0x32, 0x33, 0x34 };
    const uint8_t second_out[] = { 0x41, 0x42, 0x43, 0x44 };
    const uint8_t dropped_out[] = { 0xd1, 0xd2, 0xd3, 0xd4 };
    const uint8_t untouched[] = { 0xa5, 0xa5, 0xa5, 0xa5 };
    uint8_t observed[sizeof(first_out)];
    uint32_t status;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-iso-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2");

    udphs_start_test_link(qts, hcca);

    /* EP1 maps the DPRAM allocation, but cannot run high-bandwidth ISO. */
    udphs_configure_endpoint(qts, 1,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(2) |
                             UDPHS_EPTCFG_NB_TRANS(2), 0);
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                              UDPHS_EPTCFG(1)) & UDPHS_EPTCFG_MAPPED);
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 1, true, 1);
    g_assert_cmphex(qtest_readl(qts, td) >> 28, ==, 4);

    /* A full ISO OUT queue consumes and drops instead of returning NAK. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(2) |
                             UDPHS_EPTCFG_NB_TRANS(1), 0);
    qtest_memwrite(qts, host_buffer, first_out, sizeof(first_out));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 3, false,
                             sizeof(first_out));
    qtest_memwrite(qts, host_buffer, second_out, sizeof(second_out));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 3, false,
                             sizeof(second_out));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BUSY(3) |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY | UDPHS_EPTSTA_BUSY(2) |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(first_out)));
    g_assert_false(status & UDPHS_EPTSTA_ERR_FL_ISO);

    qtest_memwrite(qts, host_buffer, dropped_out, sizeof(dropped_out));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 3, false,
                             sizeof(dropped_out));
    g_assert_cmphex(qtest_readl(qts, td) >> 28, ==, 0);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BUSY(3) |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY | UDPHS_EPTSTA_BUSY(2) |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(first_out)));
    g_assert_true(status & UDPHS_EPTSTA_ERR_FL_ISO);
    g_assert_false(status & UDPHS_EPTSTA_NAK_OUT);
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16),
                  observed, sizeof(first_out));
    g_assert_cmpmem(observed, sizeof(first_out), first_out,
                    sizeof(first_out));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(3),
                 UDPHS_EPTSTA_RXRDY);
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16),
                  observed, sizeof(second_out));
    g_assert_cmpmem(observed, sizeof(second_out), second_out,
                    sizeof(second_out));

    /* Ordinary full-speed ISO IN has no wire response and records error. */
    udphs_configure_endpoint(qts, 4,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(2) |
                             UDPHS_EPTCFG_NB_TRANS(1), 0);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(4),
                 UDPHS_EPTSTA_FORCESTALL);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                               UDPHS_EPTSTA(4)) &
                   UDPHS_EPTSTA_FORCESTALL);
    qtest_memset(qts, host_buffer, 0xa5, sizeof(observed));
    udphs_ohci_bulk_transfer_flags(qts, ed, td, tail, host_buffer, 4,
                                   true, 0xf2040000,
                                   sizeof(observed));
    g_assert_cmphex(qtest_readl(qts, td) >> 28, ==, 5);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(4));
    g_assert_true(status & UDPHS_EPTSTA_ERR_FL_ISO);
    g_assert_false(status & (UDPHS_EPTSTA_NAK_IN |
                             UDPHS_EPTSTA_TXCOMPLT |
                             UDPHS_EPTSTA_TXRDY));
    qtest_memread(qts, host_buffer, observed, sizeof(observed));
    g_assert_cmpmem(observed, sizeof(observed), untouched,
                    sizeof(untouched));

    qtest_quit(qts);
}

static void test_udphs_high_bandwidth_iso_zlp(void)
{
    const uint32_t qh = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t qtd = SAM9X7_SRAM0_BASE + 0x180;
    const uint32_t host_buffer = SAM9X7_SRAM0_BASE + 0x300;
    const uint8_t untouched[] = { 0xa5, 0xa5, 0xa5, 0xa5 };
    uint8_t observed[sizeof(untouched)];
    uint32_t token;
    unsigned int i;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-hb-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2");

    pmc_configure_usb_host(qts);
    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_TST, 2);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_PULLD_DIS);
    qtest_clock_step(qts, 1000000);

    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_CONFIGFLAG, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_PORTSC(1)) & UHPHS_PORTSC_CONNECT);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PORTSC(1),
                 UHPHS_PORTSC_PRESET);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_PORTSC(1), 0);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_EHCI_BASE +
                              UHPHS_PORTSC(1)) & UHPHS_PORTSC_PED);
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                  UDPHS_INT_SPEED);

    /* EP3 is the first endpoint that supports high-bandwidth ISO. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(2) |
                             UDPHS_EPTCFG_NB_TRANS(2), 0);

    /* An empty high-bandwidth ISO IN transaction defaults to a DATA0 ZLP. */
    qtest_memset(qts, qh, 0, 0x200);
    qtest_memset(qts, host_buffer, 0xa5, sizeof(untouched));
    qtest_writel(qts, qh, qh | BIT(1));
    qtest_writel(qts, qh + 4,
                 (64U << 16) | BIT(15) | BIT(14) |
                 (2U << 12) | (3U << 8));
    qtest_writel(qts, qh + 8, BIT(30));
    qtest_writel(qts, qh + 16, qtd);
    qtest_writel(qts, qh + 20, BIT(0));
    qtest_writel(qts, qtd, BIT(0));
    qtest_writel(qts, qtd + 4, BIT(0));
    qtest_writel(qts, qtd + 8,
                 (sizeof(untouched) << 16) | (3U << 10) |
                 (1U << 8) | BIT(7));
    qtest_writel(qts, qtd + 12, host_buffer);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_ASYNCLISTADDR, qh);
    qtest_writel(qts, SAM9X7_UHPHS_EHCI_BASE + UHPHS_USBCMD,
                 UHPHS_USBCMD_RUN | UHPHS_USBCMD_ASE);

    for (i = 0; i < 100; i++) {
        token = qtest_readl(qts, qtd + 8);
        if (!(token & BIT(7))) {
            break;
        }
        qtest_clock_step(qts, 1000000);
    }
    g_assert_cmpuint(i, <, 100);
    token = qtest_readl(qts, qtd + 8);
    g_assert_cmphex(token & 0xff, ==, 0);
    g_assert_cmphex((token >> 16) & 0x7fff, ==, sizeof(untouched));
    qtest_memread(qts, host_buffer, observed, sizeof(observed));
    g_assert_cmpmem(observed, sizeof(observed), untouched,
                    sizeof(untouched));
    token = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_true(token & UDPHS_EPTSTA_ERR_FL_ISO);
    g_assert_false(token & (UDPHS_EPTSTA_NAK_IN |
                            UDPHS_EPTSTA_TXCOMPLT |
                            UDPHS_EPTSTA_TXRDY));

    qtest_quit(qts);
}

static void test_udphs_raw_ep0_and_pio_data_tokens(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t setup_ed = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t setup_td = SAM9X7_SRAM0_BASE + 0x120;
    const uint32_t setup_tail = SAM9X7_SRAM0_BASE + 0x130;
    const uint32_t setup_buf = SAM9X7_SRAM0_BASE + 0x140;
    const uint32_t bulk_ed = SAM9X7_SRAM0_BASE + 0x180;
    const uint32_t bulk_td = SAM9X7_SRAM0_BASE + 0x1a0;
    const uint32_t bulk_tail = SAM9X7_SRAM0_BASE + 0x1b0;
    const uint32_t bulk_buf = SAM9X7_SRAM0_BASE + 0x200;
    const uint8_t setup[] = { 0x80, 0x06, 0x00, 0x01,
                              0x00, 0x00, 0x12, 0x00 };
    const uint8_t setup_out[] = { 0x40, 0x5a, 0x00, 0x00,
                                  0x00, 0x00, 0x04, 0x00 };
    const uint8_t setup_ep1_in[] = { 0xc0, 0x75, 0x00, 0x00,
                                     0x00, 0x00, 0x08, 0x00 };
    const uint8_t setup_ep3_out[] = { 0x40, 0x93, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00 };
    const uint8_t setup_ep3_data_out[] = { 0x40, 0x94, 0x00, 0x00,
                                           0x00, 0x00, 0x04, 0x00 };
    const uint8_t control_in_data[] = {
        0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
        0x25, 0x05, 0x75, 0x09, 0x00, 0x01, 0x01, 0x02,
        0x03, 0x01,
    };
    const uint8_t out_data[] = {
        0x75, 0x64, 0x70, 0x68, 0x73, 0x2d, 0x6f, 0x75,
        0x74, 0x2d, 0x74, 0x6f, 0x6b, 0x65, 0x6e,
    };
    const uint8_t in_data[] = {
        0x75, 0x64, 0x70, 0x68, 0x73, 0x2d, 0x69, 0x6e,
        0x2d, 0x74, 0x6f, 0x6b, 0x65, 0x6e,
    };
    const uint8_t ep1_in_data[] = {
        0x91, 0x75, 0xc0, 0x01, 0x91, 0x75, 0xc0, 0x02,
    };
    uint8_t observed[sizeof(control_in_data)];
    uint32_t status;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2");

    pmc_write_pcr(qts, 4, PMC_PCR_EN);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(8));

    /* Keep OHCI running from PLLA while the device UTMI clock is stopped. */
    pmc_configure_pll(qts, 0);
    pmc_write_pcr(qts, 22, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_USB, PMC_USB_USBDIV(9));
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_SCER, PMC_SC_UHP);
    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/pclk"),
                     !=, 0);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/udphs/utmi"),
                     ==, 0);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/uhphs-ohci/uhpck"),
                     !=, 0);

    aic_configure(qts, 23, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0017);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_TST, 3);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_DETACH |
                 UDPHS_CTRL_PULLD_DIS);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_PULLD_DIS);

    qtest_memset(qts, hcca, 0, 0x100);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);

    qtest_memwrite(qts, setup_buf, setup, sizeof(setup));
    qtest_writel(qts, setup_ed, 64U << 16);
    qtest_writel(qts, setup_ed + 4, setup_tail);
    qtest_writel(qts, setup_ed + 8, setup_td);
    qtest_writel(qts, setup_ed + 12, 0);
    qtest_writel(qts, setup_td, 0xf2000000);
    qtest_writel(qts, setup_td + 4, setup_buf);
    qtest_writel(qts, setup_td + 8, setup_tail);
    qtest_writel(qts, setup_td + 12, setup_buf + sizeof(setup) - 1);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL_HEAD,
                 setup_ed);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_CLE);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_CLF);
    qtest_clock_step(qts, 5 * 1000000LL);

    /* Raw SETUP delivery waits for both device clocks. */
    g_assert_cmphex(qtest_readl(qts, setup_ed + 8) & ~3U, ==, setup_td);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(0)) &
                   UDPHS_EPTSTA_RX_SETUP);
    g_assert_false(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                               UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    pmc_enable_upll(qts);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(1),
                 UHPFS_PORT_PRS);
    qtest_clock_step(qts, 2 * 1000000LL);
    /* Bus reset clears endpoint configuration; firmware then enables EP0. */
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(0)),
                    ==, UDPHS_EPTSTA_RESET);
    udphs_configure_endpoint(qts, 0,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_BANKS(1),
                             UDPHS_EPTCTL_RX_SETUP_IE);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_EPT(0));
    ohci_wait_for_ed(qts, setup_ed, setup_tail);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(0)) &
                    (UDPHS_EPTSTA_RX_SETUP |
                     UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RX_SETUP |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(setup)));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE, observed, sizeof(setup));
    g_assert_cmpmem(observed, sizeof(setup), setup, sizeof(setup));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(0)) &
                    UDPHS_EPTSTA_BYTE_COUNT(0x7ff), ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(0),
                 UDPHS_EPTSTA_RX_SETUP);

    /* Complete the control-IN data and status-OUT stages token by token. */
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE, control_in_data,
                   sizeof(control_in_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(0),
                 UDPHS_EPTSTA_TXRDY);
    qtest_memset(qts, bulk_buf, 0, sizeof(control_in_data));
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             bulk_buf, 0, 0xf3100000,
                             sizeof(control_in_data));
    qtest_memread(qts, bulk_buf, observed, sizeof(control_in_data));
    g_assert_cmpmem(observed, sizeof(control_in_data), control_in_data,
                    sizeof(control_in_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(0),
                 UDPHS_EPTSTA_TXCOMPLT);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             0, 0, 0xf3080000, 0);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(0));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY);
    g_assert_true(status & UDPHS_EPTSTA_NAK_OUT);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(0),
                 UDPHS_EPTSTA_RXRDY);

    /* Exercise the opposite control-OUT/data/status-IN direction too. */
    qtest_memwrite(qts, setup_buf, setup_out, sizeof(setup_out));
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             setup_buf, 0, 0xf2000000, sizeof(setup_out));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(0));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RX_SETUP |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RX_SETUP |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(setup_out)));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE, observed,
                  sizeof(setup_out));
    g_assert_cmpmem(observed, sizeof(setup_out), setup_out,
                    sizeof(setup_out));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(0),
                 UDPHS_EPTSTA_RX_SETUP);
    qtest_memwrite(qts, bulk_buf, out_data, 4);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             bulk_buf, 0, 0xf3080000, 4);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(0));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY | UDPHS_EPTSTA_BYTE_COUNT(4));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE, observed, 4);
    g_assert_cmpmem(observed, 4, out_data, 4);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(0),
                 UDPHS_EPTSTA_RXRDY);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(0),
                 UDPHS_EPTSTA_TXRDY);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             0, 0, 0xf3100000, 0);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(0));
    g_assert_true(status & UDPHS_EPTSTA_TXCOMPLT);
    g_assert_false(status & UDPHS_EPTSTA_NAK_IN);

    /* CONTROL is also documented on EP1-6; EPT_DIR is ignored. */
    udphs_configure_endpoint(qts, 1,
                             UDPHS_EPTCFG_SIZE(0) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_BANKS(1),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_memwrite(qts, setup_buf, setup_ep1_in, sizeof(setup_ep1_in));
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             setup_buf, 1, 0xf2000000,
                             sizeof(setup_ep1_in));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RX_SETUP |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RX_SETUP |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(setup_ep1_in)));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16), observed,
                  sizeof(setup_ep1_in));
    g_assert_cmpmem(observed, sizeof(setup_ep1_in), setup_ep1_in,
                    sizeof(setup_ep1_in));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(1),
                 UDPHS_EPTSTA_RX_SETUP);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(1), bulk_buf);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(1),
                 UDPHS_DMA_CHANN_ENB |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(ep1_in_data)));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(1)) &
                    (UDPHS_DMA_CHANN_ENB |
                     UDPHS_DMA_BUFF_COUNT(0xffff)), ==,
                    UDPHS_DMA_CHANN_ENB |
                    UDPHS_DMA_BUFF_COUNT(sizeof(ep1_in_data)));
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16),
                   ep1_in_data, sizeof(ep1_in_data));
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                               UDPHS_EPTSTA(1)) & UDPHS_EPTSTA_TXRDY);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(1),
                 UDPHS_EPTSTA_TXRDY);
    qtest_memset(qts, bulk_buf, 0, sizeof(ep1_in_data));
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             bulk_buf, 1, 0xf3100000,
                             sizeof(ep1_in_data));
    qtest_memread(qts, bulk_buf, observed, sizeof(ep1_in_data));
    g_assert_cmpmem(observed, sizeof(ep1_in_data), ep1_in_data,
                    sizeof(ep1_in_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(1)) &
                    (UDPHS_DMA_CHANN_ENB |
                     UDPHS_DMA_BUFF_COUNT(0xffff)), ==,
                    UDPHS_DMA_CHANN_ENB |
                    UDPHS_DMA_BUFF_COUNT(sizeof(ep1_in_data)));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(1), 0);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             0, 1, 0xf3080000, 0);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY);
    g_assert_true(status & UDPHS_EPTSTA_NAK_OUT);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(1),
                 UDPHS_EPTSTA_RXRDY);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLDIS(1),
                 UDPHS_EPTCTL_AUTO_VALID);

    /* MAPD is resource-only: CONTROL with more than one bank stalls. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_BANKS(2), 0);
    qtest_memwrite(qts, setup_buf, setup_ep3_out, sizeof(setup_ep3_out));
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             setup_buf, 3, 0xf2000000,
                             sizeof(setup_ep3_out));
    g_assert_cmphex(qtest_readl(qts, setup_td) >> 28, ==, 4);

    /* CONTROL packet sizes above 64 bytes are operationally unsupported. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(7) |
                             UDPHS_EPTCFG_BANKS(1), 0);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             setup_buf, 3, 0xf2000000,
                             sizeof(setup_ep3_out));
    g_assert_cmphex(qtest_readl(qts, setup_td) >> 28, ==, 4);

    /* A one-bank EP3 completes a no-data control-OUT request. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_BANKS(1), 0);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             setup_buf, 3, 0xf2000000,
                             sizeof(setup_ep3_out));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16), observed,
                  sizeof(setup_ep3_out));
    g_assert_cmpmem(observed, sizeof(setup_ep3_out), setup_ep3_out,
                    sizeof(setup_ep3_out));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(3),
                 UDPHS_EPTSTA_RX_SETUP);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(3),
                 UDPHS_EPTSTA_TXRDY);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             0, 3, 0xf3100000, 0);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_true(status & UDPHS_EPTSTA_TXCOMPLT);
    g_assert_false(status & UDPHS_EPTSTA_NAK_IN);

    /* AUTO_VALID is ignored throughout a nonzero control-OUT data stage. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_BANKS(1),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_memwrite(qts, setup_buf, setup_ep3_data_out,
                   sizeof(setup_ep3_data_out));
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             setup_buf, 3, 0xf2000000,
                             sizeof(setup_ep3_data_out));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16), observed,
                  sizeof(setup_ep3_data_out));
    g_assert_cmpmem(observed, sizeof(setup_ep3_data_out),
                    setup_ep3_data_out, sizeof(setup_ep3_data_out));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(3),
                 UDPHS_EPTSTA_RX_SETUP);
    qtest_memwrite(qts, bulk_buf, out_data, 4);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             bulk_buf, 3, 0xf3080000, 4);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY | UDPHS_EPTSTA_BYTE_COUNT(4));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16), observed, 4);
    g_assert_cmpmem(observed, 4, out_data, 4);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(3),
                 UDPHS_EPTSTA_RXRDY);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(3),
                 UDPHS_EPTSTA_TXRDY);
    udphs_ohci_control_token(qts, setup_ed, setup_td, setup_tail,
                             0, 3, 0xf3100000, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                              UDPHS_EPTSTA(3)) & UDPHS_EPTSTA_TXCOMPLT);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLDIS(3),
                 UDPHS_EPTCTL_AUTO_VALID);

    udphs_configure_endpoint(qts, 1,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_RXRDY_IE);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_EPT(1));
    qtest_memwrite(qts, bulk_buf, out_data, sizeof(out_data));
    qtest_writel(qts, bulk_ed,
                 (64U << 16) | (1U << 11) | (1U << 7));
    qtest_writel(qts, bulk_ed + 4, bulk_tail);
    qtest_writel(qts, bulk_ed + 8, bulk_td);
    qtest_writel(qts, bulk_ed + 12, 0);
    qtest_writel(qts, bulk_td, 0xf2000000);
    qtest_writel(qts, bulk_td + 4, bulk_buf);
    qtest_writel(qts, bulk_td + 8, bulk_tail);
    qtest_writel(qts, bulk_td + 12, bulk_buf + sizeof(out_data) - 1);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_BULK_HEAD,
                 bulk_ed);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_BLE);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_BLF);
    ohci_wait_for_ed(qts, bulk_ed, bulk_tail);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(1)) &
                    (UDPHS_EPTSTA_RXRDY |
                     UDPHS_EPTSTA_BUSY(3) |
                     UDPHS_EPTSTA_BYTE_COUNT(0x7ff) |
                     UDPHS_EPTSTA_SHORT), ==,
                    UDPHS_EPTSTA_RXRDY |
                    UDPHS_EPTSTA_BUSY(1) |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(out_data)) |
                    UDPHS_EPTSTA_SHORT);
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (1U << 16),
                  observed, sizeof(out_data));
    g_assert_cmpmem(observed, sizeof(out_data), out_data, sizeof(out_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(1)) &
                    (UDPHS_EPTSTA_RXRDY |
                     UDPHS_EPTSTA_BYTE_COUNT(0x7ff) |
                     UDPHS_EPTSTA_SHORT), ==,
                    UDPHS_EPTSTA_RXRDY | UDPHS_EPTSTA_SHORT);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(1),
                 UDPHS_EPTSTA_RXRDY);

    udphs_configure_endpoint(qts, 2,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_TXCOMPLT_IE);
    /* EPTRST clears endpoint state but preserves a nonzero TOGGLESQ_STA. */
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(2)) &
                    UDPHS_EPTSTA_TOGGLE_MASK, ==,
                    UDPHS_EPTSTA_RESET);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTRST, BIT(2));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(2)) &
                    UDPHS_EPTSTA_TOGGLE_MASK, ==,
                    UDPHS_EPTSTA_RESET);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_EPT(2));
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (2U << 16),
                   in_data, sizeof(in_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(2),
                 UDPHS_EPTSTA_TXRDY);
    qtest_memset(qts, bulk_buf, 0, sizeof(in_data));
    qtest_writel(qts, bulk_ed,
                 (64U << 16) | (2U << 11) | (2U << 7));
    qtest_writel(qts, bulk_ed + 4, bulk_tail);
    qtest_writel(qts, bulk_ed + 8, bulk_td);
    qtest_writel(qts, bulk_td, 0xf2000000);
    qtest_writel(qts, bulk_td + 4, bulk_buf);
    qtest_writel(qts, bulk_td + 8, bulk_tail);
    qtest_writel(qts, bulk_td + 12, bulk_buf + sizeof(in_data) - 1);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_BULK_HEAD,
                 bulk_ed);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_BLF);
    ohci_wait_for_ed(qts, bulk_ed, bulk_tail);
    qtest_memread(qts, bulk_buf, observed, sizeof(in_data));
    g_assert_cmpmem(observed, sizeof(in_data), in_data, sizeof(in_data));
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(2)) &
                  UDPHS_EPTSTA_TXCOMPLT);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(2)) &
                   UDPHS_EPTSTA_TXRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(2)) &
                    UDPHS_EPTSTA_TOGGLE_MASK, ==, 0);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTRST, BIT(2));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(2)) &
                    UDPHS_EPTSTA_TOGGLE_MASK, ==, 0);

    /* Raw-token smoke coverage for valid EP3/EP4 isochronous modes. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(2) |
                             UDPHS_EPTCFG_NB_TRANS(1), 0);
    qtest_writeb(qts, bulk_buf, 0x93);
    udphs_ohci_bulk_transfer(qts, bulk_ed, bulk_td, bulk_tail,
                             bulk_buf, 3, false, 1);
    g_assert_cmphex(qtest_readl(qts, bulk_td) >> 28, ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(3)) &
                    (UDPHS_EPTSTA_RXRDY |
                     UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY | UDPHS_EPTSTA_BYTE_COUNT(1));
    qtest_readb(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(3),
                 UDPHS_EPTSTA_RXRDY);

    udphs_configure_endpoint(qts, 4,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(2) |
                             UDPHS_EPTCFG_NB_TRANS(1), 0);
    qtest_writeb(qts, SAM9X7_UDPHS_FIFO_BASE + (4U << 16), 0x94);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(4),
                 UDPHS_EPTSTA_TXRDY);
    qtest_writeb(qts, bulk_buf, 0);
    udphs_ohci_bulk_transfer(qts, bulk_ed, bulk_td, bulk_tail,
                             bulk_buf, 4, true, 1);
    g_assert_cmphex(qtest_readl(qts, bulk_td) >> 28, ==, 0);
    g_assert_cmphex(qtest_readb(qts, bulk_buf), ==, 0x94);

    /* Interrupt packets work through 512 bytes. */
    udphs_configure_endpoint(qts, 6,
                             UDPHS_EPTCFG_SIZE(6) |
                             UDPHS_EPTCFG_TYPE(3) |
                             UDPHS_EPTCFG_BANKS(1), 0);
    qtest_writeb(qts, bulk_buf, 0x95);
    udphs_ohci_bulk_transfer(qts, bulk_ed, bulk_td, bulk_tail,
                             bulk_buf, 6, false, 1);
    g_assert_cmphex(qtest_readl(qts, bulk_td) >> 28, ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                              UDPHS_EPTSTA(6)) & UDPHS_EPTSTA_RXRDY);

    /* This controller reserves 1024-byte packets for isochronous mode. */
    udphs_configure_endpoint(qts, 6,
                             UDPHS_EPTCFG_SIZE(7) |
                             UDPHS_EPTCFG_TYPE(3) |
                             UDPHS_EPTCFG_BANKS(1), 0);
    qtest_writeb(qts, bulk_buf, 0x96);
    udphs_ohci_bulk_transfer(qts, bulk_ed, bulk_td, bulk_tail,
                             bulk_buf, 6, false, 1);
    g_assert_cmphex(qtest_readl(qts, bulk_td) >> 28, ==, 4);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                               UDPHS_EPTSTA(6)) & UDPHS_EPTSTA_RXRDY);

    /* MAPD is resource-only; an architecturally illegal mode stalls tokens. */
    udphs_configure_endpoint(qts, 4,
                             UDPHS_EPTCFG_SIZE(7) |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2), 0);
    qtest_writeb(qts, bulk_buf, 0xa5);
    udphs_ohci_bulk_transfer(qts, bulk_ed, bulk_td, bulk_tail,
                             bulk_buf, 4, false, 1);
    g_assert_cmphex(qtest_readl(qts, bulk_td) >> 28, ==, 4);
    udphs_configure_endpoint(qts, 5,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(1) |
                             UDPHS_EPTCFG_NB_TRANS(1), 0);
    udphs_ohci_bulk_transfer(qts, bulk_ed, bulk_td, bulk_tail,
                             bulk_buf, 5, false, 1);
    g_assert_cmphex(qtest_readl(qts, bulk_td) >> 28, ==, 4);

    qtest_qmp_device_del(qts, "sam9x75-udphs-gadget");
    g_assert_false(qtest_readl(qts, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(8));
    qtest_quit(qts);
}

static void udphs_ohci_start_bulk_transfer(QTestState *qts, uint32_t ed,
                                           uint32_t td, uint32_t tail,
                                           uint32_t buffer,
                                           unsigned int ep, bool in,
                                           uint32_t td_flags,
                                           size_t length)
{
    uint32_t direction = in ? 2 : 1;

    qtest_writel(qts, ed,
                 (64U << 16) | (direction << 11) | (ep << 7));
    qtest_writel(qts, ed + 4, tail);
    qtest_writel(qts, ed + 8, td);
    qtest_writel(qts, ed + 12, 0);
    qtest_writel(qts, td, td_flags);
    qtest_writel(qts, td + 4, length ? buffer : 0);
    qtest_writel(qts, td + 8, tail);
    qtest_writel(qts, td + 12, length ? buffer + length - 1 : 0);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_BULK_HEAD, ed);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_BLE);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_BLF);
}

static void udphs_ohci_bulk_transfer_flags(QTestState *qts, uint32_t ed,
                                           uint32_t td, uint32_t tail,
                                           uint32_t buffer,
                                           unsigned int ep, bool in,
                                           uint32_t td_flags,
                                           size_t length)
{
    udphs_ohci_start_bulk_transfer(qts, ed, td, tail, buffer, ep, in,
                                   td_flags, length);
    ohci_wait_for_ed(qts, ed, tail);
}

static void udphs_ohci_bulk_transfer(QTestState *qts, uint32_t ed,
                                     uint32_t td, uint32_t tail,
                                     uint32_t buffer, unsigned int ep,
                                     bool in, size_t length)
{
    udphs_ohci_bulk_transfer_flags(qts, ed, td, tail, buffer, ep, in,
                                   0xf2000000, length);
}

static void test_udphs_intdis_dma_gating(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t ed = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t td = SAM9X7_SRAM0_BASE + 0x120;
    const uint32_t tail = SAM9X7_SRAM0_BASE + 0x130;
    const uint32_t host_buffer = SAM9X7_SRAM0_BASE + 0x200;
    const uint32_t clear_source_buffer = SAM9X7_SRAM0_BASE + 0x800;
    const uint32_t disable_ie_buffer = SAM9X7_SRAM0_BASE + 0x900;
    const uint32_t clear_intdis_buffer = SAM9X7_SRAM0_BASE + 0xa00;
    const uint32_t busy_buffer = SAM9X7_SRAM0_BASE + 0xb00;
    const uint32_t reset_source_buffer = SAM9X7_SRAM0_BASE + 0xc00;
    const uint32_t iso_out_buffer = SAM9X7_SRAM0_BASE + 0xd00;
    const uint8_t clear_source_data[] = { 0xc1, 0xea, 0x12, 0x01 };
    const uint8_t disable_ie_data[] = { 0xd1, 0x5a, 0xb1, 0xed };
    const uint8_t clear_intdis_data[] = { 0x1d, 0x15, 0xab, 0x1e };
    const uint8_t busy_first[] = { 0xb0, 0x00, 0x00, 0x01 };
    const uint8_t busy_second[] = { 0xb0, 0x00, 0x00, 0x02 };
    const uint8_t reset_source_data[] = { 0xe9, 0x70, 0x00, 0x06 };
    const uint8_t iso_out_data[] = { 0x15, 0x00, 0x00, 0x04 };
    uint8_t observed[sizeof(clear_source_data)];
    uint32_t status;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-intdis-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2");

    udphs_start_test_link(qts, hcca);

    /* Clearing an enabled NAK source starts a previously blocked IN DMA. */
    udphs_configure_endpoint(qts, 2,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID |
                             UDPHS_EPTCTL_INTDIS_DMA |
                             UDPHS_EPTCTL_NAK_IN_IE);
    qtest_memset(qts, host_buffer, 0, sizeof(clear_source_data));
    udphs_ohci_start_bulk_transfer(qts, ed, td, tail, host_buffer, 2,
                                   true, 0xf2040000,
                                   sizeof(clear_source_data));
    qtest_clock_step(qts, 3 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, ed + 8) & ~3U, ==, td);
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                              UDPHS_EPTSTA(2)) & UDPHS_EPTSTA_NAK_IN);
    qtest_memwrite(qts, clear_source_buffer, clear_source_data,
                   sizeof(clear_source_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(2),
                 clear_source_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(2),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(clear_source_data)));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(2)), ==,
                    clear_source_buffer);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(2));
    g_assert_cmphex(status, ==,
                    UDPHS_DMA_CHANN_ENB |
                    UDPHS_DMA_BUFF_COUNT(sizeof(clear_source_data)));
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_EPT(2));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCLRSTA(2),
                 UDPHS_EPTSTA_NAK_IN);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(2)), ==,
                    clear_source_buffer + sizeof(clear_source_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(2)), ==,
                    UDPHS_DMA_END_BF_ST);
    ohci_wait_for_ed(qts, ed, tail);
    qtest_memread(qts, host_buffer, observed, sizeof(clear_source_data));
    g_assert_cmpmem(observed, sizeof(clear_source_data),
                    clear_source_data, sizeof(clear_source_data));

    /* Disabling the local cause also resumes DMA; global EPT IEN is clear. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID |
                             UDPHS_EPTCTL_INTDIS_DMA |
                             UDPHS_EPTCTL_TXRDY_IE);
    qtest_memwrite(qts, disable_ie_buffer, disable_ie_data,
                   sizeof(disable_ie_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(3),
                 disable_ie_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(3),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(disable_ie_data)));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(3)), ==,
                    disable_ie_buffer);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_EPT(3));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLDIS(3),
                 UDPHS_EPTCTL_TXRDY_IE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(3)), ==,
                    disable_ie_buffer + sizeof(disable_ie_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(3)), ==,
                    UDPHS_DMA_END_BF_ST);
    qtest_memset(qts, host_buffer, 0, sizeof(disable_ie_data));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 3, true,
                             sizeof(disable_ie_data));
    qtest_memread(qts, host_buffer, observed, sizeof(disable_ie_data));
    g_assert_cmpmem(observed, sizeof(disable_ie_data), disable_ie_data,
                    sizeof(disable_ie_data));

    /* Clearing INTDIS itself lets a pending OUT bank reach DMA. */
    udphs_configure_endpoint(qts, 1,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID |
                             UDPHS_EPTCTL_INTDIS_DMA |
                             UDPHS_EPTCTL_RXRDY_IE);
    qtest_memset(qts, clear_intdis_buffer, 0,
                 sizeof(clear_intdis_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(1),
                 clear_intdis_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(1),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(clear_intdis_data)));
    qtest_memwrite(qts, host_buffer, clear_intdis_data,
                   sizeof(clear_intdis_data));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 1, false,
                             sizeof(clear_intdis_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(1)), ==,
                    clear_intdis_buffer);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(1));
    g_assert_cmphex(status & (UDPHS_EPTSTA_RXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff)), ==,
                    UDPHS_EPTSTA_RXRDY |
                    UDPHS_EPTSTA_BYTE_COUNT(sizeof(clear_intdis_data)));
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_EPT(1));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLDIS(1),
                 UDPHS_EPTCTL_INTDIS_DMA);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(1)), ==,
                    clear_intdis_buffer + sizeof(clear_intdis_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(1)), ==,
                    UDPHS_DMA_END_BF_ST);
    qtest_memread(qts, clear_intdis_buffer, observed,
                  sizeof(clear_intdis_data));
    g_assert_cmpmem(observed, sizeof(clear_intdis_data),
                    clear_intdis_data, sizeof(clear_intdis_data));

    /* PIO AUTO_VALID deasserts BUSY and services the remaining OUT bank. */
    udphs_configure_endpoint(qts, 5,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID |
                             UDPHS_EPTCTL_INTDIS_DMA |
                             UDPHS_EPTCTL_BUSY_IE);
    qtest_memwrite(qts, host_buffer, busy_first, sizeof(busy_first));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 5, false,
                             sizeof(busy_first));
    qtest_memwrite(qts, host_buffer, busy_second, sizeof(busy_second));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 5, false,
                             sizeof(busy_second));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(5));
    g_assert_cmphex(status & UDPHS_EPTSTA_BUSY(3), ==,
                    UDPHS_EPTSTA_BUSY(2));
    qtest_memset(qts, busy_buffer, 0, sizeof(busy_second));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(5),
                 busy_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(5),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(busy_second)));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(5)), ==, busy_buffer);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_EPT(5));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (5U << 16),
                  observed, sizeof(busy_first));
    g_assert_cmpmem(observed, sizeof(busy_first), busy_first,
                    sizeof(busy_first));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(5)), ==,
                    busy_buffer + sizeof(busy_second));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(5)), ==,
                    UDPHS_DMA_END_BF_ST);
    qtest_memread(qts, busy_buffer, observed, sizeof(busy_second));
    g_assert_cmpmem(observed, sizeof(busy_second), busy_second,
                    sizeof(busy_second));

    /* ISO OUT bit 11 is a positive error source, not active-low TXRDY. */
    udphs_configure_endpoint(qts, 4,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(1) |
                             UDPHS_EPTCFG_BANKS(2) |
                             UDPHS_EPTCFG_NB_TRANS(1),
                             UDPHS_EPTCTL_AUTO_VALID |
                             UDPHS_EPTCTL_INTDIS_DMA |
                             UDPHS_EPTCTL_TXRDY_IE);
    qtest_memset(qts, iso_out_buffer, 0, sizeof(iso_out_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(4),
                 iso_out_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(4),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(iso_out_data)));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(4)), ==,
                    iso_out_buffer);
    qtest_memwrite(qts, host_buffer, iso_out_data, sizeof(iso_out_data));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 4, false,
                             sizeof(iso_out_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(4)), ==,
                    iso_out_buffer + sizeof(iso_out_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(4)), ==,
                    UDPHS_DMA_END_BF_ST);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_EPT(4));
    qtest_memread(qts, iso_out_buffer, observed, sizeof(iso_out_data));
    g_assert_cmpmem(observed, sizeof(iso_out_data), iso_out_data,
                    sizeof(iso_out_data));

    /* EPTRST clears the blocking NAK and immediately reschedules IN DMA. */
    udphs_configure_endpoint(qts, 6,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID |
                             UDPHS_EPTCTL_INTDIS_DMA |
                             UDPHS_EPTCTL_NAK_IN_IE);
    qtest_memset(qts, host_buffer, 0, sizeof(reset_source_data));
    udphs_ohci_start_bulk_transfer(qts, ed, td, tail, host_buffer, 6,
                                   true, 0xf2040000,
                                   sizeof(reset_source_data));
    qtest_clock_step(qts, 3 * 1000000LL);
    g_assert_cmphex(qtest_readl(qts, ed + 8) & ~3U, ==, td);
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                              UDPHS_EPTSTA(6)) & UDPHS_EPTSTA_NAK_IN);
    qtest_memwrite(qts, reset_source_buffer, reset_source_data,
                   sizeof(reset_source_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(6),
                 reset_source_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(6),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(reset_source_data)));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(6)), ==,
                    reset_source_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTRST, BIT(6));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(6)), ==,
                    reset_source_buffer + sizeof(reset_source_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(6)), ==,
                    UDPHS_DMA_END_BF_ST);
    ohci_wait_for_ed(qts, ed, tail);
    qtest_memread(qts, host_buffer, observed, sizeof(reset_source_data));
    g_assert_cmpmem(observed, sizeof(reset_source_data), reset_source_data,
                    sizeof(reset_source_data));

    qtest_quit(qts);
}

static void test_udphs_dma_data_tokens(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t ed = SAM9X7_SRAM0_BASE + 0x100;
    const uint32_t td = SAM9X7_SRAM0_BASE + 0x120;
    const uint32_t tail = SAM9X7_SRAM0_BASE + 0x130;
    const uint32_t host_buffer = SAM9X7_SRAM0_BASE + 0x200;
    const uint32_t dma_in_buffer = SAM9X7_SRAM0_BASE + 0x800;
    const uint32_t dma_out_buffer = SAM9X7_SRAM0_BASE + 0x900;
    const uint32_t chain_desc = SAM9X7_SRAM0_BASE + 0xc00;
    const uint32_t chain_tail = SAM9X7_SRAM0_BASE + 0xc10;
    const uint32_t chain_buffer = SAM9X7_SRAM0_BASE + 0xd00;
    const uint8_t oldest[] = { 0x6f, 0x6c, 0x64, 0x65, 0x73, 0x74 };
    const uint8_t newest[] = { 0x6e, 0x65, 0x77, 0x65, 0x73, 0x74 };
    uint8_t in_data[96];
    uint8_t out_data[23];
    uint8_t observed[sizeof(in_data)];
    uint32_t status;
    unsigned int i;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-dma-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2");

    for (i = 0; i < sizeof(in_data); i++) {
        in_data[i] = 0x40 ^ i;
    }
    for (i = 0; i < sizeof(out_data); i++) {
        out_data[i] = 0xa0 + i;
    }

    pmc_configure_usb_host(qts);
    pmc_write_pcr(qts, 23, PMC_PCR_EN);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_TST, 3);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_PULLD_DIS);
    qtest_clock_step(qts, 1000000);
    g_assert_true(qtest_readl(qts, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(1),
                 UHPFS_PORT_PRS);
    qtest_clock_step(qts, 2 * 1000000LL);
    qtest_memset(qts, hcca, 0, 0x100);
    qtest_writel(qts, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);

    /* Linux's direct-buffer IN path: memory -> two endpoint banks -> host. */
    udphs_configure_endpoint(qts, 2,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_DMA(2));
    qtest_memwrite(qts, dma_in_buffer, in_data, sizeof(in_data));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(2),
                 dma_in_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(2),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(in_data)));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(2)), ==,
                    dma_in_buffer + sizeof(in_data));
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                  UDPHS_INT_DMA(2));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(2));
    g_assert_cmphex(status, ==, UDPHS_DMA_END_BF_ST);
    g_assert_false(status & (UDPHS_DMA_CHANN_ENB | UDPHS_DMA_CHANN_ACT));
    qtest_memread(qts, SAM9X7_UDPHS_FIFO_BASE + (2U << 16),
                  observed, sizeof(in_data) - 64);
    g_assert_cmpmem(observed, sizeof(in_data) - 64,
                    in_data + 64, sizeof(in_data) - 64);
    qtest_memset(qts, host_buffer, 0, sizeof(in_data));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 2, true,
                             64);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(2));
    g_assert_cmphex(status & (UDPHS_EPTSTA_TXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff) |
                              UDPHS_EPTSTA_BUSY(3)), ==,
                    UDPHS_EPTSTA_BUSY(1));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer + 64, 2, true,
                             sizeof(in_data) - 64);
    qtest_memread(qts, host_buffer, observed, sizeof(in_data));
    g_assert_cmpmem(observed, sizeof(in_data), in_data, sizeof(in_data));

    /* SHRT_PCKT appends a ZLP when an IN buffer ends on a packet boundary. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLENB(2),
                 UDPHS_EPTCTL_SHORT_IE);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(2),
                 dma_in_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(2),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE | UDPHS_DMA_BUFF_LENGTH(64));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(2));
    g_assert_cmphex(status & UDPHS_EPTSTA_BUSY(3), ==,
                    UDPHS_EPTSTA_BUSY(2));
    qtest_memset(qts, host_buffer, 0, 64);
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 2, true, 64);
    qtest_memread(qts, host_buffer, observed, 64);
    g_assert_cmpmem(observed, 64, in_data, 64);
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 2, true, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(2)) &
                    UDPHS_EPTSTA_BUSY(3), ==, 0);

    /* Linux's unknown-size OUT path ends on a USB short packet. */
    udphs_configure_endpoint(qts, 1,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_DMA(1));
    qtest_memset(qts, dma_out_buffer, 0, 64);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(1),
                 dma_out_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(1),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_TR_EN |
                 UDPHS_DMA_END_BF_EN | UDPHS_DMA_END_TR_IE |
                 UDPHS_DMA_END_BF_IE | UDPHS_DMA_BUFF_LENGTH(64));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(1));
    g_assert_cmphex(status, ==,
                    UDPHS_DMA_CHANN_ENB | UDPHS_DMA_BUFF_COUNT(64));
    qtest_memwrite(qts, host_buffer, out_data, sizeof(out_data));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 1, false,
                             sizeof(out_data));
    qtest_memread(qts, dma_out_buffer, observed, sizeof(out_data));
    g_assert_cmpmem(observed, sizeof(out_data), out_data, sizeof(out_data));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(1)), ==,
                    dma_out_buffer + sizeof(out_data));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(1));
    g_assert_cmphex(status, ==,
                    UDPHS_DMA_BUFF_COUNT(64 - sizeof(out_data)) |
                    UDPHS_DMA_END_TR_ST);
    g_assert_false(status & (UDPHS_DMA_CHANN_ENB | UDPHS_DMA_CHANN_ACT));

    /* A zero-length OUT packet also terminates without decrementing count. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(1),
                 dma_out_buffer);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(1),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_TR_EN |
                 UDPHS_DMA_END_BF_EN | UDPHS_DMA_END_TR_IE |
                 UDPHS_DMA_BUFF_LENGTH(64));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 1, false, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(1)), ==, dma_out_buffer);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(1));
    g_assert_cmphex(status, ==,
                    UDPHS_DMA_BUFF_COUNT(64) | UDPHS_DMA_END_TR_ST);

    /* A full two-bank IN queue exposes the newly freed CPU bank. */
    udphs_configure_endpoint(qts, 3,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2), 0);
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16),
                   oldest, sizeof(oldest));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(3),
                 UDPHS_EPTSTA_TXRDY);
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16),
                   newest, sizeof(newest));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(3),
                 UDPHS_EPTSTA_TXRDY);
    qtest_memset(qts, host_buffer, 0, sizeof(oldest));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 3, true,
                             sizeof(oldest));
    qtest_memread(qts, host_buffer, observed, sizeof(oldest));
    g_assert_cmpmem(observed, sizeof(oldest), oldest, sizeof(oldest));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_cmphex(status & (UDPHS_EPTSTA_TXRDY |
                              UDPHS_EPTSTA_BYTE_COUNT(0x7ff) |
                              UDPHS_EPTSTA_BUSY(3)), ==,
                    UDPHS_EPTSTA_BUSY(1));

    /* RXRDY_TXKL kills the newest packet and leaves the oldest queued. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTRST, BIT(3));
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16),
                   oldest, sizeof(oldest));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(3),
                 UDPHS_EPTSTA_TXRDY);
    qtest_memwrite(qts, SAM9X7_UDPHS_FIFO_BASE + (3U << 16),
                   newest, sizeof(newest));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(3),
                 UDPHS_EPTSTA_TXRDY);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(3),
                 UDPHS_EPTSTA_RXRDY);
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(3));
    g_assert_cmphex(status & UDPHS_EPTSTA_BUSY(3), ==,
                    UDPHS_EPTSTA_BUSY(1));
    qtest_memset(qts, host_buffer, 0, sizeof(oldest));
    udphs_ohci_bulk_transfer(qts, ed, td, tail, host_buffer, 3, true,
                             sizeof(oldest));
    qtest_memread(qts, host_buffer, observed, sizeof(oldest));
    g_assert_cmpmem(observed, sizeof(oldest), oldest, sizeof(oldest));

    /* Completion IRQ enables belong to the descriptor that completed. */
    udphs_configure_endpoint(qts, 6,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_writel(qts, chain_desc, chain_tail);
    qtest_writel(qts, chain_desc + 4, chain_buffer);
    qtest_writel(qts, chain_desc + 8,
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_LDNXT_DSC |
                 UDPHS_DMA_END_BF_EN | UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(4));
    qtest_writel(qts, chain_tail, 0);
    qtest_writel(qts, chain_tail + 4, chain_buffer + 4);
    qtest_writel(qts, chain_tail + 8, 0);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMANXTDSC(6),
                 chain_desc);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(6),
                 UDPHS_DMA_LDNXT_DSC);
    /* The local event exists, but its global summary is still masked. */
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_DMA(6));
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_DMA(6));
    g_assert_true(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                  UDPHS_INT_DMA(6));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(6));
    g_assert_true(status & UDPHS_DMA_END_BF_ST);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_DMA(6));

    /* A following descriptor must not retroactively enable that event. */
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_EPTRST, BIT(6));
    qtest_writel(qts, chain_desc + 8,
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_LDNXT_DSC |
                 UDPHS_DMA_END_BF_EN | UDPHS_DMA_BUFF_LENGTH(4));
    qtest_writel(qts, chain_tail + 8, UDPHS_DMA_END_BF_IE);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMANXTDSC(6),
                 chain_desc);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(6),
                 UDPHS_DMA_LDNXT_DSC);
    g_assert_false(qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_DMA(6));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(6));
    g_assert_true(status & UDPHS_DMA_END_BF_ST);

    /* A system-memory decode error stops the channel without overrunning. */
    udphs_configure_endpoint(qts, 4,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(4),
                 0x10000000);
    qtest_writel(qts, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(4),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_TR_IE |
                 UDPHS_DMA_BUFF_LENGTH(4));
    status = qtest_readl(qts, SAM9X7_UDPHS_BASE + UDPHS_DMASTATUS(4));
    g_assert_cmphex(status, ==,
                    UDPHS_DMA_BUFF_COUNT(4) | UDPHS_DMA_END_TR_ST);
    g_assert_false(status & (UDPHS_DMA_CHANN_ENB | UDPHS_DMA_CHANN_ACT));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(4)), ==, 0x10000000);

    qtest_quit(qts);
}

static void test_udphs_migration(void)
{
    const uint32_t hcca = SAM9X7_SRAM0_BASE;
    const uint32_t in_ed = SAM9X7_SRAM0_BASE + 0xb00;
    const uint32_t in_td = SAM9X7_SRAM0_BASE + 0xb20;
    const uint32_t in_tail = SAM9X7_SRAM0_BASE + 0xb30;
    const uint32_t host_buffer = SAM9X7_SRAM0_BASE + 0xc00;
    const uint8_t fifo_data[] = {
        0x6d, 0x69, 0x67, 0x72, 0x61, 0x74, 0x65,
    };
    const uint8_t disabled_fifo_data[] = {
        0xd1, 0x5a, 0xb1, 0xed, 0x75,
    };
    uint8_t observed[sizeof(fifo_data)];
    const uint32_t endpoint_config = UDPHS_EPTCFG_SIZE(3) |
                                     UDPHS_EPTCFG_DIR_IN |
                                     UDPHS_EPTCFG_TYPE(2) |
                                     UDPHS_EPTCFG_BANKS(2);
    const uint32_t dma_descriptor = SAM9X7_SRAM0_BASE + 0x800;
    const uint32_t dma_address = SAM9X7_SRAM0_BASE + 0x900;
    const uint32_t active_dma_address = SAM9X7_SRAM0_BASE + 0xa00;
    const uint32_t pending_dma_address = SAM9X7_SRAM0_BASE + 0xd00;
    const uint8_t pending_dma_data[] = { 0xd6, 0xa1, 0x91, 0x75 };
    const uint32_t dma_control = UDPHS_DMA_END_TR_EN |
                                 UDPHS_DMA_END_BF_EN |
                                 UDPHS_DMA_END_TR_IE |
                                 UDPHS_DMA_BUFF_LENGTH(0x40);
    uint32_t endpoint_byte_count;
    QTestState *from = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-migrate-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2");
    QTestState *to = qtest_init(
        SAM9X75_MACHINE
        " -device at91-udphs-gadget,id=sam9x75-udphs-migrate-gadget,"
        "udphs=/machine/soc/udphs,bus=usb-bus.0,port=2 -incoming defer");

    pmc_configure_usb_host(from);
    pmc_write_pcr(from, 4, PMC_PCR_EN);
    pmc_write_pcr(from, 23, PMC_PCR_EN);
    aic_configure(from, 23, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0017);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_DEV_ADDR(7) | UDPHS_CTRL_FADDR_EN |
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_PULLD_DIS);
    qtest_writel(from, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_RH_PORT(1),
                 UHPFS_PORT_PRS);
    qtest_clock_step(from, 2 * 1000000LL);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_CTRL,
                 UDPHS_CTRL_DEV_ADDR(7) | UDPHS_CTRL_FADDR_EN |
                 UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_PULLD_DIS);
    udphs_configure_endpoint(from, 2, endpoint_config,
                             UDPHS_EPTCTL_TXRDY_IE);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_IEN,
                 UDPHS_INT_DMA(6));
    qtest_memwrite(from, SAM9X7_UDPHS_FIFO_BASE + (2U << 16),
                   fifo_data, sizeof(fifo_data));
    endpoint_byte_count = qtest_readl(from, SAM9X7_UDPHS_BASE +
                                      UDPHS_EPTSTA(2)) &
                          UDPHS_EPTSTA_BYTE_COUNT(0x7ff);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_DMANXTDSC(2),
                 dma_descriptor);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(2),
                 dma_address);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(2),
                 dma_control);
    udphs_configure_endpoint(from, 1,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(1),
                 active_dma_address);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(1),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_TR_EN |
                 UDPHS_DMA_END_TR_IE | UDPHS_DMA_BUFF_LENGTH(64));

    /* Endpoint disable resets protocol metadata without erasing DPRAM. */
    udphs_configure_endpoint(from, 5,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2), 0);
    qtest_memwrite(from, SAM9X7_UDPHS_FIFO_BASE + (5U << 16),
                   disabled_fifo_data, sizeof(disabled_fifo_data));
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLDIS(5),
                 UDPHS_EPTCTL_ENABLE);
    g_assert_cmphex(qtest_readl(from, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTCTL(5)), ==, 0);
    g_assert_cmphex(qtest_readl(from, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(5)), ==,
                    UDPHS_EPTSTA_RESET);
    qtest_memread(from, SAM9X7_UDPHS_FIFO_BASE + (5U << 16),
                  observed, sizeof(disabled_fifo_data));
    g_assert_cmpmem(observed, sizeof(disabled_fifo_data),
                    disabled_fifo_data, sizeof(disabled_fifo_data));
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLENB(5),
                 UDPHS_EPTCTL_ENABLE);
    qtest_memread(from, SAM9X7_UDPHS_FIFO_BASE + (5U << 16),
                  observed, sizeof(disabled_fifo_data));
    g_assert_cmpmem(observed, sizeof(disabled_fifo_data),
                    disabled_fifo_data, sizeof(disabled_fifo_data));
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLDIS(5),
                 UDPHS_EPTCTL_ENABLE);

    /* Migrate a locally and globally enabled, unread DMA completion. */
    udphs_configure_endpoint(from, 6,
                             UDPHS_EPTCFG_SIZE(3) |
                             UDPHS_EPTCFG_DIR_IN |
                             UDPHS_EPTCFG_TYPE(2) |
                             UDPHS_EPTCFG_BANKS(2),
                             UDPHS_EPTCTL_AUTO_VALID);
    qtest_memwrite(from, pending_dma_address, pending_dma_data,
                   sizeof(pending_dma_data));
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_DMAADDRESS(6),
                 pending_dma_address);
    qtest_writel(from, SAM9X7_UDPHS_BASE + UDPHS_DMACONTROL(6),
                 UDPHS_DMA_CHANN_ENB | UDPHS_DMA_END_BF_EN |
                 UDPHS_DMA_END_BF_IE |
                 UDPHS_DMA_BUFF_LENGTH(sizeof(pending_dma_data)));
    g_assert_true(qtest_readl(from, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                  UDPHS_INT_DMA(6));
    g_assert_true(qtest_readl(from, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(8));
    g_assert_true(qtest_readl(from, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    g_assert_true(qtest_readl(from, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmpuint(get_clock_period(to, "/machine/soc/udphs/pclk"),
                     !=, 0);
    g_assert_cmpuint(get_clock_period(to, "/machine/soc/udphs/utmi"),
                     ==, CLOCK_PERIOD_FROM_HZ(480000000));
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE + UDPHS_CTRL), ==,
                    UDPHS_CTRL_DEV_ADDR(7) | UDPHS_CTRL_FADDR_EN |
                    UDPHS_CTRL_EN_UDPHS | UDPHS_CTRL_PULLD_DIS);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTCFG(2)), ==,
                    endpoint_config | UDPHS_EPTCFG_MAPPED);
    /* The partially filled bank remains unvalidated and keeps TXRDY low. */
    g_assert_false(qtest_readl(to, SAM9X7_UDPHS_BASE + UDPHS_EPTSTA(2)) &
                   UDPHS_EPTSTA_TXRDY);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(2)) &
                    UDPHS_EPTSTA_BYTE_COUNT(0x7ff), ==,
                    endpoint_byte_count);
    qtest_memread(to, SAM9X7_UDPHS_FIFO_BASE + (2U << 16),
                  observed, sizeof(observed));
    g_assert_cmpmem(observed, sizeof(observed), fifo_data,
                    sizeof(fifo_data));
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMANXTDSC(2)), ==, dma_descriptor);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(2)), ==, dma_address);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMACONTROL(2)), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMAADDRESS(1)), ==,
                    active_dma_address);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMACONTROL(1)), ==,
                    UDPHS_DMA_END_TR_EN | UDPHS_DMA_END_TR_IE);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(1)), ==,
                    UDPHS_DMA_CHANN_ENB | UDPHS_DMA_BUFF_COUNT(64));
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTCTL(5)), ==, 0);
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_EPTSTA(5)), ==,
                    UDPHS_EPTSTA_RESET);
    qtest_memread(to, SAM9X7_UDPHS_FIFO_BASE + (5U << 16),
                  observed, sizeof(disabled_fifo_data));
    g_assert_cmpmem(observed, sizeof(disabled_fifo_data),
                    disabled_fifo_data, sizeof(disabled_fifo_data));
    qtest_writel(to, SAM9X7_UDPHS_BASE + UDPHS_EPTCTLENB(5),
                 UDPHS_EPTCTL_ENABLE);
    qtest_memread(to, SAM9X7_UDPHS_FIFO_BASE + (5U << 16),
                  observed, sizeof(disabled_fifo_data));
    g_assert_cmpmem(observed, sizeof(disabled_fifo_data),
                    disabled_fifo_data, sizeof(disabled_fifo_data));
    g_assert_true(qtest_readl(to, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                  UDPHS_INT_DMA(6));
    g_assert_true(qtest_readl(to, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(8));
    g_assert_true(qtest_readl(to, SAM9X7_UHPHS_OHCI_BASE +
                              UHPFS_HC_RH_PORT(1)) & UHPFS_PORT_CCS);
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));

    /* DMASTATUS and its global summary remain read-to-clear after loading. */
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(6)), ==,
                    UDPHS_DMA_END_BF_ST);
    g_assert_false(qtest_readl(to, SAM9X7_UDPHS_BASE + UDPHS_INTSTA) &
                   UDPHS_INT_DMA(6));
    g_assert_false(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(23));
    g_assert_cmphex(qtest_readl(to, SAM9X7_UDPHS_BASE +
                                UDPHS_DMASTATUS(6)), ==, 0);

    /* The restored bridge/address/endpoint must carry a real host token. */
    qtest_memset(to, hcca, 0, 0x100);
    qtest_writel(to, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_HCCA, hcca);
    qtest_writel(to, SAM9X7_UDPHS_BASE + UDPHS_EPTSETSTA(2),
                 UDPHS_EPTSTA_TXRDY);
    qtest_memset(to, host_buffer, 0, sizeof(fifo_data));
    qtest_writel(to, in_ed,
                 (64U << 16) | (2U << 11) | (2U << 7) | 7);
    qtest_writel(to, in_ed + 4, in_tail);
    qtest_writel(to, in_ed + 8, in_td);
    qtest_writel(to, in_ed + 12, 0);
    qtest_writel(to, in_td, 0xf2000000);
    qtest_writel(to, in_td + 4, host_buffer);
    qtest_writel(to, in_td + 8, in_tail);
    qtest_writel(to, in_td + 12,
                 host_buffer + sizeof(fifo_data) - 1);
    qtest_writel(to, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_BULK_HEAD,
                 in_ed);
    qtest_writel(to, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_CONTROL,
                 UHPFS_CONTROL_OPERATIONAL | UHPFS_CONTROL_BLE);
    qtest_writel(to, SAM9X7_UHPHS_OHCI_BASE + UHPFS_HC_COMMAND_STATUS,
                 UHPFS_COMMAND_BLF);
    ohci_wait_for_ed(to, in_ed, in_tail);
    qtest_memread(to, host_buffer, observed, sizeof(observed));
    g_assert_cmpmem(observed, sizeof(observed), fifo_data,
                    sizeof(fifo_data));
    qtest_qmp_device_del(to, "sam9x75-udphs-migrate-gadget");
    g_assert_false(qtest_readl(to, SAM9X7_PIOC_BASE + PIO_PDSR) & BIT(8));

    qtest_quit(to);
    qtest_quit(from);
}

static void mcan_enable_clocks(QTestState *qts, unsigned int pid)
{
    pmc_write_pcr(qts, pid, PMC_PCR_EN | PMC_PCR_GCKEN);
}

static void mcan_configure(QTestState *qts, uint64_t base,
                           uint32_t rx_offset, unsigned int rx_count,
                           uint32_t tx_offset, unsigned int tx_count,
                           bool fd, bool loopback)
{
    uint32_t mode = 0;

    if (fd) {
        mode |= MCAN_CCCR_FDOE | MCAN_CCCR_BRSE;
    }
    if (loopback) {
        mode |= MCAN_CCCR_TEST | MCAN_CCCR_MON;
    }

    /* CCE itself is not protected; the other configuration bits are. */
    qtest_writel(qts, base + MCAN_CCCR, MCAN_CCCR_INIT | MCAN_CCCR_CCE);
    qtest_writel(qts, base + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE | mode);
    qtest_writel(qts, base + MCAN_GFC, 0);
    qtest_writel(qts, base + MCAN_RXESC, fd ? 7 : 0);
    qtest_writel(qts, base + MCAN_RXF0C,
                 rx_offset | MCAN_RXF0C_SIZE(rx_count));
    qtest_writel(qts, base + MCAN_TXESC, fd ? 7 : 0);
    qtest_writel(qts, base + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(tx_count));
    if (loopback) {
        qtest_writel(qts, base + MCAN_TEST, MCAN_TEST_LBCK);
    }
    qtest_writel(qts, base + MCAN_CCCR, mode);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_CCCR), ==, mode);
}

static void mcan_write_tx_element(QTestState *qts, uint32_t offset,
                                  uint32_t word0, uint32_t word1,
                                  const uint32_t *data,
                                  unsigned int data_words)
{
    unsigned int i;

    qtest_writel(qts, SAM9X7_SRAM0_BASE + offset, word0);
    qtest_writel(qts, SAM9X7_SRAM0_BASE + offset + 4, word1);
    for (i = 0; i < data_words; i++) {
        qtest_writel(qts, SAM9X7_SRAM0_BASE + offset + 8 + i * 4,
                     data[i]);
    }
}

static void mcan_begin_loopback_configuration(QTestState *qts,
                                               uint64_t base)
{
    const uint32_t mode = MCAN_CCCR_TEST | MCAN_CCCR_MON;

    qtest_writel(qts, base + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE);
    qtest_writel(qts, base + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE | mode);
}

static void mcan_end_loopback_configuration(QTestState *qts,
                                             uint64_t base)
{
    const uint32_t mode = MCAN_CCCR_TEST | MCAN_CCCR_MON;

    qtest_writel(qts, base + MCAN_TEST, MCAN_TEST_LBCK);
    qtest_writel(qts, base + MCAN_CCCR, mode);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_CCCR), ==, mode);
}

static void mcan_loopback_frame(QTestState *qts, uint64_t base,
                                uint32_t tx_offset, uint32_t id,
                                bool extended, bool remote, uint32_t data)
{
    uint32_t word0 = extended ? (id & 0x1fffffff) | MCAN_ELEMENT_XTD :
                                (id & 0x7ff) << 18;
    uint32_t word1 = remote ? 0 : MCAN_ELEMENT_DLC(4);

    if (remote) {
        word0 |= MCAN_ELEMENT_RTR;
    }
    mcan_write_tx_element(qts, tx_offset, word0, word1, &data,
                          remote ? 0 : 1);
    qtest_writel(qts, base + MCAN_TXBAR, BIT(0));
    g_assert_true(qtest_readl(qts, base + MCAN_TXBTO) & BIT(0));
}

static void mcan_set_gfc(QTestState *qts, uint64_t base, uint32_t gfc)
{
    mcan_begin_loopback_configuration(qts, base);
    qtest_writel(qts, base + MCAN_GFC, gfc);
    mcan_end_loopback_configuration(qts, base);
}

static uint32_t mcan_wait_register(QTestState *qts, uint64_t address,
                                   uint32_t mask, uint32_t expected)
{
    int64_t deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
    uint32_t value;

    do {
        value = qtest_readl(qts, address);
        if ((value & mask) == expected) {
            return value;
        }
        g_usleep(1000);
    } while (g_get_monotonic_time() < deadline);

    g_error("timed out waiting for M_CAN register 0x%" PRIx64
            " mask 0x%08x to become 0x%08x (value 0x%08x)",
            address, mask, expected, value);
}

static void test_mcan_sam_register_semantics(void)
{
    static const uint64_t bases[] = {
        SAM9X7_MCAN0_BASE, SAM9X7_MCAN1_BASE,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(bases); i++) {
        uint32_t timeout;
        unsigned int tos;

        /* SAM9X7 exposes only WDC[7:0], without requiring CCE. */
        qtest_writel(qts, bases[i] + MCAN_RWD, 0x1234abcd);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_RWD), ==, 0xcd);

        /* DBTP remains protected and uses the SAM9X7 mask (bit 3 is RES0). */
        qtest_writel(qts, bases[i] + MCAN_DBTP, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_DBTP), ==,
                        0x00000a33);

        /* TSCFG is likewise writable only while INIT and CCE are set. */
        qtest_writel(qts, bases[i] + MCAN_TSU_TSCFG, 0x00005a07);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TSU_TSCFG), ==, 0);

        qtest_writel(qts, bases[i] + MCAN_CCCR,
                     MCAN_CCCR_INIT | MCAN_CCCR_CCE);
        qtest_writel(qts, bases[i] + MCAN_DBTP, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_DBTP), ==,
                        0x009f1ff7);

        qtest_writel(qts, bases[i] + MCAN_CCCR,
                     MCAN_CCCR_INIT | MCAN_CCCR_CCE | MCAN_CCCR_TEST);
        qtest_writel(qts, bases[i] + MCAN_TEST, UINT32_MAX);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TEST), ==, 0x70);

        /* A write reloads TOP only for the continuous timeout source. */
        qtest_writel(qts, bases[i] + MCAN_TOCC, MCAN_TOCC_TOP(0x1234));
        qtest_writel(qts, bases[i] + MCAN_TOCV, 0xdeadbeef);
        timeout = qtest_readl(qts, bases[i] + MCAN_TOCV);
        g_assert_cmphex(timeout, ==, 0x1234);
        for (tos = 1; tos < 4; tos++) {
            qtest_writel(qts, bases[i] + MCAN_TOCC,
                         MCAN_TOCC_TOP(0x2000 + tos) |
                         MCAN_TOCC_TOS(tos));
            qtest_writel(qts, bases[i] + MCAN_TOCV,
                         0xa5000000 | tos);
            g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TOCV), ==,
                            timeout);
        }

        /* TSCFG itself is destructive; ATB clears only TBPRE[15:8]. */
        qtest_writel(qts, bases[i] + MCAN_TSU_TSCFG, 0x00005a07);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TSU_TSCFG), ==,
                        0x00005a07);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TSU_TSCFG), ==,
                        0);
        qtest_writel(qts, bases[i] + MCAN_TSU_TSCFG, 0x0000a507);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TSU_ATB), ==, 0);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TSU_TSCFG), ==,
                        0x00000007);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TSU_TSCFG), ==,
                        0);
    }

    qtest_quit(qts);
}

static void test_mcan_sam_register_migration(void)
{
    static const uint64_t bases[] = {
        SAM9X7_MCAN0_BASE, SAM9X7_MCAN1_BASE,
    };
    static const uint32_t rwd[] = { 0x5a, 0xa5 };
    static const uint32_t timeout[] = { 0x1234, 0x5678 };
    static const uint32_t tscfg[] = { 0x00004107, 0x00005203 };
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(bases); i++) {
        qtest_writel(from, bases[i] + MCAN_RWD, rwd[i]);
        qtest_writel(from, bases[i] + MCAN_CCCR,
                     MCAN_CCCR_INIT | MCAN_CCCR_CCE);
        qtest_writel(from, bases[i] + MCAN_DBTP, UINT32_MAX);
        qtest_writel(from, bases[i] + MCAN_CCCR,
                     MCAN_CCCR_INIT | MCAN_CCCR_CCE | MCAN_CCCR_TEST);
        qtest_writel(from, bases[i] + MCAN_TEST, UINT32_MAX);
        qtest_writel(from, bases[i] + MCAN_TOCC,
                     MCAN_TOCC_TOP(timeout[i]));
        qtest_writel(from, bases[i] + MCAN_TOCV, UINT32_MAX);
        qtest_writel(from, bases[i] + MCAN_TSU_TSCFG, tscfg[i]);
    }

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    for (i = 0; i < ARRAY_SIZE(bases); i++) {
        g_assert_cmphex(qtest_readl(to, bases[i] + MCAN_RWD), ==, rwd[i]);
        g_assert_cmphex(qtest_readl(to, bases[i] + MCAN_DBTP), ==,
                        0x009f1ff7);
        g_assert_cmphex(qtest_readl(to, bases[i] + MCAN_TEST), ==, 0x70);
        g_assert_cmphex(qtest_readl(to, bases[i] + MCAN_TOCV), ==,
                        timeout[i]);

        /* The restored value still observes the SAM destructive-read rule. */
        g_assert_cmphex(qtest_readl(to, bases[i] + MCAN_TSU_TSCFG), ==,
                        tscfg[i]);
        g_assert_cmphex(qtest_readl(to, bases[i] + MCAN_TSU_TSCFG), ==, 0);
    }

    qtest_quit(to);
    qtest_quit(from);
}

static void mcan_configure_tx_format_case(QTestState *qts, uint64_t base,
                                          uint32_t rx_offset,
                                          uint32_t txe_offset,
                                          uint32_t tx_offset,
                                          uint32_t features, uint32_t tss)
{
    uint32_t mode = MCAN_CCCR_TEST | MCAN_CCCR_MON | features;

    qtest_writel(qts, base + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE);
    qtest_writel(qts, base + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE | mode);
    qtest_writel(qts, base + MCAN_GFC, 0);
    qtest_writel(qts, base + MCAN_RXESC, 0);
    qtest_writel(qts, base + MCAN_RXF0C,
                 rx_offset | MCAN_RXF0C_SIZE(1));
    qtest_writel(qts, base + MCAN_TXESC, 0);
    qtest_writel(qts, base + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(1));
    qtest_writel(qts, base + MCAN_TXEFC,
                 txe_offset | MCAN_TXEFC_SIZE(1));
    qtest_writel(qts, base + MCAN_TSCC, tss);
    qtest_writel(qts, base + MCAN_TEST, MCAN_TEST_LBCK);
    qtest_writel(qts, base + MCAN_CCCR, mode);
}

static void mcan_set_timestamp_source(QTestState *qts, uint64_t base,
                                      uint32_t tss)
{
    uint32_t mode = qtest_readl(qts, base + MCAN_CCCR) &
                    (MCAN_CCCR_TEST | MCAN_CCCR_MON | MCAN_CCCR_FDOE |
                     MCAN_CCCR_BRSE);

    qtest_writel(qts, base + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE);
    qtest_writel(qts, base + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE | mode);
    qtest_writel(qts, base + MCAN_TSCC, tss);
    qtest_writel(qts, base + MCAN_CCCR, mode);
}

static void test_mcan_tx_flag_normalization_and_timestamps(void)
{
    static const struct {
        uint32_t features;
        uint32_t tss;
        bool remote;
        uint32_t effective_word0_flags;
        uint32_t effective_word1_flags;
        uint16_t timestamp;
        uint16_t visible_counter;
    } cases[] = {
        { 0, 1, false, 0, 0, 0, 1 },
        { MCAN_CCCR_FDOE, 0, false, MCAN_ELEMENT_ESI,
          MCAN_ELEMENT_FDF, 0, 0 },
        { MCAN_CCCR_FDOE | MCAN_CCCR_BRSE, 1, false,
          MCAN_ELEMENT_ESI, MCAN_ELEMENT_FDF | MCAN_ELEMENT_BRS, 1, 2 },
        { MCAN_CCCR_FDOE | MCAN_CCCR_BRSE, 3, true, 0, 0, 0, 0 },
    };
    const uint64_t base = SAM9X7_MCAN0_BASE;
    const uint32_t rx_offset = 0x3400;
    const uint32_t txe_offset = 0x4600;
    const uint32_t tx_offset = 0x4700;
    const uint32_t data[] = { 0x44332211, 0x88776655 };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    mcan_enable_clocks(qts, 29);
    for (i = 0; i < ARRAY_SIZE(cases); i++) {
        uint32_t id = (0x500U + i) << 18;
        uint32_t remote = 0;
        uint32_t guest_word0 = id | MCAN_ELEMENT_ESI | remote;
        uint32_t guest_word1 = MCAN_ELEMENT_EFC | MCAN_ELEMENT_MM(i) |
                               MCAN_ELEMENT_DLC(8) | MCAN_ELEMENT_FDF |
                               MCAN_ELEMENT_BRS;
        uint32_t effective_word0 = id | cases[i].effective_word0_flags |
                                   remote;
        uint32_t effective_word1 = cases[i].effective_word1_flags;

        if (cases[i].remote) {
            remote = MCAN_ELEMENT_RTR;
            guest_word0 |= remote;
            effective_word0 |= remote;
        }

        mcan_configure_tx_format_case(qts, base, rx_offset, txe_offset,
                                      tx_offset, cases[i].features,
                                      cases[i].tss);
        if (cases[i].tss == 0 || cases[i].tss == 3) {
            g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 0);
        }

        mcan_write_tx_element(qts, tx_offset, guest_word0, guest_word1,
                              data, ARRAY_SIZE(data));
        qtest_writel(qts, base + MCAN_TXBAR, BIT(0));
        mcan_wait_register(qts, base + MCAN_TXBTO, BIT(0), BIT(0));
        mcan_wait_register(qts, base + MCAN_RXF0S, 0x7f, 1);
        mcan_wait_register(qts, base + MCAN_TXEFS, 0x3f, 1);

        /* Effective bus/event flags must not rewrite the guest Tx element. */
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + tx_offset),
                        ==, guest_word0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + tx_offset + 4),
                        ==, guest_word1);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + tx_offset + 8),
                        ==, data[0]);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + tx_offset + 12),
                        ==, data[1]);

        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset),
                        ==, effective_word0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset + 4),
                        ==, BIT(31) | MCAN_ELEMENT_DLC(8) |
                            effective_word1 | cases[i].timestamp);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + txe_offset),
                        ==, effective_word0);
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + txe_offset + 4),
                        ==, MCAN_TX_EVENT_ET_TX | MCAN_ELEMENT_MM(i) |
                            MCAN_ELEMENT_DLC(8) | effective_word1 |
                            cases[i].timestamp);
        g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==,
                        cases[i].visible_counter);

        if (i == 1) {
            /* TSS=0 captures zero without advancing the internal count. */
            mcan_set_timestamp_source(qts, base, 1);
            g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 1);
        } else if (i == 3) {
            /* Reserved TSS=3 also captures zero without advancing it. */
            mcan_set_timestamp_source(qts, base, 1);
            g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 2);
        }
    }

    /* TSCV writes clear the hidden counter for TSS=0 and reserved TSS=3. */
    mcan_set_timestamp_source(qts, base, 0);
    qtest_writel(qts, base + MCAN_TSCV, UINT32_MAX);
    mcan_set_timestamp_source(qts, base, 1);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 0);
    qtest_writel(qts, base + MCAN_TXBAR, BIT(0));
    mcan_wait_register(qts, base + MCAN_TXBTO, BIT(0), BIT(0));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 1);

    mcan_set_timestamp_source(qts, base, 3);
    qtest_writel(qts, base + MCAN_TSCV, UINT32_MAX);
    mcan_set_timestamp_source(qts, base, 1);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 0);
    qtest_writel(qts, base + MCAN_TXBAR, BIT(0));
    mcan_wait_register(qts, base + MCAN_TXBTO, BIT(0), BIT(0));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 1);

    /* The external-source approximation alone ignores TSCV writes. */
    mcan_set_timestamp_source(qts, base, 2);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 1);
    qtest_writel(qts, base + MCAN_TSCV, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 1);
    mcan_set_timestamp_source(qts, base, 1);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 1);
    qtest_writel(qts, base + MCAN_TSCV, UINT32_MAX);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_TSCV), ==, 0);

    qtest_quit(qts);
}

static void test_mcan_registers_configuration_and_reset(void)
{
    static const uint64_t bases[] = {
        SAM9X7_MCAN0_BASE, SAM9X7_MCAN1_BASE,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(bases); i++) {
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_CREL), ==,
                        0x33000000);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_ENDN), ==,
                        0x87654321);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_DBTP), ==,
                        0x00000a33);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_CCCR), ==,
                        MCAN_CCCR_INIT);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_NBTP), ==,
                        0x06000a03);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_PSR), ==,
                        0x00000707);
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_TSU_TSS2), ==,
                        0x000a0000);

        qtest_writel(qts, bases[i] + MCAN_RXF0C,
                     0x3400 | MCAN_RXF0C_SIZE(4));
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_RXF0C), ==, 0);

        qtest_writel(qts, bases[i] + MCAN_CCCR,
                     MCAN_CCCR_INIT | MCAN_CCCR_CCE);
        qtest_writel(qts, bases[i] + MCAN_RXF0C,
                     0x3400 | MCAN_RXF0C_SIZE(4));
        g_assert_cmphex(qtest_readl(qts, bases[i] + MCAN_RXF0C), ==,
                        0x3400 | MCAN_RXF0C_SIZE(4));
    }

    qtest_writel(qts, SAM9X7_SRAM0_BASE + 0x3400, 0x9750cafe);
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_CCCR), ==,
                    MCAN_CCCR_INIT);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_RXF0C), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN1_BASE + MCAN_RXF0C), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + 0x3400), ==,
                    0x9750cafe);

    qtest_quit(qts);
}

static void test_mcan_internal_loopback_clocks_and_irqs(void)
{
    const uint32_t rx_offset = 0x3400;
    const uint32_t tx_offset = 0x4700;
    const uint32_t data[] = { 0x44332211, 0x88776655 };
    const uint32_t word0 = 0x123U << 18;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    aic_configure(qts, 29, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d001d);
    aic_configure(qts, 68, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0044);
    /* Keep the host-interface clock running while the core clock is gated. */
    pmc_write_pcr(qts, 29, PMC_PCR_EN);
    mcan_configure(qts, SAM9X7_MCAN0_BASE, rx_offset, 4,
                   tx_offset, 1, false, true);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_IE,
                 MCAN_IR_RF0N | MCAN_IR_TC);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_ILS, MCAN_IR_RF0N);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_ILE,
                 MCAN_ILE_EINT0 | MCAN_ILE_EINT1);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBTIE, BIT(0));
    mcan_write_tx_element(qts, tx_offset, word0, MCAN_ELEMENT_DLC(8),
                          data, ARRAY_SIZE(data));

    /* With cclk gated, the request remains pending and no IRQ is raised. */
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBRP), ==,
                    BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_IR), ==, 0);

    /* Enabling only cclk releases the already queued transmission. */
    mcan_enable_clocks(qts, 29);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBRP), ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBTO) &
                  BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_RXF0S) &
                    0x003f3f7f, ==, 0x00010001);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset), ==,
                    word0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset + 4), ==,
                    BIT(31) | MCAN_ELEMENT_DLC(8));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset + 8), ==,
                    data[0]);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset + 12), ==,
                    data[1]);

    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(29));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(4));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_IR, MCAN_IR_RF0N);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(4));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(29));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_IR, MCAN_IR_TC);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(29));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_RXF0A, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_RXF0S) &
                    0x7f, ==, 0);

    qtest_quit(qts);
}

static void test_mcan_linux_v33_tx_event_fifo(void)
{
    const uint32_t rx_offset = 0x3400;
    const uint32_t txe_offset = 0x4600;
    const uint32_t tx_offset = 0x4700;
    const uint32_t tx_element_size = 72;
    const uint32_t mode = MCAN_CCCR_TEST | MCAN_CCCR_MON;
    const uint32_t word0[] = {
        0x321U << 18,
        0x456U << 18,
    };
    const uint32_t word1[] = {
        MCAN_ELEMENT_EFC | MCAN_ELEMENT_MM(0) | MCAN_ELEMENT_DLC(8),
        MCAN_ELEMENT_EFC | MCAN_ELEMENT_MM(1) | MCAN_ELEMENT_DLC(4),
    };
    const uint32_t data0[] = { 0x44332211, 0x88776655 };
    const uint32_t data1[] = { 0xddccbbaa };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    aic_configure(qts, 29, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d001d);
    mcan_enable_clocks(qts, 29);

    /* Mirror the Linux v3.3 setup for the SAM9X7 mram-cfg partition. */
    qtest_memset(qts, SAM9X7_SRAM0_BASE + rx_offset, 0,
                 tx_offset + 32 * tx_element_size - rx_offset);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_RXESC, 0x777);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_GFC, 0);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(32));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXESC, 7);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXEFC,
                 txe_offset | MCAN_TXEFC_SIZE(32));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_RXF0C,
                 rx_offset | MCAN_RXF0C_SIZE(64));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE | mode);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TEST, MCAN_TEST_LBCK);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_IE, MCAN_IR_TEFN);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_ILS, 0);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TSCC, 0x000f0001);

    /* Linux clears CCE, enables INT0, reads TFQPI, then clears INIT. */
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_CCCR,
                 MCAN_CCCR_INIT | mode);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_ILE, MCAN_ILE_EINT0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXFQS), ==,
                    32);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_CCCR, mode);

    /* Linux puts the FIFO index in MM and requests a Tx event with EFC. */
    mcan_write_tx_element(qts, tx_offset, word0[0], word1[0],
                          data0, ARRAY_SIZE(data0));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(0));
    mcan_write_tx_element(qts, tx_offset + tx_element_size,
                          word0[1], word1[1], data1, ARRAY_SIZE(data1));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(1));

    g_assert_true(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_IR) &
                  MCAN_IR_TEFN);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(29));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXEFS), ==,
                    MCAN_TXEFS_STATUS(2, 0, 2));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + txe_offset), ==,
                    word0[0]);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + txe_offset + 4),
                    ==, MCAN_TX_EVENT_ET_TX | MCAN_ELEMENT_MM(0) |
                        MCAN_ELEMENT_DLC(8));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + txe_offset + 8),
                    ==, word0[1]);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + txe_offset + 12),
                    ==, MCAN_TX_EVENT_ET_TX | MCAN_ELEMENT_MM(1) |
                        MCAN_ELEMENT_DLC(4) | 1);

    /* The driver acknowledges IR before advancing the Tx Event FIFO. */
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_IR, MCAN_IR_TEFN);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(29));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXEFA, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXEFS), ==,
                    MCAN_TXEFS_STATUS(0, 2, 2));

    /* Asserting CCE resets handler status, while IR remains latched. */
    mcan_write_tx_element(qts, tx_offset + 2 * tx_element_size,
                          word0[0], word1[0] | MCAN_ELEMENT_MM(2),
                          data0, ARRAY_SIZE(data0));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(2));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXEFS), ==,
                    MCAN_TXEFS_STATUS(1, 2, 3));
    g_assert_true(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBTO) &
                  BIT(2));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_CCCR,
                 MCAN_CCCR_INIT | MCAN_CCCR_CCE);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXEFS), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXFQS), ==,
                    32);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBRP), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBTO), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBCF), ==,
                    0);
    g_assert_true(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_IR) &
                  MCAN_IR_TEFN);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(3));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBRP), ==,
                    0);

    qtest_quit(qts);
}

static void test_mcan_external_can_fd(void)
{
    const uint32_t rx_offset = 0x7800;
    const uint32_t tx_offset = 0x4700;
    uint32_t data[16];
    const uint32_t word0 = 0x5a5U << 18;
    QTestState *qts = qtest_init(
        "-object can-bus,id=canbus "
        "-machine sam9x75-curiosity,canbus0=canbus,canbus1=canbus");
    unsigned int i;

    aic_configure(qts, 29, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d001d);
    aic_configure(qts, 30, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d001e);
    aic_configure(qts, 69, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0045);
    mcan_enable_clocks(qts, 29);
    mcan_enable_clocks(qts, 30);

    mcan_configure(qts, SAM9X7_MCAN1_BASE, rx_offset, 4,
                   0, 0, true, false);
    qtest_writel(qts, SAM9X7_MCAN1_BASE + MCAN_IE, MCAN_IR_RF0N);
    qtest_writel(qts, SAM9X7_MCAN1_BASE + MCAN_ILE, MCAN_ILE_EINT0);
    mcan_configure(qts, SAM9X7_MCAN0_BASE, 0, 0,
                   tx_offset, 1, true, false);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_IE, MCAN_IR_TC);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_ILE, MCAN_ILE_EINT0);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBTIE, BIT(0));

    for (i = 0; i < ARRAY_SIZE(data); i++) {
        data[i] = 0x10203040U ^ (0x11111111U * i);
    }
    mcan_write_tx_element(qts, tx_offset, word0,
                          MCAN_ELEMENT_FDF | MCAN_ELEMENT_BRS |
                          MCAN_ELEMENT_DLC(15),
                          data, ARRAY_SIZE(data));
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(0));

    g_assert_true(qtest_readl(qts, SAM9X7_MCAN0_BASE + MCAN_TXBTO) &
                  BIT(0));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_MCAN1_BASE + MCAN_RXF0S) &
                    0x003f3f7f, ==, 0x00010001);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset), ==,
                    word0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset + 4), ==,
                    BIT(31) | MCAN_ELEMENT_FDF | MCAN_ELEMENT_BRS |
                    MCAN_ELEMENT_DLC(15));
    for (i = 0; i < ARRAY_SIZE(data); i++) {
        g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rx_offset + 8 +
                                    i * 4), ==, data[i]);
    }
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(29));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(30));

    /* Route a second MCAN1 receive notification through its INT1 line. */
    qtest_writel(qts, SAM9X7_MCAN1_BASE + MCAN_IR, MCAN_IR_RF0N);
    qtest_writel(qts, SAM9X7_MCAN1_BASE + MCAN_RXF0A, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(30));
    qtest_writel(qts, SAM9X7_MCAN1_BASE + MCAN_ILS, MCAN_IR_RF0N);
    qtest_writel(qts, SAM9X7_MCAN1_BASE + MCAN_ILE, MCAN_ILE_EINT1);
    qtest_writel(qts, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(0));
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(5));
    qtest_writel(qts, SAM9X7_MCAN1_BASE + MCAN_IR, MCAN_IR_RF0N);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(5));

    qtest_quit(qts);
}

static void test_mcan_acceptance_filters_and_gfc(void)
{
    const uint64_t base = SAM9X7_MCAN0_BASE;
    const uint32_t sid_offset = 0x2000;
    const uint32_t xid_offset = 0x2100;
    const uint32_t rxf0_offset = 0x3000;
    const uint32_t rxf1_offset = 0x3100;
    const uint32_t tx_offset = 0x4000;
    static const uint32_t standard_filters[] = {
        MCAN_STD_FILTER(0, MCAN_FILTER_FIFO0, 0x100, 0x10f),
        MCAN_STD_FILTER(1, MCAN_FILTER_FIFO1, 0x200, 0x201),
        MCAN_STD_FILTER(2, MCAN_FILTER_REJECT, 0x300, 0x7f0),
        MCAN_STD_FILTER(0, MCAN_FILTER_PRIO_FIFO0, 0x400, 0x40f),
        MCAN_STD_FILTER(3, MCAN_FILTER_FIFO0, 0x555, 0x555),
    };
    static const uint32_t extended_filters[][2] = {
        {
            MCAN_EXT_FILTER0(MCAN_FILTER_FIFO0, 0x100),
            MCAN_EXT_FILTER1(0, 0x10f),
        }, {
            MCAN_EXT_FILTER0(MCAN_FILTER_FIFO1, 0x222),
            MCAN_EXT_FILTER1(1, 0x333),
        }, {
            MCAN_EXT_FILTER0(MCAN_FILTER_REJECT, 0x400),
            MCAN_EXT_FILTER1(2, 0x700),
        }, {
            MCAN_EXT_FILTER0(MCAN_FILTER_PRIO_FIFO1, 0x1abc000),
            MCAN_EXT_FILTER1(3, 0x1abc00f),
        }, {
            MCAN_EXT_FILTER0(MCAN_FILTER_PRIORITY, 0x777),
            MCAN_EXT_FILTER1(1, 0x777),
        },
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    mcan_enable_clocks(qts, 29);
    for (i = 0; i < ARRAY_SIZE(standard_filters); i++) {
        qtest_writel(qts, SAM9X7_SRAM0_BASE + sid_offset + i * 4,
                     standard_filters[i]);
    }
    for (i = 0; i < ARRAY_SIZE(extended_filters); i++) {
        qtest_writel(qts, SAM9X7_SRAM0_BASE + xid_offset + i * 8,
                     extended_filters[i][0]);
        qtest_writel(qts, SAM9X7_SRAM0_BASE + xid_offset + i * 8 + 4,
                     extended_filters[i][1]);
    }

    mcan_begin_loopback_configuration(qts, base);
    qtest_writel(qts, base + MCAN_SIDFC,
                 sid_offset | MCAN_SIDFC_SIZE(ARRAY_SIZE(standard_filters)));
    qtest_writel(qts, base + MCAN_XIDFC,
                 xid_offset | MCAN_XIDFC_SIZE(ARRAY_SIZE(extended_filters)));
    qtest_writel(qts, base + MCAN_XIDAM, 0xfff);
    qtest_writel(qts, base + MCAN_GFC,
                 BIT(1) | (2U << 2) | (1U << 4));
    qtest_writel(qts, base + MCAN_RXESC, 0);
    qtest_writel(qts, base + MCAN_RXF0C,
                 rxf0_offset | MCAN_RXF0C_SIZE(8));
    qtest_writel(qts, base + MCAN_RXF1C,
                 rxf1_offset | MCAN_RXF1C_SIZE(8));
    qtest_writel(qts, base + MCAN_TXESC, 0);
    qtest_writel(qts, base + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(1));
    mcan_end_loopback_configuration(qts, base);

    mcan_loopback_frame(qts, base, tx_offset, 0x105, false, false,
                        0x11111111);
    mcan_loopback_frame(qts, base, tx_offset, 0x201, false, false,
                        0x22222222);
    mcan_loopback_frame(qts, base, tx_offset, 0x305, false, false,
                        0x33333333);
    /* SFT=3 disables element 4, so this follows GFC.ANFS to FIFO 1. */
    mcan_loopback_frame(qts, base, tx_offset, 0x555, false, false,
                        0x44444444);
    /* RRFS rejects the matching remote frame before the filter list. */
    mcan_loopback_frame(qts, base, tx_offset, 0x105, false, true, 0);
    mcan_loopback_frame(qts, base, tx_offset, 0x405, false, false,
                        0x55555555);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_HPMS), ==, 0x00000381);
    g_assert_true(qtest_readl(qts, base + MCAN_IR) & MCAN_IR_HPM);
    qtest_writel(qts, base + MCAN_IR, MCAN_IR_HPM);

    /* XIDAM maps this identifier into the range of extended filter 0. */
    mcan_loopback_frame(qts, base, tx_offset, 0x12340105, true, false,
                        0x66666666);
    mcan_loopback_frame(qts, base, tx_offset, 0x222, true, false,
                        0x77777777);
    mcan_loopback_frame(qts, base, tx_offset, 0x456, true, false,
                        0x88888888);
    /* EFT=3 bypasses XIDAM for this range. */
    mcan_loopback_frame(qts, base, tx_offset, 0x1abc005, true, false,
                        0x99999999);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_HPMS), ==, 0x000083c3);
    qtest_writel(qts, base + MCAN_IR, MCAN_IR_HPM);
    mcan_loopback_frame(qts, base, tx_offset, 0x777, true, false,
                        0xaaaaaaaa);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_HPMS), ==, 0x00008400);
    mcan_loopback_frame(qts, base, tx_offset, 0x888, true, false,
                        0xbbbbbbbb);

    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S) & 0x003f3f7f,
                    ==, MCAN_RXFS_STATUS(3, 0, 3));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S) & 0x003f3f7f,
                    ==, MCAN_RXFS_STATUS(4, 0, 4));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf0_offset + 4),
                    ==, MCAN_ELEMENT_DLC(4));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf0_offset +
                                16 + 4),
                    ==, (3U << 24) | MCAN_ELEMENT_DLC(4));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf1_offset + 4),
                    ==, (1U << 24) | MCAN_ELEMENT_DLC(4));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf1_offset +
                                16 + 4),
                    ==, BIT(31) | MCAN_ELEMENT_DLC(4));

    /* Exercise both accepting GFC actions, then both reject encodings. */
    mcan_set_gfc(qts, base, 1U << 2);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_HPMS), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S) & 0x003f3f7f,
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S) & 0x003f3f7f,
                    ==, 0);
    mcan_loopback_frame(qts, base, tx_offset, 0x600, false, false,
                        0xcccccccc);
    mcan_loopback_frame(qts, base, tx_offset, 0x888, true, false,
                        0xdddddddd);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S) & 0x7f, ==, 1);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S) & 0x7f, ==, 1);
    g_assert_true(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf0_offset + 4) &
                  BIT(31));
    g_assert_true(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf1_offset + 4) &
                  BIT(31));

    mcan_set_gfc(qts, base, (3U << 2) | (2U << 4));
    mcan_loopback_frame(qts, base, tx_offset, 0x601, false, false,
                        0xeeeeeeee);
    mcan_loopback_frame(qts, base, tx_offset, 0x889, true, false,
                        0xffffffff);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S) & 0x7f, ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S) & 0x7f, ==, 0);

    qtest_quit(qts);
}

static void test_mcan_dedicated_rx_buffers(void)
{
    const uint64_t base = SAM9X7_MCAN0_BASE;
    const uint32_t sid_offset = 0x2000;
    const uint32_t xid_offset = 0x2100;
    const uint32_t rxf1_offset = 0x3200;
    const uint32_t rxb_offset = 0x3400;
    const uint32_t tx_offset = 0x4000;
    const uint32_t element_size = 20;
    static const uint32_t standard_filters[] = {
        MCAN_STD_FILTER(0, MCAN_FILTER_RX_BUFFER, 0x123, 37),
        MCAN_STD_FILTER(0, MCAN_FILTER_RX_BUFFER, 0x123, 5),
        MCAN_STD_FILTER(1, MCAN_FILTER_FIFO1, 0x123, 0x123),
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    mcan_enable_clocks(qts, 29);
    for (i = 0; i < ARRAY_SIZE(standard_filters); i++) {
        qtest_writel(qts, SAM9X7_SRAM0_BASE + sid_offset + i * 4,
                     standard_filters[i]);
    }
    qtest_writel(qts, SAM9X7_SRAM0_BASE + xid_offset,
                 MCAN_EXT_FILTER0(MCAN_FILTER_RX_BUFFER, 0x12345));
    qtest_writel(qts, SAM9X7_SRAM0_BASE + xid_offset + 4,
                 MCAN_EXT_FILTER1(0, 63));

    mcan_begin_loopback_configuration(qts, base);
    qtest_writel(qts, base + MCAN_SIDFC,
                 sid_offset | MCAN_SIDFC_SIZE(ARRAY_SIZE(standard_filters)));
    qtest_writel(qts, base + MCAN_XIDFC,
                 xid_offset | MCAN_XIDFC_SIZE(1));
    qtest_writel(qts, base + MCAN_XIDAM, 0x1fffffff);
    qtest_writel(qts, base + MCAN_GFC, (2U << 2) | (2U << 4));
    qtest_writel(qts, base + MCAN_RXESC, 1U << 8);
    qtest_writel(qts, base + MCAN_RXBC, rxb_offset);
    qtest_writel(qts, base + MCAN_RXF1C,
                 rxf1_offset | MCAN_RXF1C_SIZE(4));
    qtest_writel(qts, base + MCAN_TXESC, 0);
    qtest_writel(qts, base + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(1));
    mcan_end_loopback_configuration(qts, base);

    mcan_loopback_frame(qts, base, tx_offset, 0x123, false, false,
                        0x11112222);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT2), ==, BIT(5));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxb_offset +
                                37 * element_size + 8),
                    ==, 0x11112222);
    g_assert_true(qtest_readl(qts, base + MCAN_IR) & MCAN_IR_DRX);

    /* A set NDAT locks that buffer, so filtering continues at element 1. */
    mcan_loopback_frame(qts, base, tx_offset, 0x123, false, false,
                        0x33334444);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT1), ==, BIT(5));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxb_offset +
                                5 * element_size + 8),
                    ==, 0x33334444);

    /* With both buffers locked, the following filter routes to FIFO 1. */
    mcan_loopback_frame(qts, base, tx_offset, 0x123, false, false,
                        0x55556666);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S) & 0x7f, ==, 1);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf1_offset + 4),
                    ==, (2U << 24) | MCAN_ELEMENT_DLC(4));

    qtest_writel(qts, base + MCAN_NDAT2, BIT(5));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT2), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT1), ==, BIT(5));
    mcan_loopback_frame(qts, base, tx_offset, 0x123, false, false,
                        0x77778888);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT2), ==, BIT(5));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxb_offset +
                                37 * element_size + 8),
                    ==, 0x77778888);

    qtest_writel(qts, base + MCAN_NDAT1, BIT(5));
    qtest_writel(qts, base + MCAN_NDAT2, BIT(5));
    mcan_loopback_frame(qts, base, tx_offset, 0x12345, true, false,
                        0x9999aaaa);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT2), ==, BIT(31));
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxb_offset +
                                63 * element_size),
                    ==, MCAN_ELEMENT_XTD | 0x12345);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxb_offset +
                                63 * element_size + 8),
                    ==, 0x9999aaaa);
    qtest_writel(qts, base + MCAN_NDAT2, 0);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT2), ==, BIT(31));
    qtest_writel(qts, base + MCAN_NDAT2, BIT(31));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT2), ==, 0);

    qtest_quit(qts);
}

static void test_mcan_rx_fifo_overflow_and_watermarks(void)
{
    const uint64_t base = SAM9X7_MCAN0_BASE;
    const uint32_t rxf0_offset = 0x3000;
    const uint32_t rxf1_offset = 0x3100;
    const uint32_t tx_offset = 0x4000;
    const uint32_t rxf0_ir = MCAN_IR_RF0N | MCAN_IR_RF0W |
                             MCAN_IR_RF0F | MCAN_IR_RF0L;
    const uint32_t rxf1_ir = MCAN_IR_RF1N | MCAN_IR_RF1W |
                             MCAN_IR_RF1F | MCAN_IR_RF1L;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    mcan_enable_clocks(qts, 29);
    mcan_begin_loopback_configuration(qts, base);
    qtest_writel(qts, base + MCAN_GFC, 1U << 2);
    qtest_writel(qts, base + MCAN_RXESC, 0);
    qtest_writel(qts, base + MCAN_RXF0C,
                 rxf0_offset | MCAN_RXF0C_SIZE(2) |
                 MCAN_RXFC_WATERMARK(1));
    qtest_writel(qts, base + MCAN_RXF1C,
                 rxf1_offset | MCAN_RXF1C_SIZE(2) |
                 MCAN_RXFC_WATERMARK(2) | MCAN_RXFC_OVERWRITE);
    qtest_writel(qts, base + MCAN_TXESC, 0);
    qtest_writel(qts, base + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(1));
    mcan_end_loopback_configuration(qts, base);

    mcan_loopback_frame(qts, base, tx_offset, 0x101, false, false,
                        0x11111111);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(1, 0, 1));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_IR) & rxf0_ir, ==,
                    MCAN_IR_RF0N | MCAN_IR_RF0W);
    qtest_writel(qts, base + MCAN_IR, rxf0_ir);

    mcan_loopback_frame(qts, base, tx_offset, 0x102, false, false,
                        0x22222222);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(2, 0, 0) | BIT(24));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_IR) & rxf0_ir, ==,
                    MCAN_IR_RF0N | MCAN_IR_RF0F);
    qtest_writel(qts, base + MCAN_IR, rxf0_ir);

    mcan_loopback_frame(qts, base, tx_offset, 0x103, false, false,
                        0x33333333);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(2, 0, 0) | BIT(24) | BIT(25));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_IR) & rxf0_ir, ==,
                    MCAN_IR_RF0L);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf0_offset + 8),
                    ==, 0x11111111);
    qtest_writel(qts, base + MCAN_IR, MCAN_IR_RF0L);
    qtest_writel(qts, base + MCAN_RXF0A, 0);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(1, 1, 0));

    mcan_loopback_frame(qts, base, tx_offset, 0x201, true, false,
                        0x44444444);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_IR) & rxf1_ir, ==,
                    MCAN_IR_RF1N);
    qtest_writel(qts, base + MCAN_IR, rxf1_ir);
    mcan_loopback_frame(qts, base, tx_offset, 0x202, true, false,
                        0x55555555);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S), ==,
                    MCAN_RXFS_STATUS(2, 0, 0) | BIT(24));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_IR) & rxf1_ir, ==,
                    MCAN_IR_RF1N | MCAN_IR_RF1W | MCAN_IR_RF1F);
    qtest_writel(qts, base + MCAN_IR, rxf1_ir);

    mcan_loopback_frame(qts, base, tx_offset, 0x203, true, false,
                        0x66666666);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S), ==,
                    MCAN_RXFS_STATUS(2, 1, 1) | BIT(24));
    g_assert_cmphex(qtest_readl(qts, base + MCAN_IR) & rxf1_ir, ==,
                    MCAN_IR_RF1N);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf1_offset), ==,
                    MCAN_ELEMENT_XTD | 0x203);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + rxf1_offset +
                                16),
                    ==, MCAN_ELEMENT_XTD | 0x202);
    qtest_writel(qts, base + MCAN_RXF1A, 1);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF1S), ==,
                    MCAN_RXFS_STATUS(1, 0, 1));

    qtest_quit(qts);
}

static void test_mcan_rx_mram_bounds(void)
{
    const uint64_t base = SAM9X7_MCAN0_BASE;
    const uint32_t sid_offset = 0x2000;
    const uint32_t tx_offset = 0x4000;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    mcan_enable_clocks(qts, 29);
    qtest_writel(qts, SAM9X7_SRAM0_BASE + sid_offset,
                 MCAN_STD_FILTER(1, MCAN_FILTER_FIFO0, 0x123, 0x123));
    qtest_writel(qts, SAM9X7_SRAM0_BASE + 0xfffc, 0xa5a5a5a5);
    mcan_begin_loopback_configuration(qts, base);
    qtest_writel(qts, base + MCAN_SIDFC,
                 sid_offset | MCAN_SIDFC_SIZE(1));
    qtest_writel(qts, base + MCAN_RXF0C,
                 0xfffc | MCAN_RXF0C_SIZE(1));
    qtest_writel(qts, base + MCAN_TXESC, 0);
    qtest_writel(qts, base + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(1));
    mcan_end_loopback_configuration(qts, base);
    mcan_loopback_frame(qts, base, tx_offset, 0x123, false, false,
                        0x11112222);
    g_assert_true(qtest_readl(qts, base + MCAN_IR) & MCAN_IR_MRAF);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_RXF0S), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_SRAM0_BASE + 0xfffc), ==,
                    0xa5a5a5a5);
    qtest_writel(qts, base + MCAN_IR, MCAN_IR_MRAF);

    /* An extended element needs eight bytes, so 0xfffc is out of range. */
    mcan_begin_loopback_configuration(qts, base);
    qtest_writel(qts, base + MCAN_SIDFC, 0);
    qtest_writel(qts, base + MCAN_XIDFC,
                 0xfffc | MCAN_XIDFC_SIZE(1));
    mcan_end_loopback_configuration(qts, base);
    mcan_loopback_frame(qts, base, tx_offset, 0x123, true, false,
                        0x33334444);
    g_assert_true(qtest_readl(qts, base + MCAN_IR) & MCAN_IR_MRAF);
    qtest_writel(qts, base + MCAN_IR, MCAN_IR_MRAF);

    /* A failed dedicated-buffer write must not set its NDAT flag. */
    qtest_writel(qts, SAM9X7_SRAM0_BASE + sid_offset,
                 MCAN_STD_FILTER(0, MCAN_FILTER_RX_BUFFER, 0x456, 0));
    mcan_begin_loopback_configuration(qts, base);
    qtest_writel(qts, base + MCAN_SIDFC,
                 sid_offset | MCAN_SIDFC_SIZE(1));
    qtest_writel(qts, base + MCAN_XIDFC, 0);
    qtest_writel(qts, base + MCAN_RXBC, 0xfffc);
    mcan_end_loopback_configuration(qts, base);
    mcan_loopback_frame(qts, base, tx_offset, 0x456, false, false,
                        0x55556666);
    g_assert_true(qtest_readl(qts, base + MCAN_IR) & MCAN_IR_MRAF);
    g_assert_cmphex(qtest_readl(qts, base + MCAN_NDAT1), ==, 0);

    qtest_quit(qts);
}

static void test_mcan_wrapped_rx_migration(void)
{
    const uint64_t base = SAM9X7_MCAN0_BASE;
    const uint32_t rxf0_offset = 0x3000;
    const uint32_t rxf1_offset = 0x3100;
    const uint32_t tx_offset = 0x4000;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    mcan_enable_clocks(from, 29);
    mcan_begin_loopback_configuration(from, base);
    qtest_writel(from, base + MCAN_GFC, 1U << 2);
    qtest_writel(from, base + MCAN_RXESC, 0);
    qtest_writel(from, base + MCAN_RXF0C,
                 rxf0_offset | MCAN_RXF0C_SIZE(3));
    qtest_writel(from, base + MCAN_RXF1C,
                 rxf1_offset | MCAN_RXF1C_SIZE(2) |
                 MCAN_RXFC_OVERWRITE);
    qtest_writel(from, base + MCAN_TXESC, 0);
    qtest_writel(from, base + MCAN_TXBC,
                 tx_offset | MCAN_TXBC_FIFO_SIZE(1));
    mcan_end_loopback_configuration(from, base);

    mcan_loopback_frame(from, base, tx_offset, 0x101, false, false,
                        0x11111111);
    mcan_loopback_frame(from, base, tx_offset, 0x102, false, false,
                        0x22222222);
    mcan_loopback_frame(from, base, tx_offset, 0x103, false, false,
                        0x33333333);
    qtest_writel(from, base + MCAN_RXF0A, 0);
    mcan_loopback_frame(from, base, tx_offset, 0x104, false, false,
                        0x44444444);
    mcan_loopback_frame(from, base, tx_offset, 0x201, true, false,
                        0x55555555);
    mcan_loopback_frame(from, base, tx_offset, 0x202, true, false,
                        0x66666666);
    mcan_loopback_frame(from, base, tx_offset, 0x203, true, false,
                        0x77777777);

    g_assert_cmphex(qtest_readl(from, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(3, 1, 1) | BIT(24));
    g_assert_cmphex(qtest_readl(from, base + MCAN_RXF1S), ==,
                    MCAN_RXFS_STATUS(2, 1, 1) | BIT(24));

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmphex(qtest_readl(to, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(3, 1, 1) | BIT(24));
    g_assert_cmphex(qtest_readl(to, base + MCAN_RXF1S), ==,
                    MCAN_RXFS_STATUS(2, 1, 1) | BIT(24));
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM0_BASE + rxf0_offset + 8),
                    ==, 0x44444444);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM0_BASE + rxf0_offset +
                                16 + 8),
                    ==, 0x22222222);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM0_BASE + rxf0_offset +
                                2 * 16 + 8),
                    ==, 0x33333333);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM0_BASE + rxf1_offset + 8),
                    ==, 0x77777777);
    g_assert_cmphex(qtest_readl(to, SAM9X7_SRAM0_BASE + rxf1_offset +
                                16 + 8),
                    ==, 0x66666666);

    qtest_writel(to, base + MCAN_RXF0A, 2);
    g_assert_cmphex(qtest_readl(to, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(1, 0, 1));
    qtest_writel(to, base + MCAN_RXF0A, 0);
    g_assert_cmphex(qtest_readl(to, base + MCAN_RXF0S), ==,
                    MCAN_RXFS_STATUS(0, 1, 1));
    qtest_writel(to, base + MCAN_RXF1A, 1);
    g_assert_cmphex(qtest_readl(to, base + MCAN_RXF1S), ==,
                    MCAN_RXFS_STATUS(1, 0, 1));
    qtest_writel(to, base + MCAN_RXF1A, 0);
    g_assert_cmphex(qtest_readl(to, base + MCAN_RXF1S), ==,
                    MCAN_RXFS_STATUS(0, 1, 1));

    qtest_quit(to);
    qtest_quit(from);
}

static void test_mcan_mram_error_and_migration(void)
{
    const uint32_t invalid_tx_offset = 0xfffc;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");

    aic_configure(from, 68, AIC_SMR_LEVEL_HIGH | 5, 0x5d5d0044);
    mcan_enable_clocks(from, 29);
    mcan_configure(from, SAM9X7_MCAN0_BASE, 0x3400, 4,
                   invalid_tx_offset, 1, true, true);
    qtest_writel(from, SAM9X7_MCAN0_BASE + MCAN_IE, MCAN_IR_MRAF);
    qtest_writel(from, SAM9X7_MCAN0_BASE + MCAN_ILS, MCAN_IR_MRAF);
    qtest_writel(from, SAM9X7_MCAN0_BASE + MCAN_ILE, MCAN_ILE_EINT1);
    qtest_writel(from, SAM9X7_MCAN0_BASE + MCAN_TXBAR, BIT(0));
    g_assert_true(qtest_readl(from, SAM9X7_MCAN0_BASE + MCAN_IR) &
                  MCAN_IR_MRAF);
    g_assert_true(qtest_readl(from, SAM9X7_MCAN0_BASE + MCAN_TXBRP) &
                  BIT(0));
    g_assert_true(qtest_readl(from, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(4));

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_true(qtest_readl(to, SAM9X7_MCAN0_BASE + MCAN_IR) &
                  MCAN_IR_MRAF);
    g_assert_true(qtest_readl(to, SAM9X7_MCAN0_BASE + MCAN_TXBRP) & BIT(0));
    g_assert_true(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(4));
    qtest_writel(to, SAM9X7_MCAN0_BASE + MCAN_IR, MCAN_IR_MRAF);
    g_assert_false(qtest_readl(to, SAM9X7_AIC_BASE + AIC_IPR2) & BIT(4));

    qtest_quit(to);
    qtest_quit(from);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("sam9x75/memory-and-identification",
                   test_memory_and_identification);
    qtest_add_func("sam9x75/matrix/registers-and-protection",
                   test_matrix_registers_and_protection);
    qtest_add_func("sam9x75/matrix/boot-remap", test_matrix_boot_remap);
    qtest_add_func("sam9x75/matrix/remap-migration",
                   test_matrix_remap_migration);
    qtest_add_func("sam9x75/rom/supplied-image", test_rom_image_loading);
    qtest_add_func("sam9x75/rom/cpu-entry", test_rom_cpu_entry);
    qtest_add_func("sam9x75/boot/direct-linux-ddr-assignment",
                   test_direct_linux_ddr_assignment);
    qtest_add_func("sam9x75/boot/firmware-elf-true-reset-assignment",
                   test_firmware_elf_true_reset_assignment);
    qtest_add_func("sam9x75/rom/image-size-validation",
                   test_rom_image_size_validation);
    qtest_add_func("sam9x75/rom/boot-path-validation",
                   test_rom_boot_path_validation);
    qtest_add_func("sam9x75/dbgu/registers-and-loopback",
                   test_dbgu_registers_and_loopback);
    qtest_add_func("sam9x75/dbgu/chardev", test_dbgu_chardev);
    qtest_add_func("sam9x75/dbgu/xdmac-requests",
                   test_dbgu_xdmac_requests);
    qtest_add_func("sam9x75/i2smcc/registers-irq-and-protection",
                   test_i2smcc_registers_irq_and_protection);
    qtest_add_func("sam9x75/i2smcc/loopback-timing-and-xdmac",
                   test_i2smcc_loopback_timing_and_xdmac);
    qtest_add_func("sam9x75/i2smcc/tdm-compact-and-mono",
                   test_i2smcc_tdm_compact_and_mono);
    qtest_add_func("sam9x75/classd/registers-and-protection",
                   test_classd_registers_and_protection);
    qtest_add_func("sam9x75/classd/linux-regcache-resume",
                   test_classd_linux_regcache_resume);
    qtest_add_func("sam9x75/classd/timing-irq-and-xdmac",
                   test_classd_timing_irq_and_xdmac);
    qtest_add_func("sam9x75/aic/dbgu-integration",
                   test_aic_dbgu_integration);
    qtest_add_func("sam9x75/aic/priority-and-nesting",
                   test_aic_priority_and_nesting);
    qtest_add_func("sam9x75/aic/fiq-mask-and-write-protection",
                   test_aic_fiq_mask_and_write_protection);
    qtest_add_func("sam9x75/gem/registers-mdio-dma-and-irqs",
                   test_gem_registers_mdio_dma_and_irqs);
    qtest_add_func("sam9x75/gem/statistics-generated-and-clear",
                   test_gem_statistics_generated_and_clear);
    qtest_add_func("sam9x75/gem/statistics-test-controls",
                   test_gem_statistics_test_controls);
    qtest_add_func("sam9x75/gem/statistics-migration",
                   test_gem_statistics_migration);
    qtest_add_func("sam9x75/board/ethernet-clock-jumper",
                   test_board_ethernet_clock_jumper);
    qtest_add_func("sam9x75/flexcom-usart/registers-irq-and-protection",
                   test_flexcom_usart_registers_irq_and_protection);
    qtest_add_func("sam9x75/flexcom-usart/fifo-loopback-and-timeout",
                   test_flexcom_usart_fifo_loopback_and_timeout);
    qtest_add_func("sam9x75/flexcom-usart/chardev",
                   test_flexcom_usart_chardev);
    qtest_add_func("sam9x75/flexcom-usart/migration",
                   test_flexcom_usart_migration);
    qtest_add_func("sam9x75/flexcom-usart/xdmac-and-mode-gating",
                   test_flexcom_usart_xdmac_requests_and_mode_gating);
    qtest_add_func("sam9x75/flexcom-spi/registers-irq-and-protection",
                   test_flexcom_spi_registers_irq_and_protection);
    qtest_add_func("sam9x75/flexcom-spi/instance-capabilities",
                   test_flexcom_spi_instance_capabilities);
    qtest_add_func("sam9x75/flexcom-spi/fifo-loopback-and-timing",
                   test_flexcom_spi_fifo_loopback_and_timing);
    qtest_add_func("sam9x75/flexcom-spi/migration",
                   test_flexcom_spi_migration);
    qtest_add_func("sam9x75/flexcom-spi/xdmac-and-mode-gating",
                   test_flexcom_spi_xdmac_requests_and_mode_gating);
    qtest_add_func("sam9x75/flexcom-twi/registers-nack-and-protection",
                   test_flexcom_twi_registers_nack_and_protection);
    qtest_add_func("sam9x75/flexcom-twi/eeprom-fifo-and-access-width",
                   test_twi_eeprom_transfers_fifo_and_access_width);
    qtest_add_func("sam9x75/flexcom-twi/fifo-levels-thresholds-and-clock",
                   test_twi_fifo_levels_thresholds_and_clock);
    qtest_add_func("sam9x75/flexcom-twi/migration",
                   test_flexcom_twi_migration);
    qtest_add_func("sam9x75/flexcom-twi/xdmac-and-mode-gating",
                   test_flexcom_twi_xdmac_requests_and_mode_gating);
    qtest_add_func("sam9x75/board/mcp16502-registers-and-regulators",
                   test_mcp16502_registers_and_regulators);
    qtest_add_func("sam9x75/board/rgb-led-and-user-button",
                   test_board_rgb_led_and_user_button);
    qtest_add_func("sam9x75/board/wakeup-start-reset-and-pmic-modes",
                   test_board_wakeup_start_reset_and_pmic_modes);
    qtest_add_func("sam9x75/board/power-reset-domains",
                   test_board_power_reset_domains);
    qtest_add_func("sam9x75/board/mcp16502-push-button-timeouts",
                   test_mcp16502_push_button_timeouts);
    qtest_add_func("sam9x75/board/mcp16502-push-button-shutdown-request",
                   test_mcp16502_push_button_shutdown_request);
    qtest_add_func("sam9x75/board/mcp16502-startup-sequence",
                   test_mcp16502_startup_sequence);
    qtest_add_func("sam9x75/board/pac1934-register-protocol-and-wiring",
                   test_pac1934_register_protocol_and_board_wiring);
    qtest_add_func("sam9x75/board/pac1934-measurements-and-modes",
                   test_pac1934_measurements_accumulation_and_modes);
    qtest_add_func("sam9x75/board/pac1934-overflow-alert",
                   test_pac1934_overflow_alert_and_clear);
    qtest_add_func("sam9x75/board/pac1934-i2c-jumpers",
                   test_pac1934_i2c_jumpers);
    qtest_add_func("sam9x75/pmc/clock-tree-and-protection",
                   test_pmc_clock_tree_and_protection);
    qtest_add_func("sam9x75/pio/reset-gpio-mux-and-protection",
                   test_pio_reset_gpio_mux_and_protection);
    qtest_add_func("sam9x75/pio/interrupt-filter-and-clock-gating",
                   test_pio_interrupt_filter_and_clock_gating);
    qtest_add_func("sam9x75/pit64b/timing-gating-and-irq",
                   test_pit64b_timing_gating_and_irq);
    qtest_add_func("sam9x75/tcb/clocksource-clockevent-and-protection",
                   test_tcb_clocksource_clockevent_and_protection);
    qtest_add_func("sam9x75/tcb1/reset-masks-and-independence",
                   test_tcb1_reset_masks_and_independence);
    qtest_add_func("sam9x75/tcb1/clock-gating-and-irq",
                   test_tcb1_clock_gating_and_irq);
    qtest_add_func("sam9x75/tcb1/active-timer-migration",
                   test_tcb1_active_timer_migration);
    qtest_add_func("sam9x75/xdmac/registers-memcpy-and-descriptors",
                   test_xdmac_registers_memcpy_and_descriptors);
    qtest_add_func("sam9x75/xdmac/pacing-striding-and-errors",
                   test_xdmac_pacing_striding_and_errors);
    qtest_add_func("sam9x75/xdmac/fair-scheduling-and-flush-scope",
                   test_xdmac_fair_scheduling_and_flush_scope);
    qtest_add_func("sam9x75/xdmac/p2m-fifo-suspend-flush-and-disable",
                   test_xdmac_p2m_fifo_suspend_flush_and_disable);
    qtest_add_func("sam9x75/xdmac/p2m-flush-nonblocking-disable-order",
                   test_xdmac_p2m_flush_nonblocking_disable_order);
    qtest_add_func("sam9x75/xdmac/p2m-fifo-migration",
                   test_xdmac_p2m_fifo_migration);
    qtest_add_func("sam9x75/xdmac/flexcom-live-migration",
                   test_xdmac_flexcom_live_migration);
    qtest_add_func("sam9x75/aes/registers-ecb-irq-and-protection",
                   test_aes_registers_ecb_irq_and_protection);
    qtest_add_func("sam9x75/aes/chaining-gcm-and-xdmac",
                   test_aes_chaining_gcm_and_xdmac);
    qtest_add_func("sam9x75/aes/feedback-and-xts",
                   test_aes_feedback_and_xts);
    qtest_add_func("sam9x75/tdes/vectors-timing-irq-and-protection",
                   test_tdes_vectors_timing_irq_and_protection);
    qtest_add_func("sam9x75/tdes/chaining-feedback-and-xtea",
                   test_tdes_chaining_feedback_and_xtea);
    qtest_add_func("sam9x75/tdes/xdmac-and-last-output",
                   test_tdes_xdmac_and_last_output);
    qtest_add_func("sam9x75/sha/vectors-timing-irq-and-protection",
                   test_sha_vectors_timing_irq_and_protection);
    qtest_add_func("sam9x75/sha/hmac-check-and-manual-padding",
                   test_sha_hmac_check_and_manual_padding);
    qtest_add_func("sam9x75/sha/xdmac-auto-padding",
                   test_sha_xdmac_auto_padding);
    qtest_add_func("sam9x75/trng/timing-irq-and-protection",
                   test_trng_timing_irq_and_protection);
    qtest_add_func("sam9x75/system/slowclock-pit-reset-and-protection",
                   test_system_slowclock_pit_reset_and_protection);
    qtest_add_func("sam9x75/rstc/a1-general-reset-reporting",
                   test_rstc_a1_general_reset_reporting);
    qtest_add_func("sam9x75/rtt/count-alarm-modulo-and-protection",
                   test_rtt_count_alarm_modulo_and_protection);
    qtest_add_func("sam9x75/shdwc/registers-shutdown-and-pin-wake",
                   test_shdwc_registers_shutdown_and_pin_wake);
    qtest_add_func("sam9x75/shdwc/guest-shutdown",
                   test_shdwc_guest_shutdown);
    qtest_add_func("sam9x75/shdwc/rtc-alarm-wake",
                   test_shdwc_rtc_alarm_wake);
    qtest_add_func("sam9x75/shdwc/rtt-alarm-wake",
                   test_shdwc_rtt_alarm_wake);
    qtest_add_func("sam9x75/bsc/key-retention-and-factory-reset",
                   test_bsc_key_retention_and_factory_reset);
    qtest_add_func("sam9x75/bsc/migration", test_bsc_migration);
    qtest_add_func("sam9x75/otpc/registers-protection-and-irq",
                   test_otpc_registers_protection_and_irq);
    qtest_add_func("sam9x75/otpc/program-update-invalidate-and-hide",
                   test_otpc_program_update_invalidate_and_hide);
    qtest_add_func("sam9x75/otpc/program-validation",
                   test_otpc_program_validation);
    qtest_add_func("sam9x75/otpc/uhc-program-invalidation-gates",
                   test_otpc_uhc_program_invalidation_gates);
    qtest_add_func("sam9x75/otpc/physical-backend-persistence",
                   test_otpc_physical_backend_persistence);
    qtest_add_func("sam9x75/otpc/physical-backend-fault-latch",
                   test_otpc_physical_backend_fault_latch);
    qtest_add_func("sam9x75/otpc/backend-validation",
                   test_otpc_backend_validation);
    qtest_add_func("sam9x75/otpc/identification-and-reserved-offsets",
                   test_otpc_identification_and_reserved_offsets);
    qtest_add_func("sam9x75/otpc/identification-migration",
                   test_otpc_identification_migration);
    qtest_add_func("sam9x75/otpc/emulation-scan-and-read",
                   test_otpc_emulation_scan_and_read);
    qtest_add_func("sam9x75/otpc/corruption-bounds-and-reset",
                   test_otpc_corruption_bounds_and_reset);
    qtest_add_func("sam9x75/otpc/migration", test_otpc_migration);
    qtest_add_func("sam9x75/otpc/migration-v2-state",
                   test_otpc_migration_v2_state);
    qtest_add_func("sam9x75/gpbr/protection-and-retention",
                   test_gpbr_protection_and_retention);
    qtest_add_func("sam9x75/gpbr/tamper-clear",
                   test_gpbr_tamper_clear);
    qtest_add_func("sam9x75/rtc/calendar-alarm-irq-and-protection",
                   test_rtc_calendar_alarm_irq_and_protection);
    qtest_add_func("sam9x75/rtc/utc-tamper-and-lock",
                   test_rtc_utc_tamper_and_lock);
    qtest_add_func("sam9x75/sfr/registers-resume-and-protection",
                   test_sfr_registers_resume_and_protection);
    qtest_add_func("sam9x75/udphs/registers-endpoints-clocks-and-irq",
                   test_udphs_registers_endpoints_clocks_and_irq);
    qtest_add_func("sam9x75/udphs/shared-port-a-mux",
                   test_udphs_shared_port_a_mux);
    qtest_add_func("sam9x75/udphs/control-setup-interlock",
                   test_udphs_control_setup_interlock);
    qtest_add_func("sam9x75/udphs/iso-error-paths",
                   test_udphs_iso_error_paths);
    qtest_add_func("sam9x75/udphs/high-bandwidth-iso-zlp",
                   test_udphs_high_bandwidth_iso_zlp);
    qtest_add_func("sam9x75/udphs/raw-ep0-and-pio-data-tokens",
                   test_udphs_raw_ep0_and_pio_data_tokens);
    qtest_add_func("sam9x75/udphs/intdis-dma-gating",
                   test_udphs_intdis_dma_gating);
    qtest_add_func("sam9x75/udphs/dma-data-tokens",
                   test_udphs_dma_data_tokens);
    qtest_add_func("sam9x75/udphs/migration", test_udphs_migration);
    qtest_add_func("sam9x75/uhphs/registers-reset-and-companions",
                   test_uhphs_registers_reset_and_companions);
    qtest_add_func("sam9x75/uhphs/hotplug-shared-irq",
                   test_uhphs_hotplug_shared_irq);
    qtest_add_func("sam9x75/uhphs/power-connection-lifecycle",
                   test_uhphs_power_connection_lifecycle);
    qtest_add_func("sam9x75/uhphs/pmc-clock-gating",
                   test_uhphs_pmc_clock_gating);
    qtest_add_func("sam9x75/uhphs/ehci-frame-list-size",
                   test_uhphs_ehci_frame_list_size);
    qtest_add_func("sam9x75/uhphs/ehci-dma-error",
                   test_uhphs_ehci_dma_error);
    qtest_add_func("sam9x75/uhphs/gated-async-migration",
                   test_uhphs_gated_async_migration);
    qtest_add_func("sam9x75/uhphs/frame-phase-migration",
                   test_uhphs_frame_phase_migration);
    qtest_add_func("sam9x75/uhphs/ohci-active-async-migration",
                   test_uhphs_ohci_active_async_migration);
    qtest_add_func("sam9x75/uhphs/migration", test_uhphs_migration);
    qtest_add_func("sam9x75/mcan/sam-register-semantics",
                   test_mcan_sam_register_semantics);
    qtest_add_func("sam9x75/mcan/sam-register-migration",
                   test_mcan_sam_register_migration);
    qtest_add_func("sam9x75/mcan/tx-flag-normalization-and-timestamps",
                   test_mcan_tx_flag_normalization_and_timestamps);
    qtest_add_func("sam9x75/mcan/registers-configuration-and-reset",
                   test_mcan_registers_configuration_and_reset);
    qtest_add_func("sam9x75/mcan/internal-loopback-clocks-and-irqs",
                   test_mcan_internal_loopback_clocks_and_irqs);
    qtest_add_func("sam9x75/mcan/linux-v33-tx-event-fifo",
                   test_mcan_linux_v33_tx_event_fifo);
    qtest_add_func("sam9x75/mcan/external-can-fd",
                   test_mcan_external_can_fd);
    qtest_add_func("sam9x75/mcan/acceptance-filters-and-gfc",
                   test_mcan_acceptance_filters_and_gfc);
    qtest_add_func("sam9x75/mcan/dedicated-rx-buffers",
                   test_mcan_dedicated_rx_buffers);
    qtest_add_func("sam9x75/mcan/rx-fifo-overflow-and-watermarks",
                   test_mcan_rx_fifo_overflow_and_watermarks);
    qtest_add_func("sam9x75/mcan/rx-mram-bounds",
                   test_mcan_rx_mram_bounds);
    qtest_add_func("sam9x75/mcan/wrapped-rx-migration",
                   test_mcan_wrapped_rx_migration);
    qtest_add_func("sam9x75/mcan/mram-error-and-migration",
                   test_mcan_mram_error_and_migration);
    qtest_add_func("sam9x75/mpddrc/registers-errors-and-irq",
                   test_mpddrc_registers_errors_and_irq);
    qtest_add_func("sam9x75/wdt/reset-disable-and-lock",
                   test_wdt_reset_disable_and_lock);
    qtest_add_func("sam9x75/wdt/events-and-system-irq",
                   test_wdt_events_and_system_irq);
    qtest_add_func("sam9x75/sdhci/adma2-linux-nop-terminator",
                   test_sdhci_adma2_linux_nop_terminator);
    qtest_add_func("sam9x75/sdhci/adma2-end-int-success",
                   test_sdhci_adma2_end_int_success);
    qtest_add_func("sam9x75/sdhci/adma2-block-count-mismatch",
                   test_sdhci_adma2_block_count_mismatch);
    qtest_add_func("sam9x75/sdhci/adma2-overlong-table-mismatch",
                   test_sdhci_adma2_overlong_table_mismatch);
    qtest_add_func("sam9x75/sdhci/adma2-partial-end-nop-mismatch",
                   test_sdhci_adma2_partial_end_nop_mismatch);
    qtest_add_func("sam9x75/sdhci/adma2-descriptor-tail-fault",
                   test_sdhci_adma2_descriptor_tail_fault);
    qtest_add_func("sam9x75/sdhci/adma2-mmio-descriptor-fault",
                   test_sdhci_adma2_mmio_descriptor_fault);
    qtest_add_func("sam9x75/sdhci/adma2-read-data-fault",
                   test_sdhci_adma2_read_data_fault);
    qtest_add_func("sam9x75/sdhci/adma2-write-data-fault",
                   test_sdhci_adma2_write_data_fault);
    qtest_add_func("sam9x75/sdhci/preset-registers",
                   test_sdhci_preset_registers);
    qtest_add_func("sam9x75/sdhci/host-control2-migration",
                   test_sdhci_host_control2_migration);
    qtest_add_func("sam9x75/sdhci/software-reset-all",
                   test_sdhci_software_reset_all);
    qtest_add_func("sam9x75/sdhci/software-reset-command-irq",
                   test_sdhci_software_reset_command_irq);
    qtest_add_func("sam9x75/board/m2-interface-jumper",
                   test_board_m2_interface_jumper);
    qtest_add_func("sam9x75/ebi/chip-select-assignments",
                   test_ebi_chip_select_assignments);
    qtest_add_func("sam9x75/ebi/assignment-migration-reset",
                   test_ebi_assignment_migration_reset);
    qtest_add_func("sam9x75/ebi/assignment-migration-deassert",
                   test_ebi_assignment_migration_deassert);
    qtest_add_func("sam9x75/nand/identification-program-and-erase",
                   test_nand_identification_program_and_erase);
    qtest_add_func("sam9x75/nand/cs2-mirroring",
                   test_nand_cs2_mirroring);
    qtest_add_func("sam9x75/nand/features-and-reset-domains",
                   test_nand_features_and_reset_domains);
    qtest_add_func("sam9x75/nand/parameter-status-poll-migration",
                   test_nand_parameter_status_poll_migration);
    qtest_add_func("sam9x75/nand/page-status-poll-migration",
                   test_nand_page_status_poll_migration);
    qtest_add_func("sam9x75/nand/random-data-input-migration",
                   test_nand_random_data_input_migration);
    qtest_add_func("sam9x75/nand/program-old-source-migration",
                   test_nand_program_old_source_migration);
    qtest_add_func("sam9x75/nand/off-device-program-migration",
                   test_nand_off_device_program_migration);
    qtest_add_func("sam9x75/nand/features-status-poll-migration",
                   test_nand_features_status_poll_migration);
    qtest_add_func("sam9x75/nand/set-features-migration",
                   test_nand_set_features_migration);
    qtest_add_func("sam9x75/nand/empty-media-migration",
                   test_nand_empty_media_migration);
    qtest_add_func("sam9x75/nand/data-backend-migration",
                   test_nand_data_backend_migration);
    qtest_add_func("sam9x75/nand/raw-backend-migration",
                   test_nand_raw_backend_migration);
    qtest_add_func("sam9x75/board/memory-cs-jumpers",
                   test_board_memory_cs_jumpers);
    qtest_add_func("sam9x75/smc-pmecc/registers",
                   test_smc_and_pmecc_registers);
    qtest_add_func("sam9x75/smc-pmecc/banked-windows",
                   test_pmecc_banked_windows);
    qtest_add_func("sam9x75/smc-pmecc/safety-and-shared-irq",
                   test_smc_safety_and_shared_irq);
    qtest_add_func("sam9x75/smc-pmecc/shared-irq-migration-and-reset",
                   test_smc_shared_irq_migration_and_reset);
    qtest_add_func("sam9x75/system/or-irq-migration-and-reset",
                   test_sys_irq_migration_and_reset);
    qtest_add_func("sam9x75/qspi/status-irq-and-edges",
                   test_qspi_status_irq_and_edges);
    qtest_add_func("sam9x75/qspi/status-migration-and-reset",
                   test_qspi_status_migration_and_reset);
    qtest_add_func("sam9x75/qspi/flash-read-program-and-erase",
                   test_qspi_flash_read_program_and_erase);

    return g_test_run();
}
