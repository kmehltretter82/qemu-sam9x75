/*
 * Deterministic USB short-transfer test device for Linux ARM DMA testing.
 *
 * Copyright (c) 2026 Karl Mehltretter
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/usb/usb.h"
#include "migration/vmstate.h"
#include "desc.h"
#include "qom/object.h"

#define TYPE_USB_F008_TEST "usb-f008-test"
OBJECT_DECLARE_SIMPLE_TYPE(USBF008TestState, USB_F008_TEST)

#define F008_VENDOR_ID       0x46f4
#define F008_PRODUCT_ID      0xf008
#define F008_ISO_EP          2
#define F008_TRIGGER_ISO_EP  3
#define F008_TRIGGER_OUT_EP  4
#define F008_PACKET_SIZE     64

struct USBF008TestState {
    USBDevice dev;
    uint64_t packet_number;
    uint32_t pending_triggers;
};

enum {
    STR_MANUFACTURER = 1,
    STR_PRODUCT,
    STR_SERIAL,
};

static const USBDescStrings f008_strings = {
    [STR_MANUFACTURER] = "QEMU",
    [STR_PRODUCT] = "F008 short ISO IN test",
    [STR_SERIAL] = "F008-0001",
};

static USBDescEndpoint f008_endpoints[] = {
    {
        .bEndpointAddress = USB_DIR_IN | F008_ISO_EP,
        .bmAttributes = USB_ENDPOINT_XFER_ISOC,
        .wMaxPacketSize = F008_PACKET_SIZE,
        .bInterval = 1,
    }, {
        .bEndpointAddress = USB_DIR_IN | F008_TRIGGER_ISO_EP,
        .bmAttributes = USB_ENDPOINT_XFER_ISOC,
        .wMaxPacketSize = F008_PACKET_SIZE,
        .bInterval = 1,
    }, {
        .bEndpointAddress = USB_DIR_OUT | F008_TRIGGER_OUT_EP,
        .bmAttributes = USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize = F008_PACKET_SIZE,
        .bInterval = 1,
    },
};

static const USBDescIface f008_iface_full = {
    .bInterfaceNumber = 0,
    .bNumEndpoints = ARRAY_SIZE(f008_endpoints),
    .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
    .bInterfaceSubClass = 0xf0,
    .bInterfaceProtocol = 0x08,
    .eps = f008_endpoints,
};

static const USBDescIface f008_iface_high = {
    .bInterfaceNumber = 0,
    .bNumEndpoints = ARRAY_SIZE(f008_endpoints),
    .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
    .bInterfaceSubClass = 0xf0,
    .bInterfaceProtocol = 0x08,
    .eps = f008_endpoints,
};

static const USBDescDevice f008_device_full = {
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = 64,
    .bNumConfigurations = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .bmAttributes = USB_CFG_ATT_ONE,
            .bMaxPower = 25,
            .nif = 1,
            .ifs = &f008_iface_full,
        },
    },
};

static const USBDescDevice f008_device_high = {
    .bcdUSB = 0x0200,
    .bMaxPacketSize0 = 64,
    .bNumConfigurations = 1,
    .confs = (USBDescConfig[]) {
        {
            .bNumInterfaces = 1,
            .bConfigurationValue = 1,
            .bmAttributes = USB_CFG_ATT_ONE,
            .bMaxPower = 25,
            .nif = 1,
            .ifs = &f008_iface_high,
        },
    },
};

static const USBDesc f008_desc = {
    .id = {
        .idVendor = F008_VENDOR_ID,
        .idProduct = F008_PRODUCT_ID,
        .bcdDevice = 0x0001,
        .iManufacturer = STR_MANUFACTURER,
        .iProduct = STR_PRODUCT,
        .iSerialNumber = STR_SERIAL,
    },
    .full = &f008_device_full,
    .high = &f008_device_high,
    .str = f008_strings,
};

static void f008_handle_reset(USBDevice *dev)
{
    USBF008TestState *s = USB_F008_TEST(dev);

    s->packet_number = 0;
    s->pending_triggers = 0;
}

static void f008_handle_control(USBDevice *dev, USBPacket *p, int request,
                                int value, int index, int length,
                                uint8_t *data)
{
    int ret;

    ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
    if (ret < 0) {
        p->status = USB_RET_STALL;
    }
}

static void f008_handle_data(USBDevice *dev, USBPacket *p)
{
    static const unsigned int pattern[] = { 64, 17, 0, 31 };
    USBF008TestState *s = USB_F008_TEST(dev);
    uint8_t payload[F008_PACKET_SIZE];
    size_t len;
    size_t i;

    if (p->pid == USB_TOKEN_OUT && p->ep->nr == F008_TRIGGER_OUT_EP) {
        usb_packet_skip(p, p->iov.size);
        s->pending_triggers++;
        return;
    }

    if (p->pid != USB_TOKEN_IN ||
        (p->ep->nr != F008_ISO_EP &&
         p->ep->nr != F008_TRIGGER_ISO_EP)) {
        p->status = USB_RET_STALL;
        return;
    }

    if (p->ep->nr == F008_TRIGGER_ISO_EP) {
        if (!s->pending_triggers) {
            return;
        }
        s->pending_triggers--;
        len = MIN((size_t)17, p->iov.size);
        for (i = 0; i < len; i++) {
            payload[i] = (uint8_t)(0x70 + s->packet_number + i);
        }
        s->packet_number++;
        usb_packet_copy(p, payload, len);
        return;
    }

    len = MIN((size_t)pattern[s->packet_number % ARRAY_SIZE(pattern)],
              p->iov.size);
    for (i = 0; i < len; i++) {
        payload[i] = (uint8_t)(0x40 + s->packet_number + i);
    }
    s->packet_number++;
    usb_packet_copy(p, payload, len);
}

static void f008_realize(USBDevice *dev, Error **errp)
{
    usb_desc_create_serial(dev);
    usb_desc_init(dev);
}

static const VMStateDescription vmstate_f008 = {
    .name = TYPE_USB_F008_TEST,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_USB_DEVICE(dev, USBF008TestState),
        VMSTATE_UINT64(packet_number, USBF008TestState),
        VMSTATE_UINT32(pending_triggers, USBF008TestState),
        VMSTATE_END_OF_LIST()
    },
};

static void f008_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    USBDeviceClass *uc = USB_DEVICE_CLASS(klass);

    uc->product_desc = "F008 short ISO IN test";
    uc->usb_desc = &f008_desc;
    uc->realize = f008_realize;
    uc->handle_attach = usb_desc_attach;
    uc->handle_reset = f008_handle_reset;
    uc->handle_control = f008_handle_control;
    uc->handle_data = f008_handle_data;
    dc->vmsd = &vmstate_f008;
}

static const TypeInfo f008_info = {
    .name = TYPE_USB_F008_TEST,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBF008TestState),
    .class_init = f008_class_init,
};

static void f008_register_types(void)
{
    type_register_static(&f008_info);
}

type_init(f008_register_types)
