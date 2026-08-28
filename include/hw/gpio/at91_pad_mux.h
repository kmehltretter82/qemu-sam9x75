/*
 * Microchip AT91 shared pad multiplexer
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_GPIO_AT91_PAD_MUX_H
#define HW_GPIO_AT91_PAD_MUX_H

#include "hw/core/qdev.h"
#include "qom/object.h"

#define TYPE_AT91_PAD_MUX "at91-pad-mux"
OBJECT_DECLARE_SIMPLE_TYPE(AT91PadMuxState, AT91_PAD_MUX)

/*
 * Input driven by the pad's peripheral function, for example a FLEXCOM SPI
 * NPCS output.  A peripheral drives continuously, so this input is a plain
 * level.
 */
#define AT91_PAD_MUX_PERIPHERAL "peripheral"

/*
 * Input driven by the PIO controller for the same pad.  The PIO releases a
 * pad it does not drive by reporting a negative level, which is how
 * at91_pio_update_outputs() reports peripheral-controlled and input pins.
 */
#define AT91_PAD_MUX_PIO "pio"

struct AT91PadMuxState {
    DeviceState parent_obj;

    /* Level last reported by the peripheral function. */
    bool peripheral_level;
    /* Whether the PIO currently drives the pad, and at which level. */
    bool pio_driving;
    bool pio_level;
    /* Level of the board pull resistor, used when nothing drives the pad. */
    bool pullup;
    /* Last level propagated to the pad consumer, and whether one was sent. */
    bool level;
    bool level_valid;

    qemu_irq out;
};

#endif /* HW_GPIO_AT91_PAD_MUX_H */
