#include "usb_ch340.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Event g_interface_event;
static bool  g_usb_initialized = false;

Result ch340_init(void) {
    if (g_usb_initialized) return 0;
    Result rc = usbHsInitialize();
    if (R_FAILED(rc)) return rc;
    eventCreate(&g_interface_event, false);
    g_usb_initialized = true;
    return 0;
}

static Result cdc_control_xfer(Ch340Device *dev, u8 bmRequestType, u8 bRequest,
                                u16 wValue, u16 wIndex, void *data, u16 dataLen) {
    if (dataLen > 0x1000) return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (data && dataLen > 0) memcpy(dev->tx_buf, data, dataLen);
    return usbHsIfCtrlXfer(&dev->if_session, bmRequestType, bRequest,
                           wValue, wIndex, dataLen, dev->tx_buf);
}

static Result ch340_write_reg(Ch340Device *dev, u8 reg, u8 val) {
    dev->tx_buf[0] = reg;
    dev->tx_buf[1] = val;
    return usbHsIfCtrlXfer(&dev->if_session, 0x40, CH340_REQ_WRITE_REG,
                           val, reg | 0x80, 0, dev->tx_buf);
}

static Result ch340_set_baudrate(Ch340Device *dev, u32 baud) {
    u32 divisor = (u32)(CH340_BAUD_FACTOR / (u64)baud);
    u8 reg13_val = (divisor >> 8) & 0xFF;
    u8 reg12_val = divisor & 0xFF;
    Result rc;
    rc = ch340_write_reg(dev, 0x13, reg13_val);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x12, reg12_val);
    if (R_FAILED(rc)) return rc;
    u8 prescaler = 0;
    if (baud <= 1200) prescaler = 7;
    rc = ch340_write_reg(dev, 0x14, prescaler);
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
    rc = ch340_set_baudrate(dev, CH340_BAUDRATE);
    return rc;
}

Result ch340_connect(Ch340Device *dev) {
    if (!g_usb_initialized) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    memset(dev, 0, sizeof(Ch340Device));
    mutexInit(&dev->tx_mutex);
    mutexInit(&dev->rx_mutex);
    Result rc;
    u32 total, available;
    rc = usbHsQueryAvailableInterfaces(NULL, 0x00, &total);
    if (R_FAILED(rc) && rc != MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen))
        return rc;
    UsbHsInterface interfaces[8];
    rc = usbHsQueryAvailableInterfaces(interfaces, sizeof(interfaces), &available);
    if (R_FAILED(rc)) return rc;
    if (available == 0) return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    bool found = false;
    for (u32 i = 0; i < available && i < 8; i++) {
        UsbHsInterfaceInfo *info = &interfaces[i].inf.inf;
        u16 vid = info->device_desc.idVendor;
        u16 pid = info->device_desc.idProduct;
        if (vid == CH340_VID && pid == CH340_PID) {
            rc = usbHsAcquireUsbIf(&dev->if_session, &g_interface_event, true,
                                   0, &interfaces[i].inf);
            if (R_FAILED(rc)) continue;
            found = true;
            break;
        }
    }
    if (!found) return MAKERESULT(Module_Libnx, LibnxError_NotFound);
    rc = ch340_init_chip(dev);
    if (R_FAILED(rc)) goto fail;
    UsbHsInterface *inf = &dev->if_session.inf;
    UsbHsInterfaceDescriptor *desc = &inf->inf.inf.input_desc;
    u8 num_eps = desc->bNumEndpoints;
    dev->ep_in_addr  = 0;
    dev->ep_out_addr = 0;
    dev->max_packet_size = 64;
    for (u8 ep = 0; ep < num_eps && ep < 16; ep++) {
        u8 *ptr = (u8*)desc + sizeof(UsbHsInterfaceDescriptor);
        for (u8 j = 0; j < ep; j++) ptr += sizeof(UsbHsEndpointDescriptor);
        UsbHsEndpointDescriptor *ep_desc = (UsbHsEndpointDescriptor*)ptr;
        if (ep_desc->bDescriptorType != USB_DT_ENDPOINT) continue;
        u8 ep_addr = ep_desc->bEndpointAddress;
        bool is_in = (ep_addr & 0x80) != 0;
        if (is_in && dev->ep_in_addr == 0) {
            dev->ep_in_addr = ep_addr;
            dev->max_packet_size = ep_desc->wMaxPacketSize;
        } else if (!is_in && dev->ep_out_addr == 0) {
            dev->ep_out_addr = ep_addr;
        }
    }
    if (dev->ep_in_addr == 0 || dev->ep_out_addr == 0) {
        rc = MAKERESULT(Module_Libnx, LibnxError_NotFound);
        goto fail;
    }
    rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_out, 16, dev->max_packet_size, dev->ep_out_addr);
    if (R_FAILED(rc)) goto fail;
    rc = usbHsIfOpenUsbEp(&dev->if_session, &dev->ep_in, 16, dev->max_packet_size, dev->ep_in_addr);
    if (R_FAILED(rc)) { usbHsEpClose(&dev->ep_out); goto fail; }
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
    if (!dev->connected) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (len > 0x1000) len = 0x1000;
    mutexLock(&dev->tx_mutex);
    memcpy(dev->tx_buf, data, len);
    Result rc = usbHsEpPostBuffer(&dev->ep_out, dev->tx_buf, len);
    mutexUnlock(&dev->tx_mutex);
    return rc;
}

Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len, u64 timeout_ms, size_t *received) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, LibnxError_NotInitialized);
    if (max_len > 0x1000) max_len = 0x1000;
    *received = 0;
    Result rc = usbHsEpPostBufferAsync(&dev->ep_in, dev->rx_buf, max_len);
    if (R_FAILED(rc)) return rc;
    Event *xfer_event = usbHsEpGetXferEvent(&dev->ep_in);
    if (!xfer_event) return MAKERESULT(Module_Libnx, LibnxError_ShouldNotHappen);
    rc = eventWait(xfer_event, timeout_ms * 1000000ULL);
    if (R_FAILED(rc)) return rc;
    eventClear(xfer_event);
    UsbHsXferReport report;
    rc = usbHsEpGetXferReport(&dev->ep_in, &report);
    if (R_FAILED(rc)) return rc;
    if (report.transferredSize > 0 && report.transferredSize <= max_len) {
        mutexLock(&dev->rx_mutex);
        memcpy(buf, dev->rx_buf, report.transferredSize);
        mutexUnlock(&dev->rx_mutex);
        *received = report.transferredSize;
    }
    return 0;
}