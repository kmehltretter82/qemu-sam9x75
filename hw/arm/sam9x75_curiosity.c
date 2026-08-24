/*
 * Microchip SAM9X75 Curiosity board
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/arm/boot.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/sam9x7.h"
#include "hw/core/boards.h"
#include "hw/core/or-irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "hw/input/stellaris_gamepad.h"
#include "hw/misc/led.h"
#include "hw/misc/mcp16502.h"
#include "hw/sd/sd.h"
#include "hw/sensor/pac1934.h"
#include "hw/ssi/ssi.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qobject/qlist.h"
#include "ui/input.h"
#include "net/net.h"
#include "system/address-spaces.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "system/qtest.h"
#include "system/system.h"
#include "target/arm/cpu-qom.h"

static struct arm_boot_info sam9x75_curiosity_boot_info;

static bool sam9x75_curiosity_attach_sd(SAM9X7State *soc,
                                        unsigned int unit)
{
    DriveInfo *dinfo = drive_get(IF_SD, 0, unit);
    DeviceState *card;

    if (!dinfo) {
        return false;
    }

    card = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                            &error_fatal);
    qdev_realize_and_unref(card, soc->sdmmc[unit].bus, &error_fatal);
    return true;
}

static void sam9x75_curiosity_create_controls(MachineState *machine,
                                               SAM9X7State *soc,
                                               I2CSlave *pmic)
{
    static const int keycodes[] = {
        Q_KEY_CODE_0, Q_KEY_CODE_W, Q_KEY_CODE_R, Q_KEY_CODE_S,
    };
    QList *keycode_list = qlist_new();
    DeviceState *buttons;
    DeviceState *wake_sources;
    LEDState *led;
    qemu_irq wkup;
    unsigned int i;

    led = led_create_simple(OBJECT(machine), GPIO_POLARITY_ACTIVE_HIGH,
                            LED_COLOR_RED, "RGB LED red");
    qdev_connect_gpio_out(DEVICE(&soc->pio[2]), 14,
                          qdev_get_gpio_in(DEVICE(led), 0));
    led = led_create_simple(OBJECT(machine), GPIO_POLARITY_ACTIVE_HIGH,
                            LED_COLOR_BLUE, "RGB LED blue");
    qdev_connect_gpio_out(DEVICE(&soc->pio[2]), 20,
                          qdev_get_gpio_in(DEVICE(led), 0));
    led = led_create_simple(OBJECT(machine), GPIO_POLARITY_ACTIVE_HIGH,
                            LED_COLOR_GREEN, "RGB LED green");
    qdev_connect_gpio_out(DEVICE(&soc->pio[2]), 21,
                          qdev_get_gpio_in(DEVICE(led), 0));

    /*
     * Keyboard 0/W/R/S operate SW1 USER, SW2 WKUP, SW3 RESET and SW4
     * START respectively.  All four board switches are active-low.
     */
    buttons = qdev_new(TYPE_STELLARIS_GAMEPAD);
    object_property_add_child(OBJECT(machine), "buttons", OBJECT(buttons));
    for (i = 0; i < ARRAY_SIZE(keycodes); i++) {
        qlist_append_int(keycode_list, keycodes[i]);
    }
    qdev_prop_set_array(buttons, "keycodes", keycode_list);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(buttons), &error_fatal);

    qemu_set_irq(qdev_get_gpio_in(DEVICE(&soc->pio[2]), 9), 1);
    qdev_connect_gpio_out(buttons, 0,
        qemu_irq_invert(qdev_get_gpio_in(DEVICE(&soc->pio[2]), 9)));
    qdev_connect_gpio_out(buttons, 2,
        qemu_irq_invert(qdev_get_gpio_in_named(DEVICE(&soc->rstc),
                                                "nrst", 0)));
    qdev_connect_gpio_out(buttons, 3,
        qemu_irq_invert(qdev_get_gpio_in_named(DEVICE(pmic),
                                                "nstart", 0)));

    /* SW2 and the PMIC's nSTRTO open-drain output share WKUP0. */
    wake_sources = qdev_new(TYPE_OR_IRQ);
    object_property_add_child(OBJECT(machine), "wake-sources",
                              OBJECT(wake_sources));
    qdev_prop_set_uint16(wake_sources, "num-lines", 2);
    qdev_realize_and_unref(wake_sources, NULL, &error_fatal);
    wkup = qdev_get_gpio_in_named(DEVICE(&soc->shdwc), "wkup", 0);
    qemu_set_irq(wkup, 1);
    qdev_connect_gpio_out(wake_sources, 0, qemu_irq_invert(wkup));
    qdev_connect_gpio_out(buttons, 1,
                          qdev_get_gpio_in(wake_sources, 0));
    qdev_connect_gpio_out_named(DEVICE(pmic), "nstrto", 0,
                                qdev_get_gpio_in(wake_sources, 1));

    /* The dedicated SHDN output is the PMIC's PWRHLD input. */
    qdev_connect_gpio_out_named(DEVICE(&soc->shdwc), "shdn", 0,
        qdev_get_gpio_in_named(DEVICE(pmic), "pwrhld", 0));

    /* U8's active-low open-drain interrupt output is pulled up on PA12. */
    qemu_set_irq(qdev_get_gpio_in(DEVICE(&soc->pio[0]), 12), 1);
    qdev_connect_gpio_out_named(DEVICE(pmic), "ninto", 0,
        qemu_irq_invert(qdev_get_gpio_in(DEVICE(&soc->pio[0]), 12)));
}

