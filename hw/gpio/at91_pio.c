/*
 * Microchip AT91 PIO3 parallel I/O controller
 *
 * This is the PIO implementation used by SAM9X7.  Besides the register
 * interface, the model represents pull resistors, open-drain outputs,
 * clock gating, input filters and all five interrupt detection modes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-clock.h"
#include "hw/core/qdev-properties.h"
#include "hw/gpio/at91_pio.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

#define PIO_PER         0x000
#define PIO_PDR         0x004
#define PIO_PSR         0x008
#define PIO_OER         0x010
#define PIO_ODR         0x014
#define PIO_OSR         0x018
#define PIO_IFER        0x020
#define PIO_IFDR        0x024
#define PIO_IFSR        0x028
#define PIO_SODR        0x030
#define PIO_CODR        0x034
#define PIO_ODSR        0x038
#define PIO_PDSR        0x03c
#define PIO_IER         0x040
#define PIO_IDR         0x044
#define PIO_IMR         0x048
#define PIO_ISR         0x04c
#define PIO_MDER        0x050
#define PIO_MDDR        0x054
#define PIO_MDSR        0x058
#define PIO_PUDR        0x060
#define PIO_PUER        0x064
#define PIO_PUSR        0x068
#define PIO_ABCDSR0     0x070
#define PIO_ABCDSR1     0x074
#define PIO_IFSCDR      0x080
#define PIO_IFSCER      0x084
#define PIO_IFSCSR      0x088
#define PIO_SCDR        0x08c
#define PIO_PPDDR       0x090
#define PIO_PPDER       0x094
#define PIO_PPDSR       0x098
#define PIO_OWER        0x0a0
#define PIO_OWDR        0x0a4
#define PIO_OWSR        0x0a8
#define PIO_AIMER       0x0b0
#define PIO_AIMDR       0x0b4
#define PIO_AIMMR       0x0b8
#define PIO_ESR         0x0c0
#define PIO_LSR         0x0c4
#define PIO_ELSR        0x0c8
#define PIO_FELLSR      0x0d0
#define PIO_REHLSR      0x0d4
#define PIO_FRLHSR      0x0d8
#define PIO_WPMR        0x0e4
#define PIO_WPSR        0x0e8
#define PIO_SCHMITT     0x100
#define PIO_SLEWR       0x110
#define PIO_DRIVER      0x118

#define PIO_MMIO_SIZE   0x200

#define PIO_SCDR_DIV_MASK       0x00003fff

#define PIO_WPMR_WPEN           BIT(0)
#define PIO_WPMR_KEY_MASK       0xffffff00
#define PIO_WPMR_KEY            0x50494f00

#define PIO_WPSR_WPVS           BIT(0)

static uint32_t at91_pio_level_events(AT91PIOState *s)
{
    uint32_t levels = s->additional_mode & s->edge_mode & s->valid_mask;

    return levels & ((s->sampled_level & s->rise_high_mode) |
                     (~s->sampled_level & ~s->rise_high_mode));
}

static void at91_pio_update_irq(AT91PIOState *s)
{
    uint32_t pending = (s->isr | at91_pio_level_events(s)) & s->imr;

    /* The PIO interrupt output is suppressed when its peripheral clock is. */
    qemu_set_irq(s->irq, clock_get_hz(s->pclk) && pending);
}

static uint32_t at91_pio_output_connected(AT91PIOState *s)
{
    uint32_t enabled = s->pio_status & s->output_status & s->valid_mask;

    /* A high multi-drive output is electrically released. */
    return enabled & ~(s->multidrive_status & s->output_data);
}

static void at91_pio_update_outputs(AT91PIOState *s)
{
    uint32_t connected = at91_pio_output_connected(s);
    uint32_t levels = s->output_data & connected;
    unsigned int pin;

    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        uint32_t mask = BIT(pin);
        bool new_connected = connected & mask;
        bool old_connected = s->old_output_connected & mask;
        bool new_level = levels & mask;
        bool old_level = s->old_output_level & mask;

        if (new_connected == old_connected &&
            (!new_connected || new_level == old_level)) {
            continue;
        }

        s->old_output_connected = deposit32(s->old_output_connected,
                                             pin, 1, new_connected);
        s->old_output_level = deposit32(s->old_output_level,
                                        pin, 1, new_level);
        qemu_set_irq(s->output[pin], new_connected ? new_level : -1);
    }
}

