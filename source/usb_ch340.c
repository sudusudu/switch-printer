#include "usb_ch340.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Event  g_if_event;
static bool   g_usb_ready = false;
static Mutex  g_init_mutex;

Result ch340_init(void) {
    mutexLock(&g_init_mutex);
    if (g_usb_ready) { mutexUnlock(&g_init_mutex); return 0; }
    Result rc = usbHsInitialize();
    if (R_SUCCEEDED(rc)) {
        eventCreate(&g_if_event, false);
        g_usb_ready = true;
    }
    mutexUnlock(&g_init_mutex);
    return rc;
}

static Result ch340_write_reg(Ch340Device *dev, u8 reg, u8 val) {
    dev->tx_buf[0] = reg;
    dev->tx_buf[1] = val;
    return usbHsIfCtrlXfer(&dev->if_session,
        0x40, CH340_REQ_WRITE_REG,
        val, reg | 0x80, 0, dev->tx_buf);
}

static Result ch340_set_baud(Ch340Device *dev, u32 baud) {
    u32 div = (u32)(CH340_BAUD_FACTOR / (u64)baud);
    Result rc;
    rc = ch340_write_reg(dev, 0x13, (div >> 8) & 0xFF);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x12, div & 0xFF);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x14, (baud <= 1200) ? 7 : 0);
    return rc;
}

static Result ch340_init_chip(Ch340Device *dev) {
    Result rc;
    rc = ch340_write_reg(dev, 0x06, 0x00);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x1A, 0x00);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x18, 0x03);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x1C, 0xFF);
    if (R_FAILED(rc)) return rc;
    return ch340_set_baud(dev, CH340_BAUDRATE);
}

Result ch340_connect(Ch340Device *dev) {
    if (!g_usb_ready) return MAKERESULT(Module_Libnx, 1);
    memset(dev, 0, sizeof(Ch340Device));
    mutexInit(&dev->tx_mutex);
    mutexInit(&dev->rx_mutex);

    Result rc;
    u32 total = 0, avail = 0;

    rc = usbHsQueryAvailableInterfaces(NULL, 0x00, &total);
    if (R_FAILED(rc) && rc != MAKERESULT(Module_Libnx, 3)) return rc;

    UsbHsInterface interfaces[8];
    memset(interfaces, 0, sizeof(interfaces));
    rc = usbHsQueryAvailableInterfaces(interfaces, sizeof(interfaces), &avail);
    if (R_FAILED(rc)) return rc;
    if (avail == 0) return MAKERESULT(Module_Libnx, 4);

    bool found = false;
    for (u32 i = 0; i < avail && i < 8; i++) {
        UsbHsInterfaceInfo *info = (UsbHsInterfaceInfo*)(&interfaces[i].inf);
        u16 d_vid = info->device_desc.idVendor;
        u16 d_pid = info->device_desc.idProduct;

        if (d_vid == CH340_VID && d_pid == CH340_PID) {
            rc = usbHsAcquireUsbIf(&dev->if_session, &g_if_event, true,
                                   0, &interfaces[i].inf);
            if (R_FAILED(rc)) continue;
            found = true;
            break;
        }
    }

    if (!found) return MAKERESULT(Module_Libnx, 4);

    rc = ch340_init_chip(dev);
    if (R_FAILED(rc)) goto fail;

    dev->ep_out_addr = 0x02;
    dev->ep_in_addr  = 0x82;
    dev->max_packet_size = 64;

    rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_out, 16,
                           dev->max_packet_size, dev->ep_out_addr);
    if (R_FAILED(rc)) {
        dev->ep_out_addr = 0x01;
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_out, 16,
                               dev->max_packet_size, dev->ep_out_addr);
    }
    if (R_FAILED(rc)) goto fail;

    rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_in, 16,
                           dev->max_packet_size, dev->ep_in_addr);
    if (R_FAILED(rc)) {
        dev->ep_in_addr = 0x81;
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_in, 16,
                               dev->max_packet_size, dev->ep_in_addr);
    }
    if (R_FAILED(rc)) {
        usbHsEpClose(&dev->ep_out);
        goto fail;
    }

    dev->connected = true;
    return 0;

fail:
    usbHsIfClose(&dev->if_session);
    return rc;
}

void ch340_disconnect(Ch340Device *dev) {
    if (!dev->connected) return;
    usbHsEpClose(&dev->ep_in);
    usbHsEpClose(&dev->ep_out);
    usbHsIfClose(&dev->if_session);
    dev->connected = false;
}

Result ch340_send(Ch340Device *dev, const void *data, size_t len) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    if (len > 0x1000) len = 0x1000;
    mutexLock(&dev->tx_mutex);
    memcpy(dev->tx_buf, data, len);
    Result rc = usbHsEpPostBuffer(&dev->ep_out, dev->tx_buf, len);
    mutexUnlock(&dev->tx_mutex);
    return rc;
}

Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len,
                  u64 timeout_ms, size_t *received) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    if (max_len > 0x1000) max_len = 0x1000;
    *received = 0;

    Result rc = usbHsEpPostBufferAsync(&dev->ep_in, dev->rx_buf, max_len);
    if (R_FAILED(rc)) return rc;

    Event *ev = usbHsEpGetXferEvent(&dev->ep_in);
    if (!ev) return MAKERESULT(Module_Libnx, 3);

    rc = eventWait(ev, timeout_ms * 1000000ULL);
    if (R_FAILED(rc)) return rc;
    eventClear(ev);

    size_t transferred = max_len;
    if (transferred > 0 && transferred <= max_len) {
        mutexLock(&dev->rx_mutex);
        memcpy(buf, dev->rx_buf, transferred);
        mutexUnlock(&dev->rx_mutex);
        *received = transferred;
    }

    return 0;
}