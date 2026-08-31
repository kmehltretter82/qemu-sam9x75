/*
 * Microchip SAM9X7 SoC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "exec/cpu-interrupt.h"
#include "hw/arm/sam9x7.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "target/arm/cpu-qom.h"

static const hwaddr sam9x7_flexcom_base[SAM9X7_NUM_FLEXCOM] = {
    0xf801c000,
    0xf8020000,
    0xf8024000,
    0xf8028000,
    0xf0000000,
    0xf0004000,
    0xf8010000,
    0xf8014000,
    0xf8018000,
    0xf8040000,
    0xf8044000,
    0xf0020000,
    0xf0024000,
};

static const unsigned int sam9x7_flexcom_pid[SAM9X7_NUM_FLEXCOM] = {
    5, 6, 7, 8, 13, 14, 9, 10, 11, 15, 16, 32, 33,
};

/* FLEXCOM0-3 expose two SPI chip-select pins; FLEXCOM4-5 expose four. */
static const uint8_t sam9x7_flexcom_spi_num_cs[SAM9X7_NUM_FLEXCOM_SPI] = {
    2, 2, 2, 2, 4, 4,
};

#define SAM9X7_PMECC_GF13_INDEX_OFFSET 0x0000
#define SAM9X7_PMECC_GF13_ALPHA_OFFSET 0x4000
#define SAM9X7_PMECC_GF14_INDEX_OFFSET 0x8000
#define SAM9X7_PMECC_GF14_ALPHA_OFFSET 0x10000

static void sam9x7_build_pmecc_gf_table(uint8_t *rom, unsigned int m,
                                        uint16_t primitive,
                                        size_t index_offset,
                                        size_t alpha_offset)
{
    unsigned int field_size = 1U << m;
    unsigned int value = 1;
    unsigned int i;

    stw_le_p(rom + index_offset, UINT16_MAX);
    for (i = 0; i < field_size - 1; i++) {
        stw_le_p(rom + alpha_offset + i * sizeof(uint16_t), value);
        stw_le_p(rom + index_offset + value * sizeof(uint16_t), i);

        value <<= 1;
        if (value & field_size) {
            value ^= primitive;
        }
    }

    g_assert(value == 1);
    stw_le_p(rom + alpha_offset +
             (field_size - 1) * sizeof(uint16_t), value);
}

static void sam9x7_init_pmecc_gf_tables(MemoryRegion *rom_mr)
{
    uint8_t *rom = memory_region_get_ram_ptr(rom_mr) +
                   SAM9X7_BOOT_ROM_SIZE;

    /*
     * The SAM9X7 ECC ROM holds the lookup tables used by the PMECC software
     * correction algorithm.  It is distinct from the proprietary 80 KiB
     * boot ROM at address zero, so provide the architectural tables
     * independently of whether boot firmware is supplied with -bios or
     * firmware is started directly with -kernel.
     */
    sam9x7_build_pmecc_gf_table(rom, 13, 0x201b,
                                SAM9X7_PMECC_GF13_INDEX_OFFSET,
                                SAM9X7_PMECC_GF13_ALPHA_OFFSET);
    sam9x7_build_pmecc_gf_table(rom, 14, 0x4443,
                                SAM9X7_PMECC_GF14_INDEX_OFFSET,
                                SAM9X7_PMECC_GF14_ALPHA_OFFSET);
}

enum {
    SAM9X7_EBI_ASSIGN_DDR,
    SAM9X7_EBI_ASSIGN_NAND_CS2,
    SAM9X7_EBI_ASSIGN_NAND_D16,
    SAM9X7_NUM_EBI_ASSIGNMENTS,
};

static void sam9x7_update_cpu_reset_hold(SAM9X7State *s, bool force)
{
    CPUState *cs = CPU(&s->cpu);
    bool requested = s->vddcore_reset_active || s->power_reset_requested ||
                     s->core_reset_requested;

    if (requested && (!s->cpu_reset_hold_active || force)) {
        s->cpu_reset_hold_active = true;
        cpu_interrupt(cs, CPU_INTERRUPT_HALT);
    } else if (!requested && s->cpu_reset_hold_active) {
        s->cpu_reset_hold_active = false;
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HALT);
        cs->halted = 0;
        qemu_cpu_kick(cs);
    }
}

static void sam9x7_set_vddcore_reset(SAM9X7State *s, bool active)
{
    s->vddcore_reset_active = active;
    at91_rstc_set_core_reset_active(&s->rstc, active);

    /*
     * Board wiring can assert nRST before command-line devices have been
     * cold-plugged onto the SoC's child buses.  Delay the reset traversal
     * until every initial device exists so enter and exit visit the same
     * reset graph.
     */
    if (!s->vddcore_reset_ready || s->vddcore_reset_asserted == active) {
        return;
    }

    s->vddcore_reset_asserted = active;
    if (active) {
        resettable_assert_reset(OBJECT(s->vddcore_reset),
                                RESET_TYPE_WAKEUP);
    } else {
        resettable_release_reset(OBJECT(s->vddcore_reset),
                                 RESET_TYPE_WAKEUP);
    }
}

void sam9x7_machine_init_done(SAM9X7State *s)
{
    s->vddcore_reset_ready = true;
    sam9x7_set_vddcore_reset(s, s->vddcore_reset_active);
    sam9x7_update_cpu_reset_hold(s, true);
}

static void sam9x7_reconcile_vddcore_reset(SAM9X7State *s)
{
    sam9x7_set_vddcore_reset(s, s->power_reset_requested ||
                             at91_rstc_core_reset_requested(&s->rstc));
}

void sam9x7_prepare_machine_reset(SAM9X7State *s, ResetType type,
                                  bool core_reset)
{
    if (!s) {
        return;
    }

    if (core_reset) {
        /* The machine accepted the guest reset request. */
        sam9x7_set_vddcore_reset(s, true);
    } else if (type != RESET_TYPE_WAKEUP) {
        /* An explicit host reset cancels any retained reset interval. */
        sam9x7_set_vddcore_reset(s, false);
    }
}

static void sam9x7_set_reset(void *opaque, int n, int level)
{
    SAM9X7State *s = SAM9X7(opaque);

    switch (n) {
    case SAM9X7_RESET_POWER:
        s->power_reset_requested = !!level;
        qemu_set_irq(qdev_get_gpio_in_named(DEVICE(&s->rstc),
                                            "power-reset", 0), level);
        break;
    case SAM9X7_RESET_REQUEST:
        s->core_reset_requested = !!level;
        break;
    default:
        g_assert_not_reached();
    }

    if (s->power_reset_requested) {
        sam9x7_set_vddcore_reset(s, true);
    } else if (s->vddcore_reset_active &&
               !at91_rstc_core_reset_requested(&s->rstc)) {
        sam9x7_set_vddcore_reset(s, false);
    }
    sam9x7_update_cpu_reset_hold(s, false);
}