static uint32_t at91_pio_raw_level(AT91PIOState *s)
{
    uint32_t connected = at91_pio_output_connected(s);
    uint32_t floating = s->valid_mask & ~connected;
    uint32_t externally_driven = floating & s->external_mask;
    uint32_t undriven = floating & ~s->external_mask;
    uint32_t level;

    level = connected & s->output_data;
    level |= externally_driven & s->external_level;
    level |= undriven & s->pullup_enable;
    return level & s->valid_mask;
}

static void at91_pio_cancel_filter(AT91PIOState *s, unsigned int pin)
{
    uint32_t mask = BIT(pin);

    if (s->filter[pin].timer) {
        timer_del(s->filter[pin].timer);
    }
    s->filter_pending &= ~mask;
    s->filter_deadline[pin] = -1;
}

static void at91_pio_accept_pin(AT91PIOState *s, unsigned int pin)
{
    uint32_t mask = BIT(pin);
    bool old_level = s->sampled_level & mask;
    bool new_level = s->raw_level & mask;
    uint32_t edge_modes;
    uint32_t event = 0;

    if (old_level == new_level) {
        return;
    }

    s->sampled_level = deposit32(s->sampled_level, pin, 1, new_level);

    /* Default mode detects both edges. */
    event |= mask & ~s->additional_mode;

    /* Additional edge mode detects one selected polarity. */
    edge_modes = s->additional_mode & ~s->edge_mode;
    if ((new_level && (s->rise_high_mode & mask)) ||
        (!new_level && !(s->rise_high_mode & mask))) {
        event |= mask & edge_modes;
    }

    s->isr |= event & s->valid_mask;
    at91_pio_update_irq(s);
}

static uint64_t at91_pio_filter_delay_ns(AT91PIOState *s, unsigned int pin)
{
    uint64_t hz;
    uint64_t ticks = 1;

    if (s->slow_filter_status & BIT(pin)) {
        hz = clock_get_hz(s->slck);
        ticks = 2 * ((s->slow_clock_divider & PIO_SCDR_DIV_MASK) + 1);
    } else {
        hz = clock_get_hz(s->pclk);
    }

    if (!hz) {
        return 0;
    }

    return MAX(1, DIV_ROUND_UP(ticks * NANOSECONDS_PER_SECOND, hz));
}

static void at91_pio_schedule_filter(AT91PIOState *s, unsigned int pin)
{
    uint64_t delay = at91_pio_filter_delay_ns(s, pin);
    int64_t now;

    at91_pio_cancel_filter(s, pin);
    if (!delay) {
        return;
    }

    now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->filter_deadline[pin] = now + delay;
    s->filter_pending |= BIT(pin);
    timer_mod(s->filter[pin].timer, s->filter_deadline[pin]);
}

static void at91_pio_process_changes(AT91PIOState *s, uint32_t pins)
{
    unsigned int pin;

    pins &= s->valid_mask;
    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        uint32_t mask = BIT(pin);

        if (!(pins & mask)) {
            continue;
        }

        if ((s->raw_level & mask) == (s->sampled_level & mask)) {
            at91_pio_cancel_filter(s, pin);
        } else if (!clock_get_hz(s->pclk)) {
            at91_pio_cancel_filter(s, pin);
        } else if (s->filter_status & mask) {
            at91_pio_schedule_filter(s, pin);
        } else {
            at91_pio_cancel_filter(s, pin);
            at91_pio_accept_pin(s, pin);
        }
    }
}

static void at91_pio_refresh_pins(AT91PIOState *s)
{
    uint32_t old_raw = s->raw_level;

    at91_pio_update_outputs(s);
    s->raw_level = at91_pio_raw_level(s);
    at91_pio_process_changes(s, old_raw ^ s->raw_level);
}

static void at91_pio_filter_expire(void *opaque)
{
    AT91PIOFilterTimer *filter = opaque;
    AT91PIOState *s = filter->pio;
    unsigned int pin = filter->pin;
    uint32_t mask = BIT(pin);

    s->filter_pending &= ~mask;
    s->filter_deadline[pin] = -1;

    if (clock_get_hz(s->pclk) && (s->filter_status & mask) &&
        ((s->raw_level & mask) != (s->sampled_level & mask))) {
        at91_pio_accept_pin(s, pin);
    }
}

