/*
 * qemu-exact properties for a qemu-system-arm build.
 *
 * cpu64.c provides these symbols to qemu-system-aarch64, including when that
 * binary runs an AArch32 guest.  It is deliberately not compiled into the
 * 32-bit-only system emulator, so keeping the only definitions there made
 * every qemu-system-arm link fail as soon as cpu.c exposed the properties on
 * all CPU models.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "cpu.h"
#include "internals.h"
#include "exact.h"

static const Property arm_cpu_exact32_properties[] = {
    /* Kept for command-line compatibility; pre-v8 finalize leaves them alone. */
    DEFINE_PROP_UINT8("x-ctr-dic", ARMCPU, prop_ctr_dic, 0xff),
    DEFINE_PROP_UINT8("x-ctr-idc", ARMCPU, prop_ctr_idc, 0xff),
    DEFINE_PROP_UINT8("x-ctr-cwg", ARMCPU, prop_ctr_cwg, 0xff),
    DEFINE_PROP_UINT8("x-ctr-erg", ARMCPU, prop_ctr_erg, 0xff),
    DEFINE_PROP_UINT8("x-asid-bits", ARMCPU, prop_asid_bits, 0xff),
    DEFINE_PROP_UINT8("x-bbm-level", ARMCPU, prop_bbm_level, 0xff),
    DEFINE_PROP_BOOL("x-exact-tlb", ARMCPU, prop_exact_tlb, false),
    DEFINE_PROP_BOOL("x-exact-icache", ARMCPU, prop_exact_icache, false),
    DEFINE_PROP_BOOL("x-exact-icache-full", ARMCPU, prop_exact_icache_full,
                     false),
    DEFINE_PROP_BOOL("x-exact-dcache", ARMCPU, prop_exact_dcache, false),
    DEFINE_PROP_UINT8("x-exact-dcache-line", ARMCPU, prop_dcache_line, 64),
    DEFINE_PROP_BOOL("x-exact-exclusive", ARMCPU, prop_exact_exclusive, false),
    DEFINE_PROP_UINT32("x-exact-exclusive-rate", ARMCPU, prop_exclusive_rate,
                       10000),
    DEFINE_PROP_UINT8("x-exact-exclusive-maxrun", ARMCPU,
                      prop_exclusive_maxrun, 2),
    DEFINE_PROP_UINT64("x-exact-exclusive-seed", ARMCPU,
                       prop_exclusive_seed, 1),
};

void aarch64_add_exact_properties(Object *obj)
{
    for (size_t i = 0; i < ARRAY_SIZE(arm_cpu_exact32_properties); i++) {
        qdev_property_add_static(DEVICE(obj), &arm_cpu_exact32_properties[i]);
    }
}

void aarch64_cpu_exact_finalize(ARMCPU *cpu, Error **errp)
{
    if (cpu->prop_exact_tlb) {
        arm_exact_tlb_enabled = true;
        arm_exact_tlb_init();
    }
    if (cpu->prop_exact_dcache) {
        if (cpu->prop_dcache_line != 32 && cpu->prop_dcache_line != 64 &&
            cpu->prop_dcache_line != 128) {
            error_setg(errp, "x-exact-dcache-line must be 32, 64 or 128");
            return;
        }
        arm_exact_dcache_line = cpu->prop_dcache_line;
        arm_exact_dcache_enabled = true;
        arm_exact_dcache_init();
    }

    arm_exact_exclusive_init();
    if (cpu->prop_exact_exclusive) {
        if (cpu->prop_exclusive_rate > 1000000) {
            error_setg(errp,
                       "x-exact-exclusive-rate is per million, max 1000000");
            return;
        }
        arm_exact_exclusive_enabled = true;
        arm_exact_exclusive_rate = cpu->prop_exclusive_rate;
        arm_exact_exclusive_maxrun = cpu->prop_exclusive_maxrun;
        arm_exact_exclusive_seed = cpu->prop_exclusive_seed;
    }
    if (cpu->prop_exact_icache || cpu->prop_exact_icache_full) {
        arm_exact_icache_enabled = true;
        arm_exact_icache_full = cpu->prop_exact_icache_full;
        arm_exact_icache_init();
    }
}
