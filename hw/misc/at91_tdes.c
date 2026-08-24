/*
 * Microchip AT91 Triple Data Encryption Standard accelerator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/at91_tdes.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define TDES_CR                 0x00
#define TDES_MR                 0x04
#define TDES_IER                0x10
#define TDES_IDR                0x14
#define TDES_IMR                0x18
#define TDES_ISR                0x1c
#define TDES_KEY1WR0            0x20
#define TDES_KEY3WR1            0x34
#define TDES_IDATAR0            0x40
#define TDES_IDATAR1            0x44
#define TDES_ODATAR0            0x50
#define TDES_ODATAR1            0x54
#define TDES_IVR0               0x60
#define TDES_IVR1               0x64
#define TDES_XTEA_RNDR          0x70
#define TDES_WPMR               0xe4
#define TDES_WPSR               0xe8
#define TDES_VERSION            0xfc
#define TDES_MMIO_SIZE          0x100

#define TDES_CR_START           BIT(0)
#define TDES_CR_SWRST           BIT(8)
#define TDES_CR_UNLOCK          BIT(24)

#define TDES_MR_CIPHER          BIT(0)
#define TDES_MR_ALGO_SHIFT      1
#define TDES_MR_ALGO_MASK       (3U << TDES_MR_ALGO_SHIFT)
#define TDES_MR_KEYMOD          BIT(4)
#define TDES_MR_PKWO            BIT(6)
#define TDES_MR_PKRS            BIT(7)
#define TDES_MR_SMOD_SHIFT      8
#define TDES_MR_SMOD_MASK       (3U << TDES_MR_SMOD_SHIFT)
#define TDES_MR_OPMOD_SHIFT     12
#define TDES_MR_OPMOD_MASK      (3U << TDES_MR_OPMOD_SHIFT)
#define TDES_MR_LOD             BIT(15)
#define TDES_MR_CFBS_SHIFT      16
#define TDES_MR_CFBS_MASK       (3U << TDES_MR_CFBS_SHIFT)
#define TDES_MR_TAMPCLR         BIT(31)
#define TDES_MR_MASK            (TDES_MR_CIPHER | TDES_MR_ALGO_MASK | \
                                 TDES_MR_KEYMOD | TDES_MR_PKWO | \
                                 TDES_MR_PKRS | TDES_MR_SMOD_MASK | \
                                 TDES_MR_OPMOD_MASK | TDES_MR_LOD | \
                                 TDES_MR_CFBS_MASK | TDES_MR_TAMPCLR)
#define TDES_MR_RESET           0x00000002

#define TDES_ALGO_DES           0
#define TDES_ALGO_TDES          1
#define TDES_ALGO_XTEA          2

#define TDES_SMOD_MANUAL        0
#define TDES_SMOD_AUTO          1
#define TDES_SMOD_DMA           2

#define TDES_OPMODE_ECB         0
#define TDES_OPMODE_CBC         1
#define TDES_OPMODE_OFB         2
#define TDES_OPMODE_CFB         3

#define TDES_INT_DATRDY         BIT(0)
#define TDES_INT_URAD           BIT(8)
#define TDES_ISR_URAT_SHIFT     12
#define TDES_ISR_URAT_MASK      (3U << TDES_ISR_URAT_SHIFT)
#define TDES_INT_SECE           BIT(16)
#define TDES_INT_MASK           (TDES_INT_DATRDY | TDES_INT_URAD | \
                                 TDES_INT_SECE)

#define TDES_URAT_INPUT_BUSY    0
#define TDES_URAT_OUTPUT_BUSY   1
#define TDES_URAT_MR_BUSY       2
#define TDES_URAT_READ_WO       3

#define TDES_WPMR_KEY_MASK      0xffffff00
#define TDES_WPMR_KEY           0x44455300
#define TDES_WPMR_WPEN          BIT(0)
#define TDES_WPMR_WPITEN        BIT(1)
#define TDES_WPMR_WPCREN        BIT(2)
#define TDES_WPMR_FIRSTE        BIT(4)
#define TDES_WPMR_ACTION_SHIFT  5
#define TDES_WPMR_ACTION_MASK   (7U << TDES_WPMR_ACTION_SHIFT)
#define TDES_WPMR_MASK          (TDES_WPMR_WPEN | TDES_WPMR_WPITEN | \
                                 TDES_WPMR_WPCREN | TDES_WPMR_FIRSTE | \
                                 TDES_WPMR_ACTION_MASK)

#define TDES_WPSR_WPVS          BIT(0)
#define TDES_WPSR_SWE           BIT(3)
#define TDES_WPSR_WPVSRC_SHIFT  8
#define TDES_WPSR_WPVSRC_MASK   (0xffffU << TDES_WPSR_WPVSRC_SHIFT)
#define TDES_WPSR_SWETYP_SHIFT  24
#define TDES_WPSR_SWETYP_MASK   (0xfU << TDES_WPSR_SWETYP_SHIFT)
#define TDES_WPSR_ECLASS        BIT(31)

#define TDES_SWE_READ_WO        0
#define TDES_SWE_WRITE_RO       1
#define TDES_SWE_UNDEF_RW       2
#define TDES_SWE_CTRL_START     3
#define TDES_SWE_WEIRD_ACTION   4
#define TDES_SWE_INCOMPLETE_KEY 5

/* FIPS 46-3 permutation tables.  Entries count from the input MSB at one. */
static const uint8_t des_ip[64] = {
    58, 50, 42, 34, 26, 18, 10, 2,
    60, 52, 44, 36, 28, 20, 12, 4,
    62, 54, 46, 38, 30, 22, 14, 6,
    64, 56, 48, 40, 32, 24, 16, 8,
    57, 49, 41, 33, 25, 17, 9, 1,
    59, 51, 43, 35, 27, 19, 11, 3,
    61, 53, 45, 37, 29, 21, 13, 5,
    63, 55, 47, 39, 31, 23, 15, 7,
};