static bool at91_pio_write_protected(AT91PIOState *s, hwaddr offset)
{
    bool protected;

    switch (offset) {
    case PIO_PER:
    case PIO_PDR:
    case PIO_OER:
    case PIO_ODR:
    case PIO_IFER:
    case PIO_IFDR:
    case PIO_MDER:
    case PIO_MDDR:
    case PIO_PUDR:
    case PIO_PUER:
    case PIO_ABCDSR0:
    case PIO_ABCDSR1:
    case PIO_PPDDR:
    case PIO_PPDER:
    case PIO_OWER:
    case PIO_OWDR:
        protected = s->wpmr & PIO_WPMR_WPEN;
        break;
    default:
        protected = false;
        break;
    }

    if (protected) {
        s->wpsr = PIO_WPSR_WPVS | ((offset & 0xffff) << 8);
    }
    return protected;
}

static uint64_t at91_pio_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91PIOState *s = AT91_PIO(opaque);
    uint32_t value;

    switch (offset) {
    case PIO_PER:
    case PIO_PDR:
    case PIO_OER:
    case PIO_ODR:
    case PIO_IFER:
    case PIO_IFDR:
    case PIO_SODR:
    case PIO_CODR:
    case PIO_IER:
    case PIO_IDR:
    case PIO_MDER:
    case PIO_MDDR:
    case PIO_PUDR:
    case PIO_PUER:
    case PIO_IFSCDR:
    case PIO_IFSCER:
    case PIO_PPDDR:
    case PIO_PPDER:
    case PIO_OWER:
    case PIO_OWDR:
    case PIO_AIMER:
    case PIO_AIMDR:
    case PIO_ESR:
    case PIO_LSR:
    case PIO_FELLSR:
    case PIO_REHLSR:
        return 0;
    case PIO_PSR:
        return s->pio_status;
    case PIO_OSR:
        return s->output_status;
    case PIO_IFSR:
        return s->filter_status;
    case PIO_ODSR:
        return s->output_data;
    case PIO_PDSR:
        return s->sampled_level;
    case PIO_IMR:
        return s->imr;
    case PIO_ISR:
        value = (s->isr | at91_pio_level_events(s)) & s->valid_mask;
        s->isr = 0;
        at91_pio_update_irq(s);
        return value;
    case PIO_MDSR:
        return s->multidrive_status;
    case PIO_PUSR:
        return ~s->pullup_enable & s->valid_mask;
    case PIO_ABCDSR0:
        return s->abcdsr[0];
    case PIO_ABCDSR1:
        return s->abcdsr[1];
    case PIO_IFSCSR:
        return s->slow_filter_status;
    case PIO_SCDR:
        return s->slow_clock_divider;
    case PIO_PPDSR:
        return ~s->pulldown_enable & s->valid_mask;
    case PIO_OWSR:
        return s->output_write_status;
    case PIO_AIMMR:
        return s->additional_mode;
    case PIO_ELSR:
        return s->edge_mode;
    case PIO_FRLHSR:
        return s->rise_high_mode;
    case PIO_WPMR:
        return s->wpmr;
    case PIO_WPSR:
        value = s->wpsr;
        s->wpsr = 0;
        return value;
    case PIO_SCHMITT:
        return s->schmitt;
    case PIO_SLEWR:
        return s->slew_rate;
    case PIO_DRIVER:
        return s->driver;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIO ": read from bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        return 0;
    }
}

static void at91_pio_reconfigure_filters(AT91PIOState *s, uint32_t pins)
{
    unsigned int pin;

    pins &= s->valid_mask;
    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        if (pins & BIT(pin)) {
            at91_pio_cancel_filter(s, pin);
        }
    }
    at91_pio_process_changes(s, pins & (s->raw_level ^ s->sampled_level));
}