static void sam9x7_reset_exit(Object *obj, ResetType type)
{
    SAM9X7State *s = SAM9X7(obj);

    /* Reconstruct a physical input that remained asserted over cold reset. */
    sam9x7_reconcile_vddcore_reset(s);
    /* The CPU's reset phase clears HALT; restore an active reset hold. */
    sam9x7_update_cpu_reset_hold(s, true);
}

static int sam9x7_post_load(void *opaque, int version_id)
{
    SAM9X7State *s = SAM9X7(opaque);

    s->core_reset_requested = s->rstc.reset_request_level;
    s->power_reset_requested = s->rstc.power_reset_level;
    sam9x7_reconcile_vddcore_reset(s);
    sam9x7_update_cpu_reset_hold(s, true);
    return 0;
}

static const VMStateDescription sam9x7_vmstate = {
    .name = TYPE_SAM9X7,
    .version_id = 1,
    .minimum_version_id = 1,
    .priority = MIG_PRI_LOW,
    .post_load = sam9x7_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_END_OF_LIST()
    },
};

static void sam9x7_set_boot_remap(void *opaque, int n, int level)
{
    SAM9X7State *s = SAM9X7(opaque);

    memory_region_set_enabled(&s->boot_alias, !level);
    memory_region_set_enabled(&s->boot_sram_alias, level);
}

static void sam9x7_update_nand_assignment(SAM9X7State *s)
{
    memory_region_set_enabled(&s->nand_window,
                              s->nand_cs2_assigned &&
                              s->nand_d16_assigned);
}

static void sam9x7_set_ebi_assignment(void *opaque, int n, int level)
{
    SAM9X7State *s = SAM9X7(opaque);

    switch (n) {
    case SAM9X7_EBI_ASSIGN_DDR:
        memory_region_set_enabled(&s->ddr_window, level);
        break;
    case SAM9X7_EBI_ASSIGN_NAND_CS2:
        s->nand_cs2_assigned = level;
        sam9x7_update_nand_assignment(s);
        break;
    case SAM9X7_EBI_ASSIGN_NAND_D16:
        s->nand_d16_assigned = level;
        sam9x7_update_nand_assignment(s);
        break;
    default:
        g_assert_not_reached();
    }
}

static void sam9x7_uhphs_ohci_dma_error(void *opaque, dma_addr_t addr)
{
    at91_uhphs_ehci_record_dma_error(opaque, addr);
}

