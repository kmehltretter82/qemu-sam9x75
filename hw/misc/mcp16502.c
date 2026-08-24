/*
 * Microchip MCP16502AB PMIC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/i2c/i2c.h"
#include "hw/misc/mcp16502.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qom/object.h"

#define MCP16502_NUM_REGS       0x66

#define MCP16502_SYS_STATUS     0x04
#define MCP16502_BUCK1_STATUS   0x05
#define MCP16502_BUCK4_STATUS   0x08
#define MCP16502_LDO1_STATUS    0x09
#define MCP16502_LDO2_STATUS    0x0a

#define MCP16502_REGULATOR_EN   BIT(7)
#define MCP16502_STATUS_FAULT   BIT(7)
#define MCP16502_STATUS_SSD     BIT(2)
#define MCP16502_STATUS_POK     BIT(1)
#define MCP16502_STATUS_ENS     BIT(0)

OBJECT_DECLARE_SIMPLE_TYPE(MCP16502State, MCP16502_AB)

struct MCP16502State {
    I2CSlave parent_obj;

    uint8_t regs[MCP16502_NUM_REGS];
    uint8_t pointer;
    uint8_t count;
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

static uint8_t mcp16502_status(MCP16502State *s, uint8_t reg)
{
    uint8_t value = s->regs[reg];
    uint8_t output;
    uint8_t clear_mask;

    if (reg >= MCP16502_BUCK1_STATUS &&
        reg <= MCP16502_LDO2_STATUS) {
        output = 0x10 + (reg - MCP16502_BUCK1_STATUS) * 0x10;
        if (s->regs[output] & MCP16502_REGULATOR_EN) {
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
}

static const VMStateDescription vmstate_mcp16502 = {
    .name = TYPE_MCP16502_AB,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, MCP16502State),
        VMSTATE_UINT8_ARRAY(regs, MCP16502State, MCP16502_NUM_REGS),
        VMSTATE_UINT8(pointer, MCP16502State),
        VMSTATE_UINT8(count, MCP16502State),
        VMSTATE_END_OF_LIST()
    }
};

static void mcp16502_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    I2CSlaveClass *isc = I2C_SLAVE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "Microchip MCP16502AB PMIC";
    dc->vmsd = &vmstate_mcp16502;
    isc->event = mcp16502_event;
    isc->recv = mcp16502_recv;
    isc->send = mcp16502_send;
    rc->phases.enter = mcp16502_reset_enter;
}

static const TypeInfo mcp16502_info = {
    .name = TYPE_MCP16502_AB,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(MCP16502State),
    .class_init = mcp16502_class_init,
};

static void mcp16502_register_types(void)
{
    type_register_static(&mcp16502_info);
}

type_init(mcp16502_register_types)
