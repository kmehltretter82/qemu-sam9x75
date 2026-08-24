/*
 * Microchip AT91 SMC-connected raw NAND flash
 *
 * The SAM9X75 Curiosity board carries a Macronix MX30LF4G28AD-XKI:
 * 4-Gbit, x8, 4-KiB pages, 256-byte OOB, and 256-KiB erase blocks.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/block/at91_nand.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/block-backend.h"

#define AT91_NAND_MMIO_SIZE     0x00800000
#define AT91_NAND_ALE           BIT(21)
#define AT91_NAND_CLE           BIT(22)

#define NAND_CMD_READ0          0x00
#define NAND_CMD_RANDOM_READ    0x05
#define NAND_CMD_PAGE_PROGRAM   0x10
#define NAND_CMD_READ_START     0x30
#define NAND_CMD_ERASE          0x60
#define NAND_CMD_STATUS         0x70
#define NAND_CMD_PROGRAM_START  0x80
#define NAND_CMD_READ_ID        0x90
#define NAND_CMD_ERASE_START    0xd0
#define NAND_CMD_RANDOM_START   0xe0
#define NAND_CMD_READ_PARAM     0xec
#define NAND_CMD_GET_FEATURES   0xee
#define NAND_CMD_SET_FEATURES   0xef
#define NAND_CMD_RESET          0xff

#define NAND_STATUS_FAIL        BIT(0)
#define NAND_STATUS_READY       BIT(6)
#define NAND_STATUS_WP          BIT(7)

#define ONFI_PARAM_SIZE         256
#define ONFI_PARAM_COPIES       3
#define ONFI_CRC_BASE           0x4f4e

static uint16_t at91_nand_onfi_crc(uint16_t crc, const uint8_t *p,
                                   size_t len)
{
    unsigned int i;

    while (len--) {
        crc ^= *p++ << 8;
        for (i = 0; i < 8; i++) {
            crc = (crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0);
        }
    }
    return crc;
}

static bool at91_nand_page_is_erased(const uint8_t *page, size_t offset,
                                     size_t length)
{
    size_t i;

    for (i = offset; i < offset + length; i++) {
        if (page[i] != 0xff) {
            return false;
        }
    }
    return true;
}

static bool at91_nand_load_page(AT91NANDState *s, uint32_t page,
                                uint8_t *buf)
{
    uint8_t *sparse;
    int ret;

    memset(buf, 0xff, AT91_NAND_PAGE_TOTAL_SIZE);
    if (page >= AT91_NAND_NUM_PAGES) {
        return false;
    }

    sparse = s->sparse_pages[page];
    if (!s->blk) {
        if (sparse) {
            memcpy(buf, sparse, AT91_NAND_PAGE_TOTAL_SIZE);
        }
        return true;
    }

    if (s->raw_backend) {
        ret = blk_pread(s->blk, (uint64_t)page * AT91_NAND_PAGE_TOTAL_SIZE,
                        AT91_NAND_PAGE_TOTAL_SIZE, buf, 0);
    } else {
        ret = blk_pread(s->blk, (uint64_t)page * AT91_NAND_PAGE_SIZE,
                        AT91_NAND_PAGE_SIZE, buf, 0);
        if (sparse) {
            memcpy(buf + AT91_NAND_PAGE_SIZE,
                   sparse + AT91_NAND_PAGE_SIZE, AT91_NAND_OOB_SIZE);
        }
    }

    return ret >= 0;
}

static bool at91_nand_store_page(AT91NANDState *s, uint32_t page,
                                 const uint8_t *buf)
{
    uint8_t *sparse;
    int ret = 0;

    if (page >= AT91_NAND_NUM_PAGES) {
        return false;
    }

    if (s->blk) {
        if (s->raw_backend) {
            ret = blk_pwrite(s->blk,
                             (uint64_t)page * AT91_NAND_PAGE_TOTAL_SIZE,
                             AT91_NAND_PAGE_TOTAL_SIZE, buf, 0);
        } else {
            ret = blk_pwrite(s->blk,
                             (uint64_t)page * AT91_NAND_PAGE_SIZE,
                             AT91_NAND_PAGE_SIZE, buf, 0);
            if (at91_nand_page_is_erased(buf, AT91_NAND_PAGE_SIZE,
                                         AT91_NAND_OOB_SIZE)) {
                g_clear_pointer(&s->sparse_pages[page], g_free);
            } else {
                sparse = s->sparse_pages[page];
                if (!sparse) {
                    sparse = g_malloc0(AT91_NAND_PAGE_TOTAL_SIZE);
                    s->sparse_pages[page] = sparse;
                }
                memcpy(sparse + AT91_NAND_PAGE_SIZE,
                       buf + AT91_NAND_PAGE_SIZE, AT91_NAND_OOB_SIZE);
            }
        }
        return ret >= 0;
    }

    if (at91_nand_page_is_erased(buf, 0, AT91_NAND_PAGE_TOTAL_SIZE)) {
        g_clear_pointer(&s->sparse_pages[page], g_free);
        return true;
    }

    sparse = s->sparse_pages[page];
    if (!sparse) {
        sparse = g_malloc(AT91_NAND_PAGE_TOTAL_SIZE);
        s->sparse_pages[page] = sparse;
    }
    memcpy(sparse, buf, AT91_NAND_PAGE_TOTAL_SIZE);
    return true;
}

static uint32_t at91_nand_column(const AT91NANDState *s)
{
    if (s->address_len < 2) {
        return 0;
    }
    return s->address[0] | (s->address[1] << 8);
}

static uint32_t at91_nand_row(const AT91NANDState *s, bool has_column)
{
    unsigned int first = has_column ? 2 : 0;
    uint32_t row = 0;
    unsigned int i;

    for (i = first; i < s->address_len && i < first + 3; i++) {
        row |= s->address[i] << ((i - first) * 8);
    }
    return row;
}

static void at91_nand_prepare_id(AT91NANDState *s)
{
    static const uint8_t chip_id[8] = {
        0xc2, 0xdc, 0x90, 0xa2, 0x57, 0x03, 0xff, 0xff,
    };
    uint8_t address = s->address_len ? s->address[0] : 0;

    memset(s->data, 0xff, 8);
    if (address == 0x00) {
        memcpy(s->data, chip_id, sizeof(chip_id));
    } else if (address == 0x20) {
        memcpy(s->data, "ONFI", 4);
    }
    s->data_pos = 0;
    s->data_len = 8;
}

static void at91_nand_prepare_parameter_page(AT91NANDState *s)
{
    uint8_t *p = s->data;
    uint16_t crc;
    unsigned int i;

    memset(p, 0, ONFI_PARAM_SIZE);
    memcpy(p, "ONFI", 4);
    stw_le_p(p + 4, BIT(5));             /* ONFI 2.3 */
    stw_le_p(p + 6, 0);                  /* x8, no extended page */
    stw_le_p(p + 8, 0);                  /* no optional commands needed */
    p[14] = ONFI_PARAM_COPIES;

    memset(p + 32, ' ', 12);
    memcpy(p + 32, "MACRONIX", 8);
    memset(p + 44, ' ', 20);
    memcpy(p + 44, "MX30LF4G28AD-XKI", 17);
    p[64] = 0xc2;

    stl_le_p(p + 80, AT91_NAND_PAGE_SIZE);
    stw_le_p(p + 84, AT91_NAND_OOB_SIZE);
    stl_le_p(p + 86, 0);
    stw_le_p(p + 90, 0);
    stl_le_p(p + 92, AT91_NAND_PAGES_PER_BLOCK);
    stl_le_p(p + 96, AT91_NAND_NUM_BLOCKS);
    p[100] = 1;                          /* one LUN */
    p[101] = 0x32;                       /* 3 row + 2 column cycles */
    p[102] = 1;                          /* SLC */
    stw_le_p(p + 103, 40);
    stw_le_p(p + 105, 1000);
    p[107] = 1;
    stw_le_p(p + 108, 1000);
    p[110] = 1;
    p[112] = 8;                          /* ECC bits per 512 bytes */

    p[128] = 10;
    stw_le_p(p + 129, 0x003f);           /* async timing modes 0..5 */
    stw_le_p(p + 133, 600);
    stw_le_p(p + 135, 5000);
    stw_le_p(p + 137, 25);
    stw_le_p(p + 139, 70);

    crc = at91_nand_onfi_crc(ONFI_CRC_BASE, p, 254);
    stw_le_p(p + 254, crc);
    for (i = 1; i < ONFI_PARAM_COPIES; i++) {
        memcpy(p + i * ONFI_PARAM_SIZE, p, ONFI_PARAM_SIZE);
    }
    s->data_pos = 0;
    s->data_len = ONFI_PARAM_SIZE * ONFI_PARAM_COPIES;
}

