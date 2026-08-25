/*
 * Test short-descriptor translation and domain fault priority
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#define INVALID_L2_VA          0x10000000
#define VALID_L2_VA            0x10100000
#define SECTION_VA             0x10200000
#define EXTERNAL_L2_VA         0x10300000

#define DOMAIN_4               (4 << 5)
#define COARSE_DESCRIPTOR      1
#define SECTION_DESCRIPTOR     2
#define SMALL_PAGE_DESCRIPTOR  2
#define AP_FULL_ACCESS         (3 << 4)

#define DFSR_L2_TRANSLATION    0x47
#define DFSR_L2_DOMAIN         0x4b
#define DFSR_L1_DOMAIN         0x49
#define DFSR_L2_EXTERNAL       0x4e

#define SYS_EXIT                       0x18
#define ADP_STOPPED_APPLICATION_EXIT   0x20026
#define ADP_STOPPED_INTERNAL_ERROR     0x20024

.syntax unified
.arm

.macro check_fault va, dfsr
    ldr     r0, =fault_record
    mov     r1, #0
    str     r1, [r0, #8]

    ldr     r4, =\va
    ldr     r5, [r4]

    ldr     r0, =fault_record
    ldr     r1, [r0, #8]
    cmp     r1, #1
    bne     fail
    ldr     r1, [r0]
    ldr     r2, =\dfsr
    cmp     r1, r2
    bne     fail
    ldr     r1, [r0, #4]
    cmp     r1, r4
    bne     fail
.endm

.section .vectors, "ax"
.align 5
vector_table:
    b       fail                       /* Reset */
    b       fail                       /* Undefined instruction */
    b       fail                       /* Supervisor call */
    b       fail                       /* Prefetch abort */
    b       data_abort
    b       fail                       /* Reserved */
    b       fail                       /* IRQ */
    b       fail                       /* FIQ */

.section .text.boot, "ax"
.global _start
.type _start, %function
_start:
    ldr     sp, =stack_top
    bl      init_page_tables
    bl      enable_mmu

    /* An invalid L2 descriptor takes priority over a domain fault. */
    check_fault INVALID_L2_VA, DFSR_L2_TRANSLATION

    /* A valid L2 descriptor in a no-access domain is a page domain fault. */
    check_fault VALID_L2_VA, DFSR_L2_DOMAIN

    /* Moving the domain check must not alter the section case. */
    check_fault SECTION_VA, DFSR_L1_DOMAIN

    /* An external abort fetching an L2 descriptor also takes priority. */
    check_fault EXTERNAL_L2_VA, DFSR_L2_EXTERNAL

    /* Verify that the valid L2 descriptor maps data when its domain allows. */
    ldr     r0, =0x303                /* Domains 0 and 4 are managers. */
    mcr     p15, 0, r0, c3, c0, 0
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0
    ldr     r0, =VALID_L2_VA
    ldr     r1, [r0]
    ldr     r2, =test_value
    ldr     r2, [r2]
    cmp     r1, r2
    bne     fail

pass:
    mov     r0, #SYS_EXIT
    ldr     r1, =ADP_STOPPED_APPLICATION_EXIT
    svc     0x123456
1:
    b       1b

fail:
    mov     r0, #SYS_EXIT
    ldr     r1, =ADP_STOPPED_INTERNAL_ERROR
    svc     0x123456
1:
    b       1b
.size _start, . - _start

.type init_page_tables, %function
init_page_tables:
    /* Initialize every descriptor so the test does not depend on RAM reset. */
    ldr     r0, =l1_table
    ldr     r2, =l1_table_end
    mov     r1, #0
1:
    str     r1, [r0], #4
    cmp     r0, r2
    blo     1b

    ldr     r0, =l2_invalid
    ldr     r2, =l2_tables_end
1:
    str     r1, [r0], #4
    cmp     r0, r2
    blo     1b

    /* Identity map the first MiB for vectors, code, data, and the stack. */
    ldr     r0, =l1_table
    ldr     r1, =0x00000c02           /* Domain 0, AP=3, section. */
    str     r1, [r0]

    /* Coarse table in domain 4 whose selected L2 descriptor is invalid. */
    ldr     r1, =l2_invalid
    orr     r1, r1, #(DOMAIN_4 | COARSE_DESCRIPTOR)
    str     r1, [r0, #(INVALID_L2_VA >> 18)]

    /* Coarse table in domain 4 with a valid small-page descriptor. */
    ldr     r1, =l2_valid
    orr     r1, r1, #(DOMAIN_4 | COARSE_DESCRIPTOR)
    str     r1, [r0, #(VALID_L2_VA >> 18)]
    ldr     r1, =test_value
    orr     r1, r1, #(AP_FULL_ACCESS | SMALL_PAGE_DESCRIPTOR)
    ldr     r2, =l2_valid
    str     r1, [r2]

    /* A valid section descriptor in domain 4. */
    ldr     r1, =(DOMAIN_4 | 0x00000c00 | SECTION_DESCRIPTOR)
    str     r1, [r0, #(SECTION_VA >> 18)]

    /* A coarse table descriptor whose L2 address is not mapped by the board. */
    ldr     r1, =(0x20000000 | DOMAIN_4 | COARSE_DESCRIPTOR)
    str     r1, [r0, #(EXTERNAL_L2_VA >> 18)]
    bx      lr
.size init_page_tables, . - init_page_tables

.type enable_mmu, %function
enable_mmu:
    /* Domain 0 is a manager; domain 4 has no access. */
    mov     r0, #3
    mcr     p15, 0, r0, c3, c0, 0

    ldr     r0, =l1_table
    mcr     p15, 0, r0, c2, c0, 0
    mov     r0, #0
    mcr     p15, 0, r0, c8, c7, 0

    mrc     p15, 0, r0, c1, c0, 0
    bic     r0, r0, #(1 << 13)        /* Use low exception vectors. */
#if TEST_ARMV6
    orr     r0, r0, #(1 << 23)        /* Use the ARMv6 page-table walker. */
#else
    bic     r0, r0, #(1 << 23)        /* Use the ARMv5 page-table walker. */
#endif
    orr     r0, r0, #1
    mcr     p15, 0, r0, c1, c0, 0
    nop
    nop
    nop
    bx      lr
.size enable_mmu, . - enable_mmu

.type data_abort, %function
data_abort:
    mrc     p15, 0, r0, c5, c0, 0    /* DFSR */
    mrc     p15, 0, r1, c6, c0, 0    /* DFAR */
    ldr     r2, =fault_record
    str     r0, [r2]
    str     r1, [r2, #4]
    ldr     r3, [r2, #8]
    add     r3, r3, #1
    str     r3, [r2, #8]
    subs    pc, lr, #4                /* Skip the faulting load. */
.size data_abort, . - data_abort

.section .data, "aw"
.balign 4096
test_value:
    .word   0x51a7f00d

.section .bss.fault_record, "aw", %nobits
.balign 4
fault_record:
    .space  12

.section .bss.stack, "aw", %nobits
.balign 16
stack:
    .space  4096
stack_top:

.section .bss.page_tables, "aw", %nobits
.balign 16384
l1_table:
    .space  16384
l1_table_end:
.balign 1024
l2_invalid:
    .space  1024
l2_valid:
    .space  1024
l2_tables_end:
