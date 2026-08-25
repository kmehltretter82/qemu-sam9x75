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
#include "hw/core/loader.h"
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
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
#include "qobject/qlist.h"
#include "ui/input.h"
#include "net/net.h"
#include "system/address-spaces.h"
#include "system/block-backend.h"
#include "system/blockdev.h"
#include "system/qtest.h"
#include "system/reset.h"
#include "system/system.h"
#include "target/arm/cpu-qom.h"

#define TYPE_SAM9X75_CURIOSITY_MACHINE \
    MACHINE_TYPE_NAME("sam9x75-curiosity")
OBJECT_DECLARE_SIMPLE_TYPE(SAM9X75CuriosityMachineState,
                           SAM9X75_CURIOSITY_MACHINE)

typedef enum SAM9X75PAC1934Route {
    SAM9X75_PAC1934_ROUTE_SOC,
    SAM9X75_PAC1934_ROUTE_USB,
    SAM9X75_PAC1934_ROUTE_OFF,
} SAM9X75PAC1934Route;

typedef enum SAM9X75M2Interface {
    SAM9X75_M2_INTERFACE_SDIO,
    SAM9X75_M2_INTERFACE_SPI,
} SAM9X75M2Interface;

struct SAM9X75CuriosityMachineState {
    MachineState parent_obj;

    bool nand_cs;
    bool qspi_cs;
    bool ethernet_25mhz;
    bool otpc_write_enable;
    char *otpc_drive;
    SAM9X75PAC1934Route pac1934_route;
    SAM9X75M2Interface m2_interface;
    CanBusState *canbus[SAM9X7_NUM_MCAN];
    SAM9X7State *soc;
    struct arm_boot_info boot_info;
};

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

static void sam9x75_curiosity_load_rom(MachineState *machine,
                                        SAM9X7State *soc)
{
    g_autofree char *filename = NULL;
    Error *err = NULL;
    int64_t size;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, machine->firmware);
    if (!filename) {
        error_report("Could not find SAM9X7 ROM image '%s'",
                     machine->firmware);
        exit(EXIT_FAILURE);
    }

    size = get_image_size(filename, &err);
    if (size < 0) {
        error_report_err(err);
        exit(EXIT_FAILURE);
    }
    if (size != SAM9X7_ROM_SIZE) {
        error_report("-bios image '%s' is %" PRId64 " bytes; SAM9X75 "
                     "requires a complete 176 KiB ROM image",
                     machine->firmware, size);
        exit(EXIT_FAILURE);
    }
    if (load_image_mr(filename, &soc->rom) != size) {
        error_report("Failed to load SAM9X7 ROM image '%s'",
                     machine->firmware);
        exit(EXIT_FAILURE);
    }
}

