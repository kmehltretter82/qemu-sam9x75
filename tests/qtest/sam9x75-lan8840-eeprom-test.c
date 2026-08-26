/*
 * QTest tests for the SAM9X75 Curiosity LAN8840 daughter-card EEPROM
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "libqtest.h"
#include "migration/migration-qmp.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"

#define SAM9X75_MACHINE         "-machine sam9x75-curiosity"

#define SAM9X7_FLEXCOM7_BASE    0xf8014000
#define SAM9X7_TWI7_BASE        (SAM9X7_FLEXCOM7_BASE + 0x600)
#define SAM9X7_PMC_BASE         0xfffffc00

#define FLEX_MR                 0x00
#define FLEX_MODE_TWI           3

#define TWI_CR                  0x00
#define TWI_MMR                 0x04
#define TWI_IADR                0x0c
#define TWI_SR                  0x20
#define TWI_RHR                 0x30
#define TWI_THR                 0x34

#define TWI_CR_START            BIT(0)
#define TWI_CR_STOP             BIT(1)
#define TWI_CR_MSEN             BIT(2)
#define TWI_CR_SVDIS            BIT(5)
#define TWI_CR_QUICK            BIT(6)
#define TWI_CR_SWRST            BIT(7)

#define TWI_MMR_IADRSZ(size)    ((size) << 8)
#define TWI_MMR_MREAD           BIT(12)
#define TWI_MMR_DADR(addr)      ((addr) << 16)

#define TWI_SR_TXCOMP           BIT(0)
#define TWI_SR_RXRDY            BIT(1)
#define TWI_SR_TXRDY            BIT(2)
#define TWI_SR_NACK             BIT(8)

#define PMC_PCR                 0x88
#define PMC_PCR_EN              BIT(28)
#define PMC_PCR_CMD             BIT(31)

#define FLEXCOM7_PID            10
#define LAN8840_EEPROM_ADDR     0x54
#define AT24C01_WRITE_CYCLE_NS  (5 * SCALE_MS)

static uint32_t twi_wait_status(QTestState *qts, uint32_t mask)
{
    unsigned int i;
    uint32_t status;

    for (i = 0; i < 128; i++) {
        status = qtest_readl(qts, SAM9X7_TWI7_BASE + TWI_SR);
        if (status & mask) {
            return status;
        }
        qtest_clock_step_next(qts);
    }

    g_assert_not_reached();
}

static void twi7_enable_master(QTestState *qts)
{
    qtest_writel(qts, SAM9X7_PMC_BASE + PMC_PCR,
                 PMC_PCR_CMD | FLEXCOM7_PID | PMC_PCR_EN);
    qtest_writeb(qts, SAM9X7_FLEXCOM7_BASE + FLEX_MR, FLEX_MODE_TWI);
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_CR, TWI_CR_SWRST);
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_CR,
                 TWI_CR_MSEN | TWI_CR_SVDIS);
}

static bool eeprom_read_byte_at(QTestState *qts, uint8_t address,
                                uint32_t offset, unsigned int address_size,
                                uint8_t *value)
{
    uint32_t status;

    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_MMR,
                 TWI_MMR_DADR(address) | TWI_MMR_MREAD |
                 TWI_MMR_IADRSZ(address_size));
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_IADR, offset);
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_CR,
                 TWI_CR_START | TWI_CR_STOP);
    status = twi_wait_status(qts, TWI_SR_RXRDY | TWI_SR_NACK);
    if (status & TWI_SR_NACK) {
        return false;
    }

    *value = qtest_readb(qts, SAM9X7_TWI7_BASE + TWI_RHR);
    g_assert_true(qtest_readl(qts, SAM9X7_TWI7_BASE + TWI_SR) &
                  TWI_SR_TXCOMP);
    return true;
}

static bool eeprom_read_byte(QTestState *qts, uint8_t address,
                             uint8_t offset, uint8_t *value)
{
    return eeprom_read_byte_at(qts, address, offset, 1, value);
}

static void eeprom_write_at(QTestState *qts, uint8_t address,
                            uint32_t offset, unsigned int address_size,
                            const uint8_t *data, size_t len)
{
    size_t i;

    g_assert_cmpuint(len, >, 0);
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_MMR,
                 TWI_MMR_DADR(address) | TWI_MMR_IADRSZ(address_size));
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_IADR, offset);

    for (i = 0; i < len; i++) {
        qtest_writeb(qts, SAM9X7_TWI7_BASE + TWI_THR, data[i]);
        if (i + 1 < len) {
            twi_wait_status(qts, TWI_SR_TXRDY);
        }
    }
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_CR, TWI_CR_STOP);
    twi_wait_status(qts, TWI_SR_TXCOMP);
}

static void eeprom_write_page(QTestState *qts, uint8_t offset,
                              const uint8_t *data, size_t len)
{
    eeprom_write_at(qts, LAN8840_EEPROM_ADDR, offset, 1, data, len);
}

static void sam9x75_migrate(QTestState *from, QTestState *to)
{
    migrate_incoming_qmp(to, "tcp:127.0.0.1:0", NULL, "{}");
    migrate_qmp(from, to, NULL, NULL, "{}");
    wait_for_migration_complete(from);
    wait_for_migration_complete(to);
}

static void test_lan8840_eeprom_page_wrap_and_busy(void)
{
    static const uint8_t page_data[] = { 0xa0, 0xa1, 0xa2, 0xa3 };
    static const struct {
        uint8_t offset;
        uint8_t value;
    } expected[] = {
        { 0, 0xa2 }, { 1, 0xa3 }, { 2, 0xff },
        { 6, 0xa0 }, { 7, 0xa1 }, { 8, 0xff },
    };
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t value;
    unsigned int i;

    twi7_enable_master(qts);

    /* The exact overlay adds one erased 128-byte EEPROM at address 0x54. */
    g_assert_true(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0, &value));
    g_assert_cmphex(value, ==, 0xff);
    g_assert_false(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR + 1,
                                    0, &value));

    /* A page write beginning at byte 6 wraps within the eight-byte page. */
    eeprom_write_page(qts, 6, page_data, sizeof(page_data));

    /* Address polling is NACKed until the self-timed write cycle ends. */
    g_assert_false(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0, &value));
    qtest_clock_step(qts, AT24C01_WRITE_CYCLE_NS - 1);
    g_assert_false(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0, &value));
    qtest_clock_step(qts, 1);

    for (i = 0; i < ARRAY_SIZE(expected); i++) {
        g_assert_true(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR,
                                       expected[i].offset, &value));
        g_assert_cmphex(value, ==, expected[i].value);
    }

    qtest_quit(qts);
}

