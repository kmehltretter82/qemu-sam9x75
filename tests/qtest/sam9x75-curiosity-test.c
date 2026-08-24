/*
 * QTest tests for the Microchip SAM9X75 Curiosity machine
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/bitops.h"

#define SAM9X75_MACHINE         "-machine sam9x75-curiosity"

#define SAM9X7_BOOT_BASE        0x00000000
#define SAM9X7_ROM_BASE         0x00100000
#define SAM9X7_SRAM0_BASE       0x00300000
#define SAM9X7_SRAM1_BASE       0x00400000
#define SAM9X7_DDR_BASE         0x20000000
#define SAM9X7_NAND_BASE        0x30000000
#define SAM9X7_QSPI_MEM_BASE    0x60000000
#define SAM9X7_SDMMC0_BASE      0x80000000
#define SAM9X7_XDMAC_BASE       0xf0008000
#define SAM9X7_QSPI_BASE        0xf0014000
#define SAM9X7_I2SMCC_BASE      0xf001c000
#define SAM9X7_SHA_BASE         0xf002c000
#define SAM9X7_CLASSD_BASE      0xf003c000
#define SAM9X7_FLEXCOM6_BASE    0xf8010000
#define SAM9X7_TWI6_BASE        (SAM9X7_FLEXCOM6_BASE + 0x600)
#define SAM9X7_PIT64B0_BASE     0xf0028000
#define SAM9X7_TRNG_BASE        0xf0030000
#define SAM9X7_AES_BASE         0xf0034000
#define SAM9X7_TDES_BASE        0xf0038000
#define SAM9X7_PIT64B1_BASE     0xf0040000
#define SAM9X7_TCB_BASE         0xf8008000
#define SAM9X7_GMAC_BASE        0xf802c000
#define SAM9X7_SFR_BASE         0xf8050000
#define SAM9X7_PMECC_BASE       0xffffe000
#define SAM9X7_PMERRLOC_BASE    0xffffe600
#define SAM9X7_MPDDRC_BASE      0xffffe800
#define SAM9X7_SMC_BASE         0xffffea00
#define SAM9X7_RSTC_BASE        0xfffffe00
#define SAM9X7_SHDWC_BASE       0xfffffe10
#define SAM9X7_RTT_BASE         0xfffffe20
#define SAM9X7_PIT_BASE         0xfffffe40
#define SAM9X7_SCKC_BASE        0xfffffe50
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

#define SDHCI_BLKSIZE           0x04
#define SDHCI_BLKCNT            0x06
#define SDHCI_ARGUMENT          0x08
#define SDHCI_TRNMOD            0x0c
#define SDHCI_CMDREG            0x0e
#define SDHCI_RSPREG0           0x10
#define SDHCI_HOSTCTL           0x28
#define SDHCI_CLKCON            0x2c
#define SDHCI_SWRST             0x2f
#define SDHCI_NORINTSTS         0x30
#define SDHCI_ERRINTSTS         0x32
#define SDHCI_NORINTSTSEN       0x34
#define SDHCI_ERRINTSTSEN       0x36
#define SDHCI_ADMAERR           0x54
#define SDHCI_ADMASYSADDR       0x58

#define SDHCI_TRNS_DMA          BIT(0)
#define SDHCI_TRNS_BLK_CNT_EN   BIT(1)
#define SDHCI_TRNS_READ         BIT(4)
#define SDHCI_CMD_RESPONSE      3
#define SDHCI_CMD_DATA_PRESENT  BIT(5)
#define SDHCI_CTRL_ADMA2_32     BIT(4)
#define SDHCI_CLOCK_ENABLE      (BIT(2) | BIT(1) | BIT(0))
#define SDHCI_RESET_ALL         BIT(0)
#define SDHCI_INT_CMD_COMPLETE  BIT(0)
#define SDHCI_INT_XFER_COMPLETE BIT(1)
#define SDHCI_INT_ERROR         BIT(15)
#define SDHCI_ERR_ADMA          BIT(9)

#define SDHCI_CMD_INDEX(index)  ((index) << 8)
#define SDHCI_ADMA2_VALID       BIT(0)
#define SDHCI_ADMA2_END         BIT(1)
#define SDHCI_ADMA2_TRAN        BIT(5)

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

#define AIC_SMR_LEVEL_HIGH      (2U << 5)
#define AIC_SMR_EDGE_RISING     (3U << 5)
#define AIC_CISR_FIQ            BIT(0)
#define AIC_CISR_IRQ            BIT(1)
#define AIC_DCR_GMSK            BIT(1)
#define AIC_WPMR_KEY            0x41494300

#define FLEX_MR                 0x00
#define FLEX_MODE_USART         1
#define FLEX_MODE_TWI           3

#define GEM_NWCTRL              0x000
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

#define GEM_NWCTRL_TXEN         BIT(3)
#define GEM_NWCTRL_MPE          BIT(4)
#define GEM_NWCTRL_TSTART       BIT(9)
#define GEM_INT_XMIT_COMPLETE   BIT(7)
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
#define TWI_FIER                0x64
#define TWI_FIDR                0x68
#define TWI_FIMR                0x6c
#define TWI_WPMR                0xe4
#define TWI_WPSR                0xe8

#define TWI_CR_START            BIT(0)
#define TWI_CR_STOP             BIT(1)
#define TWI_CR_MSEN             BIT(2)
#define TWI_CR_SVDIS            BIT(5)
#define TWI_CR_SWRST            BIT(7)
#define TWI_CR_ACMEN            BIT(16)
#define TWI_CR_FIFOEN           BIT(28)

#define TWI_MMR_IADRSZ_1        BIT(8)
#define TWI_MMR_MREAD           BIT(12)
#define TWI_MMR_DADR(addr)      ((addr) << 16)

#define TWI_SR_TXCOMP           BIT(0)
#define TWI_SR_RXRDY            BIT(1)
#define TWI_SR_TXRDY            BIT(2)
#define TWI_SR_SVREAD           BIT(3)
#define TWI_SR_NACK             BIT(8)
#define TWI_SR_SCL              BIT(24)
#define TWI_SR_SDA              BIT(25)

#define TWI_ACR_DATAL(len)      (len)
#define TWI_ACR_DIR             BIT(8)

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

#define PMC_PLL_CTRL0           0x0c
#define PMC_PLL_CTRL1           0x10
#define PMC_PLL_ACR             0x18
#define PMC_PLL_UPDT            0x1c
#define PMC_MOR                 0x20
#define PMC_MCFR                0x24
#define PMC_MCKR                0x28
#define PMC_IER                 0x60
#define PMC_IDR                 0x64
#define PMC_SR                  0x68
#define PMC_WPMR                0x80
#define PMC_WPSR                0x84
#define PMC_PCR                 0x88
#define PMC_CSR1                0xa4
#define PMC_GCSR1               0xc4
#define PMC_PLL_ISR0            0xec

#define PMC_PLL_CTRL0_ENPLL     BIT(28)
#define PMC_PLL_CTRL0_ENPLLCK   BIT(29)
#define PMC_PLL_CTRL0_ENLOCK    BIT(31)
#define PMC_PLL_UPDT_UPDATE     BIT(8)
#define PMC_MOR_MOSCXTEN        BIT(0)
#define PMC_MOR_MOSCRCEN        BIT(3)
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

#define RSTC_SR                 0x04
#define RSTC_MR                 0x08

#define RSTC_SR_URSTS           BIT(0)
#define RSTC_SR_NRSTL           BIT(16)
#define RSTC_MR_URSTEN          BIT(0)
#define RSTC_MR_URSTIEN         BIT(4)
#define RSTC_MR_ENGCLR          BIT(20)
#define RSTC_KEY                0xa5000000

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

#define SFR_OHCIICR_ARIE        BIT(4)
#define SFR_WPMR_WPEN           BIT(0)
#define SFR_WPMR_KEY            0x53465200

#define MPDDRC_RTR              0x04
#define MPDDRC_CR               0x08
#define MPDDRC_IO_CALIBR        0x34
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
#define TCB_SR_CLKSTA           BIT(16)
#define TCB_WPMR_WPEN           BIT(0)
#define TCB_WPMR_WPITEN         BIT(1)
#define TCB_WPMR_WPCREN         BIT(2)
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
#define XDMAC_CC_DWIDTH_HALFWORD (1U << 11)
#define XDMAC_CC_DWIDTH_WORD    (2U << 11)
#define XDMAC_CC_CSIZE_4        (2U << 8)
#define XDMAC_CC_CSIZE_16       (4U << 8)
#define XDMAC_CC_SAM_INC        (1U << 16)
#define XDMAC_CC_DAM_INC        (1U << 18)
#define XDMAC_CC_DAM_UBS        (2U << 18)
#define XDMAC_CC_INITD          BIT(21)
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

#define NAND_CMD_READ0          0x00
#define NAND_CMD_PAGE_PROGRAM   0x10
#define NAND_CMD_READ_START     0x30
#define NAND_CMD_ERASE          0x60
#define NAND_CMD_STATUS         0x70
#define NAND_CMD_PROGRAM_START  0x80
#define NAND_CMD_READ_ID        0x90
#define NAND_CMD_ERASE_START    0xd0
#define NAND_CMD_READ_PARAM     0xec

#define NAND_STATUS_READY       BIT(6)
#define NAND_STATUS_WP          BIT(7)

#define SMC_SETUP2              0x20
#define SMC_PULSE2              0x24
#define SMC_CYCLE2              0x28
#define SMC_MODE2               0x2c
#define SMC_WPMR                0xe4
#define SMC_WPSR                0xe8
#define SMC_WPMR_KEY            0x534d4300
#define SMC_WPSR_WPVS           BIT(0)

#define PMECC_CFG               0x00
#define PMECC_SAREA             0x04
#define PMECC_SADDR             0x08
#define PMECC_EADDR             0x0c
#define PMECC_CTRL              0x14
#define PMECC_SR                0x18
#define PMECC_IER               0x1c
#define PMECC_IMR               0x24
#define PMECC_ECC0              0x40
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
#define QSPI_ISR                0x10
#define QSPI_IER                0x14
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
#define QSPI_CR_STTFR           BIT(9)
#define QSPI_CR_LASTXFER        BIT(24)
#define QSPI_MR_SMM             BIT(0)
#define QSPI_ISR_TDRE           BIT(1)
#define QSPI_ISR_TXEMPTY        BIT(2)
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

static void pmc_write_pcr(QTestState *qts, unsigned int id,
                          uint32_t config);

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

    qtest_writel(qts, SAM9X7_DDR_BASE, 0xc001d00d);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DDR_BASE), ==, 0xc001d00d);

    g_assert_cmphex(qtest_readl(qts, SAM9X7_ROM_BASE), ==,
                    qtest_readl(qts, SAM9X7_BOOT_BASE));

    qtest_quit(qts);
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

static void twi6_enable_master(QTestState *qts)
{
    qtest_writeb(qts, SAM9X7_FLEXCOM6_BASE + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_SWRST);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_MSEN | TWI_CR_SVDIS);
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
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_TXRDY);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_THR, 0xa5);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_STOP);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_MREAD |
                 TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x20);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_START);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_RXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_RHR), ==,
                    0x5a);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_STOP);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_RHR), ==,
                    0xa5);
    status = qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR);
    g_assert_true(status & TWI_SR_TXCOMP);
    g_assert_false(status & TWI_SR_RXRDY);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR,
                 TWI_CR_FIFOEN | TWI_CR_ACMEN);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_ACR, TWI_ACR_DATAL(2));
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x40);
    qtest_writew(qts, SAM9X7_TWI6_BASE + TWI_THR, 0xbbaa);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);

    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_ACR,
                 TWI_ACR_DIR | TWI_ACR_DATAL(2));
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_MMR,
                 TWI_MMR_DADR(0x53) | TWI_MMR_IADRSZ_1);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_IADR, 0x40);
    qtest_writel(qts, SAM9X7_TWI6_BASE + TWI_CR, TWI_CR_START);
    g_assert_cmphex(qtest_readw(qts, SAM9X7_TWI6_BASE + TWI_RHR), ==,
                    0xbbaa);
    status = qtest_readl(qts, SAM9X7_TWI6_BASE + TWI_SR);
    g_assert_true(status & TWI_SR_TXCOMP);
    g_assert_false(status & TWI_SR_RXRDY);

    qtest_quit(qts);
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

static void test_pmc_clock_tree_and_protection(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t value;

    g_assert_cmphex(qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MOR), ==,
                    PMC_MOR_MOSCRCEN);
    value = qtest_readl(qts, SAM9X7_PMC_BASE + PMC_SR);
    g_assert_cmphex(value & (PMC_SR_MCKRDY | PMC_SR_MOSCRCS), ==,
                    PMC_SR_MCKRDY | PMC_SR_MOSCRCS);
    g_assert_cmpuint(get_clock_period(qts, "/machine/soc/pmc/mainck"), ==,
                     CLOCK_PERIOD_FROM_HZ(12000000));
    value = qtest_readl(qts, SAM9X7_PMC_BASE + PMC_MCFR);
    g_assert_true(value & PMC_MCFR_MAINRDY);
    g_assert_false(value & PMC_MCFR_RCMEAS);
    g_assert_cmphex(value & 0xffff, ==,
                    (12000000ULL * 16 / 32000) & 0xffff);

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

    pmc_write_pcr(qts, 2, PMC_PCR_EN);
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
    qtest_clock_step(qts, 10000);
    pio_set_input(qts, 0, 29, 1);
    qtest_clock_step(qts, 40000);
    g_assert_true(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_PDSR) &
                  filter_pin);
    g_assert_false(qtest_readl(qts, SAM9X7_PIOA_BASE + PIO_ISR) &
                   filter_pin);

    pio_set_input(qts, 0, 29, 0);
    qtest_clock_step(qts, 40000);
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

static void test_sdhci_adma2_linux_nop_terminator(void)
{
    const uint64_t descriptor_addr = SAM9X7_DDR_BASE + 0x1000;
    const uint64_t data_addr = SAM9X7_DDR_BASE + 0x2000;
    g_autofree char *sd_path = NULL;
    uint8_t expected[512];
    uint8_t actual[512];
    QTestState *qts;
    GError *error = NULL;
    uint16_t rca;
    int fd;
    int ret;
    size_t i;

    for (i = 0; i < sizeof(expected); i++) {
        expected[i] = i ^ 0xa5;
    }

    fd = g_file_open_tmp("sam9x75-sdhci-XXXXXX", &sd_path, &error);
    g_assert_no_error(error);
    g_assert_cmpint(fd, >=, 0);
    ret = write(fd, expected, sizeof(expected));
    g_assert_cmpint(ret, ==, sizeof(expected));
    ret = ftruncate(fd, 1 << 20);
    g_assert_cmpint(ret, ==, 0);
    close(fd);

    qts = qtest_initf(SAM9X75_MACHINE
                      " -drive file=%s,if=sd,format=raw,auto-read-only=off",
                      sd_path);

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
                        SDHCI_CMD_INDEX(7) | SDHCI_CMD_RESPONSE);

    qtest_writeb(qts, SAM9X7_SDMMC0_BASE + SDHCI_HOSTCTL,
                 SDHCI_CTRL_ADMA2_32);
    qtest_writel(qts, descriptor_addr,
                 (512U << 16) | SDHCI_ADMA2_TRAN | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 4, data_addr);
    qtest_writel(qts, descriptor_addr + 8,
                 SDHCI_ADMA2_END | SDHCI_ADMA2_VALID);
    qtest_writel(qts, descriptor_addr + 12, 0);
    qtest_writel(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMASYSADDR,
                 descriptor_addr);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTSEN,
                 SDHCI_INT_CMD_COMPLETE | SDHCI_INT_XFER_COMPLETE);
    qtest_writew(qts, SAM9X7_SDMMC0_BASE + SDHCI_ERRINTSTSEN,
                 SDHCI_ERR_ADMA);

    /* Linux terminates ADMA2 tables with a valid END/NOP descriptor. */
    sdhci_issue_command(qts, sizeof(expected), 1, 0,
                        SDHCI_TRNS_DMA | SDHCI_TRNS_BLK_CNT_EN |
                        SDHCI_TRNS_READ,
                        SDHCI_CMD_INDEX(17) | SDHCI_CMD_DATA_PRESENT |
                        SDHCI_CMD_RESPONSE);
    qtest_clock_step(qts, 1000);

    g_assert_cmphex(qtest_readb(qts, SAM9X7_SDMMC0_BASE + SDHCI_ADMAERR),
                    ==, 0);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_ERRINTSTS) &
                    SDHCI_ERR_ADMA, ==, 0);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTS) &
                    SDHCI_INT_ERROR, ==, 0);
    g_assert_cmphex(qtest_readw(qts,
                               SAM9X7_SDMMC0_BASE + SDHCI_NORINTSTS) &
                    SDHCI_INT_XFER_COMPLETE, !=, 0);
    qtest_memread(qts, data_addr, actual, sizeof(actual));
    g_assert_cmpmem(actual, sizeof(actual), expected, sizeof(expected));

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