static void at91_pio_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91PIOState *s = AT91_PIO(opaque);
    uint32_t pins = value & s->valid_mask;

    if (offset != PIO_WPMR && at91_pio_write_protected(s, offset)) {
        return;
    }

    switch (offset) {
    case PIO_PER:
        s->pio_status |= pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_PDR:
        s->pio_status &= ~pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_OER:
        s->output_status |= pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_ODR:
        s->output_status &= ~pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_IFER:
        s->filter_status |= pins;
        at91_pio_reconfigure_filters(s, pins);
        break;
    case PIO_IFDR:
        s->filter_status &= ~pins;
        at91_pio_reconfigure_filters(s, pins);
        break;
    case PIO_SODR:
        s->output_data |= pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_CODR:
        s->output_data &= ~pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_ODSR:
        s->output_data = (s->output_data & ~s->output_write_status) |
                         (pins & s->output_write_status);
        at91_pio_refresh_pins(s);
        break;
    case PIO_IER:
        s->imr |= pins;
        at91_pio_update_irq(s);
        break;
    case PIO_IDR:
        s->imr &= ~pins;
        at91_pio_update_irq(s);
        break;
    case PIO_MDER:
        s->multidrive_status |= pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_MDDR:
        s->multidrive_status &= ~pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_PUDR:
        s->pullup_enable &= ~pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_PUER:
        s->pullup_enable |= pins & ~s->pulldown_enable;
        at91_pio_refresh_pins(s);
        break;
    case PIO_ABCDSR0:
        s->abcdsr[0] = pins;
        break;
    case PIO_ABCDSR1:
        s->abcdsr[1] = pins;
        break;
    case PIO_IFSCDR:
        s->slow_filter_status &= ~pins;
        at91_pio_reconfigure_filters(s, pins & s->filter_status);
        break;
    case PIO_IFSCER:
        s->slow_filter_status |= pins;
        at91_pio_reconfigure_filters(s, pins & s->filter_status);
        break;
    case PIO_SCDR:
        s->slow_clock_divider = value & PIO_SCDR_DIV_MASK;
        at91_pio_reconfigure_filters(s, s->filter_status &
                                     s->slow_filter_status);
        break;
    case PIO_PPDDR:
        s->pulldown_enable &= ~pins;
        at91_pio_refresh_pins(s);
        break;
    case PIO_PPDER:
        s->pulldown_enable |= pins & ~s->pullup_enable;
        at91_pio_refresh_pins(s);
        break;
    case PIO_OWER:
        s->output_write_status |= pins;
        break;
    case PIO_OWDR:
        s->output_write_status &= ~pins;
        break;
    case PIO_AIMER:
        s->additional_mode |= pins;
        at91_pio_update_irq(s);
        break;
    case PIO_AIMDR:
        s->additional_mode &= ~pins;
        at91_pio_update_irq(s);
        break;
    case PIO_ESR:
        s->edge_mode &= ~pins;
        at91_pio_update_irq(s);
        break;
    case PIO_LSR:
        s->edge_mode |= pins;
        at91_pio_update_irq(s);
        break;
    case PIO_FELLSR:
        s->rise_high_mode &= ~pins;
        at91_pio_update_irq(s);
        break;
    case PIO_REHLSR:
        s->rise_high_mode |= pins;
        at91_pio_update_irq(s);
        break;
    case PIO_WPMR:
        if ((value & PIO_WPMR_KEY_MASK) == PIO_WPMR_KEY) {
            s->wpmr = value & PIO_WPMR_WPEN;
        }
        break;
    case PIO_SCHMITT:
        s->schmitt = pins;
        break;
    case PIO_SLEWR:
        s->slew_rate = pins;
        break;
    case PIO_DRIVER:
        s->driver = pins;
        break;
    case PIO_PSR:
    case PIO_OSR:
    case PIO_IFSR:
    case PIO_PDSR:
    case PIO_IMR:
    case PIO_ISR:
    case PIO_MDSR:
    case PIO_PUSR:
    case PIO_IFSCSR:
    case PIO_PPDSR:
    case PIO_OWSR:
    case PIO_AIMMR:
    case PIO_ELSR:
    case PIO_FRLHSR:
    case PIO_WPSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIO ": write to read-only offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_PIO ": write to bad offset 0x%"
                      HWADDR_PRIx "\n", offset);
        break;
    }
}

