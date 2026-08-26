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
#include "hw/core/resettable.h"
#include "hw/misc/at91_pmecc.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/bswap.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "system/block-backend.h"

#define AT91_NAND_MMIO_SIZE     0x10000000
#define AT91_NAND_ALE           BIT(21)
#define AT91_NAND_CLE           BIT(22)

#define NAND_CMD_READ0          0x00
#define NAND_CMD_RANDOM_READ    0x05
#define NAND_CMD_PAGE_PROGRAM   0x10
#define NAND_CMD_READ_START     0x30
#define NAND_CMD_ERASE          0x60
#define NAND_CMD_STATUS         0x70
#define NAND_CMD_ENH_STATUS     0x78
#define NAND_CMD_PROGRAM_START  0x80
#define NAND_CMD_RANDOM_INPUT   0x85
#define NAND_CMD_READ_ID        0x90
#define NAND_CMD_ERASE_START    0xd0
#define NAND_CMD_RANDOM_START   0xe0
#define NAND_CMD_READ_PARAM     0xec
#define NAND_CMD_GET_FEATURES   0xee
#define NAND_CMD_SET_FEATURES   0xef
#define NAND_CMD_RESET          0xff

#define NAND_STATUS_FAIL        BIT(0)
#define NAND_STATUS_TRUE_READY  BIT(5)
#define NAND_STATUS_READY       BIT(6)
#define NAND_STATUS_WP          BIT(7)
#define NAND_STATUS_PLANE_MASK  (BIT(0) | BIT(1) | BIT(3) | BIT(4))
#define NAND_STATUS_SHARED_MASK (BIT(5) | BIT(6) | BIT(7))

#define NAND_READ_TIME_NS       (25 * SCALE_US)
#define NAND_PROGRAM_TIME_NS    (700 * SCALE_US)
#define NAND_RANDOM_PROGRAM_TIME_NS (740 * SCALE_US)
#define NAND_ERASE_TIME_NS      (6000 * SCALE_US)
#define NAND_FEATURE_TIME_NS    (1 * SCALE_US)
#define NAND_RESET_IDLE_NS      (5 * SCALE_US)
#define NAND_RESET_READ_NS      (5 * SCALE_US)
#define NAND_RESET_PROGRAM_NS   (10 * SCALE_US)
#define NAND_RESET_ERASE_NS     (500 * SCALE_US)

#define ONFI_PARAM_SIZE         256
#define ONFI_PARAM_COPIES       8
#define ONFI_CRC_BASE           0x4f4e

#define NAND_FEATURE_TIMING_MODE        0x01
#define NAND_FEATURE_IO_DRIVE_STRENGTH  0x80
#define NAND_FEATURE_RECOVERY_READ      0x89
#define NAND_FEATURE_ARRAY_MODE         0x90
#define NAND_FEATURE_CONFIGURATION      0xb0
#define NAND_FEATURE_RANDEN              BIT(1)

enum {
    NAND_OP_NONE,
    NAND_OP_PAGE_READ,
    NAND_OP_PARAMETER_READ,
    NAND_OP_GET_FEATURES,
    NAND_OP_SET_FEATURES,
    NAND_OP_PAGE_PROGRAM,
    NAND_OP_BLOCK_ERASE,
    NAND_OP_RESET,
};

static unsigned int at91_nand_plane(uint32_t row)
{
    /* A 4 KiB page makes physical address A19 row-address bit 6. */
    return (row >> 6) & 1;
}

static void at91_nand_clear_sparse_pages(AT91NANDState *s)
{
    unsigned int page;

    if (!s->sparse_pages) {
        return;
    }
    for (page = 0; page < AT91_NAND_NUM_PAGES; page++) {
        g_clear_pointer(&s->sparse_pages[page], g_free);
    }
}

