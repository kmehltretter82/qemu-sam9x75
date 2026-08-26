/*
 * QTest test cases for the Microchip SAM9X7 ADC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "migration/migration-qmp.h"
#include "qemu/bitops.h"
#include "qobject/qdict.h"

#define SAM9X75_MACHINE        "-machine sam9x75-curiosity"

#define SAM9X7_SRAM0_BASE      0x00300000
#define SAM9X7_XDMAC_BASE      0xf0008000
#define SAM9X7_ADC_BASE        0xf804c000
#define SAM9X7_AIC_BASE        0xfffff100
#define SAM9X7_PMC_BASE        0xfffffc00

#define ADC_CR                 0x00
#define ADC_MR                 0x04
#define ADC_SEQR1              0x08
#define ADC_CHER               0x10
#define ADC_CHDR               0x14
#define ADC_CHSR               0x18
#define ADC_LCDR               0x20
#define ADC_IER                0x24
#define ADC_IDR                0x28
#define ADC_IMR                0x2c
#define ADC_ISR                0x30
#define ADC_LCTMR              0x34
#define ADC_LCCWR              0x38
#define ADC_OVER               0x3c
#define ADC_EMR                0x40
#define ADC_CWR                0x44
#define ADC_CCR                0x4c
#define ADC_CDR(n)             (0x50 + (n) * 4)
#define ADC_ACR                0x94
#define ADC_PDR                0xa0
#define ADC_TSMR               0xb0
#define ADC_XPOSR              0xb4
#define ADC_YPOSR              0xb8
#define ADC_PRESSR             0xbc
#define ADC_TRGR               0xc0
#define ADC_CVR                0xd4
#define ADC_CECR               0xd8
#define ADC_TSCVR              0xdc
#define ADC_WPMR               0xe4
#define ADC_WPSR               0xe8

#define ADC_CR_SWRST           BIT(0)
#define ADC_CR_START           BIT(1)
#define ADC_CR_CMPRST          BIT(4)
#define ADC_INT_EOC(n)         BIT(n)
#define ADC_INT_DRDY           BIT(24)
#define ADC_MR_RESET           0x20000000
#define ADC_MR_PRESCAL(value)  ((value) << 8)
#define ADC_EMR_CMPTYPE        BIT(2)
#define ADC_EMR_CMPFILTER(v)   ((v) << 12)
#define ADC_EMR_OSR(value)     ((value) << 16)
#define ADC_EMR_ASTE           BIT(20)
#define ADC_EMR_TRACKX4        BIT(22)
#define ADC_ACR_RESET          0x00001200
#define ADC_WPMR_WPEN          BIT(0)
#define ADC_WPMR_WPITEN        BIT(1)
#define ADC_WPMR_WPCTEN        BIT(2)
#define ADC_WPMR_KEY           0x41444300
#define ADC_WPSR_WPVS          BIT(0)
#define ADC_WPSR_WPVSRC(off)   ((off) << 8)

#define AIC_IPR0               0x20

#define PMC_PCR                0x88
#define PMC_PCR_EN             BIT(28)
#define PMC_PCR_CMD            BIT(31)
#define PMC_PID_ADC             19
#define PMC_PID_XDMAC           20

#define XDMAC_GE               0x1c
#define XDMAC_GS               0x24
#define XDMAC_CHANNEL(n)       (0x50 + (n) * 0x40)
#define XDMAC_CSA              0x10
#define XDMAC_CDA              0x14
#define XDMAC_CUBC             0x20
#define XDMAC_CC               0x28
#define XDMAC_CC_TYPE_PER      BIT(0)
#define XDMAC_CC_DWIDTH_HALFWORD (1U << 11)
#define XDMAC_CC_DAM_INC       (1U << 18)
#define XDMAC_CC_PERID(id)     ((id) << 24)
#define XDMAC_ADC_RX_REQUEST   40

static uint32_t adc_readl(QTestState *qts, uint32_t offset)
{
    return qtest_readl(qts, SAM9X7_ADC_BASE + offset);
}

static void adc_writel(QTestState *qts, uint32_t offset, uint32_t value)
{
    qtest_writel(qts, SAM9X7_ADC_BASE + offset, value);
}

static void pmc_enable_peripheral(QTestState *qts, unsigned int pid)
{
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR,
                 PMC_PCR_CMD | PMC_PCR_EN | pid);
}

static void pmc_disable_peripheral(QTestState *qts, unsigned int pid)
{
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR, PMC_PCR_CMD | pid);
}

static void adc_set_input(QTestState *qts, unsigned int channel,
                          uint32_t microvolts)
{
    g_autofree char *property = g_strdup_printf("adci[%u]", channel);
    QDict *response;

    response = qtest_qmp(qts,
                         "{'execute':'qom-set','arguments':"
                         " {'path':'/machine/soc/adc','property':%s,"
                         "  'value':%u}}",
                         property, microvolts);
    g_assert_true(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void test_registers(void)
{
    static const uint16_t reserved_offsets[] = {
        0x0c, 0x1c, 0x48, 0x70, 0x74, 0x78, 0x7c, 0x80, 0x84,
        0x88, 0x8c, 0x90, 0x98, 0x9c, 0xa4, 0xa8, 0xac, 0xc4,
        0xc8, 0xcc, 0xd0, 0xe0, 0xec, 0xf0, 0xf4, 0xf8, 0xfc,
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    unsigned int i;

    g_assert_cmphex(adc_readl(qts, ADC_MR), ==, ADC_MR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_ACR), ==, ADC_ACR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_CHSR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_IMR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_ISR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_WPMR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_WPSR), ==, 0);

    /* Reserved and write-only locations are decoded and read as zero. */
    for (i = 0; i < G_N_ELEMENTS(reserved_offsets); i++) {
        uint16_t offset = reserved_offsets[i];

        g_assert_cmphex(adc_readl(qts, offset), ==, 0);
        adc_writel(qts, offset, UINT32_MAX);
        g_assert_cmphex(adc_readl(qts, offset), ==, 0);
    }
    g_assert_cmphex(adc_readl(qts, ADC_CR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_CHER), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_CHDR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_IER), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_IDR), ==, 0);

    adc_writel(qts, ADC_MR, UINT32_MAX);
    adc_writel(qts, ADC_SEQR1, UINT32_MAX);
    adc_writel(qts, ADC_CHER, UINT32_MAX);
    adc_writel(qts, ADC_CHDR, 0xf0);
    adc_writel(qts, ADC_IER, UINT32_MAX);
    adc_writel(qts, ADC_IDR, BIT(0) | BIT(31));
    adc_writel(qts, ADC_LCTMR, UINT32_MAX);
    adc_writel(qts, ADC_LCCWR, UINT32_MAX);
    adc_writel(qts, ADC_EMR, UINT32_MAX);
    adc_writel(qts, ADC_CWR, UINT32_MAX);
    adc_writel(qts, ADC_CCR, UINT32_MAX);
    adc_writel(qts, ADC_ACR, UINT32_MAX);
    adc_writel(qts, ADC_PDR, UINT32_MAX);
    adc_writel(qts, ADC_TSMR, UINT32_MAX);
    adc_writel(qts, ADC_TRGR, UINT32_MAX);
    adc_writel(qts, ADC_CVR, UINT32_MAX);
    adc_writel(qts, ADC_CECR, UINT32_MAX);
    adc_writel(qts, ADC_TSCVR, UINT32_MAX);

    g_assert_cmphex(adc_readl(qts, ADC_MR), ==, 0xff8fff6e);
    g_assert_cmphex(adc_readl(qts, ADC_SEQR1), ==, UINT32_MAX);
    g_assert_cmphex(adc_readl(qts, ADC_CHSR), ==, 0x0f);
    g_assert_cmphex(adc_readl(qts, ADC_IMR), ==, 0x677800fe);
    g_assert_cmphex(adc_readl(qts, ADC_LCTMR), ==, 0x31);
    g_assert_cmphex(adc_readl(qts, ADC_LCCWR), ==, 0x0fff0fff);
    g_assert_cmphex(adc_readl(qts, ADC_EMR), ==, 0x377732f7);
    g_assert_cmphex(adc_readl(qts, ADC_CWR), ==, UINT32_MAX);
    g_assert_cmphex(adc_readl(qts, ADC_CCR), ==, 0x00ff0000);
    g_assert_cmphex(adc_readl(qts, ADC_ACR), ==, 0x00001303);
    g_assert_cmphex(adc_readl(qts, ADC_PDR), ==, 0xff);
    g_assert_cmphex(adc_readl(qts, ADC_TSMR), ==, 0xf14f0f33);
    g_assert_cmphex(adc_readl(qts, ADC_TRGR), ==, 0xffff0007);
    g_assert_cmphex(adc_readl(qts, ADC_CVR), ==, UINT32_MAX);
    g_assert_cmphex(adc_readl(qts, ADC_CECR), ==, 0xff);
    g_assert_cmphex(adc_readl(qts, ADC_TSCVR), ==, UINT32_MAX);

    /* Result and status registers are read-only. */
    adc_writel(qts, ADC_LCDR, UINT32_MAX);
    adc_writel(qts, ADC_OVER, UINT32_MAX);
    adc_writel(qts, ADC_CDR(0), UINT32_MAX);
    adc_writel(qts, ADC_XPOSR, UINT32_MAX);
    adc_writel(qts, ADC_YPOSR, UINT32_MAX);
    adc_writel(qts, ADC_PRESSR, UINT32_MAX);
    g_assert_cmphex(adc_readl(qts, ADC_LCDR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_OVER), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_CDR(0)), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_XPOSR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_YPOSR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_PRESSR), ==, 0);

    qtest_quit(qts);
}

