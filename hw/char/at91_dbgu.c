/*
 * Microchip AT91 Debug Unit (DBGU)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/char/at91_dbgu.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

enum {
    DBGU_CR       = 0x00,
    DBGU_MR       = 0x04,
    DBGU_IER      = 0x08,
    DBGU_IDR      = 0x0c,
    DBGU_IMR      = 0x10,
    DBGU_CSR      = 0x14,
    DBGU_RHR      = 0x18,
    DBGU_THR      = 0x1c,
    DBGU_BRGR     = 0x20,
    DBGU_RTOR     = 0x28,
    DBGU_CIDR     = 0x40,
    DBGU_EXID     = 0x44,
    DBGU_FNTR     = 0x48,
    DBGU_WPMR     = 0xe4,
    DBGU_ADDRSIZE = 0xec,
    DBGU_IPNAME1  = 0xf0,
    DBGU_IPNAME2  = 0xf4,
    DBGU_FEATURES = 0xf8,
    DBGU_VERSION  = 0xfc,
    DBGU_PDC_PTCR = 0x120,
    DBGU_PDC_PTSR = 0x124,
};

#define DBGU_MMIO_SIZE          0x200

#define DBGU_CR_RSTRX           BIT(2)
#define DBGU_CR_RSTTX           BIT(3)
#define DBGU_CR_RXEN            BIT(4)
#define DBGU_CR_RXDIS           BIT(5)
#define DBGU_CR_TXEN            BIT(6)
#define DBGU_CR_TXDIS           BIT(7)
#define DBGU_CR_RSTSTA          BIT(8)
#define DBGU_CR_RETTO           BIT(10)
#define DBGU_CR_STTTO           BIT(11)

#define DBGU_MR_FILTER          BIT(4)
#define DBGU_MR_PAR_MASK        (7U << 9)
#define DBGU_MR_BRSRCCK         BIT(12)
#define DBGU_MR_CHMODE_MASK     (3U << 14)
#define DBGU_MR_CHMODE_AUTO     (1U << 14)
#define DBGU_MR_CHMODE_LOCAL    (2U << 14)
#define DBGU_MR_CHMODE_REMOTE   (3U << 14)
#define DBGU_MR_MODE_MASK       (DBGU_MR_FILTER | DBGU_MR_PAR_MASK | \
                                 DBGU_MR_BRSRCCK | DBGU_MR_CHMODE_MASK)

#define DBGU_INT_RXRDY          BIT(0)
#define DBGU_INT_TXRDY          BIT(1)
#define DBGU_INT_OVRE           BIT(5)
#define DBGU_INT_FRAME          BIT(6)
#define DBGU_INT_PARE           BIT(7)
#define DBGU_INT_TIMEOUT        BIT(8)
#define DBGU_INT_TXEMPTY        BIT(9)
#define DBGU_INT_COMM_TX        BIT(30)
#define DBGU_INT_COMM_RX        BIT(31)

#define DBGU_INT_IMPLEMENTED    (DBGU_INT_RXRDY | DBGU_INT_TXRDY | \
                                 DBGU_INT_OVRE | DBGU_INT_FRAME | \
                                 DBGU_INT_PARE | DBGU_INT_TIMEOUT | \
                                 DBGU_INT_TXEMPTY)
#define DBGU_INT_VALID          (DBGU_INT_IMPLEMENTED | DBGU_INT_COMM_TX | \
                                 DBGU_INT_COMM_RX)
#define DBGU_ERROR_MASK         (DBGU_INT_OVRE | DBGU_INT_FRAME | \
                                 DBGU_INT_PARE)

#define DBGU_WPMR_KEY_MASK      0xffffff00
#define DBGU_WPMR_KEY           0x55415200
#define DBGU_WPMR_WPEN          BIT(0)

static Clock *at91_dbgu_baud_clock(AT91DBGUState *s)
{
    return s->mode & DBGU_MR_BRSRCCK ? s->gclk : s->pclk;
}

static bool at91_dbgu_clocked(AT91DBGUState *s)
{
    return clock_get_hz(s->pclk) &&
           clock_get_hz(at91_dbgu_baud_clock(s)) && s->baud_generator;
}

static void at91_dbgu_set_request(qemu_irq irq, bool *old_level,
                                  bool new_level)
{
    if (*old_level != new_level) {
        *old_level = new_level;
        qemu_set_irq(irq, new_level);
    }
}

static void at91_dbgu_update_requests(AT91DBGUState *s)
{
    bool active = at91_dbgu_clocked(s) &&
                  (s->mode & DBGU_MR_CHMODE_MASK) !=
                  DBGU_MR_CHMODE_REMOTE;
    bool tx = active && s->transmitter_enabled &&
              (s->status & DBGU_INT_TXRDY);
    bool rx = active && s->receiver_enabled &&
              (s->status & DBGU_INT_RXRDY);

    at91_dbgu_set_request(s->tx_request, &s->tx_request_level, tx);
    at91_dbgu_set_request(s->rx_request, &s->rx_request_level, rx);
}

static void at91_dbgu_update_irq(AT91DBGUState *s)
{
    qemu_set_irq(s->irq, (s->status & s->interrupt_mask) != 0);
}

static void at91_dbgu_schedule_timeout(AT91DBGUState *s)
{
    Clock *clock = at91_dbgu_baud_clock(s);
    uint64_t cycles;
    uint64_t duration;

    timer_del(s->timeout_timer);
    if (!s->timeout_running || s->timeout_waiting ||
        !s->receiver_enabled || !s->receiver_timeout ||
        !s->baud_generator || !clock_get_hz(s->pclk) ||
        !clock_get_hz(clock)) {
        return;
    }

    cycles = 16ULL * s->baud_generator * s->receiver_timeout;
    duration = MAX(clock_ticks_to_ns(clock, cycles), 1);
    timer_mod_ns(s->timeout_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + duration);
}

static void at91_dbgu_timeout(void *opaque)
{
    AT91DBGUState *s = opaque;

    s->timeout_running = false;
    s->status |= DBGU_INT_TIMEOUT;
    at91_dbgu_update_irq(s);
}

static void at91_dbgu_receive_byte(AT91DBGUState *s, uint8_t value)
{
    if ((s->mode & DBGU_MR_CHMODE_MASK) == DBGU_MR_CHMODE_REMOTE) {
        qemu_chr_fe_write_all(&s->chr, &value, 1);
        return;
    }

    if (s->status & DBGU_INT_RXRDY) {
        s->status |= DBGU_INT_OVRE;
    } else {
        s->receive_holding = value;
        s->status |= DBGU_INT_RXRDY;
    }

    if ((s->mode & DBGU_MR_CHMODE_MASK) == DBGU_MR_CHMODE_AUTO &&
        s->transmitter_enabled) {
        qemu_chr_fe_write_all(&s->chr, &value, 1);
    }

    if (s->receiver_timeout) {
        s->timeout_waiting = false;
        s->timeout_running = true;
        at91_dbgu_schedule_timeout(s);
    }
    at91_dbgu_update_irq(s);
    at91_dbgu_update_requests(s);
}

static int at91_dbgu_can_receive(void *opaque)
{
    AT91DBGUState *s = opaque;

    if (!at91_dbgu_clocked(s)) {
        return 0;
    }
    if ((s->mode & DBGU_MR_CHMODE_MASK) == DBGU_MR_CHMODE_REMOTE) {
        return 1;
    }
    return s->receiver_enabled && !(s->status & DBGU_INT_RXRDY);
}

static void at91_dbgu_receive(void *opaque, const uint8_t *buf, int size)
{
    AT91DBGUState *s = opaque;

    if (size) {
        at91_dbgu_receive_byte(s, buf[0]);
    }
}

static uint64_t at91_dbgu_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91DBGUState *s = opaque;
    uint32_t value = 0;

    switch (offset) {
    case DBGU_MR:
        value = s->mode;
        break;
    case DBGU_IMR:
        value = s->interrupt_mask;
        break;
    case DBGU_CSR:
        value = s->status;
        break;
    case DBGU_RHR:
        value = s->receive_holding;
        s->status &= ~DBGU_INT_RXRDY;
        at91_dbgu_update_irq(s);
        at91_dbgu_update_requests(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case DBGU_BRGR:
        value = s->baud_generator;
        break;
    case DBGU_RTOR:
        value = s->receiver_timeout;
        break;
    case DBGU_CIDR:
        value = s->chip_id;
        break;
    case DBGU_EXID:
        value = s->extension_id;
        break;
    case DBGU_FNTR:
        value = s->force_ntrst;
        break;
    case DBGU_WPMR:
        value = s->write_protection;
        break;
    case DBGU_ADDRSIZE:
    case DBGU_IPNAME1:
    case DBGU_IPNAME2:
    case DBGU_FEATURES:
    case DBGU_VERSION:
        /* The identification block is not consumed by current firmware. */
        break;
    case DBGU_PDC_PTSR:
        /*
         * SAM9X7 has XDMAC rather than the legacy PDC.  Linux accesses the
         * old PDC status register unconditionally around console writes;
         * the reserved location reads as zero on hardware.
         */
        break;
    case DBGU_CR:
    case DBGU_IER:
    case DBGU_IDR:
    case DBGU_THR:
        /* Write-only registers read as zero. */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_DBGU, offset);
        break;
    }

    return value;
}