static bool sam9x75_curiosity_attach_spi_sd(SAM9X7State *soc,
                                            unsigned int unit)
{
    DriveInfo *dinfo = drive_get(IF_SD, 0, unit);
    DeviceState *adapter;
    DeviceState *card;

    if (!dinfo) {
        return false;
    }

    /* J24 pins 2-3 route FLEXCOM4 IO4/NPCS1 to M.2 pin 15. */
    adapter = qdev_new("ssi-sd");
    qdev_prop_set_uint8(adapter, "cs", 1);
    qdev_realize_and_unref(adapter, BUS(soc->spi[4].bus), &error_fatal);
    qdev_connect_gpio_out_named(DEVICE(&soc->spi[4]), "cs", 1,
        qdev_get_gpio_in_named(adapter, SSI_GPIO_CS, 0));

    card = qdev_new(TYPE_SD_CARD_SPI);
    qdev_prop_set_drive_err(card, "drive", blk_by_legacy_dinfo(dinfo),
                            &error_fatal);
    qdev_realize_and_unref(card, qdev_get_child_bus(adapter, "sd-bus"),
                           &error_fatal);
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
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();
    SAM9X7State *soc;
    DeviceState *qspi_flash;
    I2CSlave *pmic;
    I2CSlave *power_monitor;
    I2CBus *power_monitor_bus;
    DriveInfo *nand_dinfo;
    DriveInfo *qspi_dinfo;
    unsigned int i;

    if (machine->ram_size != SAM9X7_DDR_SIZE) {
        error_report("sam9x75-curiosity requires 256 MiB of RAM");
        exit(EXIT_FAILURE);
    }

    if (machine->firmware && machine->kernel_filename) {
        error_report("-bios and -kernel select different SAM9X75 boot "
                     "paths and cannot be combined");
        exit(EXIT_FAILURE);
    }

    soc = SAM9X7(object_new(TYPE_SAM9X7));
    object_property_add_child(OBJECT(machine), "soc", OBJECT(soc));
    object_unref(OBJECT(soc));
    board->soc = soc;

    object_property_set_link(OBJECT(soc), "memory", OBJECT(sysmem),
                             &error_abort);
    object_property_set_link(OBJECT(soc), "ddr-memory",
                             OBJECT(machine->ram), &error_abort);
    for (i = 0; i < ARRAY_SIZE(board->canbus); i++) {
        g_autofree char *name = g_strdup_printf("canbus%u", i);

        object_property_set_link(OBJECT(soc), name,
                                 OBJECT(board->canbus[i]), &error_abort);
    }
    qdev_prop_set_chr(DEVICE(&soc->dbgu), "chardev", serial_hd(0));
    for (i = 0; i < ARRAY_SIZE(soc->usart); i++) {
        qdev_prop_set_chr(DEVICE(&soc->usart[i]), "chardev",
                          serial_hd(i + 1));
    }
    qemu_configure_nic_device(DEVICE(&soc->gmac), true, NULL);
    qdev_prop_set_bit(DEVICE(&soc->gmac), "phy-clocked",
                      board->ethernet_25mhz);
    qemu_macaddr_default_if_unset(&soc->gmac.conf.macaddr);
    if (board->otpc_drive) {
        object_property_set_str(OBJECT(&soc->otpc), "drive",
                                board->otpc_drive, &error_fatal);
    }
    qdev_prop_set_bit(DEVICE(&soc->otpc), "write-enable",
                      board->otpc_write_enable);
    nand_dinfo = drive_get(IF_MTD, 0, 0);
    if (nand_dinfo) {
        qdev_prop_set_drive_err(DEVICE(&soc->nand), "drive",
                                blk_by_legacy_dinfo(nand_dinfo),
                                &error_fatal);
    }
    qemu_set_irq(qdev_get_gpio_in_named(DEVICE(&soc->nand),
                                        AT91_NAND_GPIO_NCE, 0),
                 !board->nand_cs);

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
    if (board->qspi_cs) {
        qdev_connect_gpio_out_named(DEVICE(&soc->qspi), "cs", 0,
            qdev_get_gpio_in_named(qspi_flash, SSI_GPIO_CS, 0));
    } else {
        qemu_set_irq(qdev_get_gpio_in_named(qspi_flash, SSI_GPIO_CS, 0), 1);
    }

    sysbus_realize(SYS_BUS_DEVICE(soc), &error_fatal);

    if (machine->firmware) {
        sam9x75_curiosity_load_rom(machine, soc);
    }

    /* U8 is the board's MCP16502TAB-E/S8B power-management IC. */
    pmic = i2c_slave_new(TYPE_MCP16502_AB, 0x5b);
    object_property_add_child(OBJECT(machine), "mcp16502", OBJECT(pmic));
    i2c_slave_realize_and_unref(pmic, soc->twi[6].bus, &error_fatal);
    qdev_connect_gpio_out_named(DEVICE(pmic), "nrsto", 0,
        qdev_get_gpio_in_named(DEVICE(soc), SAM9X7_GPIO_RESET,
                               SAM9X7_RESET_POWER));

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
    if (board->pac1934_route == SAM9X75_PAC1934_ROUTE_SOC) {
        power_monitor_bus = soc->twi[7].bus;
    } else {
        /* J38/J39 routed away from the SoC leave FLEXCOM7 disconnected. */
        power_monitor_bus = i2c_init_bus(DEVICE(soc), "pac1934-external");
    }
    i2c_slave_realize_and_unref(power_monitor, power_monitor_bus,
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
    if (board->m2_interface == SAM9X75_M2_INTERFACE_SDIO) {
        sam9x75_curiosity_attach_sd(soc, 1);
    } else {
        sam9x75_curiosity_attach_spi_sd(soc, 1);
    }

    /* The populated NAND drives its active-high ready signal onto PD14. */
    qemu_set_irq(qdev_get_gpio_in(DEVICE(&soc->pio[3]), 14), 1);

    board->boot_info = (struct arm_boot_info) {
        .loader_start = SAM9X7_DDR_BASE,
        .ram_size = machine->ram_size,
        .board_id = -1,
    };

    if (!qtest_enabled() && !machine->firmware) {
        arm_load_kernel(&soc->cpu, machine, &board->boot_info);
    }
}

static bool sam9x75_curiosity_get_nand_cs(Object *obj, Error **errp)
{
    return SAM9X75_CURIOSITY_MACHINE(obj)->nand_cs;
}

static void sam9x75_curiosity_set_nand_cs(Object *obj, bool value,
                                          Error **errp)
{
    SAM9X75_CURIOSITY_MACHINE(obj)->nand_cs = value;
}

static bool sam9x75_curiosity_get_qspi_cs(Object *obj, Error **errp)
{
    return SAM9X75_CURIOSITY_MACHINE(obj)->qspi_cs;
}

static void sam9x75_curiosity_set_qspi_cs(Object *obj, bool value,
                                          Error **errp)
{
    SAM9X75_CURIOSITY_MACHINE(obj)->qspi_cs = value;
}

static bool sam9x75_curiosity_get_ethernet_25mhz(Object *obj, Error **errp)
{
    return SAM9X75_CURIOSITY_MACHINE(obj)->ethernet_25mhz;
}

static void sam9x75_curiosity_set_ethernet_25mhz(Object *obj, bool value,
                                                 Error **errp)
{
    SAM9X75_CURIOSITY_MACHINE(obj)->ethernet_25mhz = value;
}

static char *sam9x75_curiosity_get_pac1934_route(Object *obj, Error **errp)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(obj);

    switch (board->pac1934_route) {
    case SAM9X75_PAC1934_ROUTE_SOC:
        return g_strdup("soc");
    case SAM9X75_PAC1934_ROUTE_USB:
        return g_strdup("usb");
    case SAM9X75_PAC1934_ROUTE_OFF:
        return g_strdup("off");
    default:
        g_assert_not_reached();
    }
}

