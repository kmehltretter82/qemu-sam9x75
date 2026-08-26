/*
 * *AT24C* series I2C EEPROM
 *
 * Copyright (c) 2015 Michael Davidsaver
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the LICENSE file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/i2c/i2c.h"
#include "hw/nvram/eeprom_at24c.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "system/block-backend.h"
#include "qom/object.h"

/* #define DEBUG_AT24C */

#ifdef DEBUG_AT24C
#define DPRINTK(FMT, ...) printf(TYPE_AT24C_EE " : " FMT, ## __VA_ARGS__)
#else
#define DPRINTK(FMT, ...) do {} while (0)
#endif

OBJECT_DECLARE_SIMPLE_TYPE(EEPROMState, AT24C_EE)

struct EEPROMState {
    I2CSlave parent_obj;

    /* address counter */
    uint32_t cur;
    /* total size in bytes */
    uint32_t rsize;
    /*
     * address byte number
     *  for  24c01, 24c02 size <= 256 byte, use only 1 byte
     *  otherwise size > 256, use 2 byte
     */
    uint8_t asize;

    /* Bytes in a page-write row; zero selects legacy whole-device wrapping. */
    uint32_t page_size;
    /* Maximum self-timed write cycle, in virtual nanoseconds. */
    uint64_t write_cycle_ns;
    /* Power-on fill used when neither a drive nor init_rom is supplied. */
    uint8_t init_value;
    /* Opt in only machines whose migration ABI includes this state. */
    bool migrate_state;

    bool writable;
    /* cells changed since the last successful backing-store sync */
    bool changed;
    /* at least one data byte was accepted in the current transaction */
    bool wrote_data;
    /* during WRITE, # of address bytes transferred */
    uint8_t haveaddr;
    uint32_t addr_accum;

    uint8_t *mem;

    BlockBackend *blk;

    QEMUTimer *write_timer;

    const uint8_t *init_rom;
    uint32_t init_rom_size;
};

static bool at24c_eeprom_busy(EEPROMState *ee)
{
    return timer_pending(ee->write_timer);
}

static void at24c_eeprom_write_complete(void *opaque)
{
    /* Expiry itself is the state transition: timer_pending() becomes false. */
    (void)opaque;
}

static void at24c_eeprom_sync(EEPROMState *ee)
{
    int ret;

    if (!ee->blk || !ee->changed) {
        ee->changed = false;
        return;
    }

    ret = blk_pwrite(ee->blk, 0, ee->rsize, ee->mem, 0);
    if (ret < 0) {
        error_report("%s: failed to write backing file", __func__);
        return;
    }

    ee->changed = false;
    DPRINTK("Wrote to backing file\n");
}

static
int at24c_eeprom_event(I2CSlave *s, enum i2c_event event)
{
    EEPROMState *ee = AT24C_EE(s);

    switch (event) {
    case I2C_START_SEND:
    case I2C_START_SEND_ASYNC:
        if (at24c_eeprom_busy(ee)) {
            return 1;
        }
        ee->haveaddr = 0;
        ee->addr_accum = 0;
        break;
    case I2C_START_RECV:
        if (at24c_eeprom_busy(ee)) {
            return 1;
        }
        DPRINTK("clear\n");
        break;
    case I2C_FINISH:
        ee->haveaddr = 0;
        at24c_eeprom_sync(ee);
        if (ee->wrote_data && ee->write_cycle_ns) {
            timer_mod_ns(ee->write_timer,
                         qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                         ee->write_cycle_ns);
        }
        ee->wrote_data = false;
        break;
    case I2C_NACK:
        break;
    default:
        return -1;
    }
    return 0;
}

static
uint8_t at24c_eeprom_recv(I2CSlave *s)
{
    EEPROMState *ee = AT24C_EE(s);
    uint8_t ret;

    /*
     * If got the byte address but not completely with address size
     * will return the invalid value
     */
    if (ee->haveaddr > 0 && ee->haveaddr < ee->asize) {
        return 0xff;
    }

    ret = ee->mem[ee->cur];

    ee->cur = (ee->cur + 1u) % ee->rsize;
    DPRINTK("Recv %02x %c\n", ret, ret);

    return ret;
}

