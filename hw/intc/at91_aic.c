/*
 * Microchip AT91 Advanced Interrupt Controller (AIC5)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/intc/at91_aic.h"
#include "hw/core/irq.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/log.h"
#include "qemu/module.h"

enum {
    AIC_SSR   = 0x00,
    AIC_SMR   = 0x04,
    AIC_SVR   = 0x08,
    AIC_IVR   = 0x10,
    AIC_FVR   = 0x14,
    AIC_ISR   = 0x18,
    AIC_IPR0  = 0x20,
    AIC_IPR1  = 0x24,
    AIC_IPR2  = 0x28,
    AIC_IPR3  = 0x2c,
    AIC_IMR   = 0x30,
    AIC_CISR  = 0x34,
    AIC_EOICR = 0x38,
    AIC_SPU   = 0x3c,
    AIC_IECR  = 0x40,
    AIC_IDCR  = 0x44,
    AIC_ICCR  = 0x48,
    AIC_ISCR  = 0x4c,
    AIC_FFER  = 0x50,
    AIC_FFDR  = 0x54,
    AIC_FFSR  = 0x58,
    AIC_SVRRER = 0x60,
    AIC_SVRRDR = 0x64,
    AIC_SVRRSR = 0x68,
    AIC_DCR   = 0x6c,
    AIC_WPMR  = 0xe4,
    AIC_WPSR  = 0xe8,
};

#define AIC_MMIO_SIZE           0x100
#define AIC_SOURCE_MASK         0x7f
#define AIC_SMR_VALID_MASK      0x67
#define AIC_SMR_TYPE_SHIFT      5
#define AIC_SMR_TYPE_MASK       3
#define AIC_SMR_PRIORITY_MASK   7
#define AIC_DCR_PROT            BIT(0)
#define AIC_DCR_GMSK            BIT(1)
#define AIC_WPMR_WPEN           BIT(0)
#define AIC_WPMR_KEY            0x41494300
#define AIC_WPMR_KEY_MASK       0xffffff00
#define AIC_WPSR_WPVS           BIT(0)

static bool at91_aic_bit_test(const uint32_t *bitmap, unsigned int source)
{
    return bitmap[source / 32] & BIT(source % 32);
}

static void at91_aic_bit_set(uint32_t *bitmap, unsigned int source,
                             bool level)
{
    if (level) {
        bitmap[source / 32] |= BIT(source % 32);
    } else {
        bitmap[source / 32] &= ~BIT(source % 32);
    }
}

static bool at91_aic_is_external(unsigned int source)
{
    /* FIQ is always external; SAM9X7 additionally exposes source 31. */
    return source == 0 || source == 31;
}

static unsigned int at91_aic_source_type(AT91AIC5State *s,
                                         unsigned int source)
{
    return (s->source_mode[source] >> AIC_SMR_TYPE_SHIFT) &
           AIC_SMR_TYPE_MASK;
}

static bool at91_aic_is_edge(AT91AIC5State *s, unsigned int source)
{
    return at91_aic_source_type(s, source) & 1;
}

static bool at91_aic_source_pending(AT91AIC5State *s, unsigned int source)
{
    unsigned int type = at91_aic_source_type(s, source);
    bool level;

    if (type & 1) {
        return at91_aic_bit_test(s->edge_pending, source);
    }

    level = at91_aic_bit_test(s->input_level, source);
    if (at91_aic_is_external(source) && type == 0) {
        return !level;
    }

    /* Internal level-sensitive sources are wired active-high. */
    return level;
}

static uint32_t at91_aic_pending_word(AT91AIC5State *s, unsigned int bank)
{
    uint32_t pending = 0;
    unsigned int source;

    for (source = bank * 32; source < (bank + 1) * 32; source++) {
        if (at91_aic_source_pending(s, source)) {
            pending |= BIT(source % 32);
        }
    }

    return pending;
}

static int at91_aic_current_priority(AT91AIC5State *s)
{
    if (!s->stack_depth) {
        return -1;
    }

    return s->active_priority[s->stack_depth - 1];
}

static int at91_aic_irq_candidate(AT91AIC5State *s)
{
    int current_priority = at91_aic_current_priority(s);
    int best_priority = current_priority;
    int best_source = -1;
    unsigned int source;

    for (source = 1; source < AT91_AIC5_NUM_SOURCES; source++) {
        int priority;

        if (!at91_aic_bit_test(s->enabled, source) ||
            at91_aic_bit_test(s->fast_forcing, source) ||
            !at91_aic_source_pending(s, source)) {
            continue;
        }

        priority = s->source_mode[source] & AIC_SMR_PRIORITY_MASK;
        if (priority > best_priority) {
            best_priority = priority;
            best_source = source;
        }
    }

    return best_source;
}

