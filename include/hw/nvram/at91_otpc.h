/*
 * Microchip AT91 OTP memory controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_NVRAM_AT91_OTPC_H
#define HW_NVRAM_AT91_OTPC_H

#include "hw/core/sysbus.h"
#include "qemu/units.h"
#include "qom/object.h"
#include "system/memory.h"

#define TYPE_AT91_OTPC "at91-otpc"
OBJECT_DECLARE_SIMPLE_TYPE(AT91OTPCState, AT91_OTPC)

#define AT91_OTPC_MMIO_SIZE              0x1000
#define AT91_OTPC_EMULATION_MEMORY_SIZE  0x1000
#define AT91_OTPC_OTP_WORDS              (10 * KiB / sizeof(uint32_t))
#define AT91_OTPC_TEMP_WORDS             256

struct AT91OTPCState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    MemoryRegion *emulation_memory;
    MemoryRegion emulation_root;
    MemoryRegion emulation_alias;
    AddressSpace emulation_as;

    uint32_t otp[AT91_OTPC_OTP_WORDS];
    uint32_t temporary[AT91_OTPC_TEMP_WORDS];
    uint32_t uid[4];

    uint32_t mr;
    uint32_t ar;
    uint32_t sr;
    uint32_t imr;
    uint32_t isr;
    uint32_t hr;
    uint32_t bar;
    uint32_t car;
    uint32_t lrmr;
    uint32_t uhc[2];
    uint32_t wpmr;
    uint32_t wpsr;
    uint32_t temporary_words;

    bool emulation_active;
    bool otp_ever_programmed;
};

#endif /* HW_NVRAM_AT91_OTPC_H */