static void sam9x7_realize(DeviceState *dev, Error **errp)
{
    SAM9X7State *s = SAM9X7(dev);
    MemoryRegion *mr;
    static const hwaddr pit64b_base[] = {
        SAM9X7_PIT64B0_BASE,
        SAM9X7_PIT64B1_BASE,
    };
    static const unsigned int pit64b_irq[] = { 37, 58 };
    static const hwaddr pio_base[] = {
        SAM9X7_PIOA_BASE,
        SAM9X7_PIOB_BASE,
        SAM9X7_PIOC_BASE,
        SAM9X7_PIOD_BASE,
    };
    static const unsigned int pio_irq[] = { 2, 3, 4, 44 };
    static const hwaddr sdmmc_base[] = {
        SAM9X7_SDMMC0_BASE,
        SAM9X7_SDMMC1_BASE,
    };
    static const unsigned int sdmmc_irq[] = { 12, 26 };
    static const unsigned int gmac_irq[SAM9X7_NUM_GMAC_QUEUES] = {
        24, 60, 61, 62, 63, 64,
    };
    static const hwaddr mcan_base[SAM9X7_NUM_MCAN] = {
        SAM9X7_MCAN0_BASE,
        SAM9X7_MCAN1_BASE,
    };
    static const unsigned int mcan_irq[SAM9X7_NUM_MCAN][2] = {
        { 29, 68 },
        { 30, 69 },
    };
    unsigned int i;

    if (!s->memory) {
        error_setg(errp, TYPE_SAM9X7 " property 'memory' was not set");
        return;
    }
    if (!s->ddr_memory) {
        error_setg(errp, TYPE_SAM9X7 " property 'ddr-memory' was not set");
        return;
    }
    if (memory_region_size(s->ddr_memory) != SAM9X7_DDR_SIZE) {
        error_setg(errp, TYPE_SAM9X7
                   " property 'ddr-memory' must be 256 MiB");
        return;
    }

    /*
     * Leave the decode window enabled until the first reset so direct-boot
     * image loading can populate RAM.  SFR reset then drives the hardware or
     * direct-Linux assignment state.
     */
    memory_region_init_alias(&s->ddr_window, OBJECT(s),
                             "sam9x7.ddr-window", s->ddr_memory, 0,
                             SAM9X7_DDR_SIZE);
    memory_region_add_subregion(s->memory, SAM9X7_DDR_BASE,
                                &s->ddr_window);

    if (!qdev_realize(DEVICE(&s->cpu), NULL, errp)) {
        return;
    }

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->aic), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->aic), 0);
    memory_region_add_subregion(s->memory, SAM9X7_AIC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->aic), 0,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->aic), 1,
                       qdev_get_gpio_in(DEVICE(&s->cpu), ARM_CPU_FIQ));

    if (!qdev_realize(DEVICE(&s->sys_irq), NULL, errp)) {
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->sys_irq), 0,
                          qdev_get_gpio_in(DEVICE(&s->aic), 1));

    if (!qdev_realize(DEVICE(&s->ebi_irq), NULL, errp)) {
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->ebi_irq), 0,
                          qdev_get_gpio_in(DEVICE(&s->aic), 49));

    if (!qdev_realize(DEVICE(&s->uhphs_irq), NULL, errp)) {
        return;
    }
    qdev_connect_gpio_out(DEVICE(&s->uhphs_irq), 0,
                          qdev_get_gpio_in(DEVICE(&s->aic), 22));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sysc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sysc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_SYSCWP_BASE, mr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sckc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sckc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_SCKC_BASE, mr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->bsc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->bsc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_BSC_BASE, mr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pmc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pmc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_PMC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pmc), 0,
                       qdev_get_gpio_in(DEVICE(&s->sys_irq), 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rstc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rstc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_RSTC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rstc), 0,
                       qdev_get_gpio_in(DEVICE(&s->sys_irq), 1));
    qdev_connect_gpio_out_named(DEVICE(&s->rstc), "reset-request", 0,
        qdev_get_gpio_in_named(dev, SAM9X7_GPIO_RESET,
                               SAM9X7_RESET_REQUEST));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->shdwc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->shdwc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_SHDWC_BASE, mr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gpbr), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gpbr), 0);
    memory_region_add_subregion(s->memory, SAM9X7_GPBR_BASE, mr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wdt), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->wdt), 0);
    memory_region_add_subregion(s->memory, SAM9X7_WDT_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->wdt), 0,
                       qdev_get_gpio_in(DEVICE(&s->sys_irq), 2));
    qdev_connect_gpio_out_named(DEVICE(&s->wdt), "reset", 0,
        qdev_get_gpio_in_named(DEVICE(&s->rstc), "wdt-reset", 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtt), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rtt), 0);
    memory_region_add_subregion(s->memory, SAM9X7_RTT_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtt), 0,
                       qdev_get_gpio_in(DEVICE(&s->sys_irq), 5));
    qdev_connect_gpio_out_named(DEVICE(&s->rtt), "alarm", 0,
        qdev_get_gpio_in_named(DEVICE(&s->shdwc), "rtt-alarm", 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->rtc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->rtc), 0);
    /*
     * The RTC's 0x100-byte register aperture has holes occupied by other
     * system-controller blocks, including SYSCWP and WDT.
     */
    memory_region_add_subregion_overlap(s->memory, SAM9X7_RTC_BASE, mr, -1);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->rtc), 0,
                       qdev_get_gpio_in(DEVICE(&s->sys_irq), 6));
    qdev_connect_gpio_out_named(DEVICE(&s->rtc), "tamper-event", 0,
        qdev_get_gpio_in_named(DEVICE(&s->gpbr), "tamper-event", 0));
    qdev_connect_gpio_out_named(DEVICE(&s->rtc), "alarm", 0,
        qdev_get_gpio_in_named(DEVICE(&s->shdwc), "rtc-alarm", 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tcb), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->tcb), 0);
    memory_region_add_subregion(s->memory, SAM9X7_TCB_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->tcb), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 17));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tcb1), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->tcb1), 0);
    memory_region_add_subregion(s->memory, SAM9X7_TCB1_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->tcb1), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 45));

    /*
     * DS60001813E Table 16.1: requests 43-48 are the compare events of
     * channel 1 of each timer block, in CPA, CPB, CPC order for TC0 then
     * TC1.  The channel-1 lines are index 3, 4 and 5 of the block's
     * compare-request array.
     */
    /*
     * Requests 41 and 42 are the capture events of TC0 and TC1; the
     * capture trigger is the TIOA pin, which this board does not route.
     */
    qdev_connect_gpio_out_named(DEVICE(&s->tcb), "capture-request", 1,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 41));
    qdev_connect_gpio_out_named(DEVICE(&s->tcb1), "capture-request", 1,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 42));
    /* Requests 49 and 50 are the external-trigger events of the same
     * channels, which fire on the TIOA or TIOB pin ABETRG selects. */
    qdev_connect_gpio_out_named(DEVICE(&s->tcb), "etrg-request", 1,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 49));
    qdev_connect_gpio_out_named(DEVICE(&s->tcb1), "etrg-request", 1,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 50));

    for (i = 0; i < 3; i++) {
        qdev_connect_gpio_out_named(DEVICE(&s->tcb), "compare-request",
            3 + i, qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request",
                                          43 + i));
        qdev_connect_gpio_out_named(DEVICE(&s->tcb1), "compare-request",
            3 + i, qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request",
                                          46 + i));
    }

    object_property_set_link(OBJECT(&s->xdmac), "dma-memory",
                             OBJECT(s->memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->xdmac), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->xdmac), 0);
    memory_region_add_subregion(s->memory, SAM9X7_XDMAC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->xdmac), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 20));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->trng), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->trng), 0);
    memory_region_add_subregion(s->memory, SAM9X7_TRNG_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->trng), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 38));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->adc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->adc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_ADC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->adc), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 19));
    qdev_connect_gpio_out_named(DEVICE(&s->adc), "rx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 40));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->aes), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->aes), 0);
    memory_region_add_subregion(s->memory, SAM9X7_AES_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->aes), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 39));
    qdev_connect_gpio_out_named(DEVICE(&s->aes), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 32));
    qdev_connect_gpio_out_named(DEVICE(&s->aes), "rx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 33));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sha), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sha), 0);
    memory_region_add_subregion(s->memory, SAM9X7_SHA_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sha), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 41));
    qdev_connect_gpio_out_named(DEVICE(&s->sha), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 34));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->tdes), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->tdes), 0);
    memory_region_add_subregion(s->memory, SAM9X7_TDES_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->tdes), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 40));
    qdev_connect_gpio_out_named(DEVICE(&s->tdes), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 31));
    qdev_connect_gpio_out_named(DEVICE(&s->tdes), "rx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 30));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->i2smcc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->i2smcc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_I2SMCC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->i2smcc), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 34));
    qdev_connect_gpio_out_named(DEVICE(&s->i2smcc), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 36));
    qdev_connect_gpio_out_named(DEVICE(&s->i2smcc), "rx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 37));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->classd), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->classd), 0);
    memory_region_add_subregion(s->memory, SAM9X7_CLASSD_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->classd), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 42));
    qdev_connect_gpio_out_named(DEVICE(&s->classd), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 35));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pit), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pit), 0);
    memory_region_add_subregion(s->memory, SAM9X7_PIT_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pit), 0,
                       qdev_get_gpio_in(DEVICE(&s->sys_irq), 3));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->sfr), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sfr), 0);
    memory_region_add_subregion(s->memory, SAM9X7_SFR_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->sfr), 0,
                       qdev_get_gpio_in(DEVICE(&s->sys_irq), 4));

    object_property_set_link(OBJECT(&s->udphs), "dma-memory",
                             OBJECT(s->memory), &error_abort);
    qdev_connect_gpio_out_named(DEVICE(&s->udphs), "device-mode", 0,
        qdev_get_gpio_in_named(DEVICE(&s->uhphs_ehci), "device-mode", 0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->udphs), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->udphs), 0);
    g_assert(memory_region_size(mr) == SAM9X7_UDPHS_FIFO_SIZE);
    memory_region_add_subregion(s->memory, SAM9X7_UDPHS_FIFO_BASE, mr);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->udphs), 1);
    g_assert(memory_region_size(mr) == SAM9X7_UDPHS_SIZE);
    memory_region_add_subregion(s->memory, SAM9X7_UDPHS_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->udphs), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 23));

    object_property_set_bool(OBJECT(&s->uhphs_ehci), "companion-enable",
                             true, &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uhphs_ehci), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uhphs_ehci), 0);
    memory_region_add_subregion(&s->uhphs_ehci_window, 0, mr);
    memory_region_add_subregion(s->memory, SAM9X7_UHPHS_EHCI_BASE,
                                &s->uhphs_ehci_window);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uhphs_ehci), 0,
                       qdev_get_gpio_in(DEVICE(&s->uhphs_irq), 0));

    object_property_set_str(OBJECT(&s->uhphs_ohci), "masterbus",
                            s->uhphs_ehci.parent_obj.ehci.bus.qbus.name,
                            &error_abort);
    object_property_set_uint(OBJECT(&s->uhphs_ohci), "num-ports", 3,
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->uhphs_ohci), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->uhphs_ohci), 0);
    memory_region_add_subregion(&s->uhphs_ohci_window, 0, mr);
    memory_region_add_subregion(s->memory, SAM9X7_UHPHS_OHCI_BASE,
                                &s->uhphs_ohci_window);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->uhphs_ohci), 0,
                       qdev_get_gpio_in(DEVICE(&s->uhphs_irq), 1));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mpddrc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mpddrc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_MPDDRC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mpddrc), 0,
                       qdev_get_gpio_in(DEVICE(&s->ebi_irq), 0));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->pmecc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pmecc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_PMECC_BASE, mr);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pmecc), 1);
    memory_region_add_subregion(s->memory, SAM9X7_PMERRLOC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pmecc), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 48));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->smc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->smc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_SMC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->smc), 0,
                       qdev_get_gpio_in(DEVICE(&s->ebi_irq), 1));

    object_property_set_link(OBJECT(&s->nand), "pmecc", OBJECT(&s->pmecc),
                             &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->nand), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nand), 0);
    g_assert(memory_region_size(mr) == SAM9X7_NAND_SIZE);
    memory_region_init_alias(&s->nand_window, OBJECT(s),
                             "sam9x7.nand-window", mr, 0,
                             SAM9X7_NAND_SIZE);
    memory_region_set_enabled(&s->nand_window, false);
    memory_region_add_subregion(s->memory, SAM9X7_NAND_BASE,
                                &s->nand_window);
    qdev_connect_gpio_out_named(DEVICE(&s->sfr), AT91_SFR_GPIO_EBI_CS,
        AT91_SFR_EBI_CS1,
        qdev_get_gpio_in_named(DEVICE(s), "ebi-assignment",
                               SAM9X7_EBI_ASSIGN_DDR));
    qdev_connect_gpio_out_named(DEVICE(&s->sfr), AT91_SFR_GPIO_EBI_CS,
        AT91_SFR_EBI_CS2,
        qdev_get_gpio_in_named(DEVICE(s), "ebi-assignment",
                               SAM9X7_EBI_ASSIGN_NAND_CS2));
    qdev_connect_gpio_out_named(DEVICE(&s->sfr),
        AT91_SFR_GPIO_NFD0_ON_D16, 0,
        qdev_get_gpio_in_named(DEVICE(s), "ebi-assignment",
                               SAM9X7_EBI_ASSIGN_NAND_D16));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->ssc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->ssc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_SSC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->ssc), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 28));
    /* DS60001813E Table 16.1: XDMAC0 requests 38 and 39 are SSC TX/RX. */
    qdev_connect_gpio_out_named(DEVICE(&s->ssc), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 38));
    qdev_connect_gpio_out_named(DEVICE(&s->ssc), "rx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 39));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->qspi), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->qspi), 0);
    memory_region_add_subregion(s->memory, SAM9X7_QSPI_BASE, mr);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->qspi), 1);
    memory_region_add_subregion(s->memory, SAM9X7_QSPI_MEM_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->qspi), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 35));
    /* DS60001813E Table 16.1: XDMAC0 requests 26 and 27 are QSPI TX/RX. */
    qdev_connect_gpio_out_named(DEVICE(&s->qspi), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 26));
    qdev_connect_gpio_out_named(DEVICE(&s->qspi), "rx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 27));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->gmac), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->gmac), 0);
    memory_region_add_subregion(s->memory, SAM9X7_GMAC_BASE, mr);
    for (i = 0; i < ARRAY_SIZE(gmac_irq); i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->gmac), i,
                           qdev_get_gpio_in(DEVICE(&s->aic), gmac_irq[i]));
    }

    for (i = 0; i < ARRAY_SIZE(s->flexcom); i++) {
        AT91SPIState *spi = i < ARRAY_SIZE(s->spi) ? &s->spi[i] : NULL;

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->flexcom[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->flexcom[i]), 0);
        memory_region_add_subregion(s->memory, sam9x7_flexcom_base[i], mr);

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->usart[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->usart[i]), 0);
        memory_region_add_subregion(s->memory,
                                    sam9x7_flexcom_base[i] + 0x200, mr);

        if (spi) {
            if (!sysbus_realize(SYS_BUS_DEVICE(spi), errp)) {
                return;
            }
            mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(spi), 0);
            memory_region_add_subregion(s->memory,
                                        sam9x7_flexcom_base[i] + 0x400, mr);
        }

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->twi[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->twi[i]), 0);
        memory_region_add_subregion(s->memory,
                                    sam9x7_flexcom_base[i] + 0x600, mr);
        qdev_connect_gpio_out_named(DEVICE(&s->flexcom[i]),
                                    "usart-enabled", 0,
            qdev_get_gpio_in_named(DEVICE(&s->usart[i]),
                                   "flexcom-enabled", 0));
        if (spi) {
            qdev_connect_gpio_out_named(DEVICE(&s->flexcom[i]),
                                        "spi-enabled", 0,
                qdev_get_gpio_in_named(DEVICE(spi),
                                       "flexcom-enabled", 0));
        }
        qdev_connect_gpio_out_named(DEVICE(&s->flexcom[i]), "twi-enabled", 0,
            qdev_get_gpio_in_named(DEVICE(&s->twi[i]),
                                   "flexcom-enabled", 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usart[i]), 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]), "usart-irq", 0));
        if (spi) {
            sysbus_connect_irq(SYS_BUS_DEVICE(spi), 0,
                qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]), "spi-irq", 0));
        }
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->twi[i]), 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]), "twi-irq", 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->flexcom[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->aic),
                                            sam9x7_flexcom_pid[i]));
        qdev_connect_gpio_out_named(DEVICE(&s->usart[i]), "tx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                   "usart-tx-request", 0));
        qdev_connect_gpio_out_named(DEVICE(&s->usart[i]), "rx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                   "usart-rx-request", 0));
        if (spi) {
            qdev_connect_gpio_out_named(DEVICE(spi), "tx-request", 0,
                qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                       "spi-tx-request", 0));
            qdev_connect_gpio_out_named(DEVICE(spi), "rx-request", 0,
                qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                       "spi-rx-request", 0));
        }
        qdev_connect_gpio_out_named(DEVICE(&s->twi[i]), "tx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                   "twi-tx-request", 0));
        qdev_connect_gpio_out_named(DEVICE(&s->twi[i]), "rx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                   "twi-rx-request", 0));
        qdev_connect_gpio_out_named(DEVICE(&s->flexcom[i]), "tx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", i * 2));
        qdev_connect_gpio_out_named(DEVICE(&s->flexcom[i]), "rx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", i * 2 + 1));
    }

    for (i = 0; i < ARRAY_SIZE(s->sdmmc); i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->sdmmc[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->sdmmc[i]), 0);
        memory_region_add_subregion(s->memory, sdmmc_base[i], mr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->sdmmc[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->aic),
                                            sdmmc_irq[i]));
    }

    for (i = 0; i < ARRAY_SIZE(s->pio); i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->pio[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pio[i]), 0);
        memory_region_add_subregion(s->memory, pio_base[i], mr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->pio[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->aic), pio_irq[i]));
    }

    for (i = 0; i < ARRAY_SIZE(s->pit64b); i++) {
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->pit64b[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->pit64b[i]), 0);
        memory_region_add_subregion(s->memory, pit64b_base[i], mr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->pit64b[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->aic), pit64b_irq[i]));
    }

    /*
     * Keep one stable migration backing for both internal ROM arrays.  The
     * hardware exposes the boot code and ECC tables through distinct guest
     * address ranges below.
     */
    if (!memory_region_init_rom(&s->rom, OBJECT(dev), "sam9x7.rom",
                                SAM9X7_BOOT_ROM_SIZE +
                                SAM9X7_ECC_ROM_SIZE, errp)) {
        return;
    }
    sam9x7_init_pmecc_gf_tables(&s->rom);

    /* At reset the dedicated boot ROM is visible at address zero. */
    memory_region_init_alias(&s->boot_alias, OBJECT(dev),
                             "sam9x7.boot-rom-alias", &s->rom, 0,
                             SAM9X7_BOOT_ROM_SIZE);
    memory_region_add_subregion(s->memory, SAM9X7_BOOT_BASE, &s->boot_alias);

    memory_region_init_alias(&s->ecc_alias, OBJECT(dev),
                             "sam9x7.ecc-rom-alias", &s->rom,
                             SAM9X7_BOOT_ROM_SIZE,
                             SAM9X7_ECC_ROM_SIZE);
    memory_region_add_subregion(s->memory, SAM9X7_ECC_ROM_BASE,
                                &s->ecc_alias);

    if (!memory_region_init_ram(&s->sram0, OBJECT(dev), "sam9x7.sram0",
                                SAM9X7_SRAM0_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->memory, SAM9X7_SRAM0_BASE, &s->sram0);

    memory_region_init_alias(&s->boot_sram_alias, OBJECT(dev),
                             "sam9x7.boot-sram0-alias", &s->sram0, 0,
                             SAM9X7_SRAM0_SIZE);
    memory_region_set_enabled(&s->boot_sram_alias, false);
    memory_region_add_subregion_overlap(s->memory, SAM9X7_BOOT_BASE,
                                        &s->boot_sram_alias, 1);

    /* Both controllers address the same 64 KiB SRAM0 from offset zero. */
    for (i = 0; i < ARRAY_SIZE(s->mcan); i++) {
        object_property_set_link(OBJECT(&s->mcan[i]), "message-ram",
                                 OBJECT(&s->sram0), &error_abort);
        if (s->canbus[i]) {
            object_property_set_link(OBJECT(&s->mcan[i]), "canbus",
                                     OBJECT(s->canbus[i]), &error_abort);
        }
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->mcan[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mcan[i]), 0);
        memory_region_add_subregion(s->memory, mcan_base[i], mr);
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->mcan[i]), 0,
                           qdev_get_gpio_in(DEVICE(&s->aic),
                                            mcan_irq[i][0]));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->mcan[i]), 1,
                           qdev_get_gpio_in(DEVICE(&s->aic),
                                            mcan_irq[i][1]));
    }

    qdev_connect_gpio_out_named(DEVICE(&s->matrix), "cpu-remap", 0,
        qdev_get_gpio_in_named(dev, "boot-remap", 0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->matrix), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->matrix), 0);
    memory_region_add_subregion(s->memory, SAM9X7_MATRIX_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->matrix), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 21));

    /* SRAM1 backs the OTP controller's emulation mode. */
    if (!memory_region_init_ram(&s->sram1, OBJECT(dev), "sam9x7.sram1",
                                SAM9X7_SRAM1_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->memory, SAM9X7_SRAM1_BASE, &s->sram1);

    object_property_set_link(OBJECT(&s->otpc), "emulation-memory",
                             OBJECT(&s->sram1), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->otpc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->otpc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_OTPC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->otpc), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 46));

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->dbgu), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->dbgu), 0);
    memory_region_add_subregion(s->memory, SAM9X7_DBGU_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->dbgu), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 47));
    qdev_connect_gpio_out_named(DEVICE(&s->dbgu), "tx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 28));
    qdev_connect_gpio_out_named(DEVICE(&s->dbgu), "rx-request", 0,
        qdev_get_gpio_in_named(DEVICE(&s->xdmac), "request", 29));
}