static const MemoryRegionOps at91_pio_ops = {
    .read = at91_pio_read,
    .write = at91_pio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_pio_set_input(void *opaque, int pin, int level)
{
    AT91PIOState *s = AT91_PIO(opaque);

    if (!(s->valid_mask & BIT(pin))) {
        return;
    }

    s->external_mask = deposit32(s->external_mask, pin, 1, level >= 0);
    if (level >= 0) {
        s->external_level = deposit32(s->external_level, pin, 1,
                                      level != 0);
    }
    at91_pio_refresh_pins(s);
}

static void at91_pio_clock_changed(void *opaque, ClockEvent event)
{
    AT91PIOState *s = AT91_PIO(opaque);
    unsigned int pin;

    if (!s->filter[0].timer) {
        return;
    }

    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        at91_pio_cancel_filter(s, pin);
    }

    if (clock_get_hz(s->pclk)) {
        at91_pio_process_changes(s, s->raw_level ^ s->sampled_level);
    }
    at91_pio_update_irq(s);
}

static void at91_pio_reset(DeviceState *dev)
{
    AT91PIOState *s = AT91_PIO(dev);
    unsigned int pin;

    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        at91_pio_cancel_filter(s, pin);
    }

    s->pio_status = s->reset_pio_mask & s->valid_mask;
    s->output_status = 0;
    s->filter_status = 0;
    s->output_data = 0;
    s->imr = 0;
    s->isr = 0;
    s->multidrive_status = 0;
    s->pullup_enable = s->reset_pullup_mask & s->valid_mask;
    s->abcdsr[0] = 0;
    s->abcdsr[1] = 0;
    s->slow_filter_status = 0;
    s->slow_clock_divider = 0;
    s->pulldown_enable = s->reset_pulldown_mask & s->valid_mask;
    s->output_write_status = 0;
    s->additional_mode = 0;
    s->edge_mode = 0;
    s->rise_high_mode = 0;
    s->wpmr = 0;
    s->wpsr = 0;
    s->schmitt = 0;
    s->slew_rate = 0;
    s->driver = 0;
    s->filter_pending = 0;

    s->old_output_connected = ~at91_pio_output_connected(s);
    s->old_output_level = ~s->output_data;
    at91_pio_update_outputs(s);
    s->raw_level = at91_pio_raw_level(s);
    s->sampled_level = s->raw_level;
    at91_pio_update_irq(s);
}

static void at91_pio_init(Object *obj)
{
    AT91PIOState *s = AT91_PIO(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_pio_ops, s,
                          TYPE_AT91_PIO, PIO_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    qdev_init_gpio_in(DEVICE(s), at91_pio_set_input, AT91_PIO_NUM_PINS);
    qdev_init_gpio_out(DEVICE(s), s->output, AT91_PIO_NUM_PINS);

    s->pclk = qdev_init_clock_in(DEVICE(s), "pclk",
                                 at91_pio_clock_changed, s, ClockUpdate);
    s->slck = qdev_init_clock_in(DEVICE(s), "slck",
                                 at91_pio_clock_changed, s, ClockUpdate);
}

static void at91_pio_realize(DeviceState *dev, Error **errp)
{
    AT91PIOState *s = AT91_PIO(dev);
    unsigned int pin;

    if (!clock_has_source(s->pclk) || !clock_has_source(s->slck)) {
        error_setg(errp, TYPE_AT91_PIO
                   ": pclk and slck clocks must be connected");
        return;
    }
    if ((s->reset_pio_mask | s->reset_pullup_mask |
         s->reset_pulldown_mask) & ~s->valid_mask) {
        error_setg(errp, TYPE_AT91_PIO
                   ": reset masks contain package-invalid pins");
        return;
    }
    if (s->reset_pullup_mask & s->reset_pulldown_mask) {
        error_setg(errp, TYPE_AT91_PIO
                   ": a reset pin cannot have both pull resistors enabled");
        return;
    }

    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        s->filter[pin].pio = s;
        s->filter[pin].pin = pin;
        s->filter[pin].timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                            at91_pio_filter_expire,
                                            &s->filter[pin]);
        s->filter_deadline[pin] = -1;
    }
}

static void at91_pio_finalize(Object *obj)
{
    AT91PIOState *s = AT91_PIO(obj);
    unsigned int pin;

    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        if (s->filter[pin].timer) {
            timer_free(s->filter[pin].timer);
        }
    }
}