static bool at91_nand_has_sparse_pages(const AT91NANDState *s)
{
    unsigned int page;

    for (page = 0; page < AT91_NAND_NUM_PAGES; page++) {
        if (s->sparse_pages[page]) {
            return true;
        }
    }
    return false;
}

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
    stw_le_p(p + 4, 0x0002);             /* ONFI 1.0 */
    stw_le_p(p + 6, 0x0018);  /* interleaving + no odd/even copyback limit */
    stw_le_p(p + 8, 0x003f);             /* supported optional commands */

    memset(p + 32, ' ', 12);
    memcpy(p + 32, "MACRONIX", 8);
    memset(p + 44, ' ', 20);
    memcpy(p + 44, "MX30LF4G28AD", 12);
    p[64] = 0xc2;

    stl_le_p(p + 80, AT91_NAND_PAGE_SIZE);
    stw_le_p(p + 84, AT91_NAND_OOB_SIZE);
    stl_le_p(p + 86, 1024);
    stw_le_p(p + 90, 64);
    stl_le_p(p + 92, AT91_NAND_PAGES_PER_BLOCK);
    stl_le_p(p + 96, AT91_NAND_NUM_BLOCKS);
    p[100] = 1;                          /* one LUN */
    p[101] = 0x23;                       /* 3 row + 2 column cycles */
    p[102] = 1;                          /* SLC */
    stw_le_p(p + 103, 40);
    stw_le_p(p + 105, 0x0406);
    p[107] = 8;
    stw_le_p(p + 108, 0);
    p[110] = 4;
    p[112] = 8;                          /* ECC bits per 512 bytes */
    p[113] = 1;
    p[114] = 0x0e;

    p[128] = 10;
    stw_le_p(p + 129, 0x003f);           /* async timing modes 0..5 */
    stw_le_p(p + 131, 0x003f);           /* cache timing modes 0..5 */
    stw_le_p(p + 133, 700);
    stw_le_p(p + 135, 6000);
    stw_le_p(p + 137, 25);
    stw_le_p(p + 139, 60);
    p[167] = 3;                          /* randomizer and recovery read */
    p[169] = 5;                          /* recovery-read levels */

    crc = at91_nand_onfi_crc(ONFI_CRC_BASE, p, 254);
    stw_le_p(p + 254, crc);
    for (i = 1; i < ONFI_PARAM_COPIES; i++) {
        memcpy(p + i * ONFI_PARAM_SIZE, p, ONFI_PARAM_SIZE);
    }
    s->data_pos = 0;
    s->data_len = ONFI_PARAM_SIZE * ONFI_PARAM_COPIES;
}

static uint8_t at91_nand_feature_mask(uint8_t address)
{
    switch (address) {
    case NAND_FEATURE_TIMING_MODE:
    case NAND_FEATURE_RECOVERY_READ:
        return 0x07;
    case NAND_FEATURE_IO_DRIVE_STRENGTH:
        return 0x01;
    case NAND_FEATURE_ARRAY_MODE:
        return 0x03;
    case NAND_FEATURE_CONFIGURATION:
        return 0x07;
    default:
        return 0;
    }
}

static bool at91_nand_feature_value_valid(uint8_t address, uint8_t value)
{
    switch (address) {
    case NAND_FEATURE_TIMING_MODE:
    case NAND_FEATURE_RECOVERY_READ:
        return value <= 5;
    case NAND_FEATURE_IO_DRIVE_STRENGTH:
    case NAND_FEATURE_CONFIGURATION:
        return true;
    case NAND_FEATURE_ARRAY_MODE:
        return value == 0 || value == 1 || value == 3;
    default:
        return false;
    }
}

static bool at91_nand_commit_features(AT91NANDState *s)
{
    uint8_t address = s->feature_address;
    uint8_t value = s->data[0] & at91_nand_feature_mask(address);

    if (!at91_nand_feature_value_valid(address, value)) {
        return false;
    }

    memset(s->features[address], 0, sizeof(s->features[address]));
    s->features[address][0] = value;
    return true;
}

static void at91_nand_prepare_features(AT91NANDState *s)
{
    s->feature_address = s->address_len ? s->address[0] : 0;
    memcpy(s->data, s->features[s->feature_address], 4);
    s->data_pos = 0;
    s->data_len = 4;
}

static void at91_nand_set_ready(AT91NANDState *s, bool ready)
{
    if (ready) {
        s->status |= NAND_STATUS_TRUE_READY | NAND_STATUS_READY;
    } else {
        s->status &= ~(NAND_STATUS_TRUE_READY | NAND_STATUS_READY);
    }
    qemu_set_irq(s->ready, ready);
}

static void at91_nand_start_operation(AT91NANDState *s, uint8_t operation,
                                      int64_t delay_ns)
{
    g_assert(s->pending_operation == NAND_OP_NONE);
    s->pending_page = 0;
    s->pending_column = 0;
    switch (operation) {
    case NAND_OP_PAGE_READ:
        s->pending_page = at91_nand_row(s, true);
        s->pending_column = at91_nand_column(s);
        break;
    case NAND_OP_PAGE_PROGRAM:
        s->pending_page = s->current_page == UINT32_MAX ?
                          at91_nand_row(s, true) : s->current_page;
        break;
    case NAND_OP_BLOCK_ERASE:
        s->pending_page = at91_nand_row(s, false);
        break;
    default:
        break;
    }
    s->pending_operation = operation;
    at91_nand_set_ready(s, false);
    timer_mod(s->operation_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay_ns);
}

