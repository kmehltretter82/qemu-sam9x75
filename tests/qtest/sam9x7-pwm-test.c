/*
 * QTest tests for the SAM9X7 32-bit PWM and its PIO/LED routing
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"
#include "migration/migration-qmp.h"
#include "qemu/bitops.h"
#include "qobject/qdict.h"

#define MACHINE "-machine sam9x75-curiosity"
#define PWM_BASE 0xf8034000
#define PMC_BASE 0xfffffc00
#define AIC_BASE 0xfffff100
#define PIOB_BASE 0xfffff600
#define PIOC_BASE 0xfffff800
#define PWM_MR 0x00
#define PWM_ENA 0x04
#define PWM_DIS 0x08
#define PWM_SR 0x0c
#define PWM_IER 0x10
#define PWM_IDR 0x14
#define PWM_IMR 0x18
#define PWM_ISR 0x1c
#define PWM_CH(n, reg) (0x200 + (n) * 0x20 + (reg))
#define PWM_CMR 0x00
#define PWM_CDTY 0x04
#define PWM_CPRD 0x08
#define PWM_CCNT 0x0c
#define PWM_CUPD 0x10
#define PWM_CALG BIT(8)
#define PWM_CPOL BIT(9)
#define PWM_CPD BIT(10)
#define PMC_PCR 0x88
#define PMC_CMD BIT(31)
#define PMC_EN BIT(28)
#define PIO_PER 0x00
#define PIO_PDR 0x04
#define PIO_OER 0x10
#define PIO_ODR 0x14
#define PIO_SODR 0x30
#define PIO_CODR 0x34
#define PIO_PDSR 0x3c
#define PIO_PUDR 0x60
#define PIO_ABCDSR0 0x70
#define PIO_ABCDSR1 0x74
#define PIO_PPDER 0x94
#define LED_PINS (BIT(20) | BIT(21))

static uint32_t readl(QTestState *qts, unsigned int reg)
{
    return qtest_readl(qts, PWM_BASE + reg);
}

static void writel(QTestState *qts, unsigned int reg, uint32_t value)
{
    qtest_writel(qts, PWM_BASE + reg, value);
}

static void clock_enable(QTestState *qts, unsigned int pid, bool enabled)
{
    qtest_writel(qts, PMC_BASE + PMC_PCR,
                 PMC_CMD | (enabled ? PMC_EN : 0) | pid);
}

static void configure(QTestState *qts, unsigned int n, uint32_t mode,
                       uint32_t period, uint32_t duty)
{
    writel(qts, PWM_CH(n, PWM_CMR), mode);
    writel(qts, PWM_CH(n, PWM_CDTY), duty);
    writel(qts, PWM_CH(n, PWM_CPRD), period);
}

static unsigned int led_intensity(QTestState *qts, const char *color)
{
    g_autofree char *path = g_strdup_printf("/machine/rgb-led-%s", color);
    QDict *response = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':%s,"
        "'property':'intensity-percent'}}", path);
    unsigned int value;

    g_assert_false(qdict_haskey(response, "error"));
    value = qdict_get_int(response, "return");
    qobject_unref(response);
    return value;
}

static void route_leds(QTestState *qts)
{
    clock_enable(qts, 4, true);
    qtest_writel(qts, PIOC_BASE + PIO_PUDR, LED_PINS);
    qtest_writel(qts, PIOC_BASE + PIO_PPDER, LED_PINS);
    qtest_writel(qts, PIOC_BASE + PIO_ABCDSR0, 0);
    qtest_writel(qts, PIOC_BASE + PIO_ABCDSR1, LED_PINS);
    qtest_writel(qts, PIOC_BASE + PIO_PDR, LED_PINS);
}

static void test_registers(void)
{
    QTestState *qts = qtest_init(MACHINE);
    unsigned int n, reg;

    for (reg = 0; reg <= PWM_ISR; reg += 4) {
        g_assert_cmphex(readl(qts, reg), ==, 0);
    }
    writel(qts, PWM_MR, UINT32_MAX);
    g_assert_cmphex(readl(qts, PWM_MR), ==, 0x0fff0fff);
    writel(qts, PWM_IER, UINT32_MAX);
    g_assert_cmphex(readl(qts, PWM_IMR), ==, 0x0f);
    writel(qts, PWM_IDR, 5);
    g_assert_cmphex(readl(qts, PWM_IMR), ==, 0x0a);
    for (n = 0; n < 4; n++) {
        configure(qts, n, UINT32_MAX, UINT32_MAX, 0xfedcba98);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CMR)), ==, 0x70f);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CPRD)), ==, UINT32_MAX);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CDTY)), ==, 0xfedcba98);
        writel(qts, PWM_CH(n, PWM_CCNT), UINT32_MAX);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CCNT)), ==, 0);
        writel(qts, PWM_CH(n, PWM_CUPD), UINT32_MAX);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CUPD)), ==, 0);
    }
    writel(qts, PWM_SR, UINT32_MAX);
    writel(qts, PWM_IMR, 0);
    writel(qts, PWM_ISR, UINT32_MAX);
    g_assert_cmphex(readl(qts, PWM_SR), ==, 0);
    g_assert_cmphex(readl(qts, PWM_IMR), ==, 0x0a);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    qtest_quit(qts);
}

static void test_left_waveform(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    clock_enable(qts, 18, true); /* Reset MCK is the 12 MHz main RC clock. */
    configure(qts, 2, 0, 12000, 9000); /* 1 ms, 25% active, starts inactive. */
    writel(qts, PWM_ENA, BIT(2));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    qtest_clock_step(qts, 749999);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), ==, 9000);
    qtest_clock_step(qts, 249999);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    qtest_clock_step(qts, 1);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(2));
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    qtest_quit(qts);
}

