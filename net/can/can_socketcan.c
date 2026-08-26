/*
 * CAN c support to connect to the Linux host SocketCAN interfaces
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014-2018 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "chardev/char.h"
#include "qemu/sockets.h"
#include "qemu/error-report.h"
#include "net/can_emu.h"
#include "net/can_host.h"

#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include "qom/object.h"

#ifndef CANFD_FDF
#define CANFD_FDF 0x04
#endif

#ifndef DEBUG_CAN
#define DEBUG_CAN 0
#endif /*DEBUG_CAN*/

#define TYPE_CAN_HOST_SOCKETCAN "can-host-socketcan"
OBJECT_DECLARE_SIMPLE_TYPE(CanHostSocketCAN, CAN_HOST_SOCKETCAN)

struct CanHostSocketCAN {
    CanHostState       parent;
    char               *ifname;

    qemu_can_filter    *rfilter;
    int                rfilter_num;
    can_err_mask_t     err_mask;

    int                fd;
};

/* Check that QEMU and Linux kernel flags encoding and structure matches */
QEMU_BUILD_BUG_ON(QEMU_CAN_EFF_FLAG != CAN_EFF_FLAG);
QEMU_BUILD_BUG_ON(QEMU_CAN_RTR_FLAG != CAN_RTR_FLAG);
QEMU_BUILD_BUG_ON(QEMU_CAN_ERR_FLAG != CAN_ERR_FLAG);
QEMU_BUILD_BUG_ON(QEMU_CAN_INV_FILTER != CAN_INV_FILTER);
QEMU_BUILD_BUG_ON(QEMU_CAN_FRMF_BRS != CANFD_BRS);
QEMU_BUILD_BUG_ON(QEMU_CAN_FRMF_ESI != CANFD_ESI);

static void can_host_socketcan_display_msg(struct qemu_can_frame *msg)
{
    int i;
    FILE *logfile = qemu_log_trylock();

    if (logfile) {
        fprintf(logfile, "[cansocketcan]: %03X [%01d] %s %s",
                msg->can_id & QEMU_CAN_EFF_MASK,
                msg->can_dlc,
                msg->can_id & QEMU_CAN_EFF_FLAG ? "EFF" : "SFF",
                msg->can_id & QEMU_CAN_RTR_FLAG ? "RTR" : "DAT");

        for (i = 0; i < msg->can_dlc; i++) {
            fprintf(logfile, " %02X", msg->data[i]);
        }
        fprintf(logfile, "\n");
        qemu_log_unlock(logfile);
    }
}

static void can_host_socketcan_read(void *opaque)
{
    CanHostSocketCAN *c = opaque;
    CanHostState *ch = CAN_HOST(c);
    union {
        struct can_frame classic;
        struct canfd_frame fd;
    } host_frame;
    qemu_can_frame frame = { 0 };
    ssize_t len;

    len = read(c->fd, &host_frame.fd, sizeof(host_frame.fd));
    if (len < 0) {
        warn_report("CAN bus host read failed (%s)", strerror(errno));
        return;
    }

    if (len == CAN_MTU) {
        frame.can_id = host_frame.classic.can_id;
        frame.can_dlc = host_frame.classic.can_dlc;
        if (frame.can_dlc > CAN_MAX_DLEN) {
            warn_report("CAN bus host read invalid classic CAN length %u",
                        frame.can_dlc);
            return;
        }
        memcpy(frame.data, host_frame.classic.data, frame.can_dlc);
    } else if (len == CANFD_MTU && ch->bus_client.fd_mode) {
        frame.can_id = host_frame.fd.can_id;
        frame.can_dlc = host_frame.fd.len;
        if (frame.can_dlc > CANFD_MAX_DLEN) {
            warn_report("CAN bus host read invalid CAN FD length %u",
                        frame.can_dlc);
            return;
        }
        frame.flags = (host_frame.fd.flags & (CANFD_BRS | CANFD_ESI)) |
                      QEMU_CAN_FRMF_TYPE_FD;
        memcpy(frame.data, host_frame.fd.data, frame.can_dlc);
    } else {
        warn_report("CAN bus host read unexpected frame size %zd", len);
        return;
    }

    can_bus_client_send(&ch->bus_client, &frame, 1);

    if (DEBUG_CAN) {
        can_host_socketcan_display_msg(&frame);
    }
}

