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
#include "hw/core/qdev-properties.h"
#include "hw/sd/sd.h"
#include "hw/ssi/ssi.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/units.h"
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

static void sam9x75_curiosity_init(MachineState *machine)
{
    MemoryRegion *sysmem = get_system_memory();
    SAM9X7State *soc;
    DeviceState *qspi_flash;
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