static void test_write_protection_and_reset(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    adc_writel(qts, ADC_WPMR, UINT32_MAX);
    g_assert_cmphex(adc_readl(qts, ADC_WPMR), ==, 0);

    adc_writel(qts, ADC_WPMR, ADC_WPMR_KEY | ADC_WPMR_WPEN);
    adc_writel(qts, ADC_MR, 0);
    adc_writel(qts, ADC_EMR, UINT32_MAX);
    g_assert_cmphex(adc_readl(qts, ADC_MR), ==, ADC_MR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_EMR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_WPSR), ==,
                    ADC_WPSR_WPVS | ADC_WPSR_WPVSRC(ADC_EMR));
    g_assert_cmphex(adc_readl(qts, ADC_WPSR), ==, 0);

    adc_writel(qts, ADC_WPMR, ADC_WPMR_KEY | ADC_WPMR_WPITEN);
    adc_writel(qts, ADC_IER, ADC_INT_EOC(0));
    g_assert_cmphex(adc_readl(qts, ADC_IMR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_WPSR), ==,
                    ADC_WPSR_WPVS | ADC_WPSR_WPVSRC(ADC_IER));

    adc_writel(qts, ADC_MR, BIT(1));
    adc_writel(qts, ADC_WPMR, ADC_WPMR_KEY | ADC_WPMR_WPCTEN);
    adc_writel(qts, ADC_CR, ADC_CR_SWRST);
    g_assert_cmphex(adc_readl(qts, ADC_MR), ==, BIT(1));
    g_assert_cmphex(adc_readl(qts, ADC_WPSR), ==, ADC_WPSR_WPVS);

    /* Software reset restores registers but deliberately preserves WPMR. */
    adc_writel(qts, ADC_WPMR, ADC_WPMR_KEY | ADC_WPMR_WPEN);
    adc_writel(qts, ADC_MR, 0);
    adc_writel(qts, ADC_CR, ADC_CR_SWRST);
    g_assert_cmphex(adc_readl(qts, ADC_MR), ==, ADC_MR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_ACR), ==, ADC_ACR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_IMR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_CHSR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_WPMR), ==, ADC_WPMR_WPEN);
    g_assert_cmphex(adc_readl(qts, ADC_WPSR), ==, 0);

    qtest_system_reset(qts);
    g_assert_cmphex(adc_readl(qts, ADC_MR), ==, ADC_MR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_ACR), ==, ADC_ACR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_WPMR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_WPSR), ==, 0);

    qtest_quit(qts);
}