static void at91_nand_prepare_features(AT91NANDState *s)
{
    s->feature_address = s->address_len ? s->address[0] : 0;
    memcpy(s->data, s->features[s->feature_address], 4);
    s->data_pos = 0;
    s->data_len = 4;
}

static void at91_nand_read_page(AT91NANDState *s)
{
    uint32_t page = at91_nand_row(s, true);
    uint32_t column = at91_nand_column(s);

    s->status &= ~NAND_STATUS_FAIL;
    if (!at91_nand_load_page(s, page, s->data)) {
        memset(s->data, 0xff, AT91_NAND_PAGE_TOTAL_SIZE);
        s->status |= NAND_STATUS_FAIL;
    }
    s->current_page = page;
    s->data_pos = MIN(column, (uint32_t)AT91_NAND_PAGE_TOTAL_SIZE);
    s->data_len = AT91_NAND_PAGE_TOTAL_SIZE;
}

static void at91_nand_program_page(AT91NANDState *s)
{
    uint32_t page = at91_nand_row(s, true);
    unsigned int i;

    s->status &= ~NAND_STATUS_FAIL;
    if (!at91_nand_load_page(s, page, s->data)) {
        s->status |= NAND_STATUS_FAIL;
        return;
    }
    for (i = 0; i < AT91_NAND_PAGE_TOTAL_SIZE; i++) {
        s->data[i] &= s->program[i];
    }
    if (!at91_nand_store_page(s, page, s->data)) {
        s->status |= NAND_STATUS_FAIL;
    }
}

static void at91_nand_erase_block(AT91NANDState *s)
{
    uint32_t page = at91_nand_row(s, false);
    uint32_t first = page & ~(AT91_NAND_PAGES_PER_BLOCK - 1);
    unsigned int i;

    s->status &= ~NAND_STATUS_FAIL;
    memset(s->data, 0xff, AT91_NAND_PAGE_TOTAL_SIZE);
    if (first >= AT91_NAND_NUM_PAGES) {
        s->status |= NAND_STATUS_FAIL;
        return;
    }
    for (i = 0; i < AT91_NAND_PAGES_PER_BLOCK; i++) {
        if (!at91_nand_store_page(s, first + i, s->data)) {
            s->status |= NAND_STATUS_FAIL;
            return;
        }
    }
}

static void at91_nand_command(AT91NANDState *s, uint8_t command)
{
    uint8_t previous = s->command;

    s->previous_command = previous;
    switch (command) {
    case NAND_CMD_READ0:
    case NAND_CMD_READ_ID:
    case NAND_CMD_READ_PARAM:
    case NAND_CMD_GET_FEATURES:
    case NAND_CMD_SET_FEATURES:
    case NAND_CMD_ERASE:
        s->address_len = 0;
        s->data_pos = 0;
        s->data_len = 0;
        break;
    case NAND_CMD_PROGRAM_START:
        s->address_len = 0;
        s->program_column = 0;
        s->program_pos = UINT32_MAX;
        memset(s->program, 0xff, sizeof(s->program));
        break;
    case NAND_CMD_RANDOM_READ:
        s->address_len = 0;
        break;
    case NAND_CMD_READ_START:
        if (previous == NAND_CMD_READ0) {
            at91_nand_read_page(s);
            command = NAND_CMD_READ0;
        }
        break;
    case NAND_CMD_RANDOM_START:
        if (previous == NAND_CMD_RANDOM_READ) {
            s->data_pos = MIN(at91_nand_column(s),
                              (uint32_t)AT91_NAND_PAGE_TOTAL_SIZE);
            command = NAND_CMD_READ0;
        }
        break;
    case NAND_CMD_PAGE_PROGRAM:
        if (previous == NAND_CMD_PROGRAM_START) {
            at91_nand_program_page(s);
        }
        break;
    case NAND_CMD_ERASE_START:
        if (previous == NAND_CMD_ERASE) {
            at91_nand_erase_block(s);
        }
        break;
    case NAND_CMD_STATUS:
        break;
    case NAND_CMD_RESET:
        s->status = NAND_STATUS_READY | NAND_STATUS_WP;
        s->address_len = 0;
        s->data_pos = 0;
        s->data_len = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_NAND ": unknown command 0x%02x\n", command);
        break;
    }
    s->command = command;
}

static void at91_nand_address(AT91NANDState *s, uint8_t address)
{
    if (s->address_len < ARRAY_SIZE(s->address)) {
        s->address[s->address_len++] = address;
    }
}

static void at91_nand_data_write(AT91NANDState *s, uint8_t value)
{
    if (s->command == NAND_CMD_PROGRAM_START) {
        if (s->program_pos == UINT32_MAX) {
            s->program_column = at91_nand_column(s);
            s->program_pos = s->program_column;
        }
        if (s->program_pos < AT91_NAND_PAGE_TOTAL_SIZE) {
            s->program[s->program_pos++] = value;
        } else {
            s->status |= NAND_STATUS_FAIL;
        }
    } else if (s->command == NAND_CMD_SET_FEATURES) {
        if (!s->data_pos) {
            s->feature_address = s->address_len ? s->address[0] : 0;
        }
        if (s->data_pos < 4) {
            s->features[s->feature_address][s->data_pos++] = value;
        }
    }
}

static uint8_t at91_nand_data_read(AT91NANDState *s)
{
    switch (s->command) {
    case NAND_CMD_STATUS:
        return s->status;
    case NAND_CMD_READ_ID:
        if (!s->data_len) {
            at91_nand_prepare_id(s);
        }
        break;
    case NAND_CMD_READ_PARAM:
        if (!s->data_len) {
            at91_nand_prepare_parameter_page(s);
        }
        break;
    case NAND_CMD_GET_FEATURES:
        if (!s->data_len) {
            at91_nand_prepare_features(s);
        }
        break;
    default:
        break;
    }

    if (s->data_pos < s->data_len) {
        return s->data[s->data_pos++];
    }
    return 0xff;
}

static uint64_t at91_nand_read(void *opaque, hwaddr offset,
                               unsigned int size)
{
    AT91NANDState *s = AT91_NAND(opaque);

    if (!s->selected) {
        return UINT64_MAX;
    }
    if (offset & (AT91_NAND_ALE | AT91_NAND_CLE)) {
        return 0xff;
    }
    return at91_nand_data_read(s);
}

static void at91_nand_write(void *opaque, hwaddr offset, uint64_t value,
                            unsigned int size)
{
    AT91NANDState *s = AT91_NAND(opaque);

    if (!s->selected) {
        return;
    }
    if (offset & AT91_NAND_CLE) {
        at91_nand_command(s, value);
    } else if (offset & AT91_NAND_ALE) {
        at91_nand_address(s, value);
    } else {
        at91_nand_data_write(s, value);
    }
}

