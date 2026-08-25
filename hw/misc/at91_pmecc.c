/*
 * Microchip AT91 programmable multibit ECC controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/irq.h"
#include "hw/misc/at91_pmecc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PMECC_CFG          0x00
#define PMECC_SAREA        0x04
#define PMECC_SADDR        0x08
#define PMECC_EADDR        0x0c
#define PMECC_RESERVED_CLK 0x10
#define PMECC_CTRL         0x14
#define PMECC_SR           0x18
#define PMECC_IER          0x1c
#define PMECC_IDR          0x20
#define PMECC_IMR          0x24
#define PMECC_ISR          0x28
#define PMECC_BANK_COUNT   AT91_PMECC_MAX_SECTORS
#define PMECC_BANK_STRIDE  0x40
#define PMECC_ECC_FIRST    0x40
#define PMECC_ECC_SIZE     AT91_PMECC_ECC_BYTES
#define PMECC_REM_FIRST    0x240
#define PMECC_REM_SIZE     (AT91_PMECC_REM_REGS * sizeof(uint32_t))
#define PMECC_MMIO_SIZE    0x600

#define PMECC_CFG_MASK     0x00111317
#define PMECC_CFG_STRENGTH_MASK 0x7
#define PMECC_CFG_SECTOR_1024 BIT(4)
#define PMECC_CFG_NSECTORS_SHIFT 8
#define PMECC_CFG_NSECTORS_MASK 0x3
#define PMECC_CFG_WRITE    BIT(12)
#define PMECC_CFG_SPARE_ENABLE BIT(16)
#define PMECC_CFG_AUTO_ENABLE BIT(20)
#define PMECC_CTRL_RST     BIT(0)
#define PMECC_CTRL_DATA    BIT(1)
#define PMECC_CTRL_USER    BIT(2)
#define PMECC_CTRL_ENABLE  BIT(4)
#define PMECC_CTRL_DISABLE BIT(5)
#define PMECC_SR_BUSY      BIT(0)
#define PMECC_SR_ENABLE    BIT(4)
#define PMECC_ISR_MASK     0xff

#define PMERRLOC_CFG       0x00
#define PMERRLOC_PRIM      0x04
#define PMERRLOC_LEN       0x08
#define PMERRLOC_DIS       0x0c
#define PMERRLOC_SR        0x10
#define PMERRLOC_IER       0x14
#define PMERRLOC_IDR       0x18
#define PMERRLOC_IMR       0x1c
#define PMERRLOC_ISR       0x20
#define PMERRLOC_SIGMA0    0x28
#define PMERRLOC_SIGMA_LAST 0x88
#define PMERRLOC_EL_FIRST  0x8c
#define PMERRLOC_EL_LAST   0xe8
#define PMERRLOC_MMIO_SIZE 0x100

#define PMERRLOC_DONE      BIT(0)
#define PMERRLOC_ERR_COUNT_SHIFT 8
#define PMERRLOC_CFG_ERRNUM_SHIFT 16
#define PMERRLOC_CFG_ERRNUM_MASK  0x1f

#define PMECC_GF_MAX_SIZE  (1 << 14)

static const uint8_t at91_pmecc_strengths[] = { 2, 4, 8, 12, 24 };

typedef enum AT91PMECCProfileStatus {
    PMECC_PROFILE_VALID,
    PMECC_PROFILE_INVALID,
    PMECC_PROFILE_UNIMPLEMENTED,
} AT91PMECCProfileStatus;

static void at91_pmecc_update_irq(AT91PMECCState *s)
{
    qemu_set_irq(s->irq, (s->isr && (s->imr & BIT(0))) ||
                         (s->errloc_isr & s->errloc_imr));
}

static bool at91_pmecc_bank_access(hwaddr offset, hwaddr first,
                                   hwaddr bank_size, unsigned int size,
                                   unsigned int *bank,
                                   unsigned int *bank_offset)
{
    hwaddr relative;

    if (offset < first) {
        return false;
    }

    relative = offset - first;
    *bank = relative / PMECC_BANK_STRIDE;
    *bank_offset = relative % PMECC_BANK_STRIDE;

    return *bank < PMECC_BANK_COUNT &&
           *bank_offset < bank_size && size <= bank_size - *bank_offset;
}

static uint16_t at91_pmecc_primitive(unsigned int m)
{
    switch (m) {
    case 13:
        return 0x201b;
    case 14:
        return 0x4443;
    default:
        return 0;
    }
}

static bool at91_pmecc_build_gf(AT91PMECCState *s, unsigned int m)
{
    unsigned int field_size = 1U << m;
    unsigned int n = field_size - 1;
    uint16_t primitive = at91_pmecc_primitive(m);
    unsigned int value = 1;
    unsigned int i;

    if (!primitive || field_size > PMECC_GF_MAX_SIZE) {
        return false;
    }
    if (s->gf_m == m) {
        return true;
    }

    memset(s->index_of, 0xff, PMECC_GF_MAX_SIZE * sizeof(*s->index_of));
    for (i = 0; i < n; i++) {
        s->alpha_to[i] = value;
        s->index_of[value] = i;
        value <<= 1;
        if (value & field_size) {
            value ^= primitive;
        }
    }

    if (value != 1) {
        s->gf_m = 0;
        return false;
    }

    s->gf_m = m;
    return true;
}

static uint16_t at91_pmecc_gf_mul(AT91PMECCState *s, uint16_t left,
                                  uint16_t right, unsigned int m)
{
    unsigned int n = (1U << m) - 1;
    unsigned int exponent;

    if (!left || !right) {
        return 0;
    }

    exponent = s->index_of[left] + s->index_of[right];
    if (exponent >= n) {
        exponent -= n;
    }
    return s->alpha_to[exponent];
}

static bool at91_pmecc_build_minpoly(AT91PMECCState *s, unsigned int root,
                                     unsigned int m, uint16_t *result)
{
    uint16_t coefficients[15] = { 1 };
    uint16_t next[15];
    unsigned int n = (1U << m) - 1;
    unsigned int exponent = root % n;
    unsigned int first = exponent;
    unsigned int degree = 0;
    unsigned int i;

    do {
        uint16_t factor = s->alpha_to[exponent];

        if (degree >= m) {
            return false;
        }
        memset(next, 0, sizeof(next));
        for (i = 0; i <= degree; i++) {
            next[i] ^= at91_pmecc_gf_mul(s, coefficients[i], factor, m);
            next[i + 1] ^= coefficients[i];
        }
        memcpy(coefficients, next, sizeof(coefficients));
        degree++;
        exponent = (exponent * 2) % n;
    } while (exponent != first);

    if (degree != m) {
        return false;
    }

    *result = 0;
    for (i = 0; i <= degree; i++) {
        if (coefficients[i] > 1) {
            return false;
        }
        *result |= coefficients[i] << i;
    }
    return true;
}

static bool at91_pmecc_poly_test(const uint64_t *polynomial,
                                 unsigned int bit)
{
    return polynomial[bit / 64] & (UINT64_C(1) << (bit % 64));
}

static void at91_pmecc_poly_toggle(uint64_t *polynomial, unsigned int bit)
{
    polynomial[bit / 64] ^= UINT64_C(1) << (bit % 64);
}

static bool at91_pmecc_build_generator(AT91PMECCState *s, unsigned int m,
                                       unsigned int strength)
{
    uint64_t polynomial[AT91_PMECC_PARITY_WORDS] = { 1 };
    uint64_t next[AT91_PMECC_PARITY_WORDS];
    unsigned int degree = 0;
    unsigned int field_mask = (1U << m) - 1;
    unsigned int i;

    if (strength > AT91_PMECC_MAX_STRENGTH ||
        m * strength > AT91_PMECC_PARITY_WORDS * 64 - 1) {
        return false;
    }

    memset(s->minpoly, 0, sizeof(s->minpoly));
    for (i = 0; i < strength; i++) {
        uint16_t minimal;
        unsigned int a;
        unsigned int b;

        if (!at91_pmecc_build_minpoly(s, 2 * i + 1, m, &minimal)) {
            return false;
        }
        s->minpoly[i] = minimal & field_mask;

        memset(next, 0, sizeof(next));
        for (a = 0; a <= degree; a++) {
            if (!at91_pmecc_poly_test(polynomial, a)) {
                continue;
            }
            for (b = 0; b <= m; b++) {
                if (minimal & BIT(b)) {
                    at91_pmecc_poly_toggle(next, a + b);
                }
            }
        }
        memcpy(polynomial, next, sizeof(polynomial));
        degree += m;
    }

    if (degree != m * strength ||
        !at91_pmecc_poly_test(polynomial, degree)) {
        return false;
    }

    at91_pmecc_poly_toggle(polynomial, degree);
    memcpy(s->generator, polynomial, sizeof(s->generator));
    return true;
}

static bool at91_pmecc_prepare_math(AT91PMECCState *s, unsigned int m,
                                    unsigned int strength)
{
    return at91_pmecc_build_gf(s, m) &&
           at91_pmecc_build_generator(s, m, strength);
}

static void at91_pmecc_stop_stream(AT91PMECCState *s)
{
    s->busy = false;
    s->stream_active = false;
}

static void at91_pmecc_clear_stream(AT91PMECCState *s)
{
    at91_pmecc_stop_stream(s);
    s->stream_write = false;
    s->stream_m = 0;
    s->stream_strength = 0;
    s->stream_nsectors = 0;
    s->stream_sector_size = 0;
    s->stream_ecc_bits = 0;
    s->stream_ecc_bytes = 0;
    s->stream_page_size = 0;
    s->stream_next_column = 0;
    memset(s->encode_rem, 0, sizeof(s->encode_rem));
    memset(s->decode_rem, 0, sizeof(s->decode_rem));
    memset(s->generator, 0, sizeof(s->generator));
    memset(s->minpoly, 0, sizeof(s->minpoly));
}

static void at91_pmecc_clear_results(AT91PMECCState *s)
{
    memset(s->ecc, 0xff, sizeof(s->ecc));
    memset(s->rem, 0, sizeof(s->rem));
    s->isr = 0;
}

static AT91PMECCProfileStatus at91_pmecc_latch_profile(AT91PMECCState *s)
{
    unsigned int strength_index = s->cfg & PMECC_CFG_STRENGTH_MASK;
    unsigned int ecc_area_size;

    if (strength_index >= ARRAY_SIZE(at91_pmecc_strengths)) {
        return PMECC_PROFILE_INVALID;
    }

    s->stream_write = s->cfg & PMECC_CFG_WRITE;
    s->stream_m = (s->cfg & PMECC_CFG_SECTOR_1024) ? 14 : 13;
    s->stream_sector_size = (s->cfg & PMECC_CFG_SECTOR_1024) ?
                            1024 : 512;
    s->stream_strength = at91_pmecc_strengths[strength_index];
    s->stream_nsectors = 1U << ((s->cfg >> PMECC_CFG_NSECTORS_SHIFT) &
                                PMECC_CFG_NSECTORS_MASK);
    s->stream_ecc_bits = s->stream_m * s->stream_strength;
    s->stream_ecc_bytes = DIV_ROUND_UP(s->stream_ecc_bits, 8);
    s->stream_page_size = s->stream_sector_size * s->stream_nsectors;

    if (s->stream_nsectors > AT91_PMECC_MAX_SECTORS ||
        s->stream_ecc_bytes > AT91_PMECC_ECC_BYTES ||
        DIV_ROUND_UP(s->stream_strength, 2) > AT91_PMECC_REM_REGS) {
        return PMECC_PROFILE_INVALID;
    }
    if (s->cfg & PMECC_CFG_SPARE_ENABLE) {
        return PMECC_PROFILE_UNIMPLEMENTED;
    }

    if (s->stream_write) {
        return PMECC_PROFILE_VALID;
    }
    if (!(s->cfg & PMECC_CFG_AUTO_ENABLE)) {
        return PMECC_PROFILE_UNIMPLEMENTED;
    }
    if (s->saddr > s->eaddr || s->eaddr > s->sarea) {
        return PMECC_PROFILE_INVALID;
    }

    ecc_area_size = s->stream_nsectors * s->stream_ecc_bytes;
    return s->eaddr - s->saddr + 1 == ecc_area_size ?
           PMECC_PROFILE_VALID : PMECC_PROFILE_INVALID;
}

static void at91_pmecc_start_data(AT91PMECCState *s)
{
    AT91PMECCProfileStatus profile;

    at91_pmecc_clear_stream(s);
    at91_pmecc_clear_results(s);
    at91_pmecc_update_irq(s);

    if (!s->enabled) {
        return;
    }
    profile = at91_pmecc_latch_profile(s);
    if (profile == PMECC_PROFILE_UNIMPLEMENTED) {
        qemu_log_mask(LOG_UNIMP,
                      TYPE_AT91_PMECC ": unsupported CFG 0x%08x"
                      " for DATA phase\n", s->cfg);
        at91_pmecc_clear_stream(s);
        return;
    }
    if (profile == PMECC_PROFILE_INVALID ||
        !at91_pmecc_prepare_math(s, s->stream_m, s->stream_strength)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": invalid CFG 0x%08x"
                      " for DATA phase\n", s->cfg);
        at91_pmecc_clear_stream(s);
        return;
    }

    s->busy = true;
    s->stream_active = true;
}

static void at91_pmecc_shift_encoder(AT91PMECCState *s,
                                     unsigned int sector, bool input)
{
    uint64_t *remainder = s->encode_rem[sector];
    unsigned int bits = s->stream_ecc_bits;
    unsigned int words = DIV_ROUND_UP(bits, 64);
    bool feedback = input ^ at91_pmecc_poly_test(remainder, bits - 1);
    unsigned int i;

    for (i = words - 1; i > 0; i--) {
        remainder[i] = (remainder[i] << 1) |
                       (remainder[i - 1] >> 63);
    }
    remainder[0] <<= 1;
    if (bits % 64) {
        remainder[words - 1] &= (UINT64_C(1) << (bits % 64)) - 1;
    }
    if (feedback) {
        for (i = 0; i < words; i++) {
            remainder[i] ^= s->generator[i];
        }
    }
}

static void at91_pmecc_encode_byte(AT91PMECCState *s,
                                   unsigned int sector, uint8_t value)
{
    unsigned int bit;

    for (bit = 0; bit < 8; bit++) {
        at91_pmecc_shift_encoder(s, sector, value & BIT(bit));
    }
}

static void at91_pmecc_finish_encoding_sector(AT91PMECCState *s,
                                              unsigned int sector)
{
    unsigned int bit;

    memset(s->ecc[sector], 0, s->stream_ecc_bytes);
    for (bit = 0; bit < s->stream_ecc_bits; bit++) {
        unsigned int coefficient = s->stream_ecc_bits - 1 - bit;

        if (at91_pmecc_poly_test(s->encode_rem[sector], coefficient)) {
            s->ecc[sector][bit / 8] |= BIT(bit % 8);
        }
    }
}

static void at91_pmecc_decode_bit(AT91PMECCState *s, unsigned int sector,
                                  bool input)
{
    unsigned int field_mask = (1U << s->stream_m) - 1;
    unsigned int i;

    for (i = 0; i < s->stream_strength; i++) {
        uint16_t remainder = s->decode_rem[sector][i];
        bool top = remainder & BIT(s->stream_m - 1);

        remainder = ((remainder << 1) | input) & field_mask;
        if (top) {
            remainder ^= s->minpoly[i];
        }
        s->decode_rem[sector][i] = remainder;
    }
}

static void at91_pmecc_decode_byte(AT91PMECCState *s, unsigned int sector,
                                   uint8_t value, unsigned int nbits)
{
    unsigned int bit;

    for (bit = 0; bit < nbits; bit++) {
        at91_pmecc_decode_bit(s, sector, value & BIT(bit));
    }
}

static void at91_pmecc_finish_decoding_sector(AT91PMECCState *s,
                                              unsigned int sector)
{
    bool corrupted = false;
    unsigned int i;

    memset(s->rem[sector], 0, sizeof(s->rem[sector]));
    for (i = 0; i < s->stream_strength; i++) {
        uint16_t remainder = s->decode_rem[sector][i];

        if (i & 1) {
            s->rem[sector][i / 2] |= (uint32_t)remainder << 16;
        } else {
            s->rem[sector][i / 2] |= remainder;
        }
        corrupted |= remainder != 0;
    }

    if (corrupted) {
        s->isr |= BIT(sector);
    } else {
        s->isr &= ~BIT(sector);
    }
    at91_pmecc_update_irq(s);
}

void at91_pmecc_transfer_byte(AT91PMECCState *s, bool write, uint8_t value)
{
    unsigned int sector;
    uint32_t column;

    if (!s || !s->stream_active) {
        return;
    }
    if (!s->enabled || write != s->stream_write) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": unexpected NAND %s during %s"
                      " stream\n", write ? "write" : "read",
                      s->stream_write ? "write" : "read");
        at91_pmecc_stop_stream(s);
        return;
    }

    /* PMECC sees the gated byte stream, not the NAND column/address bus. */
    column = s->stream_next_column++;

    if (column < s->stream_page_size) {
        sector = column / s->stream_sector_size;
        if (write) {
            at91_pmecc_encode_byte(s, sector, value);
            if ((column + 1) % s->stream_sector_size == 0) {
                at91_pmecc_finish_encoding_sector(s, sector);
            }
            if (column + 1 == s->stream_page_size) {
                at91_pmecc_stop_stream(s);
            }
        } else {
            at91_pmecc_decode_byte(s, sector, value, 8);
        }
        return;
    }

    if (write) {
        at91_pmecc_stop_stream(s);
        return;
    }

    column -= s->stream_page_size;
    if (column < s->saddr) {
        return;
    }
    if (column > s->eaddr) {
        at91_pmecc_stop_stream(s);
        return;
    }

    column -= s->saddr;
    sector = column / s->stream_ecc_bytes;
    if (sector < s->stream_nsectors) {
        unsigned int ecc_byte = column % s->stream_ecc_bytes;
        unsigned int bits_done = ecc_byte * 8;
        unsigned int nbits = MIN(8, s->stream_ecc_bits - bits_done);

        at91_pmecc_decode_byte(s, sector, value, nbits);
        if (ecc_byte + 1 == s->stream_ecc_bytes) {
            at91_pmecc_finish_decoding_sector(s, sector);
        }
    }
    if (column + 1 == s->stream_nsectors * s->stream_ecc_bytes) {
        at91_pmecc_stop_stream(s);
    }
}