static void test_polarity_and_extremes(void)
{
    QTestState *qts = qtest_init(MACHINE);
    unsigned int polarity, duty;

    route_leds(qts);
    clock_enable(qts, 18, true);
    for (polarity = 0; polarity <= 1; polarity++) {
        for (duty = 0; duty <= 1200; duty += 600) {
            configure(qts, 2, polarity ? PWM_CPOL : 0, 1200, duty);
            writel(qts, PWM_ENA, BIT(2));
            g_assert_cmpuint(led_intensity(qts, "blue"), ==,
                             ((duty == 0) ^ polarity) * 100);
            qtest_clock_step(qts, 50000);
            g_assert_cmpuint(led_intensity(qts, "blue"), ==,
                             ((duty <= 600) ^ polarity) * 100);
            writel(qts, PWM_DIS, BIT(2));
            qtest_clock_step(qts, 50000);
            g_assert_cmphex(readl(qts, PWM_SR), ==, 0);
        }
    }
    /* Configuring polarity on a stopped channel affects the pad immediately. */
    writel(qts, PWM_CH(2, PWM_CMR), 0);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    writel(qts, PWM_CH(2, PWM_CMR), PWM_CPOL);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    qtest_quit(qts);
}

static void test_center_waveform(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    clock_enable(qts, 18, true);
    configure(qts, 3, PWM_CALG, 1200, 600); /* Complete period is 200 us. */
    writel(qts, PWM_ENA, BIT(3));
    qtest_clock_step(qts, 50000);
    g_assert_cmpuint(led_intensity(qts, "green"), ==, 100);
    qtest_clock_step(qts, 50000);
    g_assert_cmpuint(readl(qts, PWM_CH(3, PWM_CCNT)), ==, 1200);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0); /* No interrupt at the apex. */
    qtest_clock_step(qts, 25000);
    g_assert_cmpuint(readl(qts, PWM_CH(3, PWM_CCNT)), ==, 900);
    qtest_clock_step(qts, 25000);
    g_assert_cmpuint(led_intensity(qts, "green"), ==, 0);
    qtest_clock_step(qts, 50000);
    g_assert_cmpuint(readl(qts, PWM_CH(3, PWM_CCNT)), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(3));
    qtest_quit(qts);
}

static void test_interrupts_and_channels(void)
{
    QTestState *qts = qtest_init(MACHINE);
    unsigned int n;

    clock_enable(qts, 18, true);
    for (n = 0; n < 4; n++) {
        configure(qts, n, 0, 1200 * (n + 1), 0);
    }
    writel(qts, PWM_ENA, UINT32_MAX);
    g_assert_cmphex(readl(qts, PWM_SR), ==, 0xf);
    qtest_clock_step(qts, 100000);
    g_assert_false(qtest_readl(qts, AIC_BASE + 0x20) & BIT(18));
    writel(qts, PWM_IER, BIT(0)); /* A stale period flag becomes an IRQ. */
    g_assert_true(qtest_readl(qts, AIC_BASE + 0x20) & BIT(18));
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(0));
    g_assert_false(qtest_readl(qts, AIC_BASE + 0x20) & BIT(18));
    writel(qts, PWM_IER, BIT(1));
    qtest_clock_step(qts, 100000);
    g_assert_true(qtest_readl(qts, AIC_BASE + 0x20) & BIT(18));
    writel(qts, PWM_IDR, BIT(0) | BIT(1));
    g_assert_false(qtest_readl(qts, AIC_BASE + 0x20) & BIT(18));
    qtest_clock_step(qts, 200000);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0xf);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    qtest_quit(qts);
}

static void test_buffered_updates(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    clock_enable(qts, 18, true);
    configure(qts, 2, 0, 12000, 9000);
    writel(qts, PWM_ENA, BIT(2));
    qtest_clock_step(qts, 500000);
    writel(qts, PWM_CH(2, PWM_CUPD), 6000);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CDTY)), ==, 9000);
    qtest_clock_step(qts, 499999);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CDTY)), ==, 9000);
    qtest_clock_step(qts, 1);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CDTY)), ==, 6000);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(2));
    qtest_clock_step(qts, 500000);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    writel(qts, PWM_CH(2, PWM_CMR), PWM_CPD);
    writel(qts, PWM_CH(2, PWM_CUPD), 24000);
    qtest_clock_step(qts, 500000);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CPRD)), ==, 24000);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CDTY)), ==, 6000);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(2));
    qtest_clock_step(qts, 1999999);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(2));
    qtest_quit(qts);
}

static void test_disable_and_update(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    clock_enable(qts, 18, true);
    configure(qts, 2, 0, 12000, 0); /* Constant active output. */
    writel(qts, PWM_ENA, BIT(2));
    qtest_clock_step(qts, 250000);
    writel(qts, PWM_CH(2, PWM_CUPD), 12000); /* Next cycle becomes inactive. */
    writel(qts, PWM_DIS, BIT(2));
    g_assert_cmphex(readl(qts, PWM_SR), ==, BIT(2));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    qtest_clock_step(qts, 749999);
    g_assert_cmphex(readl(qts, PWM_SR), ==, BIT(2));
    qtest_clock_step(qts, 1);
    g_assert_cmphex(readl(qts, PWM_SR), ==, 0);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CDTY)), ==, 12000);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(2));
    qtest_clock_step(qts, 5000000);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    /* Re-enable starts at zero; disable of an inactive channel does nothing. */
    writel(qts, PWM_DIS, BIT(1));
    writel(qts, PWM_ENA, BIT(2));
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), ==, 0);
    qtest_quit(qts);
}

static void test_enabled_register_restrictions(void)
{
    QTestState *qts = qtest_init(MACHINE);

    clock_enable(qts, 18, true);
    configure(qts, 0, 0, 12000, 6000);
    writel(qts, PWM_ENA, BIT(0));
    qtest_clock_step(qts, 100000);
    writel(qts, PWM_CH(0, PWM_CMR), PWM_CPOL | PWM_CALG | PWM_CPD);
    writel(qts, PWM_CH(0, PWM_CDTY), 123);
    writel(qts, PWM_CH(0, PWM_CPRD), 456);
    g_assert_cmphex(readl(qts, PWM_CH(0, PWM_CMR)), ==, PWM_CPD);
    g_assert_cmpuint(readl(qts, PWM_CH(0, PWM_CDTY)), ==, 6000);
    g_assert_cmpuint(readl(qts, PWM_CH(0, PWM_CPRD)), ==, 12000);
    /* Repeated enable must not restart the counter. */
    writel(qts, PWM_ENA, BIT(0));
    g_assert_cmpuint(readl(qts, PWM_CH(0, PWM_CCNT)), ==, 1200);
    qtest_quit(qts);
}