static void test_lan8840_eeprom_independent_of_j12(void)
{
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE ",ethernet-25mhz=off");
    uint8_t value;

    twi7_enable_master(qts);
    g_assert_true(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0x7f,
                                   &value));
    g_assert_cmphex(value, ==, 0xff);

    qtest_quit(qts);
}

static void test_lan8840_eeprom_reset_clears_busy(void)
{
    static const uint8_t data = 0x3c;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t value;

    twi7_enable_master(qts);
    eeprom_write_page(qts, 0x20, &data, 1);
    g_assert_false(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0x20,
                                    &value));

    qtest_system_reset(qts);
    twi7_enable_master(qts);
    g_assert_true(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0x20, &value));
    g_assert_cmphex(value, ==, data);

    qtest_quit(qts);
}

static void test_lan8840_eeprom_repeated_start_write_latch(void)
{
    static const uint8_t data = 0x6d;
    QTestState *qts = qtest_init(SAM9X75_MACHINE);
    uint8_t value;

    twi7_enable_master(qts);
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_MMR,
                 TWI_MMR_DADR(LAN8840_EEPROM_ADDR) | TWI_MMR_IADRSZ(1));
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_IADR, 0x30);
    qtest_writeb(qts, SAM9X7_TWI7_BASE + TWI_THR, data);
    twi_wait_status(qts, TWI_SR_TXRDY);

    /* QUICK issues a repeated START before it ends the active write. */
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_MMR,
                 TWI_MMR_DADR(LAN8840_EEPROM_ADDR));
    qtest_writel(qts, SAM9X7_TWI7_BASE + TWI_CR, TWI_CR_QUICK);
    twi_wait_status(qts, TWI_SR_TXCOMP);

    /* The repeated START must not discard the pending write-cycle latch. */
    g_assert_false(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0x30,
                                    &value));
    qtest_clock_step(qts, AT24C01_WRITE_CYCLE_NS);
    g_assert_true(eeprom_read_byte(qts, LAN8840_EEPROM_ADDR, 0x30, &value));
    g_assert_cmphex(value, ==, data);

    qtest_quit(qts);
}

