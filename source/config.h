#pragma once

// ============================================================
// Switch Printer v2.0 — 集中配置
// Switch 硬件 + Atmosphere CFW 深度集成
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

// --- USB 断连恢复 ---
#define USB_RECONNECT_TIMEOUT_SEC 30      // USB 断开后等待重连秒数
#define USB_RECONNECT_POLL_MS    500      // 重连轮询间隔 (ms)
#define RESUME_FILE_PATH  "sdmc:/switch/gcode/resume.txt"  // 断点保存路径

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

// --- 多核分配 (Switch 4×Cortex-A57) ---
#define CORE_UI     0                     // 主线程: UI渲染 + 输入处理
#define CORE_PRINT  1                     // 打印线程: G-code发送
#define CORE_HTTP   2                     // HTTP服务线程

// --- GPU UI (deko3d) ---
#define GPU_TARGET_FPS         60         // 目标帧率
#define GPU_FONT_SIZE          18         // 字体像素大小
#define GPU_TOUCH_DEAD_ZONE    4          // 触摸死区 (像素, 防抖动)

// --- 电源管理 ---
#define UI_DIM_TIMEOUT_SEC     60         // 无操作后降亮度等待秒数 (0=禁用)
#define UI_DIM_BRIGHTNESS      0.15f      // 降亮度后的亮度比例 (0.0~1.0)
#define UI_BURNIN_SHIFT_PX     1          // 防烧屏像素微移量
#define UI_BURNIN_INTERVAL_SEC 30         // 防烧屏微移间隔 (秒)

// --- 日志系统 ---
#define LOG_FILE_PATH    "sdmc:/switch/switch_printer.log"
#define LOG_MAX_SIZE     (128 * 1024)     // 日志文件最大字节数 (128KB, 环形截断)
#define LOG_FLUSH_EVERY  8               // 每 N 条日志刷新到 SD 卡

// --- 崩溃处理 ---
#define CRASH_DUMP_PATH  "sdmc:/switch/crash_dump.txt"
// Atmosphere fatal 重定向: 注册后崩溃自动写寄存器+backtrace 到 CRASH_DUMP_PATH

// --- Console UI (legacy, Phase 2 后由 GPU UI 替代) ---
#define CONSOLE_WIDTH          78         // 控制台字符宽度
#define UI_REFRESH_NS          100000000ULL  // UI 刷新间隔 100ms (纳秒)
#define UI_SCAN_INTERVAL_NS    1000000000ULL // 扫描重试间隔 1s (纳秒)
#define USB_SCAN_RETRY_MAX     30         // USB 扫描最大重试次数
