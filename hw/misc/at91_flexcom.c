/*
 * Microchip AT91 FLEXCOM wrapper
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/char/at91_usart.h"
#include "hw/core/irq.h"
#include "hw/i2c/at91_twi.h"
#include "hw/misc/at91_flexcom.h"
#include "hw/ssi/at91_spi.h"
#include "migration/vmstate.h"
#include "qemu/bitops.h"
#include "qemu/module.h"

#define FLEXCOM_MMIO_SIZE   0x200
#define FLEX_MR             0x00
#define FLEX_RHR            0x10
#define FLEX_THR            0x20

static void at91_flexcom_update_irq(AT91FlexcomState *s)
{
    bool level = false;

    switch (s->mr) {
    case AT91_FLEXCOM_MODE_USART:
        level = s->usart_irq_level;
        break;
    case AT91_FLEXCOM_MODE_SPI:
        level = s->spi && s->spi_irq_level;
        break;
    case AT91_FLEXCOM_MODE_TWI:
        level = s->twi_irq_level;
        break;
    default:
        break;
    }
    qemu_set_irq(s->irq, level);
}

static void at91_flexcom_update_dma(AT91FlexcomState *s)
{
    bool tx = false;
    bool rx = false;

    switch (s->mr) {
    case AT91_FLEXCOM_MODE_USART:
        tx = s->usart_tx_request_level;
        rx = s->usart_rx_request_level;
        break;
    case AT91_FLEXCOM_MODE_SPI:
        if (s->spi) {
            tx = s->spi_tx_request_level;
            rx = s->spi_rx_request_level;
        }
        break;
    case AT91_FLEXCOM_MODE_TWI:
        tx = s->twi_tx_request_level;
        rx = s->twi_rx_request_level;
        break;
    default:
        break;
    }
    /*
     * The request lines are levels shared by all three personalities.  Only
     * a change may reach the XDMAC: re-driving an already-high line when the
     * other direction changes looked like a second request while the first
     * was still pending, which the XDMAC reported as a request overflow and
     * Linux then aborted the transfer.
     */
    if (tx != s->tx_request_out) {
        s->tx_request_out = tx;
        qemu_set_irq(s->tx_request, tx);
    }
    if (rx != s->rx_request_out) {
        s->rx_request_out = rx;
        qemu_set_irq(s->rx_request, rx);
    }
}

static void at91_flexcom_update_mode(AT91FlexcomState *s)
{
    qemu_set_irq(s->usart_enabled, s->mr == AT91_FLEXCOM_MODE_USART);
    qemu_set_irq(s->spi_enabled,
                 s->spi && s->mr == AT91_FLEXCOM_MODE_SPI);
    qemu_set_irq(s->twi_enabled, s->mr == AT91_FLEXCOM_MODE_TWI);
    at91_flexcom_update_irq(s);
    at91_flexcom_update_dma(s);
}

static uint32_t at91_flexcom_merge_write(uint32_t old, hwaddr offset,
                                         uint64_t value, unsigned int size)
{
    unsigned int shift = (offset & 3) * 8;
    uint64_t mask = MAKE_64BIT_MASK(shift, size * 8);

    return (old & ~mask) | (((uint64_t)value << shift) & mask);
}

static uint64_t at91_flexcom_read(void *opaque, hwaddr offset,
                                  unsigned int size)
{
    AT91FlexcomState *s = AT91_FLEXCOM(opaque);
    hwaddr reg = offset & ~3;

    switch (reg) {
    case FLEX_MR:
        return s->mr >> ((offset & 3) * 8);
    case FLEX_RHR:
        if (s->mr == AT91_FLEXCOM_MODE_USART && s->usart) {
            return at91_usart_flexcom_read(s->usart, size);
        }
        if (s->mr == AT91_FLEXCOM_MODE_SPI && s->spi) {
            return at91_spi_flexcom_read(s->spi, size);
        }
        if (s->mr == AT91_FLEXCOM_MODE_TWI && s->twi) {
            return at91_twi_flexcom_read(s->twi, size);
        }
        return 0;
    case FLEX_THR:
        return s->thr >> ((offset & 3) * 8);
    default:
        return 0;
    }
}

static void at91_flexcom_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned int size)
{
    AT91FlexcomState *s = AT91_FLEXCOM(opaque);
    hwaddr reg = offset & ~3;

    switch (reg) {
    case FLEX_MR:
        s->mr = at91_flexcom_merge_write(s->mr, offset, value, size) & 0x3;
        at91_flexcom_update_mode(s);
        break;
    case FLEX_THR:
        s->thr = at91_flexcom_merge_write(s->thr, offset, value, size) &
                 0xffff;
        if (s->mr == AT91_FLEXCOM_MODE_USART && s->usart) {
            at91_usart_flexcom_write(s->usart, value, size);
        } else if (s->mr == AT91_FLEXCOM_MODE_SPI && s->spi) {
            at91_spi_flexcom_write(s->spi, value, size);
        } else if (s->mr == AT91_FLEXCOM_MODE_TWI && s->twi) {
            at91_twi_flexcom_write(s->twi, value, size);
        }
        break;
    default:
        break;
    }
}

static const MemoryRegionOps at91_flexcom_ops = {
    .read = at91_flexcom_read,
    .write = at91_flexcom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = false,
    },
};

static void at91_flexcom_set_usart_irq(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->usart_irq_level = level;
    at91_flexcom_update_irq(s);
}

static void at91_flexcom_set_spi_irq(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->spi_irq_level = level;
    at91_flexcom_update_irq(s);
}

static void at91_flexcom_set_twi_irq(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->twi_irq_level = level;
    at91_flexcom_update_irq(s);
}

