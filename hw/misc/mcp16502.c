/*
 * Microchip MCP16502AB PMIC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/i2c/i2c.h"
#include "hw/misc/mcp16502.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/timer.h"
#include "qom/object.h"
#include "system/runstate.h"

#define MCP16502_NUM_REGS       0x66

#define MCP16502_SYS_TMG        0x02
#define MCP16502_SYS_CFG        0x03
#define MCP16502_SYS_STATUS     0x04
#define MCP16502_BUCK1_STATUS   0x05
#define MCP16502_BUCK4_STATUS   0x08
#define MCP16502_LDO1_STATUS    0x09
#define MCP16502_LDO2_STATUS    0x0a

#define MCP16502_SYS_TMG_PBTO_SHIFT      6
#define MCP16502_SYS_TMG_PBINTTO_SHIFT   4

#define MCP16502_SYS_CFG_TSDMSK          BIT(7)
#define MCP16502_SYS_CFG_TWRMSK          BIT(6)
#define MCP16502_SYS_CFG_FSD_SHIFT       2

#define MCP16502_SYS_STATUS_TSD          BIT(7)
#define MCP16502_SYS_STATUS_TWR          BIT(6)
#define MCP16502_SYS_STATUS_PBINT        BIT(5)

#define MCP16502_REGULATOR_EN   BIT(7)
#define MCP16502_STATUS_FAULT   BIT(7)
#define MCP16502_STATUS_SSD     BIT(2)
#define MCP16502_STATUS_POK     BIT(1)
#define MCP16502_STATUS_ENS     BIT(0)

typedef enum MCP16502PushTimerPhase {
    MCP16502_PUSH_TIMER_IDLE,
    MCP16502_PUSH_TIMER_LONG_PRESS,
    MCP16502_PUSH_TIMER_INTERRUPT,
} MCP16502PushTimerPhase;

OBJECT_DECLARE_SIMPLE_TYPE(MCP16502State, MCP16502_AB)

struct MCP16502State {
    I2CSlave parent_obj;

    qemu_irq nstrto;
    qemu_irq ninto;
    qemu_irq nrsto;
    QEMUTimer *push_timer;

    uint8_t regs[MCP16502_NUM_REGS];
    uint8_t pointer;
    uint8_t count;
    uint8_t push_timer_phase;
    bool nstart_level;
    bool pwrhld_level;
    bool lpm_level;
    bool hpm_level;
    bool forced_off;
    bool request_system_shutdown;
};

/*
 * OTP defaults for the MCP16502TAB-E/S8B fitted to the SAM9X75 Curiosity.
 * SELV2 is floating and SELVL1 is high on this board.
 */
static const uint8_t mcp16502ab_reset_regs[MCP16502_NUM_REGS] = {
    [0x00] = 0xdb, [0x01] = 0x20, [0x02] = 0x54, [0x03] = 0xc0,
    [0x10] = 0xf7, [0x11] = 0xb7, [0x12] = 0x37, [0x13] = 0xf7,
    [0x14] = 0x09, [0x15] = 0xb0,
    [0x20] = 0xeb, [0x21] = 0xab, [0x22] = 0xab, [0x23] = 0xeb,
    [0x24] = 0x1d, [0x25] = 0xa0,
    [0x30] = 0xe3, [0x31] = 0xa3, [0x32] = 0x23, [0x33] = 0xe3,
    [0x34] = 0x2c, [0x35] = 0xb0,
    [0x40] = 0xe3, [0x41] = 0xa3, [0x42] = 0x23, [0x43] = 0xe3,
    [0x44] = 0x2c, [0x45] = 0xa0,
    [0x50] = 0xb7, [0x51] = 0xb7, [0x52] = 0x37, [0x53] = 0xb7,
    [0x54] = 0x09, [0x55] = 0xa0,
    [0x60] = 0x37, [0x61] = 0x37, [0x62] = 0x37, [0x63] = 0x37,
    [0x64] = 0x01, [0x65] = 0xa0,
};