static const uint8_t des_fp[64] = {
    40, 8, 48, 16, 56, 24, 64, 32,
    39, 7, 47, 15, 55, 23, 63, 31,
    38, 6, 46, 14, 54, 22, 62, 30,
    37, 5, 45, 13, 53, 21, 61, 29,
    36, 4, 44, 12, 52, 20, 60, 28,
    35, 3, 43, 11, 51, 19, 59, 27,
    34, 2, 42, 10, 50, 18, 58, 26,
    33, 1, 41, 9, 49, 17, 57, 25,
};

static const uint8_t des_e[48] = {
    32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9,
    8, 9, 10, 11, 12, 13, 12, 13, 14, 15, 16, 17,
    16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
    24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1,
};

static const uint8_t des_p[32] = {
    16, 7, 20, 21, 29, 12, 28, 17,
    1, 15, 23, 26, 5, 18, 31, 10,
    2, 8, 24, 14, 32, 27, 3, 9,
    19, 13, 30, 6, 22, 11, 4, 25,
};

static const uint8_t des_pc1[56] = {
    57, 49, 41, 33, 25, 17, 9,
    1, 58, 50, 42, 34, 26, 18,
    10, 2, 59, 51, 43, 35, 27,
    19, 11, 3, 60, 52, 44, 36,
    63, 55, 47, 39, 31, 23, 15,
    7, 62, 54, 46, 38, 30, 22,
    14, 6, 61, 53, 45, 37, 29,
    21, 13, 5, 28, 20, 12, 4,
};

static const uint8_t des_pc2[48] = {
    14, 17, 11, 24, 1, 5, 3, 28,
    15, 6, 21, 10, 23, 19, 12, 4,
    26, 8, 16, 7, 27, 20, 13, 2,
    41, 52, 31, 37, 47, 55, 30, 40,
    51, 45, 33, 48, 44, 49, 39, 56,
    34, 53, 46, 42, 50, 36, 29, 32,
};

static const uint8_t des_shifts[16] = {
    1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1,
};

static const uint8_t des_sbox[8][64] = {
    {
        14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
        0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
        4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
        15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13,
    }, {
        15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
        3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
        0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
        13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9,
    }, {
        10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
        13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
        13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
        1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12,
    }, {
        7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
        13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
        10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
        3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14,
    }, {
        2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
        14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
        4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
        11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3,
    }, {
        12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
        10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
        9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
        4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13,
    }, {
        4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
        13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
        1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
        6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12,
    }, {
        13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
        1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
        7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
        2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11,
    },
};

static uint64_t at91_tdes_permute(uint64_t input, const uint8_t *table,
                                  unsigned int output_bits,
                                  unsigned int input_bits)
{
    uint64_t output = 0;
    unsigned int i;

    for (i = 0; i < output_bits; i++) {
        output <<= 1;
        output |= (input >> (input_bits - table[i])) & 1;
    }
    return output;
}

static void at91_tdes_make_subkeys(const uint8_t key[8],
                                   uint64_t subkeys[16])
{
    uint64_t permuted = at91_tdes_permute(ldq_be_p(key), des_pc1,
                                          ARRAY_SIZE(des_pc1), 64);
    uint32_t c = permuted >> 28;
    uint32_t d = permuted & 0x0fffffff;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(des_shifts); i++) {
        unsigned int shift = des_shifts[i];
        uint64_t cd;

        c = ((c << shift) | (c >> (28 - shift))) & 0x0fffffff;
        d = ((d << shift) | (d >> (28 - shift))) & 0x0fffffff;
        cd = (uint64_t)c << 28 | d;
        subkeys[i] = at91_tdes_permute(cd, des_pc2,
                                      ARRAY_SIZE(des_pc2), 56);
    }
}

static uint32_t at91_tdes_feistel(uint32_t right, uint64_t subkey)
{
    uint64_t expanded = at91_tdes_permute(right, des_e,
                                          ARRAY_SIZE(des_e), 32) ^ subkey;
    uint32_t substituted = 0;
    unsigned int i;

    for (i = 0; i < 8; i++) {
        unsigned int six = (expanded >> (42 - i * 6)) & 0x3f;
        unsigned int row = ((six & 0x20) >> 4) | (six & 1);
        unsigned int column = (six >> 1) & 0xf;

        substituted = (substituted << 4) |
                      des_sbox[i][row * 16 + column];
    }
    return at91_tdes_permute(substituted, des_p, ARRAY_SIZE(des_p), 32);
}

