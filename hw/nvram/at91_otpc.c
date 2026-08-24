/*
 * Microchip AT91 OTP memory controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/nvram/at91_otpc.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define OTPC_CR                 0x00
#define OTPC_MR                 0x04
#define OTPC_AR                 0x08
#define OTPC_SR                 0x0c
#define OTPC_IER                0x10
#define OTPC_IDR                0x14
#define OTPC_IMR                0x18
#define OTPC_ISR                0x1c
#define OTPC_HR                 0x20
#define OTPC_DR                 0x24
#define OTPC_BAR                0x30
#define OTPC_CAR                0x34
#define OTPC_LRMR               0x40
#define OTPC_UHC0R              0x50
#define OTPC_UHC1R              0x54
#define OTPC_UID0R              0x60
#define OTPC_UID1R              0x64
#define OTPC_UID2R              0x68
#define OTPC_UID3R              0x6c
#define OTPC_WPMR               0xe4
#define OTPC_WPSR               0xe8

/* Masks from the SAM9X7 device pack. */
#define OTPC_CR_MASK            0xffff93d7
#define OTPC_MR_MASK            0xffff9391
#define OTPC_AR_MASK            0x000100ff
#define OTPC_SR_MASK            0x000083ff
#define OTPC_INT_MASK           0x10017fff
#define OTPC_HR_MASK            0xffffffbf
#define OTPC_CAR_MASK           0x0000ffff
#define OTPC_LRMR_MASK          0xffff0013
#define OTPC_UHC0R_MASK         0x000000ff
#define OTPC_UHC1R_MASK         0x0003c7ff
#define OTPC_UHC1R_URDDIS       BIT(0)
#define OTPC_UHC1R_UPGDIS       BIT(1)
#define OTPC_UHC1R_URFDIS       BIT(17)
#define OTPC_WPMR_MASK          0xffffff17
#define OTPC_WPSR_MASK          0x8fffff0f

#define OTPC_CR_PGM             BIT(0)
#define OTPC_CR_CKSGEN          BIT(1)
#define OTPC_CR_INVLD           BIT(2)
#define OTPC_CR_HIDE            BIT(4)
#define OTPC_CR_READ            BIT(6)
#define OTPC_CR_FLUSH           BIT(7)
#define OTPC_CR_KBSTART         BIT(8)
#define OTPC_CR_KBSTOP          BIT(9)
#define OTPC_CR_REPAIR          BIT(12)
#define OTPC_CR_REFRESH         BIT(15)
#define OTPC_CR_KEY_SHIFT       16
#define OTPC_CR_USER_KEY        0x7167
#define OTPC_CR_REPAIR_KEY      0x7364
#define OTPC_CR_PACKET_ACCESS   (OTPC_CR_PGM | OTPC_CR_CKSGEN | \
                                 OTPC_CR_INVLD | OTPC_CR_HIDE | \
                                 OTPC_CR_READ | OTPC_CR_KBSTART)

#define OTPC_MR_UHCRRDIS        BIT(0)
#define OTPC_MR_NPCKT           BIT(4)
#define OTPC_MR_EMUL            BIT(7)
#define OTPC_MR_RDDIS           BIT(8)
#define OTPC_MR_WRDIS           BIT(9)
#define OTPC_MR_LOCK            BIT(15)
#define OTPC_MR_ADDR_SHIFT      16
#define OTPC_MR_ADDR_MASK       0xffff0000

#define OTPC_AR_DADDR_MASK      0x000000ff
#define OTPC_AR_INCRT           BIT(16)

#define OTPC_SR_EMUL            BIT(3)
#define OTPC_SR_ONEF            BIT(9)

#define OTPC_ISR_PGERR          BIT(4)
#define OTPC_ISR_LKERR          BIT(5)
#define OTPC_ISR_IVERR          BIT(6)
#define OTPC_ISR_WERR           BIT(7)
#define OTPC_ISR_EOR            BIT(8)
#define OTPC_ISR_EOF            BIT(9)
#define OTPC_ISR_EORF           BIT(11)
#define OTPC_ISR_CKERR          BIT(12)
#define OTPC_ISR_COERR          BIT(13)
#define OTPC_ISR_HDERR          BIT(14)
#define OTPC_ISR_KBERR          BIT(16)
#define OTPC_ISR_SECE           BIT(28)