static void test_clock_freeze(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    configure(qts, 2, 1, 12000, 6000);
    writel(qts, PWM_ENA, BIT(2));
    qtest_clock_step(qts, 2000000);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), ==, 0);
    clock_enable(qts, 18, true);
    qtest_clock_step(qts, 83); /* Fraction of the first MCK/2 tick. */
    clock_enable(qts, 18, false);
    qtest_clock_step(qts, 3000000);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    clock_enable(qts, 18, true);
    qtest_clock_step(qts, 83);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), ==, 1);
    qtest_clock_step(qts, 1000000);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    clock_enable(qts, 18, false);
    writel(qts, PWM_CH(2, PWM_CUPD), 12000);
    writel(qts, PWM_DIS, BIT(2));
    qtest_clock_step(qts, 10000000);
    g_assert_cmphex(readl(qts, PWM_SR), ==, BIT(2));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    clock_enable(qts, 18, true);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(readl(qts, PWM_SR), ==, 0);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    qtest_quit(qts);
}

static void test_clock_rate_change(void)
{
    QTestState *qts = qtest_init(MACHINE);

    clock_enable(qts, 18, true);
    configure(qts, 0, 0, 12000, 0);
    writel(qts, PWM_ENA, BIT(0));
    qtest_clock_step(qts, 25);
    /* Switch to 6 MHz, preserving 0.3 tick. */
    qtest_writel(qts, PMC_BASE + 0x28, 1 | BIT(4));
    qtest_clock_step(qts, 116);
    g_assert_cmpuint(readl(qts, PWM_CH(0, PWM_CCNT)), ==, 0);
    qtest_clock_step(qts, 1);
    g_assert_cmpuint(readl(qts, PWM_CH(0, PWM_CCNT)), ==, 1);
    qtest_quit(qts);
}

static void test_shared_clocks(void)
{
    QTestState *qts = qtest_init(MACHINE);

    clock_enable(qts, 18, true);
    configure(qts, 0, 11, 60, 0);
    configure(qts, 1, 12, 60, 0);
    writel(qts, PWM_MR, 3 | (3U << 8) | (5U << 16) | (2U << 24));
    writel(qts, PWM_ENA, BIT(0) | BIT(1)); /* CLKA=MCK/24, CLKB=MCK/20. */
    qtest_clock_step(qts, 100000);
    g_assert_cmpuint(readl(qts, PWM_CH(0, PWM_CCNT)), ==, 50);
    g_assert_cmpuint(readl(qts, PWM_CH(1, PWM_CCNT)), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(1));
    /* CLKA off, CLKB unchanged. */
    writel(qts, PWM_MR, (5U << 16) | (2U << 24));
    qtest_clock_step(qts, 200000);
    g_assert_cmpuint(readl(qts, PWM_CH(0, PWM_CCNT)), ==, 50);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(1));
    writel(qts, PWM_MR, 3 | (3U << 8) | (5U << 16) | (2U << 24));
    qtest_clock_step(qts, 20000);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(0));
    qtest_quit(qts);
}

static void test_prescalers(void)
{
    QTestState *qts = qtest_init(MACHINE);
    unsigned int pres;

    clock_enable(qts, 18, true);
    for (pres = 0; pres <= 10; pres++) {
        configure(qts, 0, pres, 120, 0);
        writel(qts, PWM_ENA, BIT(0));
        qtest_clock_step(qts, (10000LL << pres) - 1);
        g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
        writel(qts, PWM_DIS, BIT(0));
        qtest_clock_step(qts, 1);
        g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(0));
        g_assert_cmphex(readl(qts, PWM_SR), ==, 0);
    }
    qtest_quit(qts);
}

