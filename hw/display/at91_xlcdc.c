/*
 * Microchip SAM9X7 XLCDC display controller
 *
 * Register interface as used by Linux drivers/mfd/atmel-hlcdc.c and
 * drivers/gpu/drm/atmel-hlcdc for "microchip,sam9x75-xlcdc":
 *
 *  - LCDC_CFG0..6, EN/DIS/SR with the CLKSTS, LCDSTS, DISPSTS, CMSTS and
 *    (inverted) SDSTS handshake, SIPSTS always clear
 *  - IER/IDR/IMR/ISR with the start-of-frame interrupt, ISR read-to-clear
 *  - four layers (base, two overlays, high-end overlay) at 0x60, 0x160,
 *    0x260 and 0x360: layer IER/IDR/IMR/ISR, ENR, FBA and CFGn
 *
 * While the display is enabled, every frame fetches the base layer from
 * memory by DMA, line by line, as the silicon does. There is no host
 * display output: the point is the memory traffic (for example for
 * "-d dma_coherency"). A checksum of the last frame is traced.
 *
 * Not modelled: overlay composition, CLUT, YUV, scaling, pixel timing
 * beyond one frame period, underrun.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/display/at91_xlcdc.h"
#include "migration/vmstate.h"
#include "system/dma.h"
#include "trace.h"

/* global registers */
#define LCDC_CFG(n)         ((n) * 4)
#define LCDC_EN             0x20
#define LCDC_DIS            0x24
#define LCDC_SR             0x28
#define LCDC_IER            0x2c
#define LCDC_IDR            0x30
#define LCDC_IMR            0x34
#define LCDC_ISR            0x38
#define LCDC_ATTRE          0x3c

#define EN_PIXEL_CLK        BIT(0)
#define EN_SYNC             BIT(1)
#define EN_DISP             BIT(2)
#define EN_PWM              BIT(3)
#define EN_SIP              BIT(4)
#define EN_SD               BIT(5)
#define EN_CM               BIT(6)
#define EN_FOLLOW           (EN_PIXEL_CLK | EN_SYNC | EN_DISP | EN_PWM | EN_CM)

#define ISR_SOF             BIT(0)
#define ISR_LAYER(n)        BIT(8 + (n))

/* layers */
#define NLAYERS             4
static const hwaddr layer_base[NLAYERS] = { 0x60, 0x160, 0x260, 0x360 };
#define LAYER_IER           0x00
#define LAYER_IDR           0x04
#define LAYER_IMR           0x08
#define LAYER_ISR           0x0c
#define LAYER_ENR           0x10
#define LAYER_FBA(p)        (0x18 + 4 * (p))
#define LAYER_CFG(n)        (0x1c + 4 * (n))

/* base layer config registers (see the Linux layer description) */
#define BASE_CFG_FORMAT     1
#define BASE_CFG_XSTRIDE    2
#define BASE_CFG_GENERAL    4
#define GENERAL_DMA         BIT(0)

#define FRAME_NS            (20 * SCALE_MS)     /* 50 Hz */

static uint32_t *reg(AT91XLCDCState *s, hwaddr addr)
{
    return &s->regs[addr / 4];
}

static int layer_of(hwaddr addr, hwaddr *off)
{
    for (int i = 0; i < NLAYERS; i++) {
        if (addr >= layer_base[i] && addr < layer_base[i] + 0x100) {
            *off = addr - layer_base[i];
            return i;
        }
    }
    return -1;
}

static void xlcdc_update_irq(AT91XLCDCState *s)
{
    uint32_t pending = s->isr;

    for (int i = 0; i < NLAYERS; i++) {
        if (*reg(s, layer_base[i] + LAYER_ISR) &
            *reg(s, layer_base[i] + LAYER_IMR)) {
            pending |= ISR_LAYER(i);
        }
    }
    qemu_set_irq(s->irq, !!(pending & s->imr));
}

static bool xlcdc_running(AT91XLCDCState *s)
{
    return (s->sr & (EN_PIXEL_CLK | EN_SYNC | EN_DISP)) ==
           (EN_PIXEL_CLK | EN_SYNC | EN_DISP);
}

/* bytes per pixel of the base layer's RGB mode, 0 if unsupported */
static unsigned int xlcdc_bpp(uint32_t format)
{
    if ((format & 3) != 0) {
        return 0;                       /* CLUT and YUV not modelled */
    }
    switch ((format >> 4) & 0xf) {
    case 0 ... 4:                       /* 4444, 565, 1555 */
        return 2;
    case 5 ... 8:                       /* 16/18-bit packed variants */
    case 10:                            /* RGB888 */
        return 3;
    case 9: case 11: case 12: case 13:  /* XRGB/ARGB/RGBA 8888 */
        return 4;
    default:
        return 0;
    }
}