static
int at24c_eeprom_send(I2CSlave *s, uint8_t data)
{
    EEPROMState *ee = AT24C_EE(s);

    if (ee->haveaddr < ee->asize) {
        ee->addr_accum <<= 8;
        ee->addr_accum |= data;
        ee->haveaddr++;
        if (ee->haveaddr == ee->asize) {
            ee->cur = ee->addr_accum % ee->rsize;
            DPRINTK("Set pointer %04x\n", ee->cur);
        }

    } else {
        if (ee->writable) {
            uint32_t page_base = ee->cur - ee->cur % ee->page_size;
            uint32_t page_offset = ee->cur - page_base;

            DPRINTK("Send %02x\n", data);
            ee->mem[ee->cur] = data;
            ee->changed = true;
            ee->wrote_data = true;
            ee->cur = page_base + (page_offset + 1) % ee->page_size;
        } else {
            DPRINTK("Send error %02x read-only\n", data);
            ee->cur = (ee->cur + 1u) % ee->rsize;
        }
    }

    return 0;
}

I2CSlave *at24c_eeprom_init(I2CBus *bus, uint8_t address, uint32_t rom_size)
{
    return at24c_eeprom_init_rom(bus, address, rom_size, NULL, 0);
}

I2CSlave *at24c_eeprom_init_rom(I2CBus *bus, uint8_t address, uint32_t rom_size,
                                const uint8_t *init_rom, uint32_t init_rom_size)
{
    EEPROMState *s;

    s = AT24C_EE(i2c_slave_new(TYPE_AT24C_EE, address));

    qdev_prop_set_uint32(DEVICE(s), "rom-size", rom_size);

    /* TODO: Model init_rom with QOM properties. */
    s->init_rom = init_rom;
    s->init_rom_size = init_rom_size;

    i2c_slave_realize_and_unref(I2C_SLAVE(s), bus, &error_abort);

    return I2C_SLAVE(s);
}

static void at24c_eeprom_realize(DeviceState *dev, Error **errp)
{
    EEPROMState *ee = AT24C_EE(dev);

    if (!ee->rsize) {
        error_setg(errp, "%s: rom-size must be non-zero", TYPE_AT24C_EE);
        return;
    }

    if (ee->init_rom_size > ee->rsize) {
        error_setg(errp, "%s: init rom is larger than rom: %u > %u",
                   TYPE_AT24C_EE, ee->init_rom_size, ee->rsize);
        return;
    }

    if (ee->blk) {
        int64_t len = blk_getlength(ee->blk);

        if (len != ee->rsize) {
            error_setg(errp, "%s: Backing file size %" PRId64 " != %u",
                       TYPE_AT24C_EE, len, ee->rsize);
            return;
        }

        if (blk_set_perm(ee->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                         BLK_PERM_ALL, &error_fatal) < 0)
        {
            error_setg(errp, "%s: Backing file incorrect permission",
                       TYPE_AT24C_EE);
            return;
        }
    }

    if (ee->asize > 2) {
        error_setg(errp, "%s: address-size must be 0 (auto), 1 or 2",
                   TYPE_AT24C_EE);
        return;
    }

    if (!ee->page_size) {
        ee->page_size = ee->rsize;
    } else if (ee->page_size > ee->rsize ||
               (ee->page_size & (ee->page_size - 1)) ||
               ee->rsize % ee->page_size) {
        error_setg(errp, "%s: page-size must be a power-of-two divisor "
                   "of rom-size", TYPE_AT24C_EE);
        return;
    }

    ee->mem = g_malloc(ee->rsize);
    memset(ee->mem, ee->init_value, ee->rsize);

    if (ee->blk) {
        int ret = blk_pread(ee->blk, 0, ee->rsize, ee->mem, 0);

        if (ret < 0) {
            error_setg(errp, "%s: Failed initial sync with backing file",
                       TYPE_AT24C_EE);
            return;
        }
        DPRINTK("Reset read backing file\n");
    } else if (ee->init_rom) {
        memcpy(ee->mem, ee->init_rom, MIN(ee->init_rom_size, ee->rsize));
    }

    /*
     * If address size didn't define with property set
     *   value is 0 as default, setting it by Rom size detecting.
     */
    if (ee->asize == 0) {
        if (ee->rsize <= 256) {
            ee->asize = 1;
        } else {
            ee->asize = 2;
        }
    }
}