static void test_clock_gated_conversion_and_irq(void)
{
    const uint32_t expected = 2048;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;

    adc_set_input(qts, 0, 1650000);
    adc_writel(qts, ADC_CHER, BIT(0));
    adc_writel(qts, ADC_IER, ADC_INT_EOC(0) | ADC_INT_DRDY);
    adc_writel(qts, ADC_CR, ADC_CR_START);

    /* A conversion remains suspended while its PMC peripheral clock is off. */
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(19));

    pmc_enable_peripheral(qts, PMC_PID_ADC);
    qtest_clock_step(qts, 1000000);
    status = adc_readl(qts, ADC_ISR);
    g_assert_cmphex(status & (ADC_INT_EOC(0) | ADC_INT_DRDY), ==,
                    ADC_INT_EOC(0) | ADC_INT_DRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(19));

    g_assert_cmphex(adc_readl(qts, ADC_CDR(0)), ==, expected);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_EOC(0), ==, 0);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(19));

    g_assert_cmphex(qtest_readw(qts, SAM9X7_ADC_BASE + ADC_LCDR), ==,
                    expected);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_DRDY, ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(19));

    qtest_quit(qts);
}

static void test_multichannel_sequence(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;

    adc_set_input(qts, 0, 825000);
    adc_set_input(qts, 1, 2475000);
    pmc_enable_peripheral(qts, PMC_PID_ADC);
    adc_writel(qts, ADC_CHER, BIT(0) | BIT(1));
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 1000000);

    status = adc_readl(qts, ADC_ISR);
    g_assert_cmphex(status & (ADC_INT_EOC(0) | ADC_INT_EOC(1) |
                              ADC_INT_DRDY), ==,
                    ADC_INT_EOC(0) | ADC_INT_EOC(1) | ADC_INT_DRDY);
    g_assert_cmphex(adc_readl(qts, ADC_CDR(0)), ==, 1024);
    g_assert_cmphex(adc_readl(qts, ADC_CDR(1)), ==, 3072);
    g_assert_cmphex(adc_readl(qts, ADC_LCDR), ==, 3072);

    qtest_quit(qts);
}

