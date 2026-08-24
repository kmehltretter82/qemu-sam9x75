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

bool sam9x7_core_reset_requested(const SAM9X7State *s)
{
    return s && s->core_reset_requested;
}

static void sam9x7_set_reset(void *opaque, int n, int level)
{
    SAM9X7State *s = SAM9X7(opaque);
    CPUState *cs = CPU(&s->cpu);

    switch (n) {
    case SAM9X7_RESET_POWER:
        qemu_set_irq(qdev_get_gpio_in_named(DEVICE(&s->rstc),
                                            "power-reset", 0), level);
        if (level) {
            cpu_interrupt(cs, CPU_INTERRUPT_HALT);
        } else {
            /* Release a CPU held while the PMIC's nRSTO was asserted. */
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HALT);
            cs->halted = 0;
            qemu_cpu_kick(cs);
        }
        break;
    case SAM9X7_RESET_REQUEST:
        s->core_reset_requested = !!level;
        break;
    default:
        g_assert_not_reached();
    }
}

static void sam9x7_set_boot_remap(void *opaque, int n, int level)
{
    SAM9X7State *s = SAM9X7(opaque);

    memory_region_set_enabled(&s->boot_alias, !level);
    memory_region_set_enabled(&s->boot_sram_alias, level);
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
    unsigned int i;

    if (!s->memory) {
        error_setg(errp, TYPE_SAM9X7 " property 'memory' was not set");
        return;
    }

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

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->mpddrc), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->mpddrc), 0);
    memory_region_add_subregion(s->memory, SAM9X7_MPDDRC_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->mpddrc), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 49));

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

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->nand), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->nand), 0);
    memory_region_add_subregion(s->memory, SAM9X7_NAND_BASE, mr);

    if (!sysbus_realize(SYS_BUS_DEVICE(&s->qspi), errp)) {
        return;
    }
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->qspi), 0);
    memory_region_add_subregion(s->memory, SAM9X7_QSPI_BASE, mr);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->qspi), 1);
    memory_region_add_subregion(s->memory, SAM9X7_QSPI_MEM_BASE, mr);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->qspi), 0,
                       qdev_get_gpio_in(DEVICE(&s->aic), 35));

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

        if (!sysbus_realize(SYS_BUS_DEVICE(&s->spi[i]), errp)) {
            return;
        }
        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->spi[i]), 0);
        memory_region_add_subregion(s->memory,
                                    sam9x7_flexcom_base[i] + 0x400, mr);

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
        qdev_connect_gpio_out_named(DEVICE(&s->flexcom[i]), "spi-enabled", 0,
            qdev_get_gpio_in_named(DEVICE(&s->spi[i]),
                                   "flexcom-enabled", 0));
        qdev_connect_gpio_out_named(DEVICE(&s->flexcom[i]), "twi-enabled", 0,
            qdev_get_gpio_in_named(DEVICE(&s->twi[i]),
                                   "flexcom-enabled", 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->usart[i]), 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]), "usart-irq", 0));
        sysbus_connect_irq(SYS_BUS_DEVICE(&s->spi[i]), 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]), "spi-irq", 0));
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
        qdev_connect_gpio_out_named(DEVICE(&s->spi[i]), "tx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                   "spi-tx-request", 0));
        qdev_connect_gpio_out_named(DEVICE(&s->spi[i]), "rx-request", 0,
            qdev_get_gpio_in_named(DEVICE(&s->flexcom[i]),
                                   "spi-rx-request", 0));
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

    if (!memory_region_init_rom(&s->rom, OBJECT(dev), "sam9x7.rom",
                                SAM9X7_ROM_SIZE, errp)) {
        return;
    }
    memory_region_add_subregion(s->memory, SAM9X7_ROM_BASE, &s->rom);

    /* At reset the internal ROM is visible through the boot window. */
    memory_region_init_alias(&s->boot_alias, OBJECT(dev),
                             "sam9x7.boot-rom-alias", &s->rom, 0,
                             SAM9X7_ROM_SIZE);
    memory_region_add_subregion(s->memory, SAM9X7_BOOT_BASE, &s->boot_alias);

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

static void sam9x7_init(Object *obj)
{
    SAM9X7State *s = SAM9X7(obj);
    static const unsigned int pit64b_pid[] = { 37, 58 };
    static const unsigned int pio_pid[] = { 2, 3, 4, 44 };
    static const unsigned int sdmmc_pid[] = { 12, 26 };
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

    object_initialize_child(obj, "xdmac", &s->xdmac, TYPE_AT91_XDMAC);
    qdev_connect_clock_in(DEVICE(&s->xdmac), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[20]"));

    object_initialize_child(obj, "trng", &s->trng, TYPE_AT91_TRNG);
    qdev_connect_clock_in(DEVICE(&s->trng), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[38]"));

    object_initialize_child(obj, "aes", &s->aes, TYPE_AT91_AES);
    qdev_connect_clock_in(DEVICE(&s->aes), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[39]"));

    object_initialize_child(obj, "sha", &s->sha, TYPE_AT91_SHA);
    qdev_connect_clock_in(DEVICE(&s->sha), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[41]"));

    object_initialize_child(obj, "tdes", &s->tdes, TYPE_AT91_TDES);
    qdev_connect_clock_in(DEVICE(&s->tdes), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[40]"));

    object_initialize_child(obj, "i2smcc", &s->i2smcc, TYPE_AT91_I2SMCC);
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

    object_initialize_child(obj, "mpddrc", &s->mpddrc, TYPE_AT91_MPDDRC);

    object_initialize_child(obj, "pmecc", &s->pmecc, TYPE_AT91_PMECC);
    qdev_connect_clock_in(DEVICE(&s->pmecc), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[48]"));

    object_initialize_child(obj, "smc", &s->smc, TYPE_AT91_SMC);
    object_initialize_child(obj, "nand", &s->nand, TYPE_AT91_NAND);

    object_initialize_child(obj, "qspi", &s->qspi, TYPE_AT91_OSPI);
    qdev_connect_clock_in(DEVICE(&s->qspi), "pclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "pclk[35]"));
    qdev_connect_clock_in(DEVICE(&s->qspi), "gclk",
                          qdev_get_clock_out(DEVICE(&s->pmc), "gclk[35]"));

    object_initialize_child(obj, "gmac", &s->gmac, TYPE_CADENCE_GEM);
    qdev_prop_set_uint8(DEVICE(&s->gmac), "phy-addr", 1);
    qdev_prop_set_uint32(DEVICE(&s->gmac), "phy-id", 0x00221650);
    qdev_prop_set_uint8(DEVICE(&s->gmac), "num-priority-queues",
                       SAM9X7_NUM_GMAC_QUEUES);

    for (i = 0; i < ARRAY_SIZE(s->flexcom); i++) {
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
        object_initialize_child(obj, spi_name, &s->spi[i], TYPE_AT91_SPI);
        object_initialize_child(obj, twi_name, &s->twi[i], TYPE_AT91_TWI);
        at91_flexcom_set_children(&s->flexcom[i], &s->usart[i], &s->spi[i],
                                 &s->twi[i]);
        qdev_connect_clock_in(DEVICE(&s->usart[i]), "pclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->usart[i]), "gclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), gclk_name));
        qdev_connect_clock_in(DEVICE(&s->spi[i]), "pclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), pclk_name));
        qdev_connect_clock_in(DEVICE(&s->spi[i]), "gclk",
                             qdev_get_clock_out(DEVICE(&s->pmc), gclk_name));
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
}

static const Property sam9x7_properties[] = {
    DEFINE_PROP_LINK("memory", SAM9X7State, memory, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void sam9x7_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip SAM9X7 SoC";
    dc->realize = sam9x7_realize;
    dc->user_creatable = false;
    device_class_set_props(dc, sam9x7_properties);
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