static uint8_t mcp16502_write_mask(uint8_t reg)
{
    unsigned int offset = reg & 0xf;

    switch (reg) {
    case 0x02:
        return 0xf7;
    case 0x03:
        return 0xff;
    default:
        break;
    }

    if (reg >= 0x10 && reg <= 0x45 && offset <= 5) {
        return 0xff;
    }

    if ((reg >= 0x50 && reg <= 0x55) ||
        (reg >= 0x60 && reg <= 0x65)) {
        if (offset <= 3) {
            return 0xbf;
        }
        if (offset == 4) {
            return 0xff;
        }
        if (offset == 5) {
            return 0xaf;
        }
    }

    return 0;
}

static int mcp16502_power_mode(MCP16502State *s)
{
    if (s->forced_off) {
        return -1;
    }
    if (!s->pwrhld_level) {
        return s->lpm_level ? 2 : -1;
    }
    if (s->lpm_level) {
        return 1;
    }
    if (s->hpm_level && (s->regs[0x03] & BIT(5))) {
        return 3;
    }
    return 0;
}

static void mcp16502_update_outputs(MCP16502State *s)
{
    uint8_t status = s->regs[MCP16502_SYS_STATUS];
    uint8_t config = s->regs[MCP16502_SYS_CFG];
    bool interrupt;

    interrupt = (status & MCP16502_SYS_STATUS_PBINT) ||
        ((status & MCP16502_SYS_STATUS_TSD) &&
         !(config & MCP16502_SYS_CFG_TSDMSK)) ||
        ((status & MCP16502_SYS_STATUS_TWR) &&
         !(config & MCP16502_SYS_CFG_TWRMSK));

    /* Active-low open-drain pins are exposed as logical assertions. */
    qemu_set_irq(s->nstrto, !s->nstart_level);
    qemu_set_irq(s->ninto, interrupt);
    qemu_set_irq(s->nrsto, s->forced_off || !s->pwrhld_level);
}

static int64_t mcp16502_scale_timing(MCP16502State *s, int64_t ns)
{
    unsigned int fsd = extract32(s->regs[MCP16502_SYS_CFG],
                                 MCP16502_SYS_CFG_FSD_SHIFT, 2);

    /* FSD=10 lowers the oscillator by 16.5%; FSD=11 raises it by 16.5%. */
    if (fsd == 2) {
        return muldiv64(ns, 10000, 8350);
    }
    if (fsd == 3) {
        return muldiv64(ns, 10000, 11650);
    }
    return ns;
}

static int64_t mcp16502_push_timeout(MCP16502State *s)
{
    unsigned int selector = extract32(s->regs[MCP16502_SYS_TMG],
                                      MCP16502_SYS_TMG_PBTO_SHIFT, 2);

    return mcp16502_scale_timing(s,
        (2LL << selector) * NANOSECONDS_PER_SECOND);
}

static int64_t mcp16502_interrupt_timeout(MCP16502State *s)
{
    static const int64_t delays[] = {
        100 * SCALE_MS,
        500 * SCALE_MS,
        NANOSECONDS_PER_SECOND,
        2 * NANOSECONDS_PER_SECOND,
    };
    unsigned int selector = extract32(s->regs[MCP16502_SYS_TMG],
                                      MCP16502_SYS_TMG_PBINTTO_SHIFT, 2);

    return mcp16502_scale_timing(s, delays[selector]);
}

static void mcp16502_start_push_timer(MCP16502State *s)
{
    s->push_timer_phase = MCP16502_PUSH_TIMER_LONG_PRESS;
    timer_mod_ns(s->push_timer,
        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + mcp16502_push_timeout(s));
}

static void mcp16502_cancel_push_timer(MCP16502State *s)
{
    s->push_timer_phase = MCP16502_PUSH_TIMER_IDLE;
    timer_del(s->push_timer);
}

static void mcp16502_push_timer_tick(void *opaque)
{
    MCP16502State *s = opaque;

    switch (s->push_timer_phase) {
    case MCP16502_PUSH_TIMER_LONG_PRESS:
        if (s->nstart_level || !s->pwrhld_level || s->forced_off) {
            mcp16502_cancel_push_timer(s);
            return;
        }
        s->regs[MCP16502_SYS_STATUS] |= MCP16502_SYS_STATUS_PBINT;
        s->push_timer_phase = MCP16502_PUSH_TIMER_INTERRUPT;
        timer_mod_ns(s->push_timer,
            qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
            mcp16502_interrupt_timeout(s));
        mcp16502_update_outputs(s);
        break;
    case MCP16502_PUSH_TIMER_INTERRUPT:
        s->forced_off = true;
        s->push_timer_phase = MCP16502_PUSH_TIMER_IDLE;
        mcp16502_update_outputs(s);
        if (s->request_system_shutdown) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        break;
    case MCP16502_PUSH_TIMER_IDLE:
        break;
    default:
        g_assert_not_reached();
    }
}