static void test_enhanced_resolution_sequence(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;
    unsigned int i;

    adc_set_input(qts, 0, 825000);
    adc_set_input(qts, 1, 2475000);
    pmc_enable_peripheral(qts, PMC_PID_ADC);
    adc_writel(qts, ADC_EMR, ADC_EMR_ASTE | ADC_EMR_OSR(1));
    adc_writel(qts, ADC_CHER, BIT(0) | BIT(1));
    adc_writel(qts, ADC_CR, ADC_CR_START);

    for (i = 0; i < 6; i++) {
        qtest_clock_step_next(qts);
    }
    status = adc_readl(qts, ADC_ISR);
    g_assert_cmphex(status & (ADC_INT_EOC(0) | ADC_INT_EOC(1) |
                              ADC_INT_DRDY), ==, 0);

    qtest_clock_step_next(qts);
    status = adc_readl(qts, ADC_ISR);
    g_assert_cmphex(status & (ADC_INT_EOC(0) | ADC_INT_EOC(1) |
                              ADC_INT_DRDY), ==,
                    ADC_INT_EOC(0) | ADC_INT_DRDY);
    g_assert_cmphex(adc_readl(qts, ADC_CDR(0)), ==, 2048);
    g_assert_cmphex(adc_readl(qts, ADC_LCDR), ==, 2048);

    qtest_clock_step_next(qts);
    status = adc_readl(qts, ADC_ISR);
    g_assert_cmphex(status & (ADC_INT_EOC(0) | ADC_INT_EOC(1) |
                              ADC_INT_DRDY), ==,
                    ADC_INT_EOC(1) | ADC_INT_DRDY);
    g_assert_cmphex(adc_readl(qts, ADC_CDR(1)), ==, 6144);
    g_assert_cmphex(adc_readl(qts, ADC_LCDR), ==, 6144);

    qtest_quit(qts);
}

static void test_clock_suspend_and_tracking(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    adc_set_input(qts, 0, 1650000);
    pmc_enable_peripheral(qts, PMC_PID_ADC);
    adc_writel(qts, ADC_MR, ADC_MR_PRESCAL(255));
    adc_writel(qts, ADC_CHER, BIT(0));
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 10000);
    pmc_disable_peripheral(qts, PMC_PID_ADC);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_EOC(0), ==, 0);

    pmc_enable_peripheral(qts, PMC_PID_ADC);
    qtest_clock_step(qts, 2000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_EOC(0), ==,
                    ADC_INT_EOC(0));

    adc_writel(qts, ADC_CR, ADC_CR_SWRST);
    adc_writel(qts, ADC_EMR, ADC_EMR_TRACKX4);
    adc_writel(qts, ADC_CHER, BIT(0));
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_EOC(0), ==,
                    ADC_INT_EOC(0));

    qtest_quit(qts);
}

static void test_comparison_start_condition(void)
{
    QTestState *qts = qtest_init(SAM9X75_MACHINE);

    pmc_enable_peripheral(qts, PMC_PID_ADC);
    adc_writel(qts, ADC_CHER, BIT(0));
    adc_writel(qts, ADC_CWR, 2000);
    adc_writel(qts, ADC_EMR,
               ADC_EMR_CMPTYPE | ADC_EMR_CMPFILTER(3));

    adc_set_input(qts, 0, 1650000);
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_DRDY, ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_CDR(0)), ==, 2048);

    /* CMPTYPE starts storage at the first match, independent of CMPFILTER. */
    adc_set_input(qts, 0, 825000);
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_DRDY, ==,
                    ADC_INT_DRDY);
    g_assert_cmphex(adc_readl(qts, ADC_LCDR), ==, 1024);

    adc_set_input(qts, 0, 1650000);
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_DRDY, ==,
                    ADC_INT_DRDY);
    g_assert_cmphex(adc_readl(qts, ADC_LCDR), ==, 2048);

    adc_writel(qts, ADC_CR, ADC_CR_CMPRST);
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(adc_readl(qts, ADC_ISR) & ADC_INT_DRDY, ==, 0);

    qtest_quit(qts);
}

