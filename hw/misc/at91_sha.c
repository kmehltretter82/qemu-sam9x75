/*
 * Microchip AT91 Secure Hash Algorithm accelerator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/at91_sha.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define SHA_CR                  0x00
#define SHA_MR                  0x04
#define SHA_IER                 0x10
#define SHA_IDR                 0x14
#define SHA_IMR                 0x18
#define SHA_ISR                 0x1c
#define SHA_MSR                 0x20
#define SHA_BCR                 0x30
#define SHA_IDATAR0             0x40
#define SHA_IDATAR15            0x7c
#define SHA_IODATAR0            0x80
#define SHA_IODATAR15           0xbc
#define SHA_WPMR                0xe4
#define SHA_WPSR                0xe8
#define SHA_VERSION             0xfc
#define SHA_MMIO_SIZE           0x100

#define SHA_CR_START            BIT(0)
#define SHA_CR_FIRST            BIT(4)
#define SHA_CR_SWRST            BIT(8)
#define SHA_CR_WUIHV            BIT(12)
#define SHA_CR_WUIEHV           BIT(13)
#define SHA_CR_UNLOCK           BIT(24)

#define SHA_MR_SMOD_MASK        3U
#define SHA_MR_SMOD_MANUAL      0
#define SHA_MR_SMOD_AUTO        1
#define SHA_MR_SMOD_IDATAR0     2
#define SHA_MR_AOE              BIT(3)
#define SHA_MR_PROCDLY          BIT(4)
#define SHA_MR_UIHV             BIT(5)
#define SHA_MR_UIEHV            BIT(6)
#define SHA_MR_BPE              BIT(7)
#define SHA_MR_ALGO_SHIFT       8
#define SHA_MR_ALGO_MASK        (0xfU << SHA_MR_ALGO_SHIFT)
#define SHA_MR_TMPLCK           BIT(15)
#define SHA_MR_DUALBUFF         BIT(16)
#define SHA_MR_CHECK_SHIFT      24
#define SHA_MR_CHECK_MASK       (3U << SHA_MR_CHECK_SHIFT)
#define SHA_MR_CHKCNT_SHIFT     28
#define SHA_MR_CHKCNT_MASK      (0xfU << SHA_MR_CHKCNT_SHIFT)
#define SHA_MR_MASK             (SHA_MR_SMOD_MASK | SHA_MR_AOE | \
                                 SHA_MR_PROCDLY | SHA_MR_UIHV | \
                                 SHA_MR_UIEHV | SHA_MR_BPE | \
                                 SHA_MR_ALGO_MASK | SHA_MR_TMPLCK | \
                                 SHA_MR_DUALBUFF | SHA_MR_CHECK_MASK | \
                                 SHA_MR_CHKCNT_MASK)
#define SHA_MR_RESET            0x00000100

#define SHA_ALGO_SHA1           0
#define SHA_ALGO_SHA256         1
#define SHA_ALGO_SHA384         2
#define SHA_ALGO_SHA512         3
#define SHA_ALGO_SHA224         4
#define SHA_ALGO_HMAC_SHA1      8
#define SHA_ALGO_HMAC_SHA256    9
#define SHA_ALGO_HMAC_SHA384    10
#define SHA_ALGO_HMAC_SHA512    11
#define SHA_ALGO_HMAC_SHA224    12

#define SHA_CHECK_NONE          0
#define SHA_CHECK_IR1           1
#define SHA_CHECK_MESSAGE       2

#define SHA_INT_DATRDY          BIT(0)
#define SHA_ISR_WRDY            BIT(4)
#define SHA_INT_URAD            BIT(8)
#define SHA_ISR_URAT_SHIFT      12
#define SHA_ISR_URAT_MASK       (7U << SHA_ISR_URAT_SHIFT)
#define SHA_INT_CHECKF          BIT(16)
#define SHA_ISR_CHKST_SHIFT     20
#define SHA_ISR_CHKST_MASK      (0xfU << SHA_ISR_CHKST_SHIFT)
#define SHA_ISR_CHKST_OK        (5U << SHA_ISR_CHKST_SHIFT)
#define SHA_INT_SECE            BIT(24)
#define SHA_INT_MASK            (SHA_INT_DATRDY | SHA_INT_URAD | \
                                 SHA_INT_CHECKF | SHA_INT_SECE)

#define SHA_URAT_INPUT_BUSY     0
#define SHA_URAT_OUTPUT_BUSY    1
#define SHA_URAT_MR_BUSY        2
#define SHA_URAT_READ_WO        5

#define SHA_WPMR_KEY_MASK       0xffffff00
#define SHA_WPMR_KEY            0x53484100
#define SHA_WPMR_WPEN           BIT(0)
#define SHA_WPMR_WPITEN         BIT(1)
#define SHA_WPMR_WPCREN         BIT(2)
#define SHA_WPMR_FIRSTE         BIT(4)
#define SHA_WPMR_ACTION_SHIFT   5
#define SHA_WPMR_ACTION_MASK    (3U << SHA_WPMR_ACTION_SHIFT)
#define SHA_WPMR_MASK           (SHA_WPMR_WPEN | SHA_WPMR_WPITEN | \
                                 SHA_WPMR_WPCREN | SHA_WPMR_FIRSTE | \
                                 SHA_WPMR_ACTION_MASK)

#define SHA_WPSR_WPVS           BIT(0)
#define SHA_WPSR_SWE            BIT(3)
#define SHA_WPSR_WPVSRC_SHIFT   8
#define SHA_WPSR_SWETYP_SHIFT   24
#define SHA_WPSR_ECLASS         BIT(31)

#define SHA_SWE_READ_WO         0
#define SHA_SWE_WRITE_RO        1
#define SHA_SWE_UNDEF_RW        2
#define SHA_SWE_CTRL_START      3
#define SHA_SWE_AUTO_START      4
#define SHA_SWE_BAD_START       5

enum {
    SHA_WRITE_DATA,
    SHA_WRITE_IR0,
    SHA_WRITE_IR1,
};

enum {
    SHA_STAGE_INPUT,
    SHA_STAGE_PADDING,
    SHA_STAGE_HMAC_OUTER,
};

static unsigned int at91_sha_algo(const AT91SHAState *s)
{
    return (s->mr & SHA_MR_ALGO_MASK) >> SHA_MR_ALGO_SHIFT;
}

static unsigned int at91_sha_base_algo(const AT91SHAState *s)
{
    unsigned int algo = at91_sha_algo(s);

    return algo >= SHA_ALGO_HMAC_SHA1 ? algo - SHA_ALGO_HMAC_SHA1 : algo;
}

static bool at91_sha_is_hmac(const AT91SHAState *s)
{
    unsigned int algo = at91_sha_algo(s);

    return algo >= SHA_ALGO_HMAC_SHA1 && algo <= SHA_ALGO_HMAC_SHA224;
}

static unsigned int at91_sha_smod(const AT91SHAState *s)
{
    return s->mr & SHA_MR_SMOD_MASK;
}

static unsigned int at91_sha_check_mode(const AT91SHAState *s)
{
    return (s->mr & SHA_MR_CHECK_MASK) >> SHA_MR_CHECK_SHIFT;
}

static size_t at91_sha_block_size(const AT91SHAState *s)
{
    unsigned int algo = at91_sha_base_algo(s);

    return algo == SHA_ALGO_SHA384 || algo == SHA_ALGO_SHA512 ? 128 : 64;
}

static unsigned int at91_sha_state_words(const AT91SHAState *s)
{
    switch (at91_sha_base_algo(s)) {
    case SHA_ALGO_SHA1:
        return 5;
    case SHA_ALGO_SHA384:
    case SHA_ALGO_SHA512:
        return 16;
    default:
        return 8;
    }
}

static unsigned int at91_sha_digest_words(const AT91SHAState *s)
{
    switch (at91_sha_base_algo(s)) {
    case SHA_ALGO_SHA1:
        return 5;
    case SHA_ALGO_SHA224:
        return 7;
    case SHA_ALGO_SHA384:
        return 12;
    case SHA_ALGO_SHA512:
        return 16;
    default:
        return 8;
    }
}

static void at91_sha_set_standard_iv(AT91SHAState *s)
{
    static const uint32_t sha1_iv[5] = {
        0x67452301U, 0xefcdab89U, 0x98badcfeU,
        0x10325476U, 0xc3d2e1f0U,
    };
    static const uint32_t sha224_iv[8] = {
        0xc1059ed8U, 0x367cd507U, 0x3070dd17U, 0xf70e5939U,
        0xffc00b31U, 0x68581511U, 0x64f98fa7U, 0xbefa4fa4U,
    };
    static const uint32_t sha256_iv[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    static const uint64_t sha384_iv[8] = {
        0xcbbb9d5dc1059ed8ULL, 0x629a292a367cd507ULL,
        0x9159015a3070dd17ULL, 0x152fecd8f70e5939ULL,
        0x67332667ffc00b31ULL, 0x8eb44a8768581511ULL,
        0xdb0c2e0d64f98fa7ULL, 0x47b5481dbefa4fa4ULL,
    };
    static const uint64_t sha512_iv[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL,
    };
    unsigned int i;

    memset(s->hash, 0, sizeof(s->hash));
    switch (at91_sha_base_algo(s)) {
    case SHA_ALGO_SHA1:
        for (i = 0; i < ARRAY_SIZE(sha1_iv); i++) {
            s->hash[i] = sha1_iv[i];
        }
        break;
    case SHA_ALGO_SHA224:
        for (i = 0; i < ARRAY_SIZE(sha224_iv); i++) {
            s->hash[i] = sha224_iv[i];
        }
        break;
    case SHA_ALGO_SHA256:
        for (i = 0; i < ARRAY_SIZE(sha256_iv); i++) {
            s->hash[i] = sha256_iv[i];
        }
        break;
    case SHA_ALGO_SHA384:
        memcpy(s->hash, sha384_iv, sizeof(sha384_iv));
        break;
    case SHA_ALGO_SHA512:
        memcpy(s->hash, sha512_iv, sizeof(sha512_iv));
        break;
    default:
        memcpy(s->hash, sha256_iv, sizeof(sha256_iv));
        break;
    }
}

static void at91_sha_load_ir(AT91SHAState *s, const uint32_t ir[16])
{
    unsigned int i;

    memset(s->hash, 0, sizeof(s->hash));
    if (at91_sha_block_size(s) == 128) {
        for (i = 0; i < 8; i++) {
            s->hash[i] = (uint64_t)bswap32(ir[i * 2]) << 32 |
                         bswap32(ir[i * 2 + 1]);
        }
    } else {
        for (i = 0; i < at91_sha_state_words(s); i++) {
            s->hash[i] = bswap32(ir[i]);
        }
    }
}

static void at91_sha_prepare_first(AT91SHAState *s)
{
    if (!s->first_pending) {
        return;
    }
    if (at91_sha_is_hmac(s) || (s->mr & SHA_MR_UIHV)) {
        at91_sha_load_ir(s, s->ir0);
    } else if (s->mr & SHA_MR_UIEHV) {
        at91_sha_load_ir(s, s->ir1);
    } else {
        at91_sha_set_standard_iv(s);
    }
    s->first_pending = false;
}

static void at91_sha1_transform(AT91SHAState *s)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    uint32_t orig[5];
    unsigned int i;

    for (i = 0; i < 16; i++) {
        w[i] = ldl_be_p(s->block + i * 4);
    }
    for (i = 16; i < 80; i++) {
        w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }
    for (i = 0; i < 5; i++) {
        orig[i] = s->hash[i];
    }
    a = orig[0];
    b = orig[1];
    c = orig[2];
    d = orig[3];
    e = orig[4];

    for (i = 0; i < 80; i++) {
        uint32_t f, k, temp;

        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999U;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1U;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcU;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6U;
        }
        temp = rol32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol32(b, 30);
        b = a;
        a = temp;
    }
    s->hash[0] = orig[0] + a;
    s->hash[1] = orig[1] + b;
    s->hash[2] = orig[2] + c;
    s->hash[3] = orig[3] + d;
    s->hash[4] = orig[4] + e;
}

static void at91_sha256_transform(AT91SHAState *s)
{
    static const uint32_t k[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t orig[8];
    unsigned int i;

    for (i = 0; i < 16; i++) {
        w[i] = ldl_be_p(s->block + i * 4);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i - 15], 7) ^ ror32(w[i - 15], 18) ^
                      (w[i - 15] >> 3);
        uint32_t s1 = ror32(w[i - 2], 17) ^ ror32(w[i - 2], 19) ^
                      (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    for (i = 0; i < 8; i++) {
        orig[i] = s->hash[i];
    }
    a = orig[0];
    b = orig[1];
    c = orig[2];
    d = orig[3];
    e = orig[4];
    f = orig[5];
    g = orig[6];
    h = orig[7];

    for (i = 0; i < 64; i++) {
        uint32_t s1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    s->hash[0] = orig[0] + a;
    s->hash[1] = orig[1] + b;
    s->hash[2] = orig[2] + c;
    s->hash[3] = orig[3] + d;
    s->hash[4] = orig[4] + e;
    s->hash[5] = orig[5] + f;
    s->hash[6] = orig[6] + g;
    s->hash[7] = orig[7] + h;
}

static void at91_sha512_transform(AT91SHAState *s)
{
    static const uint64_t k[80] = {
        0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL,
        0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
        0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
        0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
        0xd807aa98a3030242ULL, 0x12835b0145706fbeULL,
        0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
        0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL,
        0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
        0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
        0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
        0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL,
        0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
        0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL,
        0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
        0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
        0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
        0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL,
        0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
        0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL,
        0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
        0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
        0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
        0xd192e819d6ef5218ULL, 0xd69906245565a910ULL,
        0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
        0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL,
        0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
        0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
        0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
        0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL,
        0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
        0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL,
        0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
        0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
        0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
        0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL,
        0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
        0x28db77f523047d84ULL, 0x32caab7b40c72493ULL,
        0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
        0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
        0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
    };
    uint64_t w[80];
    uint64_t a, b, c, d, e, f, g, h;
    uint64_t orig[8];
    unsigned int i;

    for (i = 0; i < 16; i++) {
        w[i] = ldq_be_p(s->block + i * 8);
    }
    for (i = 16; i < 80; i++) {
        uint64_t s0 = ror64(w[i - 15], 1) ^ ror64(w[i - 15], 8) ^
                      (w[i - 15] >> 7);
        uint64_t s1 = ror64(w[i - 2], 19) ^ ror64(w[i - 2], 61) ^
                      (w[i - 2] >> 6);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(orig, s->hash, sizeof(orig));
    a = orig[0];
    b = orig[1];
    c = orig[2];
    d = orig[3];
    e = orig[4];
    f = orig[5];
    g = orig[6];
    h = orig[7];

    for (i = 0; i < 80; i++) {
        uint64_t s0 = ror64(a, 28) ^ ror64(a, 34) ^ ror64(a, 39);
        uint64_t s1 = ror64(e, 14) ^ ror64(e, 18) ^ ror64(e, 41);
        uint64_t ch = (e & f) ^ ((~e) & g);
        uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t temp1 = h + s1 + ch + k[i] + w[i];
        uint64_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    s->hash[0] = orig[0] + a;
    s->hash[1] = orig[1] + b;
    s->hash[2] = orig[2] + c;
    s->hash[3] = orig[3] + d;
    s->hash[4] = orig[4] + e;
    s->hash[5] = orig[5] + f;
    s->hash[6] = orig[6] + g;
    s->hash[7] = orig[7] + h;
}

static void at91_sha_transform(AT91SHAState *s)
{
    at91_sha_prepare_first(s);
    switch (at91_sha_base_algo(s)) {
    case SHA_ALGO_SHA1:
        at91_sha1_transform(s);
        break;
    case SHA_ALGO_SHA384:
    case SHA_ALGO_SHA512:
        at91_sha512_transform(s);
        break;
    default:
        at91_sha256_transform(s);
        break;
    }
}

static void at91_sha_update_output(AT91SHAState *s)
{
    unsigned int i;

    memset(s->output, 0, sizeof(s->output));
    if (at91_sha_block_size(s) == 128) {
        for (i = 0; i < 8; i++) {
            s->output[i * 2] = bswap32(s->hash[i] >> 32);
            s->output[i * 2 + 1] = bswap32(s->hash[i]);
        }
    } else {
        for (i = 0; i < at91_sha_state_words(s); i++) {
            s->output[i] = bswap32(s->hash[i]);
        }
    }
}

static void at91_sha_set_irq(AT91SHAState *s, uint32_t bits)
{
    s->isr |= bits;
    qemu_set_irq(s->irq, !!(s->isr & s->imr & SHA_INT_MASK));
}

static void at91_sha_clear_irq(AT91SHAState *s, uint32_t bits)
{
    s->isr &= ~bits;
    qemu_set_irq(s->irq, !!(s->isr & s->imr & SHA_INT_MASK));
}

static void at91_sha_set_request(AT91SHAState *s, bool level)
{
    if (s->tx_request_level != level) {
        s->tx_request_level = level;
        qemu_set_irq(s->tx_request, level);
    }
}

static bool at91_sha_can_accept(const AT91SHAState *s)
{
    bool more_message = !s->msr || s->bcr;

    return clock_get_hz(s->pclk) && !s->busy && !s->locked &&
           at91_sha_smod(s) == SHA_MR_SMOD_IDATAR0 &&
           (more_message || s->awaiting_check);
}

static void at91_sha_update_request(AT91SHAState *s)
{
    at91_sha_set_request(s, at91_sha_can_accept(s));
}

static void at91_sha_raise_urad(AT91SHAState *s, unsigned int type)
{
    s->isr &= ~SHA_ISR_URAT_MASK;
    s->isr |= type << SHA_ISR_URAT_SHIFT;
    at91_sha_set_irq(s, SHA_INT_URAD);
}

static void at91_sha_maybe_lock(AT91SHAState *s, bool software_event)
{
    unsigned int action = (s->wpmr & SHA_WPMR_ACTION_MASK) >>
                          SHA_WPMR_ACTION_SHIFT;

    if ((software_event && (action == 1 || action == 3)) || action == 3) {
        s->locked = true;
        at91_sha_update_request(s);
    }
}

static void at91_sha_raise_swe(AT91SHAState *s, hwaddr offset,
                               unsigned int type, bool error)
{
    bool first_only = s->wpmr & SHA_WPMR_FIRSTE;

    if (!first_only || !(s->wpsr & SHA_WPSR_SWE)) {
        s->wpsr &= ~(SHA_WPSR_ECLASS |
                      (0xfU << SHA_WPSR_SWETYP_SHIFT));
        s->wpsr |= SHA_WPSR_SWE | (type << SHA_WPSR_SWETYP_SHIFT);
        if (!(s->wpsr & SHA_WPSR_WPVS)) {
            s->wpsr &= ~(0xffU << SHA_WPSR_WPVSRC_SHIFT);
            s->wpsr |= (offset & 0xff) << SHA_WPSR_WPVSRC_SHIFT;
        }
        if (error) {
            s->wpsr |= SHA_WPSR_ECLASS;
        }
    }
    at91_sha_set_irq(s, SHA_INT_SECE);
    at91_sha_maybe_lock(s, true);
}

static bool at91_sha_write_protected(AT91SHAState *s, hwaddr offset,
                                      uint32_t enable)
{
    if (!(s->wpmr & enable)) {
        return false;
    }
    if (!(s->wpmr & SHA_WPMR_FIRSTE) || !(s->wpsr & SHA_WPSR_WPVS)) {
        s->wpsr &= ~(0xffU << SHA_WPSR_WPVSRC_SHIFT);
        s->wpsr |= SHA_WPSR_WPVS |
                   ((offset & 0xff) << SHA_WPSR_WPVSRC_SHIFT);
    }
    at91_sha_set_irq(s, SHA_INT_SECE);
    at91_sha_maybe_lock(s, true);
    return true;
}

static uint64_t at91_sha_processing_cycles(const AT91SHAState *s)
{
    bool slow = s->mr & SHA_MR_PROCDLY;

    switch (at91_sha_base_algo(s)) {
    case SHA_ALGO_SHA1:
        return (slow ? 209 : 85) + 2;
    case SHA_ALGO_SHA384:
    case SHA_ALGO_SHA512:
        return (slow ? 209 : 88) + 2;
    default:
        return (slow ? 194 : 72) + 2;
    }
}

static void at91_sha_schedule(AT91SHAState *s)
{
    uint64_t duration;

    if (!s->busy || timer_pending(s->processing_timer) ||
        !clock_get_hz(s->pclk)) {
        return;
    }
    duration = MAX(clock_ticks_to_ns(s->pclk,
                                     at91_sha_processing_cycles(s)), 1);
    timer_mod_ns(s->processing_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration);
}

static void at91_sha_reset_input(AT91SHAState *s)
{
    size_t block_size = at91_sha_block_size(s);

    memset(s->block, 0, sizeof(s->block));
    s->input_words = 0;
    s->input_bytes = 0;
    s->expected_bytes = s->msr ? MIN((uint64_t)s->bcr, block_size) :
                                 block_size;
}

static void at91_sha_write_bit_length(AT91SHAState *s, uint64_t bytes)
{
    size_t block_size = at91_sha_block_size(s);
    uint64_t bits = bytes << 3;

    if (block_size == 128) {
        stq_be_p(s->block + block_size - 16, 0);
    }
    stq_be_p(s->block + block_size - 8, bits);
}

static bool at91_sha_process_final_input(AT91SHAState *s)
{
    size_t block_size = at91_sha_block_size(s);
    size_t length_size = block_size == 128 ? 16 : 8;
    uint64_t total = s->msr;

    if (at91_sha_is_hmac(s)) {
        total += block_size;
    }

    if (s->input_bytes == block_size) {
        at91_sha_transform(s);
        memset(s->block, 0, sizeof(s->block));
        s->block[0] = 0x80;
        at91_sha_write_bit_length(s, total);
        return false;
    }

    memset(s->block + s->input_bytes, 0, block_size - s->input_bytes);
    s->block[s->input_bytes] = 0x80;
    if (s->input_bytes + 1 + length_size <= block_size) {
        at91_sha_write_bit_length(s, total);
        at91_sha_transform(s);
        return true;
    }

    at91_sha_transform(s);
    memset(s->block, 0, sizeof(s->block));
    at91_sha_write_bit_length(s, total);
    return false;
}

static unsigned int at91_sha_num_check_words(const AT91SHAState *s)
{
    unsigned int count = (s->mr & SHA_MR_CHKCNT_MASK) >>
                         SHA_MR_CHKCNT_SHIFT;

    if (!count) {
        count = at91_sha_digest_words(s);
    }
    return MIN(count, at91_sha_state_words(s));
}

static void at91_sha_complete_check(AT91SHAState *s,
                                    const uint32_t expected[16])
{
    unsigned int count = at91_sha_num_check_words(s);

    s->isr &= ~SHA_ISR_CHKST_MASK;
    if (!memcmp(s->output, expected, count * sizeof(expected[0]))) {
        s->isr |= SHA_ISR_CHKST_OK;
    }
    at91_sha_set_irq(s, SHA_INT_CHECKF);
}

static void at91_sha_finish_result(AT91SHAState *s)
{
    at91_sha_update_output(s);
    s->busy = false;
    s->output_valid = true;
    s->processing_stage = SHA_STAGE_INPUT;
    at91_sha_reset_input(s);
    at91_sha_set_irq(s, SHA_INT_DATRDY);

    if (at91_sha_check_mode(s) == SHA_CHECK_IR1) {
        at91_sha_complete_check(s, s->ir1);
    } else if (at91_sha_check_mode(s) == SHA_CHECK_MESSAGE) {
        s->awaiting_check = true;
        s->check_words = 0;
        memset(s->check, 0, sizeof(s->check));
    }
    at91_sha_update_request(s);
}

static void at91_sha_finish_block(AT91SHAState *s)
{
    if (s->bcr) {
        bool report = at91_sha_smod(s) != SHA_MR_SMOD_IDATAR0 ||
                      (s->mr & SHA_MR_BPE);

        if (report) {
            at91_sha_update_output(s);
            s->output_valid = true;
            at91_sha_set_irq(s, SHA_INT_DATRDY);
        }
        s->busy = false;
        s->processing_stage = SHA_STAGE_INPUT;
        at91_sha_reset_input(s);
        at91_sha_update_request(s);
        return;
    }

    at91_sha_finish_result(s);
}

static void at91_sha_prepare_hmac_outer(AT91SHAState *s)
{
    size_t block_size = at91_sha_block_size(s);
    unsigned int words = at91_sha_digest_words(s);
    unsigned int i;

    at91_sha_update_output(s);
    memset(s->block, 0, sizeof(s->block));
    for (i = 0; i < words; i++) {
        stl_le_p(s->block + i * 4, s->output[i]);
    }
    s->block[words * 4] = 0x80;
    at91_sha_write_bit_length(s, block_size + words * 4);
    s->processing_stage = SHA_STAGE_HMAC_OUTER;
    s->input_bytes = block_size;
    at91_sha_schedule(s);
}

static void at91_sha_finish_inner(AT91SHAState *s)
{
    if (at91_sha_is_hmac(s)) {
        at91_sha_prepare_hmac_outer(s);
    } else {
        at91_sha_finish_result(s);
    }
}

static void at91_sha_processing_done(void *opaque)
{
    AT91SHAState *s = opaque;
    bool auto_final;

    if (!s->busy || !clock_get_hz(s->pclk)) {
        return;
    }

    switch (s->processing_stage) {
    case SHA_STAGE_PADDING:
        at91_sha_transform(s);
        at91_sha_finish_inner(s);
        return;
    case SHA_STAGE_HMAC_OUTER:
        at91_sha_load_ir(s, s->ir1);
        at91_sha_transform(s);
        at91_sha_finish_result(s);
        return;
    default:
        break;
    }

    auto_final = s->msr && !s->bcr;
    if (auto_final) {
        if (!at91_sha_process_final_input(s)) {
            s->processing_stage = SHA_STAGE_PADDING;
            at91_sha_schedule(s);
            return;
        }
        at91_sha_finish_inner(s);
        return;
    }

    at91_sha_transform(s);
    if (at91_sha_is_hmac(s) && !s->msr) {
        at91_sha_finish_inner(s);
    } else {
        at91_sha_finish_block(s);
    }
}

static void at91_sha_start_block(AT91SHAState *s, bool automatic)
{
    if (s->locked) {
        at91_sha_raise_swe(s, SHA_CR,
                           automatic ? SHA_SWE_AUTO_START :
                                       SHA_SWE_CTRL_START,
                           false);
        return;
    }
    if (s->busy) {
        at91_sha_raise_urad(s, SHA_URAT_INPUT_BUSY);
        return;
    }
    s->isr &= ~(SHA_INT_DATRDY | SHA_INT_CHECKF | SHA_ISR_CHKST_MASK);
    s->output_valid = false;
    s->awaiting_check = false;
    s->busy = true;
    s->processing_stage = SHA_STAGE_INPUT;
    qemu_set_irq(s->irq, !!(s->isr & s->imr & SHA_INT_MASK));
    at91_sha_update_request(s);
    at91_sha_schedule(s);
}

static void at91_sha_write_check_word(AT91SHAState *s, uint32_t value)
{
    unsigned int count = at91_sha_num_check_words(s);

    if (s->check_words >= count) {
        return;
    }
    s->check[s->check_words++] = value;
    if (s->check_words == count) {
        s->awaiting_check = false;
        at91_sha_complete_check(s, s->check);
        at91_sha_update_request(s);
    }
}

static void at91_sha_write_data(AT91SHAState *s, hwaddr offset,
                                uint32_t value)
{
    size_t block_size = at91_sha_block_size(s);
    unsigned int position;
    unsigned int valid;

    if (s->write_target != SHA_WRITE_DATA) {
        uint32_t *ir = s->write_target == SHA_WRITE_IR0 ? s->ir0 : s->ir1;

        if (offset < SHA_IDATAR0 || offset > SHA_IDATAR15) {
            at91_sha_raise_swe(s, offset, SHA_SWE_UNDEF_RW, false);
            return;
        }
        ir[(offset - SHA_IDATAR0) / 4] = value;
        return;
    }

    if (s->awaiting_check) {
        at91_sha_write_check_word(s, value);
        return;
    }
    if (s->busy) {
        at91_sha_raise_urad(s, SHA_URAT_INPUT_BUSY);
        return;
    }

    if (at91_sha_smod(s) == SHA_MR_SMOD_IDATAR0) {
        if (offset != SHA_IDATAR0) {
            at91_sha_raise_swe(s, offset, SHA_SWE_UNDEF_RW, false);
            return;
        }
        position = s->input_words;
    } else if (offset <= SHA_IDATAR15) {
        position = (offset - SHA_IDATAR0) / 4;
    } else {
        position = 16 + (offset - SHA_IODATAR0) / 4;
    }

    if (position >= block_size / 4 || position != s->input_words) {
        at91_sha_raise_swe(s, offset, SHA_SWE_UNDEF_RW, false);
        return;
    }
    stl_le_p(s->block + position * 4, value);
    s->input_words++;
    if (s->msr) {
        valid = MIN(s->bcr, 4U);
        s->bcr -= valid;
    } else {
        valid = 4;
    }
    s->input_bytes += valid;

    if (at91_sha_smod(s) != SHA_MR_SMOD_MANUAL &&
        s->expected_bytes && s->input_bytes >= s->expected_bytes) {
        at91_sha_start_block(s, true);
    }
}

static uint64_t at91_sha_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91SHAState *s = AT91_SHA(opaque);
    uint32_t value;
    unsigned int index;

    if (offset >= SHA_IODATAR0 && offset <= SHA_IODATAR15) {
        index = (offset - SHA_IODATAR0) / 4;
        if (s->busy) {
            at91_sha_raise_urad(s, SHA_URAT_OUTPUT_BUSY);
            return 0;
        }
        if (!s->output_valid) {
            at91_sha_raise_swe(s, offset, SHA_SWE_UNDEF_RW, true);
            return 0;
        }
        value = s->output[index];
        at91_sha_clear_irq(s, SHA_INT_DATRDY | SHA_INT_CHECKF |
                              SHA_ISR_CHKST_MASK);
        return value;
    }

    switch (offset) {
    case SHA_MR:
        return s->mr;
    case SHA_IMR:
        return s->imr;
    case SHA_ISR:
        value = s->isr;
        if (clock_get_hz(s->pclk) && !s->busy && !s->locked) {
            value |= SHA_ISR_WRDY;
        }
        at91_sha_clear_irq(s, SHA_INT_SECE);
        return value;
    case SHA_MSR:
        return s->msr;
    case SHA_BCR:
        return s->bcr;
    case SHA_WPMR:
        return s->wpmr;
    case SHA_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case SHA_VERSION:
        return s->version;
    case SHA_CR:
    case SHA_IER:
    case SHA_IDR:
        at91_sha_raise_urad(s, SHA_URAT_READ_WO);
        at91_sha_raise_swe(s, offset, SHA_SWE_READ_WO, false);
        return 0;
    default:
        if (offset >= SHA_IDATAR0 && offset <= SHA_IDATAR15) {
            at91_sha_raise_urad(s, SHA_URAT_READ_WO);
            at91_sha_raise_swe(s, offset, SHA_SWE_READ_WO, false);
            return 0;
        }
        at91_sha_raise_swe(s, offset, SHA_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SHA ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_sha_reset_registers(AT91SHAState *s, bool hardware)
{
    timer_del(s->processing_timer);
    s->mr = SHA_MR_RESET;
    s->imr = 0;
    s->isr = 0;
    s->msr = 0;
    s->bcr = 0;
    memset(s->block, 0, sizeof(s->block));
    memset(s->output, 0, sizeof(s->output));
    memset(s->check, 0, sizeof(s->check));
    memset(s->hash, 0, sizeof(s->hash));
    s->input_words = 0;
    s->input_bytes = 0;
    s->expected_bytes = at91_sha_block_size(s);
    s->check_words = 0;
    s->write_target = SHA_WRITE_DATA;
    s->processing_stage = SHA_STAGE_INPUT;
    s->first_pending = false;
    s->busy = false;
    s->output_valid = false;
    s->awaiting_check = false;
    if (hardware) {
        s->wpmr = 0;
        s->wpsr = 0;
        memset(s->ir0, 0, sizeof(s->ir0));
        memset(s->ir1, 0, sizeof(s->ir1));
        s->locked = false;
    }
    qemu_set_irq(s->irq, 0);
    at91_sha_update_request(s);
}

static void at91_sha_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91SHAState *s = AT91_SHA(opaque);
    uint32_t val = value;

    if ((offset == SHA_MR || offset == SHA_MSR || offset == SHA_BCR) &&
        at91_sha_write_protected(s, offset, SHA_WPMR_WPEN)) {
        return;
    }
    if ((offset == SHA_IER || offset == SHA_IDR) &&
        at91_sha_write_protected(s, offset, SHA_WPMR_WPITEN)) {
        return;
    }
    if (offset == SHA_CR &&
        at91_sha_write_protected(s, offset, SHA_WPMR_WPCREN)) {
        return;
    }

    if ((offset >= SHA_IDATAR0 && offset <= SHA_IDATAR15) ||
        (offset >= SHA_IODATAR0 && offset <= SHA_IODATAR15)) {
        at91_sha_write_data(s, offset, val);
        return;
    }

    switch (offset) {
    case SHA_CR:
        if (val & SHA_CR_SWRST) {
            at91_sha_reset_registers(s, false);
            return;
        }
        if (val & SHA_CR_WUIHV) {
            s->write_target = SHA_WRITE_IR0;
        } else if (val & SHA_CR_WUIEHV) {
            s->write_target = SHA_WRITE_IR1;
        } else {
            s->write_target = SHA_WRITE_DATA;
        }
        if (val & SHA_CR_FIRST) {
            s->first_pending = true;
        }
        if (val & SHA_CR_START) {
            if (at91_sha_smod(s) == SHA_MR_SMOD_MANUAL) {
                at91_sha_start_block(s, false);
            } else {
                at91_sha_raise_swe(s, offset, SHA_SWE_BAD_START, false);
            }
        }
        if ((val & SHA_CR_UNLOCK) && !s->wpsr) {
            s->locked = false;
            at91_sha_update_request(s);
        }
        break;
    case SHA_MR:
        if (s->busy) {
            at91_sha_raise_urad(s, SHA_URAT_MR_BUSY);
            break;
        }
        s->mr = val & SHA_MR_MASK;
        at91_sha_reset_input(s);
        at91_sha_update_request(s);
        break;
    case SHA_IER:
        s->imr |= val & SHA_INT_MASK;
        qemu_set_irq(s->irq, !!(s->isr & s->imr & SHA_INT_MASK));
        break;
    case SHA_IDR:
        s->imr &= ~(val & SHA_INT_MASK);
        qemu_set_irq(s->irq, !!(s->isr & s->imr & SHA_INT_MASK));
        break;
    case SHA_MSR:
        s->msr = val;
        break;
    case SHA_BCR:
        s->bcr = val;
        at91_sha_reset_input(s);
        at91_sha_update_request(s);
        break;
    case SHA_WPMR:
        if ((val & SHA_WPMR_KEY_MASK) == SHA_WPMR_KEY) {
            s->wpmr = val & SHA_WPMR_MASK;
        }
        break;
    case SHA_IMR:
    case SHA_ISR:
    case SHA_WPSR:
    case SHA_VERSION:
        at91_sha_raise_swe(s, offset, SHA_SWE_WRITE_RO, false);
        break;
    default:
        at91_sha_raise_swe(s, offset, SHA_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_SHA ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_sha_ops = {
    .read = at91_sha_read,
    .write = at91_sha_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_sha_clock_changed(void *opaque, ClockEvent event)
{
    AT91SHAState *s = opaque;

    timer_del(s->processing_timer);
    at91_sha_schedule(s);
    at91_sha_update_request(s);
}

static void at91_sha_reset(DeviceState *dev)
{
    at91_sha_reset_registers(AT91_SHA(dev), true);
}

static void at91_sha_init(Object *obj)
{
    AT91SHAState *s = AT91_SHA(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &at91_sha_ops, s,
                          TYPE_AT91_SHA, SHA_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_sha_clock_changed,
                                 s, ClockUpdate);
    s->processing_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                       at91_sha_processing_done, s);
}

static void at91_sha_finalize(Object *obj)
{
    AT91SHAState *s = AT91_SHA(obj);

    timer_free(s->processing_timer);
}

static void at91_sha_realize(DeviceState *dev, Error **errp)
{
    AT91SHAState *s = AT91_SHA(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_SHA ": pclk must be connected");
    }
}

static int at91_sha_post_load(void *opaque, int version_id)
{
    AT91SHAState *s = opaque;

    s->mr &= SHA_MR_MASK;
    s->imr &= SHA_INT_MASK;
    s->isr &= SHA_INT_MASK | SHA_ISR_URAT_MASK | SHA_ISR_CHKST_MASK;
    s->wpmr &= SHA_WPMR_MASK;
    s->write_target = MIN(s->write_target, (uint8_t)SHA_WRITE_IR1);
    s->processing_stage = MIN(s->processing_stage,
                              (uint8_t)SHA_STAGE_HMAC_OUTER);
    s->tx_request_level = false;
    qemu_set_irq(s->irq, !!(s->isr & s->imr & SHA_INT_MASK));
    at91_sha_update_request(s);
    at91_sha_schedule(s);
    return 0;
}

static const VMStateDescription vmstate_at91_sha = {
    .name = TYPE_AT91_SHA,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_sha_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91SHAState),
        VMSTATE_UINT32(imr, AT91SHAState),
        VMSTATE_UINT32(isr, AT91SHAState),
        VMSTATE_UINT32(msr, AT91SHAState),
        VMSTATE_UINT32(bcr, AT91SHAState),
        VMSTATE_UINT32(wpmr, AT91SHAState),
        VMSTATE_UINT32(wpsr, AT91SHAState),
        VMSTATE_UINT8_ARRAY(block, AT91SHAState, AT91_SHA_MAX_BLOCK_SIZE),
        VMSTATE_UINT32_ARRAY(output, AT91SHAState, AT91_SHA_MAX_WORDS),
        VMSTATE_UINT32_ARRAY(ir0, AT91SHAState, AT91_SHA_MAX_WORDS),
        VMSTATE_UINT32_ARRAY(ir1, AT91SHAState, AT91_SHA_MAX_WORDS),
        VMSTATE_UINT32_ARRAY(check, AT91SHAState, AT91_SHA_MAX_WORDS),
        VMSTATE_UINT64_ARRAY(hash, AT91SHAState, 8),
        VMSTATE_UINT32(input_words, AT91SHAState),
        VMSTATE_UINT32(input_bytes, AT91SHAState),
        VMSTATE_UINT32(expected_bytes, AT91SHAState),
        VMSTATE_UINT32(check_words, AT91SHAState),
        VMSTATE_UINT8(write_target, AT91SHAState),
        VMSTATE_UINT8(processing_stage, AT91SHAState),
        VMSTATE_BOOL(first_pending, AT91SHAState),
        VMSTATE_BOOL(busy, AT91SHAState),
        VMSTATE_BOOL(locked, AT91SHAState),
        VMSTATE_BOOL(output_valid, AT91SHAState),
        VMSTATE_BOOL(awaiting_check, AT91SHAState),
        VMSTATE_TIMER_PTR(processing_timer, AT91SHAState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_sha_properties[] = {
    DEFINE_PROP_UINT32("version", AT91SHAState, version, 0x700),
};

static void at91_sha_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Secure Hash Algorithm accelerator";
    dc->realize = at91_sha_realize;
    dc->vmsd = &vmstate_at91_sha;
    device_class_set_props(dc, at91_sha_properties);
    device_class_set_legacy_reset(dc, at91_sha_reset);
}

static const TypeInfo at91_sha_info = {
    .name = TYPE_AT91_SHA,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91SHAState),
    .instance_init = at91_sha_init,
    .instance_finalize = at91_sha_finalize,
    .class_init = at91_sha_class_init,
};

static void at91_sha_register_types(void)
{
    type_register_static(&at91_sha_info);
}

type_init(at91_sha_register_types)