static void test_at24c_large_device_address_accumulator(void)
{
    static const uint8_t data = 0x5a;
    QTestState *qts = qtest_init(
        SAM9X75_MACHINE
        " -device at24c-eeprom,bus=i2c7,address=0x53,"
        "rom-size=131072,address-size=2");
    uint8_t value;

    twi7_enable_master(qts);

    /* Leave the current address at one, then begin a fresh address phase. */
    g_assert_true(eeprom_read_byte_at(qts, 0x53, 0, 2, &value));
    g_assert_cmphex(value, ==, 0);
    eeprom_write_at(qts, 0x53, 0, 2, &data, 1);

    /* Reset removes current-address history without erasing the array. */
    qtest_system_reset(qts);
    twi7_enable_master(qts);
    g_assert_true(eeprom_read_byte_at(qts, 0x53, 0, 2, &value));
    g_assert_cmphex(value, ==, data);

    qtest_quit(qts);
}

static void test_lan8840_eeprom_migration(void)
{
    static const uint8_t data = 0x5a;
    QTestState *from = qtest_init(SAM9X75_MACHINE);
    QTestState *to = qtest_init(SAM9X75_MACHINE " -incoming defer");
    int64_t from_clock;
    uint8_t value;

    twi7_enable_master(from);
    eeprom_write_page(from, 0x42, &data, 1);
    from_clock = qtest_clock_step(from, 1);
    g_assert_cmpint(qtest_clock_set(to, from_clock), ==, from_clock);

    sam9x75_migrate(from, to);

    /* Both the programmed array and the outstanding busy timer migrate. */
    g_assert_false(eeprom_read_byte(to, LAN8840_EEPROM_ADDR, 0x42,
                                    &value));
    qtest_clock_step(to, AT24C01_WRITE_CYCLE_NS);
    g_assert_true(eeprom_read_byte(to, LAN8840_EEPROM_ADDR, 0x42, &value));
    g_assert_cmphex(value, ==, data);

    qtest_quit(to);
    qtest_quit(from);
}

static void test_at24c_legacy_migration_opt_out(void)
{
    static const char *args =
        SAM9X75_MACHINE
        " -device at24c-eeprom,bus=i2c7,address=0x53,rom-size=256";
    static const uint8_t data = 0xa5;
    QTestState *from = qtest_init(args);
    QTestState *to = qtest_initf("%s -incoming defer", args);
    uint8_t value;

    twi7_enable_master(from);
    eeprom_write_at(from, 0x53, 0x22, 1, &data, 1);
    g_assert_true(eeprom_read_byte(from, 0x53, 0x22, &value));
    g_assert_cmphex(value, ==, data);

    sam9x75_migrate(from, to);

    /* Existing machine users keep the historical no-EEPROM-state ABI. */
    g_assert_true(eeprom_read_byte(to, 0x53, 0x22, &value));
    g_assert_cmphex(value, ==, 0);

    qtest_quit(to);
    qtest_quit(from);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("sam9x75/lan8840-eeprom/page-wrap-and-busy",
                   test_lan8840_eeprom_page_wrap_and_busy);
    qtest_add_func("sam9x75/lan8840-eeprom/independent-of-j12",
                   test_lan8840_eeprom_independent_of_j12);
    qtest_add_func("sam9x75/lan8840-eeprom/reset-clears-busy",
                   test_lan8840_eeprom_reset_clears_busy);
    qtest_add_func("sam9x75/lan8840-eeprom/repeated-start-write-latch",
                   test_lan8840_eeprom_repeated_start_write_latch);
    qtest_add_func("sam9x75/lan8840-eeprom/migration",
                   test_lan8840_eeprom_migration);
    qtest_add_func("sam9x75/at24c/large-device-address-accumulator",
                   test_at24c_large_device_address_accumulator);
    qtest_add_func("sam9x75/at24c/legacy-migration-opt-out",
                   test_at24c_legacy_migration_opt_out);

    return g_test_run();
}
