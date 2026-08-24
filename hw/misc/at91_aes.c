/*
 * Microchip AT91 Advanced Encryption Standard accelerator
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "crypto/aes.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/at91_aes.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define AES_CR                  0x00
#define AES_MR                  0x04
#define AES_IER                 0x10
#define AES_IDR                 0x14
#define AES_IMR                 0x18
#define AES_ISR                 0x1c
#define AES_KEYWR0              0x20
#define AES_KEYWR7              0x3c
#define AES_IDATAR0             0x40
#define AES_IDATAR3             0x4c
#define AES_ODATAR0             0x50
#define AES_ODATAR3             0x5c
#define AES_IVR0                0x60
#define AES_IVR3                0x6c
#define AES_AADLENR             0x70
#define AES_CLENR               0x74
#define AES_GHASHR0             0x78
#define AES_GHASHR3             0x84
#define AES_TAGR0               0x88
#define AES_TAGR3               0x94
#define AES_CTRR                0x98
#define AES_GCMHR0              0x9c
#define AES_GCMHR3              0xa8
#define AES_EMR                 0xb0
#define AES_BCNT                0xb4
#define AES_TWR0                0xc0
#define AES_TWR3                0xcc
#define AES_ALPHAR0             0xd0
#define AES_ALPHAR3             0xdc
#define AES_WPMR                0xe4
#define AES_WPSR                0xe8
#define AES_VERSION             0xfc
#define AES_MMIO_SIZE           0x100

#define AES_CR_START            BIT(0)
#define AES_CR_SWRST            BIT(8)
#define AES_CR_UNLOCK           BIT(24)

#define AES_MR_CIPHER           BIT(0)
#define AES_MR_GTAGEN           BIT(1)
#define AES_MR_DUALBUFF         BIT(3)
#define AES_MR_PROCDLY_MASK     (0xfU << 4)
#define AES_MR_SMOD_SHIFT       8
#define AES_MR_SMOD_MASK        (3U << AES_MR_SMOD_SHIFT)
#define AES_MR_KEYSIZE_SHIFT    10
#define AES_MR_KEYSIZE_MASK     (3U << AES_MR_KEYSIZE_SHIFT)
#define AES_MR_OPMOD_SHIFT      12
#define AES_MR_OPMOD_MASK       (7U << AES_MR_OPMOD_SHIFT)
#define AES_MR_LOD              BIT(15)
#define AES_MR_CFBS_SHIFT       16
#define AES_MR_CFBS_MASK        (7U << AES_MR_CFBS_SHIFT)
#define AES_MR_CKEY_SHIFT       20
#define AES_MR_CKEY_MASK        (0xfU << AES_MR_CKEY_SHIFT)
#define AES_MR_TAMPCLR          BIT(31)
#define AES_MR_WRITABLE_MASK    (AES_MR_CIPHER | AES_MR_GTAGEN | \
                                 AES_MR_DUALBUFF | \
                                 AES_MR_PROCDLY_MASK | \
                                 AES_MR_SMOD_MASK | \
                                 AES_MR_KEYSIZE_MASK | \
                                 AES_MR_OPMOD_MASK | AES_MR_LOD | \
                                 AES_MR_CFBS_MASK | AES_MR_TAMPCLR)
#define AES_MR_RESET            0x00080000

#define AES_SMOD_MANUAL         0
#define AES_SMOD_AUTO           1
#define AES_SMOD_DMA            2

#define AES_OPMODE_ECB          0
#define AES_OPMODE_CBC          1
#define AES_OPMODE_OFB          2
#define AES_OPMODE_CFB          3
#define AES_OPMODE_CTR          4
#define AES_OPMODE_GCM          5
#define AES_OPMODE_XTS          6

#define AES_INT_DATRDY          BIT(0)
#define AES_INT_URAD            BIT(8)
#define AES_INT_TAGRDY          BIT(16)
#define AES_INT_EOPAD           BIT(17)
#define AES_INT_PLENERR         BIT(18)
#define AES_INT_SECE            BIT(19)
#define AES_INT_MASK            (AES_INT_DATRDY | AES_INT_URAD | \
                                 AES_INT_TAGRDY | AES_INT_EOPAD | \
                                 AES_INT_PLENERR | AES_INT_SECE)
#define AES_ISR_URAT_SHIFT      12
#define AES_ISR_URAT_MASK       (0xfU << AES_ISR_URAT_SHIFT)
#define AES_URAT_WOR_RD         5

#define AES_EMR_BPE             BIT(31)
#define AES_EMR_NHEAD_MASK      (0xffU << 16)
#define AES_EMR_PADLEN_MASK     (0xffU << 8)
#define AES_EMR_PKRS            BIT(7)
#define AES_EMR_PLIPD           BIT(5)
#define AES_EMR_PLIPEN          BIT(4)
#define AES_EMR_APM             BIT(1)
#define AES_EMR_APEN            BIT(0)
#define AES_EMR_MASK            (AES_EMR_BPE | AES_EMR_NHEAD_MASK | \
                                 AES_EMR_PADLEN_MASK | AES_EMR_PKRS | \
                                 AES_EMR_PLIPD | AES_EMR_PLIPEN | \
                                 AES_EMR_APM | AES_EMR_APEN)

#define AES_WPMR_KEY_MASK       0xffffff00
#define AES_WPMR_KEY            0x41455300
#define AES_WPMR_WPEN           BIT(0)
#define AES_WPMR_WPITEN         BIT(1)
#define AES_WPMR_WPCREN         BIT(2)
#define AES_WPMR_FIRSTE         BIT(4)
#define AES_WPMR_ACTION_MASK    (7U << 5)
#define AES_WPMR_MASK           (AES_WPMR_WPEN | AES_WPMR_WPITEN | \
                                 AES_WPMR_WPCREN | AES_WPMR_FIRSTE | \
                                 AES_WPMR_ACTION_MASK)

#define AES_WPSR_WPVS           BIT(0)
#define AES_WPSR_SWE            BIT(3)
#define AES_WPSR_WPVSRC_SHIFT   8
#define AES_WPSR_SWETYP_SHIFT   24
#define AES_WPSR_ECLASS         BIT(31)

#define AES_SWE_READ_WO         0
#define AES_SWE_WRITE_RO        1
#define AES_SWE_UNDEF_RW        2
#define AES_SWE_CTRL_START      3
#define AES_SWE_INCOMPLETE_KEY  5

static unsigned int at91_aes_smod(const AT91AESState *s)
{
    return (s->mr & AES_MR_SMOD_MASK) >> AES_MR_SMOD_SHIFT;
}

static unsigned int at91_aes_opmode(const AT91AESState *s)
{
    return (s->mr & AES_MR_OPMOD_MASK) >> AES_MR_OPMOD_SHIFT;
}

static void at91_aes_update_irq(AT91AESState *s)
{
    qemu_set_irq(s->irq, !!(s->isr & s->imr & AES_INT_MASK));
}

static void at91_aes_set_request(qemu_irq request, bool *old_level,
                                 bool new_level)
{
    if (*old_level != new_level) {
        *old_level = new_level;
        qemu_set_irq(request, new_level);
    }
}

static void at91_aes_update_requests(AT91AESState *s)
{
    bool dma = at91_aes_smod(s) == AES_SMOD_DMA;
    bool clocked = clock_get_hz(s->pclk) != 0;
    bool tx = dma && clocked && s->key_complete &&
              !s->input_ready && !s->output_pending;
    bool rx = dma && clocked && s->output_pending &&
              !(s->mr & AES_MR_LOD);

    at91_aes_set_request(s->tx_request, &s->tx_request_level, tx);
    at91_aes_set_request(s->rx_request, &s->rx_request_level, rx);
}

static void at91_aes_set_isr(AT91AESState *s, uint32_t bits)
{
    s->isr |= bits;
    at91_aes_update_irq(s);
}

static void at91_aes_clear_isr(AT91AESState *s, uint32_t bits)
{
    s->isr &= ~bits;
    at91_aes_update_irq(s);
}

static void at91_aes_raise_urad(AT91AESState *s, unsigned int type)
{
    s->isr &= ~AES_ISR_URAT_MASK;
    s->isr |= AES_INT_URAD | (type << AES_ISR_URAT_SHIFT);
    at91_aes_update_irq(s);
}

static void at91_aes_raise_swe(AT91AESState *s, hwaddr offset,
                               unsigned int type, bool error)
{
    bool first_only = s->wpmr & AES_WPMR_FIRSTE;

    if (!first_only || !(s->wpsr & AES_WPSR_SWE)) {
        s->wpsr &= ~(AES_WPSR_ECLASS | (0xfU << AES_WPSR_SWETYP_SHIFT) |
                      (0xffU << AES_WPSR_WPVSRC_SHIFT));
        s->wpsr |= AES_WPSR_SWE | ((offset & 0xff) <<
                                   AES_WPSR_WPVSRC_SHIFT) |
                   (type << AES_WPSR_SWETYP_SHIFT);
        if (error) {
            s->wpsr |= AES_WPSR_ECLASS;
        }
    }
    at91_aes_set_isr(s, AES_INT_SECE);
}

static bool at91_aes_write_protected(AT91AESState *s, hwaddr offset,
                                     uint32_t enable)
{
    if (!(s->wpmr & enable)) {
        return false;
    }

    if (!(s->wpmr & AES_WPMR_FIRSTE) || !(s->wpsr & AES_WPSR_WPVS)) {
        s->wpsr &= ~(0xffU << AES_WPSR_WPVSRC_SHIFT);
        s->wpsr |= AES_WPSR_WPVS |
                   ((offset & 0xff) << AES_WPSR_WPVSRC_SHIFT);
    }
    at91_aes_set_isr(s, AES_INT_SECE);
    return true;
}

static void at91_aes_words_to_bytes(const uint32_t words[4], uint8_t bytes[16])
{
    unsigned int i;

    for (i = 0; i < 4; i++) {
        stl_le_p(bytes + i * sizeof(words[i]), words[i]);
    }
}

static void at91_aes_bytes_to_words(const uint8_t bytes[16], uint32_t words[4])
{
    unsigned int i;

    for (i = 0; i < 4; i++) {
        words[i] = ldl_le_p(bytes + i * sizeof(words[i]));
    }
}

static uint64_t at91_aes_load_le(const uint8_t *data, unsigned int size)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static void at91_aes_store_le(uint8_t *data, uint64_t value,
                              unsigned int size)
{
    unsigned int i;

    for (i = 0; i < size; i++) {
        data[i] = value >> (i * 8);
    }
}

static unsigned int at91_aes_key_words(const AT91AESState *s)
{
    switch ((s->mr & AES_MR_KEYSIZE_MASK) >> AES_MR_KEYSIZE_SHIFT) {
    case 0:
        return 4;
    case 1:
        return 6;
    case 2:
        return 8;
    default:
        return 0;
    }
}

static bool at91_aes_make_key(const AT91AESState *s, AES_KEY *key,
                              bool decrypt)
{
    uint8_t bytes[32];
    unsigned int words = at91_aes_key_words(s);
    unsigned int i;
    int result;

    if (!words) {
        return false;
    }
    for (i = 0; i < words; i++) {
        stl_le_p(bytes + i * 4, s->key[i]);
    }
    if (decrypt) {
        result = AES_set_decrypt_key(bytes, words * 32, key);
    } else {
        result = AES_set_encrypt_key(bytes, words * 32, key);
    }
    return result == 0;
}

static void at91_aes_inc16(uint8_t counter[16])
{
    if (++counter[15] == 0) {
        counter[14]++;
    }
}

static void at91_aes_inc32(uint8_t counter[16])
{
    int i;

    for (i = 15; i >= 12; i--) {
        if (++counter[i]) {
            break;
        }
    }
}

static void at91_aes_dec32(uint8_t counter[16])
{
    int i;

    for (i = 15; i >= 12; i--) {
        if (counter[i]-- != 0) {
            break;
        }
    }
}

static void at91_aes_shift_right(uint8_t value[16])
{
    unsigned int i;
    uint8_t carry = 0;

    for (i = 0; i < 16; i++) {
        uint8_t next = value[i] & 1;

        value[i] = (value[i] >> 1) | (carry << 7);
        carry = next;
    }
}

static void at91_aes_gf128_multiply(uint8_t result[16],
                                    const uint8_t x[16],
                                    const uint8_t y[16])
{
    uint8_t z[16] = { 0 };
    uint8_t v[16];
    unsigned int bit;
    unsigned int i;

    memcpy(v, y, sizeof(v));
    for (bit = 0; bit < 128; bit++) {
        if (x[bit / 8] & BIT(7 - bit % 8)) {
            for (i = 0; i < 16; i++) {
                z[i] ^= v[i];
            }
        }
        if (v[15] & 1) {
            at91_aes_shift_right(v);
            v[0] ^= 0xe1;
        } else {
            at91_aes_shift_right(v);
        }
    }
    memcpy(result, z, sizeof(z));
}

static void at91_aes_ghash_update(AT91AESState *s, const uint8_t block[16])
{
    uint8_t hash[16];
    uint8_t h[16];
    unsigned int i;

    at91_aes_words_to_bytes(s->ghash, hash);
    at91_aes_words_to_bytes(s->gcmh, h);
    for (i = 0; i < 16; i++) {
        hash[i] ^= block[i];
    }
    at91_aes_gf128_multiply(hash, hash, h);
    at91_aes_bytes_to_words(hash, s->ghash);
}

static void at91_aes_xts_mul_x(uint8_t tweak[16])
{
    unsigned int i;
    unsigned int carry = 0;

    for (i = 0; i < 16; i++) {
        unsigned int next = tweak[i] >> 7;

        tweak[i] = (tweak[i] << 1) | carry;
        carry = next;
    }
    if (carry) {
        tweak[0] ^= 0x87;
    }
}

static void at91_aes_generate_gcm_h(AT91AESState *s)
{
    AES_KEY key;
    uint8_t zero[16] = { 0 };
    uint8_t h[16];

    if (!at91_aes_make_key(s, &key, false)) {
        return;
    }
    AES_encrypt(zero, h, &key);
    at91_aes_bytes_to_words(h, s->gcmh);
    memset(s->ghash, 0, sizeof(s->ghash));
    memset(s->tag, 0, sizeof(s->tag));
    s->gcm_aad_done = 0;
    s->gcm_text_done = 0;
    s->gcm_counter_valid = false;
    at91_aes_set_isr(s, AES_INT_DATRDY);
}

static void at91_aes_initialize_gcm_counter(AT91AESState *s)
{
    if (s->gcm_counter_valid) {
        return;
    }
    at91_aes_words_to_bytes(s->iv, s->gcm_counter);
    memcpy(s->gcm_j0, s->gcm_counter, sizeof(s->gcm_j0));
    at91_aes_dec32(s->gcm_j0);
    s->gcm_counter_valid = true;
}

static void at91_aes_finalize_gcm(AT91AESState *s, const AES_KEY *key)
{
    uint8_t lengths[16];
    uint8_t hash[16];
    uint8_t encrypted_j0[16];
    uint8_t tag[16];
    unsigned int i;

    stq_be_p(lengths, (uint64_t)s->aadlen * 8);
    stq_be_p(lengths + 8, (uint64_t)s->clen * 8);
    at91_aes_ghash_update(s, lengths);
    at91_aes_words_to_bytes(s->ghash, hash);
    AES_encrypt(s->gcm_j0, encrypted_j0, key);
    for (i = 0; i < 16; i++) {
        tag[i] = hash[i] ^ encrypted_j0[i];
    }
    at91_aes_bytes_to_words(tag, s->tag);
    at91_aes_set_isr(s, AES_INT_TAGRDY);
}

static bool at91_aes_process_gcm(AT91AESState *s, const AES_KEY *key)
{
    uint8_t block[16];
    uint8_t stream[16];
    uint32_t remaining;
    unsigned int valid;
    unsigned int i;

    at91_aes_initialize_gcm_counter(s);
    if (s->gcm_aad_done < s->aadlen) {
        remaining = s->aadlen - s->gcm_aad_done;
        valid = MIN(remaining, 16U);
        memset(block, 0, sizeof(block));
        memcpy(block, s->input, valid);
        at91_aes_ghash_update(s, block);
        s->gcm_aad_done += valid;
        memset(s->output, 0, sizeof(s->output));
        if (s->gcm_aad_done == s->aadlen && s->clen == 0 &&
            (s->mr & AES_MR_GTAGEN)) {
            at91_aes_finalize_gcm(s, key);
        }
        return false;
    }

    remaining = s->clen - MIN(s->gcm_text_done, s->clen);
    valid = MIN(remaining, 16U);
    AES_encrypt(s->gcm_counter, stream, key);
    at91_aes_inc32(s->gcm_counter);
    for (i = 0; i < 16; i++) {
        s->output[i] = s->input[i] ^ stream[i];
    }
    memset(block, 0, sizeof(block));
    if (s->mr & AES_MR_CIPHER) {
        memcpy(block, s->output, valid);
    } else {
        memcpy(block, s->input, valid);
    }
    at91_aes_ghash_update(s, block);
    s->gcm_text_done += valid;
    if (s->gcm_text_done >= s->clen && (s->mr & AES_MR_GTAGEN)) {
        at91_aes_finalize_gcm(s, key);
    }
    return true;
}

static bool at91_aes_process_block(AT91AESState *s)
{
    AES_KEY key;
    uint8_t iv[16];
    uint8_t tmp[16];
    uint8_t original[16];
    uint8_t tweak[16];
    unsigned int mode = at91_aes_opmode(s);
    unsigned int i;
    bool decrypt = !(s->mr & AES_MR_CIPHER);

    if (mode == AES_OPMODE_GCM) {
        if (!at91_aes_make_key(s, &key, false)) {
            return false;
        }
        return at91_aes_process_gcm(s, &key);
    }
    if (!at91_aes_make_key(s, &key, decrypt &&
                           mode != AES_OPMODE_CTR &&
                           mode != AES_OPMODE_OFB &&
                           mode != AES_OPMODE_CFB)) {
        return false;
    }

    memcpy(original, s->input, sizeof(original));
    at91_aes_words_to_bytes(s->iv, iv);
    switch (mode) {
    case AES_OPMODE_ECB:
        if (decrypt) {
            AES_decrypt(s->input, s->output, &key);
        } else {
            AES_encrypt(s->input, s->output, &key);
        }
        break;
    case AES_OPMODE_CBC:
        if (decrypt) {
            AES_decrypt(s->input, tmp, &key);
            for (i = 0; i < 16; i++) {
                s->output[i] = tmp[i] ^ iv[i];
            }
            memcpy(iv, original, sizeof(iv));
        } else {
            for (i = 0; i < 16; i++) {
                tmp[i] = s->input[i] ^ iv[i];
            }
            AES_encrypt(tmp, s->output, &key);
            memcpy(iv, s->output, sizeof(iv));
        }
        at91_aes_bytes_to_words(iv, s->iv);
        break;
    case AES_OPMODE_OFB:
        AES_encrypt(iv, tmp, &key);
        for (i = 0; i < 16; i++) {
            s->output[i] = s->input[i] ^ tmp[i];
        }
        at91_aes_bytes_to_words(tmp, s->iv);
        break;
    case AES_OPMODE_CFB: {
        unsigned int segment = s->output_size;

        AES_encrypt(iv, tmp, &key);
        for (i = 0; i < segment; i++) {
            s->output[i] = s->input[i] ^ tmp[i];
        }
        memmove(iv, iv + segment, sizeof(iv) - segment);
        memcpy(iv + sizeof(iv) - segment,
               decrypt ? original : s->output, segment);
        at91_aes_bytes_to_words(iv, s->iv);
        break;
    }
    case AES_OPMODE_CTR:
        AES_encrypt(iv, tmp, &key);
        for (i = 0; i < 16; i++) {
            s->output[i] = s->input[i] ^ tmp[i];
        }
        at91_aes_inc16(iv);
        at91_aes_bytes_to_words(iv, s->iv);
        break;
    case AES_OPMODE_XTS:
        at91_aes_words_to_bytes(s->tweak, tmp);
        for (i = 0; i < 16; i++) {
            tweak[i] = tmp[15 - i];
        }
        for (i = 0; i < 16; i++) {
            tmp[i] = s->input[i] ^ tweak[i];
        }
        if (decrypt) {
            AES_decrypt(tmp, s->output, &key);
        } else {
            AES_encrypt(tmp, s->output, &key);
        }
        for (i = 0; i < 16; i++) {
            s->output[i] ^= tweak[i];
        }
        at91_aes_xts_mul_x(tweak);
        for (i = 0; i < 16; i++) {
            tmp[i] = tweak[15 - i];
        }
        at91_aes_bytes_to_words(tmp, s->tweak);
        break;
    default:
        memset(s->output, 0, sizeof(s->output));
        return false;
    }
    return true;
}

static unsigned int at91_aes_transfer_size(const AT91AESState *s)
{
    static const uint8_t cfb_size[] = { 16, 8, 4, 2, 1 };
    unsigned int cfbs;

    if (at91_aes_opmode(s) != AES_OPMODE_CFB) {
        return 16;
    }
    cfbs = (s->mr & AES_MR_CFBS_MASK) >> AES_MR_CFBS_SHIFT;
    return cfbs < ARRAY_SIZE(cfb_size) ? cfb_size[cfbs] : 16;
}

static void at91_aes_maybe_process(AT91AESState *s)
{
    bool has_output;

    if (!s->input_ready || !clock_get_hz(s->pclk) || s->output_pending) {
        at91_aes_update_requests(s);
        return;
    }
    if (!s->key_complete) {
        at91_aes_raise_swe(s, AES_CR, AES_SWE_INCOMPLETE_KEY, true);
        s->input_ready = false;
        s->input_valid = 0;
        s->dma_input_pos = 0;
        at91_aes_update_requests(s);
        return;
    }

    at91_aes_clear_isr(s, AES_INT_DATRDY | AES_INT_TAGRDY);
    s->output_size = at91_aes_transfer_size(s);
    has_output = at91_aes_process_block(s);
    s->input_ready = false;
    s->input_valid = 0;
    s->dma_input_pos = 0;
    s->dma_output_pos = 0;
    if (has_output && at91_aes_smod(s) == AES_SMOD_DMA &&
        !(s->mr & AES_MR_LOD)) {
        s->output_pending = true;
    }
    at91_aes_set_isr(s, AES_INT_DATRDY);
    at91_aes_update_requests(s);
}

static void at91_aes_key_written(AT91AESState *s, unsigned int index,
                                 uint32_t value)
{
    unsigned int words = at91_aes_key_words(s);
    uint16_t required;

    if (index == 0) {
        s->key_written = 0;
        s->key_complete = false;
    }
    s->key[index] = value;
    s->key_written |= BIT(index);
    at91_aes_clear_isr(s, AES_INT_TAGRDY);
    if (!words) {
        at91_aes_update_requests(s);
        return;
    }
    required = MAKE_64BIT_MASK(0, words);
    if (index == words - 1 && (s->key_written & required) == required) {
        s->key_complete = true;
        at91_aes_generate_gcm_h(s);
    }
    at91_aes_update_requests(s);
}

static void at91_aes_finish_dma_output(AT91AESState *s)
{
    s->output_pending = false;
    s->dma_output_pos = 0;
    at91_aes_clear_isr(s, AES_INT_DATRDY);
    at91_aes_update_requests(s);
}

static uint64_t at91_aes_read_output(AT91AESState *s, hwaddr offset,
                                     unsigned int size)
{
    unsigned int position;
    uint64_t value;

    if (at91_aes_smod(s) == AES_SMOD_DMA && offset == AES_ODATAR0) {
        position = s->dma_output_pos;
        if (position + size > s->output_size) {
            return 0;
        }
        value = at91_aes_load_le(s->output + position, size);
        s->dma_output_pos += size;
        if (s->dma_output_pos >= s->output_size) {
            at91_aes_finish_dma_output(s);
        }
        return value;
    }

    position = offset - AES_ODATAR0;
    value = at91_aes_load_le(s->output + position, size);
    at91_aes_clear_isr(s, AES_INT_DATRDY);
    return value;
}

static uint64_t at91_aes_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91AESState *s = AT91_AES(opaque);
    uint32_t value;
    unsigned int index;

    if (offset >= AES_ODATAR0 && offset <= AES_ODATAR3) {
        return at91_aes_read_output(s, offset, size);
    }
    if (offset >= AES_GHASHR0 && offset <= AES_GHASHR3) {
        index = (offset - AES_GHASHR0) / 4;
        return s->ghash[index];
    }
    if (offset >= AES_TAGR0 && offset <= AES_TAGR3) {
        index = (offset - AES_TAGR0) / 4;
        value = s->tag[index];
        at91_aes_clear_isr(s, AES_INT_TAGRDY);
        return value;
    }
    if (offset >= AES_GCMHR0 && offset <= AES_GCMHR3) {
        index = (offset - AES_GCMHR0) / 4;
        return s->gcmh[index];
    }
    if (offset >= AES_TWR0 && offset <= AES_TWR3) {
        index = (offset - AES_TWR0) / 4;
        return s->tweak[index];
    }

    switch (offset) {
    case AES_MR:
        return s->mr;
    case AES_IMR:
        return s->imr;
    case AES_ISR:
        value = s->isr;
        s->isr &= ~(AES_INT_SECE | AES_INT_PLENERR | AES_INT_EOPAD);
        at91_aes_update_irq(s);
        return value;
    case AES_AADLENR:
        return s->aadlen;
    case AES_CLENR:
        return s->clen;
    case AES_CTRR:
        if (!s->gcm_counter_valid) {
            return 0;
        }
        return ldl_be_p(s->gcm_counter + 12);
    case AES_EMR:
        return s->emr;
    case AES_BCNT:
        return s->bcnt;
    case AES_WPMR:
        return s->wpmr;
    case AES_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case AES_VERSION:
        return s->version;
    case AES_CR:
    case AES_IER:
    case AES_IDR:
        at91_aes_raise_urad(s, AES_URAT_WOR_RD);
        at91_aes_raise_swe(s, offset, AES_SWE_READ_WO, false);
        return 0;
    default:
        if ((offset >= AES_KEYWR0 && offset <= AES_KEYWR7) ||
            (offset >= AES_IDATAR0 && offset <= AES_IDATAR3) ||
            (offset >= AES_IVR0 && offset <= AES_IVR3) ||
            (offset >= AES_ALPHAR0 && offset <= AES_ALPHAR3)) {
            at91_aes_raise_urad(s, AES_URAT_WOR_RD);
            at91_aes_raise_swe(s, offset, AES_SWE_READ_WO, false);
            return 0;
        }
        at91_aes_raise_swe(s, offset, AES_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_AES ": read from reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_aes_reset_registers(AT91AESState *s, bool hardware)
{
    s->mr = AES_MR_RESET;
    s->imr = 0;
    s->isr = 0;
    memset(s->key, 0, sizeof(s->key));
    memset(s->iv, 0, sizeof(s->iv));
    s->aadlen = 0;
    s->clen = 0;
    memset(s->ghash, 0, sizeof(s->ghash));
    memset(s->tag, 0, sizeof(s->tag));
    memset(s->gcmh, 0, sizeof(s->gcmh));
    s->emr = 0;
    s->bcnt = 0;
    memset(s->tweak, 0, sizeof(s->tweak));
    memset(s->alpha, 0, sizeof(s->alpha));
    s->gcm_aad_done = 0;
    s->gcm_text_done = 0;
    memset(s->input, 0, sizeof(s->input));
    memset(s->output, 0, sizeof(s->output));
    memset(s->gcm_counter, 0, sizeof(s->gcm_counter));
    memset(s->gcm_j0, 0, sizeof(s->gcm_j0));
    s->input_valid = 0;
    s->key_written = 0;
    s->dma_input_pos = 0;
    s->dma_output_pos = 0;
    s->output_size = 16;
    s->mr_key_seen = false;
    s->key_complete = false;
    s->input_ready = false;
    s->output_pending = false;
    s->gcm_counter_valid = false;
    if (hardware) {
        s->wpmr = 0;
        s->wpsr = 0;
    }
    at91_aes_update_irq(s);
    at91_aes_update_requests(s);
}

static void at91_aes_write_input(AT91AESState *s, hwaddr offset,
                                 uint64_t value, unsigned int size)
{
    unsigned int expected = at91_aes_transfer_size(s);
    unsigned int input_size = size;
    unsigned int position;
    uint16_t expected_mask;

    if (s->output_pending) {
        at91_aes_raise_urad(s, 0);
        return;
    }
    if (s->mr & AES_MR_LOD) {
        at91_aes_clear_isr(s, AES_INT_DATRDY);
    }

    if (at91_aes_smod(s) == AES_SMOD_DMA && offset == AES_IDATAR0) {
        position = s->dma_input_pos;
        /*
         * The DMA source transfer type is a word even for CFB8/CFB16;
         * only the low segment bytes are consumed by the accelerator.
         */
        if (at91_aes_opmode(s) == AES_OPMODE_CFB && expected < size &&
            size == sizeof(uint32_t) && position == 0) {
            input_size = expected;
        }
        if (position + input_size > expected) {
            at91_aes_raise_urad(s, 0);
            return;
        }
        at91_aes_store_le(s->input + position, value, input_size);
        s->dma_input_pos += input_size;
        if (s->dma_input_pos >= expected) {
            s->input_ready = true;
        }
    } else {
        position = offset - AES_IDATAR0;
        if (position + size > sizeof(s->input)) {
            return;
        }
        at91_aes_store_le(s->input + position, value, size);
        s->input_valid |= MAKE_64BIT_MASK(position, size);
        expected_mask = MAKE_64BIT_MASK(0, expected);
        if ((s->input_valid & expected_mask) == expected_mask) {
            s->input_ready = true;
        }
    }

    if (s->input_ready && at91_aes_smod(s) != AES_SMOD_MANUAL) {
        at91_aes_maybe_process(s);
    } else {
        at91_aes_update_requests(s);
    }
}