static void sam9x7_vddcore_add(SAM9X7State *s, DeviceState *dev)
{
    resettable_container_add(s->vddcore_reset, OBJECT(dev));
}

static void sam9x7_init_vddcore_reset(SAM9X7State *s)
{
    Object *container = object_new(TYPE_RESETTABLE_CONTAINER);
    unsigned int i;

    object_property_add_child(OBJECT(s), "vddcore-reset", container);
    s->vddcore_reset = RESETTABLE_CONTAINER(container);
    object_unref(container);

    sam9x7_vddcore_add(s, DEVICE(&s->cpu));
    sam9x7_vddcore_add(s, DEVICE(&s->aic));
    sam9x7_vddcore_add(s, DEVICE(&s->pmc));
    sam9x7_vddcore_add(s, DEVICE(&s->wdt));
    sam9x7_vddcore_add(s, DEVICE(&s->pit));
    sam9x7_vddcore_add(s, DEVICE(&s->tcb));
    sam9x7_vddcore_add(s, DEVICE(&s->tcb1));
    sam9x7_vddcore_add(s, DEVICE(&s->xdmac));
    sam9x7_vddcore_add(s, DEVICE(&s->trng));
    sam9x7_vddcore_add(s, DEVICE(&s->adc));
    sam9x7_vddcore_add(s, DEVICE(&s->aes));
    sam9x7_vddcore_add(s, DEVICE(&s->sha));
    sam9x7_vddcore_add(s, DEVICE(&s->tdes));
    sam9x7_vddcore_add(s, DEVICE(&s->i2smcc));
    sam9x7_vddcore_add(s, DEVICE(&s->classd));
    sam9x7_vddcore_add(s, DEVICE(&s->sfr));
    sam9x7_vddcore_add(s, DEVICE(&s->udphs));
    sam9x7_vddcore_add(s, DEVICE(&s->uhphs_ehci));
    sam9x7_vddcore_add(s, DEVICE(&s->uhphs_ohci));
    sam9x7_vddcore_add(s, DEVICE(&s->mpddrc));
    sam9x7_vddcore_add(s, DEVICE(&s->pmecc));
    sam9x7_vddcore_add(s, DEVICE(&s->smc));
    sam9x7_vddcore_add(s, DEVICE(&s->ssc));
    sam9x7_vddcore_add(s, DEVICE(&s->qspi));
    sam9x7_vddcore_add(s, DEVICE(&s->gmac));
    sam9x7_vddcore_add(s, DEVICE(&s->matrix));
    sam9x7_vddcore_add(s, DEVICE(&s->otpc));
    sam9x7_vddcore_add(s, DEVICE(&s->dbgu));

    for (i = 0; i < ARRAY_SIZE(s->pit64b); i++) {
        sam9x7_vddcore_add(s, DEVICE(&s->pit64b[i]));
    }
    for (i = 0; i < ARRAY_SIZE(s->mcan); i++) {
        sam9x7_vddcore_add(s, DEVICE(&s->mcan[i]));
    }
    for (i = 0; i < ARRAY_SIZE(s->flexcom); i++) {
        sam9x7_vddcore_add(s, DEVICE(&s->flexcom[i]));
        sam9x7_vddcore_add(s, DEVICE(&s->usart[i]));
        if (i < ARRAY_SIZE(s->spi)) {
            sam9x7_vddcore_add(s, DEVICE(&s->spi[i]));
        }
        sam9x7_vddcore_add(s, DEVICE(&s->twi[i]));
    }
    for (i = 0; i < ARRAY_SIZE(s->sdmmc); i++) {
        sam9x7_vddcore_add(s, DEVICE(&s->sdmmc[i]));
    }
    for (i = 0; i < ARRAY_SIZE(s->pio); i++) {
        sam9x7_vddcore_add(s, DEVICE(&s->pio[i]));
    }
}