#define OTPC_HR_PACKET_MASK     0x00000007
#define OTPC_HR_PACKET_KEY      2
#define OTPC_HR_PACKET_BOOT     3
#define OTPC_HR_PACKET_SECURE   4
#define OTPC_HR_PACKET_HARDWARE 5
#define OTPC_HR_PACKET_CUSTOM   6
#define OTPC_HR_LOCK            BIT(3)
#define OTPC_HR_INVLD_SHIFT     4
#define OTPC_HR_INVLD_MASK      0x00000030
#define OTPC_HR_RESERVED        BIT(6)
#define OTPC_HR_ONE             BIT(7)
#define OTPC_HR_SIZE_SHIFT      8
#define OTPC_HR_SIZE_MASK       0x0000ff00

#define OTPC_WPMR_WPCFEN        BIT(0)
#define OTPC_WPMR_WPITEN        BIT(1)
#define OTPC_WPMR_WPCTEN        BIT(2)
#define OTPC_WPMR_FIRSTE        BIT(4)
#define OTPC_WPMR_CONFIG_MASK   0x00000017
#define OTPC_WPMR_KEY_SHIFT     8
#define OTPC_WPMR_KEY           0x4f5450

#define OTPC_WPSR_WPVS          BIT(0)
#define OTPC_WPSR_CLEARED_MASK  0x0000000f
#define OTPC_WPSR_SWE           BIT(3)
#define OTPC_WPSR_WPVSRC_SHIFT  8
#define OTPC_WPSR_WPVSRC_MASK   0x00ffff00
#define OTPC_WPSR_SWETYP_SHIFT  24
#define OTPC_WPSR_SWETYP_MASK   0x0f000000
#define OTPC_WPSR_ECLASS        BIT(31)

#define OTPC_SWE_READ_WO        0
#define OTPC_SWE_WRITE_RO       1
#define OTPC_SWE_KEY_ERROR      3

#define OTPC_BOOT_SIZE          17
#define OTPC_SECURE_SIZE        7
#define OTPC_HARDWARE_SIZE      1

typedef struct AT91OTPCPacket {
    uint32_t address;
    uint32_t header;
    uint32_t payload_words;
} AT91OTPCPacket;

static void at91_otpc_update_irq(AT91OTPCState *s)
{
    qemu_set_irq(s->irq, (s->isr & s->imr) != 0);
}

static void at91_otpc_set_interrupts(AT91OTPCState *s, uint32_t interrupts)
{
    s->isr |= interrupts & OTPC_INT_MASK;
    at91_otpc_update_irq(s);
}

static void at91_otpc_report_software_error(AT91OTPCState *s,
                                             unsigned int type,
                                             bool error)
{
    if (!(s->wpmr & OTPC_WPMR_FIRSTE) || !(s->wpsr & OTPC_WPSR_SWE)) {
        s->wpsr &= ~(OTPC_WPSR_SWETYP_MASK | OTPC_WPSR_ECLASS);
        s->wpsr |= (type << OTPC_WPSR_SWETYP_SHIFT) &
                   OTPC_WPSR_SWETYP_MASK;
        if (error) {
            s->wpsr |= OTPC_WPSR_ECLASS;
        }
    }
    s->wpsr |= OTPC_WPSR_SWE;
    at91_otpc_set_interrupts(s, OTPC_ISR_SECE);
}

static void at91_otpc_report_write_protection(AT91OTPCState *s,
                                               hwaddr offset)
{
    if (!(s->wpmr & OTPC_WPMR_FIRSTE) || !(s->wpsr & OTPC_WPSR_WPVS)) {
        s->wpsr &= ~OTPC_WPSR_WPVSRC_MASK;
        s->wpsr |= (offset << OTPC_WPSR_WPVSRC_SHIFT) &
                   OTPC_WPSR_WPVSRC_MASK;
    }
    s->wpsr |= OTPC_WPSR_WPVS;
    at91_otpc_set_interrupts(s, OTPC_ISR_SECE);
}

static bool at91_otpc_write_is_protected(AT91OTPCState *s, hwaddr offset,
                                          uint32_t enable)
{
    if (!(s->wpmr & enable)) {
        return false;
    }

    at91_otpc_report_write_protection(s, offset);
    return true;
}