static int at91_pio_post_load(void *opaque, int version_id)
{
    AT91PIOState *s = AT91_PIO(opaque);
    uint32_t pending = s->filter_pending;
    unsigned int pin;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    s->filter_pending = 0;
    for (pin = 0; pin < AT91_PIO_NUM_PINS; pin++) {
        timer_del(s->filter[pin].timer);
        if (pending & BIT(pin)) {
            s->filter_pending |= BIT(pin);
            timer_mod(s->filter[pin].timer,
                      MAX(now, s->filter_deadline[pin]));
        }
    }

    s->old_output_connected = ~at91_pio_output_connected(s);
    s->old_output_level = ~s->output_data;
    at91_pio_update_outputs(s);
    at91_pio_update_irq(s);
    return 0;
}

static const VMStateDescription at91_pio_vmstate = {
    .name = TYPE_AT91_PIO,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = at91_pio_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_CLOCK(pclk, AT91PIOState),
        VMSTATE_CLOCK(slck, AT91PIOState),
        VMSTATE_UINT32(pio_status, AT91PIOState),
        VMSTATE_UINT32(output_status, AT91PIOState),
        VMSTATE_UINT32(filter_status, AT91PIOState),
        VMSTATE_UINT32(output_data, AT91PIOState),
        VMSTATE_UINT32(imr, AT91PIOState),
        VMSTATE_UINT32(isr, AT91PIOState),
        VMSTATE_UINT32(multidrive_status, AT91PIOState),
        VMSTATE_UINT32(pullup_enable, AT91PIOState),
        VMSTATE_UINT32_ARRAY(abcdsr, AT91PIOState, 2),
        VMSTATE_UINT32(slow_filter_status, AT91PIOState),
        VMSTATE_UINT32(slow_clock_divider, AT91PIOState),
        VMSTATE_UINT32(pulldown_enable, AT91PIOState),
        VMSTATE_UINT32(output_write_status, AT91PIOState),
        VMSTATE_UINT32(additional_mode, AT91PIOState),
        VMSTATE_UINT32(edge_mode, AT91PIOState),
        VMSTATE_UINT32(rise_high_mode, AT91PIOState),
        VMSTATE_UINT32(wpmr, AT91PIOState),
        VMSTATE_UINT32(wpsr, AT91PIOState),
        VMSTATE_UINT32(schmitt, AT91PIOState),
        VMSTATE_UINT32(slew_rate, AT91PIOState),
        VMSTATE_UINT32(driver, AT91PIOState),
        VMSTATE_UINT32(external_level, AT91PIOState),
        VMSTATE_UINT32(external_mask, AT91PIOState),
        VMSTATE_UINT32(raw_level, AT91PIOState),
        VMSTATE_UINT32(sampled_level, AT91PIOState),
        VMSTATE_UINT32(filter_pending, AT91PIOState),
        VMSTATE_INT64_ARRAY(filter_deadline, AT91PIOState,
                            AT91_PIO_NUM_PINS),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_pio_properties[] = {
    DEFINE_PROP_UINT32("valid-mask", AT91PIOState, valid_mask, UINT32_MAX),
    DEFINE_PROP_UINT32("reset-pio-mask", AT91PIOState, reset_pio_mask,
                       UINT32_MAX),
    DEFINE_PROP_UINT32("reset-pullup-mask", AT91PIOState,
                       reset_pullup_mask, 0),
    DEFINE_PROP_UINT32("reset-pulldown-mask", AT91PIOState,
                       reset_pulldown_mask, 0),
};

static void at91_pio_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 PIO3 parallel I/O controller";
    dc->realize = at91_pio_realize;
    dc->vmsd = &at91_pio_vmstate;
    device_class_set_legacy_reset(dc, at91_pio_reset);
    device_class_set_props(dc, at91_pio_properties);
}

static const TypeInfo at91_pio_info = {
    .name = TYPE_AT91_PIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91PIOState),
    .instance_init = at91_pio_init,
    .instance_finalize = at91_pio_finalize,
    .class_init = at91_pio_class_init,
};

static void at91_pio_register_types(void)
{
    type_register_static(&at91_pio_info);
}

type_init(at91_pio_register_types)
