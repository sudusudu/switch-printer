#include "usb_ch340.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Event g_interface_event;
static bool  g_usb_initialized = false;

// ============================================================
// USB 子系统初始化
// ============================================================
Result ch340_init(void) {
    if (g_usb_initialized) return 0;
    Result rc = usbHsInitialize();
    if (R_FAILED(rc)) return rc;
    eventCreate(&g_interface_event, false);
    g_usb_initialized = true;
    return 0;
}

// ============================================================
// 发送 CDC 控制请求
// ============================================================
static Result cdc_control_xfer(Ch340Device *dev, u8 bmRequestType, u8 bRequest,
                                u16 wValue, u16 wIndex, void *data, u16 dataLen) {
    // 借用 tx_buf 做控制传输（对齐已保证）
    if (dataLen > 0x1000) return MAKERESULT(Module_Libnx, 2);
    if (data && dataLen > 0) memcpy(dev->tx_buf, data, dataLen);

    return usbHsIfCtrlXfer(&dev->if_session, bmRequestType, bRequest,
                           wValue, wIndex, dataLen, dev->tx_buf);
}

// ============================================================
// CH340 专用：写寄存器
// ============================================================
static Result ch340_write_reg(Ch340Device *dev, u8 reg, u8 val) {
    dev->tx_buf[0] = reg;
    dev->tx_buf[1] = val;
    return usbHsIfCtrlXfer(&dev->if_session,
        0x40,                    // bmRequestType: Host-to-Device, Vendor
        CH340_REQ_WRITE_REG,     // bRequest
        val,                     // wValue
        reg | 0x80,              // wIndex
        0,                       // data length (already in setup packet)
        dev->tx_buf);
}

// ============================================================
// CH340 专用：设置波特率
// ============================================================
static Result ch340_set_baudrate(Ch340Device *dev, u32 baud) {
    // CH340 波特率计算：divisor = 12MHz * 32 / baud
    // 实际使用 CH340_BAUD_FACTOR
    u32 divisor = (u32)(CH340_BAUD_FACTOR / (u64)baud);

    // 写波特率分频器（两个寄存器：0x13=低字节, 0x12=高字节）
    u8 reg13_val = (divisor >> 8) & 0xFF;
    u8 reg12_val = divisor & 0xFF;

    Result rc;
    rc = ch340_write_reg(dev, 0x13, reg13_val);
    if (R_FAILED(rc)) return rc;
    rc = ch340_write_reg(dev, 0x12, reg12_val);
    if (R_FAILED(rc)) return rc;

    // CH340 通常还有预分频器
    u8 prescaler = 0;
    if (baud <= 1200) prescaler = 7;
    // 其他波特率通常用 0（不分频）或根据实际测试调整
    rc = ch340_write_reg(dev, 0x14, prescaler);
    return rc;
}

// ============================================================
// CH340 专用：初始化芯片握手和模式
// ============================================================
static Result ch340_init_chip(Ch340Device *dev) {
    Result rc;

    // 复位 CH340
    rc = ch340_write_reg(dev, 0x06, 0x00);
    if (R_FAILED(rc)) return rc;

    // 设置握手信号：DTR=1, RTS=1
    rc = ch340_write_reg(dev, 0x1A, 0x00);
    if (R_FAILED(rc)) return rc;

    // 设置串口模式：8N1
    rc = ch340_write_reg(dev, 0x18, 0x03);
    if (R_FAILED(rc)) return rc;

    // 使能 RX/TX
    rc = ch340_write_reg(dev, 0x1C, 0xFF);
    if (R_FAILED(rc)) return rc;

    // 设置波特率
    rc = ch340_set_baudrate(dev, CH340_BAUDRATE);
    return rc;
}

// ============================================================
// 扫描并连接 CH340 打印机
// ============================================================
Result ch340_connect(Ch340Device *dev) {
    if (!g_usb_initialized) return MAKERESULT(Module_Libnx, 1);

    memset(dev, 0, sizeof(Ch340Device));
    mutexInit(&dev->tx_mutex);
    mutexInit(&dev->rx_mutex);

    Result rc;
    u32 total, available;

    // 先列出所有可用接口
    rc = usbHsQueryAvailableInterfaces(NULL, 0x00, &total);
    if (R_FAILED(rc) && rc != MAKERESULT(Module_Libnx, 3))
        return rc;

    // 查询实际设备
    UsbHsInterface interfaces[8];
    rc = usbHsQueryAvailableInterfaces(interfaces, sizeof(interfaces), &available);
    if (R_FAILED(rc)) return rc;
    if (available == 0) return MAKERESULT(Module_Libnx, 4);

    // 遍历找 CH340
    bool found = false;
    for (u32 i = 0; i < available && i < 8; i++) {
        UsbHsInterfaceInfo *info = &interfaces[i].inf.inf;
        u16 vid = info->device_desc.idVendor;
        u16 pid = info->device_desc.idProduct;

        if (vid == CH340_VID && pid == CH340_PID) {
            // 找到 CH340！获取接口
            rc = usbHsAcquireUsbIf(&dev->if_session, &g_interface_event, true,
                                   0, &interfaces[i].inf);
            if (R_FAILED(rc)) continue;
            found = true;
            break;
        }
    }

    if (!found) return MAKERESULT(Module_Libnx, 4);

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
        rc = MAKERESULT(Module_Libnx, 4);
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
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    if (len > 0x1000) len = 0x1000;
    mutexLock(&dev->tx_mutex);
    memcpy(dev->tx_buf, data, len);
    Result rc = usbHsEpPostBuffer(&dev->ep_out, dev->tx_buf, len);
    mutexUnlock(&dev->tx_mutex);
    return rc;
}

Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len, u64 timeout_ms, size_t *received) {
    if (!dev->connected) return MAKERESULT(Module_Libnx, 1);
    if (max_len > 0x1000) max_len = 0x1000;
    *received = 0;
    Result rc = usbHsEpPostBufferAsync(&dev->ep_in, dev->rx_buf, max_len);
    if (R_FAILED(rc)) return rc;
    Event *xfer_event = usbHsEpGetXferEvent(&dev->ep_in);
    if (!xfer_event) return MAKERESULT(Module_Libnx, 3);
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