static void hardware_clock(QTestState *qts)
{
    QDict *response;

    /* Disable the reset watchdog before the 20 s run. */
    qtest_writel(qts, 0xffffff84, BIT(15));
    /* 24 MHz * (65 + 1 + 2/3), PLLA's fixed /2, then MCK /3. */
    qtest_writel(qts, PMC_BASE + 0x20, 0x00370000 | BIT(0) | BIT(24));
    qtest_writel(qts, PMC_BASE + 0x1c, 0x003f0000);
    qtest_writel(qts, PMC_BASE + 0x18, 0x00020010);
    qtest_writel(qts, PMC_BASE + 0x10, (65U << 24) | 0x002aaaab);
    qtest_writel(qts, PMC_BASE + 0x1c, 0x003f0100);
    qtest_writel(qts, PMC_BASE + 0x0c, BIT(31) | BIT(29) | BIT(28));
    qtest_writel(qts, PMC_BASE + 0x1c, 0x003f0100);
    qtest_writel(qts, PMC_BASE + 0x28, 2 | (3U << 8));
    clock_enable(qts, 18, true);
    response = qtest_qmp(qts,
        "{'execute':'qom-get','arguments':{'path':'/machine/soc/pwm/pclk',"
        "'property':'qtest-clock-period'}}");
    g_assert_false(qdict_haskey(response, "error"));
    g_assert_cmpuint(qdict_get_int(response, "return"), ==,
                     (1000000000ULL << 32) / 266666666);
    qobject_unref(response);
}

static void test_hardware_32bit_period(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    hardware_clock(qts);
    /* Exact register values from the captured corrected-driver 20 s test. */
    configure(qts, 2, 1, 0x9ef21aae, 0x4f790d57);
    writel(qts, PWM_ENA, BIT(2));
    qtest_clock_step(qts, 9999999900LL);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    g_assert_cmpuint(readl(qts, PWM_CH(2, PWM_CCNT)), >, 0xffff);
    qtest_clock_step(qts, 200);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    writel(qts, PWM_DIS, BIT(2));
    qtest_clock_step(qts, 9999999800LL);
    g_assert_cmphex(readl(qts, PWM_SR), ==, BIT(2));
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    qtest_clock_step(qts, 200);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(2));
    g_assert_cmphex(readl(qts, PWM_SR), ==, 0);
    qtest_quit(qts);
}

static void test_hardware_truncated_driver_period(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    hardware_clock(qts);
    /* The old driver's fls() truncation produced 3.894 s, not 20 s. */
    configure(qts, 2, 0, 0x3de4355c, 0x9ef21aae);
    writel(qts, PWM_ENA, BIT(2));
    qtest_clock_step(qts, 3880000000LL);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    qtest_clock_step(qts, 50000000);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, BIT(2));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    qtest_quit(qts);
}

static void test_pin_mux(void)
{
    QTestState *qts = qtest_init(MACHINE);

    route_leds(qts);
    clock_enable(qts, 18, true);
    configure(qts, 2, 0, 12000, 0);
    configure(qts, 3, PWM_CPOL, 12000, 12000);
    writel(qts, PWM_ENA, BIT(2) | BIT(3));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    g_assert_cmpuint(led_intensity(qts, "green"), ==, 100);
    g_assert_cmphex(qtest_readl(qts, PIOC_BASE + PIO_PDSR) & LED_PINS, ==,
                    LED_PINS);
    /* Peripheral A removes PWM, even though PDR still owns the pad. */
    qtest_writel(qts, PIOC_BASE + PIO_ABCDSR1, 0);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    g_assert_cmpuint(led_intensity(qts, "green"), ==, 0);
    qtest_writel(qts, PIOC_BASE + PIO_ABCDSR1, LED_PINS);
    qtest_writel(qts, PIOC_BASE + PIO_PER, BIT(20));
    qtest_writel(qts, PIOC_BASE + PIO_OER, BIT(20));
    qtest_writel(qts, PIOC_BASE + PIO_CODR, BIT(20));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    g_assert_cmpuint(led_intensity(qts, "green"), ==, 100);
    qtest_writel(qts, PIOC_BASE + PIO_SODR, BIT(20));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    qtest_writel(qts, PIOC_BASE + PIO_ODR, BIT(20));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    qtest_writel(qts, PIOC_BASE + PIO_PDR, BIT(20));
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 100);
    qtest_quit(qts);
}