static bool can_host_socketcan_can_receive(CanBusClientState *client)
{
    return true;
}

static ssize_t can_host_socketcan_receive(CanBusClientState *client,
                            const qemu_can_frame *frames, size_t frames_cnt)
{
    CanHostState *ch = container_of(client, CanHostState, bus_client);
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(ch);
    union {
        struct can_frame classic;
        struct canfd_frame fd;
    } host_frame = { 0 };

    const void *buffer;
    size_t len;
    int res;

    if (c->fd < 0) {
        return -1;
    }
    if (!frames_cnt) {
        return 0;
    }
    if (frames->flags & QEMU_CAN_FRMF_TYPE_FD) {
        if (!ch->bus_client.fd_mode) {
            return 0;
        }
        if (frames->can_dlc > CANFD_MAX_DLEN) {
            warn_report("[cansocketcan]: invalid CAN FD length %u",
                        frames->can_dlc);
            return -1;
        }
        host_frame.fd.can_id = frames->can_id;
        host_frame.fd.len = frames->can_dlc;
        host_frame.fd.flags = (frames->flags &
                               (QEMU_CAN_FRMF_BRS | QEMU_CAN_FRMF_ESI)) |
                              CANFD_FDF;
        memcpy(host_frame.fd.data, frames->data, frames->can_dlc);
        buffer = &host_frame.fd;
        len = CANFD_MTU;
    } else {
        if (frames->can_dlc > CAN_MAX_DLEN) {
            warn_report("[cansocketcan]: invalid classic CAN length %u",
                        frames->can_dlc);
            return -1;
        }
        host_frame.classic.can_id = frames->can_id;
        host_frame.classic.can_dlc = frames->can_dlc;
        memcpy(host_frame.classic.data, frames->data, frames->can_dlc);
        buffer = &host_frame.classic;
        len = CAN_MTU;
    }

    res = write(c->fd, buffer, len);

    if (!res) {
        warn_report("[cansocketcan]: write message to host returns zero");
        return -1;
    }

    if (res != len) {
        if (res < 0) {
            warn_report("[cansocketcan]: write to host failed (%s)",
                        strerror(errno));
        } else {
            warn_report("[cansocketcan]: write to host truncated");
        }
        return -1;
    }

    return 1;
}

static void can_host_socketcan_disconnect(CanHostState *ch)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(ch);

    if (c->fd >= 0) {
        qemu_set_fd_handler(c->fd, NULL, NULL, c);
        close(c->fd);
        c->fd = -1;
    }

    g_free(c->rfilter);
    c->rfilter = NULL;
    c->rfilter_num = 0;
}

static CanBusClientInfo can_host_socketcan_bus_client_info = {
    .can_receive = can_host_socketcan_can_receive,
    .receive = can_host_socketcan_receive,
};

