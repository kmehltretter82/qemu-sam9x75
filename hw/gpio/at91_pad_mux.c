/*
 * Microchip AT91 shared pad multiplexer
 *
 * A single AT91 pad can be driven either by its peripheral function or by the
 * PIO controller, selected by the PIO's PER/PDR state.  Both sources exist as
 * separate QEMU GPIO lines, so a board that routes such a pad to an off-chip
 * consumer needs one net that reflects whichever source currently owns it.
 *
 * The PIO reports a released pad with a negative level, so this device gives
 * the PIO priority whenever it drives and otherwise falls back to the
 * peripheral function.  When neither source drives, the board pull resistor
 * defines the level.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/gpio/at91_pad_mux.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"

static void at91_pad_mux_update(AT91PadMuxState *s)
{
    bool level;

    if (s->pio_driving) {
        level = s->pio_level;
    } else {
        level = s->peripheral_level;
    }

    if (s->level_valid && level == s->level) {
        return;
    }
    s->level = level;
    s->level_valid = true;
    qemu_set_irq(s->out, level);
}

static void at91_pad_mux_set_peripheral(void *opaque, int n, int level)
{
    AT91PadMuxState *s = AT91_PAD_MUX(opaque);

    /* A released peripheral output leaves the pad to its pull resistor. */
    s->peripheral_level = level < 0 ? s->pullup : level != 0;
    at91_pad_mux_update(s);
}

static void at91_pad_mux_set_pio(void *opaque, int n, int level)
{
    AT91PadMuxState *s = AT91_PAD_MUX(opaque);

    s->pio_driving = level >= 0;
    if (s->pio_driving) {
        s->pio_level = level != 0;
    }
    at91_pad_mux_update(s);
}

static void at91_pad_mux_init(Object *obj)
{
    AT91PadMuxState *s = AT91_PAD_MUX(obj);
    DeviceState *dev = DEVICE(obj);

    qdev_init_gpio_in_named(dev, at91_pad_mux_set_peripheral,
                            AT91_PAD_MUX_PERIPHERAL, 1);
    qdev_init_gpio_in_named(dev, at91_pad_mux_set_pio, AT91_PAD_MUX_PIO, 1);
    qdev_init_gpio_out(dev, &s->out, 1);
}

static void at91_pad_mux_reset_hold(Object *obj, ResetType type)
{
    AT91PadMuxState *s = AT91_PAD_MUX(obj);

    /*
     * Neither source has reported a level yet, so the pad rests at its pull
     * resistor.  Both the PIO and the peripheral re-report their outputs
     * during their own reset.
     */
    s->pio_driving = false;
    s->pio_level = s->pullup;
    s->peripheral_level = s->pullup;
    s->level_valid = false;
    at91_pad_mux_update(s);
}

static const VMStateDescription vmstate_at91_pad_mux = {
    .name = "at91-pad-mux",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(peripheral_level, AT91PadMuxState),
        VMSTATE_BOOL(pio_driving, AT91PadMuxState),
        VMSTATE_BOOL(pio_level, AT91PadMuxState),
        VMSTATE_BOOL(level, AT91PadMuxState),
        VMSTATE_BOOL(level_valid, AT91PadMuxState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_pad_mux_properties[] = {
    DEFINE_PROP_BOOL("pullup", AT91PadMuxState, pullup, true),
};

static void at91_pad_mux_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "AT91 shared pad multiplexer";
    dc->vmsd = &vmstate_at91_pad_mux;
    device_class_set_props(dc, at91_pad_mux_properties);
    rc->phases.hold = at91_pad_mux_reset_hold;
}

static const TypeInfo at91_pad_mux_types[] = {
    {
        .name          = TYPE_AT91_PAD_MUX,
        .parent        = TYPE_DEVICE,
        .instance_size = sizeof(AT91PadMuxState),
        .instance_init = at91_pad_mux_init,
        .class_init    = at91_pad_mux_class_init,
    },
};

DEFINE_TYPES(at91_pad_mux_types)