static void at91_nand_read_page(AT91NANDState *s)
{
    uint32_t page = s->pending_page;
    uint32_t column = s->pending_column;

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
    uint32_t page = s->pending_page;
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
    uint32_t page = s->pending_page;
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

static void at91_nand_complete_operation(void *opaque)
{
    AT91NANDState *s = opaque;
    uint8_t operation = s->pending_operation;
    uint8_t plane;

    s->pending_operation = NAND_OP_NONE;
    switch (operation) {
    case NAND_OP_PAGE_READ:
        at91_nand_read_page(s);
        break;
    case NAND_OP_PARAMETER_READ:
        at91_nand_prepare_parameter_page(s);
        break;
    case NAND_OP_GET_FEATURES:
        at91_nand_prepare_features(s);
        break;
    case NAND_OP_SET_FEATURES:
        at91_nand_commit_features(s);
        break;
    case NAND_OP_PAGE_PROGRAM:
        at91_nand_program_page(s);
        plane = at91_nand_plane(s->pending_page);
        memset(s->plane_status, 0, sizeof(s->plane_status));
        s->plane_status[plane] = s->status & NAND_STATUS_PLANE_MASK;
        break;
    case NAND_OP_BLOCK_ERASE:
        at91_nand_erase_block(s);
        plane = at91_nand_plane(s->pending_page);
        memset(s->plane_status, 0, sizeof(s->plane_status));
        s->plane_status[plane] = s->status & NAND_STATUS_PLANE_MASK;
        break;
    case NAND_OP_RESET:
        break;
    default:
        g_assert_not_reached();
    }
    at91_nand_set_ready(s, true);
}

static void at91_nand_sync_operation(AT91NANDState *s)
{
    /*
     * A guest can poll the MMIO data port in one uninterrupted TCG slice.
     * Complete an elapsed virtual-clock deadline on that access as well as
     * from the timer callback, so the status loop observes the same deadline.
     */
    if (s->pending_operation != NAND_OP_NONE &&
        timer_pending(s->operation_timer) &&
        timer_expire_time_ns(s->operation_timer) <=
        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL)) {
        timer_del(s->operation_timer);
        at91_nand_complete_operation(s);
    }
}

static int64_t at91_nand_reset_time_ns(const AT91NANDState *s)
{
    switch (s->pending_operation) {
    case NAND_OP_PAGE_READ:
    case NAND_OP_PARAMETER_READ:
    case NAND_OP_GET_FEATURES:
        return NAND_RESET_READ_NS;
    case NAND_OP_PAGE_PROGRAM:
        return NAND_RESET_PROGRAM_NS;
    case NAND_OP_BLOCK_ERASE:
        return NAND_RESET_ERASE_NS;
    default:
        return NAND_RESET_IDLE_NS;
    }
}

static bool at91_nand_data_output_command(uint8_t command)
{
    switch (command) {
    case NAND_CMD_READ0:
    case NAND_CMD_READ_PARAM:
    case NAND_CMD_GET_FEATURES:
        return true;
    default:
        return false;
    }
}

static bool at91_nand_status_command(uint8_t command)
{
    return command == NAND_CMD_STATUS || command == NAND_CMD_ENH_STATUS;
}

static bool at91_nand_command_or_suspended(const AT91NANDState *s,
                                           uint8_t command)
{
    return s->command == command ||
           (at91_nand_status_command(s->command) &&
            s->previous_command == command);
}