static const MemoryRegionOps at91_nand_ops = {
    .read = at91_nand_read,
    .write = at91_nand_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void at91_nand_nce(void *opaque, int n, int level)
{
    AT91NANDState *s = AT91_NAND(opaque);

    assert(n == 0);
    s->selected = !level;
}

static void at91_nand_reset(DeviceState *dev)
{
    AT91NANDState *s = AT91_NAND(dev);

    s->command = NAND_CMD_READ0;
    s->previous_command = NAND_CMD_READ0;
    s->status = NAND_STATUS_READY | NAND_STATUS_WP;
    memset(s->address, 0, sizeof(s->address));
    s->address_len = 0;
    s->feature_address = 0;
    memset(s->features, 0, sizeof(s->features));
    s->current_page = 0;
    s->data_pos = 0;
    s->data_len = 0;
    s->program_column = 0;
    s->program_pos = 0;
    memset(s->data, 0xff, sizeof(s->data));
    memset(s->program, 0xff, sizeof(s->program));
}

static void at91_nand_realize(DeviceState *dev, Error **errp)
{
    AT91NANDState *s = AT91_NAND(dev);
    int64_t length;
    int ret;

    s->sparse_pages = g_new0(uint8_t *, AT91_NAND_NUM_PAGES);
    if (!s->blk) {
        return;
    }

    length = blk_getlength(s->blk);
    if (length != AT91_NAND_DATA_SIZE && length != AT91_NAND_RAW_SIZE) {
        error_setg(errp,
                   "at91-nand drive must be exactly %" PRIu64
                   " bytes (data) or %" PRIu64 " bytes (data+OOB)",
                   AT91_NAND_DATA_SIZE, AT91_NAND_RAW_SIZE);
        return;
    }
    s->raw_backend = length == AT91_NAND_RAW_SIZE;
    ret = blk_set_perm(s->blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                       BLK_PERM_ALL, errp);
    if (ret < 0) {
        return;
    }
}

static void at91_nand_finalize(Object *obj)
{
    AT91NANDState *s = AT91_NAND(obj);
    unsigned int i;

    if (s->sparse_pages) {
        for (i = 0; i < AT91_NAND_NUM_PAGES; i++) {
            g_free(s->sparse_pages[i]);
        }
        g_free(s->sparse_pages);
    }
}

/* Flash contents are storage state; the in-flight NAND protocol is migrated. */
static const VMStateDescription at91_nand_vmstate = {
    .name = TYPE_AT91_NAND,
    .version_id = 2,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(command, AT91NANDState),
        VMSTATE_BOOL_V(selected, AT91NANDState, 2),
        VMSTATE_UINT8(previous_command, AT91NANDState),
        VMSTATE_UINT8(status, AT91NANDState),
        VMSTATE_UINT8_ARRAY(address, AT91NANDState, 5),
        VMSTATE_UINT8(address_len, AT91NANDState),
        VMSTATE_UINT8(feature_address, AT91NANDState),
        VMSTATE_UINT8_2DARRAY(features, AT91NANDState, 256, 4),
        VMSTATE_UINT32(current_page, AT91NANDState),
        VMSTATE_UINT32(data_pos, AT91NANDState),
        VMSTATE_UINT32(data_len, AT91NANDState),
        VMSTATE_UINT32(program_column, AT91NANDState),
        VMSTATE_UINT32(program_pos, AT91NANDState),
        VMSTATE_BUFFER(data, AT91NANDState),
        VMSTATE_BUFFER(program, AT91NANDState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property at91_nand_properties[] = {
    DEFINE_PROP_DRIVE("drive", AT91NANDState, blk),
};

static void at91_nand_init(Object *obj)
{
    AT91NANDState *s = AT91_NAND(obj);

    s->selected = true;
    memory_region_init_io(&s->mmio, obj, &at91_nand_ops, s,
                          TYPE_AT91_NAND, AT91_NAND_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    qdev_init_gpio_in_named(DEVICE(obj), at91_nand_nce,
                            AT91_NAND_GPIO_NCE, 1);
}

static void at91_nand_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Macronix MX30LF4G28AD raw NAND on AT91 SMC";
    dc->realize = at91_nand_realize;
    dc->vmsd = &at91_nand_vmstate;
    device_class_set_legacy_reset(dc, at91_nand_reset);
    device_class_set_props(dc, at91_nand_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo at91_nand_info = {
    .name = TYPE_AT91_NAND,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91NANDState),
    .instance_init = at91_nand_init,
    .instance_finalize = at91_nand_finalize,
    .class_init = at91_nand_class_init,
};

static void at91_nand_register_types(void)
{
    type_register_static(&at91_nand_info);
}

type_init(at91_nand_register_types)