static bool at91_aic_fiq_pending(AT91AIC5State *s)
{
    unsigned int source;

    for (source = 0; source < AT91_AIC5_NUM_SOURCES; source++) {
        if (!at91_aic_bit_test(s->enabled, source) ||
            !at91_aic_source_pending(s, source)) {
            continue;
        }

        if (source == 0 || at91_aic_bit_test(s->fast_forcing, source)) {
            return true;
        }
    }

    return false;
}

static uint32_t at91_aic_core_status(AT91AIC5State *s)
{
    uint32_t status = 0;

    if (s->debug_control & AIC_DCR_GMSK) {
        return 0;
    }

    if (at91_aic_fiq_pending(s)) {
        status |= BIT(0);
    }
    if (at91_aic_irq_candidate(s) >= 0) {
        status |= BIT(1);
    }

    return status;
}

static void at91_aic_update(AT91AIC5State *s)
{
    uint32_t status = at91_aic_core_status(s);

    qemu_set_irq(s->fiq, !!(status & BIT(0)));
    qemu_set_irq(s->irq, !!(status & BIT(1)));
}

static void at91_aic_set_irq(void *opaque, int source, int level)
{
    AT91AIC5State *s = opaque;
    bool old_level;
    bool trigger = false;
    unsigned int type;

    g_assert(source >= 0 && source < AT91_AIC5_NUM_SOURCES);

    old_level = at91_aic_bit_test(s->input_level, source);
    level = !!level;
    if (old_level == level) {
        return;
    }

    at91_aic_bit_set(s->input_level, source, level);
    type = at91_aic_source_type(s, source);
    if (type & 1) {
        if (at91_aic_is_external(source) && type == 1) {
            trigger = old_level && !level;
        } else {
            trigger = !old_level && level;
        }
        if (trigger) {
            at91_aic_bit_set(s->edge_pending, source, true);
        }
    }

    at91_aic_update(s);
}

static uint32_t at91_aic_irq_vector(AT91AIC5State *s, int source)
{
    if (source < 0) {
        return s->spurious_vector;
    }

    if (at91_aic_bit_test(s->source_index_return, source)) {
        return source;
    }

    return s->source_vector[source];
}

static void at91_aic_acknowledge_irq(AT91AIC5State *s, int source)
{
    unsigned int priority;

    if (source < 0) {
        return;
    }

    priority = s->source_mode[source] & AIC_SMR_PRIORITY_MASK;
    g_assert(s->stack_depth < AT91_AIC5_PRIORITY_LEVELS);
    s->active_source[s->stack_depth] = source;
    s->active_priority[s->stack_depth] = priority;
    s->stack_depth++;

    if (at91_aic_is_edge(s, source)) {
        at91_aic_bit_set(s->edge_pending, source, false);
    }

    at91_aic_update(s);
}

static uint32_t at91_aic_read_ivr(AT91AIC5State *s)
{
    int source = at91_aic_irq_candidate(s);
    uint32_t vector = at91_aic_irq_vector(s, source);

    if (s->debug_control & AIC_DCR_PROT) {
        /* The matching IVR write performs the deferred push and acknowledge. */
        s->protected_source = source;
    } else {
        at91_aic_acknowledge_irq(s, source);
    }

    return vector;
}

static uint32_t at91_aic_read_fvr(AT91AIC5State *s)
{
    uint32_t vector;

    if (!at91_aic_fiq_pending(s)) {
        return s->spurious_vector;
    }

    vector = s->source_vector[0];
    if (at91_aic_bit_test(s->enabled, 0) &&
        at91_aic_source_pending(s, 0) && at91_aic_is_edge(s, 0)) {
        at91_aic_bit_set(s->edge_pending, 0, false);
    }
    at91_aic_update(s);

    return vector;
}