static void can_host_socketcan_connect(CanHostState *ch, Error **errp)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(ch);
    int s; /* can raw socket */
    int mtu;
    int enable_canfd = 1;
    struct sockaddr_can addr;
    struct ifreq ifr;

    if (!c->ifname) {
        error_setg(errp, "'if' property not set");
        return;
    }

    /* open socket */
    s = qemu_socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) {
        error_setg_errno(errp, errno, "failed to create CAN_RAW socket");
        return;
    }

    addr.can_family = AF_CAN;
    memset(&ifr.ifr_name, 0, sizeof(ifr.ifr_name));
    strcpy(ifr.ifr_name, c->ifname);
    /* check if the frame fits into the CAN netdevice */
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0) {
        error_setg_errno(errp, errno,
                         "SocketCAN host interface %s not available",
                         c->ifname);
        goto fail;
    }
    addr.can_ifindex = ifr.ifr_ifindex;

    if (ioctl(s, SIOCGIFMTU, &ifr) < 0) {
        error_setg_errno(errp, errno,
                         "SocketCAN host interface %s SIOCGIFMTU failed",
                         c->ifname);
        goto fail;
    }
    mtu = ifr.ifr_mtu;

    if (mtu >= CANFD_MTU) {
        /* interface is ok - try to switch the socket into CAN FD mode */
        if (setsockopt(s, SOL_CAN_RAW, CAN_RAW_FD_FRAMES,
                        &enable_canfd, sizeof(enable_canfd))) {
            warn_report("SocketCAN host interface %s enabling CAN FD failed",
                        c->ifname);
        } else {
            c->parent.bus_client.fd_mode = true;
        }
    }

    c->err_mask = 0xffffffff; /* Receive error frame. */
    setsockopt(s, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                   &c->err_mask, sizeof(c->err_mask));

    c->rfilter_num = 1;
    c->rfilter = g_new(struct qemu_can_filter, c->rfilter_num);

    /* Receive all data frame. If |= CAN_INV_FILTER no data. */
    c->rfilter[0].can_id = 0;
    c->rfilter[0].can_mask = 0;
    c->rfilter[0].can_mask &= ~CAN_ERR_FLAG;

    setsockopt(s, SOL_CAN_RAW, CAN_RAW_FILTER, c->rfilter,
               c->rfilter_num * sizeof(struct qemu_can_filter));

    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        error_setg_errno(errp, errno, "failed to bind to host interface %s",
                         c->ifname);
        goto fail;
    }

    c->fd = s;
    ch->bus_client.info = &can_host_socketcan_bus_client_info;
    qemu_set_fd_handler(c->fd, can_host_socketcan_read, NULL, c);
    return;

fail:
    close(s);
    g_free(c->rfilter);
    c->rfilter = NULL;
    c->rfilter_num = 0;
}

static char *can_host_socketcan_get_if(Object *obj, Error **errp)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(obj);

    return g_strdup(c->ifname);
}

static void can_host_socketcan_set_if(Object *obj, const char *value,
                                      Error **errp)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(obj);
    struct ifreq ifr;

    if (strlen(value) >= sizeof(ifr.ifr_name)) {
        error_setg(errp, "CAN interface name longer than %zd characters",
                   sizeof(ifr.ifr_name) - 1);
        return;
    }

    if (c->fd != -1) {
        error_setg(errp, "CAN interface already connected");
        return;
    }

    g_free(c->ifname);
    c->ifname = g_strdup(value);
}

static void can_host_socketcan_instance_init(Object *obj)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(obj);

    c->fd = -1;
}

static void can_host_socketcan_class_init(ObjectClass *klass,
                                          const void *class_data G_GNUC_UNUSED)
{
    CanHostClass *chc = CAN_HOST_CLASS(klass);

    object_class_property_add_str(klass, "if",
                                  can_host_socketcan_get_if,
                                  can_host_socketcan_set_if);
    chc->connect = can_host_socketcan_connect;
    chc->disconnect = can_host_socketcan_disconnect;
}

static void can_host_socketcan_finalize(Object *obj)
{
    CanHostSocketCAN *c = CAN_HOST_SOCKETCAN(obj);

    g_free(c->ifname);
}

static const TypeInfo can_host_socketcan_info = {
    .parent = TYPE_CAN_HOST,
    .name = TYPE_CAN_HOST_SOCKETCAN,
    .instance_size = sizeof(CanHostSocketCAN),
    .instance_init = can_host_socketcan_instance_init,
    .instance_finalize = can_host_socketcan_finalize,
    .class_init = can_host_socketcan_class_init,
};

static void can_host_register_types(void)
{
    type_register_static(&can_host_socketcan_info);
}

type_init(can_host_register_types);
