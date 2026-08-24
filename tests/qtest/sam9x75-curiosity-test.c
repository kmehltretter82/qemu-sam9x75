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
#define SAM9X7_QSPI_BASE        0xf0014000
#define SAM9X7_FLEXCOM6_BASE    0xf8010000
#define SAM9X7_TWI6_BASE        (SAM9X7_FLEXCOM6_BASE + 0x600)
#define SAM9X7_PIT64B0_BASE     0xf0028000
#define SAM9X7_PIT64B1_BASE     0xf0040000
#define SAM9X7_SFR_BASE         0xf8050000
#define SAM9X7_PMECC_BASE       0xffffe000
#define SAM9X7_PMERRLOC_BASE    0xffffe600
#define SAM9X7_MPDDRC_BASE      0xffffe800
#define SAM9X7_SMC_BASE         0xffffea00
#define SAM9X7_RSTC_BASE        0xfffffe00
#define SAM9X7_PIT_BASE         0xfffffe40
#define SAM9X7_SCKC_BASE        0xfffffe50
#define SAM9X7_SYSCWP_BASE      0xfffffedc
#define SAM9X7_AIC_BASE         0xfffff100
#define SAM9X7_DBGU_BASE        0xfffff200
#define SAM9X7_PIOA_BASE        0xfffff400
#define SAM9X7_PIOB_BASE        0xfffff600
#define SAM9X7_PIOC_BASE        0xfffff800
#define SAM9X7_PIOD_BASE        0xfffffa00
#define SAM9X7_PMC_BASE         0xfffffc00
#define SAM9X7_WDT_BASE         0xffffff80

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
#define DBGU_CIDR               0x40
#define DBGU_EXID               0x44

#define DBGU_CR_RXEN            BIT(4)
#define DBGU_CR_TXEN            BIT(6)
#define DBGU_MR_LOCAL_LOOPBACK  (2U << 14)
#define DBGU_INT_RXRDY          BIT(0)
#define DBGU_INT_TXRDY          BIT(1)
#define DBGU_INT_TXEMPTY        BIT(9)

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
#define RSTC_KEY                0xa5000000

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
#define PIT64B_WPMR             0xe4
#define PIT64B_WPSR             0xe8

#define PIT64B_CR_START         BIT(0)
#define PIT64B_CR_SWRST         BIT(8)
#define PIT64B_MR_CONT          BIT(0)
#define PIT64B_INT_PERIOD       BIT(0)
#define PIT64B_INT_OVRE         BIT(1)
#define PIT64B_WPMR_WPEN        BIT(0)
#define PIT64B_WPMR_KEY         0x50495400

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

    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR), ==,
                    DBGU_INT_TXRDY | DBGU_INT_TXEMPTY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_IMR), ==, 0);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_BRGR, 217);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_BRGR), ==, 217);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_IER,
                 DBGU_INT_RXRDY | DBGU_INT_TXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_IMR), ==,
                    DBGU_INT_RXRDY | DBGU_INT_TXRDY);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_IDR, DBGU_INT_TXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_IMR), ==,
                    DBGU_INT_RXRDY);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_MR,
                 DBGU_MR_LOCAL_LOOPBACK);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_CR,
                 DBGU_CR_RXEN | DBGU_CR_TXEN);
    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_THR, 0x5a);
    g_assert_true(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                  DBGU_INT_RXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_RHR), ==, 0x5a);
    g_assert_false(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_CSR) &
                   DBGU_INT_RXRDY);

    qtest_quit(qts);
}

static void test_dbgu_chardev(void)
{
    int sock_fd;
    uint8_t value;
    QTestState *qts = qtest_init_with_serial(SAM9X75_MACHINE, &sock_fd);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_CR,
                 DBGU_CR_RXEN | DBGU_CR_TXEN);

    value = 0xa5;
    g_assert_cmpint(send(sock_fd, &value, 1, 0), ==, 1);
    dbgu_wait_status(qts, DBGU_INT_RXRDY);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_DBGU_BASE + DBGU_RHR), ==, value);

    qtest_writel(qts, SAM9X7_DBGU_BASE + DBGU_THR, 0x3c);
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
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
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
    qtest_add_func("sam9x75/aic/dbgu-integration",
                   test_aic_dbgu_integration);
    qtest_add_func("sam9x75/aic/priority-and-nesting",
                   test_aic_priority_and_nesting);
    qtest_add_func("sam9x75/aic/fiq-mask-and-write-protection",
                   test_aic_fiq_mask_and_write_protection);
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
    qtest_add_func("sam9x75/system/slowclock-pit-reset-and-protection",
                   test_system_slowclock_pit_reset_and_protection);
    qtest_add_func("sam9x75/sfr/registers-resume-and-protection",
                   test_sfr_registers_resume_and_protection);
    qtest_add_func("sam9x75/mpddrc/registers-errors-and-irq",
                   test_mpddrc_registers_errors_and_irq);
    qtest_add_func("sam9x75/wdt/reset-disable-and-lock",
                   test_wdt_reset_disable_and_lock);
    qtest_add_func("sam9x75/wdt/events-and-system-irq",
                   test_wdt_events_and_system_irq);
    qtest_add_func("sam9x75/nand/identification-program-and-erase",
                   test_nand_identification_program_and_erase);
    qtest_add_func("sam9x75/smc-pmecc/registers",
                   test_smc_and_pmecc_registers);
    qtest_add_func("sam9x75/qspi/flash-read-program-and-erase",
                   test_qspi_flash_read_program_and_erase);

    return g_test_run();
}