static uint64_t at91_aic_read(void *opaque, hwaddr offset,
                              unsigned int size)
{
    AT91AIC5State *s = opaque;
    unsigned int source = s->selected_source;
    uint32_t value = 0;

    switch (offset) {
    case AIC_SSR:
        value = source;
        break;
    case AIC_SMR:
        value = s->source_mode[source];
        break;
    case AIC_SVR:
        value = s->source_vector[source];
        break;
    case AIC_IVR:
        value = at91_aic_read_ivr(s);
        break;
    case AIC_FVR:
        value = at91_aic_read_fvr(s);
        break;
    case AIC_ISR:
        if (s->stack_depth) {
            value = s->active_source[s->stack_depth - 1];
        }
        break;
    case AIC_IPR0:
    case AIC_IPR1:
    case AIC_IPR2:
    case AIC_IPR3:
        value = at91_aic_pending_word(s, (offset - AIC_IPR0) / 4);
        break;
    case AIC_IMR:
        value = at91_aic_bit_test(s->enabled, source);
        break;
    case AIC_CISR:
        value = at91_aic_core_status(s);
        break;
    case AIC_SPU:
        value = s->spurious_vector;
        break;
    case AIC_FFSR:
        value = at91_aic_bit_test(s->fast_forcing, source);
        break;
    case AIC_SVRRSR:
        value = at91_aic_bit_test(s->source_index_return, source);
        break;
    case AIC_DCR:
        value = s->debug_control;
        break;
    case AIC_WPMR:
        value = s->write_protection_mode;
        break;
    case AIC_WPSR:
        value = s->write_protection_status;
        s->write_protection_status = 0;
        break;
    case AIC_EOICR:
    case AIC_IECR:
    case AIC_IDCR:
    case AIC_ICCR:
    case AIC_ISCR:
    case AIC_FFER:
    case AIC_FFDR:
    case AIC_SVRRER:
    case AIC_SVRRDR:
        /* Write-only registers read as zero. */
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_AIC5, offset);
        break;
    }

    return value;
}

static bool at91_aic_write_is_protected(AT91AIC5State *s, hwaddr offset)
{
    if (!(s->write_protection_mode & AIC_WPMR_WPEN)) {
        return false;
    }

    switch (offset) {
    case AIC_SMR:
    case AIC_SVR:
    case AIC_SPU:
    case AIC_DCR:
        s->write_protection_status = (offset << 8) | AIC_WPSR_WPVS;
        return true;
    default:
        return false;
    }
}

static void at91_aic_write(void *opaque, hwaddr offset, uint64_t value,
                           unsigned int size)
{
    AT91AIC5State *s = opaque;
    unsigned int source = s->selected_source;
    uint32_t v = value;

    if (at91_aic_write_is_protected(s, offset)) {
        return;
    }

    switch (offset) {
    case AIC_SSR:
        s->selected_source = v & AIC_SOURCE_MASK;
        break;
    case AIC_SMR:
        s->source_mode[source] = v & AIC_SMR_VALID_MASK;
        at91_aic_update(s);
        break;
    case AIC_SVR:
        s->source_vector[source] = v;
        break;
    case AIC_EOICR:
        if (s->stack_depth) {
            s->stack_depth--;
        }
        at91_aic_update(s);
        break;
    case AIC_SPU:
        s->spurious_vector = v;
        break;
    case AIC_IECR:
        if (v & 1) {
            at91_aic_bit_set(s->enabled, source, true);
            at91_aic_update(s);
        }
        break;
    case AIC_IDCR:
        if (v & 1) {
            at91_aic_bit_set(s->enabled, source, false);
            at91_aic_update(s);
        }
        break;
    case AIC_ICCR:
        if ((v & 1) && at91_aic_is_edge(s, source)) {
            at91_aic_bit_set(s->edge_pending, source, false);
            at91_aic_update(s);
        }
        break;
    case AIC_ISCR:
        if ((v & 1) && at91_aic_is_edge(s, source)) {
            at91_aic_bit_set(s->edge_pending, source, true);
            at91_aic_update(s);
        }
        break;
    case AIC_FFER:
        if (v & 1) {
            at91_aic_bit_set(s->fast_forcing, source, true);
            at91_aic_update(s);
        }
        break;
    case AIC_FFDR:
        if (v & 1) {
            at91_aic_bit_set(s->fast_forcing, source, false);
            at91_aic_update(s);
        }
        break;
    case AIC_SVRRER:
        if (v & 1) {
            at91_aic_bit_set(s->source_index_return, source, true);
        }
        break;
    case AIC_SVRRDR:
        if (v & 1) {
            at91_aic_bit_set(s->source_index_return, source, false);
        }
        break;
    case AIC_DCR:
        s->debug_control = v & (AIC_DCR_PROT | AIC_DCR_GMSK);
        if (!(s->debug_control & AIC_DCR_PROT)) {
            s->protected_source = -1;
        }
        at91_aic_update(s);
        break;
    case AIC_WPMR:
        if ((v & AIC_WPMR_KEY_MASK) == AIC_WPMR_KEY) {
            s->write_protection_mode = v & AIC_WPMR_WPEN;
        }
        break;
    case AIC_IVR:
        if (s->debug_control & AIC_DCR_PROT) {
            int protected_source = s->protected_source;

            s->protected_source = -1;
            at91_aic_acknowledge_irq(s, protected_source);
        }
        /* IVR writes have no effect outside protection mode. */
        break;
    case AIC_FVR:
    case AIC_ISR:
    case AIC_IPR0:
    case AIC_IPR1:
    case AIC_IPR2:
    case AIC_IPR3:
    case AIC_IMR:
    case AIC_CISR:
    case AIC_FFSR:
    case AIC_SVRRSR:
    case AIC_WPSR:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_AIC5, offset);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid offset 0x%" HWADDR_PRIx "\n",
                      TYPE_AT91_AIC5, offset);
        break;
    }
}

