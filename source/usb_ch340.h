#pragma once
#include <switch.h>
#include "config.h"

#define CH340_REQ_WRITE_REG   0x9A
#define CH340_REQ_SERIAL_INIT 0xA1
#define CH340_REQ_MODEM_CTRL  0xA4
#define CH340_BAUD_FACTOR     1532620800ULL

// Caller must allocate with aligned_alloc(0x1000, ...) for USB HS DMA
typedef struct {
    UsbHsClientIfSession if_session;
    UsbHsClientEpSession  ep_out;
    UsbHsClientEpSession  ep_in;
    bool connected;
    Mutex mutex;
    u8   tx_buf[CH340_BUF_SIZE] __attribute__((aligned(0x1000)));
    u8   rx_buf[CH340_BUF_SIZE] __attribute__((aligned(0x1000)));
} Ch340Device;

Result ch340_init(void);
// dev must be allocated with aligned_alloc(0x1000, ...) and mutexInit called
Result ch340_connect(Ch340Device *dev);
void   ch340_disconnect(Ch340Device *dev);
// Thread-safe. data must not be NULL, len <= CH340_BUF_SIZE
Result ch340_send(Ch340Device *dev, const void *data, size_t len);
// Thread-safe. buf and received must not be NULL
Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len,
                  u64 timeout_ms, size_t *received);
