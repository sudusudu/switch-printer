#include "usb_ch340.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Mutex g_init_mutex;
static bool  g_usb_ready = false;

static void init_mutex_once(void) {
    static bool done = false;
    if (!done) { mutexInit(&g_init_mutex); done = true; }
}

Result ch340_init(void) {
    init_mutex_once();
    mutexLock(&g_init_mutex);
    if (g_usb_ready) { mutexUnlock(&g_init_mutex); return 0; }
    Result rc = usbHsInitialize();
    if (R_SUCCEEDED(rc)) g_usb_ready = true;
    mutexUnlock(&g_init_mutex);
    return rc;
}

static Result ch340_write_reg(Ch340Device *dev, u8 reg, u8 val) {
    dev->tx_buf[0] = reg;
    dev->tx_buf[1] = val;
    u32 transferred = 0;
    return usbHsIfCtrlXfer(&dev->if_session,
        0x40, CH340_REQ_WRITE_REG,
        val, reg | 0x80, 0,
        dev->tx_buf, &transferred);
}

static Result ch340_set_baud(Ch340Device *dev, u32 baud) {
    u32 div = (u32)(CH340_BAUD_FACTOR / (u64)baud);
    Result rc;
    rc = ch340_write_reg(dev, 0x13, (div >> 8) & 0xFF);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x12, div & 0xFF);
    if (R_FAILED(rc)) return rc;
    return ch340_write_reg(dev, 0x14, (baud <= 1200) ? 7 : 0);
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
    mutexInit(&dev->mutex);

    s32 total = 0;
    Result rc = usbHsQueryAvailableInterfaces(NULL, NULL, 0, &total);
    if (R_FAILED(rc)) return rc;
    if (total == 0) return MAKERESULT(Module_Libnx, 4);

    UsbHsInterface interfaces[8];
    memset(interfaces, 0, sizeof(interfaces));
    s32 found_count = 0;
    rc = usbHsQueryAvailableInterfaces(NULL, interfaces, sizeof(interfaces), &found_count);
    if (R_FAILED(rc)) return rc;

    bool found = false;
    s32 limit = (found_count < 8) ? found_count : 8;
    for (s32 i = 0; i < limit; i++) {
        if (interfaces[i].device_desc.idVendor != CH340_VID ||
            interfaces[i].device_desc.idProduct != CH340_PID) continue;
        rc = usbHsAcquireUsbIf(&dev->if_session, &interfaces[i]);
        if (R_SUCCEEDED(rc)) { found = true; break; }
    }
    if (!found) return MAKERESULT(Module_Libnx, 4);

    rc = ch340_init_chip(dev);
    if (R_FAILED(rc)) goto fail;

    struct usb_endpoint_descriptor *out_desc = NULL;
    struct usb_endpoint_descriptor *in_desc  = NULL;
    for (s32 i = 0; i < limit; i++) {
        for (int j = 0; j < 15; j++) {
            if (interfaces[i].inf.output_endpoint_descs[j].bEndpointAddress != 0 && !out_desc)
                out_desc = &interfaces[i].inf.output_endpoint_descs[j];
            if (interfaces[i].inf.input_endpoint_descs[j].bEndpointAddress != 0 && !in_desc)
                in_desc = &interfaces[i].inf.input_endpoint_descs[j];
        }
        if (out_desc && in_desc) break;
    }

    if (out_desc) {
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_out, 8, 0x1000, out_desc);
    } else {
        struct usb_endpoint_descriptor fake_out = {0};
        fake_out.bLength = 7;
        fake_out.bDescriptorType = 5;
        fake_out.bEndpointAddress = 0x02;
        fake_out.bmAttributes = 2;
        fake_out.wMaxPacketSize = 64;
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_out, 8, 0x1000, &fake_out);
    }
    if (R_FAILED(rc)) goto fail;

    if (in_desc) {
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_in, 8, 0x1000, in_desc);
    } else {
        struct usb_endpoint_descriptor fake_in = {0};
        fake_in.bLength = 7;
        fake_in.bDescriptorType = 5;
        fake_in.bEndpointAddress = 0x82;
        fake_in.bmAttributes = 2;
        fake_in.wMaxPacketSize = 64;
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_in, 8, 0x1000, &fake_in);
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
    dev->connected = false;
    usbHsEpClose(&dev->ep_in);
    usbHsEpClose(&dev->ep_out);
    usbHsIfClose(&dev->if_session);
}

Result ch340_send(Ch340Device *dev, const void *data, size_t len) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    if (len > 0x1000) len = 0x1000;
    mutexLock(&dev->mutex);
    memcpy(dev->tx_buf, data, len);
    u32 transferred = 0;
    Result rc = usbHsEpPostBuffer(&dev->ep_out, dev->tx_buf, len, &transferred);
    mutexUnlock(&dev->mutex);
    return rc;
}

Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len,
                  u64 timeout_ms, size_t *received) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    if (max_len > 0x1000) max_len = 0x1000;
    *received = 0;
    mutexLock(&dev->mutex);
    u32 transferred = 0;
    Result rc = usbHsEpPostBuffer(&dev->ep_in, dev->rx_buf, max_len, &transferred);
    if (R_SUCCEEDED(rc) && transferred > 0 && transferred <= max_len) {
        memcpy(buf, dev->rx_buf, transferred);
        *received = transferred;
    }
    mutexUnlock(&dev->mutex);
    return rc;
}