static const MemoryRegionOps at91_aic_ops = {
    .read = at91_aic_read,
    .write = at91_aic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void at91_aic_reset(DeviceState *dev)
{
    AT91AIC5State *s = AT91_AIC5(dev);

    memset(s->source_mode, 0, sizeof(s->source_mode));
    memset(s->source_vector, 0, sizeof(s->source_vector));
    memset(s->input_level, 0, sizeof(s->input_level));
    memset(s->edge_pending, 0, sizeof(s->edge_pending));
    memset(s->enabled, 0, sizeof(s->enabled));
    memset(s->fast_forcing, 0, sizeof(s->fast_forcing));
    memset(s->source_index_return, 0, sizeof(s->source_index_return));
    memset(s->active_source, 0, sizeof(s->active_source));
    memset(s->active_priority, 0, sizeof(s->active_priority));

    /* External FIQ and IRQ pins idle high with their reset low-level mode. */
    at91_aic_bit_set(s->input_level, 0, true);
    at91_aic_bit_set(s->input_level, 31, true);

    s->stack_depth = 0;
    s->selected_source = 0;
    s->protected_source = -1;
    s->spurious_vector = 0;
    s->debug_control = 0;
    s->write_protection_mode = 0;
    s->write_protection_status = 0;
    at91_aic_update(s);
}

static int at91_aic_post_load(void *opaque, int version_id)
{
    AT91AIC5State *s = opaque;

    if (version_id < 2) {
        memset(s->source_index_return, 0, sizeof(s->source_index_return));
        s->protected_source = -1;
    }
    at91_aic_update(s);
    return 0;
}

static const VMStateDescription vmstate_at91_aic = {
    .name = TYPE_AT91_AIC5,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = at91_aic_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(source_mode, AT91AIC5State,
                             AT91_AIC5_NUM_SOURCES),
        VMSTATE_UINT32_ARRAY(source_vector, AT91AIC5State,
                             AT91_AIC5_NUM_SOURCES),
        VMSTATE_UINT32_ARRAY(input_level, AT91AIC5State,
                             AT91_AIC5_NUM_SOURCES / 32),
        VMSTATE_UINT32_ARRAY(edge_pending, AT91AIC5State,
                             AT91_AIC5_NUM_SOURCES / 32),
        VMSTATE_UINT32_ARRAY(enabled, AT91AIC5State,
                             AT91_AIC5_NUM_SOURCES / 32),
        VMSTATE_UINT32_ARRAY(fast_forcing, AT91AIC5State,
                             AT91_AIC5_NUM_SOURCES / 32),
        VMSTATE_UINT32_ARRAY_V(source_index_return, AT91AIC5State,
                               AT91_AIC5_NUM_SOURCES / 32, 2),
        VMSTATE_UINT8_ARRAY(active_source, AT91AIC5State,
                            AT91_AIC5_PRIORITY_LEVELS),
        VMSTATE_UINT8_ARRAY(active_priority, AT91AIC5State,
                            AT91_AIC5_PRIORITY_LEVELS),
        VMSTATE_UINT8(stack_depth, AT91AIC5State),
        VMSTATE_UINT8(selected_source, AT91AIC5State),
        VMSTATE_INT16_V(protected_source, AT91AIC5State, 2),
        VMSTATE_UINT32(spurious_vector, AT91AIC5State),
        VMSTATE_UINT32(debug_control, AT91AIC5State),
        VMSTATE_UINT32(write_protection_mode, AT91AIC5State),
        VMSTATE_UINT32(write_protection_status, AT91AIC5State),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_aic_init(Object *obj)
{
    AT91AIC5State *s = AT91_AIC5(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_aic_ops, s,
                          TYPE_AT91_AIC5, AIC_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->fiq);
    qdev_init_gpio_in(DEVICE(obj), at91_aic_set_irq,
                      AT91_AIC5_NUM_SOURCES);
}

static void at91_aic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 Advanced Interrupt Controller (AIC5)";
    dc->vmsd = &vmstate_at91_aic;
    device_class_set_legacy_reset(dc, at91_aic_reset);
}

static const TypeInfo at91_aic_info = {
    .name = TYPE_AT91_AIC5,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91AIC5State),
    .instance_init = at91_aic_init,
    .class_init = at91_aic_class_init,
};

static void at91_aic_register_types(void)
{
    type_register_static(&at91_aic_info);
}

type_init(at91_aic_register_types)