static void at91_tdes_des_crypt(const uint8_t input[8], uint8_t output[8],
                                const uint8_t key[8], bool decrypt)
{
    uint64_t subkeys[16];
    uint64_t block;
    uint32_t left;
    uint32_t right;
    unsigned int i;

    at91_tdes_make_subkeys(key, subkeys);
    block = at91_tdes_permute(ldq_be_p(input), des_ip,
                             ARRAY_SIZE(des_ip), 64);
    left = block >> 32;
    right = block;
    for (i = 0; i < ARRAY_SIZE(subkeys); i++) {
        uint32_t next = left ^ at91_tdes_feistel(
            right, subkeys[decrypt ? ARRAY_SIZE(subkeys) - 1 - i : i]);

        left = right;
        right = next;
    }
    block = (uint64_t)right << 32 | left;
    stq_be_p(output, at91_tdes_permute(block, des_fp,
                                      ARRAY_SIZE(des_fp), 64));
}

static void at91_tdes_xtea_crypt(const AT91TDESState *s,
                                 const uint8_t input[8], uint8_t output[8],
                                 bool decrypt)
{
    const uint32_t delta = 0x9e3779b9;
    uint32_t value0 = ldl_le_p(input);
    uint32_t value1 = ldl_le_p(input + 4);
    unsigned int rounds = (s->xtea_rounds & 0x3f) + 1;
    uint32_t sum;
    unsigned int i;

    if (decrypt) {
        sum = delta * rounds;
        for (i = 0; i < rounds; i++) {
            value1 -= (((value0 << 4) ^ (value0 >> 5)) + value0) ^
                      (sum + s->key[(sum >> 11) & 3]);
            sum -= delta;
            value0 -= (((value1 << 4) ^ (value1 >> 5)) + value1) ^
                      (sum + s->key[sum & 3]);
        }
    } else {
        sum = 0;
        for (i = 0; i < rounds; i++) {
            value0 += (((value1 << 4) ^ (value1 >> 5)) + value1) ^
                      (sum + s->key[sum & 3]);
            sum += delta;
            value1 += (((value0 << 4) ^ (value0 >> 5)) + value0) ^
                      (sum + s->key[(sum >> 11) & 3]);
        }
    }
    stl_le_p(output, value0);
    stl_le_p(output + 4, value1);
}

static unsigned int at91_tdes_algorithm(const AT91TDESState *s)
{
    return (s->mr & TDES_MR_ALGO_MASK) >> TDES_MR_ALGO_SHIFT;
}

static unsigned int at91_tdes_smod(const AT91TDESState *s)
{
    return (s->mr & TDES_MR_SMOD_MASK) >> TDES_MR_SMOD_SHIFT;
}

static unsigned int at91_tdes_opmode(const AT91TDESState *s)
{
    return (s->mr & TDES_MR_OPMOD_MASK) >> TDES_MR_OPMOD_SHIFT;
}

static unsigned int at91_tdes_transfer_size(const AT91TDESState *s)
{
    static const uint8_t cfb_size[] = { 8, 4, 2, 1 };
    unsigned int cfbs;

    if (at91_tdes_opmode(s) != TDES_OPMODE_CFB) {
        return 8;
    }
    cfbs = (s->mr & TDES_MR_CFBS_MASK) >> TDES_MR_CFBS_SHIFT;
    return cfb_size[cfbs];
}

static uint8_t at91_tdes_required_key_mask(const AT91TDESState *s)
{
    switch (at91_tdes_algorithm(s)) {
    case TDES_ALGO_DES:
        return 0x03;
    case TDES_ALGO_TDES:
        return s->mr & TDES_MR_KEYMOD ? 0x0f : 0x3f;
    case TDES_ALGO_XTEA:
        return 0x0f;
    default:
        return 0;
    }
}

static bool at91_tdes_key_complete(const AT91TDESState *s)
{
    uint8_t required = at91_tdes_required_key_mask(s);

    return !(s->mr & TDES_MR_PKRS) && required &&
           (s->key_written & required) == required;
}