static void test_active_conversion_migration(void)
{
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    int64_t from_clock;

    adc_set_input(from, 0, 1650000);
    pmc_enable_peripheral(from, PMC_PID_ADC);
    adc_writel(from, ADC_MR, ADC_MR_PRESCAL(255));
    adc_writel(from, ADC_CHER, BIT(0));
    adc_writel(from, ADC_CR, ADC_CR_START);
    from_clock = qtest_clock_step(from, 10000);
    g_assert_cmphex(adc_readl(from, ADC_ISR) & ADC_INT_EOC(0), ==, 0);

    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);

    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);
    g_assert_cmphex(adc_readl(to, ADC_MR), ==, ADC_MR_PRESCAL(255));
    g_assert_cmphex(adc_readl(to, ADC_CHSR), ==, BIT(0));
    qtest_clock_step(to, 2000000);
    g_assert_cmphex(adc_readl(to, ADC_ISR) & ADC_INT_EOC(0), ==,
                    ADC_INT_EOC(0));
    g_assert_cmphex(adc_readl(to, ADC_CDR(0)), ==, 2048);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_dma_and_system_reset(void)
{
    const uint32_t channel = XDMAC_CHANNEL(0);
    const uint32_t destination = SAM9X7_SRAM0_BASE + 0x100;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint32_t status;

    adc_set_input(qts, 0, 1650000);
    pmc_enable_peripheral(qts, PMC_PID_XDMAC);
    pmc_enable_peripheral(qts, PMC_PID_ADC);

    qtest_writew(qts, destination, 0xa5a5);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CSA,
                 SAM9X7_ADC_BASE + ADC_LCDR);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CDA,
                 destination);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CUBC, 1);
    qtest_writel(qts, SAM9X7_XDMAC_BASE + channel + XDMAC_CC,
                 XDMAC_CC_TYPE_PER |
                 XDMAC_CC_DWIDTH_HALFWORD |
                 XDMAC_CC_DAM_INC |
                 XDMAC_CC_PERID(XDMAC_ADC_RX_REQUEST));
    qtest_writel(qts, SAM9X7_XDMAC_BASE + XDMAC_GE, BIT(0));
    g_assert_true(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));

    adc_writel(qts, ADC_CHER, BIT(0));
    adc_writel(qts, ADC_IER, ADC_INT_EOC(0));
    adc_writel(qts, ADC_CR, ADC_CR_START);
    qtest_clock_step(qts, 1000000);

    g_assert_cmphex(qtest_readw(qts, destination), ==, 2048);
    g_assert_false(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS) & BIT(0));
    status = adc_readl(qts, ADC_ISR);
    g_assert_true(status & ADC_INT_EOC(0));
    g_assert_false(status & ADC_INT_DRDY);
    g_assert_true(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(19));

    qtest_system_reset(qts);
    g_assert_cmphex(adc_readl(qts, ADC_MR), ==, ADC_MR_RESET);
    g_assert_cmphex(adc_readl(qts, ADC_CHSR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_IMR), ==, 0);
    g_assert_cmphex(adc_readl(qts, ADC_ISR), ==, 0);
    g_assert_cmphex(qtest_readl(qts, SAM9X7_XDMAC_BASE + XDMAC_GS), ==, 0);
    g_assert_false(qtest_readl(qts, SAM9X7_AIC_BASE + AIC_IPR0) & BIT(19));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/sam9x7/adc/registers", test_registers);
    qtest_add_func("/sam9x7/adc/write-protection-reset",
                   test_write_protection_and_reset);
    qtest_add_func("/sam9x7/adc/clock-conversion-irq",
                   test_clock_gated_conversion_and_irq);
    qtest_add_func("/sam9x7/adc/multichannel-sequence",
                   test_multichannel_sequence);
    qtest_add_func("/sam9x7/adc/enhanced-resolution-sequence",
                   test_enhanced_resolution_sequence);
    qtest_add_func("/sam9x7/adc/clock-suspend-tracking",
                   test_clock_suspend_and_tracking);
    qtest_add_func("/sam9x7/adc/comparison-start-condition",
                   test_comparison_start_condition);
    qtest_add_func("/sam9x7/adc/active-conversion-migration",
                   test_active_conversion_migration);
    qtest_add_func("/sam9x7/adc/dma-system-reset",
                   test_dma_and_system_reset);

    return g_test_run();
}