static void sam9x75_curiosity_set_pac1934_route(Object *obj,
                                                 const char *value,
                                                 Error **errp)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(obj);

    if (!strcmp(value, "soc")) {
        board->pac1934_route = SAM9X75_PAC1934_ROUTE_SOC;
    } else if (!strcmp(value, "usb")) {
        board->pac1934_route = SAM9X75_PAC1934_ROUTE_USB;
    } else if (!strcmp(value, "off")) {
        board->pac1934_route = SAM9X75_PAC1934_ROUTE_OFF;
    } else {
        error_setg(errp, "Invalid PAC1934 route '%s'", value);
        error_append_hint(errp, "Valid values are soc, usb and off.\n");
    }
}

static char *sam9x75_curiosity_get_m2_interface(Object *obj, Error **errp)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(obj);

    switch (board->m2_interface) {
    case SAM9X75_M2_INTERFACE_SDIO:
        return g_strdup("sdio");
    case SAM9X75_M2_INTERFACE_SPI:
        return g_strdup("spi");
    default:
        g_assert_not_reached();
    }
}

static void sam9x75_curiosity_set_m2_interface(Object *obj,
                                                const char *value,
                                                Error **errp)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(obj);

    if (!strcmp(value, "sdio")) {
        board->m2_interface = SAM9X75_M2_INTERFACE_SDIO;
    } else if (!strcmp(value, "spi")) {
        board->m2_interface = SAM9X75_M2_INTERFACE_SPI;
    } else {
        error_setg(errp, "Invalid M.2 interface '%s'", value);
        error_append_hint(errp, "Valid values are sdio and spi.\n");
    }
}

static char *sam9x75_curiosity_get_otpc_drive(Object *obj, Error **errp)
{
    return g_strdup(SAM9X75_CURIOSITY_MACHINE(obj)->otpc_drive);
}

static void sam9x75_curiosity_set_otpc_drive(Object *obj,
                                              const char *value,
                                              Error **errp)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(obj);

    g_free(board->otpc_drive);
    board->otpc_drive = g_strdup(value);
}

static bool sam9x75_curiosity_get_otpc_write_enable(Object *obj,
                                                     Error **errp)
{
    return SAM9X75_CURIOSITY_MACHINE(obj)->otpc_write_enable;
}

static void sam9x75_curiosity_set_otpc_write_enable(Object *obj,
                                                     bool value,
                                                     Error **errp)
{
    SAM9X75_CURIOSITY_MACHINE(obj)->otpc_write_enable = value;
}