static void at91_tdes_primitive(const AT91TDESState *s,
                                const uint8_t input[8], uint8_t output[8],
                                bool decrypt)
{
    uint8_t key[24];
    uint8_t temporary[8];
    unsigned int i;

    if (at91_tdes_algorithm(s) == TDES_ALGO_XTEA) {
        at91_tdes_xtea_crypt(s, input, output, decrypt);
        return;
    }
    for (i = 0; i < ARRAY_SIZE(s->key); i++) {
        stl_le_p(key + i * 4, s->key[i]);
    }
    if (at91_tdes_algorithm(s) == TDES_ALGO_DES) {
        at91_tdes_des_crypt(input, output, key, decrypt);
        return;
    }

    if (decrypt) {
        const uint8_t *key3 = s->mr & TDES_MR_KEYMOD ? key : key + 16;

        at91_tdes_des_crypt(input, temporary, key3, true);
        at91_tdes_des_crypt(temporary, output, key + 8, false);
        at91_tdes_des_crypt(output, temporary, key, true);
        memcpy(output, temporary, sizeof(temporary));
    } else {
        const uint8_t *key3 = s->mr & TDES_MR_KEYMOD ? key : key + 16;

        at91_tdes_des_crypt(input, temporary, key, false);
        at91_tdes_des_crypt(temporary, output, key + 8, true);
        at91_tdes_des_crypt(output, temporary, key3, false);
        memcpy(output, temporary, sizeof(temporary));
    }
}

static void at91_tdes_words_to_bytes(const uint32_t words[2],
                                     uint8_t bytes[8])
{
    stl_le_p(bytes, words[0]);
    stl_le_p(bytes + 4, words[1]);
}

static void at91_tdes_bytes_to_words(const uint8_t bytes[8],
                                     uint32_t words[2])
{
    words[0] = ldl_le_p(bytes);
    words[1] = ldl_le_p(bytes + 4);
}

static void at91_tdes_process_block(AT91TDESState *s)
{
    uint8_t iv[8];
    uint8_t temporary[8];
    uint8_t original[8];
    unsigned int segment = s->output_size;
    unsigned int i;
    bool decrypt = !(s->mr & TDES_MR_CIPHER);

    memcpy(original, s->input, sizeof(original));
    at91_tdes_words_to_bytes(s->iv, iv);
    switch (at91_tdes_opmode(s)) {
    case TDES_OPMODE_ECB:
        at91_tdes_primitive(s, s->input, s->output, decrypt);
        break;
    case TDES_OPMODE_CBC:
        if (decrypt) {
            at91_tdes_primitive(s, s->input, temporary, true);
            for (i = 0; i < 8; i++) {
                s->output[i] = temporary[i] ^ iv[i];
            }
            memcpy(iv, original, sizeof(iv));
        } else {
            for (i = 0; i < 8; i++) {
                temporary[i] = s->input[i] ^ iv[i];
            }
            at91_tdes_primitive(s, temporary, s->output, false);
            memcpy(iv, s->output, sizeof(iv));
        }
        at91_tdes_bytes_to_words(iv, s->iv);
        break;
    case TDES_OPMODE_OFB:
        at91_tdes_primitive(s, iv, temporary, false);
        for (i = 0; i < 8; i++) {
            s->output[i] = s->input[i] ^ temporary[i];
        }
        at91_tdes_bytes_to_words(temporary, s->iv);
        break;
    case TDES_OPMODE_CFB:
        at91_tdes_primitive(s, iv, temporary, false);
        for (i = 0; i < segment; i++) {
            s->output[i] = s->input[i] ^ temporary[i];
        }
        memmove(iv, iv + segment, sizeof(iv) - segment);
        memcpy(iv + sizeof(iv) - segment,
               decrypt ? original : s->output, segment);
        at91_tdes_bytes_to_words(iv, s->iv);
        break;
    default:
        g_assert_not_reached();
    }
}

static void at91_tdes_update_irq(AT91TDESState *s)
{
    qemu_set_irq(s->irq, !!(s->isr & s->imr & TDES_INT_MASK));
}

static void at91_tdes_set_request(qemu_irq request, bool *old_level,
                                  bool new_level)
{
    if (*old_level != new_level) {
        *old_level = new_level;
        qemu_set_irq(request, new_level);
    }
}

static void at91_tdes_update_requests(AT91TDESState *s)
{
    bool dma = at91_tdes_smod(s) == TDES_SMOD_DMA;
    bool clocked = clock_get_hz(s->pclk) != 0;
    bool tx = dma && clocked && !s->busy && !s->locked &&
              !s->output_pending && at91_tdes_key_complete(s);
    bool rx = dma && clocked && s->output_pending &&
              !(s->mr & TDES_MR_LOD);

    at91_tdes_set_request(s->tx_request, &s->tx_request_level, tx);
    at91_tdes_set_request(s->rx_request, &s->rx_request_level, rx);
}

static void at91_tdes_set_isr(AT91TDESState *s, uint32_t bits)
{
    s->isr |= bits;
    at91_tdes_update_irq(s);
}

static void at91_tdes_clear_isr(AT91TDESState *s, uint32_t bits)
{
    s->isr &= ~bits;
    at91_tdes_update_irq(s);
}

static void at91_tdes_raise_urad(AT91TDESState *s, unsigned int type)
{
    s->isr &= ~TDES_ISR_URAT_MASK;
    s->isr |= TDES_INT_URAD | (type << TDES_ISR_URAT_SHIFT);
    at91_tdes_update_irq(s);
}