static size_t at91_otpc_source_words(AT91OTPCState *s)
{
    return s->emulation_active ?
           AT91_OTPC_EMULATION_MEMORY_SIZE / sizeof(uint32_t) :
           AT91_OTPC_OTP_WORDS;
}

static bool at91_otpc_source_read(AT91OTPCState *s, uint32_t address,
                                  uint32_t *value)
{
    uint32_t little_endian_value;

    if (address >= at91_otpc_source_words(s)) {
        return false;
    }

    if (!s->emulation_active) {
        *value = s->otp[address];
        return true;
    }

    if (address_space_read(&s->emulation_as,
                           (hwaddr)address * sizeof(uint32_t),
                           MEMTXATTRS_UNSPECIFIED, &little_endian_value,
                           sizeof(little_endian_value)) != MEMTX_OK) {
        return false;
    }

    *value = le32_to_cpu(little_endian_value);
    return true;
}

static bool at91_otpc_otp_contains_one(AT91OTPCState *s)
{
    size_t i;

    for (i = 0; i < ARRAY_SIZE(s->otp); i++) {
        if (s->otp[i]) {
            return true;
        }
    }
    return false;
}

static void at91_otpc_mark_corrupt(AT91OTPCState *s, uint32_t address,
                                    uint32_t header)
{
    s->hr = header & OTPC_HR_MASK;
    s->mr = (s->mr & ~OTPC_MR_ADDR_MASK) |
            ((address << OTPC_MR_ADDR_SHIFT) & OTPC_MR_ADDR_MASK);
    at91_otpc_set_interrupts(s, OTPC_ISR_COERR);
}

static bool at91_otpc_special_size_valid(uint32_t packet, uint32_t size)
{
    switch (packet) {
    case OTPC_HR_PACKET_BOOT:
        return size == OTPC_BOOT_SIZE;
    case OTPC_HR_PACKET_SECURE:
        return size == OTPC_SECURE_SIZE;
    case OTPC_HR_PACKET_HARDWARE:
        return size == OTPC_HARDWARE_SIZE;
    default:
        return true;
    }
}

static bool at91_otpc_header_valid(uint32_t header)
{
    uint32_t packet = header & OTPC_HR_PACKET_MASK;

    return (header & OTPC_HR_ONE) && !(header & OTPC_HR_RESERVED) &&
           packet >= 1 && packet <= OTPC_HR_PACKET_CUSTOM;
}

static bool at91_otpc_find_tail(AT91OTPCState *s,
                                AT91OTPCPacket *result)
{
    size_t words = at91_otpc_source_words(s);
    uint32_t address = 0;

    while (address < words) {
        uint32_t header, packet, size, invalid;
        uint64_t next;

        if (!at91_otpc_source_read(s, address, &header)) {
            return false;
        }

        packet = header & OTPC_HR_PACKET_MASK;
        size = extract32(header, OTPC_HR_SIZE_SHIFT, 8);
        invalid = extract32(header, OTPC_HR_INVLD_SHIFT, 2);
        next = (uint64_t)address + size + 2;

        /*
         * ADDR=0xffff selects the prospective new-packet location.  Return
         * its raw contents so software can account for pre-programmed ones.
         */
        if (header == 0 || !at91_otpc_header_valid(header) ||
            (invalid != 3 && !at91_otpc_special_size_valid(packet, size))) {
            result->address = address;
            result->header = header;
            result->payload_words = MIN(size + 1,
                                        (uint32_t)(words - address - 1));
            return true;
        }
        if (next > words) {
            return false;
        }
        address = next;
    }

    return false;
}