static void sam9x75_curiosity_machine_instance_init(Object *obj)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(obj);

    board->nand_cs = true;
    board->qspi_cs = true;
    board->ethernet_25mhz = true;
    board->pac1934_route = SAM9X75_PAC1934_ROUTE_SOC;
    board->m2_interface = SAM9X75_M2_INTERFACE_SDIO;

    object_property_add_link(obj, "canbus0", TYPE_CAN_BUS,
                             (Object **)&board->canbus[0],
                             object_property_allow_set_link, 0);
    object_property_add_link(obj, "canbus1", TYPE_CAN_BUS,
                             (Object **)&board->canbus[1],
                             object_property_allow_set_link, 0);
}

static void sam9x75_curiosity_machine_instance_finalize(Object *obj)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(obj);

    g_free(board->otpc_drive);
}

static void sam9x75_curiosity_machine_reset(MachineState *machine,
                                             ResetType type)
{
    SAM9X75CuriosityMachineState *board =
        SAM9X75_CURIOSITY_MACHINE(machine);
    bool core_reset = type == RESET_TYPE_COLD && board->soc &&
        (sam9x7_core_reset_requested(board->soc) ||
         at91_rstc_watchdog_reset_pending(&board->soc->rstc));

    qemu_devices_reset(core_reset ? RESET_TYPE_WAKEUP : type);
}

static void sam9x75_curiosity_machine_class_init(ObjectClass *oc,
                                                  const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("arm926"),
        NULL,
    };

    mc->desc = "Microchip SAM9X75 Curiosity (ARM926EJ-S)";
    mc->init = sam9x75_curiosity_init;
    mc->reset = sam9x75_curiosity_machine_reset;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = 256 * MiB;
    mc->default_ram_id = "sam9x75-curiosity.ddr";
    mc->max_cpus = 1;
    mc->default_cpus = 1;

    object_class_property_add_bool(oc, "nand-cs",
                                   sam9x75_curiosity_get_nand_cs,
                                   sam9x75_curiosity_set_nand_cs);
    object_class_property_set_description(oc, "nand-cs",
                                          "Connect the J9 NAND CS jumper");
    object_class_property_add_bool(oc, "qspi-cs",
                                   sam9x75_curiosity_get_qspi_cs,
                                   sam9x75_curiosity_set_qspi_cs);
    object_class_property_set_description(oc, "qspi-cs",
                                          "Connect the J10 QSPI CS jumper");
    object_class_property_add_bool(oc, "ethernet-25mhz",
                                   sam9x75_curiosity_get_ethernet_25mhz,
                                   sam9x75_curiosity_set_ethernet_25mhz);
    object_class_property_set_description(oc, "ethernet-25mhz",
        "Connect the J12 25 MHz Ethernet reference-clock jumper");
    object_class_property_add_str(oc, "pac1934-route",
                                  sam9x75_curiosity_get_pac1934_route,
                                  sam9x75_curiosity_set_pac1934_route);
    object_class_property_set_description(oc, "pac1934-route",
        "Route both J38/J39 PAC1934 I2C jumpers to soc, usb, or off");
    object_class_property_add_str(oc, "m2-interface",
                                  sam9x75_curiosity_get_m2_interface,
                                  sam9x75_curiosity_set_m2_interface);
    object_class_property_set_description(oc, "m2-interface",
        "Route the J24 M.2 host-interface jumper to sdio or spi");
    object_class_property_add_str(oc, "otpc-drive",
                                  sam9x75_curiosity_get_otpc_drive,
                                  sam9x75_curiosity_set_otpc_drive);
    object_class_property_set_description(oc, "otpc-drive",
        "Block node or drive ID for the raw 10 KiB physical OTP image");
    object_class_property_add_bool(oc, "otpc-write-enable",
                                   sam9x75_curiosity_get_otpc_write_enable,
                                   sam9x75_curiosity_set_otpc_write_enable);
    object_class_property_set_description(oc, "otpc-write-enable",
        "Permit irreversible writes to the configured physical OTP image");
}

static const TypeInfo sam9x75_curiosity_machine_types[] = {
    {
        .name = TYPE_SAM9X75_CURIOSITY_MACHINE,
        .parent = TYPE_MACHINE,
        .class_init = sam9x75_curiosity_machine_class_init,
        .instance_init = sam9x75_curiosity_machine_instance_init,
        .instance_finalize =
            sam9x75_curiosity_machine_instance_finalize,
        .instance_size = sizeof(SAM9X75CuriosityMachineState),
        .interfaces = arm_machine_interfaces,
    },
};

DEFINE_TYPES(sam9x75_curiosity_machine_types)
