#pragma once
#include <switch.h>
#include "config.h"

// CH340 寄存器命令
#define CH340_REQ_WRITE_REG   0x9A
#define CH340_REQ_SERIAL_INIT 0xA1
#define CH340_REQ_MODEM_CTRL  0xA4

// CH340_BAUD_FACTOR, CH340_BUF_SIZE 定义在 config.h

// ============================================================
// Caller must allocate with aligned_alloc(0x1000, ...) for USB HS DMA
// and call mutexInit(&dev->mutex) before first connect
// ============================================================
typedef struct {
    UsbHsClientIfSession if_session;
    UsbHsClientEpSession  ep_out;
    UsbHsClientEpSession  ep_in;
    bool connected;
    Mutex mutex;
    u8   tx_buf[CH340_BUF_SIZE] __attribute__((aligned(0x1000)));
    u8   rx_buf[CH340_BUF_SIZE] __attribute__((aligned(0x1000)));
} Ch340Device;

// 初始化 USB 子系统（幂等）
Result ch340_init(void);
// dev: aligned_alloc 分配且 mutexInit 已调用
Result ch340_connect(Ch340Device *dev);
void   ch340_disconnect(Ch340Device *dev);
// 线程安全。data 非 NULL，len ≤ CH340_BUF_SIZE 时截断
Result ch340_send(Ch340Device *dev, const void *data, size_t len);
// 线程安全。buf 和 received 必须非 NULL。timeout_ms 单位毫秒，0=立即返回
Result ch340_recv(Ch340Device *dev, void *buf, size_t max_len,
                  u64 timeout_ms, size_t *received);