static void at91_otpc_scan_area(AT91OTPCState *s)
{
    size_t words = at91_otpc_source_words(s);
    uint32_t address = 0;

    s->bar = 0;
    s->car = 0;
    s->uhc[0] = 0;
    s->uhc[1] = 0;

    while (address < words) {
        uint32_t header, packet, size, invalid;
        uint64_t next;

        if (!at91_otpc_source_read(s, address, &header)) {
            at91_otpc_mark_corrupt(s, address, 0);
            return;
        }

        /* An all-zero word is the normal end of the programmed area. */
        if (header == 0) {
            return;
        }

        packet = header & OTPC_HR_PACKET_MASK;
        size = extract32(header, OTPC_HR_SIZE_SHIFT, 8);
        invalid = extract32(header, OTPC_HR_INVLD_SHIFT, 2);
        next = (uint64_t)address + size + 2;

        if (!at91_otpc_header_valid(header) || next > words) {
            at91_otpc_mark_corrupt(s, address, header);
            return;
        }

        if (invalid == 3) {
            address = next;
            continue;
        }
        if (!at91_otpc_special_size_valid(packet, size)) {
            at91_otpc_mark_corrupt(s, address, header);
            return;
        }

        /* Checksum calculation is deliberately left for the next slice. */
        if (header & OTPC_HR_LOCK) {
            at91_otpc_set_interrupts(s, OTPC_ISR_CKERR);
            address = next;
            continue;
        }

        switch (packet) {
        case OTPC_HR_PACKET_BOOT:
            s->bar = (s->bar & 0xffff0000) | address;
            break;
        case OTPC_HR_PACKET_SECURE:
            s->bar = (s->bar & 0x0000ffff) | (address << 16);
            break;
        case OTPC_HR_PACKET_HARDWARE:
            if (!at91_otpc_source_read(s, address + 1, &s->uhc[0]) ||
                !at91_otpc_source_read(s, address + 2, &s->uhc[1])) {
                at91_otpc_mark_corrupt(s, address, header);
                return;
            }
            s->uhc[0] &= OTPC_UHC0R_MASK;
            s->uhc[1] &= OTPC_UHC1R_MASK;
            break;
        case OTPC_HR_PACKET_CUSTOM:
            s->car = address & OTPC_CAR_MASK;
            break;
        default:
            break;
        }

        address = next;
    }
}

static bool at91_otpc_find_packet(AT91OTPCState *s, uint32_t selected,
                                  AT91OTPCPacket *result)
{
    size_t words = at91_otpc_source_words(s);
    uint32_t address = 0;

    if (selected == UINT16_MAX) {
        return at91_otpc_find_tail(s, result);
    }
    if (selected >= words) {
        return false;
    }

    while (address < words) {
        uint32_t header, packet, size, invalid;
        bool corrupt, overflow;
        uint64_t next;

        if (!at91_otpc_source_read(s, address, &header)) {
            return false;
        }

        if (header == 0) {
            /* Blank words behave as an empty, one-word packet buffer. */
            result->address = selected;
            result->header = 0;
            result->payload_words = 1;
            return true;
        }

        packet = header & OTPC_HR_PACKET_MASK;
        size = extract32(header, OTPC_HR_SIZE_SHIFT, 8);
        invalid = extract32(header, OTPC_HR_INVLD_SHIFT, 2);
        next = (uint64_t)address + size + 2;
        overflow = next > words;
        corrupt = !at91_otpc_header_valid(header) || overflow ||
                  (invalid != 3 &&
                   !at91_otpc_special_size_valid(packet, size));
        if (selected >= address &&
            selected < (overflow ? words : next)) {
            result->address = address;
            result->header = header;
            result->payload_words = MIN(size + 1 + corrupt,
                                        (uint32_t)AT91_OTPC_TEMP_WORDS);
            return true;
        }
        if (overflow) {
            return false;
        }
        address = next;
    }

    return false;
}

static void at91_otpc_flush(AT91OTPCState *s)
{
    s->hr = 0;
    memset(s->temporary, 0, sizeof(s->temporary));
    s->temporary_words = 0;
    s->sr &= ~OTPC_SR_ONEF;
    at91_otpc_set_interrupts(s, OTPC_ISR_EOF);
}

static bool at91_otpc_temporary_has_data(AT91OTPCState *s)
{
    size_t i;

    if (s->hr) {
        return true;
    }
    for (i = 0; i < s->temporary_words; i++) {
        if (s->temporary[i]) {
            return true;
        }
    }
    return false;
}