static void at91_flexcom_set_usart_tx_request(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->usart_tx_request_level = level;
    at91_flexcom_update_dma(s);
}

static void at91_flexcom_set_usart_rx_request(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->usart_rx_request_level = level;
    at91_flexcom_update_dma(s);
}

static void at91_flexcom_set_spi_tx_request(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->spi_tx_request_level = level;
    at91_flexcom_update_dma(s);
}

static void at91_flexcom_set_spi_rx_request(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->spi_rx_request_level = level;
    at91_flexcom_update_dma(s);
}

static void at91_flexcom_set_twi_tx_request(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->twi_tx_request_level = level;
    at91_flexcom_update_dma(s);
}

static void at91_flexcom_set_twi_rx_request(void *opaque, int n, int level)
{
    AT91FlexcomState *s = opaque;

    s->twi_rx_request_level = level;
    at91_flexcom_update_dma(s);
}

static void at91_flexcom_reset(DeviceState *dev)
{
    AT91FlexcomState *s = AT91_FLEXCOM(dev);

    /* FLEXCOM comes up in USART mode on SAM9X7. */
    s->mr = AT91_FLEXCOM_MODE_USART;
    s->thr = 0;
    s->usart_irq_level = false;
    s->spi_irq_level = false;
    s->twi_irq_level = false;
    s->usart_tx_request_level = false;
    s->usart_rx_request_level = false;
    s->spi_tx_request_level = false;
    s->spi_rx_request_level = false;
    s->twi_tx_request_level = false;
    s->twi_rx_request_level = false;
    s->tx_request_out = false;
    s->rx_request_out = false;
    at91_flexcom_update_mode(s);
}

static int at91_flexcom_post_load(void *opaque, int version_id)
{
    AT91FlexcomState *s = AT91_FLEXCOM(opaque);

    /* Re-drive the request lines from the migrated personality levels. */
    s->tx_request_out = false;
    s->rx_request_out = false;
    at91_flexcom_update_mode(s);
    return 0;
}

static const VMStateDescription at91_flexcom_vmstate = {
    .name = TYPE_AT91_FLEXCOM,
    .version_id = 4,
    .minimum_version_id = 1,
    .post_load = at91_flexcom_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(mr, AT91FlexcomState),
        VMSTATE_UINT32_V(thr, AT91FlexcomState, 2),
        VMSTATE_BOOL_V(usart_irq_level, AT91FlexcomState, 2),
        VMSTATE_BOOL_V(spi_irq_level, AT91FlexcomState, 2),
        VMSTATE_BOOL_V(twi_irq_level, AT91FlexcomState, 2),
        VMSTATE_BOOL_V(usart_tx_request_level, AT91FlexcomState, 3),
        VMSTATE_BOOL_V(usart_rx_request_level, AT91FlexcomState, 3),
        VMSTATE_BOOL_V(spi_tx_request_level, AT91FlexcomState, 3),
        VMSTATE_BOOL_V(spi_rx_request_level, AT91FlexcomState, 3),
        VMSTATE_BOOL_V(twi_tx_request_level, AT91FlexcomState, 3),
        VMSTATE_BOOL_V(twi_rx_request_level, AT91FlexcomState, 3),
        VMSTATE_BOOL_V(tx_request_out, AT91FlexcomState, 4),
        VMSTATE_BOOL_V(rx_request_out, AT91FlexcomState, 4),
        VMSTATE_END_OF_LIST()
    },
};

static void at91_flexcom_init(Object *obj)
{
    AT91FlexcomState *s = AT91_FLEXCOM(obj);
    DeviceState *dev = DEVICE(obj);

    memory_region_init_io(&s->mmio, obj, &at91_flexcom_ops, s,
                          TYPE_AT91_FLEXCOM, FLEXCOM_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(dev, &s->usart_enabled, "usart-enabled", 1);
    qdev_init_gpio_out_named(dev, &s->spi_enabled, "spi-enabled", 1);
    qdev_init_gpio_out_named(dev, &s->twi_enabled, "twi-enabled", 1);
    qdev_init_gpio_out_named(dev, &s->tx_request, "tx-request", 1);
    qdev_init_gpio_out_named(dev, &s->rx_request, "rx-request", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_usart_irq,
                            "usart-irq", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_spi_irq, "spi-irq", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_twi_irq, "twi-irq", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_usart_tx_request,
                            "usart-tx-request", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_usart_rx_request,
                            "usart-rx-request", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_spi_tx_request,
                            "spi-tx-request", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_spi_rx_request,
                            "spi-rx-request", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_twi_tx_request,
                            "twi-tx-request", 1);
    qdev_init_gpio_in_named(dev, at91_flexcom_set_twi_rx_request,
                            "twi-rx-request", 1);
}

void at91_flexcom_set_children(AT91FlexcomState *s, AT91USARTState *usart,
                               AT91SPIState *spi, AT91TWIState *twi)
{
    s->usart = usart;
    s->spi = spi;
    s->twi = twi;
}

static void at91_flexcom_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Microchip AT91 FLEXCOM wrapper";
    dc->vmsd = &at91_flexcom_vmstate;
    device_class_set_legacy_reset(dc, at91_flexcom_reset);
}

static const TypeInfo at91_flexcom_info = {
    .name = TYPE_AT91_FLEXCOM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91FlexcomState),
    .instance_init = at91_flexcom_init,
    .class_init = at91_flexcom_class_init,
};

static void at91_flexcom_register_types(void)
{
    type_register_static(&at91_flexcom_info);
}

type_init(at91_flexcom_register_types)
