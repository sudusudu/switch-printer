#include "usb_ch340.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Mutex g_init_mutex;
static bool  g_usb_ready = false;

Result ch340_init(void) {
    // 线程安全的懒初始化（外层 Mutex 保护 once 标志）
    static Mutex once_mutex;
    static bool once_inited = false;
    static bool once_done = false;
    // BSS 零值 Mutex 不可直接使用——先显式初始化
    if (!once_inited) { mutexInit(&once_mutex); once_inited = true; }
    mutexLock(&once_mutex);
    if (!once_done) { mutexInit(&g_init_mutex); once_done = true; }
    mutexUnlock(&once_mutex);

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

// P0: don't memset over mutex — caller must mutexInit before first connect
Result ch340_connect(Ch340Device *dev) {
    if (!g_usb_ready) return MAKERESULT(Module_Libnx, 1);

    dev->if_session = (UsbHsClientIfSession){0};
    dev->ep_out = (UsbHsClientEpSession){0};
    dev->ep_in = (UsbHsClientEpSession){0};
    dev->connected = false;
    memset(dev->tx_buf, 0, CH340_BUF_SIZE);
    memset(dev->rx_buf, 0, CH340_BUF_SIZE);

    s32 total = 0;
    Result rc = usbHsQueryAvailableInterfaces(NULL, NULL, 0, &total);
    if (R_FAILED(rc)) {
        LOG_E("usbHsQueryAvailableInterfaces(count) failed: 0x%x", rc);
        return rc;
    }
    LOG_I("USB interface count: %d", total);
    if (total == 0) {
        LOG_W("No USB interfaces available");
        return MAKERESULT(Module_Libnx, 4);
    }

    UsbHsInterface interfaces[8];
    memset(interfaces, 0, sizeof(interfaces));
    s32 found_count = 0;
    rc = usbHsQueryAvailableInterfaces(NULL, interfaces,
                                        (s32)(sizeof(interfaces) / sizeof(interfaces[0])),
                                        &found_count);
    if (R_FAILED(rc)) {
        LOG_E("usbHsQueryAvailableInterfaces(list) failed: 0x%x", rc);
        return rc;
    }
    LOG_I("USB interfaces returned: %d", found_count);

    bool found = false;
    s32 found_index = -1;
    s32 limit = (found_count < 8) ? found_count : 8;
    for (s32 i = 0; i < limit; i++) {
        LOG_I("USB[%d] VID=%04x PID=%04x", i,
              interfaces[i].device_desc.idVendor,
              interfaces[i].device_desc.idProduct);
        if (interfaces[i].device_desc.idVendor != CH340_VID ||
            interfaces[i].device_desc.idProduct != CH340_PID) continue;
        rc = usbHsAcquireUsbIf(&dev->if_session, &interfaces[i]);
        if (R_SUCCEEDED(rc)) { found = true; found_index = i; break; }
        LOG_E("usbHsAcquireUsbIf failed for CH340 candidate %d: 0x%x", i, rc);
    }
    if (!found) {
        LOG_W("CH340 VID=%04x PID=%04x not found", CH340_VID, CH340_PID);
        return MAKERESULT(Module_Libnx, 4);
    }
    LOG_I("CH340 interface acquired at index %d", found_index);

    rc = ch340_init_chip(dev);
    if (R_FAILED(rc)) {
        LOG_E("CH340 init chip failed: 0x%x", rc);
        goto fail;
    }

    // 使用 sizeof 而非硬编码 15 遍历端点描述符数组
    struct usb_endpoint_descriptor *out_desc = NULL;
    struct usb_endpoint_descriptor *in_desc  = NULL;
    int ep_max = (int)(sizeof(interfaces[0].inf.output_endpoint_descs) /
                       sizeof(interfaces[0].inf.output_endpoint_descs[0]));

    if (found_index >= 0) {
        for (int j = 0; j < ep_max; j++) {
            if (interfaces[found_index].inf.output_endpoint_descs[j].bEndpointAddress != 0 && !out_desc)
                out_desc = &interfaces[found_index].inf.output_endpoint_descs[j];
            if (interfaces[found_index].inf.input_endpoint_descs[j].bEndpointAddress != 0 && !in_desc)
                in_desc = &interfaces[found_index].inf.input_endpoint_descs[j];
        }
    }

    if (out_desc) {
        LOG_I("CH340 OUT endpoint: 0x%02x", out_desc->bEndpointAddress);
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_out, 8, CH340_BUF_SIZE, out_desc);
    } else {
        LOG_W("CH340 OUT endpoint descriptor missing; using fallback 0x02");
        struct usb_endpoint_descriptor fake_out = {0};
        fake_out.bLength = 7; fake_out.bDescriptorType = 5;
        fake_out.bEndpointAddress = 0x02; fake_out.bmAttributes = 2; fake_out.wMaxPacketSize = 64;
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_out, 8, CH340_BUF_SIZE, &fake_out);
    }
    if (R_FAILED(rc)) {
        LOG_E("Open CH340 OUT endpoint failed: 0x%x", rc);
        goto fail;
    }

    if (in_desc) {
        LOG_I("CH340 IN endpoint: 0x%02x", in_desc->bEndpointAddress);
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_in, 8, CH340_BUF_SIZE, in_desc);
    } else {
        LOG_W("CH340 IN endpoint descriptor missing; using fallback 0x82");
        struct usb_endpoint_descriptor fake_in = {0};
        fake_in.bLength = 7; fake_in.bDescriptorType = 5;
        fake_in.bEndpointAddress = 0x82; fake_in.bmAttributes = 2; fake_in.wMaxPacketSize = 64;
        rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_in, 8, CH340_BUF_SIZE, &fake_in);
    }
    if (R_FAILED(rc)) {
        LOG_E("Open CH340 IN endpoint failed: 0x%x", rc);
        usbHsEpClose(&dev->ep_out);
        goto fail;
    }

    // P0: set connected inside mutex (MOW-004, SEC-005 TOCTOU)
    mutexLock(&dev->mutex);
    dev->connected = true;
    mutexUnlock(&dev->mutex);
    LOG_I("CH340 connected");
    return 0;