static bool at91_aes_config_protected_offset(hwaddr offset)
{
    return offset == AES_MR ||
           (offset >= AES_KEYWR0 && offset <= AES_KEYWR7) ||
           (offset >= AES_IVR0 && offset <= AES_IVR3) ||
           offset == AES_AADLENR || offset == AES_CLENR ||
           (offset >= AES_GHASHR0 && offset <= AES_GHASHR3) ||
           (offset >= AES_GCMHR0 && offset <= AES_GCMHR3) ||
           offset == AES_EMR || offset == AES_BCNT ||
           (offset >= AES_TWR0 && offset <= AES_TWR3) ||
           (offset >= AES_ALPHAR0 && offset <= AES_ALPHAR3);
}

static void at91_aes_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91AESState *s = AT91_AES(opaque);
    uint32_t val = value;
    unsigned int index;

    if (at91_aes_config_protected_offset(offset) &&
        at91_aes_write_protected(s, offset, AES_WPMR_WPEN)) {
        return;
    }
    if ((offset == AES_IER || offset == AES_IDR) &&
        at91_aes_write_protected(s, offset, AES_WPMR_WPITEN)) {
        return;
    }
    if (offset == AES_CR &&
        at91_aes_write_protected(s, offset, AES_WPMR_WPCREN)) {
        return;
    }

    if (offset >= AES_KEYWR0 && offset <= AES_KEYWR7) {
        index = (offset - AES_KEYWR0) / 4;
        at91_aes_key_written(s, index, val);
        return;
    }
    if (offset >= AES_IDATAR0 && offset <= AES_IDATAR3) {
        at91_aes_write_input(s, offset, value, size);
        return;
    }
    if (offset >= AES_IVR0 && offset <= AES_IVR3) {
        index = (offset - AES_IVR0) / 4;
        s->iv[index] = val;
        if (index == 3 && at91_aes_opmode(s) == AES_OPMODE_GCM) {
            s->gcm_counter_valid = false;
            at91_aes_initialize_gcm_counter(s);
        }
        return;
    }
    if (offset >= AES_GHASHR0 && offset <= AES_GHASHR3) {
        index = (offset - AES_GHASHR0) / 4;
        s->ghash[index] = val;
        return;
    }
    if (offset >= AES_GCMHR0 && offset <= AES_GCMHR3) {
        index = (offset - AES_GCMHR0) / 4;
        s->gcmh[index] = val;
        return;
    }
    if (offset >= AES_TWR0 && offset <= AES_TWR3) {
        index = (offset - AES_TWR0) / 4;
        s->tweak[index] = val;
        return;
    }
    if (offset >= AES_ALPHAR0 && offset <= AES_ALPHAR3) {
        index = (offset - AES_ALPHAR0) / 4;
        s->alpha[index] = val;
        return;
    }

    switch (offset) {
    case AES_CR:
        if (val & AES_CR_SWRST) {
            at91_aes_reset_registers(s, false);
            return;
        }
        if ((val & AES_CR_START) && at91_aes_smod(s) == AES_SMOD_MANUAL) {
            at91_aes_clear_isr(s, AES_INT_DATRDY);
            if (!s->input_ready) {
                return;
            }
            at91_aes_maybe_process(s);
        } else if (val & AES_CR_START) {
            at91_aes_raise_swe(s, offset, AES_SWE_CTRL_START, false);
        }
        if (val & AES_CR_UNLOCK) {
            s->wpsr = 0;
        }
        break;
    case AES_MR:
        if (!s->mr_key_seen &&
            (val & AES_MR_CKEY_MASK) != (0xeU << AES_MR_CKEY_SHIFT)) {
            at91_aes_raise_swe(s, offset, AES_SWE_UNDEF_RW, false);
            break;
        }
        s->mr_key_seen = true;
        s->mr = val & AES_MR_WRITABLE_MASK;
        s->input_valid = 0;
        s->dma_input_pos = 0;
        s->input_ready = false;
        s->output_pending = false;
        s->dma_output_pos = 0;
        s->output_size = at91_aes_transfer_size(s);
        at91_aes_update_requests(s);
        break;
    case AES_IER:
        s->imr |= val & AES_INT_MASK;
        at91_aes_update_irq(s);
        break;
    case AES_IDR:
        s->imr &= ~(val & AES_INT_MASK);
        at91_aes_update_irq(s);
        break;
    case AES_AADLENR:
        s->aadlen = val;
        s->gcm_aad_done = 0;
        break;
    case AES_CLENR:
        s->clen = val;
        s->gcm_text_done = 0;
        s->gcm_counter_valid = false;
        break;
    case AES_EMR:
        s->emr = val & AES_EMR_MASK;
        break;
    case AES_BCNT:
        s->bcnt = val;
        break;
    case AES_WPMR:
        if ((val & AES_WPMR_KEY_MASK) == AES_WPMR_KEY) {
            s->wpmr = val & AES_WPMR_MASK;
        }
        break;
    case AES_IMR:
    case AES_ISR:
    case AES_TAGR0 ... AES_TAGR3:
    case AES_CTRR:
    case AES_WPSR:
    case AES_VERSION:
        at91_aes_raise_swe(s, offset, AES_SWE_WRITE_RO, false);
        break;
    default:
        at91_aes_raise_swe(s, offset, AES_SWE_UNDEF_RW, false);
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_AES ": write to reserved offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_aes_ops = {
    .read = at91_aes_read,
    .write = at91_aes_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_aes_clock_changed(void *opaque, ClockEvent event)
{
    AT91AESState *s = opaque;

    at91_aes_maybe_process(s);
    at91_aes_update_requests(s);
}

static void at91_aes_reset(DeviceState *dev)
{
    AT91AESState *s = AT91_AES(dev);

    at91_aes_reset_registers(s, true);
}

static void at91_aes_init(Object *obj)
{
    AT91AESState *s = AT91_AES(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &at91_aes_ops, s,
                          TYPE_AT91_AES, AES_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_aes_clock_changed,
                                 s, ClockUpdate);
}

static void at91_aes_realize(DeviceState *dev, Error **errp)
{
    AT91AESState *s = AT91_AES(dev);

    if (!clock_has_source(s->pclk)) {
        error_setg(errp, TYPE_AT91_AES ": pclk must be connected");
    }
}

static int at91_aes_post_load(void *opaque, int version_id)
{
    AT91AESState *s = opaque;

    s->input_valid &= 0xffff;
    s->key_written &= 0xff;
    s->dma_input_pos = MIN(s->dma_input_pos, (uint8_t)16);
    s->dma_output_pos = MIN(s->dma_output_pos, (uint8_t)16);
    s->output_size = MIN(MAX(s->output_size, (uint8_t)1), (uint8_t)16);
    s->tx_request_level = false;
    s->rx_request_level = false;
    at91_aes_update_irq(s);
    at91_aes_update_requests(s);
    at91_aes_maybe_process(s);
    return 0;
}

static const VMStateDescription vmstate_at91_aes = {
    .name = TYPE_AT91_AES,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_aes_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91AESState),
        VMSTATE_UINT32(imr, AT91AESState),
        VMSTATE_UINT32(isr, AT91AESState),
        VMSTATE_UINT32_ARRAY(key, AT91AESState, 8),
        VMSTATE_UINT32_ARRAY(iv, AT91AESState, 4),
        VMSTATE_UINT32(aadlen, AT91AESState),
        VMSTATE_UINT32(clen, AT91AESState),
        VMSTATE_UINT32_ARRAY(ghash, AT91AESState, 4),
        VMSTATE_UINT32_ARRAY(tag, AT91AESState, 4),
        VMSTATE_UINT32_ARRAY(gcmh, AT91AESState, 4),
        VMSTATE_UINT32(emr, AT91AESState),
        VMSTATE_UINT32(bcnt, AT91AESState),
        VMSTATE_UINT32_ARRAY(tweak, AT91AESState, 4),
        VMSTATE_UINT32_ARRAY(alpha, AT91AESState, 4),
        VMSTATE_UINT32(wpmr, AT91AESState),
        VMSTATE_UINT32(wpsr, AT91AESState),
        VMSTATE_UINT32(gcm_aad_done, AT91AESState),
        VMSTATE_UINT32(gcm_text_done, AT91AESState),
        VMSTATE_UINT8_ARRAY(input, AT91AESState, 16),
        VMSTATE_UINT8_ARRAY(output, AT91AESState, 16),
        VMSTATE_UINT8_ARRAY(gcm_counter, AT91AESState, 16),
        VMSTATE_UINT8_ARRAY(gcm_j0, AT91AESState, 16),
        VMSTATE_UINT16(input_valid, AT91AESState),
        VMSTATE_UINT8(key_written, AT91AESState),
        VMSTATE_UINT8(dma_input_pos, AT91AESState),
        VMSTATE_UINT8(dma_output_pos, AT91AESState),
        VMSTATE_UINT8(output_size, AT91AESState),
        VMSTATE_BOOL(mr_key_seen, AT91AESState),
        VMSTATE_BOOL(key_complete, AT91AESState),
        VMSTATE_BOOL(input_ready, AT91AESState),
        VMSTATE_BOOL(output_pending, AT91AESState),
        VMSTATE_BOOL(gcm_counter_valid, AT91AESState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_aes_properties[] = {
    DEFINE_PROP_UINT32("version", AT91AESState, version, 0x700),
};

static void at91_aes_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Advanced Encryption Standard accelerator";
    dc->realize = at91_aes_realize;
    dc->vmsd = &vmstate_at91_aes;
    device_class_set_props(dc, at91_aes_properties);
    device_class_set_legacy_reset(dc, at91_aes_reset);
}

static const TypeInfo at91_aes_info = {
    .name = TYPE_AT91_AES,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91AESState),
    .instance_init = at91_aes_init,
    .class_init = at91_aes_class_init,
};

static void at91_aes_register_types(void)
{
    type_register_static(&at91_aes_info);
}

type_init(at91_aes_register_types)