static uint16_t nand_onfi_crc(const uint8_t *data, size_t length)
{
    uint16_t crc = 0x4f4e;
    unsigned int i;

    while (length--) {
        crc ^= *data++ << 8;
        for (i = 0; i < 8; i++) {
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0);
        }
    }
    return crc;
}

static void test_nand_identification_program_and_erase(void)
{
    static const uint8_t expected_id[] = {
        0xc2, 0xdc, 0x90, 0xa2, 0x57, 0x03,
    };
    static const uint8_t payload[] = {
        0x5a, 0xa5, 0x00, 0xff, 0x36, 0xc9,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t parameter_page[256];
    unsigned int i;

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
    for (i = 0; i < sizeof(parameter_page); i++) {
        parameter_page[i] = qtest_readb(qts, NAND_DATA);
    }
    g_assert_cmpmem(parameter_page, 4, "ONFI", 4);
    g_assert_cmpmem(parameter_page + 32, 8, "MACRONIX", 8);
    g_assert_cmpmem(parameter_page + 44, 17, "MX30LF4G28AD-XKI", 17);
    g_assert_cmpuint(ldl_le_p(parameter_page + 80), ==, 4096);
    g_assert_cmpuint(lduw_le_p(parameter_page + 84), ==, 256);
    g_assert_cmpuint(ldl_le_p(parameter_page + 92), ==, 64);
    g_assert_cmpuint(ldl_le_p(parameter_page + 96), ==, 2048);
    g_assert_cmphex(lduw_le_p(parameter_page + 254), ==,
                    nand_onfi_crc(parameter_page, 254));

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
    g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==,
                    NAND_STATUS_READY | NAND_STATUS_WP);

    nand_command(qts, NAND_CMD_READ0);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_address(qts, 0x03);
    nand_address(qts, 0x00);
    nand_address(qts, 0x00);
    nand_command(qts, NAND_CMD_READ_START);
    for (i = 0; i < G_N_ELEMENTS(payload); i++) {
        g_assert_cmphex(qtest_readb(qts, NAND_DATA), ==, payload[i]);
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

static void test_qspi_flash_read_program_and_erase(void)
{
    const uint32_t addressed_data = QSPI_IFR_INSTEN | QSPI_IFR_ADDREN |
                                    QSPI_IFR_DATAEN | QSPI_IFR_ADDRL_3 |
                                    QSPI_IFR_TFRTYP_MEM;
    const uint32_t flash_offset = 0x1234;
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
                    0x00ffff03);
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
    qspi_command(qts, 0x20, flash_offset,
                 QSPI_IFR_INSTEN | QSPI_IFR_ADDREN | QSPI_IFR_ADDRL_3);
    qspi_configure_read(qts, 0x03, addressed_data);
    g_assert_cmphex(qtest_readl(qts,
                               SAM9X7_QSPI_MEM_BASE + flash_offset), ==,
                    UINT32_MAX);
    qspi_finish_transfer(qts);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("sam9x75/memory-and-identification",
                   test_memory_and_identification);
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
    qtest_add_func("sam9x75/flexcom-twi/registers-nack-and-protection",
                   test_flexcom_twi_registers_nack_and_protection);
    qtest_add_func("sam9x75/flexcom-twi/eeprom-fifo-and-access-width",
                   test_twi_eeprom_transfers_fifo_and_access_width);
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
    qtest_add_func("sam9x75/xdmac/registers-memcpy-and-descriptors",
                   test_xdmac_registers_memcpy_and_descriptors);
    qtest_add_func("sam9x75/xdmac/pacing-striding-and-errors",
                   test_xdmac_pacing_striding_and_errors);
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
    qtest_add_func("sam9x75/mpddrc/registers-errors-and-irq",
                   test_mpddrc_registers_errors_and_irq);
    qtest_add_func("sam9x75/wdt/reset-disable-and-lock",
                   test_wdt_reset_disable_and_lock);
    qtest_add_func("sam9x75/wdt/events-and-system-irq",
                   test_wdt_events_and_system_irq);
    qtest_add_func("sam9x75/sdhci/adma2-linux-nop-terminator",
                   test_sdhci_adma2_linux_nop_terminator);
    qtest_add_func("sam9x75/nand/identification-program-and-erase",
                   test_nand_identification_program_and_erase);
    qtest_add_func("sam9x75/smc-pmecc/registers",
                   test_smc_and_pmecc_registers);
    qtest_add_func("sam9x75/qspi/flash-read-program-and-erase",
                   test_qspi_flash_read_program_and_erase);

    return g_test_run();
}