fail:
    usbHsIfClose(&dev->if_session);
    return rc;
}

void ch340_disconnect(Ch340Device *dev) {
    mutexLock(&dev->mutex);
    if (!dev->connected) { mutexUnlock(&dev->mutex); return; }
    dev->connected = false;
    usbHsEpClose(&dev->ep_in);
    usbHsEpClose(&dev->ep_out);
    usbHsIfClose(&dev->if_session);
    mutexUnlock(&dev->mutex);
}

// P0: connected check inside mutex (MOW-004, SEC-005 TOCTOU)
Result ch340_send(Ch340Device *dev, const void *data, size_t len) {
    if (len > CH340_BUF_SIZE) len = CH340_BUF_SIZE;

    mutexLock(&dev->mutex);
    if (!dev->connected) { mutexUnlock(&dev->mutex); return MAKERESULT(Module_Libnx, 1); }
    memcpy(dev->tx_buf, data, len);
    u32 transferred = 0;
    Result rc = usbHsEpPostBuffer(&dev->ep_out, dev->tx_buf, len, &transferred);
    mutexUnlock(&dev->mutex);
    return rc;
}

Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len,
                  u64 timeout_ms, size_t *received) {
    if (!buf || !received) return MAKERESULT(Module_Libnx, 1);
    if (max_len > CH340_BUF_SIZE) max_len = CH340_BUF_SIZE;
    *received = 0;

    mutexLock(&dev->mutex);
    if (!dev->connected) { mutexUnlock(&dev->mutex); return MAKERESULT(Module_Libnx, 1); }
    u32 transferred = 0;
    Result rc = usbHsEpPostBuffer(&dev->ep_in, dev->rx_buf, max_len, &transferred);
    if (R_SUCCEEDED(rc) && transferred > 0 && transferred <= max_len) {
        memcpy(buf, dev->rx_buf, transferred);
        *received = transferred;
    }
    mutexUnlock(&dev->mutex);
    return rc;
}