static void at91_tdes_apply_software_action(AT91TDESState *s)
{
    unsigned int action = (s->wpmr & TDES_WPMR_ACTION_MASK) >>
                          TDES_WPMR_ACTION_SHIFT;

    if (action == 4 || action == 6) {
        memset(s->key, 0, sizeof(s->key));
        s->key_written = 0;
    }
    if (action == 1 || action == 3 || action == 4 || action == 6) {
        s->locked = true;
    }
    at91_tdes_update_requests(s);
}

static void at91_tdes_raise_swe(AT91TDESState *s, hwaddr offset,
                                unsigned int type, bool error)
{
    bool first_only = s->wpmr & TDES_WPMR_FIRSTE;

    if (!first_only || !(s->wpsr & TDES_WPSR_SWE)) {
        s->wpsr &= ~(TDES_WPSR_ECLASS | TDES_WPSR_SWETYP_MASK);
        s->wpsr |= TDES_WPSR_SWE |
                   (type << TDES_WPSR_SWETYP_SHIFT);
        if (!(s->wpsr & TDES_WPSR_WPVS)) {
            s->wpsr &= ~TDES_WPSR_WPVSRC_MASK;
            s->wpsr |= (offset & 0xffff) << TDES_WPSR_WPVSRC_SHIFT;
        }
        if (error) {
            s->wpsr |= TDES_WPSR_ECLASS;
        }
    }
    s->reports_read = false;
    at91_tdes_set_isr(s, TDES_INT_SECE);
    at91_tdes_apply_software_action(s);
}

static bool at91_tdes_write_protected(AT91TDESState *s, hwaddr offset,
                                      uint32_t enable)
{
    if (!(s->wpmr & enable)) {
        return false;
    }
    if (!(s->wpmr & TDES_WPMR_FIRSTE) ||
        !(s->wpsr & TDES_WPSR_WPVS)) {
        s->wpsr &= ~TDES_WPSR_WPVSRC_MASK;
        s->wpsr |= TDES_WPSR_WPVS |
                   ((offset & 0xffff) << TDES_WPSR_WPVSRC_SHIFT);
    }
    s->reports_read = false;
    at91_tdes_set_isr(s, TDES_INT_SECE);
    at91_tdes_apply_software_action(s);
    return true;
}

static uint64_t at91_tdes_processing_cycles(const AT91TDESState *s)
{
    switch (at91_tdes_algorithm(s)) {
    case TDES_ALGO_DES:
        return 18;
    case TDES_ALGO_TDES:
        return 50;
    case TDES_ALGO_XTEA:
        /* One complete round contains two Feistel rounds. */
        return 2 * ((s->xtea_rounds & 0x3f) + 1) + 2;
    default:
        return 1;
    }
}

static void at91_tdes_schedule(AT91TDESState *s)
{
    uint64_t duration;

    if (!s->busy || timer_pending(s->processing_timer) ||
        !clock_get_hz(s->pclk)) {
        return;
    }
    duration = MAX(clock_ticks_to_ns(s->pclk,
                                     at91_tdes_processing_cycles(s)), 1);
    timer_mod_ns(s->processing_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration);
}

static void at91_tdes_start(AT91TDESState *s, bool automatic)
{
    if (s->locked) {
        at91_tdes_raise_swe(s, TDES_CR,
                            automatic ? TDES_SWE_WEIRD_ACTION :
                                        TDES_SWE_CTRL_START,
                            false);
        return;
    }
    if (s->busy) {
        if (at91_tdes_smod(s) == TDES_SMOD_DMA) {
            at91_tdes_raise_urad(s, TDES_URAT_INPUT_BUSY);
        }
        at91_tdes_raise_swe(s, TDES_CR, TDES_SWE_WEIRD_ACTION, true);
        return;
    }
    if (!at91_tdes_key_complete(s)) {
        at91_tdes_raise_swe(s, TDES_CR, TDES_SWE_INCOMPLETE_KEY, true);
        return;
    }

    if (!automatic) {
        at91_tdes_clear_isr(s, TDES_INT_DATRDY);
    }
    s->busy = true;
    s->output_pending = false;
    s->dma_output_pos = 0;
    s->output_size = at91_tdes_transfer_size(s);
    at91_tdes_update_requests(s);
    at91_tdes_schedule(s);
}

static void at91_tdes_processing_done(void *opaque)
{
    AT91TDESState *s = opaque;

    if (!s->busy || !clock_get_hz(s->pclk)) {
        return;
    }
    at91_tdes_process_block(s);
    memset(s->input, 0, sizeof(s->input));
    s->input_valid = 0;
    s->dma_input_pos = 0;
    s->busy = false;
    s->output_pending = at91_tdes_smod(s) == TDES_SMOD_DMA &&
                        !(s->mr & TDES_MR_LOD);
    at91_tdes_set_isr(s, TDES_INT_DATRDY);
    at91_tdes_update_requests(s);
}

static uint64_t at91_tdes_load_le(const uint8_t *data, unsigned int size)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static void at91_tdes_store_le(uint8_t *data, uint64_t value,
                               unsigned int size)
{
    unsigned int i;

    for (i = 0; i < size; i++) {
        data[i] = value >> (i * 8);
    }
}

