#pragma once
#include <switch.h>
#include "config.h"

#define CDC_REQ_SET_LINE_CODING        0x20
#define CDC_REQ_GET_LINE_CODING        0x21
#define CDC_REQ_SET_CONTROL_LINE_STATE 0x22

#define CH340_REQ_READ_VERSION  0x5F
#define CH340_REQ_WRITE_REG     0x9A
#define CH340_REQ_READ_REG      0x95
#define CH340_REQ_SERIAL_INIT   0xA1
#define CH340_REQ_MODEM_CTRL    0xA4

#define CH340_BAUD_FACTOR  1532620800ULL

typedef struct {
    UsbHsClientIfSession if_session;
    UsbHsClientEpSession  ep_out;
    UsbHsClientEpSession  ep_in;
    u8  ep_out_addr;
    u8  ep_in_addr;
    u16 max_packet_size;
    bool connected;
    Mutex tx_mutex;
    Mutex rx_mutex;
    u8  tx_buf[0x1000] __attribute__((aligned(0x1000)));
    u8  rx_buf[0x1000] __attribute__((aligned(0x1000)));
} Ch340Device;

Result ch340_init(void);
Result ch340_connect(Ch340Device *dev);
void ch340_disconnect(Ch340Device *dev);
Result ch340_send(Ch340Device *dev, const void *data, size_t len);
Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len, u64 timeout_ms, size_t *received);