static void test_alternate_pins(void)
{
    QTestState *qts = qtest_init(MACHINE);
    const uint32_t pb = 0xfU << 11;
    const uint32_t pc = (3U << 10) | (0xfU << 18);
    unsigned int n;

    clock_enable(qts, 3, true);
    clock_enable(qts, 4, true);
    clock_enable(qts, 18, true);
    qtest_writel(qts, PIOB_BASE + PIO_PUDR, pb);
    qtest_writel(qts, PIOB_BASE + PIO_ABCDSR0, pb);
    qtest_writel(qts, PIOB_BASE + PIO_ABCDSR1, 0);
    qtest_writel(qts, PIOB_BASE + PIO_PDR, pb);
    qtest_writel(qts, PIOC_BASE + PIO_PUDR, pc);
    qtest_writel(qts, PIOC_BASE + PIO_ABCDSR0, 0);
    qtest_writel(qts, PIOC_BASE + PIO_ABCDSR1, pc);
    qtest_writel(qts, PIOC_BASE + PIO_PDR, pc);
    for (n = 0; n < 4; n++) {
        configure(qts, n, 0, 12000, 0);
    }
    writel(qts, PWM_ENA, 0xf);
    g_assert_cmphex(qtest_readl(qts, PIOB_BASE + PIO_PDSR) & pb, ==, pb);
    g_assert_cmphex(qtest_readl(qts, PIOC_BASE + PIO_PDSR) & pc, ==, pc);
    /* Changing peripheral selection immediately disconnects all four B pads. */
    qtest_writel(qts, PIOB_BASE + PIO_ABCDSR1, pb);
    g_assert_cmphex(qtest_readl(qts, PIOB_BASE + PIO_PDSR) & pb, ==, 0);
    qtest_quit(qts);
}

static void test_migration(gconstpointer data)
{
    const unsigned int mode = GPOINTER_TO_UINT(data);
    QTestState *from = qtest_init(MACHINE);
    QTestState *to = qtest_init(MACHINE " -incoming defer");
    int64_t now;
    uint32_t cmr = mode == 2 ? PWM_CALG : 0;

    route_leds(from);
    clock_enable(from, 18, true);
    configure(from, 2, cmr, 12000, 6000);
    writel(from, PWM_IER, BIT(2));
    writel(from, PWM_ENA, BIT(2));
    now = qtest_clock_step(from, mode == 2 ? 1250000 : 750000);
    g_assert_cmpuint(led_intensity(from, "blue"), ==, 100);
    if (mode == 1) {
        clock_enable(from, 18, false);
        writel(from, PWM_DIS, BIT(2));
    }
    writel(from, PWM_CH(2, PWM_CUPD), 12000);
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);
    qtest_clock_set(to, now);
    g_assert_cmpuint(led_intensity(to, "blue"), ==, 100);
    g_assert_cmphex(readl(to, PWM_CH(2, PWM_CMR)), ==, cmr);
    g_assert_cmpuint(readl(to, PWM_CH(2, PWM_CCNT)), ==, 9000);
    g_assert_cmpuint(readl(to, PWM_CH(2, PWM_CDTY)), ==, 6000);
    if (mode == 1) {
        qtest_clock_step(to, 3000000);
        g_assert_cmpuint(readl(to, PWM_CH(2, PWM_CCNT)), ==, 9000);
        g_assert_cmphex(readl(to, PWM_SR), ==, BIT(2));
        clock_enable(to, 18, true);
    }
    qtest_clock_step(to, mode == 2 ? 750000 : 250000);
    g_assert_cmpuint(readl(to, PWM_CH(2, PWM_CDTY)), ==, 12000);
    g_assert_cmpuint(led_intensity(to, "blue"), ==, 0);
    g_assert_true(qtest_readl(to, AIC_BASE + 0x20) & BIT(18));
    g_assert_cmphex(readl(to, PWM_ISR), ==, BIT(2));
    g_assert_false(qtest_readl(to, AIC_BASE + 0x20) & BIT(18));
    g_assert_cmphex(readl(to, PWM_SR), ==, mode == 1 ? 0 : BIT(2));
    qtest_quit(to);
    qtest_quit(from);
}