static void at91_otpc_read_packet(AT91OTPCState *s)
{
    AT91OTPCPacket packet;
    uint32_t selected = extract32(s->mr, OTPC_MR_ADDR_SHIFT, 16);
    uint32_t packet_type;
    bool one_found;
    size_t i;

    memset(s->temporary, 0, sizeof(s->temporary));
    s->temporary_words = 0;
    s->hr = 0;
    s->sr &= ~OTPC_SR_ONEF;

    if (!at91_otpc_find_packet(s, selected, &packet)) {
        at91_otpc_set_interrupts(s, OTPC_ISR_EOR);
        return;
    }

    s->hr = packet.header & OTPC_HR_MASK;
    s->temporary_words = packet.payload_words;
    packet_type = packet.header & OTPC_HR_PACKET_MASK;
    one_found = false;

    if (packet.header & OTPC_HR_LOCK) {
        at91_otpc_set_interrupts(s, OTPC_ISR_CKERR);
    }

    for (i = 0; i < packet.payload_words; i++) {
        uint32_t value = 0;

        if (!at91_otpc_source_read(s, packet.address + i + 1, &value)) {
            break;
        }
        if (packet_type == OTPC_HR_PACKET_KEY) {
            value = 0;
        }
        s->temporary[i] = value;
        one_found |= value != 0;
    }

    if (one_found) {
        s->sr |= OTPC_SR_ONEF;
    }
    at91_otpc_set_interrupts(s, OTPC_ISR_EOR);
}

static void at91_otpc_refresh(AT91OTPCState *s)
{
    s->otp_ever_programmed |= at91_otpc_otp_contains_one(s);
    s->emulation_active = (s->mr & OTPC_MR_EMUL) &&
                          !s->otp_ever_programmed;
    s->sr = (s->sr & ~OTPC_SR_EMUL) |
            (s->emulation_active ? OTPC_SR_EMUL : 0);
    at91_otpc_scan_area(s);
    at91_otpc_set_interrupts(s, OTPC_ISR_EORF);
}

static void at91_otpc_control_write(AT91OTPCState *s, uint32_t value)
{
    uint32_t commands = value & OTPC_CR_MASK;
    uint32_t key = value >> OTPC_CR_KEY_SHIFT;
    uint32_t user_keyed = OTPC_CR_PGM | OTPC_CR_CKSGEN | OTPC_CR_INVLD |
                          OTPC_CR_HIDE | OTPC_CR_KBSTART | OTPC_CR_KBSTOP |
                          OTPC_CR_REFRESH;

    if (at91_otpc_write_is_protected(s, OTPC_CR, OTPC_WPMR_WPCTEN)) {
        return;
    }

    if (s->mr & OTPC_MR_LOCK) {
        commands &= ~OTPC_CR_PACKET_ACCESS;
    }
    if (s->uhc[1] & OTPC_UHC1R_URDDIS) {
        commands &= ~OTPC_CR_READ;
    }
    if (s->uhc[1] & OTPC_UHC1R_UPGDIS) {
        commands &= ~OTPC_CR_PGM;
    }
    if ((s->uhc[1] & OTPC_UHC1R_URFDIS) &&
        !(s->sr & OTPC_SR_EMUL)) {
        commands &= ~OTPC_CR_REFRESH;
    }

    if (commands & OTPC_CR_FLUSH) {
        at91_otpc_flush(s);
    }
    if (commands & OTPC_CR_READ) {
        at91_otpc_read_packet(s);
    }

    if ((commands & user_keyed) && key != OTPC_CR_USER_KEY) {
        at91_otpc_report_software_error(s, OTPC_SWE_KEY_ERROR, true);
    } else {
        uint32_t unsupported = commands &
                               (OTPC_CR_PGM | OTPC_CR_CKSGEN |
                                OTPC_CR_INVLD | OTPC_CR_HIDE |
                                OTPC_CR_KBSTART | OTPC_CR_KBSTOP);

        if (unsupported) {
            qemu_log_mask(LOG_UNIMP,
                          TYPE_AT91_OTPC ": command(s) 0x%08x not "
                          "implemented\n", unsupported);
        }
        if (commands & OTPC_CR_PGM) {
            at91_otpc_set_interrupts(s, OTPC_ISR_PGERR);
        }
        if (commands & OTPC_CR_CKSGEN) {
            at91_otpc_set_interrupts(s, OTPC_ISR_LKERR);
        }
        if (commands & OTPC_CR_INVLD) {
            at91_otpc_set_interrupts(s, OTPC_ISR_IVERR);
        }
        if (commands & OTPC_CR_HIDE) {
            at91_otpc_set_interrupts(s, OTPC_ISR_HDERR);
        }
        if (commands & (OTPC_CR_KBSTART | OTPC_CR_KBSTOP)) {
            at91_otpc_set_interrupts(s, OTPC_ISR_KBERR);
        }
        if (commands & OTPC_CR_REFRESH) {
            at91_otpc_refresh(s);
        }
    }

    if (commands & OTPC_CR_REPAIR) {
        if (key != OTPC_CR_REPAIR_KEY) {
            at91_otpc_report_software_error(s, OTPC_SWE_KEY_ERROR, true);
        } else {
            at91_otpc_set_interrupts(s, OTPC_ISR_WERR);
        }
    }
}