static void at91_tdes_finish_dma_output(AT91TDESState *s)
{
    s->output_pending = false;
    s->dma_output_pos = 0;
    at91_tdes_clear_isr(s, TDES_INT_DATRDY);
    at91_tdes_update_requests(s);
}

static uint64_t at91_tdes_read_output(AT91TDESState *s, hwaddr offset,
                                      unsigned int size)
{
    unsigned int position;
    uint64_t value;

    if (s->busy) {
        at91_tdes_raise_urad(s, TDES_URAT_OUTPUT_BUSY);
        at91_tdes_raise_swe(s, offset, TDES_SWE_WEIRD_ACTION, true);
        return 0;
    }
    if (at91_tdes_smod(s) == TDES_SMOD_DMA) {
        if (offset == TDES_ODATAR0 && s->output_pending) {
            position = s->dma_output_pos;
            if (position + size > s->output_size) {
                return 0;
            }
            value = at91_tdes_load_le(s->output + position, size);
            s->dma_output_pos += size;
            if (s->dma_output_pos >= s->output_size) {
                at91_tdes_finish_dma_output(s);
            }
            return value;
        }
        at91_tdes_raise_swe(s, offset, TDES_SWE_WEIRD_ACTION, false);
        return 0;
    }

    position = offset - TDES_ODATAR0;
    if (position + size > sizeof(s->output)) {
        return 0;
    }
    value = at91_tdes_load_le(s->output + position, size);
    at91_tdes_clear_isr(s, TDES_INT_DATRDY);
    return value;
}

