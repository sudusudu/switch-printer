#pragma once

// ============================================================
// Switch Printer — 集中配置
// 所有单位、范围、默认值均在此定义
// ============================================================

// --- HTTP Server ---
#define HTTP_PORT             8080
#define HTTP_REQ_BUF_SIZE     16384       // HTTP 请求缓冲区 (字节)
#define HTTP_UPLOAD_MAX_MB    100         // 最大上传文件大小 (MB)

// --- USB CH340 ---
#define CH340_VID         0x1A86
#define CH340_PID         0x7523
#define CH340_BAUDRATE    250000          // 波特率 (bps)
// CH340 基频因子 (约 11.97MHz PLL × 128，非精确 12MHz)
#define CH340_BAUD_FACTOR 1532620800ULL
#define CH340_BUF_SIZE    0x1000          // USB 收发缓冲区大小 (4KB, DMA 对齐)

// --- G-code ---
#define GCODE_LINE_MAX           256      // 单行 G-code 最大长度 (含 '\0')
#define GCODE_OK_TIMEOUT         30000    // 等待 'ok' 响应的超时 (ms)
#define GCODE_EST_BYTES_PER_LINE 50       // 上传时估算行数的每行字节数 (粗略)

// --- JOG ---
#define JOG_FEEDRATE  1000               // Web UI 点动移动默认进给速率 (mm/min)

// --- Printer Bed ---
#define PRINTER_BED_X  400               // 床面 X 轴范围 (mm)
#define PRINTER_BED_Y  400               // 床面 Y 轴范围 (mm)
#define PRINTER_BED_Z  450               // 床面 Z 轴范围 (mm)

// --- Temperature ---
#define PRINTER_NOZZLE_MAX_TEMP   300     // 喷嘴最高温度 (°C)
#define PRINTER_BED_MAX_TEMP      120     // 热床最高温度 (°C)
#define TEMP_THRESHOLD_COLD       40      // 温度显示: 冷 (<40°C=蓝色)
#define TEMP_THRESHOLD_WARM       120     // 温度显示: 温 (40-120°C=绿色)
#define TEMP_THRESHOLD_HOT        190     // 温度显示: 热 (120-190°C=黄色, ≥190°C=红色)

// --- Thread 配置 ---
#define PRINT_THREAD_STACK_SIZE   0x20000 // 打印线程栈大小 (128KB)
#define PRINT_THREAD_PRIORITY     0x30    // 打印线程优先级 (48)
#define HTTPD_THREAD_STACK_SIZE   0x20000 // HTTP 服务器线程栈大小 (128KB)
#define HTTPD_THREAD_PRIORITY     0x2B    // HTTP 服务器线程优先级 (43)
#define THREAD_DEFAULT_CORE       -1      // 默认 CPU 核心 (由系统分配)

// --- Console UI ---
#define CONSOLE_WIDTH          78         // 控制台字符宽度
#define UI_REFRESH_NS          100000000ULL  // UI 刷新间隔 100ms (纳秒)
#define UI_SCAN_INTERVAL_NS    1000000000ULL // 扫描重试间隔 1s (纳秒)
#define USB_SCAN_RETRY_MAX     30         // USB 扫描最大重试次数