static uint8_t mcp16502_status(MCP16502State *s, uint8_t reg)
{
    uint8_t value = s->regs[reg];
    uint8_t output;
    uint8_t clear_mask;
    int mode;

    if (reg >= MCP16502_BUCK1_STATUS &&
        reg <= MCP16502_LDO2_STATUS) {
        output = 0x10 + (reg - MCP16502_BUCK1_STATUS) * 0x10;
        mode = mcp16502_power_mode(s);
        if (mode >= 0 && (s->regs[output + mode] & MCP16502_REGULATOR_EN)) {
            value |= MCP16502_STATUS_SSD | MCP16502_STATUS_POK |
                     MCP16502_STATUS_ENS;
        }
    }

    if (reg == MCP16502_SYS_STATUS) {
        clear_mask = 0xe0;
    } else if (reg >= MCP16502_BUCK1_STATUS &&
               reg <= MCP16502_BUCK4_STATUS) {
        clear_mask = 0xf0;
    } else {
        clear_mask = MCP16502_STATUS_FAULT;
    }
    s->regs[reg] &= ~clear_mask;

    if (reg == MCP16502_SYS_STATUS) {
        if (value & MCP16502_SYS_STATUS_PBINT) {
            mcp16502_cancel_push_timer(s);
            if (!s->nstart_level && s->pwrhld_level && !s->forced_off) {
                mcp16502_start_push_timer(s);
            }
        }
        mcp16502_update_outputs(s);
    }

    return value;
}

static uint8_t mcp16502_read(MCP16502State *s, uint8_t reg)
{
    if (reg >= MCP16502_SYS_STATUS && reg <= MCP16502_LDO2_STATUS) {
        return mcp16502_status(s, reg);
    }

    if (reg < MCP16502_NUM_REGS) {
        return s->regs[reg];
    }

    return 0;
}

static void mcp16502_set_nstart(void *opaque, int line, int level)
{
    MCP16502State *s = opaque;
    bool old_level = s->nstart_level;

    s->nstart_level = level < 0 || level;
    if (old_level && !s->nstart_level && s->pwrhld_level && !s->forced_off) {
        mcp16502_start_push_timer(s);
    } else if (!old_level && s->nstart_level &&
               s->push_timer_phase == MCP16502_PUSH_TIMER_LONG_PRESS) {
        mcp16502_cancel_push_timer(s);
    }
    mcp16502_update_outputs(s);
}

static void mcp16502_set_pwrhld(void *opaque, int line, int level)
{
    MCP16502State *s = opaque;

    s->pwrhld_level = level > 0;
    if (!s->pwrhld_level) {
        mcp16502_cancel_push_timer(s);
    } else if (!s->nstart_level && !s->forced_off) {
        mcp16502_start_push_timer(s);
    }
    mcp16502_update_outputs(s);
}

static void mcp16502_set_lpm(void *opaque, int line, int level)
{
    MCP16502State *s = opaque;

    s->lpm_level = level > 0;
    mcp16502_update_outputs(s);
}

static void mcp16502_set_hpm(void *opaque, int line, int level)
{
    MCP16502State *s = opaque;

    s->hpm_level = level > 0;
}

static int mcp16502_event(I2CSlave *i2c, enum i2c_event event)
{
    MCP16502State *s = MCP16502_AB(i2c);

    if (event == I2C_START_SEND || event == I2C_FINISH) {
        s->count = 0;
    }

    return 0;
}

static uint8_t mcp16502_recv(I2CSlave *i2c)
{
    MCP16502State *s = MCP16502_AB(i2c);

    return mcp16502_read(s, s->pointer++);
}

static int mcp16502_send(I2CSlave *i2c, uint8_t data)
{
    MCP16502State *s = MCP16502_AB(i2c);
    uint8_t mask;

    if (s->count++ == 0) {
        s->pointer = data;
        return 0;
    }

    mask = mcp16502_write_mask(s->pointer);
    if (s->pointer < MCP16502_NUM_REGS && mask) {
        s->regs[s->pointer] = (s->regs[s->pointer] & ~mask) | (data & mask);
        if (s->pointer == MCP16502_SYS_CFG) {
            mcp16502_update_outputs(s);
        }
    }
    s->pointer++;

    return 0;
}