static void sam9x7_init(Object *obj)
{
    SAM9X7State *s = SAM9X7(obj);
    static const unsigned int pit64b_pid[] = { 37, 58 };
    static const unsigned int pio_pid[] = { 2, 3, 4, 44 };
    static const unsigned int sdmmc_pid[] = { 12, 26 };
    static const unsigned int mcan_pid[SAM9X7_NUM_MCAN] = { 29, 30 };
    static const uint32_t pio_valid_mask[] = {
        UINT32_MAX,
        0x07ffffff,
        UINT32_MAX,
        0x00007fff,
    };
    static const uint32_t pio_reset_pio_mask[] = {
        UINT32_MAX,
        0x07ffffff,
        0xfdffffff,
        0x00007ff3,
    };
    static const uint32_t pio_reset_pullup_mask[] = {
        UINT32_MAX,
        0x07ffffff,
        0xfdffffff,
        0x00007ff3,
    };
    static const uint32_t pio_reset_pulldown_mask[] = {
        0,
        0,
        0x02000000,
        0x0000000c,
    };
    unsigned int i;

    qdev_init_gpio_in_named(DEVICE(s), sam9x7_set_reset,
                            SAM9X7_GPIO_RESET, 2);
    qdev_init_gpio_in_named(DEVICE(s), sam9x7_set_boot_remap,
                            "boot-remap", 1);
    qdev_init_gpio_in_named(DEVICE(s), sam9x7_set_ebi_assignment,
                            "ebi-assignment",
                            SAM9X7_NUM_EBI_ASSIGNMENTS);

    memory_region_init(&s->uhphs_ohci_window, obj,
                       "sam9x7.uhphs-ohci-window",
                       SAM9X7_UHPHS_WINDOW_SIZE);
    memory_region_init(&s->uhphs_ehci_window, obj,
                       "sam9x7.uhphs-ehci-window",
                       SAM9X7_UHPHS_WINDOW_SIZE);

    object_initialize_child(obj, "cpu", &s->cpu,
                            ARM_CPU_TYPE_NAME("arm926"));
    object_property_set_bool(OBJECT(&s->cpu), "vfp", false, &error_abort);

    object_initialize_child(obj, "aic", &s->aic, TYPE_AT91_AIC5);
    object_initialize_child(obj, "dbgu", &s->dbgu, TYPE_AT91_DBGU);
    qdev_prop_set_uint32(DEVICE(&s->dbgu), "chip-id", SAM9X75_A1_CIDR);
    qdev_prop_set_uint32(DEVICE(&s->dbgu), "extension-id",
                         SAM9X75_D2G_EXID);

    object_initialize_child(obj, "sys-irq", &s->sys_irq, TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->sys_irq), "num-lines", 8,
                            &error_abort);
    object_initialize_child(obj, "ebi-irq", &s->ebi_irq, TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->ebi_irq), "num-lines", 2,
                            &error_abort);
    object_initialize_child(obj, "uhphs-irq", &s->uhphs_irq, TYPE_OR_IRQ);
    object_property_set_int(OBJECT(&s->uhphs_irq), "num-lines", 2,
                            &error_abort);

    s->main_xtal = qdev_init_clock_out(DEVICE(s), "main-xtal");
    s->slow_rc = qdev_init_clock_out(DEVICE(s), "slow-rc");
    s->slow_xtal = qdev_init_clock_out(DEVICE(s), "slow-xtal");
    clock_set_hz(s->main_xtal, 24000000);
    clock_set_hz(s->slow_rc, 32000);
    clock_set_hz(s->slow_xtal, 32768);

    object_initialize_child(obj, "sysc", &s->sysc, TYPE_AT91_SYSCWP);

    object_initialize_child(obj, "bsc", &s->bsc, TYPE_AT91_BSC);

    object_initialize_child(obj, "otpc", &s->otpc, TYPE_AT91_OTPC);

    object_initialize_child(obj, "matrix", &s->matrix, TYPE_AT91_MATRIX);

    object_initialize_child(obj, "sckc", &s->sckc, TYPE_AT91_SCKC);
    qdev_connect_clock_in(DEVICE(&s->sckc), "slow-rc", s->slow_rc);
    qdev_connect_clock_in(DEVICE(&s->sckc), "slow-xtal", s->slow_xtal);
    s->sckc.sysc = &s->sysc;

    object_initialize_child(obj, "pmc", &s->pmc, TYPE_AT91_PMC);
    qdev_connect_clock_in(DEVICE(&s->pmc), "main-xtal", s->main_xtal);
    qdev_connect_clock_in(DEVICE(&s->pmc), "td-slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "td-slck"));
    qdev_connect_clock_in(DEVICE(&s->pmc), "md-slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "md-slck"));
    qdev_connect_clock_in(DEVICE(&s->dbgu), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[47]"));
    qdev_connect_clock_in(DEVICE(&s->dbgu), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[47]"));

    object_initialize_child(obj, "rstc", &s->rstc, TYPE_AT91_RSTC);
    qdev_prop_set_bit(DEVICE(&s->rstc),
                      "general-reset-reports-backup", true);
    qdev_connect_clock_in(DEVICE(&s->rstc), "slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "md-slck"));
    s->rstc.sysc = &s->sysc;

    object_initialize_child(obj, "shdwc", &s->shdwc, TYPE_AT91_SHDWC);
    qdev_connect_clock_in(DEVICE(&s->shdwc), "slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "md-slck"));
    s->shdwc.sysc = &s->sysc;

    object_initialize_child(obj, "gpbr", &s->gpbr, TYPE_AT91_GPBR);
    s->gpbr.sysc = &s->sysc;
    s->gpbr.rstc = &s->rstc;

    object_initialize_child(obj, "wdt", &s->wdt, TYPE_AT91_WDT);
    qdev_connect_clock_in(DEVICE(&s->wdt), "slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "md-slck"));
    s->wdt.sysc = &s->sysc;

    object_initialize_child(obj, "pit", &s->pit, TYPE_AT91_PIT);
    qdev_connect_clock_in(DEVICE(&s->pit), "mck",
                          qdev_get_clock_out(DEVICE(&s->pmc), "mck"));
    s->pit.sysc = &s->sysc;

    object_initialize_child(obj, "rtt", &s->rtt, TYPE_AT91_RTT);
    qdev_connect_clock_in(DEVICE(&s->rtt), "slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "md-slck"));
    s->rtt.sysc = &s->sysc;

    object_initialize_child(obj, "rtc", &s->rtc, TYPE_AT91_RTC);
    s->rtc.sysc = &s->sysc;

    object_initialize_child(obj, "tcb", &s->tcb, TYPE_AT91_TCB);
    qdev_connect_clock_in(DEVICE(&s->tcb), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[17]"));
    qdev_connect_clock_in(DEVICE(&s->tcb), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[17]"));
    qdev_connect_clock_in(DEVICE(&s->tcb), "slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "md-slck"));

    object_initialize_child(obj, "tcb1", &s->tcb1, TYPE_AT91_TCB);
    qdev_connect_clock_in(DEVICE(&s->tcb1), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[45]"));
    qdev_connect_clock_in(DEVICE(&s->tcb1), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[45]"));
    qdev_connect_clock_in(DEVICE(&s->tcb1), "slck",
                          qdev_get_clock_out(DEVICE(&s->sckc), "md-slck"));

    object_initialize_child(obj, "xdmac", &s->xdmac, TYPE_AT91_XDMAC);
    qdev_prop_set_uint32(DEVICE(&s->xdmac), "version", 0x293);
    qdev_connect_clock_in(DEVICE(&s->xdmac), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[20]"));

    object_initialize_child(obj, "trng", &s->trng, TYPE_AT91_TRNG);
    qdev_prop_set_uint32(DEVICE(&s->trng), "version", 0x307);
    qdev_connect_clock_in(DEVICE(&s->trng), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[38]"));

    object_initialize_child(obj, "adc", &s->adc, TYPE_AT91_ADC);
    qdev_connect_clock_in(DEVICE(&s->adc), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[19]"));
    qdev_connect_clock_in(DEVICE(&s->adc), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[19]"));

    /*
     * Version registers measured on SAM9X75 Curiosity silicon; the models
     * default to a generic value that no part actually reports.
     */
    object_initialize_child(obj, "aes", &s->aes, TYPE_AT91_AES);
    qdev_prop_set_uint32(DEVICE(&s->aes), "version", 0x606);
    qdev_connect_clock_in(DEVICE(&s->aes), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[39]"));

    object_initialize_child(obj, "sha", &s->sha, TYPE_AT91_SHA);
    qdev_prop_set_uint32(DEVICE(&s->sha), "version", 0x604);
    qdev_connect_clock_in(DEVICE(&s->sha), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[41]"));

    object_initialize_child(obj, "tdes", &s->tdes, TYPE_AT91_TDES);
    qdev_prop_set_uint32(DEVICE(&s->tdes), "version", 0x803);
    qdev_connect_clock_in(DEVICE(&s->tdes), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[40]"));

    object_initialize_child(obj, "i2smcc", &s->i2smcc, TYPE_AT91_I2SMCC);
    qdev_prop_set_uint32(DEVICE(&s->i2smcc), "version", 0x110);
    qdev_connect_clock_in(DEVICE(&s->i2smcc), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[34]"));
    qdev_connect_clock_in(DEVICE(&s->i2smcc), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[34]"));

    object_initialize_child(obj, "classd", &s->classd, TYPE_AT91_CLASSD);
    qdev_connect_clock_in(DEVICE(&s->classd), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[42]"));
    qdev_connect_clock_in(DEVICE(&s->classd), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[42]"));

    object_initialize_child(obj, "sfr", &s->sfr, TYPE_AT91_SFR);

    object_initialize_child(obj, "udphs", &s->udphs, TYPE_AT91_UDPHS);
    qdev_connect_clock_in(DEVICE(&s->udphs), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[23]"));
    qdev_connect_clock_in(DEVICE(&s->udphs), "utmi",
                          qdev_get_clock_out(DEVICE(&s->pmc), "utmi"));

    object_initialize_child(obj, "uhphs-ehci", &s->uhphs_ehci,
                            TYPE_AT91_UHPHS_EHCI);
    qdev_connect_clock_in(DEVICE(&s->uhphs_ehci), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[22]"));
    qdev_connect_clock_in(DEVICE(&s->uhphs_ehci), "utmi",
                          qdev_get_clock_out(DEVICE(&s->pmc), "utmi"));
    object_initialize_child(obj, "uhphs-ohci", &s->uhphs_ohci,
                            TYPE_AT91_UHPHS_OHCI);
    qdev_connect_clock_in(DEVICE(&s->uhphs_ohci), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[22]"));
    qdev_connect_clock_in(DEVICE(&s->uhphs_ohci), "uhpck",
                          qdev_get_clock_out(DEVICE(&s->pmc), "uhpck"));
    s->uhphs_ohci.ohci.dma_error_cb = sam9x7_uhphs_ohci_dma_error;
    s->uhphs_ohci.ohci.dma_error_opaque = &s->uhphs_ehci;

    object_initialize_child(obj, "mpddrc", &s->mpddrc, TYPE_AT91_MPDDRC);

    object_initialize_child(obj, "pmecc", &s->pmecc, TYPE_AT91_PMECC);
    qdev_connect_clock_in(DEVICE(&s->pmecc), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[48]"));

    object_initialize_child(obj, "smc", &s->smc, TYPE_AT91_SMC);
    object_initialize_child(obj, "nand", &s->nand, TYPE_AT91_NAND);

    object_initialize_child(obj, "ssc", &s->ssc, TYPE_AT91_SSC);
    qdev_connect_clock_in(DEVICE(&s->ssc), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[28]"));
    qdev_connect_clock_in(DEVICE(&s->ssc), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[28]"));
    object_initialize_child(obj, "qspi", &s->qspi, TYPE_AT91_OSPI);
    qdev_connect_clock_in(DEVICE(&s->qspi), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[35]"));
    qdev_connect_clock_in(DEVICE(&s->qspi), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[35]"));

    object_initialize_child(obj, "gmac", &s->gmac, TYPE_CADENCE_GEM);
    /* Module ID measured on silicon; the model default is the Zynq value. */
    qdev_prop_set_uint32(DEVICE(&s->gmac), "revision", 0x4107010c);
    object_property_set_bool(OBJECT(&s->gmac), "dma-addr-64b", false,
                             &error_abort);
    object_property_set_bool(OBJECT(&s->gmac), "user-io", true, &error_abort);
    qdev_prop_set_uint8(DEVICE(&s->gmac), "phy-addr", 1);
    qdev_prop_set_uint32(DEVICE(&s->gmac), "phy-id", 0x00221650);
    qdev_prop_set_uint8(DEVICE(&s->gmac), "num-priority-queues",
                       SAM9X7_NUM_GMAC_QUEUES);

    for (i = 0; i < ARRAY_SIZE(s->mcan); i++) {
        g_autofree char *name = g_strdup_printf("mcan[%u]", i);
        g_autofree char *pclk_name =
            g_strdup_printf("pclk[%u]", mcan_pid[i]);
        g_autofree char *gclk_name =
            g_strdup_printf("gclk[%u]", mcan_pid[i]);

        object_initialize_child(obj, name, &s->mcan[i], TYPE_BOSCH_M_CAN);
        qdev_prop_set_uint32(DEVICE(&s->mcan[i]), "dbtp-mask", 0x009f1ff7);
        qdev_prop_set_bit(DEVICE(&s->mcan[i]), "tsu-destructive-read", true);
        qdev_prop_set_bit(DEVICE(&s->mcan[i]), "rwd-unprotected", true);
        qdev_connect_clock_in(DEVICE(&s->mcan[i]), "hclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->mcan[i]), "cclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), gclk_name));
    }

    for (i = 0; i < ARRAY_SIZE(s->flexcom); i++) {
        AT91SPIState *spi = i < ARRAY_SIZE(s->spi) ? &s->spi[i] : NULL;
        g_autofree char *flexcom_name =
            g_strdup_printf("flexcom[%u]", i);
        g_autofree char *usart_name = g_strdup_printf("usart[%u]", i);
        g_autofree char *spi_name = g_strdup_printf("spi[%u]", i);
        g_autofree char *twi_name = g_strdup_printf("twi[%u]", i);
        g_autofree char *bus_name = g_strdup_printf("i2c%u", i);
        g_autofree char *pclk_name =
            g_strdup_printf("pclk[%u]", sam9x7_flexcom_pid[i]);
        g_autofree char *gclk_name =
            g_strdup_printf("gclk[%u]", sam9x7_flexcom_pid[i]);

        object_initialize_child(obj, flexcom_name, &s->flexcom[i],
                                TYPE_AT91_FLEXCOM);
        object_initialize_child(obj, usart_name, &s->usart[i],
                                TYPE_AT91_USART);
        if (spi) {
            object_initialize_child(obj, spi_name, spi, TYPE_AT91_SPI);
            qdev_prop_set_uint8(DEVICE(spi), "num-cs",
                                sam9x7_flexcom_spi_num_cs[i]);
        }
        object_initialize_child(obj, twi_name, &s->twi[i], TYPE_AT91_TWI);
        at91_flexcom_set_children(&s->flexcom[i], &s->usart[i], spi,
                                 &s->twi[i]);
        qdev_connect_clock_in(DEVICE(&s->usart[i]), "pclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->usart[i]), "gclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), gclk_name));
        if (spi) {
            qdev_connect_clock_in(DEVICE(spi), "pclk",
                                 qdev_get_clock_out(DEVICE(&s->pmc),
                                                    pclk_name));
            qdev_connect_clock_in(DEVICE(spi), "gclk",
                                 qdev_get_clock_out(DEVICE(&s->pmc),
                                                    gclk_name));
        }
        qdev_prop_set_string(DEVICE(&s->twi[i]), "bus-name", bus_name);
        qdev_connect_clock_in(DEVICE(&s->twi[i]), "pclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->twi[i]), "gclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), gclk_name));
    }

    for (i = 0; i < ARRAY_SIZE(s->sdmmc); i++) {
        g_autofree char *name = g_strdup_printf("sdmmc[%u]", i);
        g_autofree char *pclk_name =
            g_strdup_printf("pclk[%u]", sdmmc_pid[i]);
        g_autofree char *gclk_name =
            g_strdup_printf("gclk[%u]", sdmmc_pid[i]);

        object_initialize_child(obj, name, &s->sdmmc[i], TYPE_AT91_SDHCI);
        qdev_connect_clock_in(DEVICE(&s->sdmmc[i]), "hclock",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->sdmmc[i]), "gclock",
                             qdev_get_clock_out(DEVICE(&s->pmc), gclk_name));
    }

    for (i = 0; i < ARRAY_SIZE(s->pio); i++) {
        g_autofree char *name = g_strdup_printf("pio[%u]", i);
        g_autofree char *pclk_name =
            g_strdup_printf("pclk[%u]", pio_pid[i]);

        object_initialize_child(obj, name, &s->pio[i], TYPE_AT91_PIO);
        qdev_prop_set_uint32(DEVICE(&s->pio[i]), "valid-mask",
                            pio_valid_mask[i]);
        qdev_prop_set_uint32(DEVICE(&s->pio[i]), "reset-pio-mask",
                            pio_reset_pio_mask[i]);
        qdev_prop_set_uint32(DEVICE(&s->pio[i]), "reset-pullup-mask",
                            pio_reset_pullup_mask[i]);
        qdev_prop_set_uint32(DEVICE(&s->pio[i]), "reset-pulldown-mask",
                            pio_reset_pulldown_mask[i]);
        qdev_connect_clock_in(DEVICE(&s->pio[i]), "pclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->pio[i]), "slck",
                             qdev_get_clock_out(DEVICE(&s->sckc),
                                                "md-slck"));
    }

    for (i = 0; i < ARRAY_SIZE(s->pit64b); i++) {
        g_autofree char *name = g_strdup_printf("pit64b[%u]", i);
        g_autofree char *pclk_name =
            g_strdup_printf("pclk[%u]", pit64b_pid[i]);
        g_autofree char *gclk_name =
            g_strdup_printf("gclk[%u]", pit64b_pid[i]);

        object_initialize_child(obj, name, &s->pit64b[i], TYPE_AT91_PIT64B);
        qdev_connect_clock_in(DEVICE(&s->pit64b[i]), "pclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->pit64b[i]), "gclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), gclk_name));
    }

    sam9x7_init_vddcore_reset(s);
}

static const Property sam9x7_properties[] = {
    DEFINE_PROP_LINK("memory", SAM9X7State, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
    DEFINE_PROP_LINK("ddr-memory", SAM9X7State, ddr_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_LINK("canbus0", SAM9X7State, canbus[0], TYPE_CAN_BUS,
                     CanBusState *),
    DEFINE_PROP_LINK("canbus1", SAM9X7State, canbus[1], TYPE_CAN_BUS,
                     CanBusState *),
};

static void sam9x7_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 SoC";
    dc->realize = sam9x7_realize;
    dc->vmsd = &sam9x7_vmstate;
    dc->user_creatable = false;
    device_class_set_props(dc, sam9x7_properties);
    rc->phases.exit = sam9x7_reset_exit;
}

static const TypeInfo sam9x7_info = {
    .name = TYPE_SAM9X7,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SAM9X7State),
    .instance_init = sam9x7_init,
    .class_init = sam9x7_class_init,
};

static void sam9x7_register_types(void)
{
    type_register_static(&sam9x7_info);
}

type_init(sam9x7_register_types)