static uint64_t at91_tdes_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91TDESState *s = AT91_TDES(opaque);
    uint32_t value;

    if (offset >= TDES_ODATAR0 && offset < TDES_ODATAR1 + 4) {
        return at91_tdes_read_output(s, offset, size);
    }

    switch (offset) {
    case TDES_MR:
        return s->mr;
    case TDES_IMR:
        return s->imr;
    case TDES_ISR:
        value = s->isr;
        s->isr &= ~TDES_INT_SECE;
        at91_tdes_update_irq(s);
        return value;
    case TDES_XTEA_RNDR:
        return s->xtea_rounds;
    case TDES_WPMR:
        return s->wpmr;
    case TDES_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        s->reports_read = true;
        return value;
    case TDES_VERSION:
        return s->version;
    case TDES_CR:
    case TDES_IER:
    case TDES_IDR:
        at91_tdes_raise_urad(s, TDES_URAT_READ_WO);
        at91_tdes_raise_swe(s, offset, TDES_SWE_READ_WO, false);
        return 0;
    default:
        if ((offset >= TDES_KEY1WR0 && offset <= TDES_KEY3WR1) ||
            (offset >= TDES_IDATAR0 && offset < TDES_IDATAR1 + 4) ||
            (offset >= TDES_IVR0 && offset < TDES_IVR1 + 4)) {
            at91_tdes_raise_urad(s, TDES_URAT_READ_WO);
            at91_tdes_raise_swe(s, offset, TDES_SWE_READ_WO, false);
            return 0;
        }
        at91_tdes_raise_swe(s, offset, TDES_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TDES ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_tdes_reset_registers(AT91TDESState *s, bool hardware)
{
    timer_del(s->processing_timer);
    s->mr = TDES_MR_RESET;
    s->imr = 0;
    s->isr = 0;
    memset(s->key, 0, sizeof(s->key));
    memset(s->iv, 0, sizeof(s->iv));
    s->xtea_rounds = 0;
    memset(s->input, 0, sizeof(s->input));
    memset(s->output, 0, sizeof(s->output));
    s->input_valid = 0;
    s->key_written = 0;
    s->dma_input_pos = 0;
    s->dma_output_pos = 0;
    s->output_size = 8;
    s->busy = false;
    s->output_pending = false;
    if (hardware) {
        s->wpmr = 0;
        s->wpsr = 0;
        s->locked = false;
        s->private_key_write_once = false;
        s->reports_read = false;
    }
    at91_tdes_update_irq(s);
    at91_tdes_update_requests(s);
}

static void at91_tdes_write_input(AT91TDESState *s, hwaddr offset,
                                  uint64_t value, unsigned int size)
{
    unsigned int expected = at91_tdes_transfer_size(s);
    unsigned int input_size = size;
    unsigned int position;
    uint8_t expected_mask;

    if (s->busy || s->output_pending) {
        if (at91_tdes_smod(s) == TDES_SMOD_DMA) {
            at91_tdes_raise_urad(s, TDES_URAT_INPUT_BUSY);
        }
        at91_tdes_raise_swe(s, offset, TDES_SWE_WEIRD_ACTION, true);
        return;
    }
    if (s->mr & TDES_MR_LOD) {
        at91_tdes_clear_isr(s, TDES_INT_DATRDY);
    }

    if (at91_tdes_smod(s) == TDES_SMOD_DMA && offset == TDES_IDATAR0) {
        position = s->dma_input_pos;
        /* DMA input remains word-wide for CFB8 and CFB16. */
        if (at91_tdes_opmode(s) == TDES_OPMODE_CFB && expected < size &&
            size == sizeof(uint32_t) && position == 0) {
            input_size = expected;
        }
        if (position + input_size > expected) {
            at91_tdes_raise_urad(s, TDES_URAT_INPUT_BUSY);
            return;
        }
        at91_tdes_store_le(s->input + position, value, input_size);
        s->dma_input_pos += input_size;
        if (s->dma_input_pos >= expected) {
            at91_tdes_start(s, true);
        }
        return;
    }

    position = offset - TDES_IDATAR0;
    if (position + size > sizeof(s->input)) {
        return;
    }
    if (expected < 4 && position == 0 && size == 4) {
        input_size = expected;
    }
    if (position + input_size > expected) {
        return;
    }
    at91_tdes_store_le(s->input + position, value, input_size);
    s->input_valid |= MAKE_64BIT_MASK(position, input_size);
    expected_mask = MAKE_64BIT_MASK(0, expected);
    if ((s->input_valid & expected_mask) == expected_mask &&
        at91_tdes_smod(s) != TDES_SMOD_MANUAL) {
        at91_tdes_start(s, true);
    } else {
        at91_tdes_update_requests(s);
    }
}

static bool at91_tdes_config_protected_offset(hwaddr offset)
{
    return offset == TDES_MR ||
           (offset >= TDES_KEY1WR0 && offset <= TDES_KEY3WR1) ||
           (offset >= TDES_IVR0 && offset <= TDES_IVR1) ||
           offset == TDES_XTEA_RNDR;
}

static void at91_tdes_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91TDESState *s = AT91_TDES(opaque);
    uint32_t val = value;
    unsigned int index;

    if (at91_tdes_config_protected_offset(offset) &&
        at91_tdes_write_protected(s, offset, TDES_WPMR_WPEN)) {
        return;
    }
    if ((offset == TDES_IER || offset == TDES_IDR) &&
        at91_tdes_write_protected(s, offset, TDES_WPMR_WPITEN)) {
        return;
    }
    if (offset == TDES_CR &&
        at91_tdes_write_protected(s, offset, TDES_WPMR_WPCREN)) {
        return;
    }

    if (offset >= TDES_KEY1WR0 && offset <= TDES_KEY3WR1) {
        if (s->busy) {
            at91_tdes_raise_swe(s, offset, TDES_SWE_WEIRD_ACTION, true);
            return;
        }
        index = (offset - TDES_KEY1WR0) / 4;
        if (index == 0) {
            s->key_written = 0;
        }
        s->key[index] = val;
        s->key_written |= BIT(index);
        at91_tdes_update_requests(s);
        return;
    }
    if (offset >= TDES_IDATAR0 && offset < TDES_IDATAR1 + 4) {
        at91_tdes_write_input(s, offset, value, size);
        return;
    }
    if (offset >= TDES_IVR0 && offset <= TDES_IVR1) {
        if (s->busy) {
            at91_tdes_raise_swe(s, offset, TDES_SWE_WEIRD_ACTION, true);
            return;
        }
        index = (offset - TDES_IVR0) / 4;
        s->iv[index] = val;
        return;
    }

    switch (offset) {
    case TDES_CR:
        if (val & TDES_CR_SWRST) {
            at91_tdes_reset_registers(s, false);
            return;
        }
        if (val & TDES_CR_START) {
            if (at91_tdes_smod(s) == TDES_SMOD_MANUAL) {
                at91_tdes_start(s, false);
            } else {
                at91_tdes_raise_swe(s, offset, TDES_SWE_CTRL_START,
                                    false);
            }
        }
        if ((val & TDES_CR_UNLOCK) && s->reports_read) {
            s->locked = false;
            at91_tdes_update_requests(s);
        }
        break;
    case TDES_MR:
        if (s->busy) {
            at91_tdes_raise_urad(s, TDES_URAT_MR_BUSY);
            at91_tdes_raise_swe(s, offset, TDES_SWE_WEIRD_ACTION, true);
            break;
        }
        s->mr = val & TDES_MR_MASK & ~TDES_MR_PKWO;
        if (val & TDES_MR_PKWO) {
            s->private_key_write_once = true;
        }
        s->input_valid = 0;
        s->dma_input_pos = 0;
        s->output_size = at91_tdes_transfer_size(s);
        if (at91_tdes_smod(s) != TDES_SMOD_DMA) {
            s->output_pending = false;
            s->dma_output_pos = 0;
        }
        at91_tdes_update_requests(s);
        break;
    case TDES_IER:
        s->imr |= val & TDES_INT_MASK;
        at91_tdes_update_irq(s);
        break;
    case TDES_IDR:
        s->imr &= ~(val & TDES_INT_MASK);
        at91_tdes_update_irq(s);
        break;
    case TDES_XTEA_RNDR:
        if (s->busy) {
            at91_tdes_raise_swe(s, offset, TDES_SWE_WEIRD_ACTION, true);
            break;
        }
        s->xtea_rounds = val & 0x3f;
        break;
    case TDES_WPMR:
        if ((val & TDES_WPMR_KEY_MASK) == TDES_WPMR_KEY) {
            s->wpmr = val & TDES_WPMR_MASK;
        }
        break;
    case TDES_IMR:
    case TDES_ISR:
    case TDES_ODATAR0 ... TDES_ODATAR1:
    case TDES_WPSR:
    case TDES_VERSION:
        at91_tdes_raise_swe(s, offset, TDES_SWE_WRITE_RO, false);
        break;
    default:
        at91_tdes_raise_swe(s, offset, TDES_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_TDES ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_tdes_ops = {
    .read = at91_tdes_read,
    .write = at91_tdes_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_tdes_clock_changed(void *opaque, ClockEvent event)
{
    AT91TDESState *s = opaque;

    timer_del(s->processing_timer);
    at91_tdes_schedule(s);
    at91_tdes_update_requests(s);
}

static void at91_tdes_reset(DeviceState *dev)
{
    at91_tdes_reset_registers(AT91_TDES(dev), true);
}

static void at91_tdes_init(Object *obj)
{
    AT91TDESState *s = AT91_TDES(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &at91_tdes_ops, s,
                          TYPE_AT91_TDES, TDES_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_tdes_clock_changed,
                                 s, ClockUpdate);
    s->processing_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       at91_tdes_processing_done, s);
}

static void at91_tdes_finalize(Object *obj)
{
    AT91TDESState *s = AT91_TDES(obj);

    timer_free(s->processing_timer);
}

static void at91_tdes_realize(DeviceState *dev, Error **errp)
{
    AT91TDESState *s = AT91_TDES(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_TDES ": pclk must be connected");
    }
}

static int at91_tdes_post_load(void *opaque, int version_id)
{
    AT91TDESState *s = opaque;

    s->mr &= TDES_MR_MASK;
    s->imr &= TDES_INT_MASK;
    s->isr &= TDES_INT_MASK | TDES_ISR_URAT_MASK;
    s->xtea_rounds &= 0x3f;
    s->wpmr &= TDES_WPMR_MASK;
    s->input_valid &= 0xff;
    s->key_written &= 0x3f;
    s->dma_input_pos = MIN(s->dma_input_pos, (uint8_t)8);
    s->dma_output_pos = MIN(s->dma_output_pos, (uint8_t)8);
    s->output_size = MIN(MAX(s->output_size, (uint8_t)1), (uint8_t)8);
    s->tx_request_level = false;
    s->rx_request_level = false;
    at91_tdes_update_irq(s);
    if (!clock_get_hz(s->pclk)) {
        timer_del(s->processing_timer);
    }
    at91_tdes_schedule(s);
    at91_tdes_update_requests(s);
    return 0;
}

static const VMStateDescription vmstate_at91_tdes = {
    .name = TYPE_AT91_TDES,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_tdes_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91TDESState),
        VMSTATE_UINT32(imr, AT91TDESState),
        VMSTATE_UINT32(isr, AT91TDESState),
        VMSTATE_UINT32_ARRAY(key, AT91TDESState, 6),
        VMSTATE_UINT32_ARRAY(iv, AT91TDESState, 2),
        VMSTATE_UINT32(xtea_rounds, AT91TDESState),
        VMSTATE_UINT32(wpmr, AT91TDESState),
        VMSTATE_UINT32(wpsr, AT91TDESState),
        VMSTATE_UINT8_ARRAY(input, AT91TDESState, 8),
        VMSTATE_UINT8_ARRAY(output, AT91TDESState, 8),
        VMSTATE_UINT8(input_valid, AT91TDESState),
        VMSTATE_UINT8(key_written, AT91TDESState),
        VMSTATE_UINT8(dma_input_pos, AT91TDESState),
        VMSTATE_UINT8(dma_output_pos, AT91TDESState),
        VMSTATE_UINT8(output_size, AT91TDESState),
        VMSTATE_BOOL(busy, AT91TDESState),
        VMSTATE_BOOL(locked, AT91TDESState),
        VMSTATE_BOOL(output_pending, AT91TDESState),
        VMSTATE_BOOL(private_key_write_once, AT91TDESState),
        VMSTATE_BOOL(reports_read, AT91TDESState),
        VMSTATE_TIMER_PTR(processing_timer, AT91TDESState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_tdes_properties[] = {
    DEFINE_PROP_UINT32("version", AT91TDESState, version, 0x700),
};

static void at91_tdes_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Triple Data Encryption Standard accelerator";
    dc->realize = at91_tdes_realize;
    dc->vmsd = &vmstate_at91_tdes;
    device_class_set_props(dc, at91_tdes_properties);
    device_class_set_legacy_reset(dc, at91_tdes_reset);
}

static const TypeInfo at91_tdes_info = {
    .name = TYPE_AT91_TDES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91TDESState),
    .instance_init = at91_tdes_init,
    .instance_finalize = at91_tdes_finalize,
    .class_init = at91_tdes_class_init,
};

static void at91_tdes_register_types(void)
{
    type_register_static(&at91_tdes_info);
}

type_init(at91_tdes_register_types)