static uint64_t at91_otpc_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91OTPCState *s = AT91_OTPC(opaque);
    uint32_t value;

    switch (offset) {
    case OTPC_CR:
    case OTPC_IER:
    case OTPC_IDR:
        at91_otpc_report_software_error(s, OTPC_SWE_READ_WO, false);
        return 0;
    case OTPC_MR:
        return s->mr & OTPC_MR_MASK;
    case OTPC_AR:
        return s->ar & OTPC_AR_MASK;
    case OTPC_SR:
        return s->sr & OTPC_SR_MASK;
    case OTPC_IMR:
        return s->imr & OTPC_INT_MASK;
    case OTPC_ISR:
        value = s->isr & OTPC_INT_MASK;
        s->isr = 0;
        at91_otpc_update_irq(s);
        return value;
    case OTPC_HR:
        return (s->mr & OTPC_MR_RDDIS) ? 0 : s->hr & OTPC_HR_MASK;
    case OTPC_DR:
        value = (s->mr & OTPC_MR_RDDIS) ? 0 :
                s->temporary[s->ar & OTPC_AR_DADDR_MASK];
        if (!(s->ar & OTPC_AR_INCRT)) {
            s->ar = (s->ar & ~OTPC_AR_DADDR_MASK) |
                    ((s->ar + 1) & OTPC_AR_DADDR_MASK);
        }
        return value;
    case OTPC_BAR:
        return s->bar;
    case OTPC_CAR:
        return s->car & OTPC_CAR_MASK;
    case OTPC_LRMR:
        return s->lrmr & (OTPC_LRMR_MASK & 0x0000ffff);
    case OTPC_UHC0R:
        return (s->mr & OTPC_MR_UHCRRDIS) ? 0 :
               s->uhc[0] & OTPC_UHC0R_MASK;
    case OTPC_UHC1R:
        return (s->mr & OTPC_MR_UHCRRDIS) ? 0 :
               s->uhc[1] & OTPC_UHC1R_MASK;
    case OTPC_UID0R:
    case OTPC_UID1R:
    case OTPC_UID2R:
    case OTPC_UID3R:
        return s->uid[(offset - OTPC_UID0R) / sizeof(uint32_t)];
    case OTPC_WPMR:
        return s->wpmr & OTPC_WPMR_CONFIG_MASK;
    case OTPC_WPSR:
        value = s->wpsr & OTPC_WPSR_MASK;
        s->wpsr &= ~OTPC_WPSR_CLEARED_MASK;
        return value;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_OTPC ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_otpc_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91OTPCState *s = AT91_OTPC(opaque);
    uint32_t old_mr;

    switch (offset) {
    case OTPC_CR:
        at91_otpc_control_write(s, value);
        break;
    case OTPC_MR:
        if (at91_otpc_write_is_protected(s, offset,
                                          OTPC_WPMR_WPCFEN)) {
            break;
        }
        if (s->mr & OTPC_MR_LOCK) {
            break;
        }
        old_mr = s->mr;
        s->mr = value & OTPC_MR_MASK;
        if (!(old_mr & OTPC_MR_NPCKT) && (s->mr & OTPC_MR_NPCKT) &&
            at91_otpc_temporary_has_data(s)) {
            at91_otpc_flush(s);
        }
        break;
    case OTPC_AR:
        s->ar = value & OTPC_AR_MASK;
        break;
    case OTPC_IER:
        if (!at91_otpc_write_is_protected(s, offset,
                                           OTPC_WPMR_WPITEN)) {
            s->imr |= value & OTPC_INT_MASK;
            at91_otpc_update_irq(s);
        }
        break;
    case OTPC_IDR:
        if (!at91_otpc_write_is_protected(s, offset,
                                           OTPC_WPMR_WPITEN)) {
            s->imr &= ~(value & OTPC_INT_MASK);
            at91_otpc_update_irq(s);
        }
        break;
    case OTPC_HR:
        if (s->mr & OTPC_MR_NPCKT) {
            s->hr = (value & (OTPC_HR_SIZE_MASK |
                              OTPC_HR_PACKET_MASK)) | OTPC_HR_ONE;
        }
        break;
    case OTPC_DR:
        if (!(s->mr & OTPC_MR_WRDIS)) {
            uint32_t address = s->ar & OTPC_AR_DADDR_MASK;

            s->temporary[address] = value;
            s->temporary_words = MAX(s->temporary_words, address + 1);
            if (s->ar & OTPC_AR_INCRT) {
                s->ar = (s->ar & ~OTPC_AR_DADDR_MASK) |
                        ((s->ar + 1) & OTPC_AR_DADDR_MASK);
            }
        }
        break;
    case OTPC_LRMR:
        if ((value >> 16) == OTPC_CR_REPAIR_KEY) {
            s->lrmr = value & (OTPC_LRMR_MASK & 0x0000ffff);
        }
        break;
    case OTPC_WPMR:
        if ((value >> OTPC_WPMR_KEY_SHIFT) != OTPC_WPMR_KEY) {
            at91_otpc_report_software_error(s, OTPC_SWE_KEY_ERROR, true);
        } else {
            s->wpmr = value & OTPC_WPMR_CONFIG_MASK;
        }
        break;
    case OTPC_SR:
    case OTPC_IMR:
    case OTPC_ISR:
    case OTPC_BAR:
    case OTPC_CAR:
    case OTPC_UHC0R:
    case OTPC_UHC1R:
    case OTPC_UID0R:
    case OTPC_UID1R:
    case OTPC_UID2R:
    case OTPC_UID3R:
    case OTPC_WPSR:
        at91_otpc_report_software_error(s, OTPC_SWE_WRITE_RO, false);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_OTPC ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_otpc_ops = {
    .read = at91_otpc_read,
    .write = at91_otpc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_otpc_reset(DeviceState *dev)
{
    AT91OTPCState *s = AT91_OTPC(dev);

    s->otp_ever_programmed |= at91_otpc_otp_contains_one(s);
    s->mr = 0;
    s->ar = 0;
    s->sr = 0;
    s->imr = 0;
    s->isr = 0;
    s->hr = 0;
    s->bar = 0;
    s->car = 0;
    s->lrmr = 0;
    s->uhc[0] = 0;
    s->uhc[1] = 0;
    s->wpmr = 0;
    s->wpsr = 0;
    s->temporary_words = 0;
    s->emulation_active = false;
    memset(s->temporary, 0, sizeof(s->temporary));

    at91_otpc_scan_area(s);
    at91_otpc_update_irq(s);
}

static void at91_otpc_init(Object *obj)
{
    AT91OTPCState *s = AT91_OTPC(obj);

    memory_region_init_io(&s->mmio, obj, &at91_otpc_ops, s,
                          TYPE_AT91_OTPC, AT91_OTPC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void at91_otpc_realize(DeviceState *dev, Error **errp)
{
    AT91OTPCState *s = AT91_OTPC(dev);

    if (!s->emulation_memory) {
        error_setg(errp, TYPE_AT91_OTPC
                   ": emulation-memory link is not set");
        return;
    }
    if (memory_region_size(s->emulation_memory) !=
        AT91_OTPC_EMULATION_MEMORY_SIZE) {
        error_setg(errp, TYPE_AT91_OTPC
                   ": emulation-memory must be exactly 4 KiB");
        return;
    }

    /*
     * The terminating root prevents FlatView from collapsing the full-size
     * alias back to the SRAM's address in its original container.
     */
    memory_region_init_io(&s->emulation_root, OBJECT(dev), NULL, NULL,
                          TYPE_AT91_OTPC "-emulation-root",
                          AT91_OTPC_EMULATION_MEMORY_SIZE);
    memory_region_init_alias(&s->emulation_alias, OBJECT(dev),
                             TYPE_AT91_OTPC "-emulation-memory",
                             s->emulation_memory, 0,
                             AT91_OTPC_EMULATION_MEMORY_SIZE);
    memory_region_add_subregion(&s->emulation_root, 0,
                                &s->emulation_alias);
    address_space_init(&s->emulation_as, &s->emulation_root,
                       TYPE_AT91_OTPC "-emulation");
}

static void at91_otpc_unrealize(DeviceState *dev)
{
    AT91OTPCState *s = AT91_OTPC(dev);

    address_space_destroy(&s->emulation_as);
    memory_region_del_subregion(&s->emulation_root, &s->emulation_alias);
    object_unparent(OBJECT(&s->emulation_alias));
    object_unparent(OBJECT(&s->emulation_root));
}

static int at91_otpc_post_load(void *opaque, int version_id)
{
    AT91OTPCState *s = opaque;

    s->otp_ever_programmed |= at91_otpc_otp_contains_one(s);
    if (s->otp_ever_programmed) {
        s->emulation_active = false;
    }
    s->sr = (s->sr & ~OTPC_SR_EMUL) |
            (s->emulation_active ? OTPC_SR_EMUL : 0);
    s->temporary_words = MIN(s->temporary_words,
                             (uint32_t)AT91_OTPC_TEMP_WORDS);
    s->mr &= OTPC_MR_MASK;
    s->ar &= OTPC_AR_MASK;
    s->sr &= OTPC_SR_MASK;
    s->imr &= OTPC_INT_MASK;
    s->isr &= OTPC_INT_MASK;
    s->hr &= OTPC_HR_MASK;
    s->car &= OTPC_CAR_MASK;
    s->lrmr &= OTPC_LRMR_MASK & 0x0000ffff;
    s->uhc[0] &= OTPC_UHC0R_MASK;
    s->uhc[1] &= OTPC_UHC1R_MASK;
    s->wpmr &= OTPC_WPMR_CONFIG_MASK;
    s->wpsr &= OTPC_WPSR_MASK;
    at91_otpc_update_irq(s);
    return 0;
}

static const VMStateDescription at91_otpc_vmstate = {
    .name = TYPE_AT91_OTPC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_otpc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(otp, AT91OTPCState, AT91_OTPC_OTP_WORDS),
        VMSTATE_UINT32_ARRAY(temporary, AT91OTPCState,
                             AT91_OTPC_TEMP_WORDS),
        VMSTATE_UINT32_ARRAY(uid, AT91OTPCState, 4),
        VMSTATE_UINT32(mr, AT91OTPCState),
        VMSTATE_UINT32(ar, AT91OTPCState),
        VMSTATE_UINT32(sr, AT91OTPCState),
        VMSTATE_UINT32(imr, AT91OTPCState),
        VMSTATE_UINT32(isr, AT91OTPCState),
        VMSTATE_UINT32(hr, AT91OTPCState),
        VMSTATE_UINT32(bar, AT91OTPCState),
        VMSTATE_UINT32(car, AT91OTPCState),
        VMSTATE_UINT32(lrmr, AT91OTPCState),
        VMSTATE_UINT32_ARRAY(uhc, AT91OTPCState, 2),
        VMSTATE_UINT32(wpmr, AT91OTPCState),
        VMSTATE_UINT32(wpsr, AT91OTPCState),
        VMSTATE_UINT32(temporary_words, AT91OTPCState),
        VMSTATE_BOOL(emulation_active, AT91OTPCState),
        VMSTATE_BOOL(otp_ever_programmed, AT91OTPCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_otpc_properties[] = {
    DEFINE_PROP_LINK("emulation-memory", AT91OTPCState, emulation_memory,
                     TYPE_MEMORY_REGION, MemoryRegion *),
    DEFINE_PROP_UINT32("uid0", AT91OTPCState, uid[0], 0),
    DEFINE_PROP_UINT32("uid1", AT91OTPCState, uid[1], 0),
    DEFINE_PROP_UINT32("uid2", AT91OTPCState, uid[2], 0),
    DEFINE_PROP_UINT32("uid3", AT91OTPCState, uid[3], 0),
};

static void at91_otpc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 OTP memory controller";
    dc->realize = at91_otpc_realize;
    dc->unrealize = at91_otpc_unrealize;
    dc->vmsd = &at91_otpc_vmstate;
    device_class_set_legacy_reset(dc, at91_otpc_reset);
    device_class_set_props(dc, at91_otpc_properties);
}

static const TypeInfo at91_otpc_info = {
    .name = TYPE_AT91_OTPC,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91OTPCState),
    .instance_init = at91_otpc_init,
    .class_init = at91_otpc_class_init,
};

static void at91_otpc_register_types(void)
{
    type_register_static(&at91_otpc_info);
}

type_init(at91_otpc_register_types)
