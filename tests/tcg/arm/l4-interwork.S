/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SCTLR.L4 (bit 15, up to v6, RAZ from v7): DDI 0100I Table 2-11 --
 * with L4 set, a load to PC does not follow LoadWritePC() interworking,
 * ie does not update the T bit from the loaded value's bit 0. This is
 * an ARMv4 compatibility knob for legacy code running on a v5T+ core
 * that would otherwise always interwork loads to PC.
 *
 * Freestanding bare-metal, not linux-user: SCTLR is privileged state
 * with no kernel entity to configure it under linux-user emulation, so
 * this must run directly via -kernel with no OS underneath, on
 * versatilepb where reset starts in privileged (svc32) mode.
 *
 * Loads a pointer whose value has bit 0 set (would select Thumb via
 * ordinary interworking) into PC. With L4 set, execution must remain
 * in ARM state at that address; the ARM-only code there writes a
 * marker word and spins. If interworking incorrectly still happens,
 * those same bytes are nonsense as Thumb and the CPU takes an
 * Undefined Instruction trap instead -- observed with the marker never
 * written, confirmed against a negative control (see the commit this
 * file was added in) rather than assumed.
 */
.syntax unified
.text
.arm
.global _start
.p2align 2
_start:
    mrc p15, 0, r0, c1, c0, 0
    orr r0, r0, #(1 << 15)         /* SCTLR.L4 */
    mcr p15, 0, r0, c1, c0, 0

    ldr r0, target_ptr
    ldr pc, [r0]                   /* LoadWritePC(): must not interwork */

.p2align 2
target_ptr:
    .word target_word

.p2align 4
target_word:
    .word target_code + 1          /* bit 0 set: Thumb if interworked */

.p2align 6
target_code:
    ldr r0, marker_addr
    ldr r1, marker_val
    str r1, [r0]
1:  b 1b

.p2align 2
marker_addr:
    .word 0x00100000
marker_val:
    .word 0xcafef00d