static uint64_t at91_pmecc_read(void *opaque, hwaddr offset,
                                unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);
    unsigned int bank;
    unsigned int bank_offset;
    unsigned int i;
    uint64_t value;

    switch (offset) {
    case PMECC_CFG:
        return s->cfg;
    case PMECC_SAREA:
        return s->sarea;
    case PMECC_SADDR:
        return s->saddr;
    case PMECC_EADDR:
        return s->eaddr;
    case PMECC_RESERVED_CLK:
        /* SAM9X7 controls the PMECC clock in the PMC, not this register. */
        return 0;
    case PMECC_CTRL:
        return 0;
    case PMECC_SR:
        return (s->enabled ? PMECC_SR_ENABLE : 0) |
               (s->busy ? PMECC_SR_BUSY : 0);
    case PMECC_IER:
    case PMECC_IDR:
        return 0;
    case PMECC_IMR:
        return s->imr;
    case PMECC_ISR:
        return s->isr;
    default:
        if (at91_pmecc_bank_access(offset, PMECC_ECC_FIRST,
                                   PMECC_ECC_SIZE, size, &bank,
                                   &bank_offset)) {
            value = 0;
            for (i = 0; i < size; i++) {
                value |= (uint64_t)s->ecc[bank][bank_offset + i] <<
                         (i * 8);
            }
            return value;
        }
        if (at91_pmecc_bank_access(offset, PMECC_REM_FIRST,
                                   PMECC_REM_SIZE, size, &bank,
                                   &bank_offset)) {
            value = 0;
            for (i = 0; i < size; i++) {
                unsigned int byte = bank_offset + i;
                uint8_t rem_byte = s->rem[bank][byte / 4] >>
                                   ((byte % 4) * 8);

                value |= (uint64_t)rem_byte << (i * 8);
            }
            return value;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_pmecc_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    switch (offset) {
    case PMECC_CFG:
        if (!s->enabled) {
            s->cfg = value & PMECC_CFG_MASK;
        } else {
            /*
             * AT91Bootstrap leaves the core enabled after its one-time
             * geometry setup, then switches between read and write by
             * changing only AUTO and NANDWR before RESET/DATA.  Real parts
             * accept that sequence, so retain the protected geometry fields
             * while allowing these operation-mode bits to change live.
             */
            s->cfg = (s->cfg & ~(PMECC_CFG_WRITE |
                                 PMECC_CFG_AUTO_ENABLE)) |
                     (value & (PMECC_CFG_WRITE |
                               PMECC_CFG_AUTO_ENABLE));
        }
        break;
    case PMECC_SAREA:
        if (!s->enabled) {
            s->sarea = value & 0x1ff;
        }
        break;
    case PMECC_SADDR:
        if (!s->enabled) {
            s->saddr = value & 0x1ff;
        }
        break;
    case PMECC_EADDR:
        if (!s->enabled) {
            s->eaddr = value & 0x1ff;
        }
        break;
    case PMECC_RESERVED_CLK:
        /* Kept for compatibility with the inherited Atmel PMECC driver. */
        break;
    case PMECC_CTRL:
        if (value & PMECC_CTRL_RST) {
            at91_pmecc_clear_stream(s);
            at91_pmecc_clear_results(s);
        }
        if (value & PMECC_CTRL_DISABLE) {
            s->enabled = false;
            at91_pmecc_stop_stream(s);
        }
        if (value & PMECC_CTRL_ENABLE) {
            s->enabled = true;
        }
        if (value & PMECC_CTRL_DATA) {
            at91_pmecc_start_data(s);
        }
        if (value & PMECC_CTRL_USER) {
            qemu_log_mask(LOG_UNIMP,
                          TYPE_AT91_PMECC ": USER phase is not implemented\n");
            at91_pmecc_stop_stream(s);
        }
        at91_pmecc_update_irq(s);
        break;
    case PMECC_IER:
        s->imr |= value & BIT(0);
        at91_pmecc_update_irq(s);
        break;
    case PMECC_IDR:
        s->imr &= ~(value & BIT(0));
        at91_pmecc_update_irq(s);
        break;
    case PMECC_SR:
    case PMECC_IMR:
    case PMECC_ISR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static void at91_pmerrloc_clear_results(AT91PMECCState *s)
{
    s->errloc_isr = 0;
    memset(s->errloc, 0, sizeof(s->errloc));
}

static uint16_t at91_pmerrloc_eval(AT91PMECCState *s, unsigned int degree,
                                   uint16_t x, unsigned int m)
{
    unsigned int field_mask = (1U << m) - 1;
    uint16_t value = s->sigma[degree] & field_mask;
    unsigned int i;

    for (i = degree; i > 0; i--) {
        value = at91_pmecc_gf_mul(s, value, x, m) ^
                (s->sigma[i - 1] & field_mask);
    }
    return value;
}

static void at91_pmerrloc_search(AT91PMECCState *s)
{
    unsigned int degree = (s->errloc_cfg >> PMERRLOC_CFG_ERRNUM_SHIFT) &
                          PMERRLOC_CFG_ERRNUM_MASK;
    unsigned int m = (s->errloc_cfg & BIT(0)) ? 14 : 13;
    unsigned int n = (1U << m) - 1;
    unsigned int length = s->errloc_len;
    unsigned int roots = 0;
    unsigned int exponent;
    unsigned int position;

    at91_pmerrloc_clear_results(s);
    if (degree > AT91_PMECC_MAX_STRENGTH || length > n ||
        !at91_pmecc_build_gf(s, m)) {
        s->errloc_isr = PMERRLOC_DONE;
        at91_pmecc_update_irq(s);
        return;
    }

    s->sigma[0] = 1;
    exponent = (1 + n - (length % n)) % n;
    for (position = 1; position <= length; position++) {
        if (!at91_pmerrloc_eval(s, degree, s->alpha_to[exponent], m)) {
            if (roots < AT91_PMECC_MAX_STRENGTH) {
                s->errloc[roots] = position;
            }
            roots++;
        }
        if (++exponent == n) {
            exponent = 0;
        }
    }

    roots = MIN(roots, AT91_PMECC_MAX_STRENGTH);
    s->errloc_isr = PMERRLOC_DONE |
                    (roots << PMERRLOC_ERR_COUNT_SHIFT);
    at91_pmecc_update_irq(s);
}

static uint64_t at91_pmerrloc_read(void *opaque, hwaddr offset,
                                   unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    switch (offset) {
    case PMERRLOC_CFG:
        return s->errloc_cfg;
    case PMERRLOC_PRIM:
        return 0;
    case PMERRLOC_LEN:
        return s->errloc_len;
    case PMERRLOC_DIS:
    case PMERRLOC_IER:
    case PMERRLOC_IDR:
        return 0;
    case PMERRLOC_SR:
        return 0;
    case PMERRLOC_IMR:
        return s->errloc_imr;
    case PMERRLOC_ISR:
        return s->errloc_isr;
    default:
        if (offset >= PMERRLOC_SIGMA0 && offset <= PMERRLOC_SIGMA_LAST &&
            !(offset & 3)) {
            if (offset == PMERRLOC_SIGMA0) {
                return 1;
            }
            return s->sigma[(offset - PMERRLOC_SIGMA0) / 4];
        }
        if (offset >= PMERRLOC_EL_FIRST && offset <= PMERRLOC_EL_LAST &&
            !(offset & 3)) {
            return s->errloc[(offset - PMERRLOC_EL_FIRST) / 4];
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": errloc read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_pmerrloc_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned int size)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    switch (offset) {
    case PMERRLOC_CFG:
        s->errloc_cfg = value & 0x001f0001;
        at91_pmerrloc_clear_results(s);
        at91_pmecc_update_irq(s);
        break;
    case PMERRLOC_LEN:
        s->errloc_len = value & 0x3fff;
        at91_pmerrloc_search(s);
        break;
    case PMERRLOC_DIS:
        if (value & BIT(0)) {
            at91_pmerrloc_clear_results(s);
            at91_pmecc_update_irq(s);
        }
        break;
    case PMERRLOC_IER:
        s->errloc_imr |= value & PMERRLOC_DONE;
        at91_pmecc_update_irq(s);
        break;
    case PMERRLOC_IDR:
        s->errloc_imr &= ~(value & PMERRLOC_DONE);
        at91_pmecc_update_irq(s);
        break;
    case PMERRLOC_PRIM:
    case PMERRLOC_SR:
    case PMERRLOC_IMR:
    case PMERRLOC_ISR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": errloc write to read-only offset"
                      " 0x%" HWADDR_PRIx "\n", offset);
        break;
    default:
        if (offset >= PMERRLOC_SIGMA0 && offset <= PMERRLOC_SIGMA_LAST &&
            !(offset & 3)) {
            if (offset != PMERRLOC_SIGMA0) {
                s->sigma[(offset - PMERRLOC_SIGMA0) / 4] =
                    value & 0x3fff;
            }
            break;
        }
        if (offset >= PMERRLOC_EL_FIRST && offset <= PMERRLOC_EL_LAST &&
            !(offset & 3)) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          TYPE_AT91_PMECC ": errloc write to read-only"
                          " offset 0x%" HWADDR_PRIx "\n", offset);
            break;
        }
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PMECC ": errloc write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static bool at91_pmecc_accepts(void *opaque, hwaddr offset,
                               unsigned int size, bool is_write,
                               MemTxAttrs attrs)
{
    unsigned int bank;
    unsigned int bank_offset;

    /* Control registers are word-wide; ECC and REM result lanes are bytes. */
    if (size == sizeof(uint32_t) && !(offset & 3)) {
        return true;
    }
    if (is_write || (size != 1 && size != 2) ||
        (offset & (size - 1))) {
        return false;
    }

    return at91_pmecc_bank_access(offset, PMECC_ECC_FIRST,
                                  PMECC_ECC_SIZE, size, &bank,
                                  &bank_offset) ||
           at91_pmecc_bank_access(offset, PMECC_REM_FIRST,
                                  PMECC_REM_SIZE, size, &bank,
                                  &bank_offset);
}

static const MemoryRegionOps at91_pmecc_ops = {
    .read = at91_pmecc_read,
    .write = at91_pmecc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
        .accepts = at91_pmecc_accepts,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const MemoryRegionOps at91_pmerrloc_ops = {
    .read = at91_pmerrloc_read,
    .write = at91_pmerrloc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_pmecc_reset(DeviceState *dev)
{
    AT91PMECCState *s = AT91_PMECC(dev);

    s->cfg = 0;
    s->sarea = 0;
    s->saddr = 0;
    s->eaddr = 0;
    s->imr = 0;
    s->enabled = false;
    at91_pmecc_clear_stream(s);
    at91_pmecc_clear_results(s);
    s->errloc_cfg = 0;
    s->errloc_prim = 0;
    s->errloc_len = 0;
    s->errloc_imr = 0;
    s->errloc_isr = 0;
    memset(s->sigma, 0, sizeof(s->sigma));
    s->sigma[0] = 1;
    memset(s->errloc, 0, sizeof(s->errloc));
    s->gf_m = 0;
    at91_pmecc_update_irq(s);
}

static bool at91_pmecc_stream_state_valid(AT91PMECCState *s)
{
    unsigned int expected_end;
    unsigned int i;

    if (!s->stream_active) {
        return !s->busy;
    }
    if (!s->enabled || !s->busy ||
        (s->stream_m != 13 && s->stream_m != 14) ||
        !s->stream_nsectors ||
        s->stream_nsectors > AT91_PMECC_MAX_SECTORS ||
        (s->stream_nsectors & (s->stream_nsectors - 1)) ||
        s->stream_sector_size != (s->stream_m == 13 ? 512 : 1024) ||
        s->stream_ecc_bits != s->stream_m * s->stream_strength ||
        s->stream_ecc_bytes != DIV_ROUND_UP(s->stream_ecc_bits, 8) ||
        s->stream_ecc_bytes > AT91_PMECC_ECC_BYTES ||
        s->stream_page_size !=
            s->stream_sector_size * s->stream_nsectors) {
        return false;
    }

    for (i = 0; i < ARRAY_SIZE(at91_pmecc_strengths); i++) {
        if (s->stream_strength == at91_pmecc_strengths[i]) {
            break;
        }
    }
    if (i == ARRAY_SIZE(at91_pmecc_strengths) ||
        DIV_ROUND_UP(s->stream_strength, 2) > AT91_PMECC_REM_REGS) {
        return false;
    }

    if (s->stream_write) {
        expected_end = s->stream_page_size;
    } else {
        if (s->saddr > s->eaddr || s->eaddr > s->sarea ||
            s->eaddr - s->saddr + 1 !=
                s->stream_nsectors * s->stream_ecc_bytes) {
            return false;
        }
        expected_end = s->stream_page_size + s->eaddr + 1;
    }

    return s->stream_next_column < expected_end &&
           at91_pmecc_prepare_math(s, s->stream_m,
                                   s->stream_strength);
}

static int at91_pmecc_post_load(void *opaque, int version_id)
{
    AT91PMECCState *s = AT91_PMECC(opaque);

    s->gf_m = 0;
    if (version_id < 2) {
        at91_pmecc_clear_stream(s);
        memset(s->ecc, 0xff, sizeof(s->ecc));
        memset(s->rem, 0, sizeof(s->rem));
        memset(s->errloc, 0, sizeof(s->errloc));
    } else if (!at91_pmecc_stream_state_valid(s)) {
        return -EINVAL;
    }

    s->errloc_prim = 0;
    s->sigma[0] = 1;
    s->imr &= BIT(0);
    s->isr &= PMECC_ISR_MASK;
    s->errloc_imr &= PMERRLOC_DONE;
    s->errloc_isr &= PMERRLOC_DONE | (PMERRLOC_CFG_ERRNUM_MASK <<
                                      PMERRLOC_ERR_COUNT_SHIFT);
    at91_pmecc_update_irq(s);
    return 0;
}

static const VMStateDescription at91_pmecc_vmstate = {
    .name = TYPE_AT91_PMECC,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = at91_pmecc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, AT91PMECCState),
        VMSTATE_UINT32(cfg, AT91PMECCState),
        VMSTATE_UINT32(sarea, AT91PMECCState),
        VMSTATE_UINT32(saddr, AT91PMECCState),
        VMSTATE_UINT32(eaddr, AT91PMECCState),
        VMSTATE_UINT32(imr, AT91PMECCState),
        VMSTATE_UINT32(isr, AT91PMECCState),
        VMSTATE_BOOL(enabled, AT91PMECCState),
        VMSTATE_UINT32(errloc_cfg, AT91PMECCState),
        VMSTATE_UINT32(errloc_prim, AT91PMECCState),
        VMSTATE_UINT32(errloc_len, AT91PMECCState),
        VMSTATE_UINT32(errloc_imr, AT91PMECCState),
        VMSTATE_UINT32(errloc_isr, AT91PMECCState),
        VMSTATE_UINT32_ARRAY(sigma, AT91PMECCState, 25),
        VMSTATE_UINT32_ARRAY_V(errloc, AT91PMECCState,
                               AT91_PMECC_MAX_STRENGTH, 2),
        VMSTATE_UINT8_2DARRAY_V(ecc, AT91PMECCState,
                                AT91_PMECC_MAX_SECTORS,
                                AT91_PMECC_ECC_BYTES, 2),
        VMSTATE_UINT32_2DARRAY_V(rem, AT91PMECCState,
                                 AT91_PMECC_MAX_SECTORS,
                                 AT91_PMECC_REM_REGS, 2),
        VMSTATE_BOOL_V(busy, AT91PMECCState, 2),
        VMSTATE_BOOL_V(stream_active, AT91PMECCState, 2),
        VMSTATE_BOOL_V(stream_write, AT91PMECCState, 2),
        VMSTATE_UINT8_V(stream_m, AT91PMECCState, 2),
        VMSTATE_UINT8_V(stream_strength, AT91PMECCState, 2),
        VMSTATE_UINT8_V(stream_nsectors, AT91PMECCState, 2),
        VMSTATE_UINT16_V(stream_sector_size, AT91PMECCState, 2),
        VMSTATE_UINT16_V(stream_ecc_bits, AT91PMECCState, 2),
        VMSTATE_UINT16_V(stream_ecc_bytes, AT91PMECCState, 2),
        VMSTATE_UINT32_V(stream_page_size, AT91PMECCState, 2),
        VMSTATE_UINT32_V(stream_next_column, AT91PMECCState, 2),
        VMSTATE_UINT64_2DARRAY_V(encode_rem, AT91PMECCState,
                                 AT91_PMECC_MAX_SECTORS,
                                 AT91_PMECC_PARITY_WORDS, 2),
        VMSTATE_UINT16_2DARRAY_V(decode_rem, AT91PMECCState,
                                 AT91_PMECC_MAX_SECTORS,
                                 AT91_PMECC_MAX_STRENGTH, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_pmecc_init(Object *obj)
{
    AT91PMECCState *s = AT91_PMECC(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->pmecc_mmio, obj, &at91_pmecc_ops, s,
                          TYPE_AT91_PMECC, PMECC_MMIO_SIZE);
    memory_region_init_io(&s->errloc_mmio, obj, &at91_pmerrloc_ops, s,
                          TYPE_AT91_PMECC ".errloc", PMERRLOC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->pmecc_mmio);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->errloc_mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    s->pclk = qdev_init_clock_in(dev, "pclk", NULL, NULL, 0);
    s->alpha_to = g_new0(uint16_t, PMECC_GF_MAX_SIZE);
    s->index_of = g_new(int16_t, PMECC_GF_MAX_SIZE);
}

static void at91_pmecc_finalize(Object *obj)
{
    AT91PMECCState *s = AT91_PMECC(obj);

    g_free(s->alpha_to);
    g_free(s->index_of);
}

static void at91_pmecc_realize(DeviceState *dev, Error **errp)
{
    AT91PMECCState *s = AT91_PMECC(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_PMECC ": pclk must be connected");
    }
}

static void at91_pmecc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 programmable multibit ECC controller";
    dc->realize = at91_pmecc_realize;
    dc->vmsd = &at91_pmecc_vmstate;
    device_class_set_legacy_reset(dc, at91_pmecc_reset);
}

static const TypeInfo at91_pmecc_info = {
    .name = TYPE_AT91_PMECC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91PMECCState),
    .instance_init = at91_pmecc_init,
    .instance_finalize = at91_pmecc_finalize,
    .class_init = at91_pmecc_class_init,
};

static void at91_pmecc_register_types(void)
{
    type_register_static(&at91_pmecc_info);
}

type_init(at91_pmecc_register_types)