static void at91_nand_command(AT91NANDState *s, uint8_t command)
{
    uint8_t previous = s->command;
    uint8_t status_return = s->previous_command;
    int64_t reset_time_ns;

    if (s->pending_operation != NAND_OP_NONE &&
        !at91_nand_status_command(command) && command != NAND_CMD_RESET) {
        return;
    }
    if (s->pending_operation == NAND_OP_RESET &&
        command == NAND_CMD_ENH_STATUS) {
        return;
    }
    if (s->pending_operation == NAND_OP_RESET && command == NAND_CMD_RESET) {
        return;
    }
    if (command != NAND_CMD_ENH_STATUS) {
        s->enhanced_status_address_len = 0;
    }

    switch (command) {
    case NAND_CMD_READ0:
        /* 00h re-enables data output after an intervening status read. */
        if (at91_nand_status_command(previous) &&
            at91_nand_data_output_command(status_return)) {
            command = status_return;
            break;
        }
        s->address_len = 0;
        s->data_pos = 0;
        s->data_len = 0;
        break;
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
        s->current_page = UINT32_MAX;
        s->program_column = 0;
        s->program_pos = UINT32_MAX;
        memset(s->program, 0xff, sizeof(s->program));
        break;
    case NAND_CMD_RANDOM_INPUT:
        if (previous == NAND_CMD_PROGRAM_START) {
            if (s->current_page == UINT32_MAX) {
                s->current_page = at91_nand_row(s, true);
            }
            s->address_len = 0;
            s->program_pos = UINT32_MAX;
            command = NAND_CMD_PROGRAM_START;
        } else {
            qemu_log_mask(LOG_GUEST_ERROR,
                          TYPE_AT91_NAND ": random data input outside "
                          "a program operation\n");
            command = previous;
        }
        break;
    case NAND_CMD_RANDOM_READ:
        s->address_len = 0;
        break;
    case NAND_CMD_READ_START:
        if (previous == NAND_CMD_READ0) {
            at91_nand_start_operation(s, NAND_OP_PAGE_READ,
                                     NAND_READ_TIME_NS);
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
            int64_t delay_ns =
                (s->features[NAND_FEATURE_CONFIGURATION][0] &
                 NAND_FEATURE_RANDEN) ? NAND_RANDOM_PROGRAM_TIME_NS :
                                       NAND_PROGRAM_TIME_NS;

            at91_nand_start_operation(s, NAND_OP_PAGE_PROGRAM,
                                     delay_ns);
        }
        break;
    case NAND_CMD_ERASE_START:
        if (previous == NAND_CMD_ERASE) {
            at91_nand_start_operation(s, NAND_OP_BLOCK_ERASE,
                                     NAND_ERASE_TIME_NS);
        }
        break;
    case NAND_CMD_STATUS:
        break;
    case NAND_CMD_ENH_STATUS:
        s->enhanced_status_address_len = 0;
        break;
    case NAND_CMD_RESET:
        reset_time_ns = at91_nand_reset_time_ns(s);
        timer_del(s->operation_timer);
        s->pending_operation = NAND_OP_NONE;
        s->status = NAND_STATUS_WP;
        s->address_len = 0;
        memset(s->plane_status, 0, sizeof(s->plane_status));
        s->data_pos = 0;
        s->data_len = 0;
        s->current_page = UINT32_MAX;
        s->program_column = 0;
        s->program_pos = 0;
        memset(s->program, 0xff, sizeof(s->program));
        at91_nand_start_operation(s, NAND_OP_RESET, reset_time_ns);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      TYPE_AT91_NAND ": unknown command 0x%02x\n", command);
        break;
    }
    if (!at91_nand_status_command(command) ||
        !at91_nand_status_command(previous)) {
        s->previous_command = previous;
    }
    s->command = command;
}

static void at91_nand_address(AT91NANDState *s, uint8_t address)
{
    if (s->command == NAND_CMD_ENH_STATUS) {
        if (s->enhanced_status_address_len <
            ARRAY_SIZE(s->enhanced_status_address)) {
            s->enhanced_status_address[s->enhanced_status_address_len++] =
                address;
        }
        return;
    }
    if (s->pending_operation != NAND_OP_NONE) {
        return;
    }
    if (s->address_len < ARRAY_SIZE(s->address)) {
        s->address[s->address_len++] = address;
    }
    if (s->address_len == 1) {
        if (s->command == NAND_CMD_READ_PARAM) {
            at91_nand_start_operation(s, NAND_OP_PARAMETER_READ,
                                     NAND_READ_TIME_NS);
        } else if (s->command == NAND_CMD_GET_FEATURES) {
            at91_nand_start_operation(s, NAND_OP_GET_FEATURES,
                                     NAND_FEATURE_TIME_NS);
        }
    }
}