static void xlcdc_fetch_base(AT91XLCDCState *s)
{
    hwaddr lb = layer_base[0];
    uint32_t cfg4 = *reg(s, LCDC_CFG(4));
    unsigned int w = (cfg4 & 0x7ff) + 1, h = ((cfg4 >> 16) & 0x7ff) + 1;
    unsigned int bpp = xlcdc_bpp(*reg(s, lb + LAYER_CFG(BASE_CFG_FORMAT)));
    int32_t xstride = *reg(s, lb + LAYER_CFG(BASE_CFG_XSTRIDE));
    dma_addr_t addr = *reg(s, lb + LAYER_FBA(0));
    g_autofree uint8_t *line = NULL;
    uint32_t sum = 0;

    if (!(*reg(s, lb + LAYER_ENR) & 1) ||
        !(*reg(s, lb + LAYER_CFG(BASE_CFG_GENERAL)) & GENERAL_DMA)) {
        return;
    }
    if (!bpp) {
        qemu_log_mask(LOG_UNIMP, "at91-xlcdc: base layer format 0x%x\n",
                      *reg(s, lb + LAYER_CFG(BASE_CFG_FORMAT)));
        return;
    }

    line = g_malloc(w * bpp);
    for (unsigned int y = 0; y < h; y++) {
        if (dma_memory_read(&s->dma_as, addr, line, w * bpp,
                            MEMTXATTRS_UNSPECIFIED) != MEMTX_OK) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "at91-xlcdc: bad framebuffer address 0x%" PRIx64
                          "\n", (uint64_t)addr);
            return;
        }
        for (unsigned int i = 0; i < w * bpp; i++) {
            sum = sum * 31 + line[i];
        }
        addr += w * bpp + xstride;
    }
    s->last_sum = sum;
    trace_at91_xlcdc_frame(s->frames, *reg(s, lb + LAYER_FBA(0)), w, h,
                           bpp, sum);
}

static void xlcdc_frame(void *opaque)
{
    AT91XLCDCState *s = opaque;

    if (!xlcdc_running(s)) {
        return;
    }
    xlcdc_fetch_base(s);
    s->frames++;
    s->isr |= ISR_SOF;
    xlcdc_update_irq(s);
    timer_mod(s->frame_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + FRAME_NS);
}

static void xlcdc_kick(AT91XLCDCState *s)
{
    if (xlcdc_running(s) && !timer_pending(s->frame_timer)) {
        timer_mod(s->frame_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + FRAME_NS);
    } else if (!xlcdc_running(s)) {
        timer_del(s->frame_timer);
    }
}

static uint64_t xlcdc_read(void *opaque, hwaddr addr, unsigned size)
{
    AT91XLCDCState *s = opaque;
    hwaddr off;
    int l;
    uint32_t v;

    switch (addr) {
    case LCDC_EN:
    case LCDC_DIS:
    case LCDC_IER:
    case LCDC_IDR:
    case LCDC_ATTRE:
        return 0;
    case LCDC_SR:
        return s->sr;
    case LCDC_IMR:
        return s->imr;
    case LCDC_ISR:
        v = s->isr;
        for (l = 0; l < NLAYERS; l++) {
            if (*reg(s, layer_base[l] + LAYER_ISR)) {
                v |= ISR_LAYER(l);
            }
        }
        s->isr = 0;
        xlcdc_update_irq(s);
        return v;
    }

    l = layer_of(addr, &off);
    if (l >= 0) {
        switch (off) {
        case LAYER_IER:
        case LAYER_IDR:
            return 0;
        case LAYER_ISR:
            v = *reg(s, addr);
            *reg(s, addr) = 0;
            xlcdc_update_irq(s);
            return v;
        }
    }
    return *reg(s, addr);
}