static void at91_dbgu_write_control(AT91DBGUState *s, uint32_t value)
{
    if (value & DBGU_CR_RSTRX) {
        s->receive_holding = 0;
        s->status &= ~(DBGU_INT_RXRDY | DBGU_ERROR_MASK);
        s->receiver_enabled = false;
        s->timeout_running = false;
        s->timeout_waiting = false;
        timer_del(s->timeout_timer);
    }
    if (value & DBGU_CR_RSTTX) {
        s->transmitter_enabled = false;
        s->status &= ~(DBGU_INT_TXRDY | DBGU_INT_TXEMPTY);
    }
    if (value & DBGU_CR_RXDIS) {
        s->receiver_enabled = false;
        s->status &= ~DBGU_INT_RXRDY;
        s->timeout_running = false;
        timer_del(s->timeout_timer);
    } else if (value & DBGU_CR_RXEN) {
        s->receiver_enabled = true;
        at91_dbgu_schedule_timeout(s);
        qemu_chr_fe_accept_input(&s->chr);
    }
    if (value & DBGU_CR_TXDIS) {
        s->transmitter_enabled = false;
        s->status &= ~(DBGU_INT_TXRDY | DBGU_INT_TXEMPTY);
    } else if (value & DBGU_CR_TXEN) {
        s->transmitter_enabled = true;
        s->status |= DBGU_INT_TXRDY | DBGU_INT_TXEMPTY;
    }
    if (value & DBGU_CR_RSTSTA) {
        s->status &= ~DBGU_ERROR_MASK;
    }
    if (value & DBGU_CR_STTTO) {
        s->status &= ~DBGU_INT_TIMEOUT;
        s->timeout_running = false;
        s->timeout_waiting = true;
        timer_del(s->timeout_timer);
    }
    if (value & DBGU_CR_RETTO) {
        s->status &= ~DBGU_INT_TIMEOUT;
        s->timeout_running = true;
        s->timeout_waiting = false;
        at91_dbgu_schedule_timeout(s);
    }

    at91_dbgu_update_irq(s);
    at91_dbgu_update_requests(s);
}