static void mcp16502_reset_enter(Object *obj, ResetType type)
{
    MCP16502State *s = MCP16502_AB(obj);

    memcpy(s->regs, mcp16502ab_reset_regs, sizeof(s->regs));
    s->pointer = 0;
    s->count = 0;
    s->nstart_level = true;
    s->pwrhld_level = true;
    s->lpm_level = false;
    s->hpm_level = false;
    s->forced_off = false;
    mcp16502_cancel_push_timer(s);
    mcp16502_update_outputs(s);
}

static int mcp16502_post_load(void *opaque, int version_id)
{
    MCP16502State *s = opaque;

    if (version_id < 2) {
        s->nstart_level = true;
        s->pwrhld_level = true;
        s->lpm_level = false;
        s->hpm_level = false;
    }
    if (version_id < 3) {
        s->push_timer_phase = MCP16502_PUSH_TIMER_IDLE;
        s->forced_off = false;
        timer_del(s->push_timer);
    }
    mcp16502_update_outputs(s);
    return 0;
}

static const VMStateDescription vmstate_mcp16502 = {
    .name = TYPE_MCP16502_AB,
    .version_id = 3,
    .minimum_version_id = 1,
    .post_load = mcp16502_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, MCP16502State),
        VMSTATE_UINT8_ARRAY(regs, MCP16502State, MCP16502_NUM_REGS),
        VMSTATE_UINT8(pointer, MCP16502State),
        VMSTATE_UINT8(count, MCP16502State),
        VMSTATE_BOOL_V(nstart_level, MCP16502State, 2),
        VMSTATE_BOOL_V(pwrhld_level, MCP16502State, 2),
        VMSTATE_BOOL_V(lpm_level, MCP16502State, 2),
        VMSTATE_BOOL_V(hpm_level, MCP16502State, 2),
        VMSTATE_UINT8_V(push_timer_phase, MCP16502State, 3),
        VMSTATE_BOOL_V(forced_off, MCP16502State, 3),
        VMSTATE_TIMER_PTR_V(push_timer, MCP16502State, 3),
        VMSTATE_END_OF_LIST()
    }
};

static void mcp16502_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    MCP16502State *s = MCP16502_AB(obj);

    qdev_init_gpio_in_named(dev, mcp16502_set_nstart, "nstart", 1);
    qdev_init_gpio_in_named(dev, mcp16502_set_pwrhld, "pwrhld", 1);
    qdev_init_gpio_in_named(dev, mcp16502_set_lpm, "lpm", 1);
    qdev_init_gpio_in_named(dev, mcp16502_set_hpm, "hpm", 1);
    qdev_init_gpio_out_named(dev, &s->nstrto, "nstrto", 1);
    qdev_init_gpio_out_named(dev, &s->ninto, "ninto", 1);
    qdev_init_gpio_out_named(dev, &s->nrsto, "nrsto", 1);
    s->push_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  mcp16502_push_timer_tick, s);
}

static void mcp16502_finalize(Object *obj)
{
    MCP16502State *s = MCP16502_AB(obj);

    timer_free(s->push_timer);
}

static const Property mcp16502_properties[] = {
    DEFINE_PROP_BOOL("request-system-shutdown", MCP16502State,
                     request_system_shutdown, true),
};

static void mcp16502_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "Microchip MCP16502AB PMIC";
    dc->vmsd = &vmstate_mcp16502;
    device_class_set_props(dc, mcp16502_properties);
    isc->event = mcp16502_event;
    isc->recv = mcp16502_recv;
    isc->send = mcp16502_send;
    rc->phases.enter = mcp16502_reset_enter;
}

static const TypeInfo mcp16502_info = {
    .name = TYPE_MCP16502_AB,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MCP16502State),
    .instance_init = mcp16502_init,
    .instance_finalize = mcp16502_finalize,
    .class_init = mcp16502_class_init,
};

static void mcp16502_register_types(void)
{
    type_register_static(&mcp16502_info);
}

type_init(mcp16502_register_types)