static void test_reset(void)
{
    QTestState *qts = qtest_init(MACHINE);
    unsigned int n;

    route_leds(qts);
    clock_enable(qts, 18, true);
    configure(qts, 2, 0, 1200, 600);
    writel(qts, PWM_ENA, BIT(2));
    writel(qts, PWM_IER, BIT(2));
    qtest_clock_step(qts, 150000);
    writel(qts, PWM_CH(2, PWM_CUPD), 1000);
    writel(qts, PWM_DIS, BIT(2));
    qtest_system_reset(qts);
    g_assert_cmphex(readl(qts, PWM_SR), ==, 0);
    g_assert_cmphex(readl(qts, PWM_IMR), ==, 0);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    for (n = 0; n < 4; n++) {
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CMR)), ==, 0);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CPRD)), ==, 0);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CDTY)), ==, 0);
        g_assert_cmphex(readl(qts, PWM_CH(n, PWM_CCNT)), ==, 0);
    }
    clock_enable(qts, 18, true);
    qtest_clock_step(qts, 1000000);
    g_assert_cmphex(readl(qts, PWM_ISR), ==, 0);
    g_assert_false(qtest_readl(qts, AIC_BASE + 0x20) & BIT(18));
    route_leds(qts);
    g_assert_cmpuint(led_intensity(qts, "blue"), ==, 0);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/sam9x7/pwm/registers", test_registers);
    qtest_add_func("/sam9x7/pwm/left-waveform", test_left_waveform);
    qtest_add_func("/sam9x7/pwm/polarity-extremes", test_polarity_and_extremes);
    qtest_add_func("/sam9x7/pwm/center-waveform", test_center_waveform);
    qtest_add_func("/sam9x7/pwm/interrupts-channels",
                   test_interrupts_and_channels);
    qtest_add_func("/sam9x7/pwm/buffered-updates", test_buffered_updates);
    qtest_add_func("/sam9x7/pwm/disable-update", test_disable_and_update);
    qtest_add_func("/sam9x7/pwm/enabled-registers",
                   test_enabled_register_restrictions);
    qtest_add_func("/sam9x7/pwm/clock-freeze", test_clock_freeze);
    qtest_add_func("/sam9x7/pwm/clock-rate-change", test_clock_rate_change);
    qtest_add_func("/sam9x7/pwm/shared-clocks", test_shared_clocks);
    qtest_add_func("/sam9x7/pwm/prescalers", test_prescalers);
    qtest_add_func("/sam9x7/pwm/hardware-32bit-period",
                   test_hardware_32bit_period);
    qtest_add_func("/sam9x7/pwm/hardware-truncated-period",
                   test_hardware_truncated_driver_period);
    qtest_add_func("/sam9x7/pwm/pin-mux", test_pin_mux);
    qtest_add_func("/sam9x7/pwm/alternate-pins", test_alternate_pins);
    qtest_add_data_func("/sam9x7/pwm/migration-active", GUINT_TO_POINTER(0),
                        test_migration);
    qtest_add_data_func("/sam9x7/pwm/migration-gated-disable",
                        GUINT_TO_POINTER(1),
                        test_migration);
    qtest_add_data_func("/sam9x7/pwm/migration-center", GUINT_TO_POINTER(2),
                        test_migration);
    qtest_add_func("/sam9x7/pwm/reset", test_reset);
    return g_test_run();
}