static bool at91_dbgu_write_protected(AT91DBGUState *s, hwaddr offset)
{
    if (!(s->write_protection & DBGU_WPMR_WPEN)) {
        return false;
    }

    return offset == DBGU_MR || offset == DBGU_BRGR ||
           offset == DBGU_RTOR || offset == DBGU_FNTR;
}

static void at91_dbgu_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91DBGUState *s = opaque;
    uint32_t v = value;
    uint8_t ch;

    if (at91_dbgu_write_protected(s, offset)) {
        return;
    }

    switch (offset) {
    case DBGU_CR:
        at91_dbgu_write_control(s, v);
        break;
    case DBGU_MR:
        s->mode = v & DBGU_MR_MODE_MASK;
        at91_dbgu_schedule_timeout(s);
        at91_dbgu_update_requests(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case DBGU_IER:
        s->interrupt_mask |= v & DBGU_INT_VALID;
        at91_dbgu_update_irq(s);
        break;
    case DBGU_IDR:
        s->interrupt_mask &= ~(v & DBGU_INT_VALID);
        at91_dbgu_update_irq(s);
        break;
    case DBGU_THR:
        if (!s->transmitter_enabled || !at91_dbgu_clocked(s) ||
            (s->mode & DBGU_MR_CHMODE_MASK) == DBGU_MR_CHMODE_REMOTE) {
            break;
        }

        ch = v;
        if ((s->mode & DBGU_MR_CHMODE_MASK) == DBGU_MR_CHMODE_LOCAL &&
            s->receiver_enabled) {
            at91_dbgu_receive_byte(s, ch);
        } else {
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
        }
        break;
    case DBGU_BRGR:
        s->baud_generator = v & 0xffff;
        at91_dbgu_schedule_timeout(s);
        at91_dbgu_update_requests(s);
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case DBGU_RTOR:
        s->receiver_timeout = v & 0xff;
        if (!s->receiver_timeout) {
            s->status &= ~DBGU_INT_TIMEOUT;
            s->timeout_running = false;
            s->timeout_waiting = false;
            timer_del(s->timeout_timer);
            at91_dbgu_update_irq(s);
        } else if (!s->timeout_waiting) {
            s->timeout_running = true;
            at91_dbgu_schedule_timeout(s);
        }
        break;
    case DBGU_FNTR:
        s->force_ntrst = v & 1;
        break;
    case DBGU_WPMR:
        if ((v & DBGU_WPMR_KEY_MASK) == DBGU_WPMR_KEY) {
            s->write_protection = v & DBGU_WPMR_WPEN;
        }
        break;
    case DBGU_PDC_PTCR:
        /* Reserved legacy PDC control location; SAM9X7 uses XDMAC. */
        break;
    case DBGU_IMR:
    case DBGU_CSR:
    case DBGU_RHR:
    case DBGU_CIDR:
    case DBGU_EXID:
    case DBGU_ADDRSIZE:
    case DBGU_IPNAME1:
    case DBGU_IPNAME2:
    case DBGU_FEATURES:
    case DBGU_VERSION:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_DBGU, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_DBGU, offset);
        break;
    }
}