static void xlcdc_write(void *opaque, hwaddr addr, uint64_t val,
                        unsigned size)
{
    AT91XLCDCState *s = opaque;
    hwaddr off;
    int l;

    switch (addr) {
    case LCDC_EN:
        s->sr |= val & EN_FOLLOW;
        if (val & EN_SD) {
            s->sr &= ~EN_SD;
        }
        xlcdc_kick(s);
        return;
    case LCDC_DIS:
        s->sr &= ~(val & EN_FOLLOW);
        if (val & EN_SD) {
            s->sr |= EN_SD;
        }
        xlcdc_kick(s);
        return;
    case LCDC_SR:
    case LCDC_IMR:
    case LCDC_ISR:
        return;
    case LCDC_IER:
        s->imr |= val;
        xlcdc_update_irq(s);
        return;
    case LCDC_IDR:
        s->imr &= ~val;
        xlcdc_update_irq(s);
        return;
    case LCDC_ATTRE:
        /* layer attributes take effect at once in this model */
        return;
    }

    l = layer_of(addr, &off);
    if (l >= 0) {
        switch (off) {
        case LAYER_IER:
            *reg(s, layer_base[l] + LAYER_IMR) |= val;
            xlcdc_update_irq(s);
            return;
        case LAYER_IDR:
            *reg(s, layer_base[l] + LAYER_IMR) &= ~val;
            xlcdc_update_irq(s);
            return;
        case LAYER_IMR:
        case LAYER_ISR:
            return;
        }
    }
    *reg(s, addr) = val;
}

static const MemoryRegionOps xlcdc_ops = {
    .read = xlcdc_read,
    .write = xlcdc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void xlcdc_reset(DeviceState *dev)
{
    AT91XLCDCState *s = AT91_XLCDC(dev);

    timer_del(s->frame_timer);
    memset(s->regs, 0, sizeof(s->regs));
    s->sr = EN_SD;          /* powered down until enabled */
    s->imr = 0;
    s->isr = 0;
    s->frames = 0;
    s->last_sum = 0;
}

static void xlcdc_realize(DeviceState *dev, Error **errp)
{
    AT91XLCDCState *s = AT91_XLCDC(dev);

    if (!s->dma_mr) {
        error_setg(errp, TYPE_AT91_XLCDC ": 'dma-memory' link not set");
        return;
    }
    address_space_init(&s->dma_as, s->dma_mr, TYPE_AT91_XLCDC "-dma");
    s->frame_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, xlcdc_frame, s);
}

static void xlcdc_unrealize(DeviceState *dev)
{
    AT91XLCDCState *s = AT91_XLCDC(dev);

    timer_free(s->frame_timer);
    address_space_destroy(&s->dma_as);
}

static void xlcdc_init(Object *obj)
{
    AT91XLCDCState *s = AT91_XLCDC(obj);

    memory_region_init_io(&s->iomem, obj, &xlcdc_ops, s, TYPE_AT91_XLCDC,
                          AT91_XLCDC_MMIO_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    object_property_add_uint64_ptr(obj, "frame-count", &s->frames,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint32_ptr(obj, "last-frame-checksum", &s->last_sum,
                                   OBJ_PROP_FLAG_READ);
}

static int xlcdc_post_load(void *opaque, int version_id)
{
    AT91XLCDCState *s = opaque;

    xlcdc_update_irq(s);
    xlcdc_kick(s);
    return 0;
}

static const VMStateDescription vmstate_xlcdc = {
    .name = TYPE_AT91_XLCDC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = xlcdc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, AT91XLCDCState, AT91_XLCDC_NREGS),
        VMSTATE_UINT32(sr, AT91XLCDCState),
        VMSTATE_UINT32(imr, AT91XLCDCState),
        VMSTATE_UINT32(isr, AT91XLCDCState),
        VMSTATE_UINT64(frames, AT91XLCDCState),
        VMSTATE_UINT32(last_sum, AT91XLCDCState),
        VMSTATE_END_OF_LIST()
    },
};

static const Property xlcdc_properties[] = {
    DEFINE_PROP_LINK("dma-memory", AT91XLCDCState, dma_mr,
                     TYPE_MEMORY_REGION, MemoryRegion *),
};

static void xlcdc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = xlcdc_realize;
    dc->unrealize = xlcdc_unrealize;
    dc->vmsd = &vmstate_xlcdc;
    dc->desc = "Microchip SAM9X7 XLCDC display controller";
    dc->user_creatable = false;
    device_class_set_legacy_reset(dc, xlcdc_reset);
    device_class_set_props(dc, xlcdc_properties);
}

static const TypeInfo xlcdc_info = {
    .name          = TYPE_AT91_XLCDC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AT91XLCDCState),
    .instance_init = xlcdc_init,
    .class_init    = xlcdc_class_init,
};

static void xlcdc_register_types(void)
{
    type_register_static(&xlcdc_info);
}

type_init(xlcdc_register_types)
