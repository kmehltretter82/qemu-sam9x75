/*
 * Gamepad style buttons connected to IRQ/GPIO lines
 *
 * Copyright (c) 2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/input/stellaris_gamepad.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "ui/console.h"

static void stellaris_gamepad_event(DeviceState *dev, QemuConsole *src,
                                    QemuInputEvent *evt)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(dev);
    int qcode = qemu_input_linux_to_qcode(evt->key.key);
    int i;

    for (i = 0; i < s->num_buttons; i++) {
        if (s->keycodes[i] == qcode && s->pressed[i] != evt->key.down) {
            s->pressed[i] = evt->key.down;
            qemu_set_irq(s->irqs[i], evt->key.down);
        }
    }
}

static void stellaris_gamepad_update_outputs(StellarisGamepad *s)
{
    unsigned int i;

    for (i = 0; i < s->num_buttons; i++) {
        qemu_set_irq(s->irqs[i], s->pressed[i]);
    }
}

static int stellaris_gamepad_post_load(void *opaque, int version_id)
{
    stellaris_gamepad_update_outputs(STELLARIS_GAMEPAD(opaque));
    return 0;
}

static const VMStateDescription vmstate_stellaris_gamepad = {
    .name = "stellaris_gamepad",
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = stellaris_gamepad_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_VARRAY_UINT32(pressed, StellarisGamepad, num_buttons,
                              0, vmstate_info_uint8, uint8_t),
        VMSTATE_END_OF_LIST()
    }
};

static const QemuInputHandler stellaris_gamepad_handler = {
    .name = "Stellaris Gamepad",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = stellaris_gamepad_event,
};

static void stellaris_gamepad_realize(DeviceState *dev, Error **errp)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(dev);

    if (s->num_buttons == 0) {
        error_setg(errp, "keycodes property array must be set");
        return;
    }

    s->irqs = g_new0(qemu_irq, s->num_buttons);
    s->pressed = g_new0(uint8_t, s->num_buttons);
    qdev_init_gpio_out(dev, s->irqs, s->num_buttons);
    s->hs = qemu_input_handler_register(dev, &stellaris_gamepad_handler);
}

static void stellaris_gamepad_unrealize(DeviceState *dev)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(dev);

    g_clear_pointer(&s->irqs, g_free);
    g_clear_pointer(&s->pressed, g_free);
    g_clear_pointer(&s->hs, qemu_input_handler_unregister);
}

static void stellaris_gamepad_reset_enter(Object *obj, ResetType type)
{
    StellarisGamepad *s = STELLARIS_GAMEPAD(obj);

    if (type != RESET_TYPE_WAKEUP || !s->retain_on_wakeup) {
        memset(s->pressed, 0, s->num_buttons * sizeof(uint8_t));
    }
}

static void stellaris_gamepad_reset_hold(Object *obj, ResetType type)
{
    stellaris_gamepad_update_outputs(STELLARIS_GAMEPAD(obj));
}

static const Property stellaris_gamepad_properties[] = {
    DEFINE_PROP_ARRAY("keycodes", StellarisGamepad, num_buttons,
                      keycodes, qdev_prop_uint32, uint32_t),
    DEFINE_PROP_BOOL("retain-on-wakeup", StellarisGamepad,
                     retain_on_wakeup, false),
};

static void stellaris_gamepad_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.enter = stellaris_gamepad_reset_enter;
    rc->phases.hold = stellaris_gamepad_reset_hold;
    dc->realize = stellaris_gamepad_realize;
    dc->unrealize = stellaris_gamepad_unrealize;
    dc->vmsd = &vmstate_stellaris_gamepad;
    device_class_set_props(dc, stellaris_gamepad_properties);
}

static const TypeInfo stellaris_gamepad_info[] = {
    {
        .name = TYPE_STELLARIS_GAMEPAD,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(StellarisGamepad),
        .class_init = stellaris_gamepad_class_init,
    },
};

DEFINE_TYPES(stellaris_gamepad_info);