static const MemoryRegionOps at91_dbgu_ops = {
    .read = at91_dbgu_read,
    .write = at91_dbgu_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_dbgu_clock_changed(void *opaque, ClockEvent event)
{
    AT91DBGUState *s = opaque;

    at91_dbgu_schedule_timeout(s);
    at91_dbgu_update_requests(s);
    if (at91_dbgu_clocked(s)) {
        qemu_chr_fe_accept_input(&s->chr);
    }
}

static void at91_dbgu_reset(DeviceState *dev)
{
    AT91DBGUState *s = AT91_DBGU(dev);

    timer_del(s->timeout_timer);
    s->mode = 0;
    s->interrupt_mask = 0;
    s->status = DBGU_INT_TXRDY | DBGU_INT_TXEMPTY;
    s->receive_holding = 0;
    s->baud_generator = 0;
    s->receiver_timeout = 0;
    s->force_ntrst = 0;
    s->write_protection = 0;
    s->receiver_enabled = false;
    s->transmitter_enabled = false;
    s->timeout_running = false;
    s->timeout_waiting = false;
    s->tx_request_level = false;
    s->rx_request_level = false;
    qemu_set_irq(s->tx_request, 0);
    qemu_set_irq(s->rx_request, 0);
    at91_dbgu_update_irq(s);
}

static int at91_dbgu_post_load(void *opaque, int version_id)
{
    AT91DBGUState *s = opaque;

    s->mode &= DBGU_MR_MODE_MASK;
    s->interrupt_mask &= DBGU_INT_VALID;
    s->status &= DBGU_INT_VALID;
    s->baud_generator &= 0xffff;
    s->receiver_timeout &= 0xff;
    s->force_ntrst &= 1;
    s->write_protection &= DBGU_WPMR_WPEN;
    s->tx_request_level = false;
    s->rx_request_level = false;
    at91_dbgu_update_irq(s);
    at91_dbgu_update_requests(s);
    at91_dbgu_schedule_timeout(s);
    return 0;
}

static const VMStateDescription vmstate_at91_dbgu = {
    .name = TYPE_AT91_DBGU,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_dbgu_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mode, AT91DBGUState),
        VMSTATE_UINT32(interrupt_mask, AT91DBGUState),
        VMSTATE_UINT32(status, AT91DBGUState),
        VMSTATE_UINT32(receive_holding, AT91DBGUState),
        VMSTATE_UINT32(baud_generator, AT91DBGUState),
        VMSTATE_UINT32(receiver_timeout, AT91DBGUState),
        VMSTATE_UINT32(force_ntrst, AT91DBGUState),
        VMSTATE_UINT32(write_protection, AT91DBGUState),
        VMSTATE_BOOL(receiver_enabled, AT91DBGUState),
        VMSTATE_BOOL(transmitter_enabled, AT91DBGUState),
        VMSTATE_BOOL(timeout_running, AT91DBGUState),
        VMSTATE_BOOL(timeout_waiting, AT91DBGUState),
        VMSTATE_TIMER_PTR(timeout_timer, AT91DBGUState),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_dbgu_realize(DeviceState *dev, Error **errp)
{
    AT91DBGUState *s = AT91_DBGU(dev);

    if (!clock_has_source(s->pclk) || !clock_has_source(s->gclk)) {
        error_setg(errp, TYPE_AT91_DBGU
                   ": pclk and gclk must be connected");
        return;
    }
    qemu_chr_fe_set_handlers(&s->chr, at91_dbgu_can_receive,
                             at91_dbgu_receive, NULL, NULL, s, NULL, true);
}

static void at91_dbgu_init(Object *obj)
{
    AT91DBGUState *s = AT91_DBGU(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_dbgu_ops, s,
                          TYPE_AT91_DBGU, DBGU_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    s->pclk = qdev_init_clock_in(dev, "pclk", at91_dbgu_clock_changed,
                                 s, ClockUpdate);
    s->gclk = qdev_init_clock_in(dev, "gclk", at91_dbgu_clock_changed,
                                 s, ClockUpdate);
    s->timeout_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                    at91_dbgu_timeout, s);
}

static void at91_dbgu_finalize(Object *obj)
{
    AT91DBGUState *s = AT91_DBGU(obj);

    timer_free(s->timeout_timer);
}

static const Property at91_dbgu_properties[] = {
    DEFINE_PROP_CHR("chardev", AT91DBGUState, chr),
    DEFINE_PROP_UINT32("chip-id", AT91DBGUState, chip_id, 0),
    DEFINE_PROP_UINT32("extension-id", AT91DBGUState, extension_id, 0),
};

static void at91_dbgu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Debug Unit";
    dc->realize = at91_dbgu_realize;
    dc->vmsd = &vmstate_at91_dbgu;
    device_class_set_legacy_reset(dc, at91_dbgu_reset);
    device_class_set_props(dc, at91_dbgu_properties);
}

static const TypeInfo at91_dbgu_info = {
    .name = TYPE_AT91_DBGU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91DBGUState),
    .instance_init = at91_dbgu_init,
    .instance_finalize = at91_dbgu_finalize,
    .class_init = at91_dbgu_class_init,
};

static void at91_dbgu_register_types(void)
{
    type_register_static(&at91_dbgu_info);
}

type_init(at91_dbgu_register_types)