static void sam9x75_curiosity_init(MachineState *machine)
{
    MemoryRegion *sysmem = get_system_memory();
    SAM9X7State *soc;
    DeviceState *qspi_flash;
    I2CSlave *pmic;
    I2CSlave *power_monitor;
    DriveInfo *nand_dinfo;
    DriveInfo *qspi_dinfo;

    if (machine->ram_size != SAM9X7_DDR_SIZE) {
        error_report("sam9x75-curiosity requires 256 MiB of RAM");
        exit(EXIT_FAILURE);
    }

    if (machine->firmware) {
        error_report("-bios is not supported until the SAM9X75 RomBOOT "
                     "path is implemented");
        exit(EXIT_FAILURE);
    }

    memory_region_add_subregion(sysmem, SAM9X7_DDR_BASE, machine->ram);

    soc = SAM9X7(object_new(TYPE_SAM9X7));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(soc));
    object_unref(OBJECT(soc));

    object_property_set_link(OBJECT(soc), "memory", OBJECT(sysmem),
                             &error_abort);
    qdev_prop_set_chr(DEVICE(&soc->dbgu), "chardev", serial_hd(0));
    qemu_configure_nic_device(DEVICE(&soc->gmac), true, NULL);
    qemu_macaddr_default_if_unset(&soc->gmac.conf.macaddr);
    nand_dinfo = drive_get(IF_MTD, 0, 0);
    if (nand_dinfo) {
        qdev_prop_set_drive_err(DEVICE(&soc->nand), "drive",
                                blk_by_legacy_dinfo(nand_dinfo),
                                &error_fatal);
    }

    /* U6 is a Microchip SST26VF064BEUI 64-Mbit SQI NOR flash. */
    qspi_flash = qdev_new("sst26vf064beui");
    qdev_prop_set_macaddr(qspi_flash, "eui48", soc->gmac.conf.macaddr.a);
    qspi_dinfo = drive_get(IF_MTD, 0, 1);
    if (qspi_dinfo) {
        qdev_prop_set_drive_err(qspi_flash, "drive",
                                blk_by_legacy_dinfo(qspi_dinfo),
                                &error_fatal);
    }
    qdev_realize_and_unref(qspi_flash, BUS(soc->qspi.spi), &error_fatal);
    qdev_connect_gpio_out_named(DEVICE(&soc->qspi), "cs", 0,
        qdev_get_gpio_in_named(qspi_flash, SSI_GPIO_CS, 0));

    sysbus_realize(SYS_BUS_DEVICE(soc), &error_fatal);

    /* U8 is the board's MCP16502TAB-E/S8B power-management IC. */
    pmic = i2c_slave_new(TYPE_MCP16502_AB, 0x5b);
    object_property_add_child(OBJECT(machine), "mcp16502", OBJECT(pmic));
    i2c_slave_realize_and_unref(pmic, soc->twi[6].bus, &error_fatal);

    /* U12 measures the four main rails through 10-milliohm shunts. */
    power_monitor = i2c_slave_new(TYPE_PAC1934, 0x10);
    object_property_add_child(OBJECT(machine), "pac1934",
                              OBJECT(power_monitor));
    object_property_set_int(OBJECT(power_monitor), "vbus1-millivolts", 3300,
                            &error_abort);
    object_property_set_int(OBJECT(power_monitor), "vbus2-millivolts", 1150,
                            &error_abort);
    object_property_set_int(OBJECT(power_monitor), "vbus3-millivolts", 1150,
                            &error_abort);
    object_property_set_int(OBJECT(power_monitor), "vbus4-millivolts", 1350,
                            &error_abort);
    i2c_slave_realize_and_unref(power_monitor, soc->twi[7].bus,
                                &error_fatal);

    /* PB18 is the PAC1934's bidirectional SLOW/ALERT board signal. */
    qdev_connect_gpio_out(DEVICE(&soc->pio[1]), 18,
        qdev_get_gpio_in_named(DEVICE(power_monitor), "slow", 0));
    qdev_connect_gpio_out_named(DEVICE(power_monitor), "alert", 0,
        qdev_get_gpio_in(DEVICE(&soc->pio[1]), 18));

    sam9x75_curiosity_create_controls(machine, soc, pmic);

    if (sam9x75_curiosity_attach_sd(soc, 0)) {
        /* The Curiosity card-detect switch drives PA23 low when inserted. */
        qemu_set_irq(qdev_get_gpio_in(DEVICE(&soc->pio[0]), 23), 0);
    }
    sam9x75_curiosity_attach_sd(soc, 1);

    /* The populated NAND drives its active-high ready signal onto PD14. */
    qemu_set_irq(qdev_get_gpio_in(DEVICE(&soc->pio[3]), 14), 1);

    sam9x75_curiosity_boot_info = (struct arm_boot_info) {
        .loader_start = SAM9X7_DDR_BASE,
        .ram_size = machine->ram_size,
        .board_id = -1,
    };

    if (!qtest_enabled()) {
        arm_load_kernel(&soc->cpu, machine, &sam9x75_curiosity_boot_info);
    }
}

static void sam9x75_curiosity_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("arm926"),
        NULL,
    };

    mc->desc = "Microchip SAM9X75 Curiosity (ARM926EJ-S)";
    mc->init = sam9x75_curiosity_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 256 * MiB;
    mc->default_ram_id = "sam9x75-curiosity.ddr";
    mc->max_cpus = 1;
    mc->default_cpus = 1;
}

DEFINE_MACHINE_ARM("sam9x75-curiosity", sam9x75_curiosity_machine_init)