static
void at24c_eeprom_reset(DeviceState *state)
{
    EEPROMState *ee = AT24C_EE(state);

    ee->changed = false;
    ee->wrote_data = false;
    ee->cur = 0;
    ee->haveaddr = 0;
    ee->addr_accum = 0;
    timer_del(ee->write_timer);
}

static int at24c_eeprom_post_load(void *opaque, int version_id)
{
    EEPROMState *ee = opaque;

    if (ee->cur >= ee->rsize || ee->haveaddr > ee->asize) {
        return -EINVAL;
    }

    return 0;
}

static bool at24c_eeprom_vmstate_needed(void *opaque)
{
    EEPROMState *ee = opaque;

    return ee->migrate_state;
}

static const VMStateDescription at24c_eeprom_vmstate = {
    .name = TYPE_AT24C_EE,
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = at24c_eeprom_vmstate_needed,
    .post_load = at24c_eeprom_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_I2C_SLAVE(parent_obj, EEPROMState),
        VMSTATE_UINT32(cur, EEPROMState),
        VMSTATE_BOOL(changed, EEPROMState),
        VMSTATE_BOOL(wrote_data, EEPROMState),
        VMSTATE_UINT8(haveaddr, EEPROMState),
        VMSTATE_UINT32(addr_accum, EEPROMState),
        VMSTATE_VBUFFER_UINT32(mem, EEPROMState, 1, NULL, rsize),
        VMSTATE_TIMER_PTR(write_timer, EEPROMState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at24c_eeprom_props[] = {
    DEFINE_PROP_UINT32("rom-size", EEPROMState, rsize, 0),
    DEFINE_PROP_UINT8("address-size", EEPROMState, asize, 0),
    DEFINE_PROP_UINT32("page-size", EEPROMState, page_size, 0),
    DEFINE_PROP_UINT64("write-cycle-ns", EEPROMState, write_cycle_ns, 0),
    DEFINE_PROP_UINT8("init-value", EEPROMState, init_value, 0),
    DEFINE_PROP_BOOL("migrate-state", EEPROMState, migrate_state, false),
    DEFINE_PROP_BOOL("writable", EEPROMState, writable, true),
    DEFINE_PROP_DRIVE("drive", EEPROMState, blk),
};

static void at24c_eeprom_instance_init(Object *obj)
{
    EEPROMState *ee = AT24C_EE(obj);

    ee->write_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                   at24c_eeprom_write_complete, ee);
}

static void at24c_eeprom_finalize(Object *obj)
{
    EEPROMState *ee = AT24C_EE(obj);

    timer_free(ee->write_timer);
    g_free(ee->mem);
}

static
void at24c_eeprom_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    I2CSlaveClass *k = I2C_SLAVE_CLASS(klass);

    dc->realize = &at24c_eeprom_realize;
    dc->vmsd = &at24c_eeprom_vmstate;
    k->event = &at24c_eeprom_event;
    k->recv = &at24c_eeprom_recv;
    k->send = &at24c_eeprom_send;

    device_class_set_props(dc, at24c_eeprom_props);
    device_class_set_legacy_reset(dc, at24c_eeprom_reset);
}

static
const TypeInfo at24c_eeprom_type = {
    .name = TYPE_AT24C_EE,
    .parent = TYPE_I2C_SLAVE,
    .instance_size = sizeof(EEPROMState),
    .class_size = sizeof(I2CSlaveClass),
    .instance_init = at24c_eeprom_instance_init,
    .instance_finalize = at24c_eeprom_finalize,
    .class_init = at24c_eeprom_class_init,
};

static void at24c_eeprom_register(void)
{
    type_register_static(&at24c_eeprom_type);
}

type_init(at24c_eeprom_register)