static void at91_nand_data_write(AT91NANDState *s, uint8_t value)
{
    if (s->pending_operation != NAND_OP_NONE) {
        return;
    }
    if (s->command == NAND_CMD_PROGRAM_START) {
        if (s->program_pos == UINT32_MAX) {
            if (s->current_page == UINT32_MAX) {
                s->current_page = at91_nand_row(s, true);
            }
            s->program_column = at91_nand_column(s);
            s->program_pos = s->program_column;
        }
        if (s->program_pos < AT91_NAND_PAGE_TOTAL_SIZE) {
            uint32_t column = s->program_pos++;

            s->program[column] = value;
            if (s->pmecc) {
                at91_pmecc_transfer_byte(s->pmecc, true, value);
            }
        } else {
            s->status |= NAND_STATUS_FAIL;
        }
    } else if (s->command == NAND_CMD_SET_FEATURES) {
        if (!s->data_pos) {
            s->feature_address = s->address_len ? s->address[0] : 0;
        }
        if (s->data_pos < 4) {
            s->data[s->data_pos++] = value;
            if (s->data_pos == 4) {
                at91_nand_start_operation(s, NAND_OP_SET_FEATURES,
                                         NAND_FEATURE_TIME_NS);
            }
        }
    }
}

static uint8_t at91_nand_data_read(AT91NANDState *s)
{
    if (s->pending_operation != NAND_OP_NONE &&
        !at91_nand_status_command(s->command)) {
        return 0xff;
    }
    switch (s->command) {
    case NAND_CMD_STATUS:
        return s->status;
    case NAND_CMD_ENH_STATUS:
        if (s->enhanced_status_address_len ==
            ARRAY_SIZE(s->enhanced_status_address)) {
            uint32_t row = s->enhanced_status_address[0] |
                           s->enhanced_status_address[1] << 8 |
                           s->enhanced_status_address[2] << 16;

            return (s->status & NAND_STATUS_SHARED_MASK) |
                   s->plane_status[at91_nand_plane(row)];
        }
        return 0xff;
    case NAND_CMD_READ_ID:
        if (!s->data_len) {
            at91_nand_prepare_id(s);
        }
        break;
    case NAND_CMD_READ_PARAM:
        if (!s->data_len && s->address_len) {
            at91_nand_prepare_parameter_page(s);
        }
        break;
    case NAND_CMD_GET_FEATURES:
        if (!s->data_len && s->address_len) {
            at91_nand_prepare_features(s);
        }
        break;
    default:
        break;
    }

    if (s->data_pos < s->data_len) {
        uint32_t column = s->data_pos++;
        uint8_t value = s->data[column];

        if (s->command == NAND_CMD_READ0 && s->pmecc) {
            at91_pmecc_transfer_byte(s->pmecc, false, value);
        }
        return value;
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
    at91_nand_sync_operation(s);
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
    at91_nand_sync_operation(s);
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

static void at91_nand_reset_hold(Object *obj, ResetType type)
{
    AT91NANDState *s = AT91_NAND(obj);

    /* The external NAND is not connected to the SoC's core-reset net. */
    if (type == RESET_TYPE_WAKEUP) {
        return;
    }

    timer_del(s->operation_timer);
    s->command = NAND_CMD_READ0;
    s->previous_command = NAND_CMD_READ0;
    s->status = NAND_STATUS_TRUE_READY | NAND_STATUS_READY |
                NAND_STATUS_WP;
    s->pending_operation = NAND_OP_NONE;
    s->pending_page = 0;
    s->pending_column = 0;
    memset(s->address, 0, sizeof(s->address));
    s->address_len = 0;
    memset(s->enhanced_status_address, 0,
           sizeof(s->enhanced_status_address));
    s->enhanced_status_address_len = 0;
    memset(s->plane_status, 0, sizeof(s->plane_status));
    s->feature_address = 0;
    memset(s->features, 0, sizeof(s->features));
    s->current_page = 0;
    s->data_pos = 0;
    s->data_len = 0;
    s->program_column = 0;
    s->program_pos = 0;
    memset(s->data, 0xff, sizeof(s->data));
    memset(s->program, 0xff, sizeof(s->program));
    at91_nand_set_ready(s, true);
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

    timer_free(s->operation_timer);
    if (s->sparse_pages) {
        at91_nand_clear_sparse_pages(s);
        g_free(s->sparse_pages);
    }
}

static bool at91_nand_sparse_payload(AT91NANDState *s, size_t size,
                                     size_t *offset, Error **errp)
{
    if (size == AT91_NAND_PAGE_TOTAL_SIZE) {
        if (s->blk) {
            error_setg(errp, "full sparse NAND state requires no backend");
            return false;
        }
        *offset = 0;
        return true;
    }

    if (size == AT91_NAND_OOB_SIZE) {
        if (!s->blk || s->raw_backend) {
            error_setg(errp,
                       "sparse NAND OOB state requires a data-only backend");
            return false;
        }
        *offset = AT91_NAND_PAGE_SIZE;
        return true;
    }

    error_setg(errp, "invalid sparse NAND payload size %zu", size);
    return false;
}

static bool at91_nand_sparse_save(QEMUFile *f, void *pv, size_t size,
                                  const VMStateField *field,
                                  JSONWriter *vmdesc, Error **errp)
{
    AT91NANDState *s = pv;
    uint32_t count = 0;
    size_t offset;
    unsigned int page;

    if (!at91_nand_sparse_payload(s, size, &offset, errp)) {
        return false;
    }

    for (page = 0; page < AT91_NAND_NUM_PAGES; page++) {
        count += s->sparse_pages[page] != NULL;
    }

    qemu_put_be32(f, count);
    for (page = 0; page < AT91_NAND_NUM_PAGES; page++) {
        if (!s->sparse_pages[page]) {
            continue;
        }
        qemu_put_be32(f, page);
        qemu_put_buffer(f, s->sparse_pages[page] + offset, size);
    }
    return true;
}

static bool at91_nand_sparse_load(QEMUFile *f, void *pv, size_t size,
                                  const VMStateField *field, Error **errp)
{
    AT91NANDState *s = pv;
    uint32_t count, page, previous = 0;
    size_t offset;
    unsigned int i;

    if (!at91_nand_sparse_payload(s, size, &offset, errp)) {
        return false;
    }
    if (at91_nand_has_sparse_pages(s)) {
        error_setg(errp, "duplicate sparse NAND migration subsection");
        return false;
    }

    count = qemu_get_be32(f);
    if (qemu_file_get_error(f) < 0) {
        error_setg(errp, "truncated sparse NAND page count");
        return false;
    }
    if (count > AT91_NAND_NUM_PAGES) {
        error_setg(errp, "invalid sparse NAND page count %" PRIu32, count);
        return false;
    }

    for (i = 0; i < count; i++) {
        uint8_t *data;

        page = qemu_get_be32(f);
        if (qemu_file_get_error(f) < 0 ||
            page >= AT91_NAND_NUM_PAGES || (i && page <= previous)) {
            error_setg(errp, "invalid sparse NAND page index %" PRIu32,
                       page);
            goto fail;
        }

        data = g_malloc0(AT91_NAND_PAGE_TOTAL_SIZE);
        if (qemu_get_buffer(f, data + offset, size) != size) {
            g_free(data);
            error_setg(errp, "truncated sparse NAND page %" PRIu32, page);
            goto fail;
        }

        s->sparse_pages[page] = data;
        previous = page;
    }

    return true;

fail:
    at91_nand_clear_sparse_pages(s);
    return false;
}

static const VMStateInfo at91_nand_sparse_info = {
    .name = "at91-nand-sparse-pages",
    .load = at91_nand_sparse_load,
    .save = at91_nand_sparse_save,
};

static bool at91_nand_sparse_full_needed(void *opaque)
{
    AT91NANDState *s = opaque;

    return !s->blk && at91_nand_has_sparse_pages(s);
}

static bool at91_nand_sparse_oob_needed(void *opaque)
{
    AT91NANDState *s = opaque;

    return s->blk && !s->raw_backend && at91_nand_has_sparse_pages(s);
}

static const VMStateDescription at91_nand_sparse_full_vmstate = {
    .name = TYPE_AT91_NAND "/sparse-full-pages",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = at91_nand_sparse_full_needed,
    .fields = (const VMStateField[]) {
        {
            .name = "pages",
            .size = AT91_NAND_PAGE_TOTAL_SIZE,
            .info = &at91_nand_sparse_info,
            .flags = VMS_SINGLE,
            .offset = 0,
        },
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription at91_nand_sparse_oob_vmstate = {
    .name = TYPE_AT91_NAND "/sparse-oob-pages",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = at91_nand_sparse_oob_needed,
    .fields = (const VMStateField[]) {
        {
            .name = "pages",
            .size = AT91_NAND_OOB_SIZE,
            .info = &at91_nand_sparse_info,
            .flags = VMS_SINGLE,
            .offset = 0,
        },
        VMSTATE_END_OF_LIST()
    },
};

static bool at91_nand_operation_needed(void *opaque)
{
    AT91NANDState *s = opaque;

    return s->pending_operation != NAND_OP_NONE;
}

static bool at91_nand_operation_post_load(void *opaque, int version_id,
                                          Error **errp)
{
    AT91NANDState *s = opaque;

    if (s->pending_operation <= NAND_OP_NONE ||
        s->pending_operation > NAND_OP_RESET) {
        error_setg(errp, "invalid pending NAND operation %u",
                   s->pending_operation);
        goto fail;
    }
    if (!timer_pending(s->operation_timer)) {
        error_setg(errp, "pending NAND operation has no completion timer");
        goto fail;
    }
    if (s->pending_page > 0x00ffffff ||
        s->pending_column > UINT16_MAX) {
        error_setg(errp,
                   "invalid pending NAND address %" PRIu32 "/%" PRIu32,
                   s->pending_page, s->pending_column);
        goto fail;
    }

    return true;

fail:
    timer_del(s->operation_timer);
    s->pending_operation = NAND_OP_NONE;
    return false;
}

static const VMStateDescription at91_nand_operation_vmstate = {
    .name = TYPE_AT91_NAND "/operation",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = at91_nand_operation_needed,
    .post_load_errp = at91_nand_operation_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(pending_operation, AT91NANDState),
        VMSTATE_UINT32(pending_page, AT91NANDState),
        VMSTATE_UINT32(pending_column, AT91NANDState),
        VMSTATE_TIMER_PTR(operation_timer, AT91NANDState),
        VMSTATE_END_OF_LIST()
    },
};

static bool at91_nand_enhanced_status_needed(void *opaque)
{
    AT91NANDState *s = opaque;

    return s->command == NAND_CMD_ENH_STATUS ||
           s->plane_status[0] || s->plane_status[1];
}

static bool at91_nand_enhanced_status_post_load(void *opaque,
                                                int version_id,
                                                Error **errp)
{
    AT91NANDState *s = opaque;

    if (s->enhanced_status_address_len >
        ARRAY_SIZE(s->enhanced_status_address) ||
        (s->command != NAND_CMD_ENH_STATUS &&
         s->enhanced_status_address_len) ||
        (s->plane_status[0] & ~NAND_STATUS_PLANE_MASK) ||
        (s->plane_status[1] & ~NAND_STATUS_PLANE_MASK)) {
        error_setg(errp, "invalid NAND Enhanced Status state");
        timer_del(s->operation_timer);
        s->pending_operation = NAND_OP_NONE;
        return false;
    }
    return true;
}

static const VMStateDescription at91_nand_enhanced_status_vmstate = {
    .name = TYPE_AT91_NAND "/enhanced-status",
    .version_id = 2,
    .minimum_version_id = 1,
    .needed = at91_nand_enhanced_status_needed,
    .post_load_errp = at91_nand_enhanced_status_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(enhanced_status_address, AT91NANDState, 3),
        VMSTATE_UINT8(enhanced_status_address_len, AT91NANDState),
        VMSTATE_UINT8_ARRAY_V(plane_status, AT91NANDState, 2, 2),
        VMSTATE_END_OF_LIST()
    },
};

static bool at91_nand_pre_load(void *opaque, Error **errp)
{
    AT91NANDState *s = opaque;

    at91_nand_clear_sparse_pages(s);
    timer_del(s->operation_timer);
    s->pending_operation = NAND_OP_NONE;
    s->pending_page = 0;
    s->pending_column = 0;
    memset(s->enhanced_status_address, 0,
           sizeof(s->enhanced_status_address));
    s->enhanced_status_address_len = 0;
    memset(s->plane_status, 0, sizeof(s->plane_status));
    return true;
}

static bool at91_nand_pending_operation_valid(const AT91NANDState *s)
{
    switch (s->pending_operation) {
    case NAND_OP_NONE:
        return true;
    case NAND_OP_PAGE_READ:
        return at91_nand_command_or_suspended(s, NAND_CMD_READ0);
    case NAND_OP_PARAMETER_READ:
        return s->address_len >= 1 &&
               at91_nand_command_or_suspended(s, NAND_CMD_READ_PARAM);
    case NAND_OP_GET_FEATURES:
        return s->address_len >= 1 &&
               at91_nand_command_or_suspended(s, NAND_CMD_GET_FEATURES);
    case NAND_OP_SET_FEATURES:
        return s->address_len >= 1 && s->data_pos == 4 &&
               at91_nand_command_or_suspended(s, NAND_CMD_SET_FEATURES);
    case NAND_OP_PAGE_PROGRAM:
        return at91_nand_command_or_suspended(s, NAND_CMD_PAGE_PROGRAM);
    case NAND_OP_BLOCK_ERASE:
        return at91_nand_command_or_suspended(s, NAND_CMD_ERASE_START);
    case NAND_OP_RESET:
        return s->command != NAND_CMD_ENH_STATUS &&
               at91_nand_command_or_suspended(s, NAND_CMD_RESET);
    default:
        return false;
    }
}

static bool at91_nand_post_load(void *opaque, int version_id, Error **errp)
{
    AT91NANDState *s = opaque;

    if (version_id < 4 && s->command == NAND_CMD_PROGRAM_START) {
        /* Older senders did not track the row of an active program. */
        s->current_page = UINT32_MAX;
    }

    if (s->address_len > ARRAY_SIZE(s->address)) {
        error_setg(errp, "invalid NAND address length %u", s->address_len);
        goto fail;
    }
    if (s->data_pos > sizeof(s->data) || s->data_len > sizeof(s->data)) {
        error_setg(errp, "invalid NAND data cursor %" PRIu32 "/%" PRIu32,
                   s->data_pos, s->data_len);
        goto fail;
    }
    if (s->command == NAND_CMD_SET_FEATURES && s->data_pos > 4) {
        error_setg(errp, "invalid NAND Set Features cursor %" PRIu32,
                   s->data_pos);
        goto fail;
    }
    if (s->program_column > UINT16_MAX ||
        (s->program_pos != UINT32_MAX && s->program_pos > UINT16_MAX)) {
        error_setg(errp, "invalid NAND program cursor %" PRIu32 "/%" PRIu32,
                   s->program_column, s->program_pos);
        goto fail;
    }
    if (s->current_page != UINT32_MAX && s->current_page > 0x00ffffff) {
        error_setg(errp, "invalid NAND current page %" PRIu32,
                   s->current_page);
        goto fail;
    }
    if (s->raw_backend && at91_nand_has_sparse_pages(s)) {
        error_setg(errp, "raw NAND backend has unexpected sparse state");
        goto fail;
    }
    if (!at91_nand_pending_operation_valid(s)) {
        error_setg(errp,
                   "inconsistent pending NAND operation %u for command 0x%02x",
                   s->pending_operation, s->command);
        goto fail;
    }

    /*
     * Versions 1 and 2 staged an in-flight Set Features transaction in the
     * feature array itself.  Recover those bytes so P4 can atomically commit
     * the transaction using the version 3 representation.
     */
    if (version_id < 3 && s->command == NAND_CMD_SET_FEATURES &&
        s->data_pos && s->data_pos < 4) {
        memcpy(s->data, s->features[s->feature_address], s->data_pos);
    }

    at91_nand_set_ready(s, s->pending_operation == NAND_OP_NONE);
    return true;

fail:
    timer_del(s->operation_timer);
    s->pending_operation = NAND_OP_NONE;
    at91_nand_set_ready(s, true);
    return false;
}

/* External backend data is shared storage; device-owned sparse state moves. */
static const VMStateDescription at91_nand_vmstate = {
    .name = TYPE_AT91_NAND,
    .version_id = 4,
    .minimum_version_id = 1,
    .pre_load_errp = at91_nand_pre_load,
    .post_load_errp = at91_nand_post_load,
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
    .subsections = (const VMStateDescription * const []) {
        &at91_nand_operation_vmstate,
        &at91_nand_enhanced_status_vmstate,
        &at91_nand_sparse_full_vmstate,
        &at91_nand_sparse_oob_vmstate,
        NULL
    },
};

static const Property at91_nand_properties[] = {
    DEFINE_PROP_DRIVE("drive", AT91NANDState, blk),
    DEFINE_PROP_LINK("pmecc", AT91NANDState, pmecc, TYPE_AT91_PMECC,
                     AT91PMECCState *),
};

static void at91_nand_init(Object *obj)
{
    AT91NANDState *s = AT91_NAND(obj);

    s->selected = true;
    memory_region_init_io(&s->mmio, obj, &at91_nand_ops, s,
                          TYPE_AT91_NAND, AT91_NAND_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    s->operation_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                      at91_nand_complete_operation, s);
    qdev_init_gpio_out_named(DEVICE(obj), &s->ready,
                             AT91_NAND_GPIO_READY, 1);
    qdev_init_gpio_in_named(DEVICE(obj), at91_nand_nce,
                            AT91_NAND_GPIO_NCE, 1);
}

static void at91_nand_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    dc->desc = "Macronix MX30LF4G28AD raw NAND on AT91 SMC";
    dc->realize = at91_nand_realize;
    dc->vmsd = &at91_nand_vmstate;
    device_class_set_props(dc, at91_nand_properties);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
    rc->phases.hold = at91_nand_reset_hold;